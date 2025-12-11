#pragma once
#include <cstddef>
#include <unordered_map>
#include <vector>
#include <mpi.h>

#include "algorithm/fault_simulator.hpp"
#include "core/circuit.hpp"
#include "core/pattern_generator.hpp"

namespace algorithm {

class LevelizedMPI : public FaultSimulator {
public:
    LevelizedMPI(const core::Circuit& circuit,
                 const std::vector<io::PatternRow>& rows,
                 MPI_Comm comm = MPI_COMM_WORLD);
    ~LevelizedMPI() override = default;

    void start() override;

private:
    void buildLevelization();
    void assignLevelsToRanks();
    int evaluateGate(const core::Gate& gate, const std::vector<int>& values) const;
    bool simulateFault(const core::Pattern& pattern,
                       const std::unordered_map<core::NetId, int>& provided_outputs,
                       core::NetId fault_net,
                       int stuck_value,
                       std::vector<int>& working_values) const;

    const core::Circuit& circuit_;
    MPI_Comm comm_;
    int mpi_rank_{0};
    int mpi_size_{1};
    int broadcast_length_{0};
    std::size_t net_count_{0};
    std::vector<core::NetId> primary_inputs_;
    std::vector<core::NetId> primary_outputs_;
    std::vector<int> net_levels_;
    std::vector<std::vector<std::size_t>> gates_by_level_;
    std::vector<int> level_owner_;
    int max_level_{0};
    mutable std::vector<int> level_buffer_;
};

}  // namespace algorithm
