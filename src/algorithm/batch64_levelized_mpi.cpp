#ifdef BATCH64LEVELIZEDMPI

#include "algorithm/batch64_levelized_mpi.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <vector>

namespace algorithm {

Batch64LevelizedMPI::Batch64LevelizedMPI(const core::Circuit& circuit,
                                         const std::vector<io::PatternRow>& rows,
                                         MPI_Comm comm)
    : FaultSimulator(circuit, rows), circuit_(circuit), comm_(comm) {
    if (MPI_Comm_rank(comm_, &mpi_rank_) != MPI_SUCCESS ||
        MPI_Comm_size(comm_, &mpi_size_) != MPI_SUCCESS) {
        throw std::runtime_error("Unable to query MPI rank/size");
    }

    net_count_ = circuit_.netCount();
    primary_inputs_ = circuit_.primaryInputs();
    primary_outputs_ = circuit_.primaryOutputs();

    output_index_by_net_.assign(net_count_, -1);
    for (std::size_t i = 0; i < primary_outputs_.size(); ++i) {
        output_index_by_net_[primary_outputs_[i]] = static_cast<int>(i);
    }

    buildLevelization();
}

void Batch64LevelizedMPI::buildLevelization() {
    const auto& gates = circuit_.gates();
    net_levels_.assign(net_count_, -1);
    for (auto pi : primary_inputs_) {
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
            max_level_ = std::max(max_level_, gate_level);
            net_levels_[gate.output] = std::max(net_levels_[gate.output], gate_level);
            placed[gate_idx] = true;
            --remaining;
            progress = true;
        }
        if (!progress) {
            throw std::runtime_error(
                "Unable to levelize circuit (combinational loop or missing dependency)");
        }
    }

    gates_by_level_.assign(max_level_ + 1, {});
    for (std::size_t gate_idx = 0; gate_idx < gates.size(); ++gate_idx) {
        const auto& gate = gates[gate_idx];
        const int lv = net_levels_[gate.output];
        if (lv < 0) {
            throw std::runtime_error("Gate output net has no level");
        }
        gates_by_level_[lv].push_back(gate_idx);
    }

    const int total_levels = max_level_ + 1;
    level_owner_.assign(total_levels > 0 ? total_levels : 1, 0);
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

Word Batch64LevelizedMPI::evaluateGate(const core::Gate& gate,
                                       const std::vector<Word>& values,
                                       const std::vector<bool>& ready,
                                       Word mask) const {
    auto fetch = [&](core::NetId net) -> Word {
        if (!ready[net]) {
            throw std::runtime_error("Unresolved net during gate evaluation");
        }
        return values[net] & mask;
    };

    switch (gate.type) {
        case core::GateType::And: {
            Word result = mask;
            for (auto net : gate.inputs) {
                result &= fetch(net);
            }
            return result & mask;
        }
        case core::GateType::Nand: {
            Word result = mask;
            for (auto net : gate.inputs) {
                result &= fetch(net);
            }
            return (~result) & mask;
        }
        case core::GateType::Or: {
            Word result = 0;
            for (auto net : gate.inputs) {
                result |= fetch(net);
            }
            return result & mask;
        }
        case core::GateType::Nor: {
            Word result = 0;
            for (auto net : gate.inputs) {
                result |= fetch(net);
            }
            return (~result) & mask;
        }
        case core::GateType::Xor: {
            Word result = 0;
            for (auto net : gate.inputs) {
                result ^= fetch(net);
            }
            return result & mask;
        }
        case core::GateType::Xnor: {
            Word result = 0;
            for (auto net : gate.inputs) {
                result ^= fetch(net);
            }
            return (~result) & mask;
        }
        case core::GateType::Not: {
            if (gate.inputs.size() != 1) {
                throw std::runtime_error("NOT gate expects exactly one input");
            }
            return (~fetch(gate.inputs.front())) & mask;
        }
        case core::GateType::Buf: {
            if (gate.inputs.size() != 1) {
                throw std::runtime_error("BUF gate expects exactly one input");
            }
            return fetch(gate.inputs.front()) & mask;
        }
        case core::GateType::Unknown:
        default:
            throw std::runtime_error("Encountered unknown gate type during evaluation");
    }
}

Word Batch64LevelizedMPI::simulateFault(const std::vector<Word>& base_values,
                                        const std::vector<bool>& base_ready,
                                        const std::vector<Word>& expected_outputs,
                                        core::NetId fault_net,
                                        Word stuck_value,
                                        Word mask,
                                        std::vector<Word>& working_values,
                                        std::vector<bool>& ready) const {
    if (fault_net >= net_count_) {
        throw std::runtime_error("Fault references unknown net");
    }

    working_values = base_values;
    ready = base_ready;
    working_values[fault_net] = stuck_value & mask;
    ready[fault_net] = true;

    const auto& gates = circuit_.gates();
    for (int level = 1; level <= max_level_; ++level) {
        const int owner = level_owner_.empty() ? 0 : level_owner_[level];
        int update_count = 0;
        if (mpi_rank_ == owner) {
            level_indices_.clear();
            level_values_.clear();
            const auto& level_gates = gates_by_level_[level];
            level_indices_.reserve(level_gates.size());
            level_values_.reserve(level_gates.size());
            for (auto gate_idx : level_gates) {
                const auto& gate = gates[gate_idx];
                if (gate.output == fault_net) {
                    continue;
                }
                const Word gate_value = evaluateGate(gate, working_values, ready, mask);
                working_values[gate.output] = gate_value;
                ready[gate.output] = true;
                level_indices_.push_back(static_cast<int>(gate.output));
                level_values_.push_back(gate_value);
            }
            update_count = static_cast<int>(level_indices_.size());
        }
        MPI_Bcast(&update_count, 1, MPI_INT, owner, comm_);
        if (update_count > 0) {
            if (mpi_rank_ != owner) {
                level_indices_.resize(update_count);
                level_values_.resize(update_count);
            }
            MPI_Bcast(level_indices_.data(), update_count, MPI_INT, owner, comm_);
            MPI_Bcast(level_values_.data(), update_count, MPI_UINT64_T, owner, comm_);
            if (mpi_rank_ != owner) {
                for (int i = 0; i < update_count; ++i) {
                    const auto net = static_cast<std::size_t>(level_indices_[i]);
                    working_values[net] = level_values_[i] & mask;
                    ready[net] = true;
                }
            }
        } else if (mpi_rank_ != owner) {
            level_indices_.clear();
            level_values_.clear();
        }
    }

    Word eq_bits = 0;
    if (mpi_rank_ == 0) {
        eq_bits = mask;
        for (std::size_t i = 0; i < primary_outputs_.size(); ++i) {
            const auto po_net = primary_outputs_[i];
            if (!ready[po_net]) {
                throw std::runtime_error("Unable to resolve primary output during fault simulation");
            }
            const Word diff = (working_values[po_net] ^ expected_outputs[i]) & mask;
            eq_bits &= (~diff) & mask;
        }
    }
    MPI_Bcast(&eq_bits, 1, MPI_UINT64_T, 0, comm_);
    return eq_bits & mask;
}

void Batch64LevelizedMPI::start() {
    const std::size_t outputs_count = primary_outputs_.size();
    std::vector<Word> working_values(net_count_, 0);
    std::vector<bool> ready(net_count_, false);

    for (std::size_t base = 0; base < rows_.size(); base += 64) {
        const std::size_t chunk_size = std::min<std::size_t>(64, rows_.size() - base);
        const Word mask =
            (chunk_size == 64) ? std::numeric_limits<Word>::max() : ((Word{1} << chunk_size) - 1);

        std::vector<Word> base_values(net_count_, 0);
        std::vector<bool> base_ready(net_count_, false);
        for (std::size_t offset = 0; offset < chunk_size; ++offset) {
            const Word bit = Word{1} << offset;
            const auto& assignments = rows_[base + offset].pattern.assignments;
            for (const auto& entry : assignments) {
                if (entry.net >= net_count_) {
                    throw std::runtime_error("Pattern references unknown net");
                }
                if (entry.value != 0 && entry.value != 1) {
                    throw std::runtime_error("Pattern contains non-binary value");
                }
                if (entry.value) {
                    base_values[entry.net] |= bit;
                }
                base_ready[entry.net] = true;
            }
        }

        std::vector<Word> expected(outputs_count, 0);
        std::vector<Word> expected_mask(outputs_count, 0);
        for (std::size_t offset = 0; offset < chunk_size; ++offset) {
            const Word bit = Word{1} << offset;
            const auto& provided = rows_[base + offset].provided_outputs;
            for (const auto& kv : provided) {
                const int idx = output_index_by_net_[kv.first];
                if (idx < 0) {
                    continue;
                }
                if (kv.second) {
                    expected[static_cast<std::size_t>(idx)] |= bit;
                }
                expected_mask[static_cast<std::size_t>(idx)] |= bit;
            }
        }

        for (std::size_t i = 0; i < outputs_count; ++i) {
            if ((expected_mask[i] & mask) != mask) {
                throw std::runtime_error("Missing expected value for primary output");
            }
        }

        for (core::NetId net = 0; net < net_count_; ++net) {
            const Word eq0 =
                simulateFault(base_values, base_ready, expected, net, Word{0}, mask, working_values, ready);
            const Word eq1 =
                simulateFault(base_values, base_ready, expected, net, mask, mask, working_values, ready);

            if (mpi_rank_ == 0) {
                for (std::size_t offset = 0; offset < chunk_size; ++offset) {
                    const bool stuck0_eq = ((eq0 >> offset) & Word{1}) != 0;
                    const bool stuck1_eq = ((eq1 >> offset) & Word{1}) != 0;
                    answers.set(base + offset, net, true, stuck0_eq);
                    answers.set(base + offset, net, false, stuck1_eq);
                }
            }
        }
    }

    MPI_Barrier(comm_);
}

}  // namespace algorithm

#endif  // BATCH64LEVELIZEDMPI
