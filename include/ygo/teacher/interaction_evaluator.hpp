#pragma once

#include "ygo/teacher/candidate_features.hpp"

namespace ygo::teacher {

class InteractionEvaluator final {
public:
    PublicEvaluatorOutcome evaluate(const CandidateFeatures& candidate,
                                    const PublicFactSnapshot& public_facts) const noexcept;
};

}  // namespace ygo::teacher
