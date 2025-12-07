#pragma once

#include <limits>
#include <vector>

#include "core/circuit.hpp"
#include "core/pattern_generator.hpp"

namespace core {

struct SimulationResult {
    std::vector<int> net_values;
    std::vector<int> primary_outputs;
};

struct FaultSpec {
    NetId net{std::numeric_limits<NetId>::max()};
    int value{};
};

class Simulator {
public:
    explicit Simulator(const Circuit& circuit);

    SimulationResult simulate(const Pattern& pattern) const;
    SimulationResult simulateFault(const Pattern& pattern, const FaultSpec& fault) const;
    std::vector<SimulationResult> simulate(const std::vector<Pattern>& patterns) const;

private:
    const Circuit& circuit_;

    SimulationResult simulateInternal(const Pattern& pattern,
                                      const FaultSpec* fault) const;
    int evaluateGateValue(GateType type, const std::vector<int>& values) const;
    bool tryEvaluateGate(const Gate& gate, std::vector<int>& values,
                         const FaultSpec* fault) const;
};

}  // namespace core
