#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "ygo/model/encoded_model_input.hpp"

namespace ygo::model {

inline constexpr std::string_view kModelBatchLayoutSchemaId =
    "ocgforge.model_batch_layout.v1";

struct CandidateOptionalPresenceV1 final {
    std::uint8_t choice = 0;
    std::uint8_t source_reference = 0;
    std::uint8_t target_reference = 0;
    std::uint8_t phase = 0;
    std::uint8_t position = 0;
    std::uint8_t source_index = 0;
    std::uint8_t amount = 0;
};

struct ModelBatchSampleHeaderV1 final {
    std::string schema_id = std::string(kEncodedModelInputSchemaId);
    std::string card_vocabulary_identity;
    std::string public_observation_digest;
    std::uint8_t perspective_player = 0;
    std::uint64_t decision_index = 0;
    std::optional<std::uint16_t> public_observation_context_kind_code;
    std::optional<std::uint8_t> public_observation_context_player;
    EncodedGlobals globals;
    std::uint32_t chain_length = 0;
    EncodedMatchContext match_context;
    std::optional<std::string> public_candidate_domain_digest;
};

struct RaggedModelBatchV1 final {
    std::string schema_id = std::string(kModelBatchLayoutSchemaId);
    std::uint32_t batch_size = 0;
    std::vector<ModelBatchSampleHeaderV1> samples;

    std::vector<std::uint64_t> candidate_offsets;
    std::vector<std::uint64_t> zone_offsets;
    std::vector<std::uint64_t> entity_offsets;
    std::vector<std::uint64_t> relationship_offsets;
    std::vector<std::uint64_t> chain_link_offsets;
    std::vector<std::uint64_t> visible_event_offsets;
    std::vector<std::uint64_t> decision_context_reference_offsets;
    std::vector<std::uint64_t> public_locator_token_offsets;
    std::vector<std::uint64_t> life_point_offsets;
    std::vector<std::uint64_t> own_deck_passcode_offsets;
    std::vector<std::uint64_t> opponent_deck_passcode_offsets;
    std::vector<std::uint64_t> own_extra_deck_passcode_offsets;
    std::vector<std::uint64_t> opponent_extra_deck_passcode_offsets;

    std::vector<EncodedCandidate> candidate_rows;
    std::vector<CandidateOptionalPresenceV1> candidate_optional_presence_masks;
    std::vector<std::string> candidate_routing_keys;
    std::vector<EncodedZone> zones;
    std::vector<EncodedEntity> entities;
    std::vector<EncodedRelationship> relationships;
    std::vector<EncodedChainLink> chain_links;
    std::vector<EncodedVisibleEvent> visible_events;
    std::vector<std::uint32_t> decision_context_reference_ordinals;
    std::vector<std::string> public_locator_tokens;
    std::vector<std::uint32_t> life_points;
    std::vector<std::uint32_t> own_deck_passcode_ids;
    std::vector<std::uint32_t> opponent_deck_passcode_ids;
    std::vector<std::uint32_t> own_extra_deck_passcode_ids;
    std::vector<std::uint32_t> opponent_extra_deck_passcode_ids;
};

struct ModelBatchPaddingRequestV1 final {
    std::uint64_t candidate_width = 0;
    std::optional<std::uint64_t> zone_width;
    std::optional<std::uint64_t> entity_width;
    std::optional<std::uint64_t> relationship_width;
    std::optional<std::uint64_t> chain_link_width;
    std::optional<std::uint64_t> visible_event_width;
    std::optional<std::uint64_t> decision_context_reference_width;
    std::optional<std::uint64_t> public_locator_token_width;
    std::optional<std::uint64_t> life_point_width;
    std::optional<std::uint64_t> own_deck_passcode_width;
    std::optional<std::uint64_t> opponent_deck_passcode_width;
    std::optional<std::uint64_t> own_extra_deck_passcode_width;
    std::optional<std::uint64_t> opponent_extra_deck_passcode_width;
};

struct ModelBatchPaddingWidthsV1 final {
    std::uint64_t candidate_width = 0;
    std::uint64_t zone_width = 0;
    std::uint64_t entity_width = 0;
    std::uint64_t relationship_width = 0;
    std::uint64_t chain_link_width = 0;
    std::uint64_t visible_event_width = 0;
    std::uint64_t decision_context_reference_width = 0;
    std::uint64_t public_locator_token_width = 0;
    std::uint64_t life_point_width = 0;
    std::uint64_t own_deck_passcode_width = 0;
    std::uint64_t opponent_deck_passcode_width = 0;
    std::uint64_t own_extra_deck_passcode_width = 0;
    std::uint64_t opponent_extra_deck_passcode_width = 0;
};

struct PaddedModelBatchV1 final {
    std::string schema_id = std::string(kModelBatchLayoutSchemaId);
    std::uint32_t batch_size = 0;
    std::vector<ModelBatchSampleHeaderV1> samples;
    ModelBatchPaddingWidthsV1 widths;

    std::vector<EncodedCandidate> candidate_features_padded;
    std::vector<std::uint8_t> candidate_row_mask;
    std::vector<CandidateOptionalPresenceV1> candidate_optional_presence_masks_padded;
    std::vector<std::string> candidate_routing_keys_padded;
    std::vector<EncodedZone> zones_padded;
    std::vector<std::uint8_t> zone_row_mask;
    std::vector<EncodedEntity> entities_padded;
    std::vector<std::uint8_t> entity_row_mask;
    std::vector<EncodedRelationship> relationships_padded;
    std::vector<std::uint8_t> relationship_row_mask;
    std::vector<EncodedChainLink> chain_links_padded;
    std::vector<std::uint8_t> chain_link_row_mask;
    std::vector<EncodedVisibleEvent> visible_events_padded;
    std::vector<std::uint8_t> visible_event_row_mask;
    std::vector<std::uint32_t> decision_context_reference_ordinals_padded;
    std::vector<std::uint8_t> decision_context_reference_row_mask;
    std::vector<std::string> public_locator_tokens_padded;
    std::vector<std::uint8_t> public_locator_token_row_mask;
    std::vector<std::uint32_t> life_points_padded;
    std::vector<std::uint8_t> life_point_row_mask;
    std::vector<std::uint32_t> own_deck_passcode_ids_padded;
    std::vector<std::uint8_t> own_deck_passcode_row_mask;
    std::vector<std::uint32_t> opponent_deck_passcode_ids_padded;
    std::vector<std::uint8_t> opponent_deck_passcode_row_mask;
    std::vector<std::uint32_t> own_extra_deck_passcode_ids_padded;
    std::vector<std::uint8_t> own_extra_deck_passcode_row_mask;
    std::vector<std::uint32_t> opponent_extra_deck_passcode_ids_padded;
    std::vector<std::uint8_t> opponent_extra_deck_passcode_row_mask;
};

struct ModelBatchLayoutV1 final {
    std::string schema_id = std::string(kModelBatchLayoutSchemaId);
    RaggedModelBatchV1 ragged;
    std::optional<PaddedModelBatchV1> padded;
};

enum class ModelBatchLayoutErrorCode : std::uint8_t {
    EmptyBatch,
    InvalidEncodedSample,
    InvalidRaggedLayout,
    CapacityTooSmall,
    InvalidPaddedLayout,
    MaskMismatch,
    PaddingValueMismatch,
    OptionalPresenceMismatch,
    RoundtripMismatch,
    CountOverflow,
    InternalFailure,
};

struct ModelBatchLayoutError final {
    ModelBatchLayoutErrorCode code = ModelBatchLayoutErrorCode::InternalFailure;
    std::string diagnostic;
};

template <typename T>
struct ModelBatchLayoutResult final {
    std::optional<T> value;
    std::optional<ModelBatchLayoutError> error;

    explicit operator bool() const noexcept {
        return value.has_value() && !error.has_value();
    }
};

using RaggedModelBatchResult = ModelBatchLayoutResult<RaggedModelBatchV1>;
using PaddedModelBatchResult = ModelBatchLayoutResult<PaddedModelBatchV1>;

std::string_view model_batch_layout_error_code_name(
    ModelBatchLayoutErrorCode code) noexcept;

RaggedModelBatchResult make_ragged_model_batch_v1(
    const std::vector<EncodedModelInputV1>& samples) noexcept;

PaddedModelBatchResult pad_model_batch_v1(
    const RaggedModelBatchV1& ragged,
    const ModelBatchPaddingRequestV1& request) noexcept;

RaggedModelBatchResult unpad_model_batch_v1(
    const PaddedModelBatchV1& padded) noexcept;

EncodedModelInputV1 reconstruct_model_batch_sample_v1(
    const RaggedModelBatchV1& ragged, std::size_t sample_index);

}  // namespace ygo::model
