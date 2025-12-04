#include "io/pattern_reader.hpp"

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

std::string removeWhitespace(const std::string& text) {
    std::string cleaned;
    cleaned.reserve(text.size());
    for (char ch : text) {
        if (!std::isspace(static_cast<unsigned char>(ch))) {
            cleaned.push_back(ch);
        }
    }
    return cleaned;
}

core::Pattern parsePatternLine(const std::string& line) {
    core::Pattern pattern;
    std::stringstream ss(line);
    std::string section;
    while (std::getline(ss, section, ',')) {
        section = trim(section);
        if (section.empty()) {
            continue;
        }
        section = removeWhitespace(section);
        const auto eq = section.find('=');
        if (eq == std::string::npos) {
            throw std::runtime_error("Invalid pattern token (missing '='): " + section);
        }
        const std::string net = section.substr(0, eq);
        const std::string value_str = section.substr(eq + 1);
        if (net.empty() || value_str.empty()) {
            throw std::runtime_error("Invalid pattern token: " + section);
        }
        if (value_str != "0" && value_str != "1") {
            throw std::runtime_error("Pattern values must be 0 or 1 for net " + net);
        }
        pattern.assignments.push_back({net, value_str == "1" ? 1 : 0});
    }

    if (pattern.assignments.empty()) {
        throw std::runtime_error("Empty pattern line encountered");
    }
    return pattern;
}

std::string stripComments(const std::string& line) {
    const auto slash_pos = line.find("//");
    const auto hash_pos = line.find('#');
    std::size_t cut_pos = std::string::npos;
    if (slash_pos != std::string::npos && hash_pos != std::string::npos) {
        cut_pos = std::min(slash_pos, hash_pos);
    } else if (slash_pos != std::string::npos) {
        cut_pos = slash_pos;
    } else if (hash_pos != std::string::npos) {
        cut_pos = hash_pos;
    }
    if (cut_pos == std::string::npos) {
        return line;
    }
    return line.substr(0, cut_pos);
}

}  // namespace

namespace io {

std::vector<core::Pattern> parsePatternFile(const std::string& file_path) {
    std::ifstream input(file_path);
    if (!input) {
        throw std::runtime_error("Unable to open pattern file: " + file_path);
    }

    std::vector<core::Pattern> patterns;
    std::string line;
    while (std::getline(input, line)) {
        line = stripComments(line);
        line = trim(line);
        if (line.empty()) {
            continue;
        }
        patterns.push_back(parsePatternLine(line));
    }

    if (patterns.empty()) {
        throw std::runtime_error("Pattern file does not contain any patterns: " + file_path);
    }
    return patterns;
}

}  // namespace io
