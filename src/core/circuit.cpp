#include "core/circuit.hpp"

#include <algorithm>
#include <cctype>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <string>
#include <utility>

namespace {

std::string toUpper(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return value;
}

template <typename Container, typename T>
bool contains(const Container& container, const T& value) {
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
    NetId id = registerNet(net, NetType::PrimaryInput);
    if (!contains(primary_inputs_, id)) {
        primary_inputs_.push_back(id);
    }
}

void Circuit::addPrimaryOutput(const std::string& net) {
    NetId id = registerNet(net, NetType::PrimaryOutput);
    if (!contains(primary_outputs_, id)) {
        primary_outputs_.push_back(id);
    }
}

void Circuit::addWire(const std::string& net) {
    NetId id = registerNet(net, NetType::Wire);
    if (!contains(wires_, id)) {
        wires_.push_back(id);
    }
}

void Circuit::addGate(const Gate& gate) {
    if (gate.output == std::numeric_limits<NetId>::max()) {
        throw std::invalid_argument("Gate output net cannot be empty");
    }
    if (gate.output >= net_names_.size()) {
        throw std::invalid_argument("Gate references unregistered output net");
    }
    for (NetId input : gate.inputs) {
        if (input == std::numeric_limits<NetId>::max() || input >= net_names_.size()) {
            throw std::invalid_argument("Gate references unregistered input net");
        }
    }
    gates_.push_back(gate);
}

void Circuit::finalizeNets() {
    const std::size_t count = net_names_.size();
    std::vector<NetId> order(count);
    std::iota(order.begin(), order.end(), 0);
    std::sort(order.begin(), order.end(),
              [&](NetId a, NetId b) { return net_names_[a] < net_names_[b]; });

    std::vector<NetId> old_to_new(count);
    for (NetId new_id = 0; new_id < count; ++new_id) {
        old_to_new[order[new_id]] = new_id;
    }

    auto remap = [&](NetId id) { return old_to_new[id]; };

    std::vector<std::string> new_names(count);
    std::vector<NetType> new_types(count);
    for (NetId new_id = 0; new_id < count; ++new_id) {
        const NetId old_id = order[new_id];
        new_names[new_id] = net_names_[old_id];
        new_types[new_id] = net_types_[old_id];
    }
    net_names_ = std::move(new_names);
    net_types_ = std::move(new_types);

    net_lookup_.clear();
    for (NetId id = 0; id < net_names_.size(); ++id) {
        net_lookup_.emplace(net_names_[id], id);
    }

    for (auto& id : primary_inputs_) {
        id = remap(id);
    }
    for (auto& id : primary_outputs_) {
        id = remap(id);
    }
    for (auto& id : wires_) {
        id = remap(id);
    }
    for (auto& gate : gates_) {
        gate.output = remap(gate.output);
        for (auto& input : gate.inputs) {
            input = remap(input);
        }
    }
}

const std::vector<NetId>& Circuit::primaryInputs() const {
    return primary_inputs_;
}

const std::vector<NetId>& Circuit::primaryOutputs() const {
    return primary_outputs_;
}

const std::vector<NetId>& Circuit::wires() const {
    return wires_;
}

const std::vector<Gate>& Circuit::gates() const {
    return gates_;
}

const std::vector<std::string>& Circuit::netNames() const {
    return net_names_;
}

std::size_t Circuit::netCount() const {
    return net_names_.size();
}

const std::string& Circuit::netName(NetId id) const {
    if (id >= net_names_.size()) {
        throw std::out_of_range("Net id out of range");
    }
    return net_names_[id];
}

bool Circuit::hasNet(const std::string& net) const {
    return net_lookup_.find(net) != net_lookup_.end();
}

NetType Circuit::netType(const std::string& net) const {
    const auto it = net_lookup_.find(net);
    if (it == net_lookup_.end()) {
        return NetType::Unknown;
    }
    return netType(it->second);
}

NetType Circuit::netType(NetId id) const {
    if (id >= net_types_.size()) {
        return NetType::Unknown;
    }
    return net_types_[id];
}

NetId Circuit::netId(const std::string& net) const {
    const auto it = net_lookup_.find(net);
    if (it == net_lookup_.end()) {
        return std::numeric_limits<NetId>::max();
    }
    return it->second;
}

NetId Circuit::ensureNet(const std::string& net, NetType type) {
    return registerNet(net, type);
}

NetId Circuit::registerNet(const std::string& net, NetType type) {
    if (net.empty()) {
        return std::numeric_limits<NetId>::max();
    }
    auto it = net_lookup_.find(net);
    if (it == net_lookup_.end()) {
        NetId id = net_names_.size();
        net_lookup_.emplace(net, id);
        net_names_.push_back(net);
        net_types_.push_back(type);
        return id;
    }
    const NetId id = it->second;
    if (net_types_[id] == NetType::Wire && type != NetType::Wire) {
        net_types_[id] = type;
    }
    return id;
}

}  // namespace core
