#pragma once

#include "ygo/teacher/strategy_profile.hpp"
#include "ygo/teacher/strategy_state_v2.hpp"
#include "ygo/teacher/teacher_decision_v2.hpp"

namespace ygo::policy {
struct PolicyInput;
}

namespace ygo::teacher {

class TeacherCoreV2 final {
public:
    TeacherCoreV2() = default;

    TeacherRankingResultV2 propose(
        const ygo::policy::PolicyInput& input,
        const StrategyProfileV1& profile,
        const EpisodeLocalStrategyStateV2& state) const;
};

}  // namespace ygo::teacher
