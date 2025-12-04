#include "io/answer_writer.hpp"

#include <fstream>
#include <stdexcept>

namespace {

std::vector<int> extractOutputs(const io::PatternRow& row, const core::Circuit& circuit) {
    std::vector<int> outputs;
    const auto& po = circuit.primaryOutputs();
    outputs.reserve(po.size());
    for (const auto& net : po) {
        const auto it = row.provided_outputs.find(net);
        if (it == row.provided_outputs.end()) {
            throw std::runtime_error("Pattern missing golden output for net " + net);
        }
        outputs.push_back(it->second);
    }
    return outputs;
}

}  // namespace

namespace io {

void writeAnswerFile(const core::Circuit& circuit, const std::vector<PatternRow>& rows,
                     const algorithm::FaultSimulator& simulator,
                     const std::string& output_path) {
    const auto& nets = simulator.netNames();

    std::ofstream output(output_path);
    if (!output) {
        throw std::runtime_error("Unable to open output file: " + output_path);
    }

    output << "# pattern_index net stuck_at_0_eq stuck_at_1_eq\n";

    for (std::size_t i = 0; i < rows.size(); ++i) {
        const auto golden_outputs = extractOutputs(rows[i], circuit);
        const auto fault_results = simulator.evaluate(rows[i].pattern);
        if (fault_results.size() != nets.size()) {
            throw std::runtime_error("Bit-parallel simulation returned inconsistent result size");
        }

        for (std::size_t net_idx = 0; net_idx < nets.size(); ++net_idx) {
            output << i << ' ' << nets[net_idx] << ' '
                   << (fault_results[net_idx].stuck0_eq ? 1 : 0) << ' '
                   << (fault_results[net_idx].stuck1_eq ? 1 : 0) << '\n';
        }
    }
}

}  // namespace io
