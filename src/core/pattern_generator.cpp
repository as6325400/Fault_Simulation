#include "core/pattern_generator.hpp"

#include <sstream>

namespace core {

PatternGenerator::PatternGenerator(const Circuit& circuit, std::uint64_t seed)
    : circuit_(circuit), rng_(seed) {}

Pattern PatternGenerator::nextPattern() {
    Pattern pattern;
    const auto& inputs = circuit_.primaryInputs();
    pattern.assignments.reserve(inputs.size());
    for (const auto& input : inputs) {
        pattern.assignments.push_back({input, bit_dist_(rng_)});
    }
    return pattern;
}

std::vector<Pattern> PatternGenerator::generate(std::size_t count) {
    std::vector<Pattern> patterns;
    patterns.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        patterns.push_back(nextPattern());
    }
    return patterns;
}

std::string Pattern::toString() const {
    std::ostringstream oss;
    for (std::size_t i = 0; i < assignments.size(); ++i) {
        if (i != 0) {
            oss << ", ";
        }
        oss << assignments[i].net << '=' << assignments[i].value;
    }
    return oss.str();
}

}  // namespace core
