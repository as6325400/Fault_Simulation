#include "io/circuit_parser.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

std::string trim(const std::string& text) {
    std::size_t start = 0;
    while (start < text.size() && std::isspace(static_cast<unsigned char>(text[start]))) {
        ++start;
    }
    std::size_t end = text.size();
    while (end > start && std::isspace(static_cast<unsigned char>(text[end - 1]))) {
        --end;
    }
    return text.substr(start, end - start);
}

std::string toLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

void stripTrailingSemicolon(std::string& text) {
    if (!text.empty() && text.back() == ';') {
        text.pop_back();
    }
}

std::vector<std::string> splitCommaSeparated(const std::string& payload) {
    std::vector<std::string> tokens;
    std::string token;
    std::stringstream ss(payload);
    while (std::getline(ss, token, ',')) {
        token = trim(token);
        if (!token.empty()) {
            tokens.push_back(token);
        }
    }
    return tokens;
}

}  // namespace

namespace io {

core::Circuit parseCircuit(const std::string& file_path) {
    std::ifstream input(file_path);
    if (!input) {
        throw std::runtime_error("Unable to open circuit file: " + file_path);
    }

    core::Circuit circuit;
    std::string pending_statement;
    std::string line;
    while (std::getline(input, line)) {
        const auto comment_pos = line.find("//");
        if (comment_pos != std::string::npos) {
            line = line.substr(0, comment_pos);
        }
        line = trim(line);
        if (line.empty()) {
            continue;
        }

        std::string lower = toLower(line);
        if (lower == "endmodule") {
            break;
        }

        pending_statement += (pending_statement.empty() ? "" : " ") + line;
        if (line.find(';') == std::string::npos) {
            continue;
        }

        std::string statement = pending_statement;
        pending_statement.clear();

        std::string lowered_statement = toLower(statement);
        if (lowered_statement.rfind("module", 0) == 0) {
            std::string declaration = trim(statement.substr(6));
            stripTrailingSemicolon(declaration);
            const auto paren_pos = declaration.find('(');
            if (paren_pos == std::string::npos) {
                throw std::runtime_error("Malformed module declaration: " + statement);
            }
            std::string module_name = trim(declaration.substr(0, paren_pos));
            circuit.setName(module_name);
            continue;
        }

        if (lowered_statement.rfind("input", 0) == 0) {
            std::string nets = trim(statement.substr(5));
            stripTrailingSemicolon(nets);
            for (const auto& net : splitCommaSeparated(nets)) {
                circuit.addPrimaryInput(net);
            }
            continue;
        }

        if (lowered_statement.rfind("output", 0) == 0) {
            std::string nets = trim(statement.substr(6));
            stripTrailingSemicolon(nets);
            for (const auto& net : splitCommaSeparated(nets)) {
                circuit.addPrimaryOutput(net);
            }
            continue;
        }

        if (lowered_statement.rfind("wire", 0) == 0) {
            std::string nets = trim(statement.substr(4));
            stripTrailingSemicolon(nets);
            for (const auto& net : splitCommaSeparated(nets)) {
                circuit.addWire(net);
            }
            continue;
        }

        // Remaining statements should describe gates.
        stripTrailingSemicolon(statement);
        const auto open_paren = statement.find('(');
        const auto close_paren = statement.rfind(')');
        if (open_paren == std::string::npos || close_paren == std::string::npos ||
            close_paren <= open_paren) {
            throw std::runtime_error("Malformed gate connection block: " + statement);
        }

        std::string header = trim(statement.substr(0, open_paren));
        std::stringstream header_ss(header);
        std::string gate_type_str;
        std::string gate_name;
        if (!(header_ss >> gate_type_str >> gate_name)) {
            throw std::runtime_error("Unable to parse gate line: " + statement);
        }

        auto connection_payload =
            statement.substr(open_paren + 1, close_paren - open_paren - 1);
        auto nets = splitCommaSeparated(connection_payload);
        if (nets.size() < 2) {
            throw std::runtime_error("Gate must have an output and at least one input: " +
                                     statement);
        }

        core::Gate gate;
        gate.type = core::gateTypeFromString(gate_type_str);
        gate.name = gate_name;
        gate.output = circuit.ensureNet(nets.front(), core::NetType::Wire);
        for (std::size_t i = 1; i < nets.size(); ++i) {
            gate.inputs.push_back(circuit.ensureNet(nets[i], core::NetType::Wire));
        }
        circuit.addGate(gate);
    }

    if (circuit.name().empty()) {
        throw std::runtime_error("Circuit missing module declaration in " + file_path);
    }
    circuit.finalizeNets();
    return circuit;
}

}  // namespace io
