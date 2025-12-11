#include "algorithm/levelized_baseline.hpp"
#include <iostream>
#include <algorithm>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <vector>
#include <omp.h>

namespace algorithm {

LevelizedBaselineSimulator::LevelizedBaselineSimulator(
    const core::Circuit& circuit, const std::vector<io::PatternRow>& rows)
    : FaultSimulator(circuit, rows), circuit_(circuit) {
    net_count_ = circuit_.netCount();
    primary_inputs_ = circuit_.primaryInputs();
    primary_outputs_ = circuit_.primaryOutputs();
    buildLevelization();
}

void LevelizedBaselineSimulator::buildLevelization() {
    const auto& gates = circuit_.gates();
    fanout_.assign(net_count_, {});
    for (std::size_t gate_idx = 0; gate_idx < gates.size(); ++gate_idx) {
        for (auto net : gates[gate_idx].inputs) {
            fanout_[net].push_back(gate_idx);
        }
    }

    net_levels_.assign(net_count_, -1);
    for (auto pi : primary_inputs_) {
        net_levels_[pi] = 0;
    }

    topo_order_.clear();
    topo_order_.reserve(gates.size());
    std::vector<bool> placed(gates.size(), false);
    std::size_t remaining = gates.size();
    max_level_=0;
    while (remaining > 0) {
        bool progress = false;
        for (std::size_t gate_idx = 0; gate_idx < gates.size(); ++gate_idx) {
            if (placed[gate_idx]) continue;
        
            const auto& gate = gates[gate_idx];
            int max_input_level = -1;
            bool ready = true;
            for (auto net : gate.inputs) {
                const int level = net_levels_[net];
                if (level == -1) {
                    ready = false;
                    break;
                }
                max_input_level = std::max(max_input_level, level);
            }

            if (!ready) continue;
        
            const int gate_level = max_input_level + 1;
            max_level_ = std::max(max_level_, gate_level);
            net_levels_[gate.output] = std::max(net_levels_[gate.output], gate_level);
            topo_order_.push_back(gate_idx);
            placed[gate_idx] = true;
            --remaining;
            progress = true;
        }
        if (!progress) {
            throw std::runtime_error(
                "Unable to levelize circuit (combinational loop or missing dependency)");
        }
    }
    /*std::cerr<<"Topo order: "<<'\n';
    for (auto idx: topo_order_) {
        std::cerr<< "gate_idx: "<<idx << ", gate_output: "<<gates[idx].output<<", gate_name: "<<gates[idx].name<<'\n';
        std::cerr<<"net_level(out, in1, in2): "<<net_levels_[gates[idx].output]<<" ";
        for (size_t i=0;i<gates[idx].inputs.size();++i) {
            std::cerr<<net_levels_[gates[idx].inputs[i]]<<" ";
        }
        std::cerr<<'\n';
    }
    std::cerr<<'\n';*/
    
    gates_by_level_.assign(max_level_+1, {});
    for (std::size_t gate_idx = 0; gate_idx < gates.size(); ++gate_idx) {
        const auto& gate = gates[gate_idx];
        int lv = net_levels_[gate.output];
        if (lv < 0) {
            throw std::runtime_error("Gate output net has no level");
        }
        gates_by_level_[lv].push_back(gate_idx);
    }

    /*for (int lv = 0; lv <= max_level_; ++lv) {
        std::cerr << "Level " << lv << ": ";
        for (auto gate_idx : gates_by_level_[lv]) {
            const auto& gate = gates[gate_idx];
            std::cerr << "(gate_idx=" << gate_idx << ", output_net=" << gate.output << ", gate_name: "<<gates[gate_idx].name<<") ";
        }
        std::cerr <<'\n';
    }
    std::cerr<<'\n';*/
}

int LevelizedBaselineSimulator::evaluateGate(const core::Gate& gate,
                                             const std::vector<int>& values) const {

    auto logical_not = [](int v) { return v ? 0 : 1; };
    switch (gate.type) {
        case core::GateType::And: {
            int result = 1;
            for (auto net : gate.inputs) {
                const int value = values[net];
                if (value == -1) {
                    throw std::runtime_error("Unresolved net during AND evaluation");
                }
                result &= value;
            }
            return result;
        }
        case core::GateType::Nand: {
            int result = 1;
            for (auto net : gate.inputs) {
                const int value = values[net];
                if (value == -1) {
                    throw std::runtime_error("Unresolved net during NAND evaluation");
                }
                result &= value;
            }
            return logical_not(result);
        }
        case core::GateType::Or: {
            int result = 0;
            for (auto net : gate.inputs) {
                const int value = values[net];
                if (value == -1) {
                    throw std::runtime_error("Unresolved net during OR evaluation");
                }
                result |= value;
            }
            return result;
        }
        case core::GateType::Nor: {
            int result = 0;
            for (auto net : gate.inputs) {
                const int value = values[net];
                if (value == -1) {
                    throw std::runtime_error("Unresolved net during NOR evaluation");
                }
                result |= value;
            }
            return logical_not(result);
        }
        case core::GateType::Xor: {
            int result = 0;
            for (auto net : gate.inputs) {
                const int value = values[net];
                if (value == -1) {
                    throw std::runtime_error("Unresolved net during XOR evaluation");
                }
                result ^= value;
            }
            return result;
        }
        case core::GateType::Xnor: {
            int result = 0;
            for (auto net : gate.inputs) {
                const int value = values[net];
                if (value == -1) {
                    throw std::runtime_error("Unresolved net during XNOR evaluation");
                }
                result ^= value;
            }
            return logical_not(result);
        }
        case core::GateType::Not: {
            if (gate.inputs.size() != 1) {
                throw std::runtime_error("NOT gate expects exactly one input");
            }
            const int value = values[gate.inputs.front()];
            if (value == -1) {
                throw std::runtime_error("Unresolved net during NOT evaluation");
            }
            return logical_not(value);
        }
        case core::GateType::Buf: {
            if (gate.inputs.size() != 1) {
                throw std::runtime_error("BUF gate expects exactly one input");
            }
            const int value = values[gate.inputs.front()];
            if (value == -1) {
                throw std::runtime_error("Unresolved net during BUF evaluation");
            }
            return value;
        }
        case core::GateType::Unknown:
        default:
            throw std::runtime_error("Encountered unknown gate type during evaluation");
    }
}

bool LevelizedBaselineSimulator::simulateFault(
    const core::Pattern& pattern,
    const std::unordered_map<core::NetId, int>& provided_outputs,
    core::NetId fault_net,
    int stuck_value,
    std::vector<int>& working_values) const {
    if (fault_net >= net_count_) {
        throw std::runtime_error("Fault references unknown net");
    }
    if (stuck_value != 0 && stuck_value != 1) {
        throw std::runtime_error("Fault value must be 0 or 1");
    }

    working_values.assign(net_count_, -1);
    for (const auto& entry : pattern.assignments) {
        if (entry.net >= net_count_) {
            throw std::runtime_error("Pattern references unknown net");
        }
        if (entry.value != 0 && entry.value != 1) {
            throw std::runtime_error("Pattern contains non-binary value");
        }
        working_values[entry.net] = entry.value;
    }

    if (fault_net < working_values.size()) {
        working_values[fault_net] = stuck_value;
    }

    const auto& gates = circuit_.gates();
    for (int lv=1; lv<=max_level_; ++lv) {
        const auto& level_gates = gates_by_level_[lv];

        for (std::size_t i = 0; i < level_gates.size(); ++i) {
            auto gate_idx = level_gates[i];
            const auto& gate = gates[gate_idx];
            if (gate.output == fault_net) continue;
            int gate_value = evaluateGate(gate, working_values);
            working_values[gate.output] = gate_value;
        }
    }

    for (auto po_net : primary_outputs_) {
        if (po_net >= working_values.size() || working_values[po_net] == -1) {
            throw std::runtime_error("Unable to resolve primary output during fault simulation");
        }
        const auto it = provided_outputs.find(po_net);
        if (it == provided_outputs.end()) {
            throw std::runtime_error("Missing expected value for primary output");
        }
        if (working_values[po_net] != it->second) {
            return false;
        }
    }
    return true;
}

void LevelizedBaselineSimulator::start() {
    std::vector<int> working_values;
    //std::cerr << "In LEVELIZEDBASELINE" << '\n';

    for (std::size_t pattern_idx = 0; pattern_idx < rows_.size(); ++pattern_idx) {
        const auto& row      = rows_[pattern_idx];
        const auto& pattern  = row.pattern;
        const auto& expected = row.provided_outputs;

        working_values.resize(net_count_);

        for (core::NetId net = 0; net < net_count_; ++net) {
            const bool stuck0_eq = simulateFault(pattern, expected, net, 0, working_values);
            const bool stuck1_eq = simulateFault(pattern, expected, net, 1, working_values);

            answers.set(pattern_idx, net, true,  stuck0_eq);
            answers.set(pattern_idx, net, false, stuck1_eq);
        }
    }
}

}  // namespace algorithm
