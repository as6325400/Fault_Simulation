#pragma once

#include <string>

#include "core/circuit.hpp"

namespace io {

core::Circuit parseCircuit(const std::string& file_path);

}  // namespace io
