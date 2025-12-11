#pragma once

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

#include "algorithm/fault_simulator.hpp"
#include "core/circuit.hpp"
#include "core/pattern_generator.hpp"

using Word = uint64_t;

namespace algorithm {

class Batch64LevelizedBaseline : public FaultSimulator {
public:
    Batch64LevelizedBaseline(const core::Circuit& circuit,
                               const std::vector<io::PatternRow>& rows);
    ~Batch64LevelizedBaseline() override = default;

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
    std::size_t net_count_{0};
    std::vector<std::size_t> topo_order_;
    std::vector<int> net_levels_;
    std::vector<std::vector<std::size_t>> fanout_;
    std::vector<core::NetId> primary_inputs_;
    std::vector<core::NetId> primary_outputs_;
    std::vector<std::vector<std::size_t>> gates_by_level_;
    int max_level_ = 0;
    std::vector<int> output_index_by_net_;
};

}  // namespace algorithm
