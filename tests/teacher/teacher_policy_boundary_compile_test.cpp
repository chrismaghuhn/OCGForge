#include "ygo/policy/policy.hpp"

#include "ygo/teacher/teacher_core.hpp"
#include "ygo/teacher/teacher_decision.hpp"
#include "ygo/teacher/teacher_explanation.hpp"

#include <type_traits>

static_assert(std::is_same_v<
              decltype(std::declval<const ygo::policy::PolicyInput&>().observation),
              const ygo::environment::PublicEnvironmentObservation&>);
static_assert(std::is_same_v<
              decltype(std::declval<const ygo::policy::PolicyInput&>().candidates),
              const std::vector<ygo::environment::EnvironmentActionCandidate>&>);
static_assert(std::is_default_constructible_v<ygo::teacher::TeacherCore>);
static_assert(std::is_default_constructible_v<ygo::teacher::TeacherRankingResult>);

int main() {
    return 0;
}
