#include "core/simulator.hpp"

#include <stdexcept>

namespace core {

Simulator::Simulator(const Circuit& circuit) : circuit_(circuit) {}

SimulationResult Simulator::simulate(const Pattern& pattern) const {
    return simulateInternal(pattern, nullptr);
}

SimulationResult Simulator::simulateFault(const Pattern& pattern,
                                          const FaultSpec& fault) const {
    if (fault.net == std::numeric_limits<NetId>::max() || fault.net >= circuit_.netCount()) {
        throw std::runtime_error("Fault references unknown net");
    }
    if (fault.value != 0 && fault.value != 1) {
        throw std::runtime_error("Fault value must be binary (0/1)");
    }
    return simulateInternal(pattern, &fault);
}

std::vector<SimulationResult> Simulator::simulate(const std::vector<Pattern>& patterns) const {
    std::vector<SimulationResult> results;
    results.reserve(patterns.size());
    for (const auto& pattern : patterns) {
        results.push_back(simulate(pattern));
    }
    return results;
}

int Simulator::evaluateGateValue(GateType type, const std::vector<int>& values) const {
    if (values.empty()) {
        throw std::runtime_error("Gate missing inputs during simulation");
    }

    auto logical_not = [](int value) { return value ? 0 : 1; };
    auto logical_and = [&]() {
        int result = 1;
        for (int value : values) {
            result &= value;
        }
        return result;
    };
    auto logical_or = [&]() {
        int result = 0;
        for (int value : values) {
            result |= value;
        }
        return result;
    };
    auto logical_xor = [&]() {
        int result = 0;
        for (int value : values) {
            result ^= value;
        }
        return result;
    };

    switch (type) {
        case GateType::And:
            return logical_and();
        case GateType::Nand:
            return logical_not(logical_and());
        case GateType::Or:
            return logical_or();
        case GateType::Nor:
            return logical_not(logical_or());
        case GateType::Xor:
            return logical_xor();
        case GateType::Xnor:
            return logical_not(logical_xor());
        case GateType::Not:
            if (values.size() != 1) {
                throw std::runtime_error("NOT gate expects exactly one input");
            }
            return logical_not(values.front());
        case GateType::Buf:
            if (values.size() != 1) {
                throw std::runtime_error("BUF gate expects exactly one input");
            }
            return values.front();
        case GateType::Unknown:
        default:
            throw std::runtime_error("Encountered unknown gate type during simulation");
    }
}

SimulationResult Simulator::simulateInternal(const Pattern& pattern,
                                             const FaultSpec* fault) const {
    std::vector<int> values(circuit_.netCount(), -1);

    auto isForcedNet = [&](NetId net) {
        return fault != nullptr && net == fault->net;
    };

    // Seed primary inputs.
    for (const auto& entry : pattern.assignments) {
        if (entry.value != 0 && entry.value != 1) {
            throw std::runtime_error("Pattern contains non-binary value for net");
        }
        if (entry.net == std::numeric_limits<NetId>::max() || entry.net >= values.size()) {
            throw std::runtime_error("Pattern references unknown net");
        }
        int value = entry.value;
        if (isForcedNet(entry.net)) {
            value = fault->value;
        }
        values[entry.net] = value;
    }

    for (const auto& pi : circuit_.primaryInputs()) {
        if (values[pi] == -1) {
            throw std::runtime_error("Pattern missing assignment for primary input");
        }
    }

    if (fault && values[fault->net] == -1) {
        values[fault->net] = fault->value;
    }

    const auto& gates = circuit_.gates();
    std::vector<bool> evaluated(gates.size(), false);
    std::size_t remaining = gates.size();

    while (remaining > 0) {
        bool progress = false;
        for (std::size_t i = 0; i < gates.size(); ++i) {
            if (evaluated[i]) {
                continue;
            }
            if (tryEvaluateGate(gates[i], values, fault)) {
                evaluated[i] = true;
                --remaining;
                progress = true;
            }
        }
        if (!progress) {
            throw std::runtime_error(
                "Unable to resolve all gates; check for combinational loops or missing nets.");
        }
    }

    SimulationResult result;
    result.net_values = values;
    const auto& outputs = circuit_.primaryOutputs();
    result.primary_outputs.reserve(outputs.size());
    for (const auto& output : outputs) {
        if (output >= values.size() || values[output] == -1) {
            throw std::runtime_error("Unable to resolve primary output");
        }
        result.primary_outputs.push_back(values[output]);
    }
    return result;
}

bool Simulator::tryEvaluateGate(const Gate& gate,
                                std::vector<int>& values,
                                const FaultSpec* fault) const {
    if (fault && gate.output == fault->net) {
        values[gate.output] = fault->value;
        return true;
    }

    std::vector<int> input_values;
    input_values.reserve(gate.inputs.size());
    for (NetId net : gate.inputs) {
        const int value = values[net];
        if (value == -1) {
            if (fault && net == fault->net) {
                values[net] = fault->value;
                input_values.push_back(fault->value);
                continue;
            }
            return false;
        }
        input_values.push_back(value);
    }

    const int gate_value = evaluateGateValue(gate.type, input_values);
    if (fault && gate.output == fault->net) {
        values[gate.output] = fault->value;
    } else {
        values[gate.output] = gate_value;
    }
    return true;
}

}  // namespace core
