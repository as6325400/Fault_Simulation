// GPU fault simulation front-end mirroring main.cpp but using CUDA simulators.

#include <algorithm>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <string>

#include "algorithm/batch1_gpu_fault.hpp"
#include "algorithm/batch32_gpu_fault.hpp"
#include "io/answer_writer.hpp"
#include "io/circuit_parser.hpp"
#include "io/pattern_loader.hpp"

namespace {

bool endsWith(const std::string& text, const std::string& suffix) {
    if (suffix.size() > text.size()) {
        return false;
    }
    return std::equal(suffix.rbegin(), suffix.rend(), text.rbegin());
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

void printUsage(const char* program) {
    std::cerr << "Usage: " << program << " <circuit> <output-path>\n";
    std::cerr << "  circuit: testcase basename or .v file under testcases/\n";
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 3) {
        printUsage(argv[0]);
        return EXIT_FAILURE;
    }

    const std::string circuit_arg = argv[1];
    const std::string output_path = argv[2];

    try {
        const std::string circuit_file = circuitFileName(circuit_arg);
        const std::string base_name = circuitBaseName(circuit_file);
        const std::string circuit_path = "testcases/" + circuit_file;
        const std::string pattern_path = "testcases/" + base_name + ".in";

        auto circuit = io::parseCircuit(circuit_path);
        auto rows = io::loadPatterns(circuit, pattern_path);

#ifdef BATCH1GPU
        algorithm::Batch1GpuFaultSimulator simulator(circuit, rows);
#else
        algorithm::Batch32GpuFaultSimulator simulator(circuit, rows);
#endif

        std::cout << simulator.describeIOShape() << '\n';
        simulator.start();
        io::writeAnswerFile(simulator, output_path);

    } catch (const std::exception& ex) {
        std::cerr << "Error: " << ex.what() << '\n';
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
