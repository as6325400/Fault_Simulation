#include "algorithm/batch_baseline.hpp"

#include <limits>
#include <stdexcept>

namespace algorithm {

namespace {

int evaluateGate(core::GateType type, const std::vector<int>& inputs) {
    if (inputs.empty()) {
        throw std::runtime_error("Gate missing inputs during DFS simulation");
    }
    auto logical_not = [](int value) { return value ? 0 : 1; };
    auto logical_and = [&]() {
        int result = 1;
        for (int value : inputs) {
            result &= value;
        }
        return result;
    };
    auto logical_or = [&]() {
        int result = 0;
        for (int value : inputs) {
            result |= value;
        }
        return result;
    };
    auto logical_xor = [&]() {
        int result = 0;
        for (int value : inputs) {
            result ^= value;
        }
        return result;
    };

    switch (type) {
        case core::GateType::And:
            return logical_and();
        case core::GateType::Nand:
            return logical_not(logical_and());
        case core::GateType::Or:
            return logical_or();
        case core::GateType::Nor:
            return logical_not(logical_or());
        case core::GateType::Xor:
            return logical_xor();
        case core::GateType::Xnor:
            return logical_not(logical_xor());
        case core::GateType::Not:
            if (inputs.size() != 1) {
                throw std::runtime_error("NOT gate expects exactly one input");
            }
            return logical_not(inputs.front());
        case core::GateType::Buf:
            if (inputs.size() != 1) {
                throw std::runtime_error("BUF gate expects exactly one input");
            }
            return inputs.front();
        case core::GateType::Unknown:
        default:
            throw std::runtime_error("Unknown gate type during DFS simulation");
    }
}

int dfs(core::NetId target,
        core::NetId fault_wire,
        bool stuck_at_0,
        const core::Circuit& circuit,
        const std::vector<int>& net_to_gate,
        std::vector<bool>& visited,
        std::vector<int>& values) {
    if (visited[target]) {
        return values[target];
    }
    if (fault_wire != std::numeric_limits<core::NetId>::max() && target == fault_wire) {
        visited[target] = true;
        values[target] = stuck_at_0 ? 0 : 1;
        return values[target];
    }

    const auto net_type = circuit.netType(target);
    if (net_type == core::NetType::PrimaryInput) {
        return values[target];
    }

    const int gate_index = net_to_gate[target];
    const auto& gate = circuit.gates()[static_cast<std::size_t>(gate_index)];
    std::vector<int> input_values;
    input_values.reserve(gate.inputs.size());
    for (auto input_net : gate.inputs) {
        input_values.push_back(dfs(input_net, fault_wire, stuck_at_0, circuit, net_to_gate, visited,
                                   values));
    }

    const int result = evaluateGate(gate.type, input_values);
    visited[target] = true;
    values[target] = result;
    return result;
}

}  // namespace

BatchBaselineSimulator::BatchBaselineSimulator(const core::Circuit& circuit,
                                               const std::vector<io::PatternRow>& rows)
    : FaultSimulator(circuit, rows) {
    net_to_gate_.assign(circuit_.netCount(), -1);
    const auto& gates = circuit_.gates();
    for (std::size_t i = 0; i < gates.size(); ++i) {
        net_to_gate_[gates[i].output] = static_cast<int>(i);
    }
}

bool BatchBaselineSimulator::simulate(std::size_t pattern_id,
                                      core::NetId fault_wire,
                                      bool stuck_at_0,
                                      const std::unordered_map<core::NetId, int>& provided_outputs) {
    std::vector<bool> visited(circuit_.netCount(), false);
    std::vector<int> values(circuit_.netCount(), -1);
    const int forced_value = stuck_at_0 ? 0 : 1;

    for (const auto& entry : rows_[pattern_id].pattern.assignments) {
        int value = entry.value;
        if (entry.net == fault_wire) {
            value = forced_value;
        }
        visited[entry.net] = true;
        values[entry.net] = value;
    }

    for (auto output_net : circuit_.primaryOutputs()) {
        const auto expected_it = provided_outputs.find(output_net);
        const int expected = expected_it->second;
        const int actual =
            dfs(output_net, fault_wire, stuck_at_0, circuit_, net_to_gate_, visited, values);
        if (actual != expected) {
            return false;
        }
    }
    return true;
}

void BatchBaselineSimulator::start() {
    for (std::size_t pattern_id = 0; pattern_id < rows_.size(); ++pattern_id) {
        std::unordered_map<core::NetId, int> reference_outputs = rows_[pattern_id].provided_outputs;

        for (core::NetId net = 0; net < circuit_.netCount(); ++net) {
            const bool stuck0_eq = simulate(pattern_id, net, true, reference_outputs);
            const bool stuck1_eq = simulate(pattern_id, net, false, reference_outputs);
            answers.set(pattern_id, net, true, stuck0_eq);
            answers.set(pattern_id, net, false, stuck1_eq);
        }
    }
}

}  // namespace algorithm
