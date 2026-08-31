#pragma once

#include <cstdint>
#include <vector>

#include "ygo/teacher/strategy_profile.hpp"
#include "ygo/trajectory/codec.hpp"

namespace ygo::teacher {

std::vector<std::uint8_t> canonical_strategy_profile_bytes(
    const StrategyProfileV1& value);
trajectory::DecodeResult<StrategyProfileV1> decode_strategy_profile(
    const std::vector<std::uint8_t>& bytes) noexcept;

std::vector<std::uint8_t> canonical_teacher_policy_binding_bytes(
    const TeacherPolicyBindingV1& value);
trajectory::DecodeResult<TeacherPolicyBindingV1> decode_teacher_policy_binding(
    const std::vector<std::uint8_t>& bytes) noexcept;

}  // namespace ygo::teacher
