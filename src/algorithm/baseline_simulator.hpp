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
    explicit BaselineSimulator(const core::Circuit& circuit);
    ~BaselineSimulator() override = default;

    const std::vector<std::string>& netNames() const override;
    std::vector<int> simulateOutputs(const core::Pattern& pattern) const;
    std::vector<FaultEvaluation> evaluate(const core::Pattern& pattern) const override;

private:
    std::vector<std::string> net_names_;
    core::Simulator simulator_;
};

}  // namespace algorithm
