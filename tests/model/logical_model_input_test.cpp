#include "ygo/model/logical_model_input.hpp"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "episodic_environment_test_access.hpp"
#include "ygo/environment/episodic_environment.hpp"
#include "ygo/environment/public_safe_state.hpp"
#include "ygo/observation/decision_integration.hpp"
#include "ygo/observation/player_observation.hpp"
#include "ygo/observation/serialization.hpp"
#include "ygo/protocol/continuation.hpp"

namespace {

using ygo::environment::EnvironmentActionCandidate;
using ygo::environment::EnvironmentActionKind;
using ygo::environment::CertifiedEnvironmentConfig;
using ygo::environment::DecisionFrame;
using ygo::environment::EpisodicEnvironment;
using ygo::environment::PublicActionKeyInput;
using ygo::environment::PublicCardReference;
using ygo::environment::PublicCardReferenceKind;
using ygo::environment::PublicChoice;
using ygo::environment::PublicChoiceKind;
using ygo::environment::PublicEnvironmentObservation;
using ygo::model::LogicalModelInput;
using ygo::model::LogicalModelProjectionErrorCode;
using ygo::model::LogicalModelProjectionResult;

using ygo::observation::ObservedCard;
using ygo::observation::PlayerObservation;
using ygo::protocol::ActionCandidate;
using ygo::protocol::DecisionRequest;

constexpr std::string_view kCurrentLocator = "p0:MONSTER_ZONE:0";
constexpr std::string_view kHistoricalHiddenLocator = "p1:SPELL_TRAP_ZONE:0";

void require(const bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

ygo::observation::ObservedCard current_card() {
    ygo::observation::ObservedCard card;
    card.locator = {std::string(kCurrentLocator)};
    card.identity_known = true;
    card.passcode = 1001;
    card.owner = 0;
    card.controller = 0;
    card.zone = ygo::observation::SemanticZone::MonsterZone;
    card.sequence = 0;
    card.position = ygo::observation::Position::FaceUpAttack;
    card.face_up = true;
    card.printed.emplace();
    card.printed->type = 0x401;
    card.printed->attribute = 0x20;
    card.printed->race = 0x1;
    card.printed->attack = 2500;
    card.printed->defense = 2000;
    card.printed->level = 8;
    card.current.emplace();
    card.current->attack = 2700;
    card.current->defense = 2100;
    card.current->status_flags = 0x8;
    return card;
}

ygo::observation::ObservedCard redacted_card() {
    ygo::observation::ObservedCard card;
    card.locator = {std::string(kHistoricalHiddenLocator)};
    card.identity_known = false;
    card.owner = 1;
    card.controller = 1;
    card.zone = ygo::observation::SemanticZone::SpellTrapZone;
    card.sequence = 0;
    card.position = ygo::observation::Position::FaceDownDefense;
    card.face_down = true;
    return card;
}

ygo::observation::PlayerObservation private_source_observation(
    const std::string& private_marker) {
    ygo::observation::PlayerObservation observation;
    observation.schema_version = "ygo.player_observation.v1";
    observation.perspective_player = 0;
    observation.decision_index = 17;
    observation.engine_step_index = 9001;

    observation.globals.duel_flags = 0x1122334455667788ULL;
    observation.globals.life_points = {8000, 7000};
    observation.globals.player_to_act = 0;
    observation.globals.turn_player = 0;
    observation.globals.turn_count = 3;
    observation.globals.phase = 2;
    observation.globals.chain_length = 1;
    observation.globals.terminal = false;

    observation.zones = {
        {1, ygo::observation::SemanticZone::SpellTrapZone, 5, 0, 5, false},
        {0, ygo::observation::SemanticZone::MonsterZone, 5, 1, 4, true},
    };
    observation.entities = {redacted_card(), current_card()};
    observation.relationships.push_back(
        {ygo::observation::RelationshipKind::Target,
         {std::string(kCurrentLocator)},
         {std::string(kHistoricalHiddenLocator)}});

    observation.chain.length = 1;
    ygo::observation::ChainLink chain_link;
    chain_link.index = 0;
    chain_link.activating_player = 0;
    chain_link.source = ygo::observation::ObservationLocator{std::string(kCurrentLocator)};
    chain_link.activation_zone = ygo::observation::SemanticZone::MonsterZone;
    chain_link.effect_description = 0x1020304050607080ULL;
    chain_link.targets = {
        ygo::observation::ObservationLocator{std::string(kHistoricalHiddenLocator)}};
    observation.chain.links.push_back(chain_link);

    ygo::observation::VisibleGameEvent event;
    event.event_index = 7;
    event.engine_step_index = 123456789;
    event.kind = ygo::observation::VisibleEventKind::CardMoved;
    event.player = 0;
    event.entity = ygo::observation::ObservationLocator{std::string(kCurrentLocator)};
    event.public_passcode = 1001;
    event.from_zone = ygo::observation::SemanticZone::Hand;
    event.to_zone = ygo::observation::SemanticZone::MonsterZone;
    event.count = 1;
    event.amount = -500;
    event.counter_type = 7;
    event.phase = 2;
    event.effect_description = 0x1020304050607080ULL;
    event.targets = {
        ygo::observation::ObservationLocator{std::string(kHistoricalHiddenLocator)}};
    observation.visible_events.push_back(event);

    observation.match_context.perspective_player = 0;
    observation.match_context.duel_flags = observation.globals.duel_flags;
    observation.match_context.knowledge.own_decklist_known = true;
    observation.match_context.knowledge.opponent_decklist_known = false;
    observation.match_context.own_deck.known = true;
    observation.match_context.own_deck.main_deck = {1001, 2002, 3003};
    observation.match_context.own_deck.extra_deck = {4004, 5005};
    observation.match_context.opponent_deck.known = false;

    observation.decision_context.kind = "card_selection";
    observation.decision_context.player = 0;
    observation.decision_context.decision_id = "private-decision." + private_marker;
    observation.decision_context.continuation_id = "private-continuation." + private_marker;
    observation.decision_context.engine_step_index = 9001;
    observation.decision_context.engine_message_type = 15;
    observation.decision_context.engine_message_name = "MSG_SELECT_CARD";
    observation.decision_context.referenced_entities = {
        {std::string(kHistoricalHiddenLocator)},
        {std::string(kCurrentLocator)},
    };
    observation.observation_hash = ygo::observation::observation_hash(observation);
    return observation;
}

PublicEnvironmentObservation public_observation(const std::string& private_marker) {
    return ygo::environment::project_public_observation(
        private_source_observation(private_marker));
}

std::unique_ptr<EpisodicEnvironment> make_real_paired_environment() {
    auto factory = EpisodicEnvironment::create(CertifiedEnvironmentConfig::canonical());
    require(std::holds_alternative<std::unique_ptr<EpisodicEnvironment>>(factory),
            "real paired-world fixture could not create the canonical environment");
    return std::move(std::get<std::unique_ptr<EpisodicEnvironment>>(factory));
}

PlayerObservation real_hidden_observation(const std::uint8_t perspective,
                                          const std::uint32_t hidden_code,
                                          const std::uint64_t engine_step_index = 91) {
    PlayerObservation observation;
    observation.perspective_player = perspective;
    observation.engine_step_index = engine_step_index;
    observation.globals.life_points = {8000, 8000};
    observation.globals.terminal = false;
    observation.match_context.perspective_player = perspective;

    const auto hidden_controller = static_cast<std::uint8_t>(1 - perspective);
    const auto locator = std::string("p") + std::to_string(hidden_controller) +
                         ":SPELL_TRAP_ZONE:0";
    observation.zones.push_back(
        {hidden_controller, ygo::observation::SemanticZone::SpellTrapZone, 1, 0, 1, false});
    ObservedCard hidden;
    hidden.locator = {locator};
    hidden.identity_known = false;
    hidden.controller = hidden_controller;
    hidden.zone = ygo::observation::SemanticZone::SpellTrapZone;
    hidden.sequence = 0;
    hidden.face_down = true;
    observation.entities.push_back(std::move(hidden));

    // The hidden identity belongs to the internal request below. It is
    // deliberately not copied into this perspective-safe observation.
    (void)hidden_code;
    observation.observation_hash = ygo::observation::observation_hash(observation);
    return observation;
}

DecisionRequest real_atomic_hidden_request(const std::uint32_t hidden_code) {
    DecisionRequest request;
    request.kind = ygo::protocol::DecisionRequestKind::CardSelection;
    request.decision_id = "private-decision.card." + std::to_string(hidden_code);
    request.engine_step_index = 91;
    request.player = 1;
    request.engine_message_type = 15;
    request.engine_message_name = "MSG_SELECT_CARD";
    request.raw_message_hash = "private-raw." + std::to_string(hidden_code);

    ActionCandidate candidate;
    candidate.action_kind = ygo::protocol::ActionKind::CardSelection;
    candidate.semantic_key = "card.0.3." + std::to_string(hidden_code) + ".0.8.0";
    candidate.source_card = hidden_code;
    candidate.source_controller = 0;
    candidate.source_location = 8;
    candidate.source_sequence = 0;
    candidate.source_index = 3;
    candidate.exact_response_bytes = {3, 0, 0, 0};
    request.candidates.push_back(std::move(candidate));
    return request;
}

PlayerObservation real_observation_for_request(const DecisionRequest& request,
                                               const std::uint32_t hidden_code) {
    auto observation = real_hidden_observation(request.player, hidden_code,
                                                request.engine_step_index);
    ygo::observation::attach_decision_context(observation, request);
    ygo::observation::VisibleGameEvent historical_event;
    historical_event.event_index = 3;
    historical_event.engine_step_index = 80;
    historical_event.kind = ygo::observation::VisibleEventKind::CardRevealed;
    historical_event.player = 0;
    historical_event.entity = ygo::observation::ObservationLocator{
        "p0:SPELL_TRAP_ZONE:0"};
    historical_event.to_zone = ygo::observation::SemanticZone::SpellTrapZone;
    observation.visible_events.push_back(std::move(historical_event));
    observation.observation_hash = ygo::observation::observation_hash(observation);
    return observation;
}

EnvironmentActionCandidate candidate_with_index(const std::uint32_t source_index) {
    PublicActionKeyInput key;
    key.action_kind = "card_selection";
    key.source_index = source_index;

    EnvironmentActionCandidate candidate;
    candidate.action_kind = EnvironmentActionKind::CardSelection;
    candidate.source_index = source_index;
    candidate.public_action_key = ygo::environment::public_action_key(key);
    return candidate;
}

EnvironmentActionCandidate candidate_with_full_descriptor() {
    PublicActionKeyInput key;
    key.action_kind = "card_selection";
    key.choice = PublicChoice{PublicChoiceKind::EffectChoice, 17, std::nullopt};
    key.source_reference =
        PublicCardReference{PublicCardReferenceKind::VisibleCard, std::string(kCurrentLocator)};
    key.target_reference = PublicCardReference{
        PublicCardReferenceKind::RedactedSlot, std::string(kHistoricalHiddenLocator)};
    key.phase = 2;
    key.position = 1;
    key.source_index = 3;
    key.amount = 17;
    key.continuation_operation = "pick";

    EnvironmentActionCandidate candidate;
    candidate.action_kind = EnvironmentActionKind::CardSelection;
    candidate.choice = key.choice;
    candidate.source_reference = key.source_reference;
    candidate.target_reference = key.target_reference;
    candidate.phase = key.phase;
    candidate.position = key.position;
    candidate.source_index = key.source_index;
    candidate.amount = key.amount;
    candidate.continuation_operation = key.continuation_operation;
    candidate.submits_engine_response = false;
    candidate.public_action_key = ygo::environment::public_action_key(key);
    return candidate;
}

const LogicalModelInput& require_value(
    const LogicalModelProjectionResult& result,
    const std::string& context) {
    require(static_cast<bool>(result), context + " did not project");
    require(result.value.has_value(), context + " returned no value");
    return *result.value;
}

void require_error(
    const LogicalModelProjectionResult& result,
    const LogicalModelProjectionErrorCode expected,
    const std::string& context) {
    require(!static_cast<bool>(result), context + " unexpectedly projected");
    require(!result.value.has_value(), context + " returned a value on failure");
    require(result.error.has_value(), context + " returned no structured error");
    require(result.error->code == expected, context + " returned the wrong error code");
    require(!result.error->diagnostic.empty(), context + " returned no diagnostic");
}

bool same_public_choice(const std::optional<PublicChoice>& left,
                        const std::optional<PublicChoice>& right) {
    if (left.has_value() != right.has_value()) {
        return false;
    }
    return !left.has_value() ||
           (left->kind == right->kind && left->value == right->value &&
            left->response_index == right->response_index);
}

bool same_public_reference(const std::optional<PublicCardReference>& left,
                           const std::optional<PublicCardReference>& right) {
    if (left.has_value() != right.has_value()) {
        return false;
    }
    return !left.has_value() ||
           (left->kind == right->kind &&
            left->observation_locator == right->observation_locator);
}

bool same_public_candidate(const EnvironmentActionCandidate& left,
                           const EnvironmentActionCandidate& right) {
    return left.action_kind == right.action_kind &&
           left.public_action_key == right.public_action_key &&
           same_public_choice(left.choice, right.choice) &&
           same_public_reference(left.source_reference, right.source_reference) &&
           same_public_reference(left.target_reference, right.target_reference) &&
           left.phase == right.phase && left.position == right.position &&
           left.source_index == right.source_index && left.amount == right.amount &&
           left.continuation_operation == right.continuation_operation &&
           left.submits_engine_response == right.submits_engine_response;
}

void require_same_public_candidate_domain(const DecisionFrame& left,
                                          const DecisionFrame& right) {
    require(left.request.kind == right.request.kind &&
                left.request.player == right.request.player,
            "real paired public request context differs");
    require(left.request.candidates.size() == right.request.candidates.size(),
            "real paired public candidate counts differ");
    for (std::size_t index = 0; index < left.request.candidates.size(); ++index) {
        require(same_public_candidate(left.request.candidates[index],
                                      right.request.candidates[index]),
                "real paired public candidate domain differs");
    }
    require(left.public_candidate_domain_digest == right.public_candidate_domain_digest,
            "real paired public candidate-domain digests differ");
}

bool same_logical_locator(const ygo::model::LogicalPublicLocator& left,
                          const ygo::model::LogicalPublicLocator& right) {
    return left.value == right.value &&
           left.public_locator_ordinal == right.public_locator_ordinal;
}

bool same_logical_locator_vector(
    const std::vector<ygo::model::LogicalPublicLocator>& left,
    const std::vector<ygo::model::LogicalPublicLocator>& right) {
    if (left.size() != right.size()) {
        return false;
    }
    for (std::size_t index = 0; index < left.size(); ++index) {
        if (!same_logical_locator(left[index], right[index])) {
            return false;
        }
    }
    return true;
}

bool same_logical_current_reference(const ygo::model::LogicalCurrentReference& left,
                                    const ygo::model::LogicalCurrentReference& right) {
    return same_logical_locator(left.locator, right.locator) &&
           left.current_entity_ordinal == right.current_entity_ordinal;
}

bool same_logical_historical_reference(
    const ygo::model::LogicalHistoricalReference& left,
    const ygo::model::LogicalHistoricalReference& right) {
    return same_logical_locator(left.locator, right.locator);
}

bool same_logical_card_properties(
    const std::optional<ygo::observation::CardProperties>& left,
    const std::optional<ygo::observation::CardProperties>& right) {
    if (left.has_value() != right.has_value()) {
        return false;
    }
    if (!left.has_value()) {
        return true;
    }
    if (left->type != right->type || left->attribute != right->attribute ||
        left->race != right->race || left->attack != right->attack ||
        left->defense != right->defense || left->base_attack != right->base_attack ||
        left->base_defense != right->base_defense || left->level != right->level ||
        left->rank != right->rank || left->link_rating != right->link_rating ||
        left->link_markers != right->link_markers || left->left_scale != right->left_scale ||
        left->right_scale != right->right_scale || left->status_flags != right->status_flags ||
        left->counters.size() != right->counters.size()) {
        return false;
    }
    for (std::size_t index = 0; index < left->counters.size(); ++index) {
        if (left->counters[index].type != right->counters[index].type ||
            left->counters[index].count != right->counters[index].count) {
            return false;
        }
    }
    return true;
}

bool same_observed_card(const ObservedCard& left, const ObservedCard& right) {
    return left.locator == right.locator && left.identity_known == right.identity_known &&
           left.passcode == right.passcode && left.owner == right.owner &&
           left.controller == right.controller && left.zone == right.zone &&
           left.sequence == right.sequence && left.overlay_sequence == right.overlay_sequence &&
           left.position == right.position && left.face_up == right.face_up &&
           left.face_down == right.face_down &&
           same_logical_card_properties(left.printed, right.printed) &&
           same_logical_card_properties(left.current, right.current);
}

bool same_globals(const ygo::observation::ObservedPlayerGlobals& left,
                  const ygo::observation::ObservedPlayerGlobals& right) {
    return left.duel_flags == right.duel_flags && left.life_points == right.life_points &&
           left.player_to_act == right.player_to_act && left.turn_player == right.turn_player &&
           left.turn_count == right.turn_count && left.phase == right.phase &&
           left.chain_length == right.chain_length && left.winner == right.winner &&
           left.win_reason == right.win_reason && left.terminal == right.terminal;
}

bool same_zones(const std::vector<ygo::observation::ObservedZone>& left,
                const std::vector<ygo::observation::ObservedZone>& right) {
    if (left.size() != right.size()) {
        return false;
    }
    for (std::size_t index = 0; index < left.size(); ++index) {
        if (left[index].player != right[index].player ||
            left[index].kind != right[index].kind ||
            left[index].total_count != right[index].total_count ||
            left[index].public_identity_count != right[index].public_identity_count ||
            left[index].hidden_count != right[index].hidden_count ||
            left[index].player_observable_order != right[index].player_observable_order) {
            return false;
        }
    }
    return true;
}

bool same_match_context(const ygo::observation::MatchContext& left,
                        const ygo::observation::MatchContext& right) {
    return left.perspective_player == right.perspective_player &&
           left.duel_flags == right.duel_flags &&
           left.knowledge.own_decklist_known == right.knowledge.own_decklist_known &&
           left.knowledge.opponent_decklist_known == right.knowledge.opponent_decklist_known &&
           left.own_deck.known == right.own_deck.known &&
           left.own_deck.main_deck == right.own_deck.main_deck &&
           left.own_deck.extra_deck == right.own_deck.extra_deck &&
           left.opponent_deck.known == right.opponent_deck.known &&
           left.opponent_deck.main_deck == right.opponent_deck.main_deck &&
           left.opponent_deck.extra_deck == right.opponent_deck.extra_deck;
}

bool same_logical_card_reference(
    const std::optional<ygo::model::LogicalPublicCardReference>& left,
    const std::optional<ygo::model::LogicalPublicCardReference>& right) {
    if (left.has_value() != right.has_value()) {
        return false;
    }
    return !left.has_value() ||
           (left->kind == right->kind &&
            same_logical_current_reference(left->reference, right->reference));
}

bool same_logical_entities(const std::vector<ygo::model::LogicalEntity>& left,
                           const std::vector<ygo::model::LogicalEntity>& right) {
    if (left.size() != right.size()) {
        return false;
    }
    for (std::size_t index = 0; index < left.size(); ++index) {
        if (!same_observed_card(left[index].card, right[index].card) ||
            left[index].public_locator_ordinal != right[index].public_locator_ordinal ||
            left[index].current_entity_ordinal != right[index].current_entity_ordinal) {
            return false;
        }
    }
    return true;
}

bool same_logical_relationships(
    const std::vector<ygo::model::LogicalRelationship>& left,
    const std::vector<ygo::model::LogicalRelationship>& right) {
    if (left.size() != right.size()) {
        return false;
    }
    for (std::size_t index = 0; index < left.size(); ++index) {
        if (left[index].kind != right[index].kind ||
            !same_logical_current_reference(left[index].source, right[index].source) ||
            !same_logical_current_reference(left[index].target, right[index].target)) {
            return false;
        }
    }
    return true;
}

bool same_logical_chain(
    const ygo::model::LogicalChainState& left,
    const ygo::model::LogicalChainState& right) {
    if (left.length != right.length || left.links.size() != right.links.size()) {
        return false;
    }
    for (std::size_t index = 0; index < left.links.size(); ++index) {
        const auto& left_link = left.links[index];
        const auto& right_link = right.links[index];
        if (left_link.index != right_link.index ||
            left_link.activating_player != right_link.activating_player ||
            left_link.activation_zone != right_link.activation_zone ||
            left_link.effect_description != right_link.effect_description ||
            left_link.source.has_value() != right_link.source.has_value() ||
            left_link.targets.size() != right_link.targets.size()) {
            return false;
        }
        if (left_link.source.has_value() &&
            !same_logical_current_reference(*left_link.source, *right_link.source)) {
            return false;
        }
        for (std::size_t target = 0; target < left_link.targets.size(); ++target) {
            if (!same_logical_current_reference(left_link.targets[target],
                                                 right_link.targets[target])) {
                return false;
            }
        }
    }
    return true;
}

bool same_logical_visible_events(
    const std::vector<ygo::model::LogicalVisibleEvent>& left,
    const std::vector<ygo::model::LogicalVisibleEvent>& right) {
    if (left.size() != right.size()) {
        return false;
    }
    for (std::size_t index = 0; index < left.size(); ++index) {
        const auto& left_event = left[index];
        const auto& right_event = right[index];
        if (left_event.event_index != right_event.event_index ||
            left_event.kind != right_event.kind || left_event.player != right_event.player ||
            left_event.public_passcode != right_event.public_passcode ||
            left_event.from_zone != right_event.from_zone ||
            left_event.to_zone != right_event.to_zone || left_event.count != right_event.count ||
            left_event.amount != right_event.amount ||
            left_event.counter_type != right_event.counter_type ||
            left_event.phase != right_event.phase || left_event.winner != right_event.winner ||
            left_event.win_reason != right_event.win_reason ||
            left_event.effect_description != right_event.effect_description ||
            left_event.entity.has_value() != right_event.entity.has_value() ||
            left_event.targets.size() != right_event.targets.size()) {
            return false;
        }
        if (left_event.entity.has_value() &&
            !same_logical_historical_reference(*left_event.entity, *right_event.entity)) {
            return false;
        }
        for (std::size_t target = 0; target < left_event.targets.size(); ++target) {
            if (!same_logical_historical_reference(left_event.targets[target],
                                                   right_event.targets[target])) {
                return false;
            }
        }
    }
    return true;
}

bool same_logical_state(const ygo::model::LogicalPublicState& left,
                        const ygo::model::LogicalPublicState& right) {
    return same_globals(left.globals, right.globals) && same_zones(left.zones, right.zones) &&
           same_logical_entities(left.entities, right.entities) &&
           same_logical_relationships(left.relationships, right.relationships) &&
           same_logical_chain(left.chain, right.chain) &&
           same_logical_visible_events(left.visible_events, right.visible_events) &&
           same_match_context(left.match_context, right.match_context);
}

bool same_logical_candidate(const ygo::model::LogicalCandidate& left,
                            const ygo::model::LogicalCandidate& right) {
    return left.action_kind == right.action_kind &&
           same_public_choice(left.choice, right.choice) &&
           same_logical_card_reference(left.source_reference, right.source_reference) &&
           same_logical_card_reference(left.target_reference, right.target_reference) &&
           left.phase == right.phase && left.position == right.position &&
           left.source_index == right.source_index && left.amount == right.amount &&
           left.continuation_operation == right.continuation_operation &&
           left.submits_engine_response == right.submits_engine_response;
}

bool same_logical_input(const LogicalModelInput& left,
                        const LogicalModelInput& right) {
    if (left.schema_id != right.schema_id ||
        left.public_observation_digest != right.public_observation_digest ||
        left.public_candidate_domain_digest != right.public_candidate_domain_digest ||
        left.perspective_player != right.perspective_player ||
        left.decision_index != right.decision_index ||
        left.public_observation_context_kind != right.public_observation_context_kind ||
        left.public_observation_context_player != right.public_observation_context_player ||
        !same_logical_locator_vector(left.referenced_public_entities,
                                     right.referenced_public_entities) ||
        !same_logical_locator_vector(left.public_locator_table,
                                     right.public_locator_table) ||
        !same_logical_state(left.public_safe_state, right.public_safe_state) ||
        left.candidate_routing.size() != right.candidate_routing.size() ||
        left.candidate_features.size() != right.candidate_features.size()) {
        return false;
    }
    for (std::size_t index = 0; index < left.candidate_routing.size(); ++index) {
        if (left.candidate_routing[index].public_action_key !=
            right.candidate_routing[index].public_action_key) {
            return false;
        }
        if (!same_logical_candidate(left.candidate_features[index],
                                    right.candidate_features[index])) {
            return false;
        }
    }
    return true;
}

std::string logical_string_fields(const LogicalModelInput& input) {
    std::string result = input.schema_id + input.public_observation_digest;
    if (input.public_candidate_domain_digest.has_value()) {
        result += *input.public_candidate_domain_digest;
    }
    if (input.public_observation_context_kind.has_value()) {
        result += *input.public_observation_context_kind;
    }
    for (const auto& locator : input.referenced_public_entities) {
        result += locator.value;
    }
    for (const auto& locator : input.public_locator_table) {
        result += locator.value;
    }
    for (const auto& candidate : input.candidate_routing) {
        result += candidate.public_action_key;
    }
    for (const auto& candidate : input.candidate_features) {
        if (candidate.source_reference.has_value()) {
            result += candidate.source_reference->reference.locator.value;
        }
        if (candidate.target_reference.has_value()) {
            result += candidate.target_reference->reference.locator.value;
        }
        result += candidate.continuation_operation;
    }
    for (const auto& entity : input.public_safe_state.entities) {
        result += entity.card.locator.value;
    }
    for (const auto& relationship : input.public_safe_state.relationships) {
        result += relationship.source.locator.value;
        result += relationship.target.locator.value;
    }
    for (const auto& link : input.public_safe_state.chain.links) {
        if (link.source.has_value()) {
            result += link.source->locator.value;
        }
        for (const auto& target : link.targets) {
            result += target.locator.value;
        }
    }
    for (const auto& event : input.public_safe_state.visible_events) {
        if (event.entity.has_value()) {
            result += event.entity->locator.value;
        }
        for (const auto& target : event.targets) {
            result += target.locator.value;
        }
    }
    return result;
}

void test_full_public_descriptor_is_copied() {
    const auto observation = public_observation("descriptor");
    const auto source = candidate_with_full_descriptor();
    const auto result = ygo::model::project_logical_model_input(observation, {source});
    const auto& projected = require_value(result, "full public descriptor");

    require(projected.schema_id == "ocgforge.model_logical_input.v1",
            "logical schema ID changed");
    require(projected.public_observation_digest ==
                ygo::environment::public_observation_digest(observation),
            "public observation digest was not recomputed");
    require(projected.public_candidate_domain_digest.has_value(),
            "candidate-domain digest was not derived from public context");
    require(*projected.public_candidate_domain_digest ==
                ygo::environment::public_candidate_domain_digest(
                    "card_selection", {source.public_action_key}),
            "candidate-domain digest was not derived from the ordered public keys");
    require(projected.candidate_features.size() == 1,
            "full public descriptor changed candidate count");
    require(projected.candidate_routing.size() == 1,
            "full public descriptor changed routing count");

    const auto& actual = projected.candidate_features.front();
    require(actual.action_kind == source.action_kind,
            "action kind was not copied");
    require(actual.choice.has_value() && source.choice.has_value() &&
                actual.choice->kind == source.choice->kind &&
                actual.choice->value == source.choice->value &&
                actual.choice->response_index == source.choice->response_index,
            "typed public choice was not copied");
    require(actual.source_reference.has_value() && source.source_reference.has_value() &&
                actual.source_reference->kind == source.source_reference->kind &&
                actual.source_reference->reference.locator.value ==
                    source.source_reference->observation_locator,
            "source reference was not copied");
    require(actual.target_reference.has_value() && source.target_reference.has_value() &&
                actual.target_reference->kind == source.target_reference->kind &&
                actual.target_reference->reference.locator.value ==
                    source.target_reference->observation_locator,
            "target reference was not copied");
    require(actual.phase == source.phase && actual.position == source.position &&
                actual.source_index == source.source_index && actual.amount == source.amount &&
                actual.continuation_operation == source.continuation_operation &&
                actual.submits_engine_response == source.submits_engine_response,
            "public candidate descriptor field was changed");
    require(projected.candidate_routing.front().public_action_key ==
                source.public_action_key,
            "public action key was not retained in routing metadata");
}

void test_public_state_fields_are_copied() {
    const auto observation = public_observation("state-fields");
    const auto result = ygo::model::project_logical_model_input(
        observation, {candidate_with_index(1)});
    const auto& projected = require_value(result, "public state fields");

    require(projected.public_safe_state.globals.duel_flags ==
                0x1122334455667788ULL &&
                projected.public_safe_state.globals.life_points ==
                    std::vector<std::uint32_t>{8000, 7000} &&
                projected.public_safe_state.globals.player_to_act == 0 &&
                projected.public_safe_state.globals.turn_player == 0 &&
                projected.public_safe_state.globals.turn_count == 3 &&
                projected.public_safe_state.globals.phase == 2 &&
                projected.public_safe_state.globals.chain_length == 1 &&
                !projected.public_safe_state.globals.terminal,
            "public globals were not copied exactly");
    require(projected.public_safe_state.zones.size() == 2,
            "public zones were not copied exactly");
    require(projected.public_safe_state.zones[0].player == 0 &&
                projected.public_safe_state.zones[0].total_count == 5 &&
                projected.public_safe_state.zones[1].player == 1 &&
                projected.public_safe_state.zones[1].hidden_count == 5,
            "public zone fields were not copied exactly");

    require(projected.public_safe_state.entities.size() == 2,
            "public entities were not copied exactly");
    require(projected.public_safe_state.entities[0].card.identity_known &&
                projected.public_safe_state.entities[0].card.passcode == 1001 &&
                projected.public_safe_state.entities[0].card.printed.has_value() &&
                projected.public_safe_state.entities[0].card.current.has_value() &&
                !projected.public_safe_state.entities[1].card.identity_known &&
                !projected.public_safe_state.entities[1].card.passcode.has_value(),
            "public entity visibility or properties were changed");

    require(projected.public_safe_state.relationships.size() == 1 &&
                projected.public_safe_state.relationships[0].kind ==
                    ygo::observation::RelationshipKind::Target &&
                projected.public_safe_state.relationships[0].source
                        .locator.value == kCurrentLocator &&
                projected.public_safe_state.relationships[0].target
                        .locator.value == kHistoricalHiddenLocator,
            "public relationships were not copied exactly");
    require(projected.public_safe_state.chain.length == 1 &&
                projected.public_safe_state.chain.links.size() == 1 &&
                projected.public_safe_state.chain.links[0].index == 0 &&
                projected.public_safe_state.chain.links[0].source.has_value() &&
                projected.public_safe_state.chain.links[0].source->locator.value ==
                    kCurrentLocator &&
                projected.public_safe_state.chain.links[0].targets.size() == 1 &&
                projected.public_safe_state.chain.links[0].targets[0].locator.value ==
                    kHistoricalHiddenLocator,
            "public chain fields were not copied exactly");

    require(projected.public_safe_state.visible_events.size() == 1 &&
                projected.public_safe_state.visible_events[0].event_index == 7 &&
                projected.public_safe_state.visible_events[0].public_passcode == 1001 &&
                projected.public_safe_state.visible_events[0].amount == -500 &&
                projected.public_safe_state.visible_events[0].targets.size() == 1,
            "public visible-event fields were not copied exactly");
    require(projected.public_safe_state.match_context.perspective_player == 0 &&
                projected.public_safe_state.match_context.own_deck.known &&
                projected.public_safe_state.match_context.own_deck.main_deck ==
                    std::vector<std::uint32_t>{1001, 2002, 3003} &&
                projected.public_safe_state.match_context.opponent_deck.known == false,
            "public match context was not copied exactly");
}

void test_candidate_order_and_value_ownership() {
    const auto observation = public_observation("order");
    std::vector<EnvironmentActionCandidate> source = {
        candidate_with_index(11), candidate_with_index(3), candidate_with_index(7)};
    const auto expected_keys = std::vector<std::string>{
        source[0].public_action_key, source[1].public_action_key, source[2].public_action_key};

    const auto result = ygo::model::project_logical_model_input(observation, source);
    const auto& projected = require_value(result, "candidate order");
    require(projected.candidate_features.size() == source.size(),
            "candidate order test changed N");
    require(projected.candidate_routing.size() == source.size(),
            "candidate order test changed routing N");
    for (std::size_t index = 0; index < source.size(); ++index) {
        require(projected.candidate_routing[index].public_action_key == expected_keys[index],
                "candidate order was sorted or changed");
        require(projected.candidate_features[index].source_index == source[index].source_index,
                "candidate descriptor was detached from its routing key");
    }

    source[0].source_index = 999;
    source[0].public_action_key = "public_action.v1.00";
    require(projected.candidate_features[0].source_index == 11,
            "logical DTO did not own the candidate descriptor");
    require(projected.candidate_routing[0].public_action_key == expected_keys[0],
            "logical DTO did not own the routing key");
}

void test_duplicate_malformed_and_unsafe_candidates_fail_closed() {
    const auto observation = public_observation("invalid-candidates");

    require_error(
        ygo::model::project_logical_model_input(observation, {}),
        LogicalModelProjectionErrorCode::EmptyCandidateDomain,
        "empty candidate domain");

    const auto duplicate = candidate_with_index(4);
    require_error(
        ygo::model::project_logical_model_input(observation, {duplicate, duplicate}),
        LogicalModelProjectionErrorCode::DuplicatePublicActionKey,
        "duplicate candidate key");

    auto malformed = candidate_with_index(5);
    malformed.public_action_key = "not-a-public-action-key";
    require_error(
        ygo::model::project_logical_model_input(observation, {malformed}),
        LogicalModelProjectionErrorCode::InvalidPublicActionKey,
        "malformed candidate key");

    auto inconsistent = candidate_with_index(12);
    inconsistent.source_index = 13;
    require_error(
        ygo::model::project_logical_model_input(observation, {inconsistent}),
        LogicalModelProjectionErrorCode::InvalidPublicCandidateDescriptor,
        "inconsistent candidate key and descriptor");

    auto invalid_action = candidate_with_index(6);
    invalid_action.action_kind = EnvironmentActionKind::Unsupported;
    require_error(
        ygo::model::project_logical_model_input(observation, {invalid_action}),
        LogicalModelProjectionErrorCode::InvalidPublicCandidateDescriptor,
        "unsupported candidate action");

    auto invalid_reference = candidate_with_full_descriptor();
    invalid_reference.source_reference->observation_locator = "invalid\nlocator";
    require_error(
        ygo::model::project_logical_model_input(observation, {invalid_reference}),
        LogicalModelProjectionErrorCode::InvalidPublicReference,
        "unsafe candidate locator");
}

void test_public_safe_decoder_failure_is_rejected() {
    const auto observation = public_observation("decoder");
    auto safe_state_bytes = observation.canonical_safe_state_bytes();
    safe_state_bytes.pop_back();
    require(!ygo::environment::decode_canonical_public_safe_state(safe_state_bytes),
            "malformed public-safe state was accepted");

    auto public_observation_bytes =
        ygo::environment::canonical_public_environment_observation_bytes(observation);
    public_observation_bytes.pop_back();
    ygo::environment::PublicEnvironmentObservationInput decoded;
    require(!ygo::environment::decode_canonical_public_environment_observation(
                public_observation_bytes, decoded),
            "malformed public observation was accepted");

    ygo::environment::PublicEnvironmentObservationInput invalid_observation;
    require_error(
        ygo::model::project_logical_model_input(
            invalid_observation, {candidate_with_index(1)}),
        LogicalModelProjectionErrorCode::PublicSafeStateDecodeFailure,
        "public safe state decoder failure");
}

void test_locator_table_is_deterministic() {
    auto first = public_observation("locator-a");
    auto second = public_observation("locator-b");
    std::reverse(second.decision_context.referenced_entities.begin(),
                 second.decision_context.referenced_entities.end());

    const auto first_result = ygo::model::project_logical_model_input(
        first, {candidate_with_full_descriptor()});
    const auto second_result = ygo::model::project_logical_model_input(
        second, {candidate_with_full_descriptor()});
    const auto& left = require_value(first_result, "first locator table");
    const auto& right = require_value(second_result, "second locator table");

    require(left.public_locator_table.size() == right.public_locator_table.size(),
            "locator table size changed with source reference order");
    for (std::size_t index = 0; index < left.public_locator_table.size(); ++index) {
        require(left.public_locator_table[index].value ==
                    right.public_locator_table[index].value &&
                    left.public_locator_table[index].public_locator_ordinal ==
                        right.public_locator_table[index].public_locator_ordinal,
                "locator table was not deterministic");
        if (index > 0) {
            require(left.public_locator_table[index - 1].value <
                        left.public_locator_table[index].value,
                    "locator table was not sorted by public token");
        }
    }
    require(left.public_locator_table.size() == 2,
            "unexpected public locator token set");
    require(left.public_locator_table[0].value == kCurrentLocator &&
                left.public_locator_table[0].public_locator_ordinal == 0 &&
                left.public_locator_table[1].value == kHistoricalHiddenLocator &&
                left.public_locator_table[1].public_locator_ordinal == 1,
            "public locator ordinals do not follow deterministic token order");
}

void test_historical_event_is_not_rebound_to_current_entity() {
    const auto observation = public_observation("historical");
    const auto candidate = candidate_with_full_descriptor();
    const auto result = ygo::model::project_logical_model_input(observation, {candidate});
    const auto& projected = require_value(result, "historical event");

    require(projected.public_safe_state.entities.size() == 2,
            "historical fixture lost a current or redacted entity");
    const auto& current = projected.public_safe_state.entities[0];
    require(current.card.locator.value == kCurrentLocator &&
                current.current_entity_ordinal == 0,
            "current entity ordinal was not derived from exact current resolution");

    require(projected.public_safe_state.visible_events.size() == 1,
            "historical fixture lost its visible event");
    const auto& event = projected.public_safe_state.visible_events[0];
    require(event.entity.has_value() &&
                event.entity->locator.value == kCurrentLocator &&
                event.entity->locator.public_locator_ordinal ==
                    current.public_locator_ordinal,
            "historical event did not retain token equality");
    require(projected.candidate_features[0].source_reference.has_value() &&
                projected.candidate_features[0].source_reference->reference
                        .current_entity_ordinal == 0,
            "current candidate reference did not receive exact current ordinal");
    require(event.public_passcode.has_value() && *event.public_passcode == 1001,
            "logical event passcode was not preserved exactly");
}

void test_private_decision_and_continuation_metadata_are_ignored() {
    const auto observation_a = public_observation("world-a");
    const auto observation_b = public_observation("world-b");
    const auto candidates = std::vector<EnvironmentActionCandidate>{
        candidate_with_full_descriptor(), candidate_with_index(8)};

    const auto public_bytes_a =
        ygo::environment::canonical_public_environment_observation_bytes(observation_a);
    const auto public_bytes_b =
        ygo::environment::canonical_public_environment_observation_bytes(observation_b);
    require(public_bytes_a == public_bytes_b,
            "paired fixture did not produce equal public observations");

    const auto result_a =
        ygo::model::project_logical_model_input(observation_a, candidates);
    const auto result_b =
        ygo::model::project_logical_model_input(observation_b, candidates);
    const auto& left = require_value(result_a, "paired world A");
    const auto& right = require_value(result_b, "paired world B");

    require(left.public_observation_digest == right.public_observation_digest,
            "paired worlds changed logical observation digest");
    require(left.public_candidate_domain_digest == right.public_candidate_domain_digest,
            "paired worlds changed logical candidate digest");
    require(left.public_locator_table.size() == right.public_locator_table.size() &&
                left.candidate_features.size() == right.candidate_features.size() &&
                left.candidate_routing.size() == right.candidate_routing.size(),
            "paired worlds changed logical cardinality");
    for (std::size_t index = 0; index < left.public_locator_table.size(); ++index) {
        require(left.public_locator_table[index].value ==
                    right.public_locator_table[index].value &&
                    left.public_locator_table[index].public_locator_ordinal ==
                        right.public_locator_table[index].public_locator_ordinal,
                "paired worlds changed locator tokens");
    }
    for (std::size_t index = 0; index < left.candidate_routing.size(); ++index) {
        require(left.candidate_routing[index].public_action_key ==
                    right.candidate_routing[index].public_action_key &&
                    left.candidate_features[index].source_index ==
                        right.candidate_features[index].source_index,
                "paired worlds changed logical candidate values");
    }
    require(left.public_safe_state.visible_events[0].entity->locator.value ==
                right.public_safe_state.visible_events[0].entity->locator.value &&
                left.public_safe_state.visible_events[0].public_passcode ==
                    right.public_safe_state.visible_events[0].public_passcode,
            "paired worlds changed logical visible event");
}

void test_real_paired_hidden_worlds_have_equal_logical_inputs() {
    constexpr std::uint32_t hidden_code_a = 14821890;
    constexpr std::uint32_t hidden_code_b = 7654321;

    auto environment = make_real_paired_environment();
    const auto request_a = real_atomic_hidden_request(hidden_code_a);
    const auto request_b = real_atomic_hidden_request(hidden_code_b);
    require(request_a.candidates.size() == 1 && request_b.candidates.size() == 1,
            "real paired-world fixture did not create one internal candidate per world");
    require(request_a.candidates.front().source_card == hidden_code_a &&
                request_b.candidates.front().source_card == hidden_code_b &&
                request_a.candidates.front().source_card !=
                    request_b.candidates.front().source_card,
            "real paired worlds did not differ in hidden internal card identity");
    require(request_a.candidates.front().semantic_key !=
                request_b.candidates.front().semantic_key,
            "real paired worlds did not differ in internal semantic key");

    auto observation_a = real_observation_for_request(request_a, hidden_code_a);
    auto observation_b = real_observation_for_request(request_b, hidden_code_b);
    require(ygo::observation::canonical_serialize(observation_a) !=
                ygo::observation::canonical_serialize(observation_b),
            "real paired worlds did not differ before public projection");

    const auto frame_a = ygo::environment::detail::EpisodicEnvironmentTestAccess::
        project_frame_for_test(*environment, request_a, observation_a, std::string(64, 'a'), 7);
    const auto frame_b = ygo::environment::detail::EpisodicEnvironmentTestAccess::
        project_frame_for_test(*environment, request_b, observation_b, std::string(64, 'a'), 7);

    require(ygo::environment::canonical_public_environment_observation_bytes(
                frame_a.public_observation) ==
                ygo::environment::canonical_public_environment_observation_bytes(
                    frame_b.public_observation),
            "real paired worlds did not produce equal public observations");
    require(frame_a.public_observation_digest == frame_b.public_observation_digest,
            "real paired public observation digests differ");
    require_same_public_candidate_domain(frame_a, frame_b);
    require(frame_a.request.candidates.front().source_reference.has_value() &&
                frame_a.request.candidates.front().source_reference->kind ==
                    PublicCardReferenceKind::RedactedSlot,
            "real paired hidden candidate was not redacted by the public facade");

    // These are the only inputs passed to the model layer. In particular, the
    // internal request, PlayerObservation, and any continuation view stay in
    // this test's fixture setup and never cross the model boundary.
    const auto result_a = ygo::model::project_logical_model_input_v1(
        frame_a.public_observation, frame_a.request.candidates);
    const auto result_b = ygo::model::project_logical_model_input_v1(
        frame_b.public_observation, frame_b.request.candidates);
    const auto& left = require_value(result_a, "real paired logical world A");
    const auto& right = require_value(result_b, "real paired logical world B");

    require(same_logical_input(left, right),
            "real paired worlds produced different logical model inputs");
    require(same_logical_state(left.public_safe_state, right.public_safe_state),
            "real paired public-safe logical states differ");
    require(left.candidate_count() == frame_a.request.candidates.size() &&
                right.candidate_count() == frame_b.request.candidates.size() &&
                left.candidate_count() == right.candidate_count(),
            "real paired logical candidate count changed");
    for (std::size_t index = 0; index < left.candidate_count(); ++index) {
        require(left.candidate_routing[index].public_action_key ==
                    frame_a.request.candidates[index].public_action_key &&
                    right.candidate_routing[index].public_action_key ==
                        frame_b.request.candidates[index].public_action_key &&
                    left.candidate_routing[index].public_action_key ==
                        right.candidate_routing[index].public_action_key,
                "real paired logical candidate routing key/order changed");
    }
    require(same_logical_visible_events(left.public_safe_state.visible_events,
                                        right.public_safe_state.visible_events),
            "real paired historical event representations differ");
    require(left.public_safe_state.visible_events.size() == 1 &&
                left.public_safe_state.visible_events.front().entity.has_value() &&
                right.public_safe_state.visible_events.front().entity.has_value(),
            "real paired fixture did not retain its historical event");

    const auto public_bytes =
        ygo::environment::canonical_public_environment_observation_bytes(
            frame_a.public_observation);
    const std::string public_text(public_bytes.begin(), public_bytes.end());
    const auto contains_hidden_code = [](const std::string& text,
                                         const std::uint32_t code) {
        return text.find(std::to_string(code)) != std::string::npos;
    };
    require(!contains_hidden_code(public_text, hidden_code_a) &&
                !contains_hidden_code(public_text, hidden_code_b),
            "real paired public facade leaked a hidden passcode");
    require(!contains_hidden_code(logical_string_fields(left), hidden_code_a) &&
                !contains_hidden_code(logical_string_fields(left), hidden_code_b),
            "real paired logical output leaked a hidden passcode");
    for (const auto& entity : left.public_safe_state.entities) {
        if (!entity.card.identity_known) {
            require(!entity.card.passcode.has_value(),
                    "real paired logical redacted entity acquired a passcode");
        }
    }

    auto malformed = frame_a.request.candidates.front();
    malformed.public_action_key = "malformed-hidden." + std::to_string(hidden_code_a);
    const auto rejected = ygo::model::project_logical_model_input_v1(
        frame_a.public_observation, {malformed});
    require_error(rejected, LogicalModelProjectionErrorCode::InvalidPublicActionKey,
                  "real paired hidden diagnostic boundary");
    require(!contains_hidden_code(rejected.error->diagnostic, hidden_code_a) &&
                !contains_hidden_code(rejected.error->diagnostic, hidden_code_b),
            "real paired projection diagnostic leaked a hidden passcode");
}

void test_candidate_boundaries_and_missing_context_digest() {
    const auto observation = public_observation("boundaries");
    for (const std::uint32_t count : {24U, 25U, 129U}) {
        std::vector<EnvironmentActionCandidate> candidates;
        candidates.reserve(count);
        for (std::uint32_t index = 0; index < count; ++index) {
            candidates.push_back(candidate_with_index(index));
        }

        const auto result =
            ygo::model::project_logical_model_input(observation, candidates);
        const auto& projected = require_value(result, "candidate boundary");
        require(projected.candidate_features.size() == count,
                "candidate boundary changed N");
        require(projected.candidate_routing.size() == count,
                "candidate boundary changed routing N");
        for (std::uint32_t index = 0; index < count; ++index) {
            require(projected.candidate_features[index].source_index == index &&
                        projected.candidate_routing[index].public_action_key ==
                            candidates[index].public_action_key,
                    "candidate boundary changed order or key");
        }
    }

    auto no_request_kind = observation;
    no_request_kind.decision_context.kind.reset();
    const auto result = ygo::model::project_logical_model_input(
        no_request_kind, {candidate_with_index(1)});
    const auto& projected = require_value(result, "missing request kind");
    require(!projected.public_candidate_domain_digest.has_value(),
            "candidate-domain digest was fabricated without public request kind");
}

void test_no_private_identity_in_logical_values() {
    const auto observation = public_observation("private-values");
    const auto source = candidate_with_full_descriptor();
    const auto result = ygo::model::project_logical_model_input(observation, {source});
    const auto& projected = require_value(result, "private value boundary");

    require(projected.public_observation_digest.find("private-") == std::string::npos,
            "private observation context reached public digest text");
    require(projected.candidate_routing[0].public_action_key.find("private-") ==
                std::string::npos,
            "private marker reached public routing key");
    require(projected.public_safe_state.visible_events[0].entity->locator.value ==
                kCurrentLocator,
            "logical output did not retain the safe public event token");
}

}  // namespace

int main() {
    try {
        test_full_public_descriptor_is_copied();
        test_public_state_fields_are_copied();
        test_candidate_order_and_value_ownership();
        test_duplicate_malformed_and_unsafe_candidates_fail_closed();
        test_public_safe_decoder_failure_is_rejected();
        test_locator_table_is_deterministic();
        test_historical_event_is_not_rebound_to_current_entity();
        test_private_decision_and_continuation_metadata_are_ignored();
        test_real_paired_hidden_worlds_have_equal_logical_inputs();
        test_candidate_boundaries_and_missing_context_digest();
        test_no_private_identity_in_logical_values();
        std::cout << "logical_model_input_tests=passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
