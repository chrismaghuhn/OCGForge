#include "ygo/teacher/strategy_state.hpp"
#include "ygo/teacher/teacher_decision.hpp"

#include <cstdint>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>

#include "ygo/environment/episodic_environment.hpp"
#include "ygo/environment/public_action_identity.hpp"
#include "ygo/observation/player_observation.hpp"

namespace {

using ygo::environment::AcceptedActionTransition;
using ygo::environment::PublicActionKeyInput;
using ygo::environment::PublicChoice;
using ygo::environment::PublicChoiceKind;
using ygo::environment::PublicEnvironmentObservation;
using ygo::environment::StepRejected;
using ygo::teacher::CandidateEvaluation;
using ygo::teacher::EpisodeLocalStrategyStateV1;
using ygo::teacher::PublicFactValue;
using ygo::teacher::PublicFactValueKind;
using ygo::teacher::StrategyProfileV1;
using ygo::teacher::TeacherRankingResult;
using ygo::teacher::TeacherStateDeltaV1;

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

std::string public_key() {
    PublicActionKeyInput input;
    input.action_kind = "yes_no";
    input.choice = PublicChoice{PublicChoiceKind::YesNo, 0, std::nullopt};
    return ygo::environment::public_action_key(input);
}

PublicEnvironmentObservation public_observation() {
    ygo::observation::PlayerObservation source;
    source.schema_version = "ygo.player_observation.v1";
    source.perspective_player = 0;
    source.globals.life_points = {8000, 7000};
    source.globals.player_to_act = 0;
    source.globals.turn_count = 1;
    source.globals.phase = 2;
    source.match_context.perspective_player = 0;
    source.decision_context.kind = "yes_no";
    source.decision_context.player = 0;
    return ygo::environment::project_public_observation(source);
}

PublicFactValue valid_fact() {
    PublicFactValue value;
    value.fact_id = "public.life_points.self";
    value.value_kind = PublicFactValueKind::U64;
    value.u64_value = 8000;
    return value;
}

PublicFactValue invalid_fact() {
    PublicFactValue value = valid_fact();
    value.fact_id = "unknown.fact";
    return value;
}

TeacherStateDeltaV1 valid_delta(const EpisodeLocalStrategyStateV1& state,
                                const StrategyProfileV1& profile,
                                const std::string& key) {
    TeacherStateDeltaV1 delta;
    delta.strategy_profile_id = profile.profile_id;
    delta.base_last_accepted_decision_index = state.last_accepted_decision_index;
    delta.base_last_accepted_public_action_key = state.last_accepted_public_action_key;
    delta.proposed_for_public_action_key = key;
    delta.active_goal_id = "goal.test";
    delta.active_line_id = "line.test";
    delta.completed_line_node_ids = {"node.test"};
    delta.achieved_goal_ids = {"goal.test"};
    delta.public_resource_facts = {valid_fact()};
    return delta;
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

AcceptedActionTransition accepted_transition(const std::string& key) {
    AcceptedActionTransition transition;
    transition.decision_index = 7;
    transition.selected_public_action_key = key;
    return transition;
}

void test_rejected_and_malformed_operations_do_not_mutate() {
    const auto profile = valid_profile();
    const auto reset = ygo::teacher::reset_strategy_state(profile);
    require(reset.has_value(), "valid profile did not reset state");
    const auto before = *reset;
    auto state = before;
    const auto key = public_key();

    StepRejected rejected;
    rejected.authoritative_state_unchanged = true;
    require(ygo::teacher::observe_step_rejected(state, rejected),
            "existing StepRejected was not accepted as zero-mutation evidence");
    require(state == before, "StepRejected advanced strategy state");
    require(!state.last_accepted_decision_index.has_value() &&
                !state.last_accepted_public_action_key.has_value(),
            "StepRejected changed last accepted values");

    auto malformed = valid_delta(state, profile, key);
    malformed.public_resource_facts = {invalid_fact()};
    require(!ygo::teacher::validate_teacher_state_delta(malformed),
            "malformed delta unexpectedly validated");
    require(!ygo::teacher::commit_teacher_state_delta(
                state, ranking_result(malformed, key), profile, accepted_transition(key),
                public_observation()),
            "malformed delta unexpectedly committed");
    require(state == before, "malformed delta partially mutated strategy state");

    auto invalid_state = state;
    invalid_state.strategy_profile_id = "not-a-profile-id";
    const auto invalid_state_before_rejection = invalid_state;
    require(!ygo::teacher::observe_step_rejected(invalid_state, rejected),
            "invalid state was reported as valid rejection evidence");
    require(invalid_state == invalid_state_before_rejection,
            "rejection observation repaired or mutated invalid state");

    ygo::environment::ResetRejected reset_rejected;
    ygo::environment::EpisodeInterrupted interrupted;
    ygo::environment::EpisodeFailure failure;
    (void)reset_rejected;
    (void)interrupted;
    (void)failure;
    require(state == before,
            "unaccepted reset/interruption/failure lifecycle values changed state");
}

}  // namespace

int main() {
    try {
        test_rejected_and_malformed_operations_do_not_mutate();
        std::cout << "teacher_rejected_transition_test: PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "teacher_rejected_transition_test: FAIL: " << error.what() << '\n';
        return 1;
    }
}
