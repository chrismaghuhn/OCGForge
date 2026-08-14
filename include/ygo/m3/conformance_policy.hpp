#pragma once

#include <cstdint>
#include <vector>

#include "ygo/protocol/continuation.hpp"

namespace ygo::m3 {

// Test-only deterministic policy. It consumes only the decoded legal domain;
// it is not a gameplay Teacher and has no engine/private-state access.
class DeterministicConformancePolicy final {
public:
    explicit DeterministicConformancePolicy(std::vector<std::uint32_t> focus_codes = {},
                                            bool completion_mode = false);

    const protocol::ActionCandidate& choose(const protocol::DecisionRequest& request) const;

private:
    std::vector<std::uint32_t> focus_codes_;
    bool completion_mode_ = false;
};

}  // namespace ygo::m3
