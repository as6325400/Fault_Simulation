#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "core/pattern_generator.hpp"

namespace io {

struct PatternRow {
    core::Pattern pattern;
    std::unordered_map<std::string, int> provided_outputs;
};

std::vector<PatternRow> loadPatterns(const std::string& path);

}  // namespace io
