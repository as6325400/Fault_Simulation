#pragma once

#include "algorithm/fault_simulator.hpp"
#include "core/circuit.hpp"
#include <vector>

namespace algorithm {

// 64-pattern bitset simulator with OpenMP parallelization over fault wires.
class Batch64MtFaultSimulator : public FaultSimulator {
public:
    Batch64MtFaultSimulator(const core::Circuit& circuit, const std::vector<io::PatternRow>& rows,
                            int num_threads = 4);
    ~Batch64MtFaultSimulator() override = default;

    void start() override;

private:
    std::vector<int> net_to_gate_;
    std::vector<int> output_index_by_net_;
    int num_threads_{4};
};

}  // namespace algorithm
