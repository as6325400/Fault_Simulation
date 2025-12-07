#pragma once

#include <string>
#include <vector>

#include "core/pattern_generator.hpp"

namespace io {

std::vector<core::Pattern> parsePatternFile(const core::Circuit& circuit,
                                            const std::string& file_path);

}  // namespace io
