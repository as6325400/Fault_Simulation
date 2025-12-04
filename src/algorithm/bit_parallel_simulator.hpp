#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "algorithm/fault_simulator.hpp"
#include "core/circuit.hpp"
#include "core/pattern_generator.hpp"

namespace algorithm {

class BitParallelSimulator : public FaultSimulator {
public:
    explicit BitParallelSimulator(const core::Circuit& circuit);
    ~BitParallelSimulator() override = default;

    std::vector<FaultEvaluation> evaluate(const core::Pattern& pattern) const override;
    const std::vector<std::string>& netNames() const override;

private:
    struct ChunkFault {
        std::size_t net_index{};
        int stuck_value{};
    };

    std::vector<bool> simulateChunk(const core::Pattern& pattern,
                                    const std::vector<ChunkFault>& chunk) const;

    const core::Circuit& circuit_;
    std::vector<std::string> net_names_;
    std::unordered_map<std::string, std::size_t> net_index_;
    std::vector<std::size_t> output_indices_;
};

}  // namespace algorithm
