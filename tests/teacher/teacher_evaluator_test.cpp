#include "ygo/teacher/candidate_features.hpp"
#include "ygo/teacher/interaction_evaluator.hpp"
#include "ygo/teacher/material_evaluator.hpp"
#include "ygo/teacher/public_fact_registry.hpp"
#include "ygo/teacher/target_evaluator.hpp"
#include "ygo/teacher/tactical_evaluator.hpp"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "ygo/environment/public_action_identity.hpp"
#include "ygo/environment/public_environment_observation.hpp"
#include "ygo/environment/public_safe_state.hpp"
#include "ygo/observation/player_observation.hpp"

namespace {

using ygo::environment::EnvironmentActionCandidate;
using ygo::environment::EnvironmentActionKind;
using ygo::environment::PublicActionKeyInput;
using ygo::environment::PublicCardReference;
using ygo::environment::PublicCardReferenceKind;
using ygo::environment::PublicChoice;
using ygo::environment::PublicChoiceKind;
using ygo::teacher::CandidateEvaluationStatus;
using ygo::teacher::CandidateFeatures;
using ygo::teacher::EvaluatorScoreContribution;
using ygo::teacher::PublicEvaluatorOutcome;
using ygo::teacher::PublicFactExtractionResult;
using ygo::teacher::PublicFactRegistry;
using ygo::teacher::PublicFactSnapshot;
using ygo::teacher::PublicFactSourceClassification;
using ygo::teacher::PublicFactValidityScope;
using ygo::teacher::PublicFactValue;
using ygo::teacher::PublicFactValueKind;
using ygo::teacher::ScoreDimension;
using ygo::teacher::ScoreVector;

void require(const bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

EnvironmentActionCandidate candidate(const EnvironmentActionKind action_kind,
                                     const std::uint64_t choice_value = 0) {
    EnvironmentActionCandidate value;
    value.action_kind = action_kind;
    if (action_kind == EnvironmentActionKind::YesNo) {
        value.choice = PublicChoice{PublicChoiceKind::YesNo, choice_value % 2, std::nullopt};
    } else {
        value.choice = PublicChoice{PublicChoiceKind::EffectChoice, choice_value, std::nullopt};
    }

    PublicActionKeyInput key_input;
    key_input.action_kind = action_kind == EnvironmentActionKind::YesNo ? "yes_no" : "chain";
    key_input.choice = value.choice;
    value.public_action_key = ygo::environment::public_action_key(key_input);
    return value;
}

ygo::observation::PlayerObservation source_observation(
    const std::uint64_t private_engine_step = 11,
    const std::uint32_t private_hidden_card = 12345678) {
    ygo::observation::PlayerObservation source;
    source.schema_version = "ygo.player_observation.v1";
    source.perspective_player = 0;
    source.decision_index = 7;
    source.engine_step_index = private_engine_step;
    source.globals.duel_flags = 0x2e800;
    source.globals.life_points = {8000, 7000};
    source.globals.player_to_act = 0;
    source.globals.turn_player = 0;
    source.globals.turn_count = 3;
    source.globals.phase = 2;
    source.globals.chain_length = 2;
    source.globals.terminal = false;
    source.zones = {{0, ygo::observation::SemanticZone::MonsterZone, 2, 1, 1, true}};

    ygo::observation::ObservedCard visible;
    visible.locator = {"p0:MONSTER_ZONE:0"};
    visible.identity_known = true;
    visible.passcode = 123;
    visible.owner = 0;
    visible.controller = 0;
    visible.zone = ygo::observation::SemanticZone::MonsterZone;
    visible.sequence = 0;
    visible.face_up = true;
    source.entities.push_back(visible);

    ygo::observation::ObservedCard hidden;
    hidden.locator = {"p1:SPELL_TRAP_ZONE:0"};
    hidden.identity_known = false;
    hidden.owner = 1;
    hidden.controller = 1;
    hidden.zone = ygo::observation::SemanticZone::SpellTrapZone;
    hidden.sequence = 0;
    hidden.face_down = true;
    source.entities.push_back(hidden);

    ygo::observation::VisibleGameEvent event;
    event.event_index = 4;
    event.engine_step_index = private_engine_step;
    event.kind = ygo::observation::VisibleEventKind::ChainActivated;
    event.player = 0;
    event.amount = -250;
    source.visible_events.push_back(event);

    source.match_context.perspective_player = 0;
    source.match_context.duel_flags = source.globals.duel_flags;
    source.match_context.knowledge.own_decklist_known = true;
    source.match_context.knowledge.opponent_decklist_known = false;
    source.match_context.own_deck.known = true;
    source.match_context.opponent_deck.known = false;
    source.decision_context.kind = "chain";
    source.decision_context.player = 0;
    source.observation_hash = std::string(64, private_hidden_card == 12345678 ? 'a' : 'b');
    return source;
}

PublicFactValue boolean_fact(const std::string& fact_id,
                             const bool value,
                             const PublicFactValidityScope scope =
                                 PublicFactValidityScope::CurrentReconciliation) {
    PublicFactValue result;
    result.fact_id = fact_id;
    result.value_kind = PublicFactValueKind::Boolean;
    result.boolean_value = value;
    result.validity_scope = scope;
    return result;
}

PublicFactValue u64_fact(const std::string& fact_id,
                         const std::uint64_t value,
                         const PublicFactValidityScope scope =
                             PublicFactValidityScope::CurrentReconciliation) {
    PublicFactValue result;
    result.fact_id = fact_id;
    result.value_kind = PublicFactValueKind::U64;
    result.u64_value = value;
    result.validity_scope = scope;
    return result;
}

PublicFactValue i32_fact(const std::string& fact_id,
                         const std::int32_t value,
                         const PublicFactValidityScope scope =
                             PublicFactValidityScope::CurrentReconciliation) {
    PublicFactValue result;
    result.fact_id = fact_id;
    result.value_kind = PublicFactValueKind::I32;
    result.i32_value = value;
    result.validity_scope = scope;
    return result;
}

PublicFactValue token_fact(const std::string& fact_id,
                           const std::string& value,
                           const PublicFactValidityScope scope =
                               PublicFactValidityScope::CurrentReconciliation) {
    PublicFactValue result;
    result.fact_id = fact_id;
    result.value_kind = PublicFactValueKind::Token;
    result.token_value = value;
    result.validity_scope = scope;
    return result;
}

PublicFactValue require_fact(const PublicFactSnapshot& snapshot,
                             const std::string& fact_id) {
    const auto value = snapshot.value(fact_id);
    require(value.has_value(), "expected public fact is absent: " + fact_id);
    return *value;
}

void test_registry_and_value_contract() {
    const auto& registry = PublicFactRegistry::canonical();
    const auto& definitions = registry.definitions();
    require(!definitions.empty(), "canonical public fact registry is empty");
    for (std::size_t index = 0; index < definitions.size(); ++index) {
        require(!definitions[index].fact_id.empty(), "registry fact ID is empty");
        require(!definitions[index].allowed_scopes.empty(),
                "registry fact has no declared validity scope");
        require(!definitions[index].source_rule.empty(),
                "registry fact has no source/classification rule");
        if (index > 0) {
            require(definitions[index - 1].fact_id < definitions[index].fact_id,
                    "registry fact IDs are not strictly sorted");
        }
        for (std::size_t scope_index = 1;
             scope_index < definitions[index].allowed_scopes.size(); ++scope_index) {
            require(definitions[index].allowed_scopes[scope_index - 1] <
                        definitions[index].allowed_scopes[scope_index],
                    "registry fact scopes are not strictly ordered");
        }
        for (std::size_t token_index = 1;
             token_index < definitions[index].token_domain.size(); ++token_index) {
            require(definitions[index].token_domain[token_index - 1] <
                        definitions[index].token_domain[token_index],
                    "registry token domain is not strictly ordered");
        }
        require(definitions[index].source_classification ==
                        PublicFactSourceClassification::Direct ||
                    definitions[index].source_classification ==
                        PublicFactSourceClassification::SafeDerivation ||
                    definitions[index].source_classification ==
                        PublicFactSourceClassification::Blocked,
                "registry fact classification is invalid");
    }

    const auto boolean = boolean_fact("public.terminal", false);
    const auto u64 = u64_fact("public.life_points.self", 0);
    const auto i32 = i32_fact("test.unregistered.i32", std::numeric_limits<std::int32_t>::min());
    const auto token = token_fact("public.decision_context.kind", "chain");
    require(ygo::teacher::validate_public_fact_value(boolean), "valid boolean fact rejected");
    require(ygo::teacher::validate_public_fact_value(u64), "valid u64 fact rejected");
    require(ygo::teacher::validate_public_fact_value(i32), "valid i32 fact rejected");
    require(ygo::teacher::validate_public_fact_value(token), "valid token fact rejected");

    const auto bytes_a = ygo::teacher::canonical_public_fact_value_bytes(u64);
    const auto bytes_b = ygo::teacher::canonical_public_fact_value_bytes(u64);
    require(bytes_a == bytes_b, "canonical fact encoding is not deterministic");
    require(!registry.validate(i32), "unknown fact ID was accepted by the registry");

    auto out_of_range = u64_fact("public.perspective_player", 2);
    require(!registry.validate(out_of_range), "out-of-bound numeric fact was accepted");
    auto wrong_kind = token_fact("public.perspective_player", "zero");
    require(!registry.validate(wrong_kind), "wrong fact kind was accepted");
    auto wrong_scope = u64_fact("public.perspective_player", 0,
                                PublicFactValidityScope::AcceptedPublicHistory);
    require(!registry.validate(wrong_scope), "wrong fact scope was accepted");
    auto malformed_token = token_fact("public.decision_context.kind", "CHAIN");
    require(!ygo::teacher::validate_public_fact_value(malformed_token),
            "malformed token value was accepted");
    auto unknown_token = token_fact("public.decision_context.kind", "unknown");
    require(!registry.validate(unknown_token),
            "unknown token outside the registered domain was accepted");
    auto inactive_value = boolean_fact("public.terminal", false);
    inactive_value.u64_value = 1;
    require(!ygo::teacher::validate_public_fact_value(inactive_value),
            "inactive kind-specific value was accepted");

    require(registry.source_classification("blocked.private.opponent_hand_identity") ==
                PublicFactSourceClassification::Blocked,
            "hidden opponent hand fact is not BLOCKED");
    require(!registry.validate(token_fact("blocked.private.opponent_hand_identity", "x")),
            "BLOCKED fact produced a value");
    for (const auto& blocked_id : {
             "blocked.continuation.selected_indices",
             "blocked.continuation.remaining_indices",
             "blocked.continuation.assigned_amounts",
             "blocked.continuation.min_count",
             "blocked.continuation.max_count",
             "blocked.continuation.target_sum",
             "blocked.continuation.required_amount",
             "blocked.continuation.available_mask",
             "blocked.continuation.selected_mask",
             "blocked.continuation.can_finish",
             "blocked.continuation.can_cancel",
         }) {
        require(registry.source_classification(blocked_id) ==
                    PublicFactSourceClassification::Blocked,
                "request-wide continuation fact is not BLOCKED");
    }
}

void test_public_fact_extraction_and_safe_decode() {
    const auto public_observation =
        ygo::environment::project_public_observation(source_observation());
    const PublicFactExtractionResult extracted =
        ygo::teacher::extract_public_fact_snapshot(public_observation);
    require(extracted.valid, "valid canonical safe-state observation was rejected");
    const auto& snapshot = extracted.snapshot;
    for (std::size_t index = 1; index < snapshot.values.size(); ++index) {
        require(ygo::teacher::canonical_public_fact_value_bytes(snapshot.values[index - 1]) <
                    ygo::teacher::canonical_public_fact_value_bytes(snapshot.values[index]),
                "public fact snapshot is not strictly canonical-byte ordered");
    }
    require(require_fact(snapshot, "public.perspective_player").u64_value == 0,
            "direct perspective fact changed");
    require(require_fact(snapshot, "public.decision_context.kind").token_value == "chain",
            "direct decision context fact changed");
    require(require_fact(snapshot, "public.turn.phase").u64_value == 2,
            "safe-state phase fact changed");
    require(require_fact(snapshot, "public.turn.count").u64_value == 3,
            "safe-state turn count fact changed");
    require(require_fact(snapshot, "public.chain.length").u64_value == 2,
            "safe-state chain fact changed");
    require(require_fact(snapshot, "public.life_points.self").u64_value == 8000,
            "safe-state self life-points fact changed");
    require(require_fact(snapshot, "public.life_points.opponent").u64_value == 7000,
            "safe-state opponent life-points fact changed");
    require(require_fact(snapshot, "public.terminal").boolean_value == false,
            "safe-state terminal fact changed");
    require(require_fact(snapshot, "public.visible.entity_count").u64_value == 2,
            "safe-derived entity count changed");
    require(require_fact(snapshot, "public.visible.event_count").u64_value == 1,
            "safe-derived event count changed");
    require(require_fact(snapshot, "public.last_event.amount").i32_value == -250,
            "safe-derived last-event amount changed");
    require(require_fact(snapshot, "public.visible.face_down_present").boolean_value,
            "safe-derived face-down occupancy fact missing");
    require(require_fact(snapshot, "public.current_actor_is_perspective").boolean_value,
            "safe-derived current-actor fact missing");

    const auto invalid = ygo::teacher::extract_public_fact_snapshot(
        ygo::environment::PublicEnvironmentObservation{});
    require(!invalid.valid, "arbitrary/empty safe-state bytes were accepted");

    const auto reordered_private_a =
        ygo::environment::project_public_observation(source_observation(11, 12345678));
    const auto reordered_private_b =
        ygo::environment::project_public_observation(source_observation(999, 7654321));
    const auto facts_a = ygo::teacher::extract_public_fact_snapshot(reordered_private_a);
    const auto facts_b = ygo::teacher::extract_public_fact_snapshot(reordered_private_b);
    require(facts_a.valid && facts_b.valid && facts_a.snapshot == facts_b.snapshot,
            "equal public observations produced different fact snapshots");
}

void test_candidate_features_and_evaluators() {
    const auto public_observation =
        ygo::environment::project_public_observation(source_observation());
    const auto extracted = ygo::teacher::extract_public_fact_snapshot(public_observation);
    require(extracted.valid, "evaluator fixture facts are invalid");

    auto chain_candidate = candidate(EnvironmentActionKind::Chain, 7);
    chain_candidate.target_reference = PublicCardReference{
        PublicCardReferenceKind::VisibleCard, "p1:MONSTER_ZONE:0"};
    CandidateFeatures chain_features;
    require(ygo::teacher::extract_candidate_features(
                chain_candidate, extracted.snapshot, chain_features),
            "valid public candidate feature extraction failed");
    require(chain_features.public_action_key == chain_candidate.public_action_key &&
                chain_features.target_is_visible && !chain_features.target_is_redacted,
            "candidate public feature identity/visibility changed");

    const auto tactical = ygo::teacher::TacticalEvaluator{}.evaluate(
        chain_features, extracted.snapshot);
    const auto interaction = ygo::teacher::InteractionEvaluator{}.evaluate(
        chain_features, extracted.snapshot);
    require(tactical.public_action_key == chain_candidate.public_action_key &&
                interaction.public_action_key == chain_candidate.public_action_key,
            "generic evaluator changed the authoritative candidate key");
    require(tactical.status == CandidateEvaluationStatus::NotApplicable,
            "tactical evaluator invented a chain legality result");
    require(interaction.status == CandidateEvaluationStatus::Supported &&
                interaction.contributions.size() == 1 &&
                interaction.contributions[0].dimension == ScoreDimension::InteractionTiming,
            "interaction evaluator did not use public chain timing");

    auto redacted_candidate = candidate(EnvironmentActionKind::CardSelection, 4);
    redacted_candidate.target_reference = PublicCardReference{
        PublicCardReferenceKind::RedactedSlot, "p1:SPELL_TRAP_ZONE:0"};
    CandidateFeatures redacted_features;
    require(ygo::teacher::extract_candidate_features(
                redacted_candidate, extracted.snapshot, redacted_features),
            "redacted public candidate feature extraction failed");
    require(!redacted_features.target_is_visible && redacted_features.target_is_redacted,
            "RedactedSlot was resolved as a visible target");
    const auto redacted_target = ygo::teacher::TargetEvaluator{}.evaluate(
        redacted_features, extracted.snapshot);
    require(redacted_target.status == CandidateEvaluationStatus::Unsupported &&
                redacted_target.public_action_key == redacted_candidate.public_action_key,
            "redacted target evaluator did not remain unsupported/public-only");

    auto battle_candidate = candidate(EnvironmentActionKind::BattleCommand, 8);
    CandidateFeatures battle_features;
    require(ygo::teacher::extract_candidate_features(
                battle_candidate, extracted.snapshot, battle_features),
            "battle candidate feature extraction failed");
    const auto tactical_battle = ygo::teacher::TacticalEvaluator{}.evaluate(
        battle_features, extracted.snapshot);
    require(tactical_battle.status == CandidateEvaluationStatus::Supported &&
                tactical_battle.contributions.size() == 1 &&
                tactical_battle.contributions[0].dimension ==
                    ScoreDimension::ImmediateTacticalNecessity,
            "tactical evaluator did not use public life-points evidence");

    battle_candidate.amount = 7;
    require(ygo::teacher::extract_candidate_features(
                battle_candidate, extracted.snapshot, battle_features),
            "cost candidate feature extraction failed");
    const auto material = ygo::teacher::MaterialEvaluator{}.evaluate(
        battle_features, extracted.snapshot);
    require(material.status == CandidateEvaluationStatus::Supported &&
                material.public_action_key == battle_candidate.public_action_key &&
                material.contributions.size() == 1 &&
                material.contributions[0].dimension ==
                    ScoreDimension::ResourcePreservationAndCost,
            "material evaluator did not use public candidate/resource evidence");

    const auto target = ygo::teacher::TargetEvaluator{}.evaluate(
        chain_features, extracted.snapshot);
    require(target.status == CandidateEvaluationStatus::Supported &&
                target.contributions[0].dimension == ScoreDimension::PublicTargetValue,
            "visible target evaluator did not produce a public target contribution");
}

void test_checked_contribution_composition() {
    ScoreVector score;
    CandidateEvaluationStatus status = CandidateEvaluationStatus::NotApplicable;
    PublicEvaluatorOutcome invalid;
    invalid.public_action_key = candidate(EnvironmentActionKind::YesNo, 0).public_action_key;
    invalid.status = CandidateEvaluationStatus::Supported;
    invalid.contributions.push_back(
        {ScoreDimension::ProfilePreference, 1'000'001});
    require(!ygo::teacher::apply_public_evaluator_outcome(invalid, score, status),
            "out-of-range evaluator contribution was accepted");
    require(status == CandidateEvaluationStatus::Invalid,
            "checked contribution failure was not mapped to INVALID");
    require(score.values[static_cast<std::size_t>(ScoreDimension::ProfilePreference)] == 0,
            "failed contribution changed the score");

    PublicEvaluatorOutcome valid;
    valid.public_action_key = invalid.public_action_key;
    valid.status = CandidateEvaluationStatus::Supported;
    valid.contributions.push_back(
        {ScoreDimension::ProfilePreference, 1'000'000});
    status = CandidateEvaluationStatus::NotApplicable;
    require(ygo::teacher::apply_public_evaluator_outcome(valid, score, status),
            "valid boundary contribution was rejected");
    require(status == CandidateEvaluationStatus::Supported &&
                score.values[static_cast<std::size_t>(ScoreDimension::ProfilePreference)] ==
                    1'000'000,
            "valid contribution did not accumulate exactly");
}

void emit_fact_matrix() {
    const auto& definitions = PublicFactRegistry::canonical().definitions();
    for (const auto& definition : definitions) {
        std::cout << definition.fact_id << '|'
                  << ygo::teacher::public_fact_value_kind_name(definition.value_kind) << '|'
                  << ygo::teacher::public_fact_scope_name(definition.allowed_scopes.front())
                  << '|'
                  << ygo::teacher::public_fact_source_name(definition.source_classification)
                  << '|' << definition.source_rule << '\n';
    }
}

}  // namespace

int main(int argc, char** argv) {
    try {
        if (argc == 2 && std::string(argv[1]) == "--fact-matrix") {
            emit_fact_matrix();
            return 0;
        }
        test_registry_and_value_contract();
        test_public_fact_extraction_and_safe_decode();
        test_candidate_features_and_evaluators();
        test_checked_contribution_composition();
        std::cout << "teacher_evaluator_test: PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "teacher_evaluator_test: FAIL: " << error.what() << '\n';
        return 1;
    }
}
