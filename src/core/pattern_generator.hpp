#pragma once

#include <cstdint>
#include <random>
#include <string>
#include <utility>
#include <vector>

#include "core/circuit.hpp"

namespace core {

struct PatternEntry {
    NetId net{};
    int value{};
};

struct Pattern {
    std::vector<PatternEntry> assignments;

    std::string toString(const Circuit& circuit) const;
};

class PatternGenerator {
public:
    PatternGenerator(const Circuit& circuit, std::uint64_t seed);

    Pattern nextPattern();
    std::vector<Pattern> generate(std::size_t count);

private:
    const Circuit& circuit_;
    std::mt19937_64 rng_;
    std::uniform_int_distribution<int> bit_dist_{0, 1};
};

}  // namespace core
