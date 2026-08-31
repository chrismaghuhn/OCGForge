#pragma once

#include <memory>

namespace ygo::teacher {

// Task 8 owns the complete explanation value and its canonical codec. Task 3
// carries only this nullable opaque handle so it does not invent explanation
// or public-feature semantics early.
struct TeacherDecisionExplanation;
using TeacherDecisionExplanationPtr =
    std::shared_ptr<const TeacherDecisionExplanation>;

}  // namespace ygo::teacher
