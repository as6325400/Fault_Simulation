//#ifdef LEVELIZEDMPI

#include "algorithm/levelized_mpi.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <utility>

namespace algorithm {

LevelizedMPI::LevelizedMPI(const core::Circuit& circuit,
                           const std::vector<io::PatternRow>& rows,
                           MPI_Comm comm)
    : FaultSimulator(circuit, rows), circuit_(circuit), comm_(comm) {
    if (MPI_Comm_rank(comm_, &mpi_rank_) != MPI_SUCCESS ||
        MPI_Comm_size(comm_, &mpi_size_) != MPI_SUCCESS) {
        throw std::runtime_error("Unable to query MPI rank/size");
    }
    net_count_ = circuit_.netCount();
    if (net_count_ > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        throw std::runtime_error("Circuit too large for MPI buffers");
    }
    broadcast_length_ = static_cast<int>(net_count_);
    primary_inputs_ = circuit_.primaryInputs();
    primary_outputs_ = circuit_.primaryOutputs();
    buildLevelization();
}

void LevelizedMPI::buildLevelization() {
    const auto& gates = circuit_.gates();
    net_levels_.assign(net_count_, -1);
    for (auto pi : primary_inputs_) {
        if (pi >= net_levels_.size()) {
            throw std::runtime_error("Primary input references unknown net");
        }
        net_levels_[pi] = 0;
    }

    std::vector<bool> placed(gates.size(), false);
    std::size_t remaining = gates.size();
    max_level_ = 0;

    while (remaining > 0) {
        bool progress = false;
        for (std::size_t gate_idx = 0; gate_idx < gates.size(); ++gate_idx) {
            if (placed[gate_idx]) {
                continue;
            }
            const auto& gate = gates[gate_idx];
            int max_input_level = -1;
            bool ready = true;
            for (auto net : gate.inputs) {
                if (net >= net_levels_.size()) {
                    throw std::runtime_error("Gate input references unknown net");
                }
                const int level = net_levels_[net];
                if (level == -1) {
                    ready = false;
                    break;
                }
                max_input_level = std::max(max_input_level, level);
            }
            if (!ready) {
                continue;
            }
            const int gate_level = max_input_level + 1;
            if (gate.output >= net_levels_.size()) {
                throw std::runtime_error("Gate output references unknown net");
            }
            net_levels_[gate.output] = std::max(net_levels_[gate.output], gate_level);
            max_level_ = std::max(max_level_, gate_level);
            placed[gate_idx] = true;
            --remaining;
            progress = true;
        }
        if (!progress) {
            throw std::runtime_error(
                "Unable to levelize circuit (loop or missing dependency detected)");
        }
    }

    gates_by_level_.assign(max_level_ + 1, {});
    for (std::size_t gate_idx = 0; gate_idx < gates.size(); ++gate_idx) {
        const auto& gate = gates[gate_idx];
        const int level = net_levels_[gate.output];
        if (level < 0) {
            throw std::runtime_error("Gate output has no resolved level");
        }
        gates_by_level_[level].push_back(gate_idx);
    }

    assignLevelsToRanks();
}

void LevelizedMPI::assignLevelsToRanks() {
    const int total_levels = max_level_ + 1;
    if (total_levels <= 0) {
        level_owner_.assign(1, 0);
        return;
    }
    level_owner_.assign(total_levels, 0);
    int next_level = 0;
    int remaining_levels = total_levels;
    for (int rank = 0; rank < mpi_size_ && next_level < total_levels; ++rank) {
        const int ranks_left = mpi_size_ - rank;
        int span = remaining_levels / ranks_left;
        if (remaining_levels % ranks_left != 0) {
            ++span;
        }
        span = std::max(span, 1);
        for (int i = 0; i < span && next_level < total_levels; ++i) {
            level_owner_[next_level++] = rank;
        }
        remaining_levels = total_levels - next_level;
    }
    while (next_level < total_levels) {
        level_owner_[next_level++] = mpi_size_ - 1;
    }
}

int LevelizedMPI::evaluateGate(const core::Gate& gate,
                               const std::vector<int>& values) const {
    auto logical_not = [](int v) { return v ? 0 : 1; };
    switch (gate.type) {
        case core::GateType::And: {
            int result = 1;
            for (auto net : gate.inputs) {
                const int value = values[net];
                if (value == -1) {
                    throw std::runtime_error("Unresolved net during AND evaluation");
                }
                result &= value;
            }
            return result;
        }
        case core::GateType::Nand: {
            int result = 1;
            for (auto net : gate.inputs) {
                const int value = values[net];
                if (value == -1) {
                    throw std::runtime_error("Unresolved net during NAND evaluation");
                }
                result &= value;
            }
            return logical_not(result);
        }
        case core::GateType::Or: {
            int result = 0;
            for (auto net : gate.inputs) {
                const int value = values[net];
                if (value == -1) {
                    throw std::runtime_error("Unresolved net during OR evaluation");
                }
                result |= value;
            }
            return result;
        }
        case core::GateType::Nor: {
            int result = 0;
            for (auto net : gate.inputs) {
                const int value = values[net];
                if (value == -1) {
                    throw std::runtime_error("Unresolved net during NOR evaluation");
                }
                result |= value;
            }
            return logical_not(result);
        }
        case core::GateType::Xor: {
            int result = 0;
            for (auto net : gate.inputs) {
                const int value = values[net];
                if (value == -1) {
                    throw std::runtime_error("Unresolved net during XOR evaluation");
                }
                result ^= value;
            }
            return result;
        }
        case core::GateType::Xnor: {
            int result = 0;
            for (auto net : gate.inputs) {
                const int value = values[net];
                if (value == -1) {
                    throw std::runtime_error("Unresolved net during XNOR evaluation");
                }
                result ^= value;
            }
            return logical_not(result);
        }
        case core::GateType::Not: {
            if (gate.inputs.size() != 1) {
                throw std::runtime_error("NOT gate expects exactly one input");
            }
            const int value = values[gate.inputs.front()];
            if (value == -1) {
                throw std::runtime_error("Unresolved net during NOT evaluation");
            }
            return logical_not(value);
        }
        case core::GateType::Buf: {
            if (gate.inputs.size() != 1) {
                throw std::runtime_error("BUF gate expects exactly one input");
            }
            const int value = values[gate.inputs.front()];
            if (value == -1) {
                throw std::runtime_error("Unresolved net during BUF evaluation");
            }
            return value;
        }
        case core::GateType::Unknown:
        default:
            throw std::runtime_error("Encountered unknown gate type during evaluation");
    }
}

bool LevelizedMPI::simulateFault(
    const core::Pattern& pattern,
    const std::unordered_map<core::NetId, int>& provided_outputs,
    core::NetId fault_net,
    int stuck_value,
    std::vector<int>& working_values) const {
    const auto& gates = circuit_.gates();
    working_values.assign(net_count_, -1);
    if (mpi_rank_ == 0) {
        for (const auto& entry : pattern.assignments) {
            if (entry.net >= working_values.size()) {
                throw std::runtime_error("Pattern references unknown net");
            }
            working_values[entry.net] = entry.value;
        }
        if (fault_net >= working_values.size()) {
            throw std::runtime_error("Fault references unknown net");
        }
        working_values[fault_net] = stuck_value;
    }
    MPI_Bcast(working_values.data(), broadcast_length_, MPI_INT, 0, comm_);

    for (int level = 1; level <= max_level_; ++level) {
        const int owner = level_owner_.empty() ? 0 : level_owner_[level];
        int pair_count = 0;
        if (mpi_rank_ == owner) {
            level_buffer_.clear();
            const auto& level_gates = gates_by_level_[level];
            level_buffer_.reserve(level_gates.size() * 2);
            for (auto gate_idx : level_gates) {
                const auto& gate = gates[gate_idx];
                if (gate.output == fault_net) {
                    continue;
                }
                const int gate_value = evaluateGate(gate, working_values);
                working_values[gate.output] = gate_value;
                level_buffer_.push_back(static_cast<int>(gate.output));
                level_buffer_.push_back(gate_value);
            }
            pair_count = static_cast<int>(level_buffer_.size() / 2);
        }
        MPI_Bcast(&pair_count, 1, MPI_INT, owner, comm_);
        const int value_count = pair_count * 2;
        if (value_count > 0) {
            if (mpi_rank_ != owner) {
                level_buffer_.assign(value_count, 0);
            }
            MPI_Bcast(level_buffer_.data(), value_count, MPI_INT, owner, comm_);
            if (mpi_rank_ != owner) {
                for (int i = 0; i < pair_count; ++i) {
                    const auto net = static_cast<std::size_t>(level_buffer_[2 * i]);
                    working_values[net] = level_buffer_[2 * i + 1];
                }
            }
        } else if (mpi_rank_ != owner) {
            level_buffer_.clear();
        }
    }

    int result = 0;
    if (mpi_rank_ == 0) {
        bool equal = true;
        for (auto po : primary_outputs_) {
            if (po >= working_values.size()) {
                throw std::runtime_error("Primary output unresolved during simulation");
            }
            const auto it = provided_outputs.find(po);
            if (it == provided_outputs.end()) {
                throw std::runtime_error("Missing expected value for primary output");
            }
            if (working_values[po] != it->second) {
                equal = false;
                break;
            }
        }
        result = equal ? 1 : 0;
    }
    MPI_Bcast(&result, 1, MPI_INT, 0, comm_);
    return result == 1;
}

void LevelizedMPI::start() {
    std::vector<int> working_values(net_count_);
    for (std::size_t pattern_idx = 0; pattern_idx < rows_.size(); ++pattern_idx) {
        const auto& row = rows_[pattern_idx];
        const auto& pattern = row.pattern;
        const auto& expected = row.provided_outputs;
        for (core::NetId net = 0; net < net_count_; ++net) {
            const bool stuck0_eq = simulateFault(pattern, expected, net, 0, working_values);
            const bool stuck1_eq = simulateFault(pattern, expected, net, 1, working_values);
            if (mpi_rank_ == 0) {
                answers.set(pattern_idx, net, true, stuck0_eq);
                answers.set(pattern_idx, net, false, stuck1_eq);
            }
        }
    }
    MPI_Barrier(comm_);
}

}  // namespace algorithm

//#endif  // LEVELIZEDMPI
