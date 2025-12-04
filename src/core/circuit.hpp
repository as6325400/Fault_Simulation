#pragma once

#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

namespace core {

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
    std::string output;
    std::vector<std::string> inputs;
};

class Circuit {
public:
    void setName(std::string name);
    const std::string& name() const;

    void addPrimaryInput(const std::string& net);
    void addPrimaryOutput(const std::string& net);
    void addWire(const std::string& net);

    void addGate(const Gate& gate);

    const std::vector<std::string>& primaryInputs() const;
    const std::vector<std::string>& primaryOutputs() const;
    const std::vector<std::string>& wires() const;
    const std::vector<Gate>& gates() const;
    std::vector<std::string> netNames() const;

    bool hasNet(const std::string& net) const;
    NetType netType(const std::string& net) const;

private:
    void registerNet(const std::string& net, NetType type);

    std::string name_;
    std::vector<std::string> primary_inputs_;
    std::vector<std::string> primary_outputs_;
    std::vector<std::string> wires_;
    std::vector<Gate> gates_;
    std::unordered_map<std::string, NetType> nets_;
};

}  // namespace core
