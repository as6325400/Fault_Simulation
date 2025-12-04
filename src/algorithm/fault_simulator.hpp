#pragma once

#include <string>
#include <vector>

#include "algorithm/fault_types.hpp"
#include "core/pattern_generator.hpp"

namespace algorithm {

class FaultSimulator {
public:
    virtual ~FaultSimulator() = default;

    virtual const std::vector<std::string>& netNames() const = 0;
    virtual std::vector<FaultEvaluation> evaluate(const core::Pattern& pattern) const = 0;
};

}  // namespace algorithm
