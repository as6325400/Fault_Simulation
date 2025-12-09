#pragma once

#include "algorithm/fault_simulator.hpp"
#include "core/circuit.hpp"
#include <unordered_map>
#include <vector>

namespace algorithm {

class Batch64BaselineSimulator : public FaultSimulator {
public:
    Batch64BaselineSimulator(const core::Circuit& circuit,
                             const std::vector<io::PatternRow>& rows);
    ~Batch64BaselineSimulator() override = default;

    void start() override;

private:
    std::vector<int> net_to_gate_;
    std::vector<int> output_index_by_net_;
};

}  // namespace algorithm
