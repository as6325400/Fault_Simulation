#pragma once

#include <string>
#include <vector>

#include "algorithm/fault_simulator.hpp"
#include "core/circuit.hpp"
#include "io/pattern_loader.hpp"

namespace io {

void writeAnswerFile(const algorithm::FaultSimulator& simulator, const std::string& output_path);

}  // namespace io
