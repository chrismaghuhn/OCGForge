#include "ygo/teacher/teacher_decision.hpp"
#include "ygo/teacher/strategy_profile.hpp"
#include "ygo/policy/policy.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "ygo/environment/public_action_identity.hpp"

namespace {

using namespace ygo::environment;
using namespace ygo::policy;
using namespace ygo::teacher;

void require(const bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

EnvironmentActionCandidate candidate(const std::uint64_t choice) {
    PublicActionKeyInput key_input;
    key_input.action_kind = "yes_no";
    key_input.choice = PublicChoice{PublicChoiceKind::YesNo, choice, std::nullopt};

    EnvironmentActionCandidate value;
    value.action_kind = EnvironmentActionKind::YesNo;
    value.choice = key_input.choice;
    value.public_action_key = public_action_key(key_input);
    return value;
}

CandidateEvaluation evaluation(const EnvironmentActionCandidate& candidate_value,
                               const std::int64_t profile_score) {
    CandidateEvaluation value;
    value.public_action_key = candidate_value.public_action_key;
    value.status = CandidateEvaluationStatus::Supported;
    ScoreVector score;
    score.values[static_cast<std::size_t>(ScoreDimension::ProfilePreference)] =
        profile_score;
    value.score = score;
    value.matched_intent_ids = {"intent.test"};
    value.matched_goal_ids = {"goal.test"};
    value.matched_line_ids = {"line.test"};
    value.reason_ids = {"reason.test"};
    return value;
}

TeacherRankingResult selected_result(
    const std::vector<EnvironmentActionCandidate>& candidates) {
    TeacherRankingResult value;
    value.status = TeacherRankingStatus::Selected;
    value.evaluations = {evaluation(candidates[0], 1), evaluation(candidates[1], 2)};
    value.selected_public_action_key = candidates[1].public_action_key;
    value.selected_score_vector = value.evaluations[1].score;
    value.fallback_level = TeacherFallbackLevel::F0;
    return value;
}

void test_order_and_policy_adapter() {
    const auto first = candidate(0);
    const auto second = candidate(1);
    const std::vector<EnvironmentActionCandidate> candidates = {first, second};
    const auto result = selected_result(candidates);

    std::string diagnostic;
    require(validate_teacher_ranking_result(result, &diagnostic),
            "valid selected DTO rejected: " + diagnostic);
    require(result.evaluations.size() == candidates.size(),
            "selected DTO did not preserve N evaluation records");
    require(result.evaluations[0].public_action_key == first.public_action_key &&
                result.evaluations[1].public_action_key == second.public_action_key,
            "evaluation order changed");

    const auto selection = teacher_policy_selection_from_result(result);
    require(static_cast<bool>(selection), "valid selected DTO did not map to PolicySelection");
    require(selection.value->public_action_key == second.public_action_key,
            "adapter selected the first candidate instead of the declared key");
    require(!selection.value->rng_cursor.has_value(),
            "deterministic Teacher adapter produced RNG provenance");

    auto reconstructed = selected_result(candidates);
    require(validate_teacher_ranking_result(reconstructed),
            "identical reconstructed DTO did not validate");
    require(reconstructed.status == result.status &&
                reconstructed.selected_public_action_key == result.selected_public_action_key &&
                reconstructed.selected_score_vector == result.selected_score_vector &&
                reconstructed.evaluations[0].public_action_key ==
                    result.evaluations[0].public_action_key &&
                reconstructed.evaluations[1].score == result.evaluations[1].score,
            "identical DTO reconstruction changed deterministic values");
}

void test_non_selected_results_do_not_create_actions() {
    for (const auto status :
         {TeacherRankingStatus::InvalidInput, TeacherRankingStatus::Blocked,
          TeacherRankingStatus::Unsupported}) {
        TeacherRankingResult value;
        value.status = status;
        std::string diagnostic;
        require(validate_teacher_ranking_result(value, &diagnostic),
                "non-selected result rejected: " + diagnostic);
        const auto selection = teacher_policy_selection_from_result(value);
        require(!static_cast<bool>(selection),
                "non-selected result produced a gameplay action");
        require(selection.error.has_value(), "non-selected result lacked PolicyError");
    }
}

void test_malformed_selection_evidence_fails_closed() {
    const auto first = candidate(0);
    const auto second = candidate(1);
    const std::vector<EnvironmentActionCandidate> candidates = {first, second};

    auto duplicate = selected_result(candidates);
    duplicate.evaluations[1].public_action_key =
        duplicate.evaluations[0].public_action_key;
    require(!validate_teacher_ranking_result(duplicate),
            "duplicate evaluation public keys were accepted");

    auto unknown_selected = selected_result(candidates);
    unknown_selected.selected_public_action_key = "not-a-public-action-key";
    require(!validate_teacher_ranking_result(unknown_selected),
            "selected key outside the evaluation domain was accepted");

    auto mismatched_score = selected_result(candidates);
    mismatched_score.selected_score_vector->values[0] += 1;
    require(!validate_teacher_ranking_result(mismatched_score),
            "mismatched selected score was accepted");

    auto non_selected_with_action = selected_result(candidates);
    non_selected_with_action.status = TeacherRankingStatus::Blocked;
    require(!validate_teacher_ranking_result(non_selected_with_action),
            "blocked result carrying a selected action was accepted");

    auto invalid_enum = selected_result(candidates);
    invalid_enum.status = static_cast<TeacherRankingStatus>(4);
    require(!validate_teacher_ranking_result(invalid_enum),
            "unknown ranking status was accepted");

    auto invalid_candidate_status = selected_result(candidates);
    invalid_candidate_status.evaluations[0].status =
        static_cast<CandidateEvaluationStatus>(4);
    require(!validate_teacher_ranking_result(invalid_candidate_status),
            "unknown candidate evaluation status was accepted");

    auto unsorted_intents = selected_result(candidates);
    unsorted_intents.evaluations[0].matched_intent_ids = {"intent.b", "intent.a"};
    require(!validate_teacher_ranking_result(unsorted_intents),
            "unsorted matched intent IDs were accepted");

    auto duplicate_intents = selected_result(candidates);
    duplicate_intents.evaluations[0].matched_intent_ids = {"intent.a", "intent.a"};
    require(!validate_teacher_ranking_result(duplicate_intents),
            "duplicate matched intent IDs were accepted");

    auto unsorted_goals = selected_result(candidates);
    unsorted_goals.evaluations[0].matched_goal_ids = {"goal.b", "goal.a"};
    require(!validate_teacher_ranking_result(unsorted_goals),
            "unsorted matched goal IDs were accepted");

    auto duplicate_lines = selected_result(candidates);
    duplicate_lines.evaluations[0].matched_line_ids = {"line.a", "line.a"};
    require(!validate_teacher_ranking_result(duplicate_lines),
            "duplicate matched line IDs were accepted");

    auto unsorted_reasons = selected_result(candidates);
    unsorted_reasons.evaluations[0].reason_ids = {"reason.b", "reason.a"};
    require(!validate_teacher_ranking_result(unsorted_reasons),
            "unsorted reason IDs were accepted");

    auto duplicate_reasons = selected_result(candidates);
    duplicate_reasons.evaluations[0].reason_ids = {"reason.a", "reason.a"};
    require(!validate_teacher_ranking_result(duplicate_reasons),
            "duplicate reason IDs were accepted");

    auto invalid_fallback = selected_result(candidates);
    invalid_fallback.fallback_level = static_cast<TeacherFallbackLevel>(5);
    require(!validate_teacher_ranking_result(invalid_fallback),
            "unknown Teacher fallback level was accepted");

    auto unsupported_selected = selected_result(candidates);
    unsupported_selected.selected_public_action_key =
        unsupported_selected.evaluations[0].public_action_key;
    unsupported_selected.evaluations[0].status =
        CandidateEvaluationStatus::Unsupported;
    require(!validate_teacher_ranking_result(unsupported_selected),
            "unsupported selected evaluation was accepted");

    auto invalid_selected = selected_result(candidates);
    invalid_selected.selected_public_action_key =
        invalid_selected.evaluations[0].public_action_key;
    invalid_selected.evaluations[0].status = CandidateEvaluationStatus::Invalid;
    require(!validate_teacher_ranking_result(invalid_selected),
            "invalid selected evaluation was accepted");

    auto malformed_key = selected_result(candidates);
    malformed_key.evaluations[0].public_action_key = "public_action.v1.00";
    require(!validate_teacher_ranking_result(malformed_key),
            "malformed evaluation public key was accepted");

    auto missing_selected = selected_result(candidates);
    missing_selected.selected_public_action_key.reset();
    require(!validate_teacher_ranking_result(missing_selected),
            "SELECTED result without a selected key was accepted");
}

}  // namespace

int main() {
    try {
        test_order_and_policy_adapter();
        test_non_selected_results_do_not_create_actions();
        test_malformed_selection_evidence_fails_closed();
        std::cout << "teacher_decision_dto_test: PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "teacher_decision_dto_test: FAIL: " << error.what() << '\n';
        return 1;
    }
}
