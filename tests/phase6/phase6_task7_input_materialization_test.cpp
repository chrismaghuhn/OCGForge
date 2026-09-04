#include "ygo/phase6/task7_input_materialization.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "ygo/environment/public_action_identity.hpp"
#include "ygo/model/card_vocabulary.hpp"
#include "ygo/model/encoded_model_input.hpp"
#include "ygo/model/logical_model_input.hpp"
#include "ygo/model/model_batch_layout.hpp"

namespace {

void require(const bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

struct Fixture final {
    ygo::model::LogicalModelInputV1 logical;
    ygo::model::CardVocabularyV1 vocabulary;
    ygo::model::EncodedModelInputV1 encoded;
    ygo::model::RaggedModelBatchV1 ragged;
};

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
        event.winner = std::nullopt;
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

int main() {
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

        std::cout << "task7_input_materialization=passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
