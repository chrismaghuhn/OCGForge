#pragma once

#include "ygo/policy/production_provenance.hpp"
#include "ygo/teacher/strategy_profile.hpp"
#include "ygo/trajectory/types.hpp"

namespace ygo::policy {

teacher::TeacherPolicyBindingV1 make_teacher_policy_binding_v2(
    const teacher::StrategyProfileV1& profile);

trajectory::PolicyArtifact make_teacher_policy_artifact_v2(
    const teacher::StrategyProfileV1& profile);

}  // namespace ygo::policy
