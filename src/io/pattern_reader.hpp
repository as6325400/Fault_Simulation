#pragma once

#include <string>
#include <vector>

#include "core/pattern_generator.hpp"

namespace io {

std::vector<core::Pattern> parsePatternFile(const std::string& file_path);

}  // namespace io
