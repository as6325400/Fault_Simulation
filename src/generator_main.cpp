// Standalone pattern generator CLI.

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "algorithm/baseline_simulator.hpp"
#include "algorithm/bit_parallel_simulator.hpp"
#include "core/pattern_generator.hpp"
#include "io/answer_writer.hpp"
#include "io/circuit_parser.hpp"

namespace {

void printUsage(const char* program) {
    std::cerr << "Usage: " << program << " <circuit> [pattern-count=100] [seed=42]\n";
    std::cerr << "  circuit: basename or .v file located under testcases/\n";
}

bool endsWith(const std::string& value, const std::string& suffix) {
    if (suffix.size() > value.size()) {
        return false;
    }
    return std::equal(suffix.rbegin(), suffix.rend(), value.rbegin());
}

std::string circuitFileName(const std::string& arg) {
    if (endsWith(arg, ".v")) {
        return arg;
    }
    return arg + ".v";
}

std::string circuitBaseName(const std::string& file_name) {
    if (endsWith(file_name, ".v")) {
        return file_name.substr(0, file_name.size() - 2);
    }
    return file_name;
}

std::string computeSha256(const std::string& file_path) {
    const std::vector<std::string> commands = {"sha256sum ", "shasum -a 256 "};
    for (const auto& base : commands) {
        std::string command = base + file_path;
        FILE* handle = popen(command.c_str(), "r");
        if (!handle) {
            continue;
        }
        std::string output;
        char buffer[256];
        while (fgets(buffer, sizeof(buffer), handle) != nullptr) {
            output += buffer;
        }
        const int status = pclose(handle);
        if (status != 0) {
            continue;
        }
        std::istringstream iss(output);
        std::string digest;
        if (iss >> digest) {
            return digest;
        }
    }
    throw std::runtime_error(
        "Unable to compute SHA-256 digest; sha256sum/shasum commands not found.");
}

void writeShaFile(const std::string& file_path, const std::string& sha_path) {
    const std::string digest = computeSha256(file_path);
    std::ofstream sha_file(sha_path);
    if (!sha_file) {
        throw std::runtime_error("Failed to open SHA output file: " + sha_path);
    }
    sha_file << digest << '\n';
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        printUsage(argv[0]);
        return EXIT_FAILURE;
    }

    const std::string circuit_arg = argv[1];
    std::size_t pattern_count = 100;
    std::uint64_t seed = 42;
    if (argc >= 3) {
        pattern_count = std::stoull(argv[2]);
    }
    if (argc >= 4) {
        seed = std::stoull(argv[3]);
    }
    if (argc > 4) {
        printUsage(argv[0]);
        return EXIT_FAILURE;
    }

    const std::string circuit_file = circuitFileName(circuit_arg);
    const std::string circuit_path = "testcases/" + circuit_file;
    const std::string output_path = "testcases/" + circuitBaseName(circuit_file) + ".in";

    try {
        auto circuit = io::parseCircuit(circuit_path);
        core::PatternGenerator generator(circuit, seed);
        auto patterns = generator.generate(pattern_count);

        std::ofstream output(output_path);
        if (!output) {
            throw std::runtime_error("Failed to open output file for writing: " + output_path);
        }
        const auto& outputs = circuit.primaryOutputs();
        std::vector<io::PatternRow> rows;
        rows.reserve(patterns.size());
        algorithm::BaselineSimulator baseline(circuit, rows);
        algorithm::BitParallelSimulator bit(circuit, rows);

        for (std::size_t i = 0; i < patterns.size(); ++i) {
            output << patterns[i].toString(circuit) << " | ";
            io::PatternRow row;
            row.pattern = patterns[i];
            const auto golden_outputs = baseline.simulateOutputs(patterns[i]);
            for (std::size_t j = 0; j < outputs.size(); ++j) {
                const std::string& net_name = circuit.netName(outputs[j]);
                output << net_name << '=' << golden_outputs[j];
                row.provided_outputs[outputs[j]] = golden_outputs[j];
                if (j + 1 != outputs.size()) {
                    output << ", ";
                }
            }
            output << '\n';
            rows.push_back(std::move(row));
        }
        std::cout << "Wrote " << patterns.size() << " patterns for " << circuit_file << " to "
                  << output_path << '\n';

        bit.start();

        const std::string ans_path = "testcases/" + circuitBaseName(circuit_file) + ".ans";
        io::writeAnswerFile(bit, ans_path);
        std::cout << "Wrote fault answers to " << ans_path << '\n';
        const std::string sha_path = ans_path + ".sha";
        writeShaFile(ans_path, sha_path);
        std::cout << "Wrote SHA digest to " << sha_path << '\n';
    } catch (const std::exception& ex) {
        std::cerr << "Error: " << ex.what() << '\n';
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
