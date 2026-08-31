#include "ygo/teacher/strategy_state.hpp"
#include "ygo/teacher/teacher_decision.hpp"

#include <cstdint>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "ygo/environment/episodic_environment.hpp"
#include "ygo/environment/public_action_identity.hpp"
#include "ygo/observation/player_observation.hpp"

namespace {

using ygo::environment::AcceptedActionTransition;
using ygo::environment::PublicActionKeyInput;
using ygo::environment::PublicChoice;
using ygo::environment::PublicChoiceKind;
using ygo::environment::PublicEnvironmentObservation;
using ygo::teacher::EpisodeLocalStrategyStateV1;
using ygo::teacher::CandidateEvaluation;
using ygo::teacher::PublicFactValidityScope;
using ygo::teacher::PublicFactValue;
using ygo::teacher::PublicFactValueKind;
using ygo::teacher::StrategyProfileV1;
using ygo::teacher::TeacherRankingResult;
using ygo::teacher::TeacherStateDeltaV1;

static_assert(std::is_same_v<
              decltype(std::declval<TeacherRankingResult>().proposed_state_delta),
              std::optional<TeacherStateDeltaV1>>);

void require(const bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

StrategyProfileV1 valid_profile() {
    StrategyProfileV1 value;
    value.matchup_id = "ocgforge.matchup.swordsoul_salamangreat.v1";
    value.rules_bundle_id =
        "3adfe6b4cfe2c2805e50b389fc0eb4e70a3b0b6107436614d328fddc865e585f";
    value.format_id = "TCG_ADVANCED_2026_05_18";
    value.duel_mode = "DUEL_MODE_MR5";
    value.duel_flags = 190464;
    value.own_deck_role = 0;
    value.own_deck_id = "ocgforge.swordsoul_tenyi.ml_v1";
    value.own_deck_sha256 =
        "8ee4b699de19ff256e388d46f35b8696a60ff6ec59f0324f060a2468876711b7";
    value.opponent_deck_role = 1;
    value.opponent_deck_id = "ocgforge.salamangreat.ml_v1";
    value.opponent_deck_sha256 =
        "6041abe0a59463d0715ae1da9100090ad487de02a02794e8ec0686d4c0513188";
    ygo::teacher::GoalDefinition secondary_goal;
    secondary_goal.goal_id = "goal.other";
    ygo::teacher::GoalDefinition goal;
    goal.goal_id = "goal.test";
    ygo::teacher::LineDefinition line;
    line.line_id = "line.test";
    line.goal_id = goal.goal_id;
    ygo::teacher::LineNode node;
    node.node_id = "node.test";
    line.nodes = {node};
    value.goals = {secondary_goal, goal};
    value.lines = {line};
    value.profile_id = ygo::teacher::strategy_profile_id(value);
    return value;
}

std::string public_key(const std::uint64_t choice) {
    PublicActionKeyInput input;
    input.action_kind = "yes_no";
    input.choice = PublicChoice{PublicChoiceKind::YesNo, choice % 2, std::nullopt};
    return ygo::environment::public_action_key(input);
}

PublicEnvironmentObservation public_observation(
    const std::uint32_t self_life_points = 8000,
    const std::uint32_t opponent_life_points = 7000,
    const std::uint64_t decision_index = 43,
    const std::uint8_t perspective_player = 0) {
    ygo::observation::PlayerObservation source;
    source.schema_version = "ygo.player_observation.v1";
    source.perspective_player = perspective_player;
    source.decision_index = decision_index;
    source.globals.life_points = {self_life_points, opponent_life_points};
    source.globals.player_to_act = perspective_player;
    source.globals.turn_player = perspective_player;
    source.globals.turn_count = 1;
    source.globals.phase = 2;
    source.globals.chain_length = 0;
    source.globals.terminal = false;
    source.match_context.perspective_player = perspective_player;
    source.match_context.knowledge.own_decklist_known = true;
    source.match_context.knowledge.opponent_decklist_known = false;
    source.decision_context.kind = "yes_no";
    source.decision_context.player = perspective_player;
    return ygo::environment::project_public_observation(source);
}

PublicFactValue u64_fact(const std::string& fact_id, const std::uint64_t value,
                         const PublicFactValidityScope scope =
                             PublicFactValidityScope::CurrentReconciliation) {
    PublicFactValue result;
    result.fact_id = fact_id;
    result.value_kind = PublicFactValueKind::U64;
    result.u64_value = value;
    result.validity_scope = scope;
    return result;
}

PublicFactValue boolean_fact(const std::string& fact_id, const bool value) {
    PublicFactValue result;
    result.fact_id = fact_id;
    result.value_kind = PublicFactValueKind::Boolean;
    result.boolean_value = value;
    return result;
}

PublicFactValue token_fact(const std::string& fact_id, const std::string& value) {
    PublicFactValue result;
    result.fact_id = fact_id;
    result.value_kind = PublicFactValueKind::Token;
    result.token_value = value;
    return result;
}

TeacherStateDeltaV1 replacement_delta(const EpisodeLocalStrategyStateV1& state,
                                      const StrategyProfileV1& profile,
                                      const std::string& selected_key,
                                      const std::uint64_t self_life_points = 8000) {
    TeacherStateDeltaV1 value;
    value.strategy_profile_id = profile.profile_id;
    value.base_last_accepted_decision_index = state.last_accepted_decision_index;
    value.base_last_accepted_public_action_key = state.last_accepted_public_action_key;
    value.proposed_for_public_action_key = selected_key;
    value.active_goal_id = "goal.test";
    value.active_line_id = "line.test";
    value.completed_line_node_ids = {"node.test"};
    value.achieved_goal_ids = {"goal.test"};
    value.public_resource_facts = {
        u64_fact("public.life_points.self", self_life_points)};
    value.public_threat_facts = {u64_fact("public.life_points.opponent", 7000)};
    return value;
}

TeacherRankingResult ranking_result(const TeacherStateDeltaV1& delta,
                                    const std::string& selected_key) {
    TeacherRankingResult result;
    result.status = ygo::teacher::TeacherRankingStatus::Selected;
    CandidateEvaluation evaluation;
    evaluation.public_action_key = selected_key;
    evaluation.status = ygo::teacher::CandidateEvaluationStatus::Supported;
    result.evaluations = {evaluation};
    result.selected_public_action_key = selected_key;
    result.proposed_state_delta = delta;
    return result;
}

AcceptedActionTransition accepted_transition(const std::uint64_t decision_index,
                                             const std::string& selected_key) {
    AcceptedActionTransition value;
    value.decision_index = decision_index;
    value.selected_public_action_key = selected_key;
    return value;
}

EpisodeLocalStrategyStateV1 initial_state(const StrategyProfileV1& profile) {
    const auto state = ygo::teacher::reset_strategy_state(profile);
    require(state.has_value(), "valid profile did not reset strategy state");
    return *state;
}

void test_reset_and_participant_isolation() {
    const auto profile = valid_profile();
    auto first = initial_state(profile);
    const auto second = initial_state(profile);
    require(first == second, "equal validated profiles produced different reset states");
    require(first.active_goal_id == std::nullopt && first.active_line_id == std::nullopt &&
                first.completed_line_node_ids.empty() && first.achieved_goal_ids.empty() &&
                first.public_resource_facts.empty() && first.public_restriction_facts.empty() &&
                first.public_threat_facts.empty() &&
                !first.last_accepted_decision_index.has_value() &&
                !first.last_accepted_public_action_key.has_value(),
            "reset state was not empty");

    first.active_goal_id = "goal.local";
    first.completed_line_node_ids.push_back("node.local");
    require(!first.active_goal_id.has_value() == false && !second.active_goal_id.has_value(),
            "participant/session states share mutable storage");
}

void test_state_and_delta_validation() {
    const auto profile = valid_profile();
    const auto state = initial_state(profile);
    require(ygo::teacher::validate_strategy_state(state), "valid state was rejected");

    auto invalid_profile = state;
    invalid_profile.strategy_profile_id = "not-a-profile-id";
    require(!ygo::teacher::validate_strategy_state(invalid_profile),
            "malformed profile identity was accepted in state");

    auto invalid_key = state;
    invalid_key.last_accepted_public_action_key = "not-a-public-action-key";
    require(!ygo::teacher::validate_strategy_state(invalid_key),
            "malformed last accepted key was accepted");

    auto index_without_key = state;
    index_without_key.last_accepted_decision_index = 1;
    require(!ygo::teacher::validate_strategy_state(index_without_key),
            "state decision index without key was accepted");
    auto key_without_index = state;
    key_without_index.last_accepted_public_action_key = public_key(0);
    require(!ygo::teacher::validate_strategy_state(key_without_index),
            "state key without decision index was accepted");

    auto unsorted_ids = state;
    unsorted_ids.completed_line_node_ids = {"node.b", "node.a"};
    require(!ygo::teacher::validate_strategy_state(unsorted_ids),
            "unsorted state IDs were accepted");
    auto duplicate_ids = state;
    duplicate_ids.achieved_goal_ids = {"goal.a", "goal.a"};
    require(!ygo::teacher::validate_strategy_state(duplicate_ids),
            "duplicate state IDs were accepted");

    auto unknown_fact = state;
    unknown_fact.public_resource_facts = {u64_fact("unknown.fact", 1)};
    require(!ygo::teacher::validate_strategy_state(unknown_fact),
            "unregistered state fact was accepted");
    auto blocked_fact = state;
    blocked_fact.public_resource_facts = {
        token_fact("blocked.private.opponent_hand_identity", "hidden")};
    require(!ygo::teacher::validate_strategy_state(blocked_fact),
            "BLOCKED state fact was accepted");
    auto wrong_kind = state;
    wrong_kind.public_resource_facts = {token_fact("public.life_points.self", "8000")};
    require(!ygo::teacher::validate_strategy_state(wrong_kind),
            "wrong-kind state fact was accepted");
    auto wrong_scope = state;
    wrong_scope.public_resource_facts = {u64_fact(
        "public.life_points.self", 8000, PublicFactValidityScope::AcceptedPublicHistory)};
    require(!ygo::teacher::validate_strategy_state(wrong_scope),
            "wrong-scope state fact was accepted");
    auto out_of_bounds = state;
    out_of_bounds.public_resource_facts = {u64_fact("public.perspective_player", 2)};
    require(!ygo::teacher::validate_strategy_state(out_of_bounds),
            "out-of-bounds state fact was accepted");
    auto duplicate_facts = state;
    duplicate_facts.public_resource_facts = {
        u64_fact("public.life_points.self", 8000),
        u64_fact("public.life_points.self", 7000)};
    require(!ygo::teacher::validate_strategy_state(duplicate_facts),
            "duplicate state facts were accepted");
    auto unsorted_facts = state;
    const auto first_fact = u64_fact("public.life_points.self", 8000);
    const auto second_fact = u64_fact("public.perspective_player", 0);
    if (ygo::teacher::canonical_public_fact_value_bytes(first_fact) <
        ygo::teacher::canonical_public_fact_value_bytes(second_fact)) {
        unsorted_facts.public_resource_facts = {second_fact, first_fact};
    } else {
        unsorted_facts.public_resource_facts = {first_fact, second_fact};
    }
    require(!ygo::teacher::validate_strategy_state(unsorted_facts),
            "unsorted state facts were accepted");

    TeacherStateDeltaV1 delta = replacement_delta(state, profile, public_key(0));
    delta.invalidation_reason_ids = {"unknown.reason"};
    require(!ygo::teacher::validate_teacher_state_delta(delta),
            "unregistered invalidation reason was accepted");
    delta = replacement_delta(state, profile, public_key(0));
    delta.public_resource_facts = {u64_fact("unknown.fact", 1)};
    require(!ygo::teacher::validate_teacher_state_delta(delta),
            "unregistered delta fact was accepted");
    delta = replacement_delta(state, profile, public_key(0));
    delta.invalidation_reason_ids = {"starter_not_resolved", "resource_consumed"};
    require(!ygo::teacher::validate_teacher_state_delta(delta),
            "unsorted invalidation reasons were accepted");
    delta = replacement_delta(state, profile, public_key(0));
    delta.invalidation_reason_ids = {"resource_consumed", "resource_consumed"};
    require(!ygo::teacher::validate_teacher_state_delta(delta),
            "duplicate invalidation reasons were accepted");

    auto delta_index_without_key = replacement_delta(state, profile, public_key(0));
    delta_index_without_key.base_last_accepted_decision_index = 1;
    require(!ygo::teacher::validate_teacher_state_delta(delta_index_without_key),
            "delta base decision index without key was accepted");
    auto delta_key_without_index = replacement_delta(state, profile, public_key(0));
    delta_key_without_index.base_last_accepted_public_action_key = public_key(1);
    require(!ygo::teacher::validate_teacher_state_delta(delta_key_without_index),
            "delta base key without decision index was accepted");
}

void test_plan_references_bind_to_profile() {
    const auto profile = valid_profile();
    const auto state = initial_state(profile);
    const auto selected_key = public_key(0);

    const auto proposal_rejected = [&](const TeacherStateDeltaV1& value,
                                       const std::string& message) {
        require(!ygo::teacher::propose_teacher_state_delta(
                    state, public_observation(), 0, profile, value)
                     .has_value(),
                message);
        auto unchanged = state;
        const auto before = unchanged;
        require(!ygo::teacher::commit_teacher_state_delta(
                    unchanged, ranking_result(value, selected_key), profile,
                    0, public_observation(8000, 7000, 1),
                    accepted_transition(1, selected_key)),
                message + " at commit");
        require(unchanged == before, message + " mutated trusted state");
    };

    auto unknown_goal = replacement_delta(state, profile, selected_key);
    unknown_goal.active_goal_id = "goal.unknown";
    proposal_rejected(unknown_goal, "unknown active goal was accepted");

    auto unknown_achieved_goal = replacement_delta(state, profile, selected_key);
    unknown_achieved_goal.achieved_goal_ids = {"goal.unknown"};
    proposal_rejected(unknown_achieved_goal, "unknown achieved goal was accepted");

    auto unknown_line = replacement_delta(state, profile, selected_key);
    unknown_line.active_line_id = "line.unknown";
    proposal_rejected(unknown_line, "unknown active line was accepted");

    auto wrong_goal = replacement_delta(state, profile, selected_key);
    wrong_goal.active_goal_id = "goal.other";
    proposal_rejected(wrong_goal, "line bound to the wrong active goal was accepted");

    auto unknown_node = replacement_delta(state, profile, selected_key);
    unknown_node.completed_line_node_ids = {"node.unknown"};
    proposal_rejected(unknown_node, "unknown completed node was accepted");

    auto node_without_line = replacement_delta(state, profile, selected_key);
    node_without_line.active_line_id.reset();
    node_without_line.completed_line_node_ids = {"node.test"};
    proposal_rejected(node_without_line, "completed node without active line was accepted");
}

void test_ranking_delta_validation() {
    const auto profile = valid_profile();
    const auto state = initial_state(profile);
    const auto selected_key = public_key(0);
    const auto other_key = public_key(1);
    const auto delta = replacement_delta(state, profile, selected_key);

    auto malformed_delta = delta;
    malformed_delta.public_resource_facts = {u64_fact("unknown.fact", 1)};
    require(!ygo::teacher::validate_teacher_ranking_result(
                ranking_result(malformed_delta, selected_key)),
            "malformed delta inside selected result was accepted");

    auto non_selected_with_delta = ranking_result(delta, selected_key);
    non_selected_with_delta.status = ygo::teacher::TeacherRankingStatus::Blocked;
    non_selected_with_delta.selected_public_action_key.reset();
    require(!ygo::teacher::validate_teacher_ranking_result(non_selected_with_delta),
            "non-selected result carrying a delta was accepted");

    auto mismatched_delta_key = delta;
    mismatched_delta_key.proposed_for_public_action_key = other_key;
    require(!ygo::teacher::validate_teacher_ranking_result(
                ranking_result(mismatched_delta_key, selected_key)),
            "delta key mismatched selected key was accepted");

    auto missing_evaluation = ranking_result(delta, selected_key);
    missing_evaluation.evaluations.clear();
    require(!ygo::teacher::validate_teacher_ranking_result(missing_evaluation),
            "selected result without a matching evaluation was accepted");

    auto unchanged = state;
    const auto before = unchanged;
    require(!ygo::teacher::commit_teacher_state_delta(
                unchanged, missing_evaluation, profile,
                0, public_observation(8000, 7000, 1),
                accepted_transition(1, selected_key)),
            "malformed ranking result unexpectedly committed");
    require(unchanged == before, "malformed ranking result mutated trusted state");
}

void test_proposal_reconciles_current_public_input() {
    const auto profile = valid_profile();
    const auto selected_key = public_key(0);

    auto stale_state = initial_state(profile);
    stale_state.active_goal_id = "goal.test";
    stale_state.active_line_id = "line.test";
    stale_state.completed_line_node_ids = {"node.test"};
    stale_state.achieved_goal_ids = {"goal.test"};
    stale_state.public_resource_facts = {
        u64_fact("public.life_points.self", 8000)};
    const auto before = stale_state;

    auto requested = replacement_delta(stale_state, profile, selected_key, 7000);
    requested.active_goal_id.reset();
    requested.active_line_id.reset();
    requested.completed_line_node_ids.clear();
    requested.achieved_goal_ids.clear();
    const auto proposal = ygo::teacher::propose_teacher_state_delta(
        stale_state, public_observation(7000, 7000, 3), 0, profile, requested);
    require(proposal.has_value(), "proposal did not reconcile current public input");
    require(stale_state == before, "current-public proposal mutated trusted state");
    require(proposal->invalidation_reason_ids ==
                std::vector<std::string>{"public_state_contradiction"},
            "proposal did not carry derived contradiction evidence");
    require(!proposal->active_goal_id.has_value() && !proposal->active_line_id.has_value() &&
                proposal->completed_line_node_ids.empty() &&
                proposal->achieved_goal_ids.empty(),
            "proposal retained stale plan progress after current reconciliation");

    const auto equal_public = ygo::teacher::propose_teacher_state_delta(
        initial_state(profile), public_observation(8000, 7000, 3), 0, profile,
        replacement_delta(initial_state(profile), profile, selected_key));
    require(equal_public.has_value() && equal_public->invalidation_reason_ids.empty(),
            "equal current public input produced invalidation evidence");

    auto malformed_state = initial_state(profile);
    const auto malformed_before = malformed_state;
    require(!ygo::teacher::propose_teacher_state_delta(
                 malformed_state, PublicEnvironmentObservation{}, 0, profile,
                 replacement_delta(malformed_state, profile, selected_key))
                 .has_value(),
            "malformed current observation was accepted");
    require(malformed_state == malformed_before,
            "malformed current observation mutated trusted state");

    auto caller_reason = replacement_delta(initial_state(profile), profile, selected_key);
    caller_reason.invalidation_reason_ids = {"resource_consumed"};
    require(!ygo::teacher::propose_teacher_state_delta(
                 initial_state(profile), public_observation(), 0, profile, caller_reason)
                 .has_value(),
            "caller-authored invalidation reason was accepted");

    const auto repeated_first = ygo::teacher::propose_teacher_state_delta(
        stale_state, public_observation(7000, 7000, 3), 0, profile, requested);
    const auto repeated_second = ygo::teacher::propose_teacher_state_delta(
        stale_state, public_observation(7000, 7000, 3), 0, profile, requested);
    require(repeated_first.has_value() && repeated_second.has_value() &&
                *repeated_first == *repeated_second,
                "identical proposal inputs produced different deltas or evidence");
}

void test_participant_perspective_freshness_and_fact_source() {
    const auto profile = valid_profile();
    const auto selected_key = public_key(0);

    const auto state = initial_state(profile);
    const auto before = state;
    require(!ygo::teacher::propose_teacher_state_delta(
                 state, public_observation(8000, 7000, 12, 1), 0, profile,
                 replacement_delta(state, profile, selected_key))
                 .has_value(),
            "opponent-perspective observation was accepted for participant zero");
    require(state == before, "wrong-perspective proposal mutated trusted state");

    auto prior_state = state;
    prior_state.last_accepted_decision_index = 10;
    prior_state.last_accepted_public_action_key = public_key(1);
    const auto prior_delta = replacement_delta(prior_state, profile, selected_key);
    require(!ygo::teacher::propose_teacher_state_delta(
                 prior_state, public_observation(8000, 7000, 10), 0, profile, prior_delta)
                 .has_value(),
            "current observation equal to the last accepted index was accepted");
    require(!ygo::teacher::propose_teacher_state_delta(
                 prior_state, public_observation(8000, 7000, 9), 0, profile, prior_delta)
                 .has_value(),
            "current observation older than the last accepted index was accepted");
    require(ygo::teacher::propose_teacher_state_delta(
                prior_state, public_observation(8000, 7000, 12), 0, profile, prior_delta)
                .has_value(),
            "later same-participant observation was rejected");

    auto exact = replacement_delta(state, profile, selected_key, 7000);
    exact.public_restriction_facts = {u64_fact("public.turn.phase", 2)};
    require(ygo::teacher::propose_teacher_state_delta(
                state, public_observation(7000, 7000, 12), 0, profile, exact)
                .has_value(),
            "CURRENT facts matching the public snapshot were rejected");

    auto wrong_resource = replacement_delta(state, profile, selected_key, 8000);
    require(!ygo::teacher::propose_teacher_state_delta(
                 state, public_observation(7000, 7000, 12), 0, profile, wrong_resource)
                 .has_value(),
            "registry-valid but stale resource fact was accepted");

    auto wrong_restriction = exact;
    wrong_restriction.public_restriction_facts = {u64_fact("public.turn.phase", 3)};
    require(!ygo::teacher::propose_teacher_state_delta(
                 state, public_observation(7000, 7000, 12), 0, profile, wrong_restriction)
                 .has_value(),
            "registry-valid but unsupported restriction fact was accepted");

    auto wrong_threat = exact;
    wrong_threat.public_threat_facts = {
        u64_fact("public.life_points.opponent", 6000)};
    require(!ygo::teacher::propose_teacher_state_delta(
                 state, public_observation(7000, 7000, 12), 0, profile, wrong_threat)
                 .has_value(),
            "registry-valid but stale threat fact was accepted");
}

void test_pure_proposal_and_accepted_commit() {
    const auto profile = valid_profile();
    const auto initial = initial_state(profile);
    const auto selected_key = public_key(0);
    const auto requested = replacement_delta(initial, profile, selected_key);
    const auto proposed = ygo::teacher::propose_teacher_state_delta(
        initial, public_observation(8000, 7000, 42), 0, profile, requested);
    require(proposed.has_value(), "valid state proposal was rejected");
    require(initial == initial_state(profile), "pure proposal mutated trusted state");
    require(proposed->base_last_accepted_decision_index ==
                initial.last_accepted_decision_index &&
                proposed->base_last_accepted_public_action_key ==
                    initial.last_accepted_public_action_key,
            "proposal did not copy exact base-state bindings");

    TeacherRankingResult result;
    result.status = ygo::teacher::TeacherRankingStatus::Selected;
    result.selected_public_action_key = selected_key;
    result.proposed_state_delta = *proposed;
    require(result.proposed_state_delta.has_value() &&
                result.proposed_state_delta->proposed_for_public_action_key == selected_key,
            "TeacherRankingResult did not carry a value-owned delta");

    auto committed = initial;
    require(ygo::teacher::commit_teacher_state_delta(
                committed, ranking_result(*proposed, selected_key), profile,
                0, public_observation(8000, 7000, 42),
                accepted_transition(42, selected_key)),
            "exact accepted transition did not commit");
    require(committed.last_accepted_decision_index == std::optional<std::uint64_t>(42) &&
                committed.last_accepted_public_action_key == selected_key,
            "accepted transition did not set last accepted values");
    require(committed.active_goal_id == std::optional<std::string>("goal.test") &&
                committed.active_line_id == std::optional<std::string>("line.test") &&
                committed.completed_line_node_ids == std::vector<std::string>{"node.test"} &&
                !committed.public_resource_facts.empty(),
            "accepted replacement image was not applied");
    require(ygo::teacher::validate_strategy_state(committed),
            "committed state failed validation");
}

void test_observation_dominance_and_repeated_reconciliation() {
    const auto profile = valid_profile();
    const auto selected_key = public_key(0);
    auto stale_first = initial_state(profile);
    stale_first.active_goal_id = "goal.test";
    stale_first.active_line_id = "line.test";
    stale_first.completed_line_node_ids = {"node.test"};
    stale_first.achieved_goal_ids = {"goal.test"};
    stale_first.public_resource_facts = {
        u64_fact("public.life_points.self", 8000)};
    auto stale_requested = replacement_delta(stale_first, profile, selected_key, 7000);
    stale_requested.active_goal_id.reset();
    stale_requested.active_line_id.reset();
    stale_requested.completed_line_node_ids.clear();
    stale_requested.achieved_goal_ids.clear();
    const auto stale_proposed = ygo::teacher::propose_teacher_state_delta(
        stale_first, public_observation(7000, 7000, 12), 0, profile, stale_requested);
    require(stale_proposed.has_value(), "dominance proposal was rejected");
    require(stale_proposed->invalidation_reason_ids ==
                std::vector<std::string>{"public_state_contradiction"},
            "proposal-time contradiction evidence was lost");

    auto dominated = stale_first;
    const auto dominated_result = ygo::teacher::commit_teacher_state_delta_with_evidence(
        dominated, ranking_result(*stale_proposed, selected_key), profile,
        0, public_observation(7000, 7000, 12), accepted_transition(12, selected_key));
    require(dominated_result.has_value(),
            "participant-local commit rejected the accepted proposal");
    require(dominated_result->invalidation_reason_ids ==
                std::vector<std::string>{"public_state_contradiction"},
            "proposal-derived reasons were not preserved in commit evidence");
    require(!dominated.active_goal_id.has_value() && !dominated.active_line_id.has_value() &&
                dominated.completed_line_node_ids.empty() && dominated.achieved_goal_ids.empty() &&
                dominated.public_resource_facts ==
                    std::vector<PublicFactValue>{
                        u64_fact("public.life_points.self", 7000)},
            "accepted proposal-frame state was not retained until deferred reconciliation");
    require(dominated_result->state == dominated,
            "commit evidence did not contain the committed value-state");
    require(dominated.last_accepted_public_action_key == selected_key,
            "accepted key was lost during participant-local commit");

    const auto deferred = ygo::teacher::reconcile_strategy_state_with_evidence(
        dominated, 0, public_observation(6000, 7000, 13));
    require(deferred.has_value() &&
                deferred->invalidation_reason_ids ==
                    std::vector<std::string>{"public_state_contradiction"} &&
                deferred->state.public_resource_facts.empty() &&
                !deferred->state.active_goal_id.has_value(),
            "later same-participant reconciliation did not dominate stale proposal memory");

    const auto first = initial_state(profile);
    const auto requested = replacement_delta(first, profile, selected_key, 8000);
    const auto proposed = ygo::teacher::propose_teacher_state_delta(
        first, public_observation(8000, 7000, 2), 0, profile, requested);
    require(proposed.has_value(), "dominance proposal was rejected");

    auto left = first;
    auto right = first;
    const auto left_result = ygo::teacher::commit_teacher_state_delta_with_evidence(
        left, ranking_result(*proposed, selected_key), profile,
        0, public_observation(8000, 7000, 2), accepted_transition(2, selected_key));
    require(left_result.has_value() && left_result->invalidation_reason_ids.empty(),
            "first identical reconciliation failed");
    const auto right_result = ygo::teacher::commit_teacher_state_delta_with_evidence(
        right, ranking_result(*proposed, selected_key), profile,
        0, public_observation(8000, 7000, 2), accepted_transition(2, selected_key));
    require(right_result.has_value() && right_result->invalidation_reason_ids.empty(),
            "second identical reconciliation failed");
    require(left == right, "identical public state/reconciliation diverged");
    require(*left_result == *right_result,
            "identical public state/reconciliation evidence diverged");

    auto stale_fact_state = initial_state(profile);
    stale_fact_state.public_resource_facts = {
        u64_fact("public.life_points.self", 8000)};
    const auto first_reconciliation = ygo::teacher::reconcile_strategy_state_with_evidence(
        stale_fact_state, 0, public_observation(7000, 7000));
    const auto second_reconciliation = ygo::teacher::reconcile_strategy_state_with_evidence(
        stale_fact_state, 0, public_observation(7000, 7000));
    require(first_reconciliation.has_value() && second_reconciliation.has_value(),
            "stale fact reconciliation failed");
    require(first_reconciliation->invalidation_reason_ids ==
                std::vector<std::string>{"public_state_contradiction"},
            "standalone stale reconciliation lacked contradiction evidence");
    require(*first_reconciliation == *second_reconciliation,
            "repeated stale reconciliation was not deterministic");
}

void test_commit_mismatches_are_atomic() {
    const auto profile = valid_profile();
    const auto selected_key = public_key(0);
    const auto other_key = public_key(1);
    const auto initial = initial_state(profile);
    const auto valid_delta = replacement_delta(initial, profile, selected_key);

    const auto attempt = [&](const TeacherStateDeltaV1& delta,
                             const std::string& selected,
                             const AcceptedActionTransition& transition,
                             const PublicEnvironmentObservation& observation) {
        auto state = initial;
        const auto before = state;
        require(!ygo::teacher::commit_teacher_state_delta(
                    state, ranking_result(delta, selected), profile, 0, observation, transition),
                "invalid commit unexpectedly succeeded");
        require(state == before, "rejected commit partially mutated trusted state");
    };

    auto profile_mismatch = valid_delta;
    profile_mismatch.strategy_profile_id =
        std::string(ygo::teacher::kStrategyProfileIdentityPrefix) + std::string(64, 'b');
    attempt(profile_mismatch, selected_key, accepted_transition(1, selected_key),
            public_observation());

    auto base_index_mismatch = valid_delta;
    base_index_mismatch.base_last_accepted_decision_index = 99;
    attempt(base_index_mismatch, selected_key, accepted_transition(1, selected_key),
            public_observation());

    auto base_key_mismatch = valid_delta;
    base_key_mismatch.base_last_accepted_public_action_key = other_key;
    attempt(base_key_mismatch, selected_key, accepted_transition(1, selected_key),
            public_observation());

    attempt(valid_delta, other_key, accepted_transition(1, selected_key),
            public_observation());
    attempt(valid_delta, selected_key, accepted_transition(1, other_key),
            public_observation());
    attempt(valid_delta, selected_key, accepted_transition(1, selected_key),
            PublicEnvironmentObservation{});

    auto invalid_fact = valid_delta;
    invalid_fact.public_resource_facts = {u64_fact("unknown.fact", 1)};
    attempt(invalid_fact, selected_key, accepted_transition(1, selected_key),
            public_observation());
}

void test_frame_local_commit_and_deferred_reconciliation() {
    const auto profile = valid_profile();
    const auto selected_key = public_key(0);

    const auto initial = initial_state(profile);
    const auto proposal_observation = public_observation(8000, 7000, 12, 0);
    const auto delta = replacement_delta(initial, profile, selected_key);
    const auto proposal = ygo::teacher::propose_teacher_state_delta(
        initial, proposal_observation, 0, profile, delta);
    require(proposal.has_value(), "frame-local proposal was rejected");

    auto mismatched_fact_delta = *proposal;
    mismatched_fact_delta.public_resource_facts = {
        u64_fact("public.life_points.self", 7000)};
    auto mismatched_fact_state = initial;
    require(!ygo::teacher::commit_teacher_state_delta(
                mismatched_fact_state,
                ranking_result(mismatched_fact_delta, selected_key), profile, 0,
                proposal_observation, accepted_transition(12, selected_key)),
            "commit accepted a CURRENT fact absent from the proposal snapshot");
    require(mismatched_fact_state == initial,
            "snapshot-mismatched commit mutated trusted state");

    auto committed = initial;
    const auto opponent_observation = public_observation(2000, 9000, 13, 1);
    require(opponent_observation.perspective_player == 1,
            "opponent test observation was not perspective-bound");
    const auto committed_result = ygo::teacher::commit_teacher_state_delta_with_evidence(
        committed, ranking_result(*proposal, selected_key), profile, 0,
        proposal_observation, accepted_transition(12, selected_key));
    require(committed_result.has_value(),
            "participant-local commit required an opponent observation");
    require(committed.last_accepted_decision_index == std::optional<std::uint64_t>(12) &&
                committed.last_accepted_public_action_key == selected_key,
            "accepted transition markers were not committed");
    require(committed.public_resource_facts ==
                std::vector<PublicFactValue>{
                    u64_fact("public.life_points.self", 8000)},
            "accepted proposal-frame facts were not retained until deferred reconciliation");
    require(committed_result->invalidation_reason_ids.empty(),
            "commit synthesized post-acceptance opponent-frame evidence");

    const auto attempt_frame = [&](const std::uint64_t transition_index,
                                   const std::string& message) {
        auto state = initial;
        require(!ygo::teacher::commit_teacher_state_delta(
                    state, ranking_result(*proposal, selected_key), profile, 0,
                    proposal_observation, accepted_transition(transition_index, selected_key)),
                message);
        require(state == initial, message + " mutated trusted state");
    };
    attempt_frame(11, "accepted transition before proposal frame was accepted");
    attempt_frame(13, "accepted transition after proposal frame was accepted");

    const auto later = ygo::teacher::reconcile_strategy_state_with_evidence(
        committed, 0, public_observation(7000, 7000, 13, 0));
    require(later.has_value() &&
                later->invalidation_reason_ids ==
                    std::vector<std::string>{"public_state_contradiction"} &&
                later->state.public_resource_facts.empty(),
            "later same-participant observation did not perform deferred reconciliation");
}

}  // namespace

int main() {
    try {
        test_reset_and_participant_isolation();
        test_state_and_delta_validation();
        test_plan_references_bind_to_profile();
        test_ranking_delta_validation();
        test_proposal_reconciles_current_public_input();
        test_pure_proposal_and_accepted_commit();
        test_observation_dominance_and_repeated_reconciliation();
        test_commit_mismatches_are_atomic();
        test_participant_perspective_freshness_and_fact_source();
        test_frame_local_commit_and_deferred_reconciliation();
        std::cout << "teacher_strategy_state_test: PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "teacher_strategy_state_test: FAIL: " << error.what() << '\n';
        return 1;
    }
}
