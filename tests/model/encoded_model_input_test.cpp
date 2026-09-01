#include "ygo/model/encoded_model_input.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "episodic_environment_test_access.hpp"
#include "ygo/environment/episodic_environment.hpp"
#include "ygo/environment/public_action_identity.hpp"
#include "ygo/environment/public_environment_observation.hpp"
#include "ygo/observation/decision_integration.hpp"
#include "ygo/observation/player_observation.hpp"
#include "ygo/observation/serialization.hpp"
#include "ygo/protocol/continuation.hpp"
#include "ygo/trace/sha256.hpp"

namespace {

using ygo::environment::EnvironmentActionCandidate;
using ygo::environment::EnvironmentActionKind;
using ygo::environment::CertifiedEnvironmentConfig;
using ygo::environment::DecisionFrame;
using ygo::environment::EpisodicEnvironment;
using ygo::environment::PublicActionKeyInput;
using ygo::environment::PublicEnvironmentObservation;
using ygo::model::CardVocabularyV1;
using ygo::model::EncodedModelInputErrorCode;
using ygo::model::EncodedModelInputV1;
using ygo::model::EncodedChainLink;
using ygo::model::EncodedCurrentReference;
using ygo::model::LogicalModelInputV1;
using ygo::observation::ObservedCard;
using ygo::observation::PlayerObservation;
using ygo::protocol::ActionCandidate;
using ygo::protocol::DecisionRequest;

constexpr std::string_view kKnownLocator = "p0:MONSTER_ZONE:0";
constexpr std::string_view kRedactedLocator = "p1:SPELL_TRAP_ZONE:0";

void require(const bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

std::unique_ptr<EpisodicEnvironment> make_real_paired_environment() {
    auto factory = EpisodicEnvironment::create(CertifiedEnvironmentConfig::canonical());
    require(std::holds_alternative<std::unique_ptr<EpisodicEnvironment>>(factory),
            "encoded paired-world fixture could not create the canonical environment");
    return std::move(std::get<std::unique_ptr<EpisodicEnvironment>>(factory));
}

PlayerObservation real_hidden_observation(const std::uint8_t perspective,
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

PlayerObservation real_observation_for_request(const DecisionRequest& request) {
    auto observation = real_hidden_observation(request.player, request.engine_step_index);
    ygo::observation::attach_decision_context(observation, request);
    return observation;
}

class Reader final {
public:
    explicit Reader(const std::vector<std::uint8_t>& bytes) : bytes_(bytes) {}

    bool u8(std::uint8_t& value) {
        if (offset_ >= bytes_.size()) return false;
        value = bytes_[offset_++];
        return true;
    }

    bool u32(std::uint32_t& value) {
        if (bytes_.size() - offset_ < 4) return false;
        value = (static_cast<std::uint32_t>(bytes_[offset_]) << 24) |
                (static_cast<std::uint32_t>(bytes_[offset_ + 1]) << 16) |
                (static_cast<std::uint32_t>(bytes_[offset_ + 2]) << 8) |
                static_cast<std::uint32_t>(bytes_[offset_ + 3]);
        offset_ += 4;
        return true;
    }

    bool u16(std::uint16_t& value) {
        if (bytes_.size() - offset_ < 2) return false;
        value = static_cast<std::uint16_t>(
            (static_cast<std::uint16_t>(bytes_[offset_]) << 8) | bytes_[offset_ + 1]);
        offset_ += 2;
        return true;
    }

    bool u64(std::uint64_t& value) {
        if (bytes_.size() - offset_ < 8) return false;
        value = 0;
        for (int shift = 56; shift >= 0; shift -= 8) {
            value |= static_cast<std::uint64_t>(bytes_[offset_++]) << shift;
        }
        return true;
    }

    bool string(std::string& value) {
        std::uint32_t length = 0;
        if (!u32(length) || length > bytes_.size() - offset_) return false;
        value.assign(reinterpret_cast<const char*>(bytes_.data() + offset_), length);
        offset_ += length;
        return true;
    }

    bool bytes(std::vector<std::uint8_t>& value) {
        std::uint32_t length = 0;
        if (!u32(length) || length > bytes_.size() - offset_) return false;
        value.assign(bytes_.begin() + static_cast<std::ptrdiff_t>(offset_),
                     bytes_.begin() + static_cast<std::ptrdiff_t>(offset_ + length));
        offset_ += length;
        return true;
    }

    bool at_end() const noexcept { return offset_ == bytes_.size(); }

private:
    const std::vector<std::uint8_t>& bytes_;
    std::size_t offset_ = 0;
};

ObservedCard known_card() {
    ObservedCard card;
    card.locator = {std::string(kKnownLocator)};
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

ObservedCard redacted_card() {
    ObservedCard card;
    card.locator = {std::string(kRedactedLocator)};
    card.identity_known = false;
    card.owner = 1;
    card.controller = 1;
    card.zone = ygo::observation::SemanticZone::SpellTrapZone;
    card.sequence = 0;
    card.position = ygo::observation::Position::FaceDownDefense;
    card.face_down = true;
    return card;
}

PlayerObservation source_observation() {
    PlayerObservation observation;
    observation.perspective_player = 0;
    observation.decision_index = 17;
    observation.engine_step_index = 9001;
    observation.globals.life_points = {8000, 7000};
    observation.globals.player_to_act = 0;
    observation.globals.turn_player = 0;
    observation.globals.turn_count = 3;
    observation.globals.phase = 2;
    observation.zones = {
        {0, ygo::observation::SemanticZone::MonsterZone, 5, 1, 4, true},
        {1, ygo::observation::SemanticZone::SpellTrapZone, 5, 0, 5, false},
    };
    observation.entities = {redacted_card(), known_card()};
    observation.relationships.push_back(
        {ygo::observation::RelationshipKind::Target,
         {std::string(kKnownLocator)}, {std::string(kRedactedLocator)}});
    observation.chain.length = 1;
    ygo::observation::ChainLink chain_link;
    chain_link.index = 0;
    chain_link.activating_player = 0;
    chain_link.source = ygo::observation::ObservationLocator{std::string(kKnownLocator)};
    chain_link.activation_zone = ygo::observation::SemanticZone::MonsterZone;
    chain_link.effect_description = 0x1020304050607080ULL;
    chain_link.targets = {
        ygo::observation::ObservationLocator{std::string(kRedactedLocator)}};
    observation.chain.links.push_back(chain_link);

    ygo::observation::VisibleGameEvent event;
    event.event_index = 7;
    event.engine_step_index = 123456789;
    event.kind = ygo::observation::VisibleEventKind::CardRevealed;
    event.player = 0;
    event.entity = ygo::observation::ObservationLocator{std::string(kKnownLocator)};
    event.public_passcode = 1001;
    event.to_zone = ygo::observation::SemanticZone::MonsterZone;
    observation.visible_events.push_back(event);

    observation.match_context.perspective_player = 0;
    observation.match_context.knowledge.own_decklist_known = true;
    observation.match_context.knowledge.opponent_decklist_known = false;
    observation.match_context.own_deck.known = true;
    observation.match_context.own_deck.main_deck = {1001, 2002, 3003};
    observation.match_context.own_deck.extra_deck = {4004};
    observation.match_context.opponent_deck.known = false;

    observation.decision_context.kind = "card_selection";
    observation.decision_context.player = 0;
    observation.decision_context.referenced_entities = {
        {std::string(kRedactedLocator)}, {std::string(kKnownLocator)}};
    observation.observation_hash = ygo::observation::observation_hash(observation);
    return observation;
}

PlayerObservation historical_only_source_observation() {
    auto observation = source_observation();
    observation.visible_events.front().entity.reset();
    observation.visible_events.front().public_passcode = 5005;
    observation.observation_hash = ygo::observation::observation_hash(observation);
    return observation;
}

PublicEnvironmentObservation public_observation() {
    return ygo::environment::project_public_observation(source_observation());
}

PublicEnvironmentObservation historical_only_public_observation() {
    return ygo::environment::project_public_observation(
        historical_only_source_observation());
}

EnvironmentActionCandidate candidate_with_index(
    const std::uint32_t index,
    const std::optional<std::int32_t> amount = std::nullopt) {
    PublicActionKeyInput key;
    key.action_kind = "card_selection";
    key.source_index = index;
    key.amount = amount;

    EnvironmentActionCandidate candidate;
    candidate.action_kind = EnvironmentActionKind::CardSelection;
    candidate.source_index = index;
    candidate.amount = amount;
    candidate.public_action_key = ygo::environment::public_action_key(key);
    return candidate;
}

EnvironmentActionCandidate candidate_with_full_descriptor() {
    PublicActionKeyInput key;
    key.action_kind = "card_selection";
    key.choice = ygo::environment::PublicChoice{
        ygo::environment::PublicChoiceKind::EffectChoice, 17, std::nullopt};
    key.source_reference = ygo::environment::PublicCardReference{
        ygo::environment::PublicCardReferenceKind::VisibleCard,
        std::string(kKnownLocator)};
    key.target_reference = ygo::environment::PublicCardReference{
        ygo::environment::PublicCardReferenceKind::RedactedSlot,
        std::string(kRedactedLocator)};
    key.phase = 2;
    key.position = 1;
    key.source_index = 3;
    key.amount = -17;
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

LogicalModelInputV1 logical_input(const std::uint32_t count,
                                  const bool reverse = false,
                                  const std::optional<std::int32_t> amount = std::nullopt) {
    std::vector<EnvironmentActionCandidate> candidates;
    candidates.reserve(count);
    if (reverse) {
        for (std::uint32_t index = count; index > 0; --index) {
            candidates.push_back(candidate_with_index(index - 1, amount));
        }
    } else {
        for (std::uint32_t index = 0; index < count; ++index) {
            candidates.push_back(candidate_with_index(index, amount));
        }
    }
    const auto result = ygo::model::project_logical_model_input_v1(
        public_observation(), candidates);
    require(static_cast<bool>(result) && result.value.has_value(),
            "test public fixture did not produce a logical input");
    return std::move(*result.value);
}

CardVocabularyV1 vocabulary_with_all_fixture_cards() {
    const auto result = CardVocabularyV1::from_ascending_passcodes(
        {1001, 2002, 3003, 4004});
    require(static_cast<bool>(result) && result.value.has_value(),
            "fixture vocabulary was rejected");
    return std::move(*result.value);
}

LogicalModelInputV1 logical_input_with_card_properties(const bool printed,
                                                       const bool current) {
    auto source = source_observation();
    auto known = std::find_if(source.entities.begin(), source.entities.end(),
                              [](const ObservedCard& card) {
                                  return card.locator.value == kKnownLocator;
                              });
    require(known != source.entities.end(), "property fixture lost its known card");
    if (!printed) known->printed.reset();
    if (!current) known->current.reset();
    source.observation_hash = ygo::observation::observation_hash(source);
    const auto result = ygo::model::project_logical_model_input_v1(
        ygo::environment::project_public_observation(source),
        {candidate_with_index(1)});
    require(result && result.value.has_value(),
            "property-presence fixture did not produce a logical input");
    return std::move(*result.value);
}

void test_public_cards_events_and_decks_use_vocabulary() {
    const auto logical = logical_input(3);
    const auto vocabulary = vocabulary_with_all_fixture_cards();
    const auto result = ygo::model::encode_model_input_v1(logical, vocabulary);
    require(static_cast<bool>(result) && result.value.has_value(),
            "encoded fixture input was rejected");
    const auto& encoded = *result.value;

    require(encoded.entities.size() == 2,
            "encoded fixture changed the entity count");
    const auto known_entity = std::find_if(
        encoded.entities.begin(), encoded.entities.end(), [&](const auto& entity) {
            return encoded.public_locator_table[entity.public_locator_ordinal] == kKnownLocator;
        });
    const auto redacted_entity = std::find_if(
        encoded.entities.begin(), encoded.entities.end(), [&](const auto& entity) {
            return encoded.public_locator_table[entity.public_locator_ordinal] == kRedactedLocator;
        });
    require(known_entity != encoded.entities.end() &&
                known_entity->card_vocabulary_id == 2 &&
                redacted_entity != encoded.entities.end() &&
                redacted_entity->card_vocabulary_id == 1,
            "entity vocabulary IDs did not preserve redacted/known semantics");
    require(encoded.visible_events.size() == 1 &&
                encoded.visible_events[0].public_card_vocabulary_id == 2,
            "public event passcode was not mapped through the vocabulary");
    require(encoded.match_context.own_deck.main_deck ==
                std::vector<std::uint32_t>{2, 3, 4} &&
                encoded.match_context.own_deck.extra_deck ==
                    std::vector<std::uint32_t>{5},
            "public deck passcodes were not mapped through the vocabulary");
    require(encoded.candidate_features.size() == 3 && encoded.routing_keys.size() == 3,
            "encoded candidate cardinality changed");
    for (std::size_t index = 0; index < encoded.candidate_features.size(); ++index) {
        require(encoded.candidate_features[index].source_index == index &&
                    encoded.routing_keys[index] == logical.candidate_routing[index].public_action_key,
                "encoded candidate order or routing key changed");
    }
}

void test_card_properties_presence_is_preserved() {
    const auto vocabulary = vocabulary_with_all_fixture_cards();
    const auto check = [&](const bool printed, const bool current,
                           const std::string& context) {
        const auto logical = logical_input_with_card_properties(printed, current);
        const auto result = ygo::model::encode_model_input_v1(logical, vocabulary);
        require(result && result.value.has_value(), context + " encoding failed");
        const auto known = std::find_if(
            result.value->entities.begin(), result.value->entities.end(),
            [&](const auto& entity) {
                return result.value->public_locator_table[entity.public_locator_ordinal] ==
                       kKnownLocator;
            });
        require(known != result.value->entities.end(), context + " lost known entity");
        require(known->printed.has_value() == printed &&
                    known->current.has_value() == current,
                context + " changed CardProperties optional presence");
    };

    check(false, false, "both properties absent");
    check(true, false, "only printed properties present");
    check(false, true, "only current properties present");
}

void test_all_public_candidate_fields_map_to_fixed_encoded_values() {
    const auto public_value = public_observation();
    const auto candidate = candidate_with_full_descriptor();
    const auto logical_result = ygo::model::project_logical_model_input_v1(
        public_value, {candidate});
    require(logical_result && logical_result.value.has_value(),
            "full encoded candidate logical fixture was rejected");
    const auto vocabulary = vocabulary_with_all_fixture_cards();
    const auto result = ygo::model::encode_model_input_v1(
        *logical_result.value, vocabulary);
    require(result && result.value.has_value(),
            "full encoded candidate fixture was rejected");
    const auto& encoded = *result.value;
    const auto& actual = encoded.candidate_features.front();
    require(actual.action_kind_code == 5 && actual.choice.has_value() &&
                actual.choice->kind_code == 3 && actual.choice->value == 17 &&
                actual.source_reference.has_value() &&
                actual.source_reference->kind_code == 0 &&
                actual.source_reference->reference.public_locator_ordinal == 0 &&
                actual.source_reference->reference.current_entity_ordinal == 0 &&
                actual.target_reference.has_value() &&
                actual.target_reference->kind_code == 1 &&
                actual.target_reference->reference.public_locator_ordinal == 1 &&
                actual.target_reference->reference.current_entity_ordinal == 1 &&
                actual.phase == 2 && actual.position == 1 && actual.source_index == 3 &&
                actual.amount == -17 && actual.continuation_operation_code == 1 &&
                !actual.submits_engine_response,
            "public candidate fields did not use the frozen encoded values");
    require(encoded.entities.size() == 2 && encoded.relationships.size() == 1 &&
                encoded.chain.links.size() == 1 &&
                encoded.entities[0].zone_code == 3 && encoded.entities[0].position_code == 1 &&
                encoded.visible_events.front().kind_code == 4 &&
                encoded.visible_events.front().to_zone_code == 3 &&
                encoded.relationships.front().kind_code == 2 &&
                encoded.chain.links.front().activation_zone_code == 3,
            "public state categories did not use the frozen encoded values");
}

void test_historical_event_passcode_uses_vocabulary_without_entity_resolution() {
    std::vector<EnvironmentActionCandidate> candidates = {candidate_with_index(1)};
    const auto public_value = historical_only_public_observation();
    const auto logical_result = ygo::model::project_logical_model_input_v1(
        public_value, candidates);
    require(logical_result && logical_result.value.has_value(),
            "historical-only logical fixture was rejected");
    const auto vocabulary_result = CardVocabularyV1::from_ascending_passcodes(
        {1001, 2002, 3003, 4004, 5005});
    require(vocabulary_result && vocabulary_result.value.has_value(),
            "historical-only vocabulary was rejected");
    const auto encoded_result = ygo::model::encode_model_input_v1(
        *logical_result.value, *vocabulary_result.value);
    require(encoded_result && encoded_result.value.has_value(),
            "historical-only encoded fixture was rejected");
    require(encoded_result.value->visible_events.size() == 1 &&
                encoded_result.value->visible_events.front().public_card_vocabulary_id == 6 &&
                !encoded_result.value->visible_events.front().public_locator_ordinal.has_value(),
            "historical event passcode required an incorrect current-entity resolution");

    const auto incomplete_vocabulary = CardVocabularyV1::from_ascending_passcodes(
        {1001, 2002, 3003, 4004});
    require(incomplete_vocabulary && incomplete_vocabulary.value.has_value(),
            "incomplete historical-only vocabulary was rejected");
    const auto rejected = ygo::model::encode_model_input_v1(
        *logical_result.value, *incomplete_vocabulary.value);
    require(!rejected && rejected.error.has_value() &&
                rejected.error->code == EncodedModelInputErrorCode::UnknownPublicPasscode,
            "historical public passcode outside vocabulary was not rejected");
}

void test_logical_codec_contains_the_existing_canonical_safe_state() {
    const auto source = source_observation();
    const auto public_value = ygo::environment::project_public_observation(source);
    const auto result = ygo::model::project_logical_model_input_v1(
        public_value, {candidate_with_index(1)});
    require(result && result.value.has_value(), "logical safe-state fixture was rejected");

    const auto bytes = ygo::model::canonical_logical_model_input_bytes(*result.value);
    Reader reader(bytes);
    std::string domain;
    std::string schema;
    std::string digest;
    std::uint8_t perspective = 0;
    std::uint64_t decision_index = 0;
    std::uint8_t present = 0;
    std::uint32_t count = 0;
    require(reader.string(domain) && reader.string(schema) &&
                domain == "ocgforge.model_logical_input.v1" &&
                schema == "ocgforge.model_logical_input.v1",
            "logical canonical header order changed");
    require(reader.string(digest) && reader.u8(perspective) && reader.u64(decision_index) &&
                perspective == source.perspective_player && decision_index == source.decision_index,
            "logical canonical scalar order changed");
    require(reader.u8(present) && present <= 1, "logical context-kind presence is malformed");
    if (present != 0) {
        std::string kind;
        require(reader.string(kind) && kind == "card_selection",
                "logical context-kind encoding changed");
    }
    require(reader.u8(present) && present <= 1, "logical context-player presence is malformed");
    if (present != 0) {
        std::uint8_t player = 0;
        require(reader.u8(player) && player == 0,
                "logical context-player encoding changed");
    }
    require(reader.u32(count), "logical context-reference count is missing");
    for (std::uint32_t index = 0; index < count; ++index) {
        std::string ignored;
        require(reader.string(ignored), "logical context reference is truncated");
    }
    require(reader.u32(count), "logical locator-table count is missing");
    for (std::uint32_t index = 0; index < count; ++index) {
        std::string ignored;
        require(reader.string(ignored), "logical locator table is truncated");
    }
    std::vector<std::uint8_t> safe_state;
    require(reader.bytes(safe_state), "logical safe-state payload is missing");
    require(safe_state == public_value.canonical_safe_state_bytes(),
            "logical canonical codec did not bind the existing safe-state bytes");
}

void test_encoded_codec_field_order_and_presence_bits() {
    EncodedModelInputV1 encoded;
    encoded.card_vocabulary_identity =
        std::string(ygo::model::kCardVocabularyIdentityPrefix) + std::string(64, '0');
    encoded.public_observation_digest = std::string(64, '1');
    encoded.perspective_player = 1;
    encoded.decision_index = 42;
    encoded.public_locator_table = {"p1:MONSTER_ZONE:0"};
    encoded.public_observation_context_kind_code = std::uint16_t{5};
    encoded.public_observation_context_player = std::uint8_t{1};
    encoded.observation_context_reference_ordinals = {0};
    encoded.match_context.perspective_player = 1;
    const auto key_input = ygo::environment::PublicActionKeyInput{
        "card_selection", std::nullopt,
        ygo::environment::PublicCardReference{
            ygo::environment::PublicCardReferenceKind::RedactedSlot,
            "p1:MONSTER_ZONE:0"},
        std::nullopt, std::nullopt, std::nullopt, std::uint32_t{0},
        std::int32_t{-17}, {}};
    const auto key = ygo::environment::public_action_key(key_input);
    encoded.public_candidate_domain_digest =
        ygo::environment::public_candidate_domain_digest("card_selection", {key});
    encoded.candidate_features.push_back(
        ygo::model::EncodedCandidate{5, std::nullopt,
                                     ygo::model::EncodedCardReference{
                                         1, ygo::model::EncodedCurrentReference{0, std::nullopt}},
                                     std::nullopt,
                                     std::nullopt, std::nullopt, std::uint32_t{0},
                                     std::int32_t{-17}, 0, true});
    encoded.routing_keys = {key};

    const auto bytes = ygo::model::canonical_encoded_model_input_bytes(encoded);
    Reader reader(bytes);
    std::string value;
    require(reader.string(value) && value == "ocgforge.model_encoded_input.v1",
            "encoded identity-domain order changed");
    require(reader.string(value) && value == "ocgforge.model_encoded_input.v1",
            "encoded identity-schema order changed");
    require(reader.string(value) && value == "ocgforge.model_logical_input.v1",
            "encoded logical-schema order changed");
    require(reader.string(value) && value == encoded.card_vocabulary_identity,
            "encoded vocabulary identity order changed");
    require(reader.string(value) && value == encoded.public_observation_digest,
            "encoded observation digest order changed");
    std::uint8_t u8 = 0;
    std::uint16_t u16 = 0;
    std::uint32_t u32 = 0;
    std::uint64_t u64 = 0;
    require(reader.u8(u8) && u8 == 1 && reader.u64(u64) && u64 == 42,
            "encoded top-level scalar order changed");
    require(reader.u32(u32) && u32 == 1 && reader.string(value) &&
                value == "p1:MONSTER_ZONE:0",
            "encoded locator table order changed");
    require(reader.u8(u8) && u8 == 1 && reader.u16(u16) && u16 == 5,
            "encoded request-kind presence/code changed");
    require(reader.u8(u8) && u8 == 1 && reader.u8(u8) && u8 == 1,
            "encoded context-player presence/value changed");
    require(reader.u32(u32) && u32 == 1 && reader.u32(u32) && u32 == 0,
            "encoded context-reference order changed");

    require(reader.u64(u64) && u64 == 0 && reader.u32(u32) && u32 == 0,
            "encoded globals prefix changed");
    for (int optional = 0; optional < 2; ++optional) {
        require(reader.u8(u8) && u8 == 0, "encoded global optional presence changed");
    }
    for (int optional = 0; optional < 2; ++optional) {
        require(reader.u8(u8) && u8 == 0, "encoded global optional integer presence changed");
    }
    require(reader.u32(u32) && u32 == 0, "encoded global chain length order changed");
    for (int optional = 0; optional < 2; ++optional) {
        require(reader.u8(u8) && u8 == 0, "encoded terminal global presence changed");
    }
    require(reader.u8(u8) && u8 == 0, "encoded terminal boolean order changed");
    for (int collection = 0; collection < 6; ++collection) {
        require(reader.u32(u32) && u32 == 0,
                "encoded empty state collection order changed");
    }
    require(reader.u8(u8) && u8 == 1 && reader.u64(u64) && u64 == 0 &&
                reader.u8(u8) && u8 == 1 && reader.u8(u8) && u8 == 0,
            "encoded match-context scalar order changed");
    for (int deck = 0; deck < 2; ++deck) {
        require(reader.u8(u8) && u8 == 0 && reader.u32(u32) && u32 == 0 &&
                    reader.u32(u32) && u32 == 0,
                "encoded empty deck order changed");
    }
    require(reader.u8(u8) && u8 == 1 && reader.string(value) &&
                value == *encoded.public_candidate_domain_digest,
            "encoded candidate digest order changed");
    require(reader.u32(u32) && u32 == 1 && reader.u16(u16) && u16 == 5,
            "encoded candidate row prefix changed");
    require(reader.u8(u8) && u8 == 0 && reader.u8(u8) && u8 == 1 &&
                reader.u8(u8) && u8 == 1 && reader.u32(u32) && u32 == 0 &&
                reader.u8(u8) && u8 == 0 && reader.u8(u8) && u8 == 0,
            "encoded candidate reference presence bits changed");
    require(reader.u8(u8) && u8 == 0 && reader.u8(u8) && u8 == 0,
            "encoded candidate optional field presence changed");
    require(reader.u8(u8) && u8 == 1 && reader.u32(u32) && u32 == 0,
            "encoded source-index presence/value changed");
    require(reader.u8(u8) && u8 == 1 && reader.u32(u32) == true &&
                u32 == static_cast<std::uint32_t>(-17),
            "encoded signed amount bits changed");
    require(reader.u8(u8) && u8 == 0 && reader.u8(u8) && u8 == 1,
            "encoded continuation/submit fields changed");
    require(reader.u32(u32) && u32 == 1 && reader.string(value) && value == key &&
                reader.at_end(),
            "encoded routing sidecar order changed");
}

EncodedModelInputV1 minimal_encoded_input_for_chain(const bool source_present) {
    EncodedModelInputV1 encoded;
    encoded.card_vocabulary_identity =
        std::string(ygo::model::kCardVocabularyIdentityPrefix) + std::string(64, '0');
    encoded.public_observation_digest = std::string(64, '1');
    encoded.perspective_player = 1;
    encoded.decision_index = 42;
    encoded.public_locator_table = {"p1:MONSTER_ZONE:0"};
    encoded.public_observation_context_kind_code = std::uint16_t{5};
    encoded.public_observation_context_player = std::uint8_t{1};
    encoded.observation_context_reference_ordinals = {0};
    encoded.match_context.perspective_player = 1;
    const auto key = ygo::environment::public_action_key(
        ygo::environment::PublicActionKeyInput{
            "card_selection", std::nullopt, std::nullopt, std::nullopt, std::nullopt,
            std::nullopt, std::uint32_t{0}, std::nullopt, {}});
    encoded.public_candidate_domain_digest =
        ygo::environment::public_candidate_domain_digest("card_selection", {key});
    EncodedChainLink link;
    link.index = 0;
    if (source_present) {
        link.source = EncodedCurrentReference{0, std::nullopt};
    }
    encoded.chain.length = 1;
    encoded.chain.links.push_back(std::move(link));
    encoded.candidate_features.push_back(
        ygo::model::EncodedCandidate{5, std::nullopt, std::nullopt, std::nullopt,
                                     std::nullopt, std::nullopt, std::uint32_t{0},
                                     std::nullopt, 0, true});
    encoded.routing_keys = {key};
    return encoded;
}

void assert_minimal_chain_source_encoding(const std::vector<std::uint8_t>& bytes,
                                          const bool expected_present) {
    Reader reader(bytes);
    std::string value;
    std::uint8_t u8 = 0;
    std::uint16_t u16 = 0;
    std::uint32_t u32 = 0;
    std::uint64_t u64 = 0;
    require(reader.string(value) && reader.string(value) && reader.string(value) &&
                reader.string(value) && reader.string(value) && reader.u8(u8) &&
                reader.u64(u64),
            "chain KAT header is truncated");
    require(reader.u32(u32) && u32 == 1 && reader.string(value),
            "chain KAT locator table is malformed");
    require(reader.u8(u8) && u8 == 1 && reader.u16(u16) && u16 == 5 &&
                reader.u8(u8) && u8 == 1 && reader.u8(u8) && u8 == 1 &&
                reader.u32(u32) && u32 == 1 && reader.u32(u32) && u32 == 0,
            "chain KAT context order is malformed");
    require(reader.u64(u64) && reader.u32(u32) && u32 == 0,
            "chain KAT globals are malformed");
    for (int optional = 0; optional < 2; ++optional) {
        require(reader.u8(u8) && u8 == 0, "chain KAT player presence is malformed");
    }
    for (int optional = 0; optional < 2; ++optional) {
        require(reader.u8(u8) && u8 == 0, "chain KAT integer presence is malformed");
    }
    require(reader.u32(u32) && u32 == 0, "chain KAT global chain length is malformed");
    for (int optional = 0; optional < 2; ++optional) {
        require(reader.u8(u8) && u8 == 0, "chain KAT terminal presence is malformed");
    }
    require(reader.u8(u8) && u8 == 0, "chain KAT terminal flag is malformed");
    require(reader.u32(u32) && u32 == 0 && reader.u32(u32) && u32 == 0 &&
                reader.u32(u32) && u32 == 0 && reader.u32(u32) && u32 == 1 &&
                reader.u32(u32) && u32 == 1,
            "chain KAT state collection order is malformed");
    require(reader.u32(u32) && u32 == 0 && reader.u8(u8) && u8 == 0,
            "chain KAT link prefix is malformed");
    require(reader.u8(u8) && (u8 == (expected_present ? 1 : 0)),
            "chain source does not have exactly one presence bit");
    if (expected_present) {
        require(reader.u32(u32) && u32 == 0 && reader.u8(u8) && u8 == 0,
                "chain source payload is malformed");
    }
    require(reader.u8(u8) && u8 == 0 && reader.u8(u8) && u8 == 0 &&
                reader.u32(u32) && u32 == 0,
            "chain source tail order is malformed");
}

void test_chain_source_presence_is_encoded_once() {
    const auto present = minimal_encoded_input_for_chain(true);
    const auto absent = minimal_encoded_input_for_chain(false);
    assert_minimal_chain_source_encoding(
        ygo::model::canonical_encoded_model_input_bytes(present), true);
    assert_minimal_chain_source_encoding(
        ygo::model::canonical_encoded_model_input_bytes(absent), false);
}

void test_encoded_presence_and_signed_integer_representation() {
    const auto logical = logical_input(1, false, -17);
    const auto vocabulary = vocabulary_with_all_fixture_cards();
    const auto result = ygo::model::encode_model_input_v1(logical, vocabulary);
    require(result && result.value.has_value(), "presence fixture encoding failed");
    const auto& candidate = result.value->candidate_features.front();
    require(candidate.action_kind_code == 5 && candidate.source_index == 0 &&
                candidate.amount == -17 && candidate.continuation_operation_code == 0 &&
                !candidate.choice.has_value() && !candidate.source_reference.has_value() &&
                !candidate.target_reference.has_value(),
            "encoded optional or signed candidate fields changed representation");
}

void test_malformed_logical_and_encoded_values_fail_closed() {
    const auto vocabulary = vocabulary_with_all_fixture_cards();
    auto malformed_logical = logical_input(1);
    malformed_logical.schema_id = "ocgforge.model_logical_input.v0";
    const auto logical_result = ygo::model::encode_model_input_v1(
        malformed_logical, vocabulary);
    require(!logical_result && logical_result.error.has_value() &&
                logical_result.error->code ==
                    ygo::model::EncodedModelInputErrorCode::InvalidLogicalModelInput,
            "malformed logical schema was accepted");

    auto encoded = ygo::model::encode_model_input_v1(logical_input(1), vocabulary);
    require(encoded && encoded.value.has_value(), "malformed encoded fixture failed to build");
    auto malformed_encoded = *encoded.value;
    malformed_encoded.public_observation_context_kind_code = std::uint16_t{0};
    bool threw = false;
    try {
        (void)ygo::model::canonical_encoded_model_input_bytes(malformed_encoded);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    require(threw, "invalid encoded category was not rejected");

    malformed_encoded = *encoded.value;
    malformed_encoded.routing_keys.push_back(malformed_encoded.routing_keys.front());
    threw = false;
    try {
        (void)ygo::model::canonical_encoded_model_input_bytes(malformed_encoded);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    require(threw, "encoded routing cardinality mismatch was not rejected");

    auto mismatched_row = *encoded.value;
    mismatched_row.candidate_features.front().source_index = 7;
    threw = false;
    try {
        (void)ygo::model::canonical_encoded_model_input_bytes(mismatched_row);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    require(threw, "encoded candidate row/key mismatch was not rejected");
}

void test_outcome_u8_values_are_preserved_by_both_canonical_codecs() {
    auto logical = logical_input(1);
    logical.public_safe_state.globals.winner = 3;
    logical.public_safe_state.globals.win_reason = 7;
    logical.public_safe_state.visible_events.front().winner = 5;
    logical.public_safe_state.visible_events.front().win_reason = 9;

    const auto logical_bytes = ygo::model::canonical_logical_model_input_bytes(logical);
    require(!logical_bytes.empty() && logical.public_safe_state.globals.winner == 3 &&
                logical.public_safe_state.globals.win_reason == 7 &&
                logical.public_safe_state.visible_events.front().winner == 5 &&
                logical.public_safe_state.visible_events.front().win_reason == 9,
            "logical canonical codec changed non-player outcome values");

    const auto vocabulary = vocabulary_with_all_fixture_cards();
    const auto encoded_result = ygo::model::encode_model_input_v1(logical, vocabulary);
    require(encoded_result && encoded_result.value.has_value(),
            "encoded codec rejected non-player outcome values");
    const auto& encoded = *encoded_result.value;
    require(encoded.globals.winner == 3 && encoded.globals.win_reason == 7 &&
                encoded.visible_events.front().winner == 5 &&
                encoded.visible_events.front().win_reason == 9,
            "encoded projection changed non-player outcome values");
    require(!ygo::model::canonical_encoded_model_input_bytes(encoded).empty(),
            "encoded canonical codec rejected non-player outcome values");
}

void test_real_paired_hidden_worlds_have_equal_encoded_inputs() {
    constexpr std::uint32_t hidden_code_a = 14821890;
    constexpr std::uint32_t hidden_code_b = 7654321;
    auto environment = make_real_paired_environment();
    const auto request_a = real_atomic_hidden_request(hidden_code_a);
    const auto request_b = real_atomic_hidden_request(hidden_code_b);
    require(request_a.candidates.front().source_card == hidden_code_a &&
                request_b.candidates.front().source_card == hidden_code_b &&
                request_a.candidates.front().semantic_key !=
                    request_b.candidates.front().semantic_key,
            "encoded paired worlds did not differ internally");

    const auto observation_a = real_observation_for_request(request_a);
    const auto observation_b = real_observation_for_request(request_b);
    const auto frame_a = ygo::environment::detail::EpisodicEnvironmentTestAccess::
        project_frame_for_test(*environment, request_a, observation_a, std::string(64, 'a'), 7);
    const auto frame_b = ygo::environment::detail::EpisodicEnvironmentTestAccess::
        project_frame_for_test(*environment, request_b, observation_b, std::string(64, 'a'), 7);
    require(ygo::environment::canonical_public_environment_observation_bytes(
                frame_a.public_observation) ==
                ygo::environment::canonical_public_environment_observation_bytes(
                    frame_b.public_observation),
            "encoded paired worlds changed public observation bytes");
    require(frame_a.request.candidates.size() == 1 && frame_b.request.candidates.size() == 1 &&
                frame_a.request.candidates.front().public_action_key ==
                    frame_b.request.candidates.front().public_action_key,
            "encoded paired worlds changed the public candidate domain");

    const auto logical_a = ygo::model::project_logical_model_input_v1(
        frame_a.public_observation, frame_a.request.candidates);
    const auto logical_b = ygo::model::project_logical_model_input_v1(
        frame_b.public_observation, frame_b.request.candidates);
    require(logical_a && logical_b && logical_a.value.has_value() && logical_b.value.has_value(),
            "encoded paired logical projection failed");
    const auto vocabulary_result = CardVocabularyV1::from_ascending_passcodes({});
    require(vocabulary_result && vocabulary_result.value.has_value(),
            "empty paired-world vocabulary was rejected");
    const auto encoded_a = ygo::model::encode_model_input_v1(
        *logical_a.value, *vocabulary_result.value);
    const auto encoded_b = ygo::model::encode_model_input_v1(
        *logical_b.value, *vocabulary_result.value);
    require(encoded_a && encoded_b && encoded_a.value.has_value() && encoded_b.value.has_value(),
            "encoded paired projection failed");
    require(ygo::model::canonical_encoded_model_input_bytes(*encoded_a.value) ==
                ygo::model::canonical_encoded_model_input_bytes(*encoded_b.value) &&
                ygo::model::model_input_identity(*logical_a.value, *encoded_a.value) ==
                    ygo::model::model_input_identity(*logical_b.value, *encoded_b.value),
            "hidden paired worlds produced different encoded bytes or identity");
    require(encoded_a.value->entities.size() == 1 &&
                encoded_a.value->entities.front().card_vocabulary_id == 1,
            "hidden paired entity did not use the unknown/redacted vocabulary ID");
}

void test_unknown_known_passcode_fails_closed() {
    const auto logical = logical_input(1);
    const auto vocabulary_result = CardVocabularyV1::from_ascending_passcodes({2002, 3003});
    require(static_cast<bool>(vocabulary_result) && vocabulary_result.value.has_value(),
            "reduced vocabulary was rejected");
    const auto result = ygo::model::encode_model_input_v1(logical, *vocabulary_result.value);
    require(!result && !result.value.has_value() && result.error.has_value(),
            "known public passcode outside vocabulary was accepted");
    require(result.error->code == EncodedModelInputErrorCode::UnknownPublicPasscode,
            "unknown public passcode returned the wrong error code");
    require(result.error->diagnostic.find("1001") == std::string::npos,
            "unknown public passcode leaked into diagnostics");
}

void test_canonical_bytes_and_identity_bind_logical_encoded_and_vocabulary() {
    const auto logical = logical_input(2);
    const auto reversed = logical_input(2, true);
    const auto vocabulary = vocabulary_with_all_fixture_cards();
    const auto other_vocabulary_result =
        CardVocabularyV1::from_ascending_passcodes({1001, 2002, 3003, 4004, 5005});
    require(static_cast<bool>(other_vocabulary_result) && other_vocabulary_result.value.has_value(),
            "alternate vocabulary was rejected");

    const auto encoded_result = ygo::model::encode_model_input_v1(logical, vocabulary);
    const auto reversed_result = ygo::model::encode_model_input_v1(reversed, vocabulary);
    const auto other_encoded_result =
        ygo::model::encode_model_input_v1(logical, *other_vocabulary_result.value);
    require(encoded_result && reversed_result && other_encoded_result,
            "canonical identity fixture encoding failed");

    const auto logical_bytes = ygo::model::canonical_logical_model_input_bytes(logical);
    const auto encoded_bytes =
        ygo::model::canonical_encoded_model_input_bytes(*encoded_result.value);
    require(logical_bytes == ygo::model::canonical_logical_model_input_bytes(logical),
            "logical canonical bytes were not deterministic");
    require(encoded_bytes ==
                ygo::model::canonical_encoded_model_input_bytes(*encoded_result.value),
            "encoded canonical bytes were not deterministic");

    const auto identity = ygo::model::model_input_identity(logical, *encoded_result.value);
    require(identity.rfind("model_input.v1.", 0) == 0 && identity.size() == 79,
            "model-input identity has the wrong schema or digest shape");
    require(identity ==
                "model_input.v1." +
                    ygo::trace::sha256_bytes(
                        ygo::model::canonical_model_input_identity_bytes(
                            logical, *encoded_result.value)),
            "model-input identity did not hash the canonical identity bytes");
    require(identity != ygo::model::model_input_identity(reversed, *reversed_result.value),
            "candidate order did not change model-input identity");
    require(identity != ygo::model::model_input_identity(
                         logical, *other_encoded_result.value),
            "vocabulary identity did not change model-input identity");
}

void test_candidate_boundaries_remain_exactly_n_to_n() {
    const auto vocabulary = vocabulary_with_all_fixture_cards();
    for (const std::uint32_t count : {24U, 25U, 129U}) {
        const auto logical = logical_input(count);
        const auto result = ygo::model::encode_model_input_v1(logical, vocabulary);
        require(result && result.value.has_value(), "candidate boundary encoding failed");
        const auto& encoded = *result.value;
        require(encoded.candidate_features.size() == count &&
                    encoded.routing_keys.size() == count,
                "candidate boundary was filtered or capped");
        for (std::uint32_t index = 0; index < count; ++index) {
            require(encoded.candidate_features[index].source_index == index &&
                        encoded.routing_keys[index] == logical.candidate_routing[index].public_action_key,
                    "candidate boundary changed order or routing identity");
        }
    }
}

}  // namespace

int main() {
    try {
        test_public_cards_events_and_decks_use_vocabulary();
        test_card_properties_presence_is_preserved();
        test_all_public_candidate_fields_map_to_fixed_encoded_values();
        test_historical_event_passcode_uses_vocabulary_without_entity_resolution();
        test_logical_codec_contains_the_existing_canonical_safe_state();
        test_encoded_codec_field_order_and_presence_bits();
        test_chain_source_presence_is_encoded_once();
        test_encoded_presence_and_signed_integer_representation();
        test_malformed_logical_and_encoded_values_fail_closed();
        test_outcome_u8_values_are_preserved_by_both_canonical_codecs();
        test_real_paired_hidden_worlds_have_equal_encoded_inputs();
        test_unknown_known_passcode_fails_closed();
        test_canonical_bytes_and_identity_bind_logical_encoded_and_vocabulary();
        test_candidate_boundaries_remain_exactly_n_to_n();
        std::cout << "encoded_model_input_tests=passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
