#include "ygo/model/model_batch_layout.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "ygo/environment/public_action_identity.hpp"
#include "ygo/trace/sha256.hpp"

namespace {

using ygo::environment::PublicActionKeyInput;
using ygo::model::EncodedCandidate;
using ygo::model::EncodedChainLink;
using ygo::model::EncodedCurrentReference;
using ygo::model::EncodedEntity;
using ygo::model::EncodedModelInputV1;
using ygo::model::EncodedVisibleEvent;
using ygo::model::ModelBatchPaddingRequestV1;
using ygo::model::RaggedModelBatchV1;

void require(const bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

std::string vocabulary_identity() {
    return std::string(ygo::model::kCardVocabularyIdentityPrefix) +
           std::string(64, '0');
}

EncodedCandidate candidate(const std::uint32_t index,
                            const std::optional<std::int32_t> amount) {
    EncodedCandidate result;
    result.action_kind_code = 5;
    result.source_index = index;
    result.amount = amount;
    return result;
}

std::string candidate_key(const std::uint32_t index,
                          const std::optional<std::int32_t> amount) {
    PublicActionKeyInput input;
    input.action_kind = "card_selection";
    input.source_index = index;
    input.amount = amount;
    return ygo::environment::public_action_key(input);
}

EncodedModelInputV1 sample(const std::uint32_t candidate_count,
                           const std::uint32_t sample_index) {
    EncodedModelInputV1 value;
    value.card_vocabulary_identity = vocabulary_identity();
    value.public_observation_digest =
        std::string(64, static_cast<char>('0' + sample_index));
    value.perspective_player = 0;
    value.decision_index = 100 + sample_index;
    value.public_locator_table = {"p0:MONSTER_ZONE:0"};
    value.public_observation_context_kind_code = std::uint16_t{5};
    value.public_observation_context_player = std::uint8_t{0};
    value.observation_context_reference_ordinals = {0};
    value.globals.duel_flags = sample_index;
    value.globals.life_points = {8000, 7000};
    value.globals.player_to_act = 0;
    value.globals.turn_player = 0;
    value.globals.turn_count = 3;
    value.globals.phase = 2;
    value.globals.chain_length = sample_index == 0 ? 0 : 1;
    value.match_context.perspective_player = 0;
    value.match_context.duel_flags = sample_index;
    value.match_context.own_decklist_known = true;
    value.match_context.opponent_decklist_known = false;
    value.match_context.own_deck.known = true;
    value.match_context.own_deck.main_deck = {2};
    value.match_context.own_deck.extra_deck = {4};
    value.match_context.opponent_deck.known = false;

    EncodedEntity known;
    known.public_locator_ordinal = 0;
    known.identity_known = true;
    known.card_vocabulary_id = 2;
    known.owner = 0;
    known.controller = 0;
    known.zone_code = 3;
    known.sequence = 0;
    known.position_code = 1;
    known.face_up = true;
    value.entities.push_back(known);

    if (sample_index != 0) {
        value.public_locator_table.push_back("p1:SPELL_TRAP_ZONE:0");
        value.observation_context_reference_ordinals.push_back(1);
        EncodedEntity hidden;
        hidden.public_locator_ordinal = 1;
        hidden.identity_known = false;
        hidden.card_vocabulary_id = 1;
        hidden.owner = 1;
        hidden.controller = 1;
        hidden.zone_code = 4;
        hidden.sequence = 0;
        hidden.position_code = 8;
        hidden.face_down = true;
        value.entities.push_back(hidden);
        value.zones.push_back({0, 3, 5, 1, 4, true});
        value.zones.push_back({1, 4, 5, 0, 5, false});
        value.relationships.push_back(
            {2, EncodedCurrentReference{0, 0}, EncodedCurrentReference{1, 1}});
        EncodedChainLink link;
        link.index = 0;
        link.activating_player = 0;
        link.source = EncodedCurrentReference{0, 0};
        link.activation_zone_code = 3;
        link.effect_description = 0x1020304050607080ULL;
        link.targets.push_back(EncodedCurrentReference{1, 1});
        value.chain.links.push_back(link);
        EncodedVisibleEvent event;
        event.event_index = 7;
        event.kind_code = 4;
        event.player = 0;
        event.public_locator_ordinal = 1;
        event.public_card_vocabulary_id = 2;
        event.to_zone_code = 4;
        value.visible_events.push_back(event);
        value.match_context.own_deck.main_deck = {2, 3};
        value.match_context.own_deck.extra_deck = {4, 5};
    } else {
        value.zones.push_back({0, 3, 5, 1, 4, true});
    }

    std::vector<std::string> keys;
    for (std::uint32_t index = 0; index < candidate_count; ++index) {
        const std::optional<std::int32_t> amount =
            index % 2 == 0 ? std::nullopt : std::optional<std::int32_t>{-17};
        value.candidate_features.push_back(candidate(index, amount));
        keys.push_back(candidate_key(index, amount));
        value.routing_keys.push_back(keys.back());
    }
    value.public_candidate_domain_digest =
        ygo::environment::public_candidate_domain_digest("card_selection", keys);
    return value;
}

void require_same_sample(const EncodedModelInputV1& left,
                         const EncodedModelInputV1& right,
                         const std::string& context) {
    require(ygo::model::canonical_encoded_model_input_bytes(left) ==
                ygo::model::canonical_encoded_model_input_bytes(right),
            context + " changed the encoded sample");
}

void require_offsets(const std::vector<std::uint64_t>& offsets,
                     const std::uint64_t flat_length,
                     const std::string& context) {
    require(offsets.size() == 3 && offsets.front() == 0 && offsets.back() == flat_length &&
                offsets[0] <= offsets[1] && offsets[1] <= offsets[2],
            context + " are not B+1 monotonic offsets");
}

void test_ragged_offsets_preserve_all_variable_collections() {
    const std::vector<EncodedModelInputV1> samples = {sample(2, 0), sample(3, 1)};
    const auto result = ygo::model::make_ragged_model_batch_v1(samples);
    require(result && result.value.has_value(), "ragged batch construction failed");
    const auto& batch = *result.value;
    require(batch.batch_size == 2 && batch.samples.size() == 2,
            "ragged batch changed sample count");
    require_offsets(batch.candidate_offsets, 5, "candidate offsets");
    require(batch.candidate_offsets[1] - batch.candidate_offsets[0] == 2 &&
                batch.candidate_offsets[2] - batch.candidate_offsets[1] == 3,
            "candidate offsets did not preserve N_i");
    require_offsets(batch.zone_offsets, 3, "zone offsets");
    require_offsets(batch.entity_offsets, 3, "entity offsets");
    require_offsets(batch.relationship_offsets, 1, "relationship offsets");
    require_offsets(batch.chain_link_offsets, 1, "chain-link offsets");
    require_offsets(batch.visible_event_offsets, 1, "visible-event offsets");
    require_offsets(batch.decision_context_reference_offsets, 3,
                    "decision-context reference offsets");
    require_offsets(batch.public_locator_token_offsets, 3,
                    "locator-token offsets");
    require_offsets(batch.life_point_offsets, 4, "life-point offsets");
    require_offsets(batch.own_deck_passcode_offsets, 3, "own-deck offsets");
    require_offsets(batch.opponent_deck_passcode_offsets, 0, "opponent-deck offsets");
    require_offsets(batch.own_extra_deck_passcode_offsets, 3,
                    "own-extra-deck offsets");
    require_offsets(batch.opponent_extra_deck_passcode_offsets, 0,
                    "opponent-extra-deck offsets");
    require(batch.candidate_rows[0].source_index == 0 &&
                batch.candidate_rows[1].source_index == 1 &&
                batch.candidate_rows[2].source_index == 0 &&
                batch.candidate_routing_keys[2] == samples[1].routing_keys[0],
            "ragged candidate rows or routing keys changed source order");
}

void test_padding_masks_and_optional_presence_are_distinct() {
    const std::vector<EncodedModelInputV1> samples = {sample(2, 0), sample(3, 1)};
    const auto ragged_result = ygo::model::make_ragged_model_batch_v1(samples);
    require(ragged_result && ragged_result.value.has_value(), "padding ragged setup failed");
    ModelBatchPaddingRequestV1 request;
    request.candidate_width = 3;
    const auto padded_result = ygo::model::pad_model_batch_v1(*ragged_result.value, request);
    require(padded_result && padded_result.value.has_value(), "padding construction failed");
    const auto& padded = *padded_result.value;
    require(padded.widths.candidate_width == 3 &&
                padded.candidate_row_mask == std::vector<std::uint8_t>{1, 1, 0, 1, 1, 1},
            "candidate row masks do not mark exactly the real rows");
    require(padded.candidate_routing_keys_padded[2].empty() &&
                padded.candidate_features_padded[2].action_kind_code == 0,
            "padding rows contain routing or feature values");
    require(padded.widths.entity_width == 2 &&
                padded.entities_padded[1].card_vocabulary_id == 0 &&
                padded.entity_row_mask[1] == 0 &&
                std::any_of(padded.entities_padded.begin(), padded.entities_padded.end(),
                            [](const auto& entity) {
                                return entity.card_vocabulary_id == 1;
                            }),
            "automatic entity width used the batch sum or lost real unknown ID 1");
    require(padded.candidate_optional_presence_masks_padded[0].amount == 0 &&
                padded.candidate_row_mask[0] == 1 &&
                padded.candidate_optional_presence_masks_padded[2].amount == 0 &&
                padded.candidate_row_mask[2] == 0,
            "optional presence mask was conflated with row mask");

    ModelBatchPaddingRequestV1 wider_request;
    wider_request.candidate_width = 4;
    const auto wider_result =
        ygo::model::pad_model_batch_v1(*ragged_result.value, wider_request);
    require(wider_result && wider_result.value.has_value() &&
                wider_result.value->widths.candidate_width == 4,
            "candidate width greater than the batch maximum was rejected");
}

void test_pad_unpad_is_lossless_and_capacity_fails_closed() {
    const std::vector<EncodedModelInputV1> samples = {sample(2, 0), sample(3, 1)};
    const auto ragged_result = ygo::model::make_ragged_model_batch_v1(samples);
    require(ragged_result && ragged_result.value.has_value(), "roundtrip ragged setup failed");
    const auto original = *ragged_result.value;
    ModelBatchPaddingRequestV1 request;
    request.candidate_width = 6;
    const auto padded_result = ygo::model::pad_model_batch_v1(original, request);
    require(padded_result && padded_result.value.has_value(), "roundtrip padding failed");
    const auto unpadded_result =
        ygo::model::unpad_model_batch_v1(*padded_result.value);
    require(unpadded_result && unpadded_result.value.has_value(), "roundtrip unpadding failed");
    const auto& unpadded = *unpadded_result.value;
    require(unpadded.candidate_offsets == original.candidate_offsets &&
                unpadded.zone_offsets == original.zone_offsets &&
                unpadded.entity_offsets == original.entity_offsets,
            "roundtrip changed offsets");
    require(unpadded.candidate_rows.size() == original.candidate_rows.size() &&
                unpadded.candidate_routing_keys == original.candidate_routing_keys,
            "roundtrip changed candidate cardinality or routing keys");
    for (std::size_t index = 0; index < original.candidate_rows.size(); ++index) {
        require(unpadded.candidate_rows[index].action_kind_code ==
                    original.candidate_rows[index].action_kind_code &&
                    unpadded.candidate_rows[index].source_index ==
                        original.candidate_rows[index].source_index &&
                    unpadded.candidate_rows[index].amount ==
                        original.candidate_rows[index].amount &&
                    unpadded.candidate_rows[index].submits_engine_response ==
                        original.candidate_rows[index].submits_engine_response,
                "roundtrip changed candidate row values");
    }
    for (std::size_t index = 0; index < samples.size(); ++index) {
        require_same_sample(samples[index],
                            ygo::model::reconstruct_model_batch_sample_v1(unpadded, index),
                            "roundtrip sample " + std::to_string(index));
    }

    ModelBatchPaddingRequestV1 too_small;
    too_small.candidate_width = 2;
    const auto rejected = ygo::model::pad_model_batch_v1(original, too_small);
    require(!rejected && rejected.error.has_value(),
            "undersized candidate capacity was accepted");
    require(rejected.error->code ==
                ygo::model::ModelBatchLayoutErrorCode::CapacityTooSmall,
            "undersized capacity returned the wrong error");
    require(original.candidate_offsets == std::vector<std::uint64_t>{0, 2, 5} &&
                original.candidate_rows.size() == 5,
            "failed padding mutated the source ragged batch");

    auto bad_mask = *padded_result.value;
    bad_mask.candidate_row_mask[0] = 0;
    bad_mask.candidate_features_padded[0] = {};
    bad_mask.candidate_features_padded[0].submits_engine_response = false;
    bad_mask.candidate_routing_keys_padded[0].clear();
    bad_mask.candidate_optional_presence_masks_padded[0] = {};
    const auto bad_mask_result = ygo::model::unpad_model_batch_v1(bad_mask);
    require(!bad_mask_result && bad_mask_result.error.has_value() &&
                bad_mask_result.error->code ==
                    ygo::model::ModelBatchLayoutErrorCode::MaskMismatch,
            "non-contiguous real-row mask was accepted");

    auto bad_padding = *padded_result.value;
    bad_padding.candidate_routing_keys_padded[2] = "not-a-padding-key";
    const auto bad_padding_result = ygo::model::unpad_model_batch_v1(bad_padding);
    require(!bad_padding_result && bad_padding_result.error.has_value() &&
                bad_padding_result.error->code ==
                    ygo::model::ModelBatchLayoutErrorCode::PaddingValueMismatch,
            "non-empty padded routing slot was accepted");

    auto bad_optional_mask = *padded_result.value;
    bad_optional_mask.candidate_optional_presence_masks_padded[0].amount = 1;
    const auto bad_optional_result =
        ygo::model::unpad_model_batch_v1(bad_optional_mask);
    require(!bad_optional_result && bad_optional_result.error.has_value() &&
                bad_optional_result.error->code ==
                    ygo::model::ModelBatchLayoutErrorCode::OptionalPresenceMismatch,
            "optional presence mismatch was accepted");
}

void test_boundaries_and_layout_independence() {
    for (const std::uint32_t count : {24U, 25U, 129U}) {
        const auto source = sample(count, 0);
        const auto ragged_result = ygo::model::make_ragged_model_batch_v1({source});
        require(ragged_result && ragged_result.value.has_value(),
                "boundary ragged construction failed");
        ModelBatchPaddingRequestV1 request;
        request.candidate_width = static_cast<std::uint64_t>(count) + 1;
        const auto padded_result =
            ygo::model::pad_model_batch_v1(*ragged_result.value, request);
        require(padded_result && padded_result.value.has_value(),
                "boundary padding construction failed");
        require(padded_result.value->candidate_row_mask.size() == count + 1 &&
                    std::count(padded_result.value->candidate_row_mask.begin(),
                               padded_result.value->candidate_row_mask.end(), 1) == count,
                "boundary padding changed N");
        const auto unpadded =
            ygo::model::unpad_model_batch_v1(*padded_result.value);
        require(unpadded && unpadded.value.has_value(), "boundary unpadding failed");
        require(unpadded.value->candidate_rows.size() == count,
                "boundary roundtrip changed N");
        if (count == 129) {
            ModelBatchPaddingRequestV1 too_small;
            too_small.candidate_width = 128;
            const auto rejected =
                ygo::model::pad_model_batch_v1(*ragged_result.value, too_small);
            require(!rejected && rejected.error.has_value() &&
                        rejected.error->code ==
                            ygo::model::ModelBatchLayoutErrorCode::CapacityTooSmall,
                    "N=129 accepted a hidden width-128 cap");
        }
    }

    const auto source = sample(3, 1);
    const auto ragged = ygo::model::make_ragged_model_batch_v1({source});
    require(ragged && ragged.value.has_value(), "identity ragged construction failed");
    ModelBatchPaddingRequestV1 first_request;
    first_request.candidate_width = 3;
    ModelBatchPaddingRequestV1 second_request;
    second_request.candidate_width = 8;
    const auto first = ygo::model::pad_model_batch_v1(*ragged.value, first_request);
    const auto second = ygo::model::pad_model_batch_v1(*ragged.value, second_request);
    require(first && second && first.value.has_value() && second.value.has_value(),
            "identity layout variants failed");
    const auto first_unpadded = ygo::model::unpad_model_batch_v1(*first.value);
    const auto second_unpadded = ygo::model::unpad_model_batch_v1(*second.value);
    require(first_unpadded && second_unpadded && first_unpadded.value.has_value() &&
                second_unpadded.value.has_value(),
            "identity layout variants could not be unpadded");
    const auto first_sample =
        ygo::model::reconstruct_model_batch_sample_v1(*first_unpadded.value, 0);
    const auto second_sample =
        ygo::model::reconstruct_model_batch_sample_v1(*second_unpadded.value, 0);
    require_same_sample(first_sample, second_sample,
                        "layout metadata changed semantic encoded bytes");
}

}  // namespace

int main() {
    try {
        test_ragged_offsets_preserve_all_variable_collections();
        test_padding_masks_and_optional_presence_are_distinct();
        test_pad_unpad_is_lossless_and_capacity_fails_closed();
        test_boundaries_and_layout_independence();
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
