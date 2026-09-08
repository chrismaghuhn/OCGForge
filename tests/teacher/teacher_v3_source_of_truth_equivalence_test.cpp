#include "ygo/environment/public_action_identity.hpp"
#include "ygo/teacher/fallback_resolver.hpp"
#include "ygo/teacher/goal_line_controller.hpp"
#include "ygo/teacher/salamangreat_profile.hpp"
#include "ygo/teacher/strategy_state.hpp"
#include "ygo/teacher/strategy_state_v2.hpp"
#include "ygo/teacher/teacher_decision_v2.hpp"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <iterator>
#include <vector>

namespace {

using namespace ygo::environment;
using namespace ygo::teacher;

void require(const bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

EnvironmentActionCandidate candidate(const std::string& locator, const bool v2) {
    EnvironmentActionCandidate result;
    result.action_kind = EnvironmentActionKind::CardSelection;
    result.source_reference =
        PublicCardReference{PublicCardReferenceKind::VisibleCard, locator};
    result.card_selection_operation =
        v2 ? PublicCardSelectionOperation::Select : PublicCardSelectionOperation::None;
    PublicActionKeyInput input;
    input.action_kind = "card_selection";
    input.source_reference = result.source_reference;
    input.card_selection_operation = result.card_selection_operation;
    result.public_action_key = v2 ? public_action_key_v2(input) : public_action_key(input);
    return result;
}

TeacherFallbackCandidateValue supported(const std::string& key,
                                        const std::int64_t value) {
    TeacherFallbackCandidateValue result;
    result.public_action_key = key;
    result.status = CandidateEvaluationStatus::Supported;
    result.score = ScoreVector{};
    result.score->values[static_cast<std::size_t>(
        ScoreDimension::ActiveGoalLineOrValidatedRecoveryProgress)] = value;
    result.contributions.push_back(
        {ScoreDimension::ActiveGoalLineOrValidatedRecoveryProgress,
         static_cast<std::int32_t>(value)});
    return result;
}

TeacherFallbackCandidateValue blocked(const std::string& key) {
    TeacherFallbackCandidateValue result;
    result.public_action_key = key;
    result.status = CandidateEvaluationStatus::Supported;
    return result;
}

TeacherFallbackCandidateValue unsupported(const std::string& key) {
    TeacherFallbackCandidateValue result;
    result.public_action_key = key;
    result.status = CandidateEvaluationStatus::Unsupported;
    return result;
}

void compare_common_result(const TeacherRankingResult& v1,
                           const TeacherRankingResultV2& v2,
                           const std::vector<EnvironmentActionCandidate>& v1_candidates,
                           const std::vector<EnvironmentActionCandidate>& v2_candidates) {
    require(v1.status == v2.status && v1.fallback_level == v2.fallback_level &&
                v1.selected_score_vector == v2.selected_score_vector &&
                v1.evaluations.size() == v2.evaluations.size(),
            "V1/V2 fallback result status or shape diverged");
    for (std::size_t index = 0; index < v1.evaluations.size(); ++index) {
        require(v1.evaluations[index].status == v2.evaluations[index].status &&
                    v1.evaluations[index].score == v2.evaluations[index].score,
                "V1/V2 fallback evaluation semantics diverged");
    }
    if (v1.selected_public_action_key.has_value()) {
        require(v2.selected_public_action_key.has_value(),
                "V2 omitted a selected action present in V1");
        const auto v1_index = std::find_if(
            v1_candidates.begin(), v1_candidates.end(), [&](const auto& value) {
                return value.public_action_key == *v1.selected_public_action_key;
            });
        const auto v2_index = std::find_if(
            v2_candidates.begin(), v2_candidates.end(), [&](const auto& value) {
                return value.public_action_key == *v2.selected_public_action_key;
            });
        require(v1_index != v1_candidates.end() && v2_index != v2_candidates.end() &&
                    std::distance(v1_candidates.begin(), v1_index) ==
                        std::distance(v2_candidates.begin(), v2_index),
                "V1/V2 selected semantic candidate positions diverged");
    } else {
        require(!v2.selected_public_action_key.has_value(),
                "V2 selected an action where V1 did not");
    }
}

void test_f0_to_f3_equivalence() {
    const auto v1_candidates = std::vector<EnvironmentActionCandidate>{
        candidate("p0:MONSTER_ZONE:0", false),
        candidate("p0:MONSTER_ZONE:1", false),
    };
    const auto v2_candidates = std::vector<EnvironmentActionCandidate>{
        candidate("p0:MONSTER_ZONE:0", true),
        candidate("p0:MONSTER_ZONE:1", true),
    };
    for (std::size_t stage_index = 0; stage_index < 4; ++stage_index) {
        TeacherFallbackStageSet stages;
        stages.stage_evaluations[stage_index] = std::vector<TeacherFallbackCandidateValue>{
            supported(v1_candidates[0].public_action_key, 10),
            supported(v1_candidates[1].public_action_key, 0),
        };
        TeacherFallbackStageSet stages_v2;
        stages_v2.stage_evaluations[stage_index] =
            std::vector<TeacherFallbackCandidateValue>{
                supported(v2_candidates[0].public_action_key, 10),
                supported(v2_candidates[1].public_action_key, 0),
            };
        compare_common_result(resolve_teacher_fallback(v1_candidates, stages),
                              resolve_teacher_fallback_v2(v2_candidates, stages_v2),
                              v1_candidates, v2_candidates);
    }
}

void test_f4_blocked_unsupported_and_invalid_equivalence() {
    const auto v1_candidates = std::vector<EnvironmentActionCandidate>{
        candidate("p0:MONSTER_ZONE:0", false)};
    const auto v2_candidates = std::vector<EnvironmentActionCandidate>{
        candidate("p0:MONSTER_ZONE:0", true)};

    TeacherFallbackStageSet blocked_stages;
    blocked_stages.stage_evaluations[0] =
        std::vector<TeacherFallbackCandidateValue>{
            blocked(v1_candidates[0].public_action_key)};
    TeacherFallbackStageSet blocked_stages_v2;
    blocked_stages_v2.stage_evaluations[0] =
        std::vector<TeacherFallbackCandidateValue>{
            blocked(v2_candidates[0].public_action_key)};
    compare_common_result(resolve_teacher_fallback(v1_candidates, blocked_stages),
                          resolve_teacher_fallback_v2(v2_candidates, blocked_stages_v2),
                          v1_candidates, v2_candidates);

    TeacherFallbackStageSet unsupported_stages;
    unsupported_stages.stage_evaluations[0] =
        std::vector<TeacherFallbackCandidateValue>{
            unsupported(v1_candidates[0].public_action_key)};
    TeacherFallbackStageSet unsupported_stages_v2;
    unsupported_stages_v2.stage_evaluations[0] =
        std::vector<TeacherFallbackCandidateValue>{
            unsupported(v2_candidates[0].public_action_key)};
    compare_common_result(resolve_teacher_fallback(v1_candidates, unsupported_stages),
                          resolve_teacher_fallback_v2(v2_candidates, unsupported_stages_v2),
                          v1_candidates, v2_candidates);

    auto invalid_v1 = v1_candidates;
    invalid_v1[0].public_action_key = "public_action.v1.malformed";
    auto invalid_v2 = v2_candidates;
    invalid_v2[0].public_action_key = "public_action.v2.malformed";
    const auto v1_invalid = resolve_teacher_fallback(invalid_v1, TeacherFallbackStageSet{});
    const auto v2_invalid = resolve_teacher_fallback_v2(invalid_v2, TeacherFallbackStageSet{});
    require(v1_invalid.status == TeacherRankingStatus::InvalidInput &&
                v2_invalid.status == TeacherRankingStatus::InvalidInput &&
                v1_invalid.evaluations.size() == v2_invalid.evaluations.size(),
            "V1/V2 invalid-domain fallback behavior diverged");
}

void test_f4_and_score_tie_equivalence() {
    const auto v1_candidates = std::vector<EnvironmentActionCandidate>{
        candidate("p0:MONSTER_ZONE:0", false)};
    const auto v2_candidates = std::vector<EnvironmentActionCandidate>{
        candidate("p0:MONSTER_ZONE:0", true)};
    compare_common_result(resolve_teacher_fallback(v1_candidates, TeacherFallbackStageSet{}),
                          resolve_teacher_fallback_v2(v2_candidates, TeacherFallbackStageSet{}),
                          v1_candidates, v2_candidates);
}

void test_state_plan_reference_equivalence() {
    const auto profile = make_salamangreat_profile();
    const auto v1_reset = reset_strategy_state(profile);
    const auto v2_reset = reset_strategy_state_v2(profile);
    require(v1_reset.has_value() && v2_reset.has_value(),
            "V1/V2 state reset equivalence fixture was not valid");
    require(validate_strategy_state(*v1_reset) && validate_strategy_state_v2(*v2_reset),
            "V1/V2 reset state validation diverged");

    auto v1_active = *v1_reset;
    auto v2_active = *v2_reset;
    v1_active.active_goal_id = "goal.main1.salamangreat";
    v1_active.active_line_id = "line.main1.salamangreat";
    v2_active.active_goal_id = v1_active.active_goal_id;
    v2_active.active_line_id = v1_active.active_line_id;
    require(validate_strategy_state(v1_active) && validate_strategy_state_v2(v2_active),
            "V1/V2 valid active plan reference semantics diverged");
    const PublicFactSnapshot empty_facts;
    const auto v1_selection = select_goal_and_line(profile, v1_active, empty_facts);
    const auto v2_selection = select_goal_and_line_v2(profile, v2_active, empty_facts);
    require(v1_selection.status == v2_selection.status &&
                v1_selection.goal_id == v2_selection.goal_id &&
                v1_selection.line_id == v2_selection.line_id &&
                v1_selection.ready_node_ids == v2_selection.ready_node_ids,
            "V1/V2 goal-line selection semantics diverged");

    v1_active.active_line_id = "line.not-in-profile";
    v2_active.active_line_id = v1_active.active_line_id;
    const auto v1_invalid = select_goal_and_line(profile, v1_active, empty_facts);
    const auto v2_invalid = select_goal_and_line_v2(profile, v2_active, empty_facts);
    require(v1_invalid.status == PredicateEvaluationStatus::Invalid &&
                v2_invalid.status == PredicateEvaluationStatus::Invalid,
            "V1/V2 invalid active plan reference semantics diverged");
}

}  // namespace

int main() {
    try {
        test_f0_to_f3_equivalence();
        test_f4_blocked_unsupported_and_invalid_equivalence();
        test_f4_and_score_tie_equivalence();
        test_state_plan_reference_equivalence();
        std::cout << "teacher_v3_source_of_truth_equivalence_test: PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "teacher_v3_source_of_truth_equivalence_test: " << error.what() << '\n';
        return 1;
    }
}
