#include "algorithm/batch64_mt_fault.hpp"

#include <cstdint>
#include <limits>
#include <stdexcept>

#ifdef _OPENMP
#include <omp.h>
#endif

namespace algorithm {

namespace {

uint64_t evaluateGateBits(core::GateType type, const std::vector<uint64_t>& inputs, uint64_t mask) {
    if (inputs.empty()) {
        throw std::runtime_error("Gate missing inputs during 64-bit DFS simulation");
    }
    switch (type) {
        case core::GateType::And: {
            uint64_t v = mask;
            for (auto in : inputs) {
                v &= in;
            }
            return v & mask;
        }
        case core::GateType::Nand: {
            uint64_t v = mask;
            for (auto in : inputs) {
                v &= in;
            }
            return (~v) & mask;
        }
        case core::GateType::Or: {
            uint64_t v = 0;
            for (auto in : inputs) {
                v |= in;
            }
            return v & mask;
        }
        case core::GateType::Nor: {
            uint64_t v = 0;
            for (auto in : inputs) {
                v |= in;
            }
            return (~v) & mask;
        }
        case core::GateType::Xor: {
            uint64_t v = 0;
            for (auto in : inputs) {
                v ^= in;
            }
            return v & mask;
        }
        case core::GateType::Xnor: {
            uint64_t v = 0;
            for (auto in : inputs) {
                v ^= in;
            }
            return (~v) & mask;
        }
        case core::GateType::Not:
            if (inputs.size() != 1) {
                throw std::runtime_error("NOT gate expects exactly one input");
            }
            return (~inputs.front()) & mask;
        case core::GateType::Buf:
            if (inputs.size() != 1) {
                throw std::runtime_error("BUF gate expects exactly one input");
            }
            return inputs.front() & mask;
        case core::GateType::Unknown:
        default:
            throw std::runtime_error("Unknown gate type during 64-bit DFS simulation");
    }
}

uint64_t dfs(core::NetId target,
             core::NetId fault_wire,
             bool stuck_at_0,
             uint64_t mask,
             const core::Circuit& circuit,
             const std::vector<int>& net_to_gate,
             std::vector<bool>& visited,
             std::vector<uint64_t>& values) {
    if (target == fault_wire) {
        visited[target] = true;
        values[target] = stuck_at_0 ? uint64_t{0} : mask;
        return values[target];
    }
    if (visited[target]) {
        return values[target];
    }

    const auto net_type = circuit.netType(target);
    if (net_type == core::NetType::PrimaryInput) {
        visited[target] = true;
        return values[target];
    }

    const int gate_index = net_to_gate[target];
    if (gate_index < 0) {
        throw std::runtime_error("Unable to locate driving gate for net during 64-bit DFS");
    }

    const auto& gate = circuit.gates()[static_cast<std::size_t>(gate_index)];
    std::vector<uint64_t> input_values;
    input_values.reserve(gate.inputs.size());
    for (auto input_net : gate.inputs) {
        input_values.push_back(
            dfs(input_net, fault_wire, stuck_at_0, mask, circuit, net_to_gate, visited, values));
    }
    uint64_t result = evaluateGateBits(gate.type, input_values, mask);
    visited[target] = true;
    values[target] = result;
    return result;
}

}  // namespace

Batch64MtFaultSimulator::Batch64MtFaultSimulator(const core::Circuit& circuit,
                                                 const std::vector<io::PatternRow>& rows,
                                                 int num_threads)
    : FaultSimulator(circuit, rows), num_threads_(num_threads) {
    net_to_gate_.assign(circuit_.netCount(), -1);
    for (std::size_t i = 0; i < circuit_.gates().size(); ++i) {
        net_to_gate_[circuit_.gates()[i].output] = static_cast<int>(i);
    }

    output_index_by_net_.assign(circuit_.netCount(), -1);
    const auto& outs = circuit_.primaryOutputs();
    for (std::size_t i = 0; i < outs.size(); ++i) {
        output_index_by_net_[outs[i]] = static_cast<int>(i);
    }
}

void Batch64MtFaultSimulator::start() {
#ifdef _OPENMP
    if (num_threads_ > 0) {
        omp_set_num_threads(num_threads_);
    }
#endif

    const auto& outputs = circuit_.primaryOutputs();
    const std::size_t net_count = circuit_.netCount();

    for (std::size_t base = 0; base < rows_.size(); base += 64) {
        const std::size_t chunk_size = std::min<std::size_t>(64, rows_.size() - base);
        const uint64_t mask =
            (chunk_size == 64) ? std::numeric_limits<uint64_t>::max()
                               : ((uint64_t{1} << chunk_size) - 1);

        std::vector<uint64_t> base_values(net_count, 0);
        std::vector<bool> base_visited(net_count, false);

        for (std::size_t offset = 0; offset < chunk_size; ++offset) {
            const uint64_t bit = uint64_t{1} << offset;
            const auto& assignments = rows_[base + offset].pattern.assignments;
            for (const auto& entry : assignments) {
                if (entry.value) {
                    base_values[entry.net] |= bit;
                }
                base_visited[entry.net] = true;
            }
        }

        std::vector<uint64_t> provided_value(outputs.size(), 0);
        for (std::size_t offset = 0; offset < chunk_size; ++offset) {
            const uint64_t bit = uint64_t{1} << offset;
            const auto& provided = rows_[base + offset].provided_outputs;
            for (const auto& kv : provided) {
                const auto idx = output_index_by_net_[kv.first];
                if (idx < 0) {
                    continue;
                }
                if (kv.second) {
                    provided_value[static_cast<std::size_t>(idx)] |= bit;
                }
            }
        }

        struct FaultResultBits {
            uint64_t stuck0{0};
            uint64_t stuck1{0};
        };
        std::vector<FaultResultBits> fault_bits(net_count);

#pragma omp parallel for schedule(static)
        for (long long net = 0; net < static_cast<long long>(net_count); ++net) {
            auto computeOutputs = [&](bool stuck_at_0) {
                std::vector<uint64_t> values = base_values;
                std::vector<bool> visited = base_visited;
                if (static_cast<std::size_t>(net) < values.size()) {
                    values[static_cast<std::size_t>(net)] = stuck_at_0 ? uint64_t{0} : mask;
                    visited[static_cast<std::size_t>(net)] = true;
                }
                std::vector<uint64_t> outs(outputs.size(), 0);
                for (std::size_t i = 0; i < outputs.size(); ++i) {
                    outs[i] = dfs(outputs[i], static_cast<core::NetId>(net), stuck_at_0, mask,
                                  circuit_, net_to_gate_, visited, values);
                }
                return outs;
            };

            const auto outs0 = computeOutputs(true);
            const auto outs1 = computeOutputs(false);

            uint64_t eq0 = mask;
            uint64_t eq1 = mask;
            for (std::size_t i = 0; i < outputs.size(); ++i) {
                eq0 &= ~(outs0[i] ^ provided_value[i]) & mask;
                eq1 &= ~(outs1[i] ^ provided_value[i]) & mask;
            }
            fault_bits[static_cast<std::size_t>(net)].stuck0 = eq0;
            fault_bits[static_cast<std::size_t>(net)].stuck1 = eq1;
        }

        for (std::size_t net = 0; net < net_count; ++net) {
            const auto bits = fault_bits[net];
            for (std::size_t offset = 0; offset < chunk_size; ++offset) {
                const bool eq0 = ((bits.stuck0 >> offset) & uint64_t{1}) != 0;
                const bool eq1 = ((bits.stuck1 >> offset) & uint64_t{1}) != 0;
                answers.set(base + offset, net, true, eq0);
                answers.set(base + offset, net, false, eq1);
            }
        }
    }
}

}  // namespace algorithm
