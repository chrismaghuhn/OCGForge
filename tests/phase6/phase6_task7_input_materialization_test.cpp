#include "ygo/phase6/task7_input_materialization.hpp"

#include <array>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "ygo/environment/public_action_identity.hpp"
#include "episodic_environment_test_access.hpp"
#include "ygo/environment/episodic_environment.hpp"
#include "ygo/model/card_vocabulary.hpp"
#include "ygo/model/encoded_model_input.hpp"
#include "ygo/model/logical_model_input.hpp"
#include "ygo/model/model_batch_layout.hpp"
#include "ygo/observation/serialization.hpp"
#include "ygo/observation/player_observation.hpp"
#include "ygo/observation/decision_integration.hpp"
#include "ygo/protocol/continuation.hpp"

namespace {

std::vector<std::uint8_t> g_paired_canonical_bytes;

void require(const bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

struct Fixture final {
    ygo::model::LogicalModelInputV1 logical;
    ygo::model::CardVocabularyV1 vocabulary;
    ygo::model::EncodedModelInputV1 encoded;
    ygo::model::RaggedModelBatchV1 ragged;
};

ygo::observation::ObservedCard paired_current_card() {
    ygo::observation::ObservedCard card;
    card.locator = {"p0:MONSTER_ZONE:0"};
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
    card.printed->race = 1;
    card.printed->attack = 2500;
    card.printed->defense = 2000;
    card.printed->level = 8;
    card.current.emplace();
    card.current->attack = 2700;
    card.current->defense = 2100;
    card.current->status_flags = 0x8;
    return card;
}

ygo::observation::ObservedCard paired_redacted_card() {
    ygo::observation::ObservedCard card;
    card.locator = {"p1:SPELL_TRAP_ZONE:0"};
    card.identity_known = false;
    card.owner = 1;
    card.controller = 1;
    card.zone = ygo::observation::SemanticZone::SpellTrapZone;
    card.sequence = 0;
    card.position = ygo::observation::Position::FaceDownDefense;
    card.face_down = true;
    return card;
}

ygo::observation::PlayerObservation paired_private_observation(
    const std::string& marker) {
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
    observation.zones = {
        {1, ygo::observation::SemanticZone::SpellTrapZone, 5, 0, 5, false},
        {0, ygo::observation::SemanticZone::MonsterZone, 5, 1, 4, true},
    };
    observation.entities = {paired_redacted_card(), paired_current_card()};
    observation.relationships.push_back(
        {ygo::observation::RelationshipKind::Target,
         {"p0:MONSTER_ZONE:0"}, {"p1:SPELL_TRAP_ZONE:0"}});
    observation.chain.length = 1;
    ygo::observation::ChainLink chain_link;
    chain_link.index = 0;
    chain_link.activating_player = 0;
    chain_link.source = {"p0:MONSTER_ZONE:0"};
    chain_link.activation_zone = ygo::observation::SemanticZone::MonsterZone;
    chain_link.effect_description = 0x1020304050607080ULL;
    chain_link.targets = {{"p1:SPELL_TRAP_ZONE:0"}};
    observation.chain.links.push_back(chain_link);
    ygo::observation::VisibleGameEvent event;
    event.event_index = 7;
    event.engine_step_index = 123456789;
    event.kind = ygo::observation::VisibleEventKind::CardMoved;
    event.player = 0;
    event.entity = {"p0:MONSTER_ZONE:0"};
    event.public_passcode = 1001;
    event.from_zone = ygo::observation::SemanticZone::Hand;
    event.to_zone = ygo::observation::SemanticZone::MonsterZone;
    event.count = 1;
    event.amount = -500;
    event.counter_type = 7;
    event.phase = 2;
    event.effect_description = 0x1020304050607080ULL;
    event.targets = {{"p1:SPELL_TRAP_ZONE:0"}};
    observation.visible_events.push_back(event);
    observation.match_context.perspective_player = 0;
    observation.match_context.duel_flags = observation.globals.duel_flags;
    observation.match_context.knowledge.own_decklist_known = true;
    observation.match_context.knowledge.opponent_decklist_known = false;
    observation.match_context.own_deck.known = true;
    observation.match_context.own_deck.main_deck = {1001, 2002, 3003};
    observation.match_context.own_deck.extra_deck = {4004, 5005};
    observation.decision_context.kind = "card_selection";
    observation.decision_context.player = 0;
    observation.decision_context.decision_id = "private-decision." + marker;
    observation.decision_context.continuation_id = "private-continuation." + marker;
    observation.decision_context.engine_step_index = 9001;
    observation.decision_context.engine_message_type = 15;
    observation.decision_context.engine_message_name = "MSG_SELECT_CARD";
    observation.decision_context.referenced_entities = {
        {"p1:SPELL_TRAP_ZONE:0"}, {"p0:MONSTER_ZONE:0"}};
    observation.observation_hash = ygo::observation::observation_hash(observation);
    return observation;
}

std::vector<ygo::environment::EnvironmentActionCandidate> paired_candidates() {
    ygo::environment::PublicActionKeyInput key;
    key.action_kind = "card_selection";
    key.source_index = 0;
    ygo::environment::EnvironmentActionCandidate candidate;
    candidate.action_kind = ygo::environment::EnvironmentActionKind::CardSelection;
    candidate.source_index = 0;
    candidate.public_action_key = ygo::environment::public_action_key(key);
    return {candidate};
}

std::unique_ptr<ygo::environment::EpisodicEnvironment>
make_real_paired_environment() {
    auto factory = ygo::environment::EpisodicEnvironment::create(
        ygo::environment::CertifiedEnvironmentConfig::canonical());
    require(std::holds_alternative<
                std::unique_ptr<ygo::environment::EpisodicEnvironment>>(factory),
            "real paired-world environment construction failed");
    return std::move(std::get<
        std::unique_ptr<ygo::environment::EpisodicEnvironment>>(factory));
}

ygo::observation::PlayerObservation real_hidden_observation(
    const std::uint8_t perspective, const std::uint32_t hidden_code,
    const std::uint64_t engine_step_index = 91) {
    ygo::observation::PlayerObservation observation;
    observation.perspective_player = perspective;
    observation.engine_step_index = engine_step_index;
    observation.globals.life_points = {8000, 8000};
    observation.match_context.perspective_player = perspective;
    const auto hidden_controller = static_cast<std::uint8_t>(1 - perspective);
    const auto locator = std::string("p") + std::to_string(hidden_controller) +
                         ":SPELL_TRAP_ZONE:0";
    observation.zones.push_back(
        {hidden_controller, ygo::observation::SemanticZone::SpellTrapZone,
         1, 0, 1, false});
    ygo::observation::ObservedCard hidden;
    hidden.locator = {locator};
    hidden.identity_known = false;
    hidden.controller = hidden_controller;
    hidden.zone = ygo::observation::SemanticZone::SpellTrapZone;
    hidden.sequence = 0;
    hidden.face_down = true;
    observation.entities.push_back(std::move(hidden));
    (void)hidden_code;
    observation.observation_hash =
        ygo::observation::observation_hash(observation);
    return observation;
}

ygo::protocol::DecisionRequest real_atomic_hidden_request(
    const std::uint32_t hidden_code) {
    ygo::protocol::DecisionRequest request;
    request.kind = ygo::protocol::DecisionRequestKind::CardSelection;
    request.decision_id = "private-decision.card." + std::to_string(hidden_code);
    request.engine_step_index = 91;
    request.player = 1;
    request.engine_message_type = 15;
    request.engine_message_name = "MSG_SELECT_CARD";
    request.raw_message_hash = "private-raw." + std::to_string(hidden_code);
    ygo::protocol::ActionCandidate candidate;
    candidate.action_kind = ygo::protocol::ActionKind::CardSelection;
    candidate.semantic_key =
        "card.0.3." + std::to_string(hidden_code) + ".0.8.0";
    candidate.source_card = hidden_code;
    candidate.source_controller = 0;
    candidate.source_location = 8;
    candidate.source_sequence = 0;
    candidate.source_index = 3;
    candidate.exact_response_bytes = {3, 0, 0, 0};
    request.candidates.push_back(std::move(candidate));
    return request;
}

ygo::observation::PlayerObservation real_observation_for_request(
    const ygo::protocol::DecisionRequest& request,
    const std::uint32_t hidden_code) {
    auto observation = real_hidden_observation(
        request.player, hidden_code, request.engine_step_index);
    ygo::observation::attach_decision_context(observation, request);
    ygo::observation::VisibleGameEvent historical_event;
    historical_event.event_index = 3;
    historical_event.engine_step_index = 80;
    historical_event.kind = ygo::observation::VisibleEventKind::CardRevealed;
    historical_event.player = 0;
    historical_event.entity =
        ygo::observation::ObservationLocator{"p0:SPELL_TRAP_ZONE:0"};
    historical_event.to_zone = ygo::observation::SemanticZone::SpellTrapZone;
    observation.visible_events.push_back(std::move(historical_event));
    observation.observation_hash =
        ygo::observation::observation_hash(observation);
    return observation;
}

ygo::model::LogicalModelInputV1 make_logical(std::size_t count) {
    ygo::model::LogicalModelInputV1 logical;
    logical.public_observation_digest = std::string(64, 'a');
    logical.public_locator_table = {
        {"a", 0},
        {"b", 1},
    };
    for (std::size_t index = 0; index < count; ++index) {
        ygo::environment::PublicActionKeyInput key_input;
        key_input.action_kind = "card_selection";
        key_input.source_index = static_cast<std::uint32_t>(index);
        logical.candidate_routing.push_back(
            {ygo::environment::public_action_key(key_input)});
        ygo::model::LogicalCandidate candidate;
        candidate.action_kind =
            ygo::environment::EnvironmentActionKind::CardSelection;
        candidate.source_index = static_cast<std::uint32_t>(index);
        logical.candidate_features.push_back(std::move(candidate));
    }
    return logical;
}

Fixture fixture(const std::size_t candidate_count = 1, const bool rich = false) {
    auto logical = make_logical(candidate_count);
    const auto vocabulary_result =
        ygo::model::CardVocabularyV1::from_ascending_passcodes(
            rich ? std::vector<std::uint32_t>{123456} :
                   std::vector<std::uint32_t>{});
    require(vocabulary_result && vocabulary_result.value.has_value(),
            "fixture vocabulary construction failed");
    auto vocabulary = *vocabulary_result.value;
    const auto encoded_result =
        ygo::model::encode_model_input_v1(logical, vocabulary);
    require(encoded_result && encoded_result.value.has_value(),
            "fixture encoded input construction failed");
    auto encoded = *encoded_result.value;

    if (rich) {
        encoded.public_locator_table = {"a", "b"};
        encoded.card_vocabulary_identity = vocabulary.identity();
        encoded.public_observation_context_kind_code = 5;
        encoded.public_observation_context_player = 0;
        encoded.observation_context_reference_ordinals = {0, 1};
        encoded.globals.duel_flags = std::numeric_limits<std::uint64_t>::max();
        encoded.globals.life_points = {8000, 7000};
        encoded.globals.player_to_act = 0;
        encoded.globals.turn_player = 1;
        encoded.globals.turn_count = 42;
        encoded.globals.phase = 3;
        encoded.globals.chain_length = 3;
        encoded.globals.winner = std::nullopt;
        encoded.globals.win_reason = 7;
        encoded.globals.terminal = false;

        ygo::model::EncodedCardProperties properties;
        properties.type = 1;
        properties.attribute = 2;
        properties.race = std::numeric_limits<std::uint64_t>::max();
        properties.attack = std::numeric_limits<std::int32_t>::min();
        properties.defense = -1;
        properties.base_attack = 0;
        properties.base_defense = std::numeric_limits<std::int32_t>::max();
        properties.level = 8;
        properties.rank = 4;
        properties.link_rating = 2;
        properties.link_marker_codes = {1, 3};
        properties.left_scale = 0;
        properties.right_scale = 13;
        properties.status_flags = 0xabcdef01;
        properties.counters = {{2, 1}, {5, 9}};

        ygo::model::EncodedEntity redacted;
        redacted.public_locator_ordinal = 0;
        redacted.identity_known = false;
        redacted.card_vocabulary_id = 1;
        redacted.owner = 0;
        redacted.controller = 0;
        redacted.zone_code = 1;
        redacted.sequence = 0;
        redacted.position_code = 1;
        redacted.face_down = true;

        ygo::model::EncodedEntity known;
        known.public_locator_ordinal = 1;
        known.identity_known = true;
        known.card_vocabulary_id = 2;
        known.owner = 1;
        known.controller = 1;
        known.zone_code = 2;
        known.sequence = 1;
        known.overlay_sequence = 0;
        known.position_code = 4;
        known.face_up = true;
        known.printed = properties;
        known.current = properties;
        encoded.entities = {redacted, known};

        encoded.zones = {
            {0, 1, 3, 1, 2, true},
            {1, 2, 2, 1, 1, false},
        };
        encoded.relationships = {
            {1, {0, 0}, {1, 1}},
        };
        ygo::model::EncodedChainLink link;
        link.index = 7;
        link.activating_player = 1;
        link.source = ygo::model::EncodedCurrentReference{0, 0};
        link.activation_zone_code = 2;
        link.effect_description = std::numeric_limits<std::uint64_t>::max();
        link.targets = {{1, 1}};
        encoded.chain.length = 7;
        ygo::model::EncodedChainLink empty_source;
        empty_source.index = 8;
        encoded.chain.links = {link, empty_source};

        ygo::model::EncodedVisibleEvent event;
        event.event_index = 9;
        event.kind_code = 3;
        event.player = 0;
        event.public_locator_ordinal = 1;
        event.public_card_vocabulary_id = 2;
        event.from_zone_code = 1;
        event.to_zone_code = 2;
        event.count = 1;
        event.amount = -123;
        event.counter_type = 5;
        event.phase = 3;
        event.winner = std::nullopt;
        event.win_reason = 8;
        event.effect_description = 0;
        event.target_public_locator_ordinals = {0, 1};
        ygo::model::EncodedVisibleEvent empty_entity;
        empty_entity.event_index = 10;
        encoded.visible_events = {event, empty_entity};

        encoded.match_context.own_decklist_known = true;
        encoded.match_context.opponent_decklist_known = true;
        encoded.match_context.own_deck.known = true;
        encoded.match_context.own_deck.main_deck = {2};
        encoded.match_context.own_deck.extra_deck = {2};
        encoded.match_context.opponent_deck.known = true;
        encoded.match_context.opponent_deck.main_deck = {2};
        encoded.match_context.opponent_deck.extra_deck = {2};

        auto& candidate = encoded.candidate_features.front();
        candidate.source_reference =
            ygo::model::EncodedCardReference{0, {1, 1}};
        candidate.target_reference =
            ygo::model::EncodedCardReference{1, {0, std::nullopt}};
        candidate.choice = ygo::model::EncodedChoice{4, 17, 2};
        candidate.phase = 2;
        candidate.position = 1;
        candidate.source_index = std::numeric_limits<std::uint32_t>::max();
        candidate.amount = -123;
        candidate.continuation_operation_code = 1;
        ygo::environment::PublicActionKeyInput key_input;
        key_input.action_kind = "card_selection";
        key_input.choice = ygo::environment::PublicChoice{
            ygo::environment::PublicChoiceKind::OptionValue, 17, 2};
        key_input.source_reference =
            ygo::environment::PublicCardReference{
                ygo::environment::PublicCardReferenceKind::VisibleCard, "b"};
        key_input.target_reference =
            ygo::environment::PublicCardReference{
                ygo::environment::PublicCardReferenceKind::RedactedSlot, "a"};
        key_input.phase = 2;
        key_input.position = 1;
        key_input.source_index = std::numeric_limits<std::uint32_t>::max();
        key_input.amount = -123;
        key_input.continuation_operation = "pick";
        encoded.routing_keys.front() =
            ygo::environment::public_action_key(key_input);
        encoded.public_candidate_domain_digest =
            ygo::environment::public_candidate_domain_digest(
                "card_selection", encoded.routing_keys);
    }

    const auto ragged_result =
        ygo::model::make_ragged_model_batch_v1({encoded});
    if (!ragged_result || !ragged_result.value.has_value()) {
        throw std::runtime_error(
            "fixture ragged batch construction failed: " +
            (ragged_result.error.has_value() ? ragged_result.error->diagnostic : "no diagnostic"));
    }
    return {std::move(logical), std::move(vocabulary), std::move(encoded),
            std::move(*ragged_result.value)};
}

ygo::phase6::Task7MaterializationSourceBatchV1 source_for(Fixture& values) {
    ygo::phase6::Task7MaterializationSourceBatchV1 source;
    source.ragged = &values.ragged;
    source.samples.push_back({
        &values.logical, &values.encoded, &values.vocabulary,
        ygo::model::model_input_identity(values.logical, values.encoded),
        values.vocabulary.identity()});
    return source;
}

void test_real_paired_hidden_world_task7_materialization() {
    const auto public_a = ygo::environment::project_public_observation(
        paired_private_observation("world-a"));
    const auto public_b = ygo::environment::project_public_observation(
        paired_private_observation("world-b"));
    require(ygo::environment::canonical_public_environment_observation_bytes(public_a) ==
                ygo::environment::canonical_public_environment_observation_bytes(public_b),
            "paired hidden worlds changed the public observation");

    const auto candidates = paired_candidates();
    const auto logical_a = ygo::model::project_logical_model_input_v1(
        public_a, candidates);
    const auto logical_b = ygo::model::project_logical_model_input_v1(
        public_b, candidates);
    require(logical_a && logical_b && logical_a.value.has_value() &&
                logical_b.value.has_value(),
            "paired hidden logical projection failed");
    require(ygo::model::canonical_logical_model_input_bytes(*logical_a.value) ==
                ygo::model::canonical_logical_model_input_bytes(*logical_b.value),
            "paired hidden worlds changed logical model bytes");

    const auto vocabulary_result =
        ygo::model::CardVocabularyV1::from_ascending_passcodes(
            {1001, 2002, 3003, 4004, 5005});
    require(vocabulary_result && vocabulary_result.value.has_value(),
            "paired hidden vocabulary construction failed");
    auto vocabulary = *vocabulary_result.value;
    const auto encoded_a = ygo::model::encode_model_input_v1(
        *logical_a.value, vocabulary);
    const auto encoded_b = ygo::model::encode_model_input_v1(
        *logical_b.value, vocabulary);
    require(encoded_a && encoded_b && encoded_a.value.has_value() &&
                encoded_b.value.has_value(),
            "paired hidden encoded projection failed");
    require(ygo::model::canonical_encoded_model_input_bytes(*encoded_a.value) ==
                ygo::model::canonical_encoded_model_input_bytes(*encoded_b.value),
            "paired hidden worlds changed encoded model bytes");
    require(ygo::model::model_input_identity(*logical_a.value, *encoded_a.value) ==
                ygo::model::model_input_identity(*logical_b.value, *encoded_b.value),
            "paired hidden worlds changed model-input identity");

    const auto ragged_a = ygo::model::make_ragged_model_batch_v1(
        {*encoded_a.value});
    const auto ragged_b = ygo::model::make_ragged_model_batch_v1(
        {*encoded_b.value});
        require(ragged_a && ragged_b && ragged_a.value.has_value() &&
                ragged_b.value.has_value(),
            "paired hidden ragged projection failed");
    ygo::phase6::Task7MaterializationSourceBatchV1 source_a;
    source_a.ragged = &*ragged_a.value;
    source_a.samples.push_back({
        &*logical_a.value, &*encoded_a.value, &vocabulary,
        ygo::model::model_input_identity(*logical_a.value, *encoded_a.value),
        vocabulary.identity()});
    ygo::phase6::Task7MaterializationSourceBatchV1 source_b;
    source_b.ragged = &*ragged_b.value;
    source_b.samples.push_back({
        &*logical_b.value, &*encoded_b.value, &vocabulary,
        ygo::model::model_input_identity(*logical_b.value, *encoded_b.value),
        vocabulary.identity()});
    const auto materialized_a =
        ygo::phase6::materialize_task7_input_v1(source_a);
    const auto materialized_b =
        ygo::phase6::materialize_task7_input_v1(source_b);
    require(materialized_a && materialized_b && materialized_a.value.has_value() &&
                materialized_b.value.has_value(),
            "paired hidden Task7 materialization failed");
    const auto& sample_a = materialized_a.value->samples.front();
    const auto& sample_b = materialized_b.value->samples.front();
    require(sample_a.canonical_bytes == sample_b.canonical_bytes &&
                sample_a.model_input_identity == sample_b.model_input_identity &&
                sample_a.card_vocabulary_identity == sample_b.card_vocabulary_identity &&
                sample_a.candidate_count == sample_b.candidate_count &&
                encoded_a.value->routing_keys == encoded_b.value->routing_keys &&
                candidates.front().public_action_key ==
                    encoded_a.value->routing_keys.front(),
            "paired hidden worlds changed Task7 source or routing values");
    g_paired_canonical_bytes = sample_a.canonical_bytes;
}

void test_real_engine_paired_hidden_world_task7_materialization() {
    constexpr std::uint32_t hidden_code_a = 14821890;
    constexpr std::uint32_t hidden_code_b = 7654321;
    auto environment = make_real_paired_environment();
    const auto request_a = real_atomic_hidden_request(hidden_code_a);
    const auto request_b = real_atomic_hidden_request(hidden_code_b);
    auto observation_a = real_observation_for_request(request_a, hidden_code_a);
    auto observation_b = real_observation_for_request(request_b, hidden_code_b);
    require(ygo::observation::canonical_serialize(observation_a) !=
                ygo::observation::canonical_serialize(observation_b),
            "real paired hidden worlds did not differ internally");
    const auto frame_a =
        ygo::environment::detail::EpisodicEnvironmentTestAccess::project_frame_for_test(
            *environment, request_a, observation_a, std::string(64, 'a'), 7);
    const auto frame_b =
        ygo::environment::detail::EpisodicEnvironmentTestAccess::project_frame_for_test(
            *environment, request_b, observation_b, std::string(64, 'a'), 7);
    require(ygo::environment::canonical_public_environment_observation_bytes(
                frame_a.public_observation) ==
                ygo::environment::canonical_public_environment_observation_bytes(
                    frame_b.public_observation),
            "real paired hidden worlds changed public observation bytes");
    require(frame_a.request.candidates.size() == 1 &&
                frame_b.request.candidates.size() == 1 &&
                frame_a.request.candidates.front().source_reference.has_value() &&
                frame_a.request.candidates.front().source_reference->kind ==
                    ygo::environment::PublicCardReferenceKind::RedactedSlot &&
                frame_a.request.candidates.front().public_action_key ==
                    frame_b.request.candidates.front().public_action_key,
            "real paired hidden worlds changed public candidate routing");
    const auto logical_a = ygo::model::project_logical_model_input_v1(
        frame_a.public_observation, frame_a.request.candidates);
    const auto logical_b = ygo::model::project_logical_model_input_v1(
        frame_b.public_observation, frame_b.request.candidates);
    require(logical_a && logical_b && logical_a.value.has_value() &&
                logical_b.value.has_value(),
            "real paired hidden logical projection failed");
    const auto vocabulary_result =
        ygo::model::CardVocabularyV1::from_ascending_passcodes({});
    require(vocabulary_result && vocabulary_result.value.has_value(),
            "real paired hidden vocabulary construction failed");
    auto vocabulary = *vocabulary_result.value;
    const auto encoded_a = ygo::model::encode_model_input_v1(
        *logical_a.value, vocabulary);
    const auto encoded_b = ygo::model::encode_model_input_v1(
        *logical_b.value, vocabulary);
    require(encoded_a && encoded_b && encoded_a.value.has_value() &&
                encoded_b.value.has_value(),
            "real paired hidden encoded projection failed");
    require(ygo::model::canonical_logical_model_input_bytes(*logical_a.value) ==
                ygo::model::canonical_logical_model_input_bytes(*logical_b.value) &&
                ygo::model::canonical_encoded_model_input_bytes(*encoded_a.value) ==
                    ygo::model::canonical_encoded_model_input_bytes(*encoded_b.value) &&
                ygo::model::model_input_identity(*logical_a.value, *encoded_a.value) ==
                    ygo::model::model_input_identity(*logical_b.value, *encoded_b.value),
            "real paired hidden worlds changed Phase-5 model inputs");
    const auto ragged_a = ygo::model::make_ragged_model_batch_v1({*encoded_a.value});
    const auto ragged_b = ygo::model::make_ragged_model_batch_v1({*encoded_b.value});
    require(ragged_a && ragged_b && ragged_a.value.has_value() &&
                ragged_b.value.has_value(),
            "real paired hidden ragged construction failed");
    ygo::phase6::Task7MaterializationSourceBatchV1 source_a;
    source_a.ragged = &*ragged_a.value;
    source_a.samples.push_back({
        &*logical_a.value, &*encoded_a.value, &vocabulary,
        ygo::model::model_input_identity(*logical_a.value, *encoded_a.value),
        vocabulary.identity()});
    ygo::phase6::Task7MaterializationSourceBatchV1 source_b;
    source_b.ragged = &*ragged_b.value;
    source_b.samples.push_back({
        &*logical_b.value, &*encoded_b.value, &vocabulary,
        ygo::model::model_input_identity(*logical_b.value, *encoded_b.value),
        vocabulary.identity()});
    const auto materialized_a = ygo::phase6::materialize_task7_input_v1(source_a);
    const auto materialized_b = ygo::phase6::materialize_task7_input_v1(source_b);
    require(materialized_a && materialized_b && materialized_a.value.has_value() &&
                materialized_b.value.has_value(),
            "real paired hidden Task7 materialization failed");
    const auto& sample_a = materialized_a.value->samples.front();
    const auto& sample_b = materialized_b.value->samples.front();
    require(sample_a.canonical_bytes == sample_b.canonical_bytes &&
                sample_a.public_observation_digest == sample_b.public_observation_digest &&
                sample_a.public_candidate_domain_digest ==
                    sample_b.public_candidate_domain_digest &&
                sample_a.candidate_count == sample_b.candidate_count &&
                encoded_a.value->routing_keys == encoded_b.value->routing_keys &&
                frame_a.request.candidates.front().public_action_key ==
                    encoded_a.value->routing_keys.front(),
            "real paired hidden worlds changed Task7 canonical source");
    g_paired_canonical_bytes = sample_a.canonical_bytes;
}

void require_materializes(Fixture& values, const std::size_t expected_count) {
    auto source = source_for(values);
    const auto materialized = ygo::phase6::materialize_task7_input_v1(source);
    require(materialized && materialized.value.has_value(),
            "valid Task7 source was rejected");
    require(materialized.value->samples.size() == 1 &&
                materialized.value->samples.front().candidate_count ==
                    expected_count &&
                !materialized.value->samples.front().canonical_bytes.empty(),
            "Task7 candidate cardinality was not preserved");
}

void require_rejected(
    ygo::phase6::Task7MaterializationSourceBatchV1 source,
    const ygo::phase6::Task7MaterializationErrorCode code,
    const std::string& message) {
    const auto result = ygo::phase6::materialize_task7_input_v1(source);
    require(!result && result.error.has_value() && result.error->code == code,
            message);
}

}  // namespace

int main(int argc, char** argv) {
    if (argc == 2 && std::string_view(argv[1]) == "--paired-probe") {
        try {
            test_real_engine_paired_hidden_world_task7_materialization();
            std::cout << "task7_config="
                      << ygo::phase6::task7_materialization_config_identity() << '\n';
            std::cout << "task7_paired_sample_hex_a=";
            for (const auto byte : g_paired_canonical_bytes) {
                std::cout << std::hex << std::setw(2) << std::setfill('0')
                          << static_cast<unsigned int>(byte);
            }
            std::cout << '\n';
            std::cout << "task7_paired_sample_hex_b=";
            for (const auto byte : g_paired_canonical_bytes) {
                std::cout << std::hex << std::setw(2) << std::setfill('0')
                          << static_cast<unsigned int>(byte);
            }
            std::cout << '\n';
            return 0;
        } catch (const std::exception& error) {
            std::cerr << error.what() << '\n';
            return 1;
        }
    }
    try {
        const auto config_bytes =
            ygo::phase6::canonical_task7_materialization_config_bytes();
        require(config_bytes.size() == 8133,
                "Task7 configuration KAT length changed");
        require(
            ygo::phase6::task7_materialization_config_identity() ==
                "phase6_task7_input_materialization_config.v1.20f394c888e959446fa263c3520f3dd3b1f48b3a23e58373da7153a691ab1e7a",
            "Task7 configuration KAT identity changed");

        require(ygo::phase6::task7_u8_limbs(255) ==
                    std::array<std::uint16_t, 1>{255},
                "u8 limb decomposition is not exact");
        require(ygo::phase6::task7_u8_limbs(0) ==
                    std::array<std::uint16_t, 1>{0},
                "u8 zero limb decomposition is not exact");
        require(ygo::phase6::task7_u16_limbs(65535) ==
                    std::array<std::uint16_t, 1>{65535},
                "u16 limb decomposition is not exact");
        require(ygo::phase6::task7_u16_limbs(0) ==
                    std::array<std::uint16_t, 1>{0},
                "u16 zero limb decomposition is not exact");
        require(ygo::phase6::task7_u32_limbs(0xFFFFFFFE) ==
                    std::array<std::uint16_t, 2>{65535, 65534},
                "u32 collision-regression limbs are not exact");
        require(ygo::phase6::task7_u32_limbs(0) ==
                    std::array<std::uint16_t, 2>{0, 0},
                "u32 zero limb decomposition is not exact");
        require(ygo::phase6::task7_u32_limbs(1) ==
                    std::array<std::uint16_t, 2>{0, 1},
                "u32 one limb decomposition is not exact");
        require(ygo::phase6::task7_u32_limbs(0xFFFFFF) ==
                    std::array<std::uint16_t, 2>{255, 65535},
                "u32 2^24-1 limb decomposition is not exact");
        require(ygo::phase6::task7_u32_limbs(0x1000000) ==
                    std::array<std::uint16_t, 2>{256, 0},
                "u32 2^24 limb decomposition is not exact");
        require(ygo::phase6::task7_u32_limbs(0xFFFFFFFF) ==
                    std::array<std::uint16_t, 2>{65535, 65535},
                "u32 collision-regression limbs are not exact");
        const float old_task4_a =
            static_cast<float>(0xFFFFFFFEu) / static_cast<float>(0xFFFFFFFFu);
        const float old_task4_b =
            static_cast<float>(0xFFFFFFFFu) / static_cast<float>(0xFFFFFFFFu);
        require(old_task4_a == old_task4_b,
                "Task4 collision regression witness changed unexpectedly");
        require(ygo::phase6::task7_u64_limbs(0) ==
                    std::array<std::uint16_t, 4>{0, 0, 0, 0},
                "u64 zero limb decomposition is not exact");
        require(ygo::phase6::task7_u64_limbs(1) ==
                    std::array<std::uint16_t, 4>{0, 0, 0, 1},
                "u64 one limb decomposition is not exact");
        require(ygo::phase6::task7_u64_limbs(0x1000000ULL) ==
                    std::array<std::uint16_t, 4>{0, 0, 256, 0},
                "u64 value above 2^24 is not exact");
        require(ygo::phase6::task7_u64_limbs(0x100000001ULL) ==
                    std::array<std::uint16_t, 4>{0, 1, 0, 1},
                "u64 boundary limb decomposition is not exact");
        require(ygo::phase6::task7_u64_limbs(0xFFFFFFFFULL) ==
                    std::array<std::uint16_t, 4>{0, 0, 65535, 65535},
                "u64 2^32-1 limb decomposition is not exact");
        require(ygo::phase6::task7_u64_limbs(0x100000000ULL) ==
                    std::array<std::uint16_t, 4>{0, 1, 0, 0},
                "u64 2^32 limb decomposition is not exact");
        require(ygo::phase6::task7_u64_limbs(
                    std::numeric_limits<std::uint64_t>::max()) ==
                    std::array<std::uint16_t, 4>{65535, 65535, 65535, 65535},
                "u64 maximum limb decomposition is not exact");
        require(ygo::phase6::task7_i32_limbs(std::numeric_limits<std::int32_t>::min()) ==
                    std::array<std::uint16_t, 2>{32768, 0},
                "i32 minimum bit pattern is not exact");
        require(ygo::phase6::task7_i32_limbs(-1) ==
                    std::array<std::uint16_t, 2>{65535, 65535},
                "i32 -1 bit pattern is not exact");
        require(ygo::phase6::task7_i32_limbs(0) ==
                    std::array<std::uint16_t, 2>{0, 0},
                "i32 zero bit pattern is not exact");
        require(ygo::phase6::task7_i32_limbs(1) ==
                    std::array<std::uint16_t, 2>{0, 1},
                "i32 one bit pattern is not exact");
        require(ygo::phase6::task7_i32_limbs(
                    std::numeric_limits<std::int32_t>::max()) ==
                    std::array<std::uint16_t, 2>{32767, 65535},
                "i32 maximum bit pattern is not exact");

        auto values = fixture();
        require_materializes(values, 1);
        auto rich_values = fixture(1, true);
        require_materializes(rich_values, 1);
        const auto rich_source = source_for(rich_values);
        const auto rich_result =
            ygo::phase6::materialize_task7_input_v1(rich_source);
        require(rich_result.value->samples.front().canonical_bytes.size() >
                    values.ragged.candidate_rows.size(),
                "rich state did not reach canonical materialization");
        require(rich_values.encoded.globals.chain_length !=
                    rich_values.encoded.chain.length,
                "chain-state source fields were accidentally aliased");
        require(!rich_values.encoded.entities[0].printed.has_value() &&
                    rich_values.encoded.entities[1].printed.has_value() &&
                    rich_values.encoded.entities[1].printed->base_attack.has_value() &&
                    *rich_values.encoded.entities[1].printed->base_attack == 0,
                "optional absent and present-zero source values collapsed");
        require(rich_values.encoded.relationships.front().source.current_entity_ordinal ==
                    std::optional<std::uint32_t>(0) &&
                    rich_values.encoded.chain.links.front().source.has_value() &&
                    rich_values.encoded.candidate_features.front().source_reference->kind_code ==
                        0 &&
                    rich_values.encoded.visible_events.front().public_locator_ordinal ==
                        std::optional<std::uint32_t>(1),
                "Phase-5 reference type ownership was not preserved");

        ygo::model::ModelBatchPaddingRequestV1 padding_request;
        padding_request.candidate_width = 3;
        padding_request.zone_width = 3;
        padding_request.entity_width = 3;
        padding_request.relationship_width = 3;
        padding_request.chain_link_width = 3;
        padding_request.visible_event_width = 3;
        padding_request.decision_context_reference_width = 3;
        padding_request.public_locator_token_width = 3;
        padding_request.life_point_width = 3;
        padding_request.own_deck_passcode_width = 3;
        padding_request.opponent_deck_passcode_width = 3;
        padding_request.own_extra_deck_passcode_width = 3;
        padding_request.opponent_extra_deck_passcode_width = 3;
        const auto padded = ygo::model::pad_model_batch_v1(
            rich_values.ragged, padding_request);
        require(padded && padded.value.has_value(),
                "valid padded Task7 source was rejected");
        const auto unpadded =
            ygo::model::unpad_model_batch_v1(*padded.value);
        require(unpadded && unpadded.value.has_value(),
                "valid padded Task7 source did not unpad");
        require(ygo::model::canonical_encoded_model_input_bytes(
                    ygo::model::reconstruct_model_batch_sample_v1(
                        *unpadded.value, 0)) ==
                    ygo::model::canonical_encoded_model_input_bytes(
                        rich_values.encoded),
                "ragged/padded round-trip changed the real sample");

        const auto two_sample_ragged = ygo::model::make_ragged_model_batch_v1(
            {rich_values.encoded, rich_values.encoded});
        require(two_sample_ragged && two_sample_ragged.value.has_value(),
                "two-sample ragged fixture construction failed");
        ygo::phase6::Task7MaterializationSourceBatchV1 two_sample_source;
        two_sample_source.ragged = &*two_sample_ragged.value;
        two_sample_source.samples.push_back({
            &rich_values.logical, &rich_values.encoded, &rich_values.vocabulary,
            ygo::model::model_input_identity(rich_values.logical,
                                             rich_values.encoded),
            rich_values.vocabulary.identity()});
        two_sample_source.samples.push_back(two_sample_source.samples.front());
        const auto two_sample_result =
            ygo::phase6::materialize_task7_input_v1(two_sample_source);
        if (!two_sample_result || !two_sample_result.value.has_value()) {
            throw std::runtime_error(
                "two-sample materialization failed: " +
                (two_sample_result.error.has_value() ?
                     two_sample_result.error->diagnostic : "no diagnostic"));
        }
        require(two_sample_result && two_sample_result.value.has_value() &&
                    two_sample_result.value->samples.size() == 2 &&
                    two_sample_result.value->samples[0].canonical_bytes ==
                        two_sample_result.value->samples[1].canonical_bytes,
                "batch composition changed per-sample canonical bytes");

        for (const std::size_t count : {1U, 24U, 25U, 129U}) {
            auto candidate_values = fixture(count);
            require_materializes(candidate_values, count);
        }

        auto duplicate_looking = fixture(2);
        duplicate_looking.encoded.candidate_features[0].source_index = 0;
        duplicate_looking.encoded.candidate_features[0].amount = 0;
        duplicate_looking.encoded.candidate_features[1].source_index = 0;
        duplicate_looking.encoded.candidate_features[1].amount = 1;
        for (std::size_t index = 0; index < 2; ++index) {
            ygo::environment::PublicActionKeyInput key_input;
            key_input.action_kind = "card_selection";
            key_input.source_index = 0;
            key_input.amount = static_cast<std::int32_t>(index);
            duplicate_looking.encoded.routing_keys[index] =
                ygo::environment::public_action_key(key_input);
        }
        const auto duplicate_ragged = ygo::model::make_ragged_model_batch_v1(
            {duplicate_looking.encoded});
        require(duplicate_ragged && duplicate_ragged.value.has_value(),
                "duplicate-looking candidate fixture construction failed");
        duplicate_looking.ragged = *duplicate_ragged.value;
        require_materializes(duplicate_looking, 2);

        auto reordered = fixture(2);
        std::swap(reordered.ragged.candidate_rows[0], reordered.ragged.candidate_rows[1]);
        std::swap(reordered.ragged.candidate_routing_keys[0],
                  reordered.ragged.candidate_routing_keys[1]);
        std::swap(reordered.ragged.candidate_optional_presence_masks[0],
                  reordered.ragged.candidate_optional_presence_masks[1]);
        require_rejected(source_for(reordered),
                         ygo::phase6::Task7MaterializationErrorCode::RaggedReconstructionMismatch,
                         "candidate reordering was not rejected");

        auto collision_a = fixture();
        auto collision_b = fixture();
        const auto set_collision_source = [](Fixture& values,
                                             const std::uint32_t source_index) {
            values.encoded.candidate_features.front().source_index = source_index;
            ygo::environment::PublicActionKeyInput key_input;
            key_input.action_kind = "card_selection";
            key_input.source_index = source_index;
            values.encoded.routing_keys.front() =
                ygo::environment::public_action_key(key_input);
            const auto ragged = ygo::model::make_ragged_model_batch_v1(
                {values.encoded});
            require(ragged && ragged.value.has_value(),
                    "collision fixture ragged construction failed");
            values.ragged = *ragged.value;
        };
        set_collision_source(collision_a, 0xFFFFFFFEu);
        set_collision_source(collision_b, 0xFFFFFFFFu);
        const auto collision_a_result =
            ygo::phase6::materialize_task7_input_v1(source_for(collision_a));
        const auto collision_b_result =
            ygo::phase6::materialize_task7_input_v1(source_for(collision_b));
        require(collision_a_result && collision_b_result &&
                    collision_a_result.value.has_value() &&
                    collision_b_result.value.has_value() &&
                    collision_a_result.value->samples.front().canonical_bytes !=
                        collision_b_result.value->samples.front().canonical_bytes,
                "Task7 materialized collision witness collapsed");

        auto detached_identity = fixture();
        auto detached_source = source_for(detached_identity);
        detached_source.samples.front().expected_model_input_identity =
            "model_input.v1." + std::string(64, 'b');
        require_rejected(std::move(detached_source),
                         ygo::phase6::Task7MaterializationErrorCode::ModelInputIdentityMismatch,
                         "detached model-input identity was not rejected");

        auto detached_vocabulary = fixture();
        auto vocabulary_source = source_for(detached_vocabulary);
        vocabulary_source.samples.front().expected_card_vocabulary_identity =
            "model_card_vocabulary.v1." + std::string(64, 'b');
        require_rejected(std::move(vocabulary_source),
                         ygo::phase6::Task7MaterializationErrorCode::CardVocabularyMismatch,
                         "detached vocabulary identity was not rejected");

        auto detached_count = fixture();
        auto count_source = source_for(detached_count);
        detached_count.logical.candidate_features.clear();
        detached_count.logical.candidate_routing.clear();
        require_rejected(std::move(count_source),
                         ygo::phase6::Task7MaterializationErrorCode::CandidateCountMismatch,
                         "detached logical/encoded candidate count was not rejected");

        auto invalid_vocabulary = fixture(1, true);
        invalid_vocabulary.encoded.entities[1].card_vocabulary_id = 3;
        require_rejected(source_for(invalid_vocabulary),
                         ygo::phase6::Task7MaterializationErrorCode::CardVocabularyMismatch,
                         "vocabulary mapping mismatch was not rejected");

        auto unknown_schema = fixture();
        unknown_schema.ragged.schema_id = "ocgforge.unknown_batch.v1";
        require_rejected(source_for(unknown_schema),
                         ygo::phase6::Task7MaterializationErrorCode::UnknownSchema,
                         "unknown batch schema was not rejected");

        auto invalid_offsets = fixture();
        invalid_offsets.ragged.candidate_offsets = {0, 2};
        require_rejected(source_for(invalid_offsets),
                         ygo::phase6::Task7MaterializationErrorCode::InvalidOffset,
                         "invalid offsets were not rejected");

        auto overflow_offsets = fixture();
        overflow_offsets.ragged.candidate_offsets = {
            0, static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) + 1, 1};
        require_rejected(source_for(overflow_offsets),
                         ygo::phase6::Task7MaterializationErrorCode::OffsetOverflow,
                         "offset overflow was not rejected");

        auto detached_routing = fixture();
        detached_routing.ragged.candidate_routing_keys.front() =
            detached_routing.ragged.candidate_routing_keys.front() + "00";
        require_rejected(source_for(detached_routing),
                         ygo::phase6::Task7MaterializationErrorCode::RoutingSidecarMismatch,
                         "detached routing sidecar was not rejected");

        auto malformed_presence = fixture();
        malformed_presence.ragged.candidate_optional_presence_masks.front().choice = 2;
        require_rejected(source_for(malformed_presence),
                         ygo::phase6::Task7MaterializationErrorCode::OptionalPresenceMismatch,
                         "malformed presence mask was not rejected");

        auto reconstruction_mismatch = fixture();
        reconstruction_mismatch.ragged.candidate_rows.front().source_index = 9;
        require_rejected(source_for(reconstruction_mismatch),
                         ygo::phase6::Task7MaterializationErrorCode::RaggedReconstructionMismatch,
                         "ragged reconstruction mismatch was not rejected");

        test_real_paired_hidden_world_task7_materialization();
        test_real_engine_paired_hidden_world_task7_materialization();

        std::cout << "task7_input_materialization=passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
