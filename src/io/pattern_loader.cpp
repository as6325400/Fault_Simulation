#include "io/pattern_loader.hpp"

#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>

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

std::vector<std::string> splitAssignments(const std::string& section) {
    std::vector<std::string> tokens;
    std::string token;
    std::stringstream ss(section);
    while (std::getline(ss, token, ',')) {
        token = trim(token);
        if (!token.empty()) {
            tokens.push_back(token);
        }
    }
    return tokens;
}

int parseBit(const std::string& value) {
    if (value == "0") {
        return 0;
    }
    if (value == "1") {
        return 1;
    }
    throw std::runtime_error("Invalid bit value: " + value);
}

core::Pattern parsePatternSection(const std::string& section, const core::Circuit& circuit) {
    core::Pattern pattern;
    for (const auto& token : splitAssignments(section)) {
        const auto eq = token.find('=');
        if (eq == std::string::npos) {
            throw std::runtime_error("Assignment missing '=': " + token);
        }
        core::PatternEntry entry;
        const std::string net_name = trim(token.substr(0, eq));
        entry.net = circuit.netId(net_name);
        if (entry.net == std::numeric_limits<core::NetId>::max()) {
            throw std::runtime_error("Unknown net in pattern: " + net_name);
        }
        const std::string value_str = trim(token.substr(eq + 1));
        entry.value = parseBit(value_str);
        if (net_name.empty()) {
            throw std::runtime_error("Empty net name in assignment: " + token);
        }
        pattern.assignments.push_back(entry);
    }
    if (pattern.assignments.empty()) {
        throw std::runtime_error("Pattern line missing assignments");
    }
    return pattern;
}

std::unordered_map<core::NetId, int> parseOutputSection(const std::string& section,
                                                        const core::Circuit& circuit) {
    std::unordered_map<core::NetId, int> outputs;
    if (section.empty()) {
        return outputs;
    }
    for (const auto& token : splitAssignments(section)) {
        const auto eq = token.find('=');
        if (eq == std::string::npos) {
            throw std::runtime_error("Output assignment missing '=': " + token);
        }
        const std::string net_name = trim(token.substr(0, eq));
        const auto net = circuit.netId(net_name);
        if (net == std::numeric_limits<core::NetId>::max()) {
            throw std::runtime_error("Unknown net in output assignment: " + net_name);
        }
        const std::string value_str = trim(token.substr(eq + 1));
        if (net_name.empty()) {
            throw std::runtime_error("Empty net name in output assignment: " + token);
        }
        outputs[net] = parseBit(value_str);
    }
    return outputs;
}

}  // namespace

namespace io {

std::vector<PatternRow> loadPatterns(const core::Circuit& circuit, const std::string& path) {
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("Unable to open pattern file: " + path);
    }

    std::vector<PatternRow> rows;
    std::string line;
    while (std::getline(input, line)) {
        line = trim(line);
        if (line.empty()) {
            continue;
        }
        PatternRow row;
        const auto pipe_pos = line.find('|');
        std::string pattern_section = pipe_pos == std::string::npos ? line : line.substr(0, pipe_pos);
        std::string output_section;
        if (pipe_pos != std::string::npos) {
            output_section = line.substr(pipe_pos + 1);
        }
        row.pattern = parsePatternSection(pattern_section, circuit);
        row.provided_outputs = parseOutputSection(output_section, circuit);
        rows.push_back(std::move(row));
    }

    if (rows.empty()) {
        throw std::runtime_error("Pattern file contains no patterns: " + path);
    }
    return rows;
}

}  // namespace io
