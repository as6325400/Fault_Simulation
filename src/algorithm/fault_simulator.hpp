#pragma once

#include <stdexcept>
#include <string>
#include <vector>

#include "algorithm/fault_types.hpp"
#include "core/pattern_generator.hpp"
#include "io/pattern_loader.hpp"

namespace algorithm {

struct AnswerTable {
    std::size_t net_count{0};
    std::vector<std::vector<FaultEvaluation>> table;
    std::vector<std::vector<uint8_t>> filled_mask;
    std::vector<std::size_t> filled_counts;

    void init(std::size_t patterns, std::size_t nets) {
        net_count = nets;
        table.assign(patterns, std::vector<FaultEvaluation>(nets));
        filled_mask.assign(patterns, std::vector<uint8_t>(nets, 0));
        filled_counts.assign(patterns, 0);
    }

    bool has(std::size_t pattern_index) const {
        return pattern_index < filled_counts.size() &&
               filled_counts[pattern_index] == net_count * 2;
    }

    const std::vector<FaultEvaluation>& get(std::size_t pattern_index) const {
        if (!has(pattern_index)) {
            throw std::runtime_error("Answer table missing entry for pattern");
        }
        return table[pattern_index];
    }

    void set(std::size_t pattern_index, std::size_t net_id, bool stuck_at_0, bool equal) {
        if (pattern_index >= table.size()) {
            throw std::runtime_error("Pattern index out of range for answer table");
        }
        if (net_id >= net_count) {
            throw std::runtime_error("Net index out of range for answer table");
        }
        auto& entry = table[pattern_index][net_id];
        const uint8_t bit = stuck_at_0 ? uint8_t{1} : uint8_t{2};
        uint8_t& mask = filled_mask[pattern_index][net_id];
        if (!(mask & bit)) {
            ++filled_counts[pattern_index];
            mask |= bit;
        }
        if (stuck_at_0) {
            entry.stuck0_eq = equal;
        } else {
            entry.stuck1_eq = equal;
        }
    }

    void clear() {
        net_count = 0;
        table.clear();
        filled_mask.clear();
        filled_counts.clear();
    }
};

class FaultSimulator {
public:
    FaultSimulator(const core::Circuit& circuit, const std::vector<io::PatternRow>& rows)
        : circuit_(circuit), rows_(rows), net_names_(circuit.netNames()) {
        answers.init(rows_.size(), net_names_.size());
    }

    virtual ~FaultSimulator() = default;

    const std::vector<std::string>& netNames() const { return net_names_; }

    virtual void start() = 0;

    std::size_t patternCount() const { return rows_.size(); }

    const core::Pattern& patternAt(std::size_t index) const {
        return rows_[index].pattern;
    }

    mutable AnswerTable answers;

protected:
    const core::Circuit& circuit_;
    const std::vector<io::PatternRow>& rows_;
    std::vector<core::Pattern> patterns_cache_;
    std::vector<std::string> net_names_;
};

}  // namespace algorithm
