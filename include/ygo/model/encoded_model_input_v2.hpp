#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "ygo/model/card_vocabulary.hpp"
#include "ygo/model/encoded_model_input.hpp"
#include "ygo/model/logical_model_input_v2.hpp"

namespace ygo::model {

inline constexpr std::string_view kEncodedModelInputV2SchemaId =
    "ocgforge.model_encoded_input.v2";
inline constexpr std::string_view kModelInputIdentityV2SchemaId =
    "ocgforge.model_input_identity.v2";
inline constexpr std::string_view kModelInputIdentityV2Prefix = "model_input.v2.";

struct EncodedCandidateV2 final {
    std::uint16_t action_kind_code = 0;
    std::uint8_t card_selection_operation_code = 0;
    std::optional<EncodedChoice> choice;
    std::optional<EncodedCardReference> source_reference;
    std::optional<EncodedCardReference> target_reference;
    std::optional<std::uint32_t> phase;
    std::optional<std::uint8_t> position;
    std::optional<std::uint32_t> source_index;
    std::optional<std::int32_t> amount;
    std::uint8_t continuation_operation_code = 0;
    bool submits_engine_response = true;
};

struct EncodedModelInputV2 final {
    std::string schema_id = std::string(kEncodedModelInputV2SchemaId);
    std::string card_vocabulary_identity;
    std::string public_observation_digest;
    std::uint8_t perspective_player = 0;
    std::uint64_t decision_index = 0;
    std::vector<std::string> public_locator_table;
    std::optional<std::uint16_t> public_observation_context_kind_code;
    std::optional<std::uint8_t> public_observation_context_player;
    std::vector<std::uint32_t> observation_context_reference_ordinals;
    EncodedGlobals globals;
    std::vector<EncodedZone> zones;
    std::vector<EncodedEntity> entities;
    std::vector<EncodedRelationship> relationships;
    EncodedChainState chain;
    std::vector<EncodedVisibleEvent> visible_events;
    EncodedMatchContext match_context;
    std::optional<std::string> public_candidate_domain_digest;
    std::vector<EncodedCandidateV2> candidate_features;
    std::vector<std::string> routing_keys;

    std::size_t candidate_count() const noexcept {
        return candidate_features.size();
    }
};

enum class EncodedModelInputErrorCodeV2 : std::uint8_t {
    InvalidLogicalModelInput,
    UnknownPublicPasscode,
    InvalidEncodedModelInput,
    InternalFailure,
};

struct EncodedModelInputErrorV2 final {
    EncodedModelInputErrorCodeV2 code = EncodedModelInputErrorCodeV2::InternalFailure;
    std::string diagnostic;
};

struct EncodedModelInputResultV2 final {
    std::optional<EncodedModelInputV2> value;
    std::optional<EncodedModelInputErrorV2> error;

    explicit operator bool() const noexcept {
        return value.has_value() && !error.has_value();
    }
};

std::string_view encoded_model_input_error_code_name(
    EncodedModelInputErrorCodeV2 code) noexcept;

EncodedModelInputResultV2 encode_model_input_v2(
    const LogicalModelInputV2& logical,
    const CardVocabularyV1& vocabulary) noexcept;

std::vector<std::uint8_t> canonical_logical_model_input_bytes(
    const LogicalModelInputV2& logical);
std::vector<std::uint8_t> canonical_encoded_model_input_bytes(
    const EncodedModelInputV2& encoded);
std::vector<std::uint8_t> canonical_model_input_identity_bytes_v2(
    const LogicalModelInputV2& logical,
    const EncodedModelInputV2& encoded);
std::string model_input_identity_v2(const LogicalModelInputV2& logical,
                                 const EncodedModelInputV2& encoded);

}  // namespace ygo::model
