#include "algorithm/bit_parallel_simulator.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace algorithm {

namespace {

uint64_t andReduce(const std::vector<uint64_t>& values, const std::vector<std::size_t>& indices,
                   uint64_t mask) {
    uint64_t result = mask;
    for (auto idx : indices) {
        result &= values[idx];
    }
    return result;
}

uint64_t orReduce(const std::vector<uint64_t>& values, const std::vector<std::size_t>& indices,
                  uint64_t mask) {
    uint64_t result = 0;
    for (auto idx : indices) {
        result |= values[idx];
    }
    return result & mask;
}

uint64_t xorReduce(const std::vector<uint64_t>& values, const std::vector<std::size_t>& indices,
                   uint64_t mask) {
    uint64_t result = 0;
    for (auto idx : indices) {
        result ^= values[idx];
    }
    return result & mask;
}

}  // namespace

BitParallelSimulator::BitParallelSimulator(const core::Circuit& circuit,
                                           const std::vector<io::PatternRow>& rows)
    : FaultSimulator(circuit, rows), circuit_(circuit) {
    output_indices_ = circuit_.primaryOutputs();
}

std::vector<FaultEvaluation> BitParallelSimulator::evaluate(const core::Pattern& pattern) const {
    if (net_names_.empty()) {
        return {};
    }
    const std::size_t total_faults = net_names_.size() * 2;
    std::vector<FaultEvaluation> evaluations(net_names_.size());
    std::size_t processed = 0;
    while (processed < total_faults) {
        const std::size_t remaining = total_faults - processed;
        const std::size_t chunk_faults = std::min<std::size_t>(63, remaining);
        std::vector<ChunkFault> chunk;
        chunk.reserve(chunk_faults);
        for (std::size_t i = 0; i < chunk_faults; ++i) {
            const std::size_t fault_index = processed + i;
            const std::size_t net_index = fault_index / 2;
            const int stuck_value = (fault_index % 2 == 0) ? 0 : 1;
            chunk.push_back({net_index, stuck_value});
        }
        auto chunk_results = simulateChunk(pattern, chunk);
        for (std::size_t i = 0; i < chunk.size(); ++i) {
            const auto& fault = chunk[i];
            if (fault.stuck_value == 0) {
                evaluations[fault.net_index].stuck0_eq = chunk_results[i];
            } else {
                evaluations[fault.net_index].stuck1_eq = chunk_results[i];
            }
        }
        processed += chunk_faults;
    }
    return evaluations;
}

std::vector<bool> BitParallelSimulator::simulateChunk(
    const core::Pattern& pattern, const std::vector<ChunkFault>& chunk) const {
    if (chunk.empty()) {
        return {};
    }

    const std::size_t net_count = net_names_.size();
    const std::size_t chunk_bits = chunk.size() + 1;  // include golden context
    const uint64_t mask =
        (chunk_bits >= 64) ? std::numeric_limits<uint64_t>::max()
                           : ((uint64_t{1} << chunk_bits) - 1);
    const uint64_t mask_without_base = mask & ~uint64_t{1};

    std::vector<uint64_t> values(net_count, 0);
    std::vector<uint64_t> force_zero(net_count, 0);
    std::vector<uint64_t> force_one(net_count, 0);

    auto applyForcing = [&](std::size_t index) {
        if (force_zero[index]) {
            values[index] &= ~force_zero[index];
        }
        if (force_one[index]) {
            values[index] |= force_one[index];
        }
        values[index] &= mask;
    };

    for (std::size_t i = 0; i < chunk.size(); ++i) {
        const uint64_t bit = uint64_t{1} << (i + 1);
        const auto& fault = chunk[i];
        if (fault.stuck_value == 0) {
            force_zero[fault.net_index] |= bit;
        } else {
            force_one[fault.net_index] |= bit;
        }
    }

    for (const auto& entry : pattern.assignments) {
        const std::size_t idx = entry.net;
        if (idx >= net_count) {
            throw std::runtime_error("Pattern references unknown net");
        }
        values[idx] = entry.value ? mask : 0;
        applyForcing(idx);
    }

    for (const auto& gate : circuit_.gates()) {
        const auto& input_indices = gate.inputs;
        uint64_t result = 0;
        switch (gate.type) {
            case core::GateType::And:
                result = andReduce(values, input_indices, mask);
                break;
            case core::GateType::Nand:
                result = (~andReduce(values, input_indices, mask)) & mask;
                break;
            case core::GateType::Or:
                result = orReduce(values, input_indices, mask);
                break;
            case core::GateType::Nor:
                result = (~orReduce(values, input_indices, mask)) & mask;
                break;
            case core::GateType::Xor:
                result = xorReduce(values, input_indices, mask);
                break;
            case core::GateType::Xnor:
                result = (~xorReduce(values, input_indices, mask)) & mask;
                break;
            case core::GateType::Not:
                if (input_indices.size() != 1) {
                    throw std::runtime_error("NOT gate expects exactly one input");
                }
                result = (~values[input_indices.front()]) & mask;
                break;
            case core::GateType::Buf:
                if (input_indices.size() != 1) {
                    throw std::runtime_error("BUF gate expects exactly one input");
                }
                result = values[input_indices.front()] & mask;
                break;
            case core::GateType::Unknown:
            default:
                throw std::runtime_error("Unknown gate type encountered during simulation");
        }
        const std::size_t out_idx = gate.output;
        values[out_idx] = result;
        applyForcing(out_idx);
    }

    uint64_t eq_mask = mask_without_base;
    for (auto idx : output_indices_) {
        uint64_t bits = values[idx];
        const bool golden_bit = (bits & uint64_t{1}) != 0;
        const uint64_t golden_mask = golden_bit ? mask : uint64_t{0};
        const uint64_t diff = (bits ^ golden_mask) & mask;
        const uint64_t same = (~diff) & mask;
        eq_mask &= same;
    }

    std::vector<bool> chunk_results(chunk.size(), false);
    for (std::size_t i = 0; i < chunk.size(); ++i) {
        const std::size_t bit = i + 1;
        chunk_results[i] = ((eq_mask >> bit) & uint64_t{1}) != 0;
    }
    return chunk_results;
}

void BitParallelSimulator::start() {
    for (std::size_t i = 0; i < rows_.size(); ++i) {
        const auto evaluations = evaluate(rows_[i].pattern);
        if (evaluations.size() != net_names_.size()) {
            throw std::runtime_error("Evaluation result size mismatch");
        }
        for (std::size_t net_id = 0; net_id < evaluations.size(); ++net_id) {
            answers.set(i, net_id, true, evaluations[net_id].stuck0_eq);
            answers.set(i, net_id, false, evaluations[net_id].stuck1_eq);
        }
    }
}

}  // namespace algorithm
