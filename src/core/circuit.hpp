#pragma once

#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

namespace core {

using NetId = std::size_t;

enum class GateType {
    And,
    Or,
    Nand,
    Nor,
    Xor,
    Xnor,
    Not,
    Buf,
    Unknown,
};

enum class NetType {
    Unknown,
    PrimaryInput,
    PrimaryOutput,
    Wire,
};

GateType gateTypeFromString(const std::string& type);
std::string gateTypeToString(GateType type);

struct Gate {
    GateType type{GateType::Unknown};
    std::string name;
    NetId output{};
    std::vector<NetId> inputs;
};

class Circuit {
public:
    void setName(std::string name);
    const std::string& name() const;

    void addPrimaryInput(const std::string& net);
    void addPrimaryOutput(const std::string& net);
    void addWire(const std::string& net);

    void addGate(const Gate& gate);

    void finalizeNets();

    const std::vector<NetId>& primaryInputs() const;
    const std::vector<NetId>& primaryOutputs() const;
    const std::vector<NetId>& wires() const;
    const std::vector<Gate>& gates() const;
    const std::vector<std::string>& netNames() const;
    std::size_t netCount() const;
    const std::string& netName(NetId id) const;

    bool hasNet(const std::string& net) const;
    NetType netType(const std::string& net) const;
    NetType netType(NetId id) const;

    NetId netId(const std::string& net) const;
    NetId ensureNet(const std::string& net, NetType type);

private:
    NetId registerNet(const std::string& net, NetType type);

    std::string name_;
    std::vector<NetId> primary_inputs_;
    std::vector<NetId> primary_outputs_;
    std::vector<NetId> wires_;
    std::vector<Gate> gates_;
    std::vector<std::string> net_names_;
    std::vector<NetType> net_types_;
    std::unordered_map<std::string, NetId> net_lookup_;
};

}  // namespace core
