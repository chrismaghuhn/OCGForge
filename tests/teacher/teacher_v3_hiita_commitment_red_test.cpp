#include "ygo/environment/public_action_identity.hpp"
#include "ygo/environment/public_decision.hpp"
#include "ygo/environment/public_environment_observation.hpp"
#include "ygo/observation/player_observation.hpp"
#include "ygo/policy/policy.hpp"
#include "ygo/teacher/strategy_profile.hpp"
#include "ygo/teacher/strategy_state.hpp"
#include "ygo/teacher/teacher_core.hpp"

#include <cstdint>
#include <functional>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace ygo::environment;
using namespace ygo::teacher;

void require(const bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

PredicateRef card_selection_intent() {
    PredicateRef result;
    result.scope = PredicateScope::Candidate;
    result.predicate_id = "candidate.action_kind_equals";
    PredicateAtom argument;
    argument.kind = PredicateAtomKind::Token;
    argument.token = "card_selection";
    result.arguments = {std::move(argument)};
    return result;
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

    CandidateIntentDefinition active_intent;
    active_intent.intent_id = "intent.active_card_selection";
    active_intent.public_predicates = {card_selection_intent()};
    value.candidate_intents = {active_intent};

    GoalDefinition other_goal;
    other_goal.goal_id = "goal.other";

    GoalDefinition active_goal;
    active_goal.goal_id = "goal.test";
    value.goals = {other_goal, active_goal};

    LineNode node;
    node.node_id = "node.test";
    node.candidate_intent_ids = {"intent.active_card_selection"};

    LineDefinition line;
    line.line_id = "line.test";
    line.goal_id = active_goal.goal_id;
    line.nodes = {node};
    value.lines = {line};

    value.profile_id = strategy_profile_id(value);
    return value;
}

PublicEnvironmentObservation public_observation(
    const std::uint64_t decision_index = 234) {
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

EpisodeLocalStrategyStateV1 reset_state(
    const StrategyProfileV1& profile) {
    const auto state = reset_strategy_state(profile);
    require(state.has_value(), "valid B1 profile did not reset strategy state");
    return *state;
}

EpisodeLocalStrategyStateV1 retained_state(
    const StrategyProfileV1& profile) {
    auto state = reset_state(profile);
    state.active_goal_id = "goal.test";
    state.active_line_id = "line.test";
    return state;
}

EnvironmentActionCandidate make_card_candidate(
    const std::string& locator,
    const PublicCardSelectionOperation operation,
    const bool v2) {
    EnvironmentActionCandidate result;
    result.action_kind = EnvironmentActionKind::CardSelection;
    result.source_reference =
        PublicCardReference{PublicCardReferenceKind::VisibleCard, locator};
    result.card_selection_operation = operation;

    PublicActionKeyInput key;
    key.action_kind = "card_selection";
    key.source_reference = result.source_reference;
    key.card_selection_operation = operation;
    result.public_action_key =
        v2 ? public_action_key_v2(key) : public_action_key(key);
    return result;
}

EnvironmentActionCandidate make_cancel_candidate(const bool v2) {
    EnvironmentActionCandidate result;
    result.action_kind = EnvironmentActionKind::Cancel;

    PublicActionKeyInput key;
    key.action_kind = "cancel";
    result.public_action_key =
        v2 ? public_action_key_v2(key) : public_action_key(key);
    return result;
}

bool throws_invalid_argument(const std::function<void()>& action) {
    try {
        action();
    } catch (const std::invalid_argument&) {
        return true;
    } catch (...) {
        return false;
    }
    return false;
}

std::string ranking_status_name(const TeacherRankingStatus status) {
    switch (status) {
    case TeacherRankingStatus::Selected:
        return "selected";
    case TeacherRankingStatus::InvalidInput:
        return "invalid_input";
    case TeacherRankingStatus::Blocked:
        return "blocked";
    case TeacherRankingStatus::Unsupported:
        return "unsupported";
    }
    return "unknown";
}

TeacherRankingResult propose(
    const StrategyProfileV1& profile,
    const EpisodeLocalStrategyStateV1& state,
    const PublicEnvironmentObservation& observation,
    const std::vector<EnvironmentActionCandidate>& candidates) {
    const ygo::policy::PolicyInput input{observation, candidates};
    return TeacherCore{}.propose(input, profile, state);
}

void require_v2_candidate_shape(
    const EnvironmentActionCandidate& candidate,
    const PublicCardSelectionOperation expected_operation) {
    require(is_public_action_key_v2(candidate.public_action_key),
            "fixture did not generate a valid V2 public action key");
    require(candidate.action_kind == EnvironmentActionKind::CardSelection,
            "V2 material fixture changed its action family");
    require(candidate.card_selection_operation == expected_operation,
            "fixture operation metadata does not match the expected public operation");
}

void test_historical_v1_path() {
    const auto profile = valid_profile();
    require(validate_strategy_profile(profile),
            "B1 profile is not valid for the historical guard");
    const auto observation = public_observation();
    const auto candidates = std::vector<EnvironmentActionCandidate>{
        make_card_candidate("p0:MONSTER_ZONE:0",
                            PublicCardSelectionOperation::None, false),
        make_card_candidate("p0:MONSTER_ZONE:1",
                            PublicCardSelectionOperation::None, false),
        make_cancel_candidate(false),
    };
    for (const auto& candidate : candidates) {
        require(is_public_action_key(candidate.public_action_key),
                "historical fixture did not generate a valid V1 key");
        require(!is_public_action_key_v2(candidate.public_action_key),
                "historical V1 key was accepted by the V2 validator");
        require(candidate.card_selection_operation ==
                    PublicCardSelectionOperation::None,
                "historical V1 candidate carried non-None operation metadata");
    }

    const auto result = propose(profile, retained_state(profile), observation, candidates);
    require(result.status == TeacherRankingStatus::Selected,
            "historical V1 Teacher path was not structurally acceptable");
    require(result.selected_public_action_key.has_value() &&
                *result.selected_public_action_key !=
                    candidates.back().public_action_key,
            "historical V1 Teacher path selected Cancel over active card progress");
}

void test_v1_state_rejects_v2_action_key() {
    const auto profile = valid_profile();
    auto state = reset_state(profile);
    state.last_accepted_decision_index = 233;
    state.last_accepted_public_action_key =
        make_card_candidate("p0:MONSTER_ZONE:0",
                            PublicCardSelectionOperation::Select, true)
            .public_action_key;
    require(!validate_strategy_state(state),
            "EpisodeLocalStrategyStateV1 accepted a V2 public action key");
}

void test_codec_and_mixed_domain_guards() {
    const auto v1 = make_card_candidate(
        "p0:MONSTER_ZONE:0", PublicCardSelectionOperation::None, false);
    const auto v2 = make_card_candidate(
        "p0:MONSTER_ZONE:0", PublicCardSelectionOperation::Select, true);
    require(is_public_action_key(v1.public_action_key) &&
                !is_public_action_key_v2(v1.public_action_key),
            "V1/V2 action validators did not remain version-specific");
    require(is_public_action_key_v2(v2.public_action_key) &&
                !is_public_action_key(v2.public_action_key),
            "V2/V1 action validators did not remain version-specific");

    PublicActionKeyInput v1_with_operation;
    v1_with_operation.action_kind = "card_selection";
    v1_with_operation.card_selection_operation =
        PublicCardSelectionOperation::Select;
    require(throws_invalid_argument([&] {
                (void)public_action_key(v1_with_operation);
            }),
            "V1 codec accepted non-None card-selection operation metadata");

    PublicActionKeyInput non_card_with_operation;
    non_card_with_operation.action_kind = "cancel";
    non_card_with_operation.card_selection_operation =
        PublicCardSelectionOperation::Select;
    require(throws_invalid_argument([&] {
                (void)public_action_key_v2(non_card_with_operation);
            }),
            "V2 codec accepted Select metadata on a non-CardSelection action");

    require(!is_public_action_key_v2("public_action.v2.malformed"),
            "malformed V2 public action key was accepted");

    const auto profile = valid_profile();
    const auto mixed = std::vector<EnvironmentActionCandidate>{
        v1, v2, make_cancel_candidate(true)};
    const auto mixed_result = propose(
        profile, retained_state(profile), public_observation(), mixed);
    require(mixed_result.status == TeacherRankingStatus::InvalidInput,
            "mixed V1/V2 Teacher domain was not rejected fail closed");

    auto malformed = v2;
    malformed.public_action_key = "public_action.v2.malformed";
    const auto malformed_result = propose(
        profile, retained_state(profile), public_observation(),
        std::vector<EnvironmentActionCandidate>{malformed});
    require(malformed_result.status == TeacherRankingStatus::InvalidInput,
            "malformed public action key was not rejected fail closed");
}

struct V3Scenario final {
    std::string name;
    std::vector<EnvironmentActionCandidate> candidates;
    EpisodeLocalStrategyStateV1 state;
    std::string expected_progress;
};

void test_v3_teacher_boundary_red() {
    const auto profile = valid_profile();
    const auto observation = public_observation();
    const auto material_a_select = make_card_candidate(
        "p0:MONSTER_ZONE:0", PublicCardSelectionOperation::Select, true);
    const auto material_b_select = make_card_candidate(
        "p0:MONSTER_ZONE:1", PublicCardSelectionOperation::Select, true);
    const auto material_a_unselect = make_card_candidate(
        "p0:MONSTER_ZONE:0", PublicCardSelectionOperation::Unselect, true);
    const auto cancel = make_cancel_candidate(true);

    const std::vector<V3Scenario> scenarios = {
        {"initial_material_selection",
         {material_a_select, material_b_select, cancel},
         retained_state(profile),
         "A=+1,B=+1,Cancel=0"},
        {"second_material_selection",
         {material_a_unselect, material_b_select, cancel},
         retained_state(profile),
         "A=0,B=+1,Cancel=0"},
        {"explicit_active_match_uses_max",
         {material_a_select, cancel},
         retained_state(profile),
         "active Select=max(+3,+1)=+3,not+4"},
        {"no_retained_line_has_no_generic_bonus",
         {material_a_select, cancel},
         reset_state(profile),
         "generic Select=0"},
    };

    bool saw_expected_red = false;
    for (const auto& scenario : scenarios) {
        for (const auto& candidate : scenario.candidates) {
            if (candidate.action_kind == EnvironmentActionKind::Cancel) {
                require(candidate.card_selection_operation ==
                            PublicCardSelectionOperation::None,
                        "Cancel fixture carried card-selection operation metadata");
                require(is_public_action_key_v2(candidate.public_action_key),
                        "Cancel fixture did not use a V2 public action key");
            } else if (candidate.source_reference->observation_locator ==
                       "p0:MONSTER_ZONE:0" &&
                       scenario.name == "second_material_selection") {
                require_v2_candidate_shape(
                    candidate, PublicCardSelectionOperation::Unselect);
            } else {
                require_v2_candidate_shape(
                    candidate, PublicCardSelectionOperation::Select);
            }
        }

        const auto result = propose(
            profile, scenario.state, observation, scenario.candidates);
        if (result.status == TeacherRankingStatus::InvalidInput) {
            std::cout << "SCORING_DEFERRED=" << scenario.name
                      << " expected=" << scenario.expected_progress
                      << " current_boundary=V1_ONLY\n";
            saw_expected_red = true;
            continue;
        }

        throw std::runtime_error(
            "RED_OWNER=TEACHER_V3_PUBLIC_ACTION_BOUNDARY: current Teacher "
            "unexpectedly accepted a V2 scenario " +
            scenario.name + " with status " + ranking_status_name(result.status));
    }

    require(saw_expected_red,
            "V2 Teacher scenarios did not reach the expected V1-bound RED");
    std::cerr << "RED_OWNER=TEACHER_V3_PUBLIC_ACTION_BOUNDARY\n"
              << "RED_FAILURE_EXPECTED=YES\n"
              << "RED_FAILURE_UNRELATED=NO\n";
    throw std::runtime_error(
        "expected runtime RED: current Teacher rejects homogeneous V2 "
        "public-action domain");
}

}  // namespace

int main() {
    try {
        test_historical_v1_path();
        test_v1_state_rejects_v2_action_key();
        test_codec_and_mixed_domain_guards();
        test_v3_teacher_boundary_red();
        std::cerr << "RED_FAILURE_EXPECTED=YES but V3 Teacher boundary did not RED\n";
        return 2;
    } catch (const std::exception& error) {
        std::cerr << "teacher_v3_hiita_commitment_red_test: " << error.what()
                  << '\n';
        return 1;
    }
}
