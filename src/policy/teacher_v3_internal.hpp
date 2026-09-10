#pragma once

#include <string>

#include "ygo/policy/teacher_v3.hpp"

namespace ygo::policy::detail {

bool validate_teacher_policy_session_v3(
    const teacher::StrategyProfileV1& profile,
    const teacher::TeacherPolicyBindingV1& policy_binding,
    const trajectory::PolicyArtifact& artifact,
    const trajectory::ParticipantPolicyAssignment& assignment,
    std::string* diagnostic = nullptr) noexcept;

}  // namespace ygo::policy::detail
