#include "ygo/teacher/candidate_features.hpp"
#include "ygo/teacher/goal_line_controller.hpp"
#include "ygo/teacher/predicate_registry.hpp"
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

PredicateRef public_self_equals(const std::uint64_t value) {
    return predicate(PredicateScope::Observation, "observation.fact_u64_equals",
                     {token_atom("public.life_points.self"), u64_atom(value)});
}

PredicateRef candidate_action(const std::string& action_kind) {
    return predicate(PredicateScope::Candidate, "candidate.action_kind_equals",
                     {token_atom(action_kind)});
}

PredicateRef candidate_choice_present() {
    return predicate(PredicateScope::Candidate, "candidate.choice_present");
}

PredicateRef candidate_source_role(const std::string& role_id) {
    return predicate(PredicateScope::Candidate, "candidate.source_role_contains",
                     {token_atom(role_id)});
}

PredicateRef candidate_target_role(const std::string& role_id) {
    return predicate(PredicateScope::Candidate, "candidate.target_role_contains",
                     {token_atom(role_id)});
}

PredicateRef observation_fact_missing() {
    return predicate(PredicateScope::Observation, "observation.fact_i32_equals",
                     {token_atom("public.last_event.amount"),
                      PredicateAtom{PredicateAtomKind::I32, {}, 0, 0, 0, false}});
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
    value.card_roles = {{100, {"role.starter"}}, {200, {"role.interaction"}}};
    value.resources = {
        {"resource.phase", "public.turn.phase", 3, 10, 5},
    };

    value.candidate_intents = {
        {"intent.advance", {candidate_action("yes_no")}},
        {"intent.recover", {candidate_choice_present()}},
        {"intent.starter", {candidate_source_role("role.starter")}},
    };

    GoalDefinition alpha;
    alpha.goal_id = "goal.alpha";
    alpha.priority = 100;
    alpha.preconditions = {public_self_at_least()};
    alpha.completion_predicates = {public_self_equals(8000)};

    GoalDefinition beta;
    beta.goal_id = "goal.beta";
    beta.priority = 100;
    beta.preconditions = {public_self_at_least()};
    beta.completion_predicates = {public_self_equals(8000)};
    value.goals = {alpha, beta};

    LineNode alpha_node;
    alpha_node.node_id = "node.alpha";
    alpha_node.candidate_intent_ids = {"intent.advance"};
    alpha_node.completion_predicates = {public_self_equals(8000)};

    LineDefinition alpha_line;
    alpha_line.line_id = "line.alpha";
    alpha_line.goal_id = "goal.alpha";
    alpha_line.applicability_predicates = {public_self_at_least()};
    alpha_line.required_resources = {{"resource.phase", 1}};
    alpha_line.nodes = {alpha_node};

    LineNode first;
    first.node_id = "node.first";
    first.candidate_intent_ids = {"intent.advance"};
    first.completion_predicates = {public_self_equals(8000)};

    LineNode second;
    second.node_id = "node.second";
    second.candidate_intent_ids = {"intent.recover"};

    LineDefinition beta_line;
    beta_line.line_id = "line.beta";
    beta_line.goal_id = "goal.alpha";
    beta_line.applicability_predicates = {public_self_at_least()};
    beta_line.required_resources = {{"resource.phase", 1}};
    beta_line.nodes = {first, second};
    value.lines = {alpha_line, beta_line};

    value.preferences = {
        {ScoreDimension::ActiveGoalLineOrValidatedRecoveryProgress,
         PreferenceSubjectKind::Line, "line.alpha", 10},
        {ScoreDimension::ActiveGoalLineOrValidatedRecoveryProgress,
         PreferenceSubjectKind::Line, "line.beta", 20},
    };
    value.profile_id = strategy_profile_id(value);
    return value;
}

PublicEnvironmentObservation public_observation(
    const std::uint64_t decision_index = 12,
    const std::uint32_t self_life_points = 8000,
    const std::uint32_t opponent_life_points = 7000,
    const std::uint8_t perspective_player = 0,
    const bool include_entity = false,
    const bool identity_known = false,
    const bool entity_has_passcode = false,
    const bool duplicate_entity = false) {
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

    if (include_entity || duplicate_entity) {
        ygo::observation::ObservedCard entity;
        entity.locator = {"p0:MONSTER_ZONE:0"};
        entity.identity_known = identity_known;
        if (entity_has_passcode) {
            entity.passcode = 100;
        }
        entity.owner = 0;
        entity.controller = 0;
        entity.zone = ygo::observation::SemanticZone::MonsterZone;
        entity.sequence = 0;
        entity.face_up = identity_known;
        entity.face_down = !identity_known;
        source.entities.push_back(entity);
        if (duplicate_entity) {
            source.entities.push_back(entity);
        }
    }
    return project_public_observation(source);
}

PublicFactSnapshot public_facts(const PublicEnvironmentObservation& observation) {
    const auto extracted = extract_public_fact_snapshot(observation);
    require(extracted.valid, "public fact extraction failed");
    return extracted.snapshot;
}

EnvironmentActionCandidate candidate(
    const EnvironmentActionKind action_kind = EnvironmentActionKind::YesNo,
    const std::optional<PublicCardReference>& source = std::nullopt,
    const std::optional<PublicCardReference>& target = std::nullopt,
    const bool with_choice = false) {
    EnvironmentActionCandidate value;
    value.action_kind = action_kind;
    value.source_reference = source;
    value.target_reference = target;
    if (with_choice) {
        value.choice = PublicChoice{PublicChoiceKind::YesNo, 1, std::nullopt};
    }

    PublicActionKeyInput key;
    key.action_kind = std::string(environment_action_kind_name(action_kind));
    key.source_reference = source;
    key.target_reference = target;
    key.choice = value.choice;
    value.public_action_key = public_action_key(key);
    return value;
}

EnvironmentActionCandidate described_candidate() {
    EnvironmentActionCandidate value;
    value.action_kind = EnvironmentActionKind::Option;
    value.choice = PublicChoice{PublicChoiceKind::OptionValue, 7, 3};
    value.phase = 2;
    value.position = 1;
    value.source_index = 4;
    value.continuation_operation = "continue_public";
    value.submits_engine_response = false;

    PublicActionKeyInput key;
    key.action_kind = std::string(environment_action_kind_name(value.action_kind));
    key.choice = value.choice;
    key.phase = value.phase;
    key.position = value.position;
    key.source_index = value.source_index;
    key.continuation_operation = value.continuation_operation;
    value.public_action_key = public_action_key(key);
    return value;
}

AcceptedActionTransition accepted_transition(const std::uint64_t decision_index,
                                             const std::string& selected_key) {
    AcceptedActionTransition value;
    value.decision_index = decision_index;
    value.selected_public_action_key = selected_key;
    return value;
}

PublicCardReference visible_reference(const std::string& locator) {
    return {PublicCardReferenceKind::VisibleCard, locator};
}

PublicCardReference redacted_reference(const std::string& locator) {
    return {PublicCardReferenceKind::RedactedSlot, locator};
}

EpisodeLocalStrategyStateV1 reset_state(const StrategyProfileV1& profile) {
    const auto state = reset_strategy_state(profile);
    require(state.has_value(), "valid profile did not reset strategy state");
    return *state;
}

void test_predicate_registry_and_runtime_statuses() {
    const auto profile = valid_profile();
    std::string diagnostic;
    require(validate_strategy_profile(profile, &diagnostic),
            "registered profile was rejected: " + diagnostic);

    const auto& registry = TeacherPredicateRegistryV1::canonical();
    const std::vector<std::string> expected_predicates = {
        "candidate.action_kind_equals",       "candidate.choice_present",
        "candidate.choice_value_equals",      "candidate.continuation_present",
        "candidate.phase_equals",             "candidate.position_equals",
        "candidate.source_index_equals",      "candidate.source_role_contains",
        "candidate.source_visibility_equals", "candidate.submits_engine_response",
        "candidate.target_role_contains",     "candidate.target_visibility_equals",
        "observation.fact_boolean_equals",    "observation.fact_i32_equals",
        "observation.fact_token_equals",     "observation.fact_u64_at_least",
        "observation.fact_u64_at_most",       "observation.fact_u64_equals",
        "profile.card_role_exists",          "profile.goal_exists",
        "profile.intent_exists",             "profile.line_exists",
        "profile.resource_exists",
    };
    require(registry.definitions().size() == expected_predicates.size(),
            "initial predicate registry size changed");
    for (std::size_t index = 0; index < registry.definitions().size(); ++index) {
        require(registry.definitions()[index].predicate_id == expected_predicates[index],
                "predicate registry ID set or order changed");
        if (index > 0) {
            require(registry.definitions()[index - 1].predicate_id <
                        registry.definitions()[index].predicate_id,
                    "predicate registry is not canonically sorted");
        }
    }
    const std::vector<PredicateScope> expected_scopes = {
        PredicateScope::Candidate, PredicateScope::Candidate, PredicateScope::Candidate,
        PredicateScope::Candidate, PredicateScope::Candidate, PredicateScope::Candidate,
        PredicateScope::Candidate, PredicateScope::Candidate, PredicateScope::Candidate,
        PredicateScope::Candidate, PredicateScope::Candidate, PredicateScope::Candidate,
        PredicateScope::Observation, PredicateScope::Observation, PredicateScope::Observation,
        PredicateScope::Observation, PredicateScope::Observation, PredicateScope::Observation,
        PredicateScope::ProfileStatic, PredicateScope::ProfileStatic,
        PredicateScope::ProfileStatic, PredicateScope::ProfileStatic,
        PredicateScope::ProfileStatic,
    };
    const std::vector<std::vector<PredicateAtomKind>> expected_argument_kinds = {
        {PredicateAtomKind::Token}, {}, {PredicateAtomKind::U64}, {},
        {PredicateAtomKind::U64}, {PredicateAtomKind::U64}, {PredicateAtomKind::U64},
        {PredicateAtomKind::Token}, {PredicateAtomKind::Token}, {},
        {PredicateAtomKind::Token}, {PredicateAtomKind::Token},
        {PredicateAtomKind::Token, PredicateAtomKind::Boolean},
        {PredicateAtomKind::Token, PredicateAtomKind::I32},
        {PredicateAtomKind::Token, PredicateAtomKind::Token},
        {PredicateAtomKind::Token, PredicateAtomKind::U64},
        {PredicateAtomKind::Token, PredicateAtomKind::U64},
        {PredicateAtomKind::Token, PredicateAtomKind::U64},
        {PredicateAtomKind::Passcode}, {PredicateAtomKind::Token},
        {PredicateAtomKind::Token}, {PredicateAtomKind::Token}, {PredicateAtomKind::Token},
    };
    require(expected_scopes.size() == registry.definitions().size() &&
                expected_argument_kinds.size() == registry.definitions().size(),
            "predicate schema expectations are incomplete");
    for (std::size_t index = 0; index < registry.definitions().size(); ++index) {
        require(registry.definitions()[index].scope == expected_scopes[index] &&
                    registry.definitions()[index].argument_kinds == expected_argument_kinds[index],
                "predicate scope or ordered atom schema changed");
    }

    auto unknown = predicate(PredicateScope::Observation, "observation.unknown");
    require(!registry.validate_profile_ref(unknown, profile),
            "unknown predicate was accepted");
    auto wrong_scope = candidate_action("yes_no");
    wrong_scope.scope = PredicateScope::Observation;
    require(!registry.validate_profile_ref(wrong_scope, profile),
            "predicate with wrong scope was accepted");
    auto wrong_arity = candidate_action("yes_no");
    wrong_arity.arguments.clear();
    require(!registry.validate_profile_ref(wrong_arity, profile),
            "predicate with wrong arity was accepted");
    auto unknown_role = candidate_source_role("role.unknown");
    require(!registry.validate_profile_ref(unknown_role, profile),
            "unknown profile role was accepted");
    auto history = predicate(PredicateScope::AcceptedPublicHistory,
                             "observation.fact_u64_equals",
                             {token_atom("public.life_points.self"), u64_atom(1)});
    require(!registry.validate_profile_ref(history, profile),
            "unavailable public-history predicate was accepted");

    require(combine_predicate_statuses({}) == PredicateEvaluationStatus::True,
            "empty conjunction was not TRUE");
    require(combine_predicate_statuses({PredicateEvaluationStatus::True,
                                        PredicateEvaluationStatus::False}) ==
                PredicateEvaluationStatus::False,
            "FALSE precedence changed");
    require(combine_predicate_statuses({PredicateEvaluationStatus::False,
                                        PredicateEvaluationStatus::Unsupported}) ==
                PredicateEvaluationStatus::Unsupported,
            "UNSUPPORTED did not dominate FALSE");
    require(combine_predicate_statuses({PredicateEvaluationStatus::Unsupported,
                                        PredicateEvaluationStatus::Invalid}) ==
                PredicateEvaluationStatus::Invalid,
            "INVALID did not dominate UNSUPPORTED");
    require(combine_predicate_statuses(
                {static_cast<PredicateEvaluationStatus>(99)}) ==
                PredicateEvaluationStatus::Invalid,
            "unknown predicate status was not fail-closed");

    const auto observation = public_observation();
    const auto facts = public_facts(observation);
    require(evaluate_observation_predicate(public_self_at_least(), facts) ==
                PredicateEvaluationStatus::True,
            "public U64 predicate did not evaluate TRUE");
    auto false_fact = predicate(PredicateScope::Observation,
                                "observation.fact_u64_at_most",
                                {token_atom("public.life_points.opponent"), u64_atom(6000)});
    require(evaluate_observation_predicate(false_fact, facts) ==
                PredicateEvaluationStatus::False,
            "known public fact mismatch was not FALSE");
    require(evaluate_observation_predicate(observation_fact_missing(), facts) ==
                PredicateEvaluationStatus::Unsupported,
            "missing public fact was not UNSUPPORTED");
    require(evaluate_observation_predicate(
                predicate(PredicateScope::Observation, "observation.fact_boolean_equals",
                          {token_atom("public.terminal"),
                           PredicateAtom{PredicateAtomKind::Boolean, {}, 0, 0, 0, false}}),
                facts) == PredicateEvaluationStatus::True,
            "public BOOLEAN predicate did not evaluate TRUE");
    require(evaluate_observation_predicate(
                predicate(PredicateScope::Observation, "observation.fact_token_equals",
                          {token_atom("public.decision_context.kind"), token_atom("yes_no")}),
                facts) == PredicateEvaluationStatus::True,
            "public TOKEN predicate did not evaluate TRUE");
    PublicFactValue public_amount;
    public_amount.fact_id = "public.last_event.amount";
    public_amount.value_kind = PublicFactValueKind::I32;
    public_amount.i32_value = -3;
    PublicFactSnapshot amount_facts;
    amount_facts.values = {public_amount};
    require(evaluate_observation_predicate(
                predicate(PredicateScope::Observation, "observation.fact_i32_equals",
                          {token_atom("public.last_event.amount"),
                           PredicateAtom{PredicateAtomKind::I32, {}, 0, -3, 0, false}}),
                amount_facts) == PredicateEvaluationStatus::True,
            "public I32 predicate did not evaluate TRUE");

    auto static_goal = predicate(PredicateScope::ProfileStatic, "profile.goal_exists",
                                 {token_atom("goal.alpha")});
    require(evaluate_profile_static_predicate(static_goal, profile) ==
                PredicateEvaluationStatus::True,
            "profile-static predicate did not evaluate TRUE");
    require(evaluate_profile_static_predicate(
                predicate(PredicateScope::ProfileStatic, "profile.card_role_exists",
                          {PredicateAtom{PredicateAtomKind::Passcode, {}, 0, 0, 100, false}}),
                profile) == PredicateEvaluationStatus::True,
            "profile card-role predicate did not evaluate TRUE");
    require(evaluate_profile_static_predicate(
                predicate(PredicateScope::ProfileStatic, "profile.resource_exists",
                          {token_atom("resource.phase")}),
                profile) == PredicateEvaluationStatus::True,
            "profile resource predicate did not evaluate TRUE");
    require(evaluate_profile_static_predicate(
                predicate(PredicateScope::ProfileStatic, "profile.intent_exists",
                          {token_atom("intent.advance")}),
                profile) == PredicateEvaluationStatus::True,
            "profile intent predicate did not evaluate TRUE");
    require(evaluate_profile_static_predicate(
                predicate(PredicateScope::ProfileStatic, "profile.line_exists",
                          {token_atom("line.alpha")}),
                profile) == PredicateEvaluationStatus::True,
            "profile line predicate did not evaluate TRUE");

    const auto yes = candidate();
    require(evaluate_candidate_predicate(candidate_action("yes_no"), yes, observation, 0,
                                         profile) == PredicateEvaluationStatus::True,
            "candidate action predicate did not evaluate TRUE");
    require(evaluate_candidate_predicate(candidate_choice_present(),
                                         candidate(EnvironmentActionKind::YesNo, std::nullopt,
                                                   std::nullopt, true),
                                         observation, 0, profile) ==
                PredicateEvaluationStatus::True,
            "candidate choice predicate did not evaluate TRUE");
    require(evaluate_candidate_predicate(
                predicate(PredicateScope::Candidate, "candidate.choice_value_equals",
                          {u64_atom(1)}),
                candidate(EnvironmentActionKind::YesNo, std::nullopt, std::nullopt, true),
                observation, 0, profile) == PredicateEvaluationStatus::True,
            "candidate choice-value predicate did not evaluate TRUE");
    require(evaluate_candidate_predicate(
                predicate(PredicateScope::Candidate, "candidate.choice_value_equals",
                          {u64_atom(1)}),
                candidate(), observation, 0, profile) == PredicateEvaluationStatus::Unsupported,
            "missing candidate choice was not UNSUPPORTED");

    const auto described = described_candidate();
    require(evaluate_candidate_predicate(
                predicate(PredicateScope::Candidate, "candidate.phase_equals", {u64_atom(2)}),
                described, observation, 0, profile) == PredicateEvaluationStatus::True,
            "candidate phase predicate did not evaluate TRUE");
    require(evaluate_candidate_predicate(
                predicate(PredicateScope::Candidate, "candidate.position_equals", {u64_atom(1)}),
                described, observation, 0, profile) == PredicateEvaluationStatus::True,
            "candidate position predicate did not evaluate TRUE");
    require(evaluate_candidate_predicate(
                predicate(PredicateScope::Candidate, "candidate.source_index_equals", {u64_atom(4)}),
                described, observation, 0, profile) == PredicateEvaluationStatus::True,
            "candidate source-index predicate did not evaluate TRUE");
    require(evaluate_candidate_predicate(
                predicate(PredicateScope::Candidate, "candidate.continuation_present"), described,
                observation, 0, profile) == PredicateEvaluationStatus::True,
            "candidate continuation predicate did not evaluate TRUE");
    require(evaluate_candidate_predicate(
                predicate(PredicateScope::Candidate, "candidate.submits_engine_response"),
                described, observation, 0, profile) == PredicateEvaluationStatus::False,
            "candidate response-submission predicate did not evaluate FALSE");

    const auto known = public_observation(12, 8000, 7000, 0, true, true, true, false);
    const auto source = candidate(EnvironmentActionKind::YesNo,
                                  visible_reference("p0:MONSTER_ZONE:0"));
    const auto visible_target = candidate(
        EnvironmentActionKind::YesNo, std::nullopt,
        visible_reference("p0:MONSTER_ZONE:0"));
    const auto redacted_target = candidate(
        EnvironmentActionKind::YesNo, std::nullopt,
        redacted_reference("p0:MONSTER_ZONE:0"));
    require(evaluate_candidate_predicate(
                predicate(PredicateScope::Candidate, "candidate.source_visibility_equals",
                          {token_atom("visible")}),
                source, known, 0, profile) == PredicateEvaluationStatus::True,
            "visible source visibility predicate did not evaluate TRUE");
    require(evaluate_candidate_predicate(
                predicate(PredicateScope::Candidate, "candidate.target_visibility_equals",
                          {token_atom("visible")}),
                visible_target, known, 0, profile) == PredicateEvaluationStatus::True,
            "visible target visibility predicate did not evaluate TRUE");
    require(evaluate_candidate_predicate(
                predicate(PredicateScope::Candidate, "candidate.target_visibility_equals",
                          {token_atom("redacted")}),
                redacted_target, known, 0, profile) == PredicateEvaluationStatus::True,
            "redacted target visibility predicate did not evaluate TRUE");
    require(evaluate_candidate_predicate(candidate_source_role("role.starter"), source, known,
                                         0, profile) == PredicateEvaluationStatus::True,
            "visible known source role did not evaluate TRUE");
    require(evaluate_candidate_predicate(candidate_source_role("role.interaction"), source,
                                         known, 0, profile) == PredicateEvaluationStatus::False,
            "visible known nonmatching role was not FALSE");
    const auto target = candidate(EnvironmentActionKind::YesNo, std::nullopt,
                                  visible_reference("p0:MONSTER_ZONE:0"));
    require(evaluate_candidate_predicate(candidate_target_role("role.starter"), target,
                                         known, 0, profile) == PredicateEvaluationStatus::True,
            "visible known target role did not evaluate TRUE");
    require(evaluate_candidate_predicate(candidate_target_role("role.interaction"), target,
                                         known, 0, profile) == PredicateEvaluationStatus::False,
            "visible known nonmatching target role was not FALSE");

    const auto redacted = candidate(EnvironmentActionKind::YesNo,
                                    redacted_reference("p0:MONSTER_ZONE:0"));
    require(evaluate_candidate_predicate(candidate_source_role("role.starter"), redacted,
                                         known, 0, profile) ==
                PredicateEvaluationStatus::Unsupported,
            "RedactedSlot role predicate was not UNSUPPORTED");
    require(evaluate_candidate_predicate(candidate_source_role("role.starter"), candidate(),
                                         observation, 0, profile) == PredicateEvaluationStatus::False,
            "absent source role was not FALSE");

    const auto unknown_entity = public_observation(12, 8000, 7000, 0, true, false, false, false);
    const auto visible_unknown = candidate(EnvironmentActionKind::YesNo,
                                           visible_reference("p0:MONSTER_ZONE:0"));
    require(evaluate_candidate_predicate(candidate_source_role("role.starter"),
                                         visible_unknown, unknown_entity, 0, profile) ==
                PredicateEvaluationStatus::Invalid,
            "VisibleCard with unknown entity was not INVALID");

    const auto no_passcode = public_observation(12, 8000, 7000, 0, true, true, false, false);
    require(evaluate_candidate_predicate(candidate_source_role("role.starter"),
                                         visible_unknown, no_passcode, 0, profile) ==
                PredicateEvaluationStatus::Invalid,
            "VisibleCard with missing passcode was not INVALID");

    const auto missing_locator = candidate(EnvironmentActionKind::YesNo,
                                           visible_reference("p0:MONSTER_ZONE:9"));
    require(evaluate_candidate_predicate(candidate_source_role("role.starter"),
                                         missing_locator, known, 0, profile) ==
                PredicateEvaluationStatus::Invalid,
            "missing visible locator was not INVALID");

    bool duplicate_rejected = false;
    try {
        (void)public_observation(12, 8000, 7000, 0, true, true, true, true);
    } catch (const std::exception&) {
        duplicate_rejected = true;
    }
    require(duplicate_rejected,
            "duplicate public-safe locator was not rejected before role evaluation");

}

PublicFactSnapshot single_u64_fact(const std::string& fact_id, const std::uint64_t value) {
    PublicFactValue fact;
    fact.fact_id = fact_id;
    fact.value_kind = PublicFactValueKind::U64;
    fact.u64_value = value;
    PublicFactSnapshot snapshot;
    snapshot.values = {fact};
    return snapshot;
}

void test_resource_binding_and_runtime_statuses() {
    const auto profile = valid_profile();
    const ResourceRequirement requirement{"resource.phase", 1};
    require(evaluate_resource_requirement(requirement, profile,
                                          single_u64_fact("public.turn.phase", 2)) ==
                PredicateEvaluationStatus::True,
            "valid public resource proof was not TRUE");
    require(evaluate_resource_requirement(requirement, profile, PublicFactSnapshot{}) ==
                PredicateEvaluationStatus::Unsupported,
            "missing public resource proof was not UNSUPPORTED");
    require(evaluate_resource_requirement(requirement, profile,
                                          single_u64_fact("public.turn.phase", 0)) ==
                PredicateEvaluationStatus::False,
            "below-minimum public resource proof was not FALSE");
    require(evaluate_resource_requirement(requirement, profile,
                                          single_u64_fact("public.turn.phase", 4)) ==
                PredicateEvaluationStatus::Invalid,
            "above-profile-maximum public resource proof was not INVALID");

    PublicFactValue wrong_kind;
    wrong_kind.fact_id = "public.turn.phase";
    wrong_kind.value_kind = PublicFactValueKind::Boolean;
    PublicFactSnapshot wrong_kind_snapshot;
    wrong_kind_snapshot.values = {wrong_kind};
    require(evaluate_resource_requirement(requirement, profile, wrong_kind_snapshot) ==
                PredicateEvaluationStatus::Invalid,
            "wrong public resource kind was not INVALID");

    auto unknown_fact = profile;
    unknown_fact.resources[0].public_fact_id = "unknown.fact";
    require(!validate_strategy_profile(unknown_fact),
            "unknown resource public fact was accepted");

    auto blocked_fact = profile;
    blocked_fact.resources[0].public_fact_id = "blocked.continuation.can_finish";
    require(!validate_strategy_profile(blocked_fact),
            "blocked resource public fact was accepted");

    auto wrong_kind_profile = profile;
    wrong_kind_profile.resources[0].public_fact_id = "public.terminal";
    require(!validate_strategy_profile(wrong_kind_profile),
            "non-U64 resource public fact was accepted");

    auto excessive_max = profile;
    excessive_max.resources[0].public_fact_id = "public.perspective_player";
    excessive_max.resources[0].max_value = 2;
    require(!validate_strategy_profile(excessive_max),
            "resource maximum outside public fact bounds was accepted");
}

void test_goal_line_selection_and_progress() {
    const auto profile = valid_profile();
    const auto observation = public_observation();
    const auto facts = public_facts(observation);
    const auto reset = reset_state(profile);

    const auto selected = select_goal_and_line(profile, reset, facts);
    require(selected.status == PredicateEvaluationStatus::True &&
                selected.goal_id == std::optional<std::string>("goal.alpha") &&
                selected.line_id == std::optional<std::string>("line.beta") &&
                selected.ready_node_ids == std::vector<std::string>{"node.first", "node.second"},
            "deterministic goal/line selection or ready-node order changed");

    auto retained_goal_state = reset;
    retained_goal_state.active_goal_id = "goal.beta";
    const auto retained_goal = select_goal_and_line(profile, retained_goal_state, facts);
    require(retained_goal.status == PredicateEvaluationStatus::True &&
                retained_goal.goal_id == std::optional<std::string>("goal.beta"),
            "eligible active goal was not retained");

    auto retained_line_state = reset;
    retained_line_state.active_goal_id = "goal.alpha";
    retained_line_state.active_line_id = "line.alpha";
    const auto retained_line = select_goal_and_line(profile, retained_line_state, facts);
    require(retained_line.status == PredicateEvaluationStatus::True &&
                retained_line.line_id == std::optional<std::string>("line.alpha"),
            "eligible active line was not retained");

    const auto line_it = std::find_if(
        profile.lines.begin(), profile.lines.end(),
        [](const auto& line) { return line.line_id == "line.beta"; });
    require(line_it != profile.lines.end(), "test line missing");
    const auto accepted = accepted_transition(12, candidate().public_action_key);
    const auto completion_before_later_frame = evaluate_node_completion(
        line_it->nodes[0], accepted, public_observation(12), 0, profile);
    const auto completion_after_accept = evaluate_node_completion(
        line_it->nodes[0], accepted, public_observation(13), 0, profile);
    const auto wrong_participant = evaluate_node_completion(
        line_it->nodes[0], accepted, public_observation(13, 8000, 7000, 1), 0, profile);
    const auto empty_completion = evaluate_node_completion(
        line_it->nodes[1], accepted, public_observation(13), 0, profile);
    const auto goal_completion = evaluate_goal_completion(
        profile.goals[0], accepted, public_observation(13), 0, profile);
    require(completion_before_later_frame == PredicateEvaluationStatus::Invalid &&
                completion_after_accept == PredicateEvaluationStatus::True &&
                wrong_participant == PredicateEvaluationStatus::Invalid &&
                empty_completion == PredicateEvaluationStatus::False &&
                goal_completion == PredicateEvaluationStatus::True,
            "node completion did not require acceptance and public proof");

    std::vector<std::string> matched;
    const auto both_intents = match_candidate_intent_set(
        profile, {"intent.advance", "intent.recover"},
        candidate(EnvironmentActionKind::YesNo, std::nullopt, std::nullopt, true), observation, 0,
        matched);
    require(both_intents == PredicateEvaluationStatus::True &&
                matched == std::vector<std::string>{"intent.advance", "intent.recover"},
            "candidate intent conjunction/alternative matching changed");

    RecoverySelection no_recovery;
    const auto progress = evaluate_goal_line_progress(
        profile, selected, no_recovery,
        candidate(EnvironmentActionKind::YesNo, std::nullopt, std::nullopt, true), observation, 0);
    require(progress.status == CandidateEvaluationStatus::Supported &&
                progress.contributions.size() == 1 &&
                progress.contributions[0].dimension ==
                    ScoreDimension::ActiveGoalLineOrValidatedRecoveryProgress &&
                progress.contributions[0].value == 3,
            "active-line progress did not contribute exact +3");

    const auto no_match = evaluate_goal_line_progress(
        profile, selected, no_recovery,
        candidate(EnvironmentActionKind::Pick), observation, 0);
    require(no_match.status == CandidateEvaluationStatus::Supported &&
                no_match.contributions.size() == 1 && no_match.contributions[0].value == 0,
            "proven candidate nonmatch did not produce supported zero progress");

    GoalLineSelection no_plan;
    no_plan.status = PredicateEvaluationStatus::True;
    no_plan.goal_id = "goal.alpha";
    const auto not_applicable = evaluate_goal_line_progress(
        profile, no_plan, no_recovery, candidate(), observation, 0);
    require(not_applicable.status == CandidateEvaluationStatus::NotApplicable &&
                not_applicable.contributions.empty(),
            "missing active line/recovery was not NOT_APPLICABLE");
}

}  // namespace

int main() {
    try {
        test_predicate_registry_and_runtime_statuses();
        test_resource_binding_and_runtime_statuses();
        test_goal_line_selection_and_progress();
        std::cout << "teacher_goal_line_test: PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "teacher_goal_line_test: FAIL: " << error.what() << '\n';
        return 1;
    }
}
