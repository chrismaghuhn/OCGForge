#include <cstdint>
#include <iomanip>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "ygo/environment/public_action_identity.hpp"
#include "ygo/model/card_vocabulary.hpp"
#include "ygo/model/encoded_model_input.hpp"
#include "ygo/model/logical_model_input.hpp"
#include "ygo/model/model_batch_layout.hpp"
#include "ygo/phase6/task7_input_materialization.hpp"

namespace {

void require(const bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

ygo::model::LogicalModelInputV1 make_logical(const std::size_t count,
                                             const bool rich) {
    ygo::model::LogicalModelInputV1 logical;
    logical.public_observation_digest = std::string(64, 'a');
    if (rich) logical.public_locator_table = {{"a", 0}, {"b", 1}};
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
        logical.candidate_features.push_back(candidate);
    }
    return logical;
}

struct Fixture final {
    ygo::model::LogicalModelInputV1 logical;
    ygo::model::CardVocabularyV1 vocabulary;
    ygo::model::EncodedModelInputV1 encoded;
    ygo::model::RaggedModelBatchV1 ragged;
};

Fixture make_fixture(const std::size_t count, const bool rich) {
    auto logical = make_logical(count, rich);
    const auto vocabulary_result =
        ygo::model::CardVocabularyV1::from_ascending_passcodes(
            rich ? std::vector<std::uint32_t>{123456}
                 : std::vector<std::uint32_t>{});
    require(vocabulary_result && vocabulary_result.value.has_value(),
            "probe vocabulary construction failed");
    auto vocabulary = *vocabulary_result.value;
    const auto encoded_result =
        ygo::model::encode_model_input_v1(logical, vocabulary);
    require(encoded_result && encoded_result.value.has_value(),
            "probe encoded input construction failed");
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
        encoded.globals.win_reason = 7;

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
        encoded.zones = {{0, 1, 3, 1, 2, true}, {1, 2, 2, 1, 1, false}};
        encoded.relationships = {{1, {0, 0}, {1, 1}}};
        ygo::model::EncodedChainLink link;
        link.index = 7;
        link.activating_player = 1;
        link.source = ygo::model::EncodedCurrentReference{0, 0};
        link.activation_zone_code = 2;
        link.effect_description = std::numeric_limits<std::uint64_t>::max();
        link.targets = {{1, 1}};
        encoded.chain.length = 7;
        encoded.chain.links = {link};
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
        event.win_reason = 8;
        event.effect_description = 0;
        event.target_public_locator_ordinals = {0, 1};
        encoded.visible_events = {event};
        encoded.match_context.own_decklist_known = true;
        encoded.match_context.opponent_decklist_known = true;
        encoded.match_context.own_deck.known = true;
        encoded.match_context.own_deck.main_deck = {2};
        encoded.match_context.own_deck.extra_deck = {2};
        encoded.match_context.opponent_deck.known = true;
        encoded.match_context.opponent_deck.main_deck = {2};
        encoded.match_context.opponent_deck.extra_deck = {2};
        auto& candidate = encoded.candidate_features.front();
        candidate.source_reference = {0, {1, 1}};
        candidate.target_reference = {1, {0, std::nullopt}};
        candidate.choice = {4, 17, 2};
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
        encoded.routing_keys.front() = ygo::environment::public_action_key(key_input);
        encoded.public_candidate_domain_digest =
            ygo::environment::public_candidate_domain_digest(
                "card_selection", encoded.routing_keys);
    }
    const auto ragged_result =
        ygo::model::make_ragged_model_batch_v1({encoded});
    require(ragged_result && ragged_result.value.has_value(),
            "probe ragged batch construction failed");
    return {std::move(logical), std::move(vocabulary), std::move(encoded),
            std::move(*ragged_result.value)};
}

}  // namespace

int main(int argc, char** argv) {
    try {
        std::size_t count = 1;
        bool rich = false;
        for (int index = 1; index < argc; ++index) {
            const std::string argument = argv[index];
            if (argument == "--rich") {
                rich = true;
            } else {
                count = static_cast<std::size_t>(std::stoull(argument));
            }
        }
        require(count >= 1 && count <= 129, "probe candidate count is invalid");
        auto values = make_fixture(count, rich);
        ygo::phase6::Task7MaterializationSourceBatchV1 source;
        source.ragged = &values.ragged;
        source.samples.push_back({
            &values.logical, &values.encoded, &values.vocabulary,
            ygo::model::model_input_identity(values.logical, values.encoded),
            values.vocabulary.identity()});
        const auto materialized =
            ygo::phase6::materialize_task7_input_v1(source);
        require(materialized && materialized.value.has_value(),
                "probe Task7 materialization failed");
        const auto& sample = materialized.value->samples.front();
        std::cout << "task7_config="
                  << materialized.value->configuration_identity << '\n';
        std::cout << "task7_candidate_count=" << sample.candidate_count << '\n';
        std::cout << "task7_sample_hex=";
        for (const auto byte : sample.canonical_bytes) {
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
