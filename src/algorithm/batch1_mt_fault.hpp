#pragma once

#include "algorithm/fault_simulator.hpp"
#include "core/circuit.hpp"
#include <unordered_map>
#include <vector>

namespace algorithm {

// Single-pattern baseline DFS with OpenMP parallelization across fault wires.
class Batch1MtFaultSimulator : public FaultSimulator {
public:
    Batch1MtFaultSimulator(const core::Circuit& circuit, const std::vector<io::PatternRow>& rows,
                           int num_threads = 4);
    ~Batch1MtFaultSimulator() override = default;

    void start() override;

private:
    std::vector<int> net_to_gate_;
    int num_threads_{4};
};

}  // namespace algorithm
