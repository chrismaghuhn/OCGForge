#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "ygo/model/encoded_model_input_v2.hpp"

namespace ygo::model {

inline constexpr std::string_view kModelBatchLayoutV2SchemaId =
    "ocgforge.model_batch_layout.v2";
inline constexpr std::string_view kModelBatchLayoutV2IdentityPrefix =
    "model_batch_layout.v2.";

struct CandidateOptionalPresenceV2 final {
    std::uint8_t choice = 0;
    std::uint8_t source_reference = 0;
    std::uint8_t target_reference = 0;
    std::uint8_t phase = 0;
    std::uint8_t position = 0;
    std::uint8_t source_index = 0;
    std::uint8_t amount = 0;
};

struct ModelBatchSampleHeaderV2 final {
    std::string schema_id = std::string(kEncodedModelInputV2SchemaId);
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

struct RaggedModelBatchV2 final {
    std::string schema_id = std::string(kModelBatchLayoutV2SchemaId);
    std::uint32_t batch_size = 0;
    std::vector<ModelBatchSampleHeaderV2> samples;

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

    std::vector<EncodedCandidateV2> candidate_rows;
    std::vector<CandidateOptionalPresenceV2> candidate_optional_presence_masks;
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

struct ModelBatchPaddingRequestV2 final {
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

struct ModelBatchPaddingWidthsV2 final {
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

struct PaddedModelBatchV2 final {
    std::string schema_id = std::string(kModelBatchLayoutV2SchemaId);
    std::uint32_t batch_size = 0;
    std::vector<ModelBatchSampleHeaderV2> samples;
    ModelBatchPaddingWidthsV2 widths;

    std::vector<EncodedCandidateV2> candidate_features_padded;
    std::vector<std::uint8_t> candidate_row_mask;
    std::vector<CandidateOptionalPresenceV2> candidate_optional_presence_masks_padded;
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

struct ModelBatchLayoutV2 final {
    std::string schema_id = std::string(kModelBatchLayoutV2SchemaId);
    RaggedModelBatchV2 ragged;
    std::optional<PaddedModelBatchV2> padded;
};

enum class ModelBatchLayoutErrorCodeV2 : std::uint8_t {
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

struct ModelBatchLayoutErrorV2 final {
    ModelBatchLayoutErrorCodeV2 code = ModelBatchLayoutErrorCodeV2::InternalFailure;
    std::string diagnostic;
};

template <typename T>
struct ModelBatchLayoutResultV2 final {
    std::optional<T> value;
    std::optional<ModelBatchLayoutErrorV2> error;

    explicit operator bool() const noexcept {
        return value.has_value() && !error.has_value();
    }
};

using RaggedModelBatchResultV2 = ModelBatchLayoutResultV2<RaggedModelBatchV2>;
using PaddedModelBatchResultV2 = ModelBatchLayoutResultV2<PaddedModelBatchV2>;

std::string_view model_batch_layout_error_code_name_v2(
    ModelBatchLayoutErrorCodeV2 code) noexcept;

RaggedModelBatchResultV2 make_ragged_model_batch_v2(
    const std::vector<EncodedModelInputV2>& samples) noexcept;

PaddedModelBatchResultV2 pad_model_batch_v2(
    const RaggedModelBatchV2& ragged,
    const ModelBatchPaddingRequestV2& request) noexcept;

RaggedModelBatchResultV2 unpad_model_batch_v2(
    const PaddedModelBatchV2& padded) noexcept;

EncodedModelInputV2 reconstruct_model_batch_sample_v2(
    const RaggedModelBatchV2& ragged, std::size_t sample_index);

std::vector<std::uint8_t> canonical_model_batch_layout_bytes_v2(
    const RaggedModelBatchV2& ragged);
std::string model_batch_layout_identity_v2(
    const RaggedModelBatchV2& ragged);

}  // namespace ygo::model
