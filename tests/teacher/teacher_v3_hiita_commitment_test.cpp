#include "ygo/environment/public_action_identity.hpp"
#include "ygo/environment/public_decision.hpp"
#include "ygo/environment/public_environment_observation.hpp"
#include "ygo/observation/player_observation.hpp"
#include "ygo/policy/policy.hpp"
#include "ygo/teacher/goal_line_controller.hpp"
#include "ygo/teacher/public_fact_registry.hpp"
#include "ygo/teacher/salamangreat_profile.hpp"
#include "ygo/teacher/recovery_controller.hpp"
#include "ygo/teacher/strategy_profile.hpp"
#include "ygo/teacher/strategy_state.hpp"
#include "ygo/teacher/strategy_state_v2.hpp"
#include "ygo/teacher/teacher_core.hpp"
#include "ygo/teacher/teacher_core_v2.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

using namespace ygo::environment;
using namespace ygo::teacher;

template <typename T, typename = void>
struct has_v2_explanation_member : std::false_type {};

template <typename T>
struct has_v2_explanation_member<T, std::void_t<decltype(std::declval<T>().explanation)>>
    : std::true_type {};

static_assert(!has_v2_explanation_member<TeacherRankingResultV2>::value,
              "TeacherRankingResultV2 must not expose a V1 diagnostic type");

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

StrategyProfileV1 synthetic_scoring_profile() {
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
    const std::uint64_t decision_index = 234,
    const std::optional<std::uint64_t> private_metadata_marker = std::nullopt) {
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
    if (private_metadata_marker.has_value()) {
        const auto marker = std::to_string(*private_metadata_marker);
        source.decision_context.decision_id = "private.decision." + marker;
        source.decision_context.engine_step_index = *private_metadata_marker;
        source.decision_context.engine_message_type =
            static_cast<std::uint8_t>(*private_metadata_marker % 255);
        source.decision_context.engine_message_name = "private.message." + marker;
        source.decision_context.continuation_id = "private.continuation." + marker;
        source.observation_hash = "private.observation." + marker;
    }
    return project_public_observation(source);
}

PublicFactSnapshot public_facts(const PublicEnvironmentObservation& observation) {
    const auto extracted = extract_public_fact_snapshot(observation);
    require(extracted.valid, "Hiita public fact extraction failed");
    return extracted.snapshot;
}

EpisodeLocalStrategyStateV1 reset_state(
    const StrategyProfileV1& profile) {
    const auto state = reset_strategy_state(profile);
    require(state.has_value(), "valid B1 profile did not reset strategy state");
    return *state;
}

EpisodeLocalStrategyStateV1 synthetic_retained_state(
    const StrategyProfileV1& profile) {
    auto state = reset_state(profile);
    state.active_goal_id = "goal.test";
    state.active_line_id = "line.test";
    return state;
}

EpisodeLocalStrategyStateV1 retained_salamangreat_state(
    const StrategyProfileV1& profile) {
    auto state = reset_state(profile);
    state.active_goal_id = "goal.main1.salamangreat";
    state.active_line_id = "line.main1.salamangreat";
    return state;
}

EpisodeLocalStrategyStateV2 reset_state_v2(
    const StrategyProfileV1& profile) {
    const auto state = reset_strategy_state_v2(profile);
    require(state.has_value(), "valid profile did not reset V2 strategy state");
    return *state;
}

EpisodeLocalStrategyStateV2 synthetic_retained_state_v2(
    const StrategyProfileV1& profile) {
    auto state = reset_state_v2(profile);
    state.active_goal_id = "goal.test";
    state.active_line_id = "line.test";
    return state;
}

EpisodeLocalStrategyStateV2 retained_salamangreat_state_v2(
    const StrategyProfileV1& profile) {
    auto state = reset_state_v2(profile);
    state.active_goal_id = "goal.main1.salamangreat";
    state.active_line_id = "line.main1.salamangreat";
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

TeacherRankingResult propose(
    const StrategyProfileV1& profile,
    const EpisodeLocalStrategyStateV1& state,
    const PublicEnvironmentObservation& observation,
    const std::vector<EnvironmentActionCandidate>& candidates) {
    const ygo::policy::PolicyInput input{observation, candidates};
    return TeacherCore{}.propose(input, profile, state);
}

TeacherRankingResultV2 propose_v2(
    const StrategyProfileV1& profile,
    const EpisodeLocalStrategyStateV2& state,
    const PublicEnvironmentObservation& observation,
    const std::vector<EnvironmentActionCandidate>& candidates) {
    const ygo::policy::PolicyInput input{observation, candidates};
    return TeacherCoreV2{}.propose(input, profile, state);
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

const CandidateEvaluation& evaluation_for(
    const TeacherRankingResultV2& result,
    const std::string& public_action_key) {
    const auto found = std::find_if(
        result.evaluations.begin(), result.evaluations.end(),
        [&](const auto& evaluation) {
            return evaluation.public_action_key == public_action_key;
        });
    require(found != result.evaluations.end(),
            "V2 ranking result omitted a complete candidate evaluation");
    return *found;
}

void require_v2_progress(const TeacherRankingResultV2& result,
                         const std::string& public_action_key,
                         const std::int64_t expected) {
    const auto& evaluation = evaluation_for(result, public_action_key);
    require(evaluation.status == CandidateEvaluationStatus::Supported &&
                evaluation.score.has_value() &&
                evaluation.score->values[static_cast<std::size_t>(
                    ScoreDimension::ActiveGoalLineOrValidatedRecoveryProgress)] == expected,
            "V2 active-line progress did not match the declared contract");
}

void test_historical_v1_path() {
    const auto profile = synthetic_scoring_profile();
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

    const auto result = propose(
        profile, synthetic_retained_state(profile), observation, candidates);
    require(result.status == TeacherRankingStatus::Selected,
            "historical V1 Teacher path was not structurally acceptable");
    require(result.selected_public_action_key.has_value() &&
                *result.selected_public_action_key !=
                    candidates.back().public_action_key,
            "historical V1 Teacher path selected Cancel over active card progress");
}

void test_v1_state_rejects_v2_action_key() {
    const auto profile = synthetic_scoring_profile();
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

    const auto profile = synthetic_scoring_profile();
    const auto mixed = std::vector<EnvironmentActionCandidate>{
        v1, v2, make_cancel_candidate(true)};
    const auto mixed_result = propose(
        profile, synthetic_retained_state(profile), public_observation(), mixed);
    require(mixed_result.status == TeacherRankingStatus::InvalidInput,
            "mixed V1/V2 Teacher domain was not rejected fail closed");

    auto malformed = v2;
    malformed.public_action_key = "public_action.v2.malformed";
    const auto malformed_result = propose(
        profile, synthetic_retained_state(profile), public_observation(),
        std::vector<EnvironmentActionCandidate>{malformed});
    require(malformed_result.status == TeacherRankingStatus::InvalidInput,
            "malformed public action key was not rejected fail closed");

    const auto v3_profile = make_salamangreat_profile();
    const auto v3_state = retained_salamangreat_state_v2(v3_profile);
    const auto v3_observation = public_observation();
    const auto v2_cancel = make_cancel_candidate(true);
    const auto mixed_v2_result = propose_v2(
        v3_profile, v3_state, v3_observation,
        std::vector<EnvironmentActionCandidate>{v2, v1, v2_cancel});
    require(mixed_v2_result.status == TeacherRankingStatus::InvalidInput,
            "V2 Teacher accepted a mixed V1/V2 candidate domain");

    const auto malformed_v2_result = propose_v2(
        v3_profile, v3_state, v3_observation,
        std::vector<EnvironmentActionCandidate>{malformed});
    require(malformed_v2_result.status == TeacherRankingStatus::InvalidInput,
            "V2 Teacher accepted a malformed public action key");

    auto missing_operation = make_card_candidate(
        "p0:MONSTER_ZONE:0", PublicCardSelectionOperation::None, true);
    const auto missing_operation_result = propose_v2(
        v3_profile, v3_state, v3_observation,
        std::vector<EnvironmentActionCandidate>{missing_operation, v2_cancel});
    require(missing_operation_result.status == TeacherRankingStatus::InvalidInput,
            "V2 Teacher accepted an unselect CardSelection without an operation");

    auto non_card_operation = v2_cancel;
    non_card_operation.card_selection_operation = PublicCardSelectionOperation::Select;
    const auto non_card_operation_result = propose_v2(
        v3_profile, v3_state, v3_observation,
        std::vector<EnvironmentActionCandidate>{non_card_operation});
    require(non_card_operation_result.status == TeacherRankingStatus::InvalidInput,
            "V2 Teacher accepted non-None operation metadata on Cancel");
}

struct V3Scenario final {
    std::string name;
    std::vector<EnvironmentActionCandidate> candidates;
    std::vector<PublicCardSelectionOperation> expected_operations;
    EpisodeLocalStrategyStateV2 state;
    std::string expected_progress;
};

void test_salamangreat_reconciled_commitment() {
    const auto profile = make_salamangreat_profile();
    require(validate_strategy_profile(profile),
            "canonical Salamangreat profile is not valid");
    const auto state = retained_salamangreat_state(profile);
    const auto observation = public_observation();

    const auto reconciliation = reconcile_strategy_state_with_evidence(
        state, 0, observation);
    require(reconciliation.has_value(),
            "Salamangreat unselect reconciliation was not accepted");
    require(reconciliation->invalidation_reason_ids.empty(),
            "Salamangreat unselect reconciliation reported an invalidation");
    require(reconciliation->state.active_goal_id ==
                std::optional<std::string>("goal.main1.salamangreat") &&
                reconciliation->state.active_line_id ==
                    std::optional<std::string>("line.main1.salamangreat"),
            "reconciliation did not retain the Salamangreat goal and line");

    const auto selection = select_goal_and_line(
        profile, reconciliation->state, public_facts(observation));
    require(selection.status == PredicateEvaluationStatus::False &&
                !selection.goal_id.has_value() && !selection.line_id.has_value(),
            "Salamangreat Main1 line was eligible at the unselect boundary");
    std::cout << "SALAMANGREAT_RECONCILED_COMMITMENT=RETAINED\n"
              << "SALAMANGREAT_CURRENT_STRATEGIC_ELIGIBILITY=NO\n"
              << "SALAMANGREAT_PROFILE_CHANGED=NO\n"
              << "CARD_SELECTION_INTENT_ADDED_TO_PROFILE=NO\n"
              << "RECONCILIATION_SEMANTICS_CHANGED=NO\n";
}

void validate_v2_fixture(const V3Scenario& scenario) {
    require(scenario.candidates.size() == scenario.expected_operations.size(),
            "V3 scenario operation expectations do not cover the complete domain");
    for (std::size_t index = 0; index < scenario.candidates.size(); ++index) {
        const auto& candidate = scenario.candidates[index];
        if (candidate.action_kind == EnvironmentActionKind::Cancel) {
            require(candidate.card_selection_operation ==
                        PublicCardSelectionOperation::None,
                    "Cancel fixture carried card-selection operation metadata");
            require(is_public_action_key_v2(candidate.public_action_key),
                    "Cancel fixture did not use a V2 public action key");
        } else {
            require_v2_candidate_shape(candidate, scenario.expected_operations[index]);
        }
    }
}

void require_v2_result_selected(const TeacherRankingResultV2& result,
                                const std::vector<EnvironmentActionCandidate>& candidates) {
    std::string diagnostic;
    require(result.status == TeacherRankingStatus::Selected &&
                result.evaluations.size() == candidates.size() &&
                validate_teacher_ranking_result_v2(result, &diagnostic),
            "V2 Teacher did not return a complete valid ranking result status=" +
                std::to_string(static_cast<int>(result.status)) +
                " evaluations=" + std::to_string(result.evaluations.size()) + ": " +
                " candidates=" + std::to_string(candidates.size()) + ": " +
                diagnostic);
    require(result.selected_public_action_key.has_value() &&
                result.selected_score_vector.has_value() &&
                result.proposed_state_delta.has_value() &&
                result.proposed_state_delta->proposed_for_public_action_key ==
                    *result.selected_public_action_key,
            "V2 Teacher selected result did not carry its V2 state delta");
}

void test_hiita_v2_commitment() {
    const auto profile = make_salamangreat_profile();
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
         {PublicCardSelectionOperation::Select,
          PublicCardSelectionOperation::Select,
          PublicCardSelectionOperation::None},
         retained_salamangreat_state_v2(profile),
         "A=+1,B=+1,Cancel=0"},
        {"second_material_selection",
         {material_a_unselect, material_b_select, cancel},
         {PublicCardSelectionOperation::Unselect,
          PublicCardSelectionOperation::Select,
          PublicCardSelectionOperation::None},
         retained_salamangreat_state_v2(profile),
         "A=0,B=+1,Cancel=0"},
    };

    validate_v2_fixture(scenarios[0]);
    const auto initial = propose_v2(
        profile, scenarios[0].state, observation, scenarios[0].candidates);
    require_v2_result_selected(initial, scenarios[0].candidates);
    require(initial.fallback_level ==
                std::optional<TeacherFallbackLevel>{TeacherFallbackLevel::F0},
            "initial Hiita material domain did not resolve at F0");
    require_v2_progress(initial, material_a_select.public_action_key, 1);
    require_v2_progress(initial, material_b_select.public_action_key, 1);
    require_v2_progress(initial, cancel.public_action_key, 0);
    require(*initial.selected_public_action_key == material_a_select.public_action_key ||
                *initial.selected_public_action_key == material_b_select.public_action_key,
            "initial Hiita material selection did not choose a legal material candidate");
    validate_v2_fixture(scenarios[1]);
    const auto second = propose_v2(
        profile, scenarios[1].state, observation, scenarios[1].candidates);
    require_v2_result_selected(second, scenarios[1].candidates);
    require(second.fallback_level ==
                std::optional<TeacherFallbackLevel>{TeacherFallbackLevel::F0},
            "second Hiita material domain did not resolve at F0");
    require_v2_progress(second, material_a_unselect.public_action_key, 0);
    require_v2_progress(second, material_b_select.public_action_key, 1);
    require_v2_progress(second, cancel.public_action_key, 0);
    require(second.selected_public_action_key ==
                std::optional<std::string>(material_b_select.public_action_key),
            "Hiita second material selection did not choose the remaining Select candidate");

    const V3Scenario symmetric{
        "symmetric_second_material_selection",
        {material_a_select,
         make_card_candidate("p0:MONSTER_ZONE:1",
                             PublicCardSelectionOperation::Unselect, true),
         cancel},
        {PublicCardSelectionOperation::Select,
         PublicCardSelectionOperation::Unselect,
         PublicCardSelectionOperation::None},
        retained_salamangreat_state_v2(profile),
        "A=+1,B=0,Cancel=0"};
    validate_v2_fixture(symmetric);
    const auto symmetric_result = propose_v2(
        profile, symmetric.state, observation, symmetric.candidates);
    require_v2_result_selected(symmetric_result, symmetric.candidates);
    require(symmetric_result.fallback_level ==
                std::optional<TeacherFallbackLevel>{TeacherFallbackLevel::F0},
            "symmetric Hiita material domain did not resolve at F0");
    require_v2_progress(symmetric_result, material_a_select.public_action_key, 1);
    require_v2_progress(symmetric_result,
                        symmetric.candidates[1].public_action_key, 0);
    require_v2_progress(symmetric_result, cancel.public_action_key, 0);
    require(symmetric_result.selected_public_action_key ==
                std::optional<std::string>(material_a_select.public_action_key),
            "symmetric Hiita material selection did not choose the remaining Select candidate");
}

void test_generic_scoring_contract() {
    const auto profile = synthetic_scoring_profile();
    const auto observation = public_observation();
    const auto material_a_select = make_card_candidate(
        "p0:MONSTER_ZONE:0", PublicCardSelectionOperation::Select, true);
    const auto cancel = make_cancel_candidate(true);
    const std::vector<V3Scenario> scenarios = {
        {"explicit_active_match_uses_max",
         {material_a_select, cancel},
         {PublicCardSelectionOperation::Select,
          PublicCardSelectionOperation::None},
         synthetic_retained_state_v2(profile),
         "active Select=max(+3,+1)=+3,not+4"},
        {"no_retained_line_has_no_generic_bonus",
         {material_a_select, cancel},
         {PublicCardSelectionOperation::Select,
          PublicCardSelectionOperation::None},
         reset_state_v2(profile),
         "generic Select=0"},
    };

    validate_v2_fixture(scenarios[0]);
    const auto max_result = propose_v2(
        profile, scenarios[0].state, observation, scenarios[0].candidates);
    require_v2_result_selected(max_result, scenarios[0].candidates);
    require(max_result.fallback_level ==
                std::optional<TeacherFallbackLevel>{TeacherFallbackLevel::F0},
            "explicit active-line scenario did not resolve at F0");
    require_v2_progress(max_result, material_a_select.public_action_key, 3);
    require_v2_progress(max_result, cancel.public_action_key, 0);
    require(max_result.selected_public_action_key ==
                std::optional<std::string>(material_a_select.public_action_key),
            "explicit active-line progress did not beat Cancel");

    validate_v2_fixture(scenarios[1]);
    const auto no_commitment = propose_v2(
        profile, scenarios[1].state, observation, scenarios[1].candidates);
    require_v2_result_selected(no_commitment, scenarios[1].candidates);
    require(no_commitment.fallback_level ==
                std::optional<TeacherFallbackLevel>{TeacherFallbackLevel::F4},
            "no-commitment scenario did not fall back without a progress stage");
    require_v2_progress(no_commitment, material_a_select.public_action_key, 0);
    require_v2_progress(no_commitment, cancel.public_action_key, 0);
}

void test_select_progress_survives_unsupported_plan() {
    const auto profile = make_salamangreat_profile();
    const auto observation = public_observation();
    const auto select = make_card_candidate(
        "p0:MONSTER_ZONE:0", PublicCardSelectionOperation::Select, true);
    const auto cancel = make_cancel_candidate(true);
    GoalLineSelection unsupported_selection;
    unsupported_selection.status = PredicateEvaluationStatus::Unsupported;
    RecoverySelection no_recovery;

    const auto select_outcome = evaluate_goal_line_progress_v2(
        profile, unsupported_selection, no_recovery, select, observation, 0, true);
    require(select_outcome.status == CandidateEvaluationStatus::Supported &&
                select_outcome.contributions.size() == 1 &&
                select_outcome.contributions[0].value == 1,
            "retained Select progress was suppressed by unsupported strategic eligibility");

    const auto cancel_outcome = evaluate_goal_line_progress_v2(
        profile, unsupported_selection, no_recovery, cancel, observation, 0, true);
    require(cancel_outcome.status == CandidateEvaluationStatus::Supported &&
                cancel_outcome.contributions.size() == 1 &&
                cancel_outcome.contributions[0].value == 0,
            "Cancel was not represented as complete zero-progress evidence");
}

void require_same_public_v2_result(const TeacherRankingResultV2& left,
                                   const TeacherRankingResultV2& right) {
    require(left.status == right.status &&
                left.selected_public_action_key == right.selected_public_action_key &&
                left.selected_score_vector == right.selected_score_vector &&
                left.fallback_level == right.fallback_level &&
                left.proposed_state_delta == right.proposed_state_delta &&
                left.evaluations.size() == right.evaluations.size(),
            "paired public-equivalent V2 results differ in public ranking state");
    for (std::size_t index = 0; index < left.evaluations.size(); ++index) {
        const auto& left_evaluation = left.evaluations[index];
        const auto& right_evaluation = right.evaluations[index];
        require(left_evaluation.public_action_key == right_evaluation.public_action_key &&
                    left_evaluation.status == right_evaluation.status &&
                    left_evaluation.score == right_evaluation.score &&
                    left_evaluation.matched_intent_ids ==
                        right_evaluation.matched_intent_ids &&
                    left_evaluation.matched_goal_ids == right_evaluation.matched_goal_ids &&
                    left_evaluation.matched_line_ids == right_evaluation.matched_line_ids &&
                    left_evaluation.reason_ids == right_evaluation.reason_ids,
                "paired public-equivalent V2 evaluations differ");
    }
}

void test_v2_paired_public_equivalence() {
    const auto profile = make_salamangreat_profile();
    const auto left_observation = public_observation(234, 1);
    const auto right_observation = public_observation(234, 2);
    require(public_observation_digest(left_observation) ==
                public_observation_digest(right_observation),
            "private observation metadata changed the public observation digest");

    const auto candidates = std::vector<EnvironmentActionCandidate>{
        make_card_candidate("p0:MONSTER_ZONE:0", PublicCardSelectionOperation::Select, true),
        make_card_candidate("p0:MONSTER_ZONE:1", PublicCardSelectionOperation::Select, true),
        make_cancel_candidate(true),
    };
    const auto left = propose_v2(
        profile, retained_salamangreat_state_v2(profile), left_observation, candidates);
    const auto right = propose_v2(
        profile, retained_salamangreat_state_v2(profile), right_observation, candidates);
    require_v2_result_selected(left, candidates);
    require_v2_result_selected(right, candidates);
    require_same_public_v2_result(left, right);
}

void test_selected_result_rejects_not_applicable() {
    const auto candidate = make_card_candidate(
        "p0:MONSTER_ZONE:0", PublicCardSelectionOperation::Select, true);
    const auto other_candidate = make_card_candidate(
        "p0:MONSTER_ZONE:1", PublicCardSelectionOperation::Select, true);
    const auto make_result = [&](const CandidateEvaluationStatus status,
                                 const bool evaluation_has_score,
                                 const bool result_has_score,
                                 const std::optional<TeacherFallbackLevel> fallback,
                                 const std::optional<std::string> selected_key = std::nullopt,
                                 const std::optional<std::string> evaluation_key = std::nullopt) {
        TeacherRankingResultV2 result;
        result.status = TeacherRankingStatus::Selected;
        CandidateEvaluation evaluation;
        evaluation.public_action_key = evaluation_key.value_or(candidate.public_action_key);
        evaluation.status = status;
        if (evaluation_has_score) {
            evaluation.score = ScoreVector{};
        }
        result.evaluations.push_back(evaluation);
        result.selected_public_action_key = selected_key.value_or(candidate.public_action_key);
        if (result_has_score) {
            result.selected_score_vector = ScoreVector{};
        }
        result.fallback_level = fallback;
        return result;
    };
    const auto expect_rejected = [&](const TeacherRankingResultV2& result,
                                     const std::string& label) {
        const bool valid = validate_teacher_ranking_result_v2(result);
        const auto selection = teacher_policy_selection_from_result_v2(result);
        require(!valid && !selection.value.has_value(), label);
    };

    expect_rejected(make_result(CandidateEvaluationStatus::NotApplicable, false, false,
                                std::nullopt),
                    "Selected NotApplicable V2 result became actionable");
    expect_rejected(make_result(CandidateEvaluationStatus::Unsupported, false, false,
                                std::nullopt),
                    "Selected Unsupported V2 result became actionable");
    expect_rejected(make_result(CandidateEvaluationStatus::Invalid, false, false,
                                std::nullopt),
                    "Selected Invalid V2 result became actionable");
    expect_rejected(make_result(static_cast<CandidateEvaluationStatus>(0xff), false, false,
                                std::nullopt),
                    "Selected unknown-status V2 result became actionable");
    expect_rejected(make_result(CandidateEvaluationStatus::Supported, false, true,
                                TeacherFallbackLevel::F0),
                    "Selected V2 evaluation without a score was accepted");
    expect_rejected(make_result(CandidateEvaluationStatus::Supported, true, false,
                                TeacherFallbackLevel::F0),
                    "Selected V2 result without a selected score was accepted");

    auto mismatched_score = make_result(CandidateEvaluationStatus::Supported, true, true,
                                        TeacherFallbackLevel::F0);
    mismatched_score.selected_score_vector->values[0] = 1;
    expect_rejected(mismatched_score, "Selected V2 score mismatch was accepted");
    expect_rejected(make_result(CandidateEvaluationStatus::Supported, true, true,
                                std::nullopt),
                    "Selected V2 result without fallback was accepted");
    expect_rejected(make_result(CandidateEvaluationStatus::Supported, true, true,
                                static_cast<TeacherFallbackLevel>(0xff)),
                    "Selected V2 result with unknown fallback was accepted");
    expect_rejected(make_result(CandidateEvaluationStatus::Supported, true, true,
                                TeacherFallbackLevel::F0, candidate.public_action_key,
                                other_candidate.public_action_key),
                    "Selected V2 key absent from evaluations was accepted");

    auto duplicate = make_result(CandidateEvaluationStatus::Supported, true, true,
                                 TeacherFallbackLevel::F0);
    duplicate.evaluations.push_back(duplicate.evaluations.front());
    expect_rejected(duplicate, "Selected V2 duplicate evaluation keys were accepted");
    expect_rejected(make_result(CandidateEvaluationStatus::Supported, true, true,
                                TeacherFallbackLevel::F0, "public_action.v2.malformed"),
                    "Selected malformed V2 key was accepted");

    const auto valid = make_result(CandidateEvaluationStatus::Supported, true, true,
                                   TeacherFallbackLevel::F0);
    require(validate_teacher_ranking_result_v2(valid),
            "valid Supported V2 selected result was rejected");
    const auto selection = teacher_policy_selection_from_result_v2(valid);
    require(selection.value.has_value() &&
                selection.value->public_action_key == candidate.public_action_key,
            "valid Supported V2 selected result did not adapt to its action");
}

}  // namespace

int main() {
    try {
        test_historical_v1_path();
        test_v1_state_rejects_v2_action_key();
        test_codec_and_mixed_domain_guards();
        test_salamangreat_reconciled_commitment();
        test_hiita_v2_commitment();
        test_generic_scoring_contract();
        test_select_progress_survives_unsupported_plan();
        test_v2_paired_public_equivalence();
        test_selected_result_rejects_not_applicable();
        std::cout << "teacher_v3_hiita_commitment_test: PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "teacher_v3_hiita_commitment_test: " << error.what()
                  << '\n';
        return 1;
    }
}
