#include "ygo/policy/policy.hpp"

#include "ygo/teacher/teacher_core.hpp"
#include "ygo/teacher/teacher_decision.hpp"
#include "ygo/teacher/teacher_explanation.hpp"

#include <type_traits>
#include <utility>

using ExpectedTeacherProposeSignature = ygo::teacher::TeacherRankingResult (
    ygo::teacher::TeacherCore::*)(
        const ygo::policy::PolicyInput&,
        const ygo::teacher::StrategyProfileV1&,
        const ygo::teacher::EpisodeLocalStrategyStateV1&) const;

static_assert(std::is_same_v<
              decltype(std::declval<const ygo::policy::PolicyInput&>().observation),
              const ygo::environment::PublicEnvironmentObservation&>);
static_assert(std::is_same_v<
              decltype(std::declval<const ygo::policy::PolicyInput&>().candidates),
              const std::vector<ygo::environment::EnvironmentActionCandidate>&>);
static_assert(std::is_same_v<decltype(&ygo::teacher::TeacherCore::propose),
                             ExpectedTeacherProposeSignature>);
static_assert(std::is_default_constructible_v<ygo::teacher::TeacherCore>);
static_assert(std::is_default_constructible_v<ygo::teacher::TeacherRankingResult>);

int main() {
    return 0;
}
