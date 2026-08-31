#pragma once

#include <cstdint>
#include <vector>

#include "ygo/teacher/teacher_explanation.hpp"
#include "ygo/trajectory/codec.hpp"

namespace ygo::teacher {

bool validate_teacher_decision_explanation(
    const TeacherDecisionExplanation& value) noexcept;

std::vector<std::uint8_t> canonical_teacher_decision_explanation_bytes(
    const TeacherDecisionExplanation& value);

trajectory::DecodeResult<TeacherDecisionExplanation>
decode_teacher_decision_explanation(
    const std::vector<std::uint8_t>& bytes) noexcept;

}  // namespace ygo::teacher
