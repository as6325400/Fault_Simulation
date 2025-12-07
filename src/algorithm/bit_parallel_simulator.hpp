#pragma once

#include <string>
#include <vector>

#include "algorithm/fault_simulator.hpp"
#include "core/circuit.hpp"
#include "core/pattern_generator.hpp"

namespace algorithm {

class BitParallelSimulator : public FaultSimulator {
public:
    BitParallelSimulator(const core::Circuit& circuit, const std::vector<io::PatternRow>& rows);
    ~BitParallelSimulator() override = default;

    std::vector<FaultEvaluation> evaluate(const core::Pattern& pattern) const;
    void start() override;

private:
    struct ChunkFault {
        std::size_t net_index{};
        int stuck_value{};
    };

    std::vector<bool> simulateChunk(const core::Pattern& pattern,
                                    const std::vector<ChunkFault>& chunk) const;

    const core::Circuit& circuit_;
    std::vector<std::size_t> output_indices_;
};

}  // namespace algorithm
