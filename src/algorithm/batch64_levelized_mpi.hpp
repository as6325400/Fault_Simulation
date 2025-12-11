#pragma once

#ifdef BATCH64LEVELIZEDMPI

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

#include <mpi.h>

#include "algorithm/fault_simulator.hpp"
#include "core/circuit.hpp"
#include "core/pattern_generator.hpp"

using Word = std::uint64_t;

namespace algorithm {

class Batch64LevelizedMPI : public FaultSimulator {
public:
    Batch64LevelizedMPI(const core::Circuit& circuit,
                        const std::vector<io::PatternRow>& rows,
                        MPI_Comm comm = MPI_COMM_WORLD);
    ~Batch64LevelizedMPI() override = default;

    void start() override;

private:
    Word evaluateGate(const core::Gate& gate,
                      const std::vector<Word>& values,
                      const std::vector<bool>& ready,
                      Word mask) const;
    Word simulateFault(const std::vector<Word>& base_values,
                       const std::vector<bool>& base_ready,
                       const std::vector<Word>& expected_outputs,
                       core::NetId fault_net,
                       Word stuck_value,
                       Word mask,
                       std::vector<Word>& working_values,
                       std::vector<bool>& ready) const;
    void buildLevelization();

    const core::Circuit& circuit_;
    MPI_Comm comm_;
    int mpi_rank_{0};
    int mpi_size_{1};
    std::size_t net_count_{0};
    std::vector<core::NetId> primary_inputs_;
    std::vector<core::NetId> primary_outputs_;
    std::vector<int> net_levels_;
    std::vector<std::vector<std::size_t>> gates_by_level_;
    std::vector<int> level_owner_;
    int max_level_{0};
    std::vector<int> output_index_by_net_;

    mutable std::vector<int> level_indices_;
    mutable std::vector<Word> level_values_;
};

}  // namespace algorithm

#endif  // LEVELIZEDMPI
