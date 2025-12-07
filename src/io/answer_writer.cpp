#include "io/answer_writer.hpp"

#include <fstream>
#include <stdexcept>

namespace io {

void writeAnswerFile(const algorithm::FaultSimulator& simulator, const std::string& output_path) {
    const auto& nets = simulator.netNames();

    std::ofstream output(output_path, std::ios::binary);
    if (!output) {
        throw std::runtime_error("Unable to open output file: " + output_path);
    }

    output << "# pattern_index net stuck_at_0_eq stuck_at_1_eq\n";

    const std::size_t pattern_count = simulator.patternCount();
    for (std::size_t i = 0; i < pattern_count; ++i) {
        if (!simulator.answers.has(i)) {
            throw std::runtime_error("Answer table missing data for pattern " + std::to_string(i));
        }
        const auto& fault_results_raw = simulator.answers.get(i);
        std::vector<algorithm::FaultEvaluation> fault_results = fault_results_raw;
        if (fault_results.size() < nets.size()) {
            throw std::runtime_error("Answer size mismatch for pattern " + std::to_string(i));
        }

        for (std::size_t net_id = 0; net_id < nets.size(); ++net_id) {
            output << i << ' ' << nets[net_id] << ' '
                   << (fault_results[net_id].stuck0_eq ? 1 : 0) << ' '
                   << (fault_results[net_id].stuck1_eq ? 1 : 0) << '\n';
        }
    }
}

}  // namespace io
