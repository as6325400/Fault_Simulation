#include "algorithm/baseline_simulator.hpp"

namespace algorithm {

BaselineSimulator::BaselineSimulator(const core::Circuit& circuit,
                                     const std::vector<io::PatternRow>& rows)
    : FaultSimulator(circuit, rows), simulator_(circuit) {}

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

void BaselineSimulator::start() {
    for (std::size_t i = 0; i < rows_.size(); ++i) {
        const auto evaluations = evaluate(rows_[i].pattern);
        if (evaluations.size() != net_names_.size()) {
            throw std::runtime_error("Evaluation result size mismatch");
        }
        for (std::size_t net_id = 0; net_id < evaluations.size(); ++net_id) {
            answers.set(i, net_id, true, evaluations[net_id].stuck0_eq);
            answers.set(i, net_id, false, evaluations[net_id].stuck1_eq);
        }
    }
}

}  // namespace algorithm
