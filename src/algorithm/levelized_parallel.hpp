#pragma once

#include <cstddef>
#include <unordered_map>
#include <vector>
#include "algorithm/fault_simulator.hpp"
#include "core/circuit.hpp"
#include "core/pattern_generator.hpp"

namespace algorithm {

class LevelizedParallel : public FaultSimulator {
public:
    LevelizedParallel(const core::Circuit& circuit,
                               const std::vector<io::PatternRow>& rows);
    ~LevelizedParallel() override = default;

    void start() override;

private:
    int evaluateGate(const core::Gate& gate, const std::vector<int>& values) const;
    bool simulateFault(const core::Pattern& pattern,
                    const std::unordered_map<core::NetId, int>& provided_outputs,
                    core::NetId fault_net,
                    int stuck_value,
                    std::vector<int>& working_values) const;
    void buildLevelization();
    const core::Circuit& circuit_;
    std::size_t net_count_{0};
    std::vector<std::size_t> topo_order_;
    std::vector<int> net_levels_;
    std::vector<std::vector<std::size_t>> fanout_;
    std::vector<core::NetId> primary_inputs_;
    std::vector<core::NetId> primary_outputs_;
    std::vector<std::vector<std::size_t>> gates_by_level_;
    int max_level_ = 0;
};

}  // namespace algorithm
