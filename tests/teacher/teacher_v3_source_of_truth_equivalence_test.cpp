#include "ygo/environment/episodic_environment.hpp"
#include "ygo/environment/public_action_identity.hpp"
#include "ygo/teacher/fallback_resolver.hpp"
#include "ygo/teacher/goal_line_controller.hpp"
#include "ygo/teacher/salamangreat_profile.hpp"
#include "ygo/teacher/strategy_state.hpp"
#include "ygo/teacher/strategy_state_v2.hpp"
#include "ygo/teacher/teacher_decision.hpp"
#include "ygo/teacher/teacher_decision_v2.hpp"
#include "ygo/observation/player_observation.hpp"

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

PublicEnvironmentObservation observation(const std::uint64_t decision_index) {
    ygo::observation::PlayerObservation source;
    source.schema_version = "ygo.player_observation.v1";
    source.perspective_player = 0;
    source.decision_index = decision_index;
    source.globals.life_points = {8000, 7000};
    source.globals.player_to_act = 0;
    source.globals.turn_player = 0;
    source.globals.turn_count = 16;
    source.globals.phase = 4;
    source.globals.chain_length = 0;
    source.globals.terminal = false;
    source.match_context.perspective_player = 0;
    source.match_context.knowledge.own_decklist_known = true;
    source.match_context.knowledge.opponent_decklist_known = false;
    source.decision_context.kind = "unselect_card";
    source.decision_context.player = 0;
    return project_public_observation(source);
}

void compare_common_state(const EpisodeLocalStrategyStateV1& v1,
                          const EpisodeLocalStrategyStateV2& v2) {
    require(v1.strategy_profile_id == v2.strategy_profile_id &&
                v1.active_goal_id == v2.active_goal_id &&
                v1.active_line_id == v2.active_line_id &&
                v1.completed_line_node_ids == v2.completed_line_node_ids &&
                v1.achieved_goal_ids == v2.achieved_goal_ids &&
                v1.public_resource_facts == v2.public_resource_facts &&
                v1.public_restriction_facts == v2.public_restriction_facts &&
                v1.public_threat_facts == v2.public_threat_facts &&
                v1.last_accepted_decision_index == v2.last_accepted_decision_index &&
                v1.last_accepted_public_action_key.has_value() ==
                    v2.last_accepted_public_action_key.has_value(),
            "V1/V2 common state semantics diverged");
}

void compare_common_delta(const TeacherStateDeltaV1& v1,
                          const TeacherStateDeltaV2& v2) {
    require(v1.strategy_profile_id == v2.strategy_profile_id &&
                v1.base_last_accepted_decision_index ==
                    v2.base_last_accepted_decision_index &&
                v1.base_last_accepted_public_action_key.has_value() ==
                    v2.base_last_accepted_public_action_key.has_value() &&
                v1.active_goal_id == v2.active_goal_id &&
                v1.active_line_id == v2.active_line_id &&
                v1.completed_line_node_ids == v2.completed_line_node_ids &&
                v1.achieved_goal_ids == v2.achieved_goal_ids &&
                v1.public_resource_facts == v2.public_resource_facts &&
                v1.public_restriction_facts == v2.public_restriction_facts &&
                v1.public_threat_facts == v2.public_threat_facts &&
                v1.invalidation_reason_ids == v2.invalidation_reason_ids,
            "V1/V2 common state-delta semantics diverged");
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

void test_f4_score_tie_equivalence() {
    const auto v1_candidates = std::vector<EnvironmentActionCandidate>{
        candidate("p0:MONSTER_ZONE:0", false),
        candidate("p0:MONSTER_ZONE:1", false)};
    const auto v2_candidates = std::vector<EnvironmentActionCandidate>{
        candidate("p0:MONSTER_ZONE:0", true),
        candidate("p0:MONSTER_ZONE:1", true)};
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

void test_state_lifecycle_equivalence() {
    const auto profile = make_salamangreat_profile();
    const auto reset_v1 = reset_strategy_state(profile);
    const auto reset_v2 = reset_strategy_state_v2(profile);
    require(reset_v1.has_value() && reset_v2.has_value(),
            "V1/V2 lifecycle state reset fixture was not valid");
    auto v1_state = *reset_v1;
    auto v2_state = *reset_v2;
    v1_state.active_goal_id = "goal.main1.salamangreat";
    v1_state.active_line_id = "line.main1.salamangreat";
    v2_state.active_goal_id = v1_state.active_goal_id;
    v2_state.active_line_id = v1_state.active_line_id;

    const auto retained_v1 = reconcile_strategy_state_with_evidence(
        v1_state, 0, observation(10));
    const auto retained_v2 = reconcile_strategy_state_with_evidence_v2(
        v2_state, 0, observation(10));
    require(retained_v1.has_value() && retained_v2.has_value() &&
                retained_v1->invalidation_reason_ids ==
                    retained_v2->invalidation_reason_ids,
            "V1/V2 reconciliation retention diverged");
    compare_common_state(retained_v1->state, retained_v2->state);

    PublicFactValue stale_fact;
    stale_fact.fact_id = "public.chain.length";
    stale_fact.value_kind = PublicFactValueKind::U64;
    stale_fact.u64_value = 1;
    v1_state.public_resource_facts = {stale_fact};
    v2_state.public_resource_facts = {stale_fact};
    const auto contradicted_v1 = reconcile_strategy_state_with_evidence(
        v1_state, 0, observation(11));
    const auto contradicted_v2 = reconcile_strategy_state_with_evidence_v2(
        v2_state, 0, observation(11));
    require(contradicted_v1.has_value() && contradicted_v2.has_value() &&
                contradicted_v1->invalidation_reason_ids ==
                    contradicted_v2->invalidation_reason_ids,
            "V1/V2 contradiction reconciliation diverged");
    compare_common_state(contradicted_v1->state, contradicted_v2->state);

    auto requested_v1 = TeacherStateDeltaV1{};
    requested_v1.strategy_profile_id = profile.profile_id;
    requested_v1.proposed_for_public_action_key =
        candidate("p0:MONSTER_ZONE:0", false).public_action_key;
    requested_v1.active_goal_id = "goal.main1.salamangreat";
    requested_v1.active_line_id = "line.main1.salamangreat";
    auto requested_v2 = TeacherStateDeltaV2{};
    requested_v2.strategy_profile_id = profile.profile_id;
    requested_v2.proposed_for_public_action_key =
        candidate("p0:MONSTER_ZONE:0", true).public_action_key;
    requested_v2.active_goal_id = requested_v1.active_goal_id;
    requested_v2.active_line_id = requested_v1.active_line_id;

    const auto proposal_reset_v1 = reset_strategy_state(profile);
    const auto proposal_reset_v2 = reset_strategy_state_v2(profile);
    require(proposal_reset_v1.has_value() && proposal_reset_v2.has_value(),
            "V1/V2 proposal reset fixture was not valid");
    auto proposal_state_v1 = *proposal_reset_v1;
    auto proposal_state_v2 = *proposal_reset_v2;
    const auto proposed_v1 = propose_teacher_state_delta(
        proposal_state_v1, observation(20), 0, profile, requested_v1);
    const auto proposed_v2 = propose_teacher_state_delta_v2(
        proposal_state_v2, observation(20), 0, profile, requested_v2);
    require(proposed_v1.has_value() && proposed_v2.has_value(),
            "V1/V2 proposal lifecycle diverged");
    compare_common_delta(*proposed_v1, *proposed_v2);

    const auto selected_v1 = candidate("p0:MONSTER_ZONE:0", false);
    const auto selected_v2 = candidate("p0:MONSTER_ZONE:0", true);
    TeacherRankingResult ranking_v1;
    ranking_v1.status = TeacherRankingStatus::Selected;
    ranking_v1.evaluations = {{selected_v1.public_action_key,
                               CandidateEvaluationStatus::Supported, ScoreVector{}, {}, {}, {}, {}}};
    ranking_v1.selected_public_action_key = selected_v1.public_action_key;
    ranking_v1.selected_score_vector = ScoreVector{};
    ranking_v1.fallback_level = TeacherFallbackLevel::F4;
    ranking_v1.proposed_state_delta = *proposed_v1;

    TeacherRankingResultV2 ranking_v2;
    ranking_v2.status = TeacherRankingStatus::Selected;
    ranking_v2.evaluations = {{selected_v2.public_action_key,
                               CandidateEvaluationStatus::Supported, ScoreVector{}, {}, {}, {}, {}}};
    ranking_v2.selected_public_action_key = selected_v2.public_action_key;
    ranking_v2.selected_score_vector = ScoreVector{};
    ranking_v2.fallback_level = TeacherFallbackLevel::F4;
    ranking_v2.proposed_state_delta = *proposed_v2;

    AcceptedActionTransition accepted_v1;
    accepted_v1.decision_index = 20;
    accepted_v1.selected_public_action_key = selected_v1.public_action_key;
    AcceptedActionTransition accepted_v2 = accepted_v1;
    accepted_v2.selected_public_action_key = selected_v2.public_action_key;
    const auto commit_reset_v1 = reset_strategy_state(profile);
    const auto commit_reset_v2 = reset_strategy_state_v2(profile);
    require(commit_reset_v1.has_value() && commit_reset_v2.has_value(),
            "V1/V2 commit reset fixture was not valid");
    auto committed_v1 = *commit_reset_v1;
    auto committed_v2 = *commit_reset_v2;
    const auto committed_result_v1 = commit_teacher_state_delta_with_evidence(
        committed_v1, ranking_v1, profile, 0, observation(20), accepted_v1);
    const auto committed_result_v2 = commit_teacher_state_delta_with_evidence_v2(
        committed_v2, ranking_v2, profile, 0, observation(20), accepted_v2);
    require(committed_result_v1.has_value() && committed_result_v2.has_value(),
            "V1/V2 commit lifecycle diverged");
    compare_common_state(committed_result_v1->state, committed_result_v2->state);
    require(committed_result_v1->invalidation_reason_ids ==
                committed_result_v2->invalidation_reason_ids,
            "V1/V2 commit reconciliation evidence diverged");

    StepRejected rejected;
    rejected.authoritative_state_unchanged = true;
    require(observe_step_rejected(committed_v1, rejected) &&
                observe_step_rejected_v2(committed_v2, rejected),
            "V1/V2 StepRejected lifecycle diverged");
    rejected.authoritative_state_unchanged = false;
    require(!observe_step_rejected(committed_v1, rejected) &&
                !observe_step_rejected_v2(committed_v2, rejected),
            "V1/V2 rejected-transition failure semantics diverged");
}

}  // namespace

int main() {
    try {
        test_f0_to_f3_equivalence();
        test_f4_blocked_unsupported_and_invalid_equivalence();
        test_f4_score_tie_equivalence();
        test_state_plan_reference_equivalence();
        test_state_lifecycle_equivalence();
        std::cout << "teacher_v3_source_of_truth_equivalence_test: PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "teacher_v3_source_of_truth_equivalence_test: " << error.what() << '\n';
        return 1;
    }
}
