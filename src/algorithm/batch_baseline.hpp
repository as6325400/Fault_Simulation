#pragma once

#include "algorithm/fault_simulator.hpp"
#include "core/circuit.hpp"
#include <vector>

namespace algorithm {

class BatchBaselineSimulator : public FaultSimulator {
public:
    BatchBaselineSimulator(const core::Circuit& circuit, const std::vector<io::PatternRow>& rows);
    ~BatchBaselineSimulator() override = default;
    void start() override;
    bool simulate(std::size_t pattern_id, core::NetId fault_wire, bool stuck_at_0,
                  const std::unordered_map<core::NetId, int>& provided_outputs);

private:
    std::vector<int> net_to_gate_;
};

}
