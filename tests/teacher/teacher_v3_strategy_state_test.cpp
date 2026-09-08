#include "ygo/environment/episodic_environment.hpp"
#include "ygo/environment/public_action_identity.hpp"
#include "ygo/environment/public_environment_observation.hpp"
#include "ygo/observation/player_observation.hpp"
#include "ygo/teacher/salamangreat_profile.hpp"
#include "ygo/teacher/strategy_state.hpp"
#include "ygo/teacher/strategy_state_v2.hpp"
#include "ygo/teacher/teacher_decision_v2.hpp"

#include <cstdint>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using namespace ygo::environment;
using namespace ygo::teacher;

void require(const bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

PublicEnvironmentObservation observation(const std::uint64_t decision_index,
                                         const std::string& decision_kind =
                                             "unselect_card") {
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
    source.decision_context.kind = decision_kind;
    source.decision_context.player = 0;
    return project_public_observation(source);
}

std::string action_key(const PublicCardSelectionOperation operation,
                       const bool v2) {
    PublicActionKeyInput input;
    input.action_kind = "card_selection";
    input.source_reference = PublicCardReference{
        PublicCardReferenceKind::VisibleCard, "p0:MONSTER_ZONE:0"};
    input.card_selection_operation = operation;
    return v2 ? public_action_key_v2(input) : public_action_key(input);
}

EpisodeLocalStrategyStateV2 reset_state(const StrategyProfileV1& profile) {
    const auto result = reset_strategy_state_v2(profile);
    require(result.has_value(), "V2 strategy state reset failed");
    return *result;
}

TeacherStateDeltaV2 valid_delta(const StrategyProfileV1& profile,
                                const std::string& proposed_key) {
    TeacherStateDeltaV2 delta;
    delta.strategy_profile_id = profile.profile_id;
    delta.proposed_for_public_action_key = proposed_key;
    return delta;
}

TeacherRankingResultV2 valid_ranking(const StrategyProfileV1& profile,
                                    const std::string& selected_key) {
    TeacherRankingResultV2 result;
    result.status = TeacherRankingStatus::Selected;
    CandidateEvaluation evaluation;
    evaluation.public_action_key = selected_key;
    evaluation.status = CandidateEvaluationStatus::Supported;
    evaluation.score = ScoreVector{};
    result.evaluations.push_back(evaluation);
    result.selected_public_action_key = selected_key;
    result.selected_score_vector = ScoreVector{};
    result.fallback_level = TeacherFallbackLevel::F4;
    result.proposed_state_delta = valid_delta(profile, selected_key);
    return result;
}

void test_versioned_state_and_delta_validation() {
    const auto profile = make_salamangreat_profile();
    const auto v1_key = action_key(PublicCardSelectionOperation::None, false);
    const auto v2_key = action_key(PublicCardSelectionOperation::Select, true);

    auto state = reset_state(profile);
    require(validate_strategy_state_v2(state), "fresh V2 state did not validate");
    state.last_accepted_decision_index = 1;
    state.last_accepted_public_action_key = v2_key;
    require(validate_strategy_state_v2(state), "V2 state rejected a V2 key");
    state.last_accepted_public_action_key = v1_key;
    require(!validate_strategy_state_v2(state), "V2 state accepted a V1 key");

    auto historical = reset_strategy_state(profile);
    require(historical.has_value(), "historical strategy state reset failed");
    historical->last_accepted_decision_index = 1;
    historical->last_accepted_public_action_key = v2_key;
    require(!validate_strategy_state(*historical),
            "V1 state accepted a V2 key during V2 migration");

    auto delta = valid_delta(profile, v2_key);
    require(validate_teacher_state_delta_v2(delta), "V2 delta rejected a V2 key");
    delta.proposed_for_public_action_key = v1_key;
    require(!validate_teacher_state_delta_v2(delta), "V2 delta accepted a V1 key");

    auto historical_delta = TeacherStateDeltaV1{};
    historical_delta.strategy_profile_id = profile.profile_id;
    historical_delta.proposed_for_public_action_key = v2_key;
    require(!validate_teacher_state_delta(historical_delta),
            "V1 delta accepted a V2 key during V2 migration");
}

void test_reconciliation_and_rejected_transition() {
    const auto profile = make_salamangreat_profile();
    auto state = reset_state(profile);
    state.active_goal_id = "goal.main1.salamangreat";
    state.active_line_id = "line.main1.salamangreat";
    const auto before = state;

    const auto retained = reconcile_strategy_state_with_evidence_v2(
        state, 0, observation(10, "unselect_card"));
    require(retained.has_value() && retained->state == before &&
                retained->invalidation_reason_ids.empty(),
            "V2 reconciliation did not retain commitment across decision kind");

    PublicFactValue stale_fact;
    stale_fact.fact_id = "public.chain.length";
    stale_fact.value_kind = PublicFactValueKind::U64;
    stale_fact.u64_value = 1;
    state.public_resource_facts = {stale_fact};
    const auto contradicted = reconcile_strategy_state_with_evidence_v2(
        state, 0, observation(11, "unselect_card"));
    require(contradicted.has_value() && !contradicted->state.active_goal_id.has_value() &&
                !contradicted->state.active_line_id.has_value() &&
                contradicted->state.completed_line_node_ids.empty() &&
                contradicted->invalidation_reason_ids ==
                    std::vector<std::string>{"public_state_contradiction"},
            "V2 public contradiction did not clear commitment");

    auto rejected_state = reset_state(profile);
    rejected_state.active_goal_id = "goal.main1.salamangreat";
    rejected_state.active_line_id = "line.main1.salamangreat";
    const auto rejected_before = rejected_state;
    StepRejected rejected;
    rejected.authoritative_state_unchanged = true;
    require(observe_step_rejected_v2(rejected_state, rejected),
            "V2 rejected-step observation was not accepted");
    require(rejected_state == rejected_before,
            "V2 rejected-step observation mutated state");
}

void test_v2_commit_and_failure_are_transactional() {
    const auto profile = make_salamangreat_profile();
    const auto v2_key = action_key(PublicCardSelectionOperation::Select, true);
    const auto v1_key = action_key(PublicCardSelectionOperation::None, false);
    const auto proposal_observation = observation(20);

    auto state = reset_state(profile);
    const auto ranking = valid_ranking(profile, v2_key);
    std::string diagnostic;
    require(validate_teacher_ranking_result_v2(ranking, &diagnostic),
            "fixture V2 ranking result did not validate: " + diagnostic);

    AcceptedActionTransition accepted;
    accepted.decision_index = 20;
    accepted.selected_public_action_key = v2_key;
    const auto committed = commit_teacher_state_delta_with_evidence_v2(
        state, ranking, profile, 0, proposal_observation, accepted);
    require(committed.has_value() &&
                state.last_accepted_decision_index == std::optional<std::uint64_t>(20) &&
                state.last_accepted_public_action_key == std::optional<std::string>(v2_key),
            "accepted V2 transition did not commit V2 state");

    const auto before_rejected = state;
    auto wrong_transition = accepted;
    wrong_transition.selected_public_action_key = v1_key;
    require(!commit_teacher_state_delta_with_evidence_v2(
                state, ranking, profile, 0, observation(21), wrong_transition),
            "V2 commit accepted a V1 transition");
    require(state == before_rejected, "rejected V2 commit mutated state");

    const auto before_failed_proposal = state;
    auto invalid_delta = valid_delta(profile, "malformed");
    require(!propose_teacher_state_delta_v2(
                state, observation(21), 0, profile, invalid_delta),
            "V2 proposal accepted a malformed proposed key");
    require(state == before_failed_proposal,
            "failed V2 proposal mutated the current state");
}

}  // namespace

int main() {
    try {
        test_versioned_state_and_delta_validation();
        test_reconciliation_and_rejected_transition();
        test_v2_commit_and_failure_are_transactional();
        std::cout << "teacher_v3_strategy_state_test: PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "teacher_v3_strategy_state_test: " << error.what() << '\n';
        return 1;
    }
}
