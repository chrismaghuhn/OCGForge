#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "ygo/model/card_vocabulary.hpp"
#include "ygo/model/logical_model_input.hpp"

namespace ygo::model {

inline constexpr std::string_view kEncodedModelInputSchemaId =
    "ocgforge.model_encoded_input.v1";
inline constexpr std::string_view kModelInputIdentitySchemaId =
    "ocgforge.model_input_identity.v1";
inline constexpr std::string_view kModelInputIdentityPrefix = "model_input.v1.";

struct EncodedCardProperties final {
    std::optional<std::uint32_t> type;
    std::optional<std::uint32_t> attribute;
    std::optional<std::uint64_t> race;
    std::optional<std::int32_t> attack;
    std::optional<std::int32_t> defense;
    std::optional<std::int32_t> base_attack;
    std::optional<std::int32_t> base_defense;
    std::optional<std::uint32_t> level;
    std::optional<std::uint32_t> rank;
    std::optional<std::uint32_t> link_rating;
    std::vector<std::uint8_t> link_marker_codes;
    std::optional<std::uint32_t> left_scale;
    std::optional<std::uint32_t> right_scale;
    std::optional<std::uint32_t> status_flags;
    struct Counter final {
        std::uint32_t type = 0;
        std::uint32_t count = 0;
    };
    std::vector<Counter> counters;
};

struct EncodedGlobals final {
    std::uint64_t duel_flags = 0;
    std::vector<std::uint32_t> life_points;
    std::optional<std::uint8_t> player_to_act;
    std::optional<std::uint8_t> turn_player;
    std::optional<std::uint32_t> turn_count;
    std::optional<std::uint32_t> phase;
    std::uint32_t chain_length = 0;
    std::optional<std::uint8_t> winner;
    std::optional<std::uint8_t> win_reason;
    bool terminal = false;
};

struct EncodedZone final {
    std::uint8_t player = 0;
    std::uint8_t kind_code = 0;
    std::uint32_t total_count = 0;
    std::uint32_t public_identity_count = 0;
    std::uint32_t hidden_count = 0;
    bool player_observable_order = false;
};

struct EncodedEntity final {
    std::uint32_t public_locator_ordinal = 0;
    bool identity_known = false;
    std::uint32_t card_vocabulary_id = kCardVocabularyUnknownOrRedactedId;
    std::optional<std::uint8_t> owner;
    std::optional<std::uint8_t> controller;
    std::uint8_t zone_code = 0;
    std::optional<std::uint32_t> sequence;
    std::optional<std::uint32_t> overlay_sequence;
    std::uint8_t position_code = 0;
    bool face_up = false;
    bool face_down = false;
    std::optional<EncodedCardProperties> printed;
    std::optional<EncodedCardProperties> current;
};

struct EncodedCurrentReference final {
    std::uint32_t public_locator_ordinal = 0;
    std::optional<std::uint32_t> current_entity_ordinal;
};

struct EncodedCardReference final {
    std::uint8_t kind_code = 0;
    EncodedCurrentReference reference;
};

struct EncodedRelationship final {
    std::uint8_t kind_code = 0;
    EncodedCurrentReference source;
    EncodedCurrentReference target;
};

struct EncodedChainLink final {
    std::uint32_t index = 0;
    std::optional<std::uint8_t> activating_player;
    std::optional<EncodedCurrentReference> source;
    std::optional<std::uint8_t> activation_zone_code;
    std::optional<std::uint64_t> effect_description;
    std::vector<EncodedCurrentReference> targets;
};

struct EncodedChainState final {
    std::uint32_t length = 0;
    std::vector<EncodedChainLink> links;
};

struct EncodedVisibleEvent final {
    std::uint64_t event_index = 0;
    std::uint8_t kind_code = 0;
    std::optional<std::uint8_t> player;
    std::optional<std::uint32_t> public_locator_ordinal;
    std::optional<std::uint32_t> public_card_vocabulary_id;
    std::optional<std::uint8_t> from_zone_code;
    std::optional<std::uint8_t> to_zone_code;
    std::optional<std::uint32_t> count;
    std::optional<std::int32_t> amount;
    std::optional<std::uint32_t> counter_type;
    std::optional<std::uint32_t> phase;
    std::optional<std::uint8_t> winner;
    std::optional<std::uint8_t> win_reason;
    std::optional<std::uint64_t> effect_description;
    std::vector<std::uint32_t> target_public_locator_ordinals;
};

struct EncodedDeck final {
    bool known = false;
    std::vector<std::uint32_t> main_deck;
    std::vector<std::uint32_t> extra_deck;
};

struct EncodedMatchContext final {
    std::uint8_t perspective_player = 0;
    std::uint64_t duel_flags = 0;
    bool own_decklist_known = true;
    bool opponent_decklist_known = false;
    EncodedDeck own_deck;
    EncodedDeck opponent_deck;
};

struct EncodedChoice final {
    std::uint8_t kind_code = 0;
    std::uint64_t value = 0;
    std::optional<std::uint32_t> response_index;
};

struct EncodedCandidate final {
    std::uint16_t action_kind_code = 0;
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

struct EncodedModelInputV1 final {
    std::string schema_id = std::string(kEncodedModelInputSchemaId);
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
    std::vector<EncodedCandidate> candidate_features;
    std::vector<std::string> routing_keys;

    std::size_t candidate_count() const noexcept {
        return candidate_features.size();
    }
};

enum class EncodedModelInputErrorCode : std::uint8_t {
    InvalidLogicalModelInput,
    UnknownPublicPasscode,
    InvalidEncodedModelInput,
    InternalFailure,
};

struct EncodedModelInputError final {
    EncodedModelInputErrorCode code = EncodedModelInputErrorCode::InternalFailure;
    std::string diagnostic;
};

struct EncodedModelInputResult final {
    std::optional<EncodedModelInputV1> value;
    std::optional<EncodedModelInputError> error;

    explicit operator bool() const noexcept {
        return value.has_value() && !error.has_value();
    }
};

std::string_view encoded_model_input_error_code_name(
    EncodedModelInputErrorCode code) noexcept;

EncodedModelInputResult encode_model_input_v1(
    const LogicalModelInputV1& logical,
    const CardVocabularyV1& vocabulary) noexcept;

std::vector<std::uint8_t> canonical_logical_model_input_bytes(
    const LogicalModelInputV1& logical);
std::vector<std::uint8_t> canonical_encoded_model_input_bytes(
    const EncodedModelInputV1& encoded);
std::vector<std::uint8_t> canonical_model_input_identity_bytes(
    const LogicalModelInputV1& logical,
    const EncodedModelInputV1& encoded);
std::string model_input_identity(const LogicalModelInputV1& logical,
                                 const EncodedModelInputV1& encoded);

}  // namespace ygo::model
