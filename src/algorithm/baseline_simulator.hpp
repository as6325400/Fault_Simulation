#pragma once

#include <string>
#include <vector>

#include "algorithm/fault_simulator.hpp"
#include "core/circuit.hpp"
#include "core/pattern_generator.hpp"
#include "core/simulator.hpp"

namespace algorithm {

class BaselineSimulator : public FaultSimulator {
public:
    BaselineSimulator(const core::Circuit& circuit, const std::vector<io::PatternRow>& rows);
    ~BaselineSimulator() override = default;

    std::vector<int> simulateOutputs(const core::Pattern& pattern) const;
    std::vector<FaultEvaluation> evaluate(const core::Pattern& pattern) const;
    void start() override;

private:
    core::Simulator simulator_;
};

}  // namespace algorithm
