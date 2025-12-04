#include "core/circuit.hpp"

#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <string>
#include <utility>

namespace {

std::string toUpper(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return value;
}

template <typename Container>
bool contains(const Container& container, const std::string& value) {
    return std::find(container.begin(), container.end(), value) != container.end();
}

}  // namespace

namespace core {

GateType gateTypeFromString(const std::string& type) {
    const std::string upper = toUpper(type);
    if (upper == "AND") {
        return GateType::And;
    }
    if (upper == "OR") {
        return GateType::Or;
    }
    if (upper == "NAND") {
        return GateType::Nand;
    }
    if (upper == "NOR") {
        return GateType::Nor;
    }
    if (upper == "XOR") {
        return GateType::Xor;
    }
    if (upper == "XNOR") {
        return GateType::Xnor;
    }
    if (upper == "NOT") {
        return GateType::Not;
    }
    if (upper == "BUF") {
        return GateType::Buf;
    }
    return GateType::Unknown;
}

std::string gateTypeToString(GateType type) {
    switch (type) {
        case GateType::And:
            return "AND";
        case GateType::Or:
            return "OR";
        case GateType::Nand:
            return "NAND";
        case GateType::Nor:
            return "NOR";
        case GateType::Xor:
            return "XOR";
        case GateType::Xnor:
            return "XNOR";
        case GateType::Not:
            return "NOT";
        case GateType::Buf:
            return "BUF";
        case GateType::Unknown:
        default:
            return "UNKNOWN";
    }
}

void Circuit::setName(std::string name) {
    name_ = std::move(name);
}

const std::string& Circuit::name() const {
    return name_;
}

void Circuit::addPrimaryInput(const std::string& net) {
    if (!contains(primary_inputs_, net)) {
        primary_inputs_.push_back(net);
    }
    registerNet(net, NetType::PrimaryInput);
}

void Circuit::addPrimaryOutput(const std::string& net) {
    if (!contains(primary_outputs_, net)) {
        primary_outputs_.push_back(net);
    }
    registerNet(net, NetType::PrimaryOutput);
}

void Circuit::addWire(const std::string& net) {
    if (!contains(wires_, net)) {
        wires_.push_back(net);
    }
    registerNet(net, NetType::Wire);
}

void Circuit::addGate(const Gate& gate) {
    if (gate.output.empty()) {
        throw std::invalid_argument("Gate output net cannot be empty");
    }
    gates_.push_back(gate);
    registerNet(gate.output, NetType::Wire);
    for (const auto& input : gate.inputs) {
        if (!input.empty()) {
            registerNet(input, NetType::Wire);
        }
    }
}

const std::vector<std::string>& Circuit::primaryInputs() const {
    return primary_inputs_;
}

const std::vector<std::string>& Circuit::primaryOutputs() const {
    return primary_outputs_;
}

const std::vector<std::string>& Circuit::wires() const {
    return wires_;
}

const std::vector<Gate>& Circuit::gates() const {
    return gates_;
}

std::vector<std::string> Circuit::netNames() const {
    std::vector<std::string> names;
    names.reserve(nets_.size());
    for (const auto& entry : nets_) {
        names.push_back(entry.first);
    }
    std::sort(names.begin(), names.end());
    return names;
}

bool Circuit::hasNet(const std::string& net) const {
    return nets_.find(net) != nets_.end();
}

NetType Circuit::netType(const std::string& net) const {
    const auto it = nets_.find(net);
    if (it == nets_.end()) {
        return NetType::Unknown;
    }
    return it->second;
}

void Circuit::registerNet(const std::string& net, NetType type) {
    if (net.empty()) {
        return;
    }
    auto it = nets_.find(net);
    if (it == nets_.end()) {
        nets_.emplace(net, type);
        return;
    }
    if (it->second == NetType::Wire && type != NetType::Wire) {
        it->second = type;
    }
}

}  // namespace core
