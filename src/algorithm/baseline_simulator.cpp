#include "algorithm/baseline_simulator.hpp"

namespace algorithm {

BaselineSimulator::BaselineSimulator(const core::Circuit& circuit)
    : net_names_(circuit.netNames()), simulator_(circuit) {}

const std::vector<std::string>& BaselineSimulator::netNames() const {
    return net_names_;
}

std::vector<int> BaselineSimulator::simulateOutputs(const core::Pattern& pattern) const {
    const auto result = simulator_.simulate(pattern);
    return result.primary_outputs;
}

std::vector<FaultEvaluation> BaselineSimulator::evaluate(const core::Pattern& pattern) const {
    const auto golden = simulator_.simulate(pattern);
    const auto& reference_outputs = golden.primary_outputs;

    std::vector<FaultEvaluation> evaluations(net_names_.size());
    for (core::NetId net = 0; net < net_names_.size(); ++net) {
        core::FaultSpec stuck0{net, 0};
        core::FaultSpec stuck1{net, 1};
        const auto result0 = simulator_.simulateFault(pattern, stuck0);
        const auto result1 = simulator_.simulateFault(pattern, stuck1);

        evaluations[net].stuck0_eq = (result0.primary_outputs == reference_outputs);
        evaluations[net].stuck1_eq = (result1.primary_outputs == reference_outputs);
    }
    return evaluations;
}

}  // namespace algorithm
