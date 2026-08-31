#include "ygo/teacher/candidate_features.hpp"

#include <cstddef>
#include <cstdint>
#include <utility>

#include "ygo/environment/public_action_identity.hpp"

namespace ygo::teacher {
namespace {

bool canonical_token(const std::string_view value) noexcept {
    if (value.empty()) {
        return false;
    }
    for (const auto character : value) {
        const auto byte = static_cast<unsigned char>(character);
        if (!((byte >= 'a' && byte <= 'z') || (byte >= '0' && byte <= '9') ||
              byte == '.' || byte == '_' || byte == '-')) {
            return false;
        }
    }
    return value.front() != '.' && value.back() != '.' &&
           value.find("..") == std::string_view::npos;
}

bool valid_action_kind(const environment::EnvironmentActionKind value) noexcept {
    return static_cast<std::uint8_t>(value) <= 13;
}

bool valid_choice_kind(const environment::PublicChoiceKind value) noexcept {
    const auto code = static_cast<std::uint8_t>(value);
    return code >= 1 && code <= 5;
}

bool valid_reference_kind(const environment::PublicCardReferenceKind value) noexcept {
    return static_cast<std::uint8_t>(value) <= 1;
}

bool valid_evaluation_status(const CandidateEvaluationStatus value) noexcept {
    return static_cast<std::uint8_t>(value) <= 3;
}

bool valid_public_fact_snapshot(const PublicFactSnapshot& public_facts) noexcept {
    try {
        const auto& registry = PublicFactRegistry::canonical();
        for (std::size_t index = 0; index < public_facts.values.size(); ++index) {
            if (!registry.validate(public_facts.values[index])) {
                return false;
            }
            if (index > 0) {
                const auto& previous = public_facts.values[index - 1];
                const auto& current = public_facts.values[index];
                if (previous.fact_id == current.fact_id ||
                    !(canonical_public_fact_value_bytes(previous) <
                      canonical_public_fact_value_bytes(current))) {
                    return false;
                }
            }
        }
        return true;
    } catch (...) {
        return false;
    }
}

}  // namespace

bool extract_candidate_features(
    const environment::EnvironmentActionCandidate& candidate,
    const PublicFactSnapshot& public_facts,
    CandidateFeatures& output) noexcept {
    output = {};
    if (!valid_public_fact_snapshot(public_facts)) {
        return false;
    }
    const auto perspective = public_facts.value("public.perspective_player");
    if (!perspective.has_value() || perspective->value_kind != PublicFactValueKind::U64 ||
        perspective->u64_value > 1) {
        return false;
    }
    if (!environment::is_public_action_key(candidate.public_action_key) ||
        !valid_action_kind(candidate.action_kind)) {
        return false;
    }
    if (candidate.choice.has_value() && !valid_choice_kind(candidate.choice->kind)) {
        return false;
    }
    if (candidate.source_reference.has_value() &&
        !valid_reference_kind(candidate.source_reference->kind)) {
        return false;
    }
    if (candidate.target_reference.has_value() &&
        !valid_reference_kind(candidate.target_reference->kind)) {
        return false;
    }
    if (!candidate.continuation_operation.empty() &&
        !canonical_token(candidate.continuation_operation)) {
        return false;
    }

    output.public_action_key = candidate.public_action_key;
    output.action_kind = candidate.action_kind;
    if (candidate.choice.has_value()) {
        output.choice_kind = candidate.choice->kind;
        output.choice_value = candidate.choice->value;
        output.choice_response_index = candidate.choice->response_index;
    }
    output.phase = candidate.phase;
    output.position = candidate.position;
    output.source_index = candidate.source_index;
    output.amount = candidate.amount;
    if (candidate.source_reference.has_value()) {
        output.source_is_visible =
            candidate.source_reference->kind == environment::PublicCardReferenceKind::VisibleCard;
        output.source_is_redacted =
            candidate.source_reference->kind == environment::PublicCardReferenceKind::RedactedSlot;
    }
    if (candidate.target_reference.has_value()) {
        output.target_is_visible =
            candidate.target_reference->kind == environment::PublicCardReferenceKind::VisibleCard;
        output.target_is_redacted =
            candidate.target_reference->kind == environment::PublicCardReferenceKind::RedactedSlot;
    }
    output.has_continuation_operation = !candidate.continuation_operation.empty();
    output.submits_engine_response = candidate.submits_engine_response;
    return true;
}

bool apply_public_evaluator_outcome(const PublicEvaluatorOutcome& outcome,
                                    ScoreVector& score,
                                    CandidateEvaluationStatus& aggregate_status) noexcept {
    if (!valid_evaluation_status(outcome.status)) {
        aggregate_status = CandidateEvaluationStatus::Invalid;
        return false;
    }
    if (outcome.status == CandidateEvaluationStatus::Invalid) {
        aggregate_status = CandidateEvaluationStatus::Invalid;
        return false;
    }
    if ((outcome.status == CandidateEvaluationStatus::NotApplicable ||
         outcome.status == CandidateEvaluationStatus::Unsupported) &&
        !outcome.contributions.empty()) {
        aggregate_status = CandidateEvaluationStatus::Invalid;
        return false;
    }
    if (outcome.status == CandidateEvaluationStatus::Unsupported) {
        if (aggregate_status != CandidateEvaluationStatus::Invalid) {
            aggregate_status = CandidateEvaluationStatus::Unsupported;
        }
        return true;
    }
    if (outcome.status == CandidateEvaluationStatus::NotApplicable) {
        return aggregate_status != CandidateEvaluationStatus::Invalid;
    }

    auto next_score = score;
    for (const auto& contribution : outcome.contributions) {
        if (!add_score_contribution(next_score, contribution.dimension, contribution.value)) {
            aggregate_status = CandidateEvaluationStatus::Invalid;
            return false;
        }
    }
    score = std::move(next_score);
    if (aggregate_status == CandidateEvaluationStatus::NotApplicable) {
        aggregate_status = CandidateEvaluationStatus::Supported;
    }
    return aggregate_status != CandidateEvaluationStatus::Invalid;
}

}  // namespace ygo::teacher
