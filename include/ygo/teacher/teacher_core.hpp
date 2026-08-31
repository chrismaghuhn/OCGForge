#pragma once

#include "ygo/teacher/strategy_profile.hpp"
#include "ygo/teacher/teacher_decision.hpp"

namespace ygo::policy {
struct PolicyInput;
}

namespace ygo::teacher {

struct EpisodeLocalStrategyStateV1;

class TeacherCore final {
public:
    TeacherCore() = default;

    // Task 3 freezes the public-only declaration. Ranking, state
    // reconciliation, and fallback are implemented by later tasks.
    TeacherRankingResult propose(
        const ygo::policy::PolicyInput& input,
        const StrategyProfileV1& profile,
        const EpisodeLocalStrategyStateV1& state) const;
};

}  // namespace ygo::teacher
