#include "ygo/teacher/candidate_features.hpp"
#include "ygo/teacher/goal_line_controller.hpp"
#include "ygo/teacher/recovery_controller.hpp"
#include "ygo/teacher/strategy_profile.hpp"
#include "ygo/teacher/strategy_state.hpp"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "ygo/environment/episodic_environment.hpp"
#include "ygo/environment/public_action_identity.hpp"
#include "ygo/observation/player_observation.hpp"

namespace {

using namespace ygo::environment;
using namespace ygo::teacher;

void require(const bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

PredicateAtom token_atom(const std::string& value) {
    PredicateAtom atom;
    atom.kind = PredicateAtomKind::Token;
    atom.token = value;
    return atom;
}

PredicateAtom u64_atom(const std::uint64_t value) {
    PredicateAtom atom;
    atom.kind = PredicateAtomKind::U64;
    atom.u64 = value;
    return atom;
}

PredicateRef predicate(const PredicateScope scope, const std::string& id,
                       std::vector<PredicateAtom> arguments = {}) {
    PredicateRef value;
    value.scope = scope;
    value.predicate_id = id;
    value.arguments = std::move(arguments);
    return value;
}

PredicateRef public_self_at_least() {
    return predicate(PredicateScope::Observation, "observation.fact_u64_at_least",
                     {token_atom("public.life_points.self"), u64_atom(1)});
}

PredicateRef candidate_action(const std::string& action_kind) {
    return predicate(PredicateScope::Candidate, "candidate.action_kind_equals",
                     {token_atom(action_kind)});
}

PredicateRef candidate_choice_present() {
    return predicate(PredicateScope::Candidate, "candidate.choice_present");
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
    value.card_roles = {{100, {"role.starter"}}};
    value.resources = {{"resource.phase", "public.turn.phase", 3, 10, 5}};
    value.candidate_intents = {
        {"intent.advance", {candidate_action("yes_no")}},
        {"intent.recover", {candidate_choice_present()}},
    };

    GoalDefinition active_goal;
    active_goal.goal_id = "goal.active";
    active_goal.priority = 100;
    active_goal.preconditions = {public_self_at_least()};
    active_goal.completion_predicates = {public_self_at_least()};
    GoalDefinition recovery_goal;
    recovery_goal.goal_id = "goal.recovery";
    recovery_goal.priority = 200;
    recovery_goal.preconditions = {public_self_at_least()};
    recovery_goal.completion_predicates = {public_self_at_least()};
    value.goals = {active_goal, recovery_goal};

    LineNode completed;
    completed.node_id = "node.completed";
    completed.candidate_intent_ids = {"intent.advance"};
    completed.completion_predicates = {public_self_at_least()};

    LineNode ready;
    ready.node_id = "node.ready";
    ready.candidate_intent_ids = {"intent.recover"};
    ready.completion_predicates = {public_self_at_least()};

    LineNode blocked;
    blocked.node_id = "node.blocked";
    blocked.candidate_intent_ids = {"intent.advance"};
    blocked.completion_predicates = {public_self_at_least()};

    LineDefinition active_line;
    active_line.line_id = "line.active";
    active_line.goal_id = "goal.active";
    active_line.applicability_predicates = {public_self_at_least()};
    active_line.required_resources = {{"resource.phase", 1}};
    active_line.nodes = {blocked, completed, ready};
    active_line.dependencies = {{"node.ready", "node.blocked"}};
    active_line.recovery_edge_ids = {"recovery.a", "recovery.b", "recovery.blocked",
                                     "recovery.completed", "recovery.line"};

    LineNode recovery_node;
    recovery_node.node_id = "node.recovery";
    recovery_node.candidate_intent_ids = {"intent.recover"};
    recovery_node.completion_predicates = {public_self_at_least()};
    LineDefinition recovery_line;
    recovery_line.line_id = "line.recovery";
    recovery_line.goal_id = "goal.recovery";
    recovery_line.applicability_predicates = {public_self_at_least()};
    recovery_line.required_resources = {{"resource.phase", 1}};
    recovery_line.nodes = {recovery_node};
    value.lines = {active_line, recovery_line};

    RecoveryEdge first;
    first.recovery_edge_id = "recovery.a";
    first.source_kind = RecoverySourceKind::Node;
    first.source_id = "node.ready";
    first.invalidation_reason_ids = {"public_state_contradiction"};
    first.preconditions = {public_self_at_least()};
    first.candidate_intent_ids = {"intent.recover"};
    first.target_goal_id = "goal.recovery";
    first.target_line_id = "line.recovery";
    first.confidence_cap = ConfidenceClass::Medium;

    RecoveryEdge second = first;
    second.recovery_edge_id = "recovery.b";

    RecoveryEdge blocked_edge = first;
    blocked_edge.recovery_edge_id = "recovery.blocked";
    blocked_edge.source_id = "node.blocked";

    RecoveryEdge completed_edge = first;
    completed_edge.recovery_edge_id = "recovery.completed";
    completed_edge.source_id = "node.completed";

    RecoveryEdge line_edge = first;
    line_edge.recovery_edge_id = "recovery.line";
    line_edge.source_kind = RecoverySourceKind::Line;
    line_edge.source_id = "line.active";
    line_edge.target_goal_id = "goal.active";
    line_edge.target_line_id = "line.active";
    line_edge.confidence_cap = ConfidenceClass::Low;
    RecoveryEdge goal_edge = first;
    goal_edge.recovery_edge_id = "recovery.goal";
    goal_edge.source_kind = RecoverySourceKind::Goal;
    goal_edge.source_id = "goal.active";
    goal_edge.target_goal_id = "goal.active";
    goal_edge.target_line_id = "line.active";
    goal_edge.confidence_cap = ConfidenceClass::Fallback;
    value.recovery_edges = {first, second, blocked_edge, completed_edge, goal_edge, line_edge};

    value.profile_id = strategy_profile_id(value);
    return value;
}

PublicEnvironmentObservation public_observation(const std::uint64_t decision_index = 12,
                                                const std::uint8_t perspective_player = 0) {
    ygo::observation::PlayerObservation source;
    source.schema_version = "ygo.player_observation.v1";
    source.perspective_player = perspective_player;
    source.decision_index = decision_index;
    source.globals.life_points = {8000, 7000};
    source.globals.player_to_act = perspective_player;
    source.globals.turn_player = perspective_player;
    source.globals.turn_count = 1;
    source.globals.phase = 2;
    source.match_context.perspective_player = perspective_player;
    source.match_context.knowledge.own_decklist_known = true;
    source.match_context.knowledge.opponent_decklist_known = false;
    source.decision_context.kind = "yes_no";
    source.decision_context.player = perspective_player;
    return project_public_observation(source);
}

PublicFactValue self_fact(const std::uint64_t value) {
    PublicFactValue fact;
    fact.fact_id = "public.life_points.self";
    fact.value_kind = PublicFactValueKind::U64;
    fact.u64_value = value;
    return fact;
}

EpisodeLocalStrategyStateV1 active_state(const StrategyProfileV1& profile) {
    const auto reset = reset_strategy_state(profile);
    require(reset.has_value(), "valid profile did not reset strategy state");
    auto state = *reset;
    state.active_goal_id = "goal.active";
    state.active_line_id = "line.active";
    state.completed_line_node_ids = {"node.completed"};
    return state;
}

EnvironmentActionCandidate recovery_candidate() {
    EnvironmentActionCandidate value;
    value.action_kind = EnvironmentActionKind::YesNo;
    value.choice = PublicChoice{PublicChoiceKind::YesNo, 1, std::nullopt};
    PublicActionKeyInput key;
    key.action_kind = "yes_no";
    key.choice = value.choice;
    value.public_action_key = public_action_key(key);
    return value;
}

void test_recovery_sources_and_pre_ready_context() {
    const auto profile = valid_profile();
    const auto state = active_state(profile);
    auto stale_state = state;
    stale_state.public_resource_facts = {self_fact(7000)};

    const auto selected = select_recovery_edge(profile, stale_state, public_observation(), 0);
    require(selected.status == PredicateEvaluationStatus::True &&
                selected.recovery_edge_id == std::optional<std::string>("recovery.a") &&
                selected.target_goal_id == std::optional<std::string>("goal.recovery"),
            "deterministic recovery tie/source selection changed");
    const auto repeated = select_recovery_edge(profile, stale_state, public_observation(), 0);
    require(repeated == selected, "identical recovery input was not deterministic");
    const auto wrong_participant =
        select_recovery_edge(profile, stale_state,
                             public_observation(12, 1), 0);
    require(wrong_participant.status == PredicateEvaluationStatus::Invalid,
            "wrong participant recovery observation was accepted");

    auto stale_index_state = stale_state;
    stale_index_state.last_accepted_decision_index = 12;
    stale_index_state.last_accepted_public_action_key = recovery_candidate().public_action_key;
    const auto stale_index =
        select_recovery_edge(profile, stale_index_state, public_observation(12), 0);
    require(stale_index.status == PredicateEvaluationStatus::Invalid,
            "equal recovery observation index was accepted");

    auto goal_only_state = state;
    goal_only_state.active_line_id.reset();
    goal_only_state.completed_line_node_ids.clear();
    goal_only_state.public_resource_facts = {self_fact(7000)};
    const auto goal_selected =
        select_recovery_edge(profile, goal_only_state, public_observation(), 0);
    require(goal_selected.status == PredicateEvaluationStatus::True &&
                goal_selected.recovery_edge_id == std::optional<std::string>("recovery.goal"),
            "GOAL recovery source did not match the pre-active goal");

    auto unchanged_state = state;
    unchanged_state.public_resource_facts = {self_fact(8000)};
    auto no_reason =
        select_recovery_edge(profile, unchanged_state, public_observation(), 0);
    require(no_reason.status == PredicateEvaluationStatus::False &&
                !no_reason.recovery_edge_id.has_value(),
            "recovery without invalidation reason was accepted");

    auto no_ready_state = state;
    no_ready_state.completed_line_node_ids = {"node.blocked", "node.completed", "node.ready"};
    no_ready_state.public_resource_facts = {self_fact(7000)};
    const auto line_fallback =
        select_recovery_edge(profile, no_ready_state, public_observation(), 0);
    require(line_fallback.recovery_edge_id ==
                std::optional<std::string>("recovery.line"),
            "NODE recovery matched without a pre-ready node");

    auto source_filter_profile = profile;
    source_filter_profile.lines[0].recovery_edge_ids = {"recovery.blocked",
                                                         "recovery.completed"};
    source_filter_profile.recovery_edges.erase(
        std::remove_if(source_filter_profile.recovery_edges.begin(),
                       source_filter_profile.recovery_edges.end(),
                       [](const auto& edge) { return edge.recovery_edge_id == "recovery.goal"; }),
        source_filter_profile.recovery_edges.end());
    source_filter_profile.profile_id = strategy_profile_id(source_filter_profile);

    auto completed_state = active_state(source_filter_profile);
    completed_state.completed_line_node_ids = {"node.blocked", "node.completed", "node.ready"};
    const auto completed = select_recovery_edge(
        source_filter_profile, completed_state, public_observation(), 0);
    require(completed.status == PredicateEvaluationStatus::False &&
                !completed.recovery_edge_id.has_value(),
            "completed node was accepted as a ready recovery source");

    auto nonready_state = active_state(source_filter_profile);
    nonready_state.completed_line_node_ids = {"node.completed"};
    const auto nonready = select_recovery_edge(
        source_filter_profile, nonready_state, public_observation(), 0);
    require(nonready.status == PredicateEvaluationStatus::False &&
                !nonready.recovery_edge_id.has_value(),
            "non-ready node was accepted as a recovery source");

    auto unlisted_profile = profile;
    unlisted_profile.lines[0].recovery_edge_ids = {"recovery.line"};
    unlisted_profile.profile_id = strategy_profile_id(unlisted_profile);
    auto unlisted_state = active_state(unlisted_profile);
    unlisted_state.public_resource_facts = {self_fact(7000)};
    const auto unlisted =
        select_recovery_edge(unlisted_profile, unlisted_state, public_observation(), 0);
    require(unlisted.recovery_edge_id == std::optional<std::string>("recovery.line"),
            "unlisted NODE recovery edge was eligible");
}

void test_recovery_preconditions_and_progress() {
    const auto profile = valid_profile();
    const auto state = active_state(profile);
    const auto observation = public_observation();
    auto stale_state = state;
    stale_state.public_resource_facts = {self_fact(7000)};
    const auto recovery =
        select_recovery_edge(profile, stale_state, public_observation(), 0);
    require(recovery.recovery_edge_id.has_value(), "eligible recovery edge missing");

    GoalLineSelection active_line;
    active_line.status = PredicateEvaluationStatus::True;
    active_line.goal_id = "goal.active";
    active_line.line_id = "line.active";
    active_line.ready_node_ids = {"node.ready"};
    const auto both = evaluate_goal_line_progress(
        profile, active_line, recovery, recovery_candidate(), observation, 0);
    require(both.status == CandidateEvaluationStatus::Supported &&
                both.contributions.size() == 1 && both.contributions[0].value == 3,
            "active-line plus recovery progress did not use max(+3,+2)=+3");

    GoalLineSelection no_line;
    no_line.status = PredicateEvaluationStatus::True;
    no_line.goal_id = "goal.recovery";
    const auto recovery_only = evaluate_goal_line_progress(
        profile, no_line, recovery, recovery_candidate(), observation, 0);
    require(recovery_only.status == CandidateEvaluationStatus::Supported &&
                recovery_only.contributions.size() == 1 &&
                recovery_only.contributions[0].value == 2,
            "recovery-only progress did not contribute exact +2");

    RecoverySelection unsupported;
    unsupported.status = PredicateEvaluationStatus::Unsupported;
    const auto unsupported_outcome = evaluate_goal_line_progress(
        profile, no_line, unsupported, recovery_candidate(), observation, 0);
    require(unsupported_outcome.status == CandidateEvaluationStatus::Unsupported &&
                unsupported_outcome.contributions.empty(),
            "unsupported recovery proof produced a score");

    RecoverySelection invalid;
    invalid.status = PredicateEvaluationStatus::Invalid;
    const auto invalid_outcome = evaluate_goal_line_progress(
        profile, no_line, invalid, recovery_candidate(), observation, 0);
    require(invalid_outcome.status == CandidateEvaluationStatus::Invalid &&
                invalid_outcome.contributions.empty(),
            "invalid recovery proof produced a score");

    auto missing_fact_profile = profile;
    const auto missing_precondition = predicate(
        PredicateScope::Observation, "observation.fact_i32_equals",
        {token_atom("public.last_event.amount"),
         PredicateAtom{PredicateAtomKind::I32, {}, 0, 0, 0, false}});
    for (auto& edge : missing_fact_profile.recovery_edges) {
        edge.preconditions = {missing_precondition};
    }
    missing_fact_profile.profile_id = strategy_profile_id(missing_fact_profile);
    auto missing_fact_state = active_state(missing_fact_profile);
    missing_fact_state.public_resource_facts = {self_fact(7000)};
    const auto missing_facts =
        select_recovery_edge(missing_fact_profile, missing_fact_state,
                             public_observation(), 0);
    require(missing_facts.status == PredicateEvaluationStatus::Unsupported,
            "missing recovery precondition fact was not UNSUPPORTED");
}

}  // namespace

int main() {
    try {
        test_recovery_sources_and_pre_ready_context();
        test_recovery_preconditions_and_progress();
        std::cout << "teacher_recovery_test: PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "teacher_recovery_test: FAIL: " << error.what() << '\n';
        return 1;
    }
}
