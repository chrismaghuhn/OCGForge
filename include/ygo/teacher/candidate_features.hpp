#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "ygo/environment/public_decision.hpp"
#include "ygo/teacher/candidate_evaluator.hpp"
#include "ygo/teacher/public_fact_registry.hpp"

namespace ygo::teacher {

struct CandidateFeatures final {
    std::string public_action_key;
    environment::EnvironmentActionKind action_kind =
        environment::EnvironmentActionKind::Unsupported;
    std::optional<environment::PublicChoiceKind> choice_kind;
    std::optional<std::uint64_t> choice_value;
    std::optional<std::uint32_t> choice_response_index;
    std::optional<std::uint32_t> phase;
    std::optional<std::uint8_t> position;
    std::optional<std::uint32_t> source_index;
    std::optional<std::int32_t> amount;
    bool source_is_visible = false;
    bool source_is_redacted = false;
    bool target_is_visible = false;
    bool target_is_redacted = false;
    bool has_continuation_operation = false;
    bool submits_engine_response = true;

    bool operator==(const CandidateFeatures& other) const noexcept {
        return public_action_key == other.public_action_key &&
               action_kind == other.action_kind && choice_kind == other.choice_kind &&
               choice_value == other.choice_value &&
               choice_response_index == other.choice_response_index &&
               phase == other.phase && position == other.position &&
               source_index == other.source_index && amount == other.amount &&
               source_is_visible == other.source_is_visible &&
               source_is_redacted == other.source_is_redacted &&
               target_is_visible == other.target_is_visible &&
               target_is_redacted == other.target_is_redacted &&
               has_continuation_operation == other.has_continuation_operation &&
               submits_engine_response == other.submits_engine_response;
    }
    bool operator!=(const CandidateFeatures& other) const noexcept {
        return !(*this == other);
    }
};

struct EvaluatorScoreContribution final {
    ScoreDimension dimension = ScoreDimension::ProfilePreference;
    std::int32_t value = 0;

    bool operator==(const EvaluatorScoreContribution& other) const noexcept {
        return dimension == other.dimension && value == other.value;
    }
};

struct PublicEvaluatorOutcome final {
    std::string public_action_key;
    CandidateEvaluationStatus status = CandidateEvaluationStatus::NotApplicable;
    std::vector<EvaluatorScoreContribution> contributions;
    std::vector<std::string> matched_intent_ids;
    std::vector<std::string> matched_goal_ids;
    std::vector<std::string> matched_line_ids;
    std::vector<std::string> reason_ids;

    bool operator==(const PublicEvaluatorOutcome& other) const noexcept {
        return public_action_key == other.public_action_key &&
               status == other.status && contributions == other.contributions &&
               matched_intent_ids == other.matched_intent_ids &&
               matched_goal_ids == other.matched_goal_ids &&
               matched_line_ids == other.matched_line_ids &&
               reason_ids == other.reason_ids;
    }
    bool operator!=(const PublicEvaluatorOutcome& other) const noexcept {
        return !(*this == other);
    }
};

bool extract_candidate_features(
    const environment::EnvironmentActionCandidate& candidate,
    const PublicFactSnapshot& public_facts,
    CandidateFeatures& output) noexcept;

// Applies one evaluator's contributions transactionally through the accepted
// Task-4 helper. Unsupported/not-applicable outcomes never invent a score;
// arithmetic failure leaves score unchanged and marks the aggregate INVALID.
bool apply_public_evaluator_outcome(const PublicEvaluatorOutcome& outcome,
                                    ScoreVector& score,
                                    CandidateEvaluationStatus& aggregate_status) noexcept;

}  // namespace ygo::teacher
