#include "algorithm/batch1_mt_fault.hpp"

#include <limits>
#include <stdexcept>

#ifdef _OPENMP
#include <omp.h>
#endif

namespace algorithm {

namespace {

int evaluateGate(core::GateType type, const std::vector<int>& inputs) {
    if (inputs.empty()) {
        throw std::runtime_error("Gate missing inputs during DFS simulation");
    }
    auto logical_not = [](int value) { return value ? 0 : 1; };
    auto logical_and = [&]() {
        int result = 1;
        for (int value : inputs) {
            result &= value;
        }
        return result;
    };
    auto logical_or = [&]() {
        int result = 0;
        for (int value : inputs) {
            result |= value;
        }
        return result;
    };
    auto logical_xor = [&]() {
        int result = 0;
        for (int value : inputs) {
            result ^= value;
        }
        return result;
    };

    switch (type) {
        case core::GateType::And:
            return logical_and();
        case core::GateType::Nand:
            return logical_not(logical_and());
        case core::GateType::Or:
            return logical_or();
        case core::GateType::Nor:
            return logical_not(logical_or());
        case core::GateType::Xor:
            return logical_xor();
        case core::GateType::Xnor:
            return logical_not(logical_xor());
        case core::GateType::Not:
            if (inputs.size() != 1) {
                throw std::runtime_error("NOT gate expects exactly one input");
            }
            return logical_not(inputs.front());
        case core::GateType::Buf:
            if (inputs.size() != 1) {
                throw std::runtime_error("BUF gate expects exactly one input");
            }
            return inputs.front();
        case core::GateType::Unknown:
        default:
            throw std::runtime_error("Unknown gate type during DFS simulation");
    }
}

int dfs(core::NetId target,
        core::NetId fault_wire,
        bool stuck_at_0,
        const core::Circuit& circuit,
        const std::vector<int>& net_to_gate,
        std::vector<bool>& visited,
        std::vector<int>& values) {
    if (target == fault_wire) {
        visited[target] = true;
        values[target] = stuck_at_0 ? 0 : 1;
        return values[target];
    }
    if (visited[target]) {
        return values[target];
    }

    const auto net_type = circuit.netType(target);
    if (net_type == core::NetType::PrimaryInput) {
        if (values[target] == -1) {
            throw std::runtime_error("Missing assignment for primary input");
        }
        visited[target] = true;
        return values[target];
    }

    const int gate_index = net_to_gate[target];
    if (gate_index < 0) {
        throw std::runtime_error("Unable to locate driving gate for net");
    }
    const auto& gate = circuit.gates()[static_cast<std::size_t>(gate_index)];
    std::vector<int> input_values;
    input_values.reserve(gate.inputs.size());
    for (auto input_net : gate.inputs) {
        input_values.push_back(
            dfs(input_net, fault_wire, stuck_at_0, circuit, net_to_gate, visited, values));
    }

    const int result = evaluateGate(gate.type, input_values);
    visited[target] = true;
    values[target] = result;
    return result;
}

std::vector<int> computeReferenceOutputs(const core::Circuit& circuit,
                                         const std::vector<int>& net_to_gate,
                                         const io::PatternRow& row) {
    const auto& outputs = circuit.primaryOutputs();
    std::vector<int> refs(outputs.size(), 0);
    bool all_provided = (row.provided_outputs.size() == outputs.size());

    if (all_provided) {
        for (std::size_t i = 0; i < outputs.size(); ++i) {
            refs[i] = row.provided_outputs.at(outputs[i]);
        }
        return refs;
    }

    const core::NetId invalid_net = std::numeric_limits<core::NetId>::max();
    std::vector<bool> visited(circuit.netCount(), false);
    std::vector<int> values(circuit.netCount(), -1);
    for (const auto& entry : row.pattern.assignments) {
        visited[entry.net] = true;
        values[entry.net] = entry.value;
    }
    for (std::size_t i = 0; i < outputs.size(); ++i) {
        refs[i] = dfs(outputs[i], invalid_net, false, circuit, net_to_gate, visited, values);
    }
    return refs;
}

}  // namespace

Batch1MtFaultSimulator::Batch1MtFaultSimulator(const core::Circuit& circuit,
                                               const std::vector<io::PatternRow>& rows,
                                               int num_threads)
    : FaultSimulator(circuit, rows), num_threads_(num_threads) {
    net_to_gate_.assign(circuit_.netCount(), -1);
    const auto& gates = circuit_.gates();
    for (std::size_t i = 0; i < gates.size(); ++i) {
        net_to_gate_[gates[i].output] = static_cast<int>(i);
    }
}

void Batch1MtFaultSimulator::start() {
#ifdef _OPENMP
    if (num_threads_ > 0) {
        omp_set_num_threads(num_threads_);
    }
#endif
    const std::size_t net_count = circuit_.netCount();
    for (std::size_t pattern_id = 0; pattern_id < rows_.size(); ++pattern_id) {
        const auto reference_outputs =
            computeReferenceOutputs(circuit_, net_to_gate_, rows_[pattern_id]);
        const auto& outputs = circuit_.primaryOutputs();
        std::vector<FaultEvaluation> evals(net_count);

#pragma omp parallel for schedule(static)
        for (long long net = 0; net < static_cast<long long>(net_count); ++net) {
            auto simulateOutputs = [&](bool stuck_at_0) {
                std::vector<bool> visited(circuit_.netCount(), false);
                std::vector<int> values(circuit_.netCount(), -1);
                for (const auto& entry : rows_[pattern_id].pattern.assignments) {
                    int v = entry.value;
                    if (entry.net == static_cast<core::NetId>(net)) {
                        v = stuck_at_0 ? 0 : 1;
                    }
                    visited[entry.net] = true;
                    values[entry.net] = v;
                }
                if (static_cast<core::NetId>(net) < circuit_.netCount() &&
                    values[static_cast<std::size_t>(net)] == -1) {
                    values[static_cast<std::size_t>(net)] = stuck_at_0 ? 0 : 1;
                    visited[static_cast<std::size_t>(net)] = true;
                }

                std::vector<int> outs(outputs.size(), 0);
                for (std::size_t i = 0; i < outputs.size(); ++i) {
                    outs[i] = dfs(outputs[i], static_cast<core::NetId>(net), stuck_at_0, circuit_,
                                  net_to_gate_, visited, values);
                }
                return outs;
            };

            const auto outs0 = simulateOutputs(true);
            const auto outs1 = simulateOutputs(false);

            bool eq0 = true;
            bool eq1 = true;
            for (std::size_t i = 0; i < outputs.size(); ++i) {
                eq0 &= (outs0[i] == reference_outputs[i]);
                eq1 &= (outs1[i] == reference_outputs[i]);
            }
            evals[static_cast<std::size_t>(net)].stuck0_eq = eq0;
            evals[static_cast<std::size_t>(net)].stuck1_eq = eq1;
        }

        for (std::size_t net = 0; net < net_count; ++net) {
            answers.set(pattern_id, net, true, evals[net].stuck0_eq);
            answers.set(pattern_id, net, false, evals[net].stuck1_eq);
        }
    }
}

}  // namespace algorithm
