#include "ygo/model/model_batch_layout.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <limits>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace ygo::model {
namespace {

class LayoutFailure final {
public:
    explicit LayoutFailure(const ModelBatchLayoutErrorCode code) : code_(code) {}

    ModelBatchLayoutErrorCode code() const noexcept { return code_; }

private:
    ModelBatchLayoutErrorCode code_;
};

[[noreturn]] void fail(const ModelBatchLayoutErrorCode code) {
    throw LayoutFailure(code);
}

bool fits_u64(const std::size_t value) noexcept {
    return static_cast<std::uintmax_t>(value) <=
           static_cast<std::uintmax_t>(std::numeric_limits<std::uint64_t>::max());
}

std::size_t checked_product(const std::uint32_t batch_size,
                            const std::uint64_t width) {
    if (width > std::numeric_limits<std::size_t>::max() ||
        (batch_size != 0 && width >
                             std::numeric_limits<std::size_t>::max() / batch_size)) {
        fail(ModelBatchLayoutErrorCode::CountOverflow);
    }
    return static_cast<std::size_t>(batch_size) * static_cast<std::size_t>(width);
}

void append_offset(std::vector<std::uint64_t>& offsets,
                   const std::size_t total_flat_length) {
    if (!fits_u64(total_flat_length) || offsets.empty() ||
        total_flat_length < offsets.back()) {
        fail(ModelBatchLayoutErrorCode::CountOverflow);
    }
    offsets.push_back(static_cast<std::uint64_t>(total_flat_length));
}

std::uint64_t max_collection_length(const std::vector<std::uint64_t>& offsets) {
    if (offsets.size() < 2) {
        fail(ModelBatchLayoutErrorCode::InvalidRaggedLayout);
    }
    std::uint64_t maximum = 0;
    for (std::size_t index = 1; index < offsets.size(); ++index) {
        if (offsets[index - 1] > offsets[index]) {
            fail(ModelBatchLayoutErrorCode::InvalidRaggedLayout);
        }
        maximum = std::max(maximum, offsets[index] - offsets[index - 1]);
    }
    return maximum;
}

void validate_offsets(const std::vector<std::uint64_t>& offsets,
                      const std::uint32_t batch_size,
                      const std::size_t flat_length) {
    if (offsets.size() != static_cast<std::size_t>(batch_size) + 1 || offsets.front() != 0 ||
        !fits_u64(flat_length) ||
        offsets.back() != static_cast<std::uint64_t>(flat_length)) {
        fail(ModelBatchLayoutErrorCode::InvalidRaggedLayout);
    }
    for (std::size_t index = 1; index < offsets.size(); ++index) {
        if (offsets[index - 1] > offsets[index]) {
            fail(ModelBatchLayoutErrorCode::InvalidRaggedLayout);
        }
    }
}

void validate_header(const ModelBatchSampleHeaderV1& sample) {
    if (sample.schema_id != kEncodedModelInputSchemaId ||
        !sample.globals.life_points.empty() ||
        !sample.match_context.own_deck.main_deck.empty() ||
        !sample.match_context.own_deck.extra_deck.empty() ||
        !sample.match_context.opponent_deck.main_deck.empty() ||
        !sample.match_context.opponent_deck.extra_deck.empty()) {
        fail(ModelBatchLayoutErrorCode::InvalidRaggedLayout);
    }
}

ModelBatchSampleHeaderV1 make_header(const EncodedModelInputV1& sample) {
    ModelBatchSampleHeaderV1 header;
    header.schema_id = sample.schema_id;
    header.card_vocabulary_identity = sample.card_vocabulary_identity;
    header.public_observation_digest = sample.public_observation_digest;
    header.perspective_player = sample.perspective_player;
    header.decision_index = sample.decision_index;
    header.public_observation_context_kind_code =
        sample.public_observation_context_kind_code;
    header.public_observation_context_player = sample.public_observation_context_player;
    header.globals = sample.globals;
    header.globals.life_points.clear();
    header.chain_length = sample.chain.length;
    header.match_context = sample.match_context;
    header.match_context.own_deck.main_deck.clear();
    header.match_context.own_deck.extra_deck.clear();
    header.match_context.opponent_deck.main_deck.clear();
    header.match_context.opponent_deck.extra_deck.clear();
    header.public_candidate_domain_digest = sample.public_candidate_domain_digest;
    validate_header(header);
    return header;
}

CandidateOptionalPresenceV1 candidate_presence(const EncodedCandidate& candidate) {
    return {static_cast<std::uint8_t>(candidate.choice.has_value()),
            static_cast<std::uint8_t>(candidate.source_reference.has_value()),
            static_cast<std::uint8_t>(candidate.target_reference.has_value()),
            static_cast<std::uint8_t>(candidate.phase.has_value()),
            static_cast<std::uint8_t>(candidate.position.has_value()),
            static_cast<std::uint8_t>(candidate.source_index.has_value()),
            static_cast<std::uint8_t>(candidate.amount.has_value())};
}

bool same_presence(const CandidateOptionalPresenceV1& left,
                   const CandidateOptionalPresenceV1& right) noexcept {
    return left.choice <= 1 && left.source_reference <= 1 && left.target_reference <= 1 &&
           left.phase <= 1 && left.position <= 1 && left.source_index <= 1 && left.amount <= 1 &&
           left.choice == right.choice && left.source_reference == right.source_reference &&
           left.target_reference == right.target_reference && left.phase == right.phase &&
           left.position == right.position && left.source_index == right.source_index &&
           left.amount == right.amount;
}

EncodedModelInputV1 reconstruct_unchecked(const RaggedModelBatchV1& ragged,
                                           const std::size_t sample_index) {
    if (sample_index >= ragged.samples.size()) {
        fail(ModelBatchLayoutErrorCode::InvalidRaggedLayout);
    }
    const auto slice = [sample_index](const auto& values,
                                      const std::vector<std::uint64_t>& offsets) {
        using Value = typename std::decay_t<decltype(values)>::value_type;
        const auto begin = static_cast<std::size_t>(offsets[sample_index]);
        const auto end = static_cast<std::size_t>(offsets[sample_index + 1]);
        return std::vector<Value>(values.begin() + static_cast<std::ptrdiff_t>(begin),
                                  values.begin() + static_cast<std::ptrdiff_t>(end));
    };

    const auto& header = ragged.samples[sample_index];
    EncodedModelInputV1 output;
    output.schema_id = header.schema_id;
    output.card_vocabulary_identity = header.card_vocabulary_identity;
    output.public_observation_digest = header.public_observation_digest;
    output.perspective_player = header.perspective_player;
    output.decision_index = header.decision_index;
    output.public_locator_table = slice(ragged.public_locator_tokens,
                                        ragged.public_locator_token_offsets);
    output.public_observation_context_kind_code =
        header.public_observation_context_kind_code;
    output.public_observation_context_player = header.public_observation_context_player;
    output.observation_context_reference_ordinals =
        slice(ragged.decision_context_reference_ordinals,
              ragged.decision_context_reference_offsets);
    output.globals = header.globals;
    output.globals.life_points = slice(ragged.life_points, ragged.life_point_offsets);
    output.zones = slice(ragged.zones, ragged.zone_offsets);
    output.entities = slice(ragged.entities, ragged.entity_offsets);
    output.relationships = slice(ragged.relationships, ragged.relationship_offsets);
    output.chain.length = header.chain_length;
    output.chain.links = slice(ragged.chain_links, ragged.chain_link_offsets);
    output.visible_events = slice(ragged.visible_events, ragged.visible_event_offsets);
    output.match_context = header.match_context;
    output.match_context.own_deck.main_deck =
        slice(ragged.own_deck_passcode_ids, ragged.own_deck_passcode_offsets);
    output.match_context.own_deck.extra_deck =
        slice(ragged.own_extra_deck_passcode_ids,
              ragged.own_extra_deck_passcode_offsets);
    output.match_context.opponent_deck.main_deck =
        slice(ragged.opponent_deck_passcode_ids,
              ragged.opponent_deck_passcode_offsets);
    output.match_context.opponent_deck.extra_deck =
        slice(ragged.opponent_extra_deck_passcode_ids,
              ragged.opponent_extra_deck_passcode_offsets);
    output.public_candidate_domain_digest = header.public_candidate_domain_digest;
    output.candidate_features = slice(ragged.candidate_rows, ragged.candidate_offsets);
    output.routing_keys = slice(ragged.candidate_routing_keys, ragged.candidate_offsets);
    return output;
}

void validate_ragged(const RaggedModelBatchV1& ragged) {
    if (ragged.schema_id != kModelBatchLayoutSchemaId || ragged.batch_size == 0 ||
        ragged.samples.size() != ragged.batch_size) {
        fail(ModelBatchLayoutErrorCode::EmptyBatch);
    }
    for (const auto& sample : ragged.samples) validate_header(sample);
    validate_offsets(ragged.candidate_offsets, ragged.batch_size,
                     ragged.candidate_rows.size());
    validate_offsets(ragged.zone_offsets, ragged.batch_size, ragged.zones.size());
    validate_offsets(ragged.entity_offsets, ragged.batch_size, ragged.entities.size());
    validate_offsets(ragged.relationship_offsets, ragged.batch_size,
                     ragged.relationships.size());
    validate_offsets(ragged.chain_link_offsets, ragged.batch_size,
                     ragged.chain_links.size());
    validate_offsets(ragged.visible_event_offsets, ragged.batch_size,
                     ragged.visible_events.size());
    validate_offsets(ragged.decision_context_reference_offsets, ragged.batch_size,
                     ragged.decision_context_reference_ordinals.size());
    validate_offsets(ragged.public_locator_token_offsets, ragged.batch_size,
                     ragged.public_locator_tokens.size());
    validate_offsets(ragged.life_point_offsets, ragged.batch_size,
                     ragged.life_points.size());
    validate_offsets(ragged.own_deck_passcode_offsets, ragged.batch_size,
                     ragged.own_deck_passcode_ids.size());
    validate_offsets(ragged.opponent_deck_passcode_offsets, ragged.batch_size,
                     ragged.opponent_deck_passcode_ids.size());
    validate_offsets(ragged.own_extra_deck_passcode_offsets, ragged.batch_size,
                     ragged.own_extra_deck_passcode_ids.size());
    validate_offsets(ragged.opponent_extra_deck_passcode_offsets, ragged.batch_size,
                     ragged.opponent_extra_deck_passcode_ids.size());
    if (ragged.candidate_routing_keys.size() != ragged.candidate_rows.size() ||
        ragged.candidate_optional_presence_masks.size() != ragged.candidate_rows.size()) {
        fail(ModelBatchLayoutErrorCode::InvalidRaggedLayout);
    }
    for (std::size_t index = 0; index < ragged.candidate_rows.size(); ++index) {
        if (!same_presence(ragged.candidate_optional_presence_masks[index],
                           candidate_presence(ragged.candidate_rows[index]))) {
            fail(ModelBatchLayoutErrorCode::OptionalPresenceMismatch);
        }
    }
    for (std::size_t index = 0; index < ragged.samples.size(); ++index) {
        try {
            (void)canonical_encoded_model_input_bytes(
                reconstruct_unchecked(ragged, index));
        } catch (const std::exception&) {
            fail(ModelBatchLayoutErrorCode::InvalidEncodedSample);
        } catch (...) {
            fail(ModelBatchLayoutErrorCode::InvalidEncodedSample);
        }
    }
}

template <typename T, typename PaddingPredicate>
void collect_padded_collection(const std::vector<T>& padded_values,
                              const std::vector<std::uint8_t>& row_mask,
                              const std::uint32_t batch_size,
                              const std::uint64_t width,
                              const PaddingPredicate& is_padding,
                              std::vector<T>& flat_values,
                              std::vector<std::uint64_t>& offsets) {
    const auto expected_size = checked_product(batch_size, width);
    if (padded_values.size() != expected_size || row_mask.size() != expected_size) {
        fail(ModelBatchLayoutErrorCode::InvalidPaddedLayout);
    }
    offsets.clear();
    offsets.push_back(0);
    flat_values.clear();
    for (std::uint32_t sample = 0; sample < batch_size; ++sample) {
        bool padding_started = false;
        const auto base = static_cast<std::size_t>(sample) * static_cast<std::size_t>(width);
        for (std::uint64_t column = 0; column < width; ++column) {
            const auto index = base + static_cast<std::size_t>(column);
            const auto mask = row_mask[index];
            if (mask > 1) fail(ModelBatchLayoutErrorCode::MaskMismatch);
            if (mask == 1) {
                if (padding_started) fail(ModelBatchLayoutErrorCode::MaskMismatch);
                flat_values.push_back(padded_values[index]);
            } else {
                padding_started = true;
                if (!is_padding(padded_values[index])) {
                    fail(ModelBatchLayoutErrorCode::PaddingValueMismatch);
                }
            }
        }
        append_offset(offsets, flat_values.size());
    }
}

bool is_padding_candidate(const EncodedCandidate& value) noexcept {
    return value.action_kind_code == 0 && !value.choice.has_value() &&
           !value.source_reference.has_value() && !value.target_reference.has_value() &&
           !value.phase.has_value() && !value.position.has_value() &&
           !value.source_index.has_value() && !value.amount.has_value() &&
           value.continuation_operation_code == 0 && !value.submits_engine_response;
}

bool is_padding_zone(const EncodedZone& value) noexcept {
    return value.player == 0 && value.kind_code == 0 && value.total_count == 0 &&
           value.public_identity_count == 0 && value.hidden_count == 0 &&
           !value.player_observable_order;
}

bool is_padding_entity(const EncodedEntity& value) noexcept {
    return value.public_locator_ordinal == 0 && !value.identity_known &&
           value.card_vocabulary_id == 0 && !value.owner.has_value() &&
           !value.controller.has_value() && value.zone_code == 0 &&
           !value.sequence.has_value() && !value.overlay_sequence.has_value() &&
           value.position_code == 0 && !value.face_up && !value.face_down &&
           !value.printed.has_value() && !value.current.has_value();
}

bool is_padding_reference(const EncodedCurrentReference& value) noexcept {
    return value.public_locator_ordinal == 0 && !value.current_entity_ordinal.has_value();
}

bool is_padding_relationship(const EncodedRelationship& value) noexcept {
    return value.kind_code == 0 && is_padding_reference(value.source) &&
           is_padding_reference(value.target);
}

bool is_padding_chain_link(const EncodedChainLink& value) noexcept {
    return value.index == 0 && !value.activating_player.has_value() &&
           !value.source.has_value() && !value.activation_zone_code.has_value() &&
           !value.effect_description.has_value() && value.targets.empty();
}

bool is_padding_event(const EncodedVisibleEvent& value) noexcept {
    return value.event_index == 0 && value.kind_code == 0 && !value.player.has_value() &&
           !value.public_locator_ordinal.has_value() &&
           !value.public_card_vocabulary_id.has_value() && !value.from_zone_code.has_value() &&
           !value.to_zone_code.has_value() && !value.count.has_value() &&
           !value.amount.has_value() && !value.counter_type.has_value() &&
           !value.phase.has_value() && !value.winner.has_value() &&
           !value.win_reason.has_value() && !value.effect_description.has_value() &&
           value.target_public_locator_ordinals.empty();
}

template <typename T>
void fill_padded_collection(const std::vector<T>& flat_values,
                            const std::vector<std::uint64_t>& offsets,
                            const std::uint32_t batch_size,
                            const std::uint64_t width,
                            const T& padding,
                            std::vector<T>& padded_values,
                            std::vector<std::uint8_t>& row_mask) {
    const auto expected_size = checked_product(batch_size, width);
    padded_values.assign(expected_size, padding);
    row_mask.assign(expected_size, 0);
    for (std::uint32_t sample = 0; sample < batch_size; ++sample) {
        const auto begin = static_cast<std::size_t>(offsets[sample]);
        const auto end = static_cast<std::size_t>(offsets[sample + 1]);
        const auto count = end - begin;
        if (count > width) fail(ModelBatchLayoutErrorCode::CapacityTooSmall);
        const auto destination = static_cast<std::size_t>(sample) *
                                static_cast<std::size_t>(width);
        std::copy(flat_values.begin() + static_cast<std::ptrdiff_t>(begin),
                  flat_values.begin() + static_cast<std::ptrdiff_t>(end),
                  padded_values.begin() + static_cast<std::ptrdiff_t>(destination));
        std::fill(row_mask.begin() + static_cast<std::ptrdiff_t>(destination),
                  row_mask.begin() + static_cast<std::ptrdiff_t>(destination + count),
                  std::uint8_t{1});
    }
}

std::uint64_t choose_width(const std::optional<std::uint64_t>& requested,
                           const std::uint64_t maximum) {
    const auto width = requested.value_or(maximum);
    if (width < maximum) fail(ModelBatchLayoutErrorCode::CapacityTooSmall);
    return width;
}

void validate_padded_headers(const PaddedModelBatchV1& padded) {
    if (padded.schema_id != kModelBatchLayoutSchemaId || padded.batch_size == 0 ||
        padded.samples.size() != padded.batch_size) {
        fail(ModelBatchLayoutErrorCode::InvalidPaddedLayout);
    }
    for (const auto& sample : padded.samples) validate_header(sample);
}

void validate_padded_sizes(const PaddedModelBatchV1& padded) {
    const auto check = [&padded](const std::size_t actual,
                                 const std::uint64_t width) {
        if (actual != checked_product(padded.batch_size, width)) {
            fail(ModelBatchLayoutErrorCode::InvalidPaddedLayout);
        }
    };
    check(padded.candidate_features_padded.size(), padded.widths.candidate_width);
    check(padded.candidate_row_mask.size(), padded.widths.candidate_width);
    check(padded.candidate_optional_presence_masks_padded.size(),
          padded.widths.candidate_width);
    check(padded.candidate_routing_keys_padded.size(), padded.widths.candidate_width);
    check(padded.zones_padded.size(), padded.widths.zone_width);
    check(padded.zone_row_mask.size(), padded.widths.zone_width);
    check(padded.entities_padded.size(), padded.widths.entity_width);
    check(padded.entity_row_mask.size(), padded.widths.entity_width);
    check(padded.relationships_padded.size(), padded.widths.relationship_width);
    check(padded.relationship_row_mask.size(), padded.widths.relationship_width);
    check(padded.chain_links_padded.size(), padded.widths.chain_link_width);
    check(padded.chain_link_row_mask.size(), padded.widths.chain_link_width);
    check(padded.visible_events_padded.size(), padded.widths.visible_event_width);
    check(padded.visible_event_row_mask.size(), padded.widths.visible_event_width);
    check(padded.decision_context_reference_ordinals_padded.size(),
          padded.widths.decision_context_reference_width);
    check(padded.decision_context_reference_row_mask.size(),
          padded.widths.decision_context_reference_width);
    check(padded.public_locator_tokens_padded.size(),
          padded.widths.public_locator_token_width);
    check(padded.public_locator_token_row_mask.size(),
          padded.widths.public_locator_token_width);
    check(padded.life_points_padded.size(), padded.widths.life_point_width);
    check(padded.life_point_row_mask.size(), padded.widths.life_point_width);
    check(padded.own_deck_passcode_ids_padded.size(), padded.widths.own_deck_passcode_width);
    check(padded.own_deck_passcode_row_mask.size(), padded.widths.own_deck_passcode_width);
    check(padded.opponent_deck_passcode_ids_padded.size(),
          padded.widths.opponent_deck_passcode_width);
    check(padded.opponent_deck_passcode_row_mask.size(),
          padded.widths.opponent_deck_passcode_width);
    check(padded.own_extra_deck_passcode_ids_padded.size(),
          padded.widths.own_extra_deck_passcode_width);
    check(padded.own_extra_deck_passcode_row_mask.size(),
          padded.widths.own_extra_deck_passcode_width);
    check(padded.opponent_extra_deck_passcode_ids_padded.size(),
          padded.widths.opponent_extra_deck_passcode_width);
    check(padded.opponent_extra_deck_passcode_row_mask.size(),
          padded.widths.opponent_extra_deck_passcode_width);
}

const char* diagnostic_for(const ModelBatchLayoutErrorCode code) noexcept {
    switch (code) {
    case ModelBatchLayoutErrorCode::EmptyBatch:
        return "model batch is empty";
    case ModelBatchLayoutErrorCode::InvalidEncodedSample:
        return "model batch contains an invalid encoded sample";
    case ModelBatchLayoutErrorCode::InvalidRaggedLayout:
        return "ragged model batch layout is invalid";
    case ModelBatchLayoutErrorCode::CapacityTooSmall:
        return "model batch padding capacity is too small";
    case ModelBatchLayoutErrorCode::InvalidPaddedLayout:
        return "padded model batch layout is invalid";
    case ModelBatchLayoutErrorCode::MaskMismatch:
        return "model batch row mask is invalid";
    case ModelBatchLayoutErrorCode::PaddingValueMismatch:
        return "model batch padding row contains a value";
    case ModelBatchLayoutErrorCode::OptionalPresenceMismatch:
        return "model batch optional presence mask is invalid";
    case ModelBatchLayoutErrorCode::RoundtripMismatch:
        return "model batch ragged/padded roundtrip changed a value";
    case ModelBatchLayoutErrorCode::CountOverflow:
        return "model batch count or offset exceeds its declared width";
    case ModelBatchLayoutErrorCode::InternalFailure:
        return "model batch layout failed";
    }
    return "model batch layout failed";
}

template <typename T>
ModelBatchLayoutResult<T> failure_with_diagnostic(
    const ModelBatchLayoutErrorCode code) noexcept {
    ModelBatchLayoutResult<T> result;
    result.error = ModelBatchLayoutError{code, diagnostic_for(code)};
    return result;
}

}  // namespace

std::string_view model_batch_layout_error_code_name(
    const ModelBatchLayoutErrorCode code) noexcept {
    switch (code) {
    case ModelBatchLayoutErrorCode::EmptyBatch:
        return "empty_batch";
    case ModelBatchLayoutErrorCode::InvalidEncodedSample:
        return "invalid_encoded_sample";
    case ModelBatchLayoutErrorCode::InvalidRaggedLayout:
        return "invalid_ragged_layout";
    case ModelBatchLayoutErrorCode::CapacityTooSmall:
        return "capacity_too_small";
    case ModelBatchLayoutErrorCode::InvalidPaddedLayout:
        return "invalid_padded_layout";
    case ModelBatchLayoutErrorCode::MaskMismatch:
        return "mask_mismatch";
    case ModelBatchLayoutErrorCode::PaddingValueMismatch:
        return "padding_value_mismatch";
    case ModelBatchLayoutErrorCode::OptionalPresenceMismatch:
        return "optional_presence_mismatch";
    case ModelBatchLayoutErrorCode::RoundtripMismatch:
        return "roundtrip_mismatch";
    case ModelBatchLayoutErrorCode::CountOverflow:
        return "count_overflow";
    case ModelBatchLayoutErrorCode::InternalFailure:
        return "internal_failure";
    }
    return "internal_failure";
}

RaggedModelBatchResult make_ragged_model_batch_v1(
    const std::vector<EncodedModelInputV1>& samples) noexcept {
    try {
        if (samples.empty()) return failure_with_diagnostic<RaggedModelBatchV1>(
            ModelBatchLayoutErrorCode::EmptyBatch);
        if (samples.size() > std::numeric_limits<std::uint32_t>::max()) {
            return failure_with_diagnostic<RaggedModelBatchV1>(
                ModelBatchLayoutErrorCode::CountOverflow);
        }
        RaggedModelBatchV1 output;
        output.batch_size = static_cast<std::uint32_t>(samples.size());
        output.samples.reserve(samples.size());
        output.candidate_offsets.push_back(0);
        output.zone_offsets.push_back(0);
        output.entity_offsets.push_back(0);
        output.relationship_offsets.push_back(0);
        output.chain_link_offsets.push_back(0);
        output.visible_event_offsets.push_back(0);
        output.decision_context_reference_offsets.push_back(0);
        output.public_locator_token_offsets.push_back(0);
        output.life_point_offsets.push_back(0);
        output.own_deck_passcode_offsets.push_back(0);
        output.opponent_deck_passcode_offsets.push_back(0);
        output.own_extra_deck_passcode_offsets.push_back(0);
        output.opponent_extra_deck_passcode_offsets.push_back(0);
        for (const auto& sample : samples) {
            try {
                (void)canonical_encoded_model_input_bytes(sample);
            } catch (...) {
                return failure_with_diagnostic<RaggedModelBatchV1>(
                    ModelBatchLayoutErrorCode::InvalidEncodedSample);
            }
            output.samples.push_back(make_header(sample));
            output.candidate_rows.insert(output.candidate_rows.end(),
                                         sample.candidate_features.begin(),
                                         sample.candidate_features.end());
            output.candidate_routing_keys.insert(output.candidate_routing_keys.end(),
                                                 sample.routing_keys.begin(),
                                                 sample.routing_keys.end());
            for (const auto& candidate : sample.candidate_features) {
                output.candidate_optional_presence_masks.push_back(
                    candidate_presence(candidate));
            }
            output.zones.insert(output.zones.end(), sample.zones.begin(), sample.zones.end());
            output.entities.insert(output.entities.end(), sample.entities.begin(), sample.entities.end());
            output.relationships.insert(output.relationships.end(), sample.relationships.begin(), sample.relationships.end());
            output.chain_links.insert(output.chain_links.end(), sample.chain.links.begin(), sample.chain.links.end());
            output.visible_events.insert(output.visible_events.end(), sample.visible_events.begin(), sample.visible_events.end());
            output.decision_context_reference_ordinals.insert(
                output.decision_context_reference_ordinals.end(),
                sample.observation_context_reference_ordinals.begin(),
                sample.observation_context_reference_ordinals.end());
            output.public_locator_tokens.insert(output.public_locator_tokens.end(),
                                               sample.public_locator_table.begin(),
                                               sample.public_locator_table.end());
            output.life_points.insert(output.life_points.end(), sample.globals.life_points.begin(), sample.globals.life_points.end());
            output.own_deck_passcode_ids.insert(output.own_deck_passcode_ids.end(), sample.match_context.own_deck.main_deck.begin(), sample.match_context.own_deck.main_deck.end());
            output.opponent_deck_passcode_ids.insert(output.opponent_deck_passcode_ids.end(), sample.match_context.opponent_deck.main_deck.begin(), sample.match_context.opponent_deck.main_deck.end());
            output.own_extra_deck_passcode_ids.insert(
                output.own_extra_deck_passcode_ids.end(),
                sample.match_context.own_deck.extra_deck.begin(),
                sample.match_context.own_deck.extra_deck.end());
            output.opponent_extra_deck_passcode_ids.insert(
                output.opponent_extra_deck_passcode_ids.end(),
                sample.match_context.opponent_deck.extra_deck.begin(),
                sample.match_context.opponent_deck.extra_deck.end());
            append_offset(output.candidate_offsets, output.candidate_rows.size());
            append_offset(output.zone_offsets, output.zones.size());
            append_offset(output.entity_offsets, output.entities.size());
            append_offset(output.relationship_offsets, output.relationships.size());
            append_offset(output.chain_link_offsets, output.chain_links.size());
            append_offset(output.visible_event_offsets, output.visible_events.size());
            append_offset(output.decision_context_reference_offsets,
                          output.decision_context_reference_ordinals.size());
            append_offset(output.public_locator_token_offsets,
                          output.public_locator_tokens.size());
            append_offset(output.life_point_offsets, output.life_points.size());
            append_offset(output.own_deck_passcode_offsets,
                          output.own_deck_passcode_ids.size());
            append_offset(output.opponent_deck_passcode_offsets,
                          output.opponent_deck_passcode_ids.size());
            append_offset(output.own_extra_deck_passcode_offsets,
                          output.own_extra_deck_passcode_ids.size());
            append_offset(output.opponent_extra_deck_passcode_offsets,
                          output.opponent_extra_deck_passcode_ids.size());
        }
        validate_ragged(output);
        return {std::optional<RaggedModelBatchV1>(std::move(output)), std::nullopt};
    } catch (const LayoutFailure& error) {
        return failure_with_diagnostic<RaggedModelBatchV1>(error.code());
    } catch (const std::bad_alloc&) {
        return failure_with_diagnostic<RaggedModelBatchV1>(
            ModelBatchLayoutErrorCode::InternalFailure);
    } catch (...) {
        return failure_with_diagnostic<RaggedModelBatchV1>(
            ModelBatchLayoutErrorCode::InternalFailure);
    }
}

PaddedModelBatchResult pad_model_batch_v1(
    const RaggedModelBatchV1& ragged,
    const ModelBatchPaddingRequestV1& request) noexcept {
    try {
        validate_ragged(ragged);
        PaddedModelBatchV1 output;
        output.batch_size = ragged.batch_size;
        output.samples = ragged.samples;
        output.widths.candidate_width =
            choose_width(std::optional<std::uint64_t>(request.candidate_width),
                         max_collection_length(ragged.candidate_offsets));
        output.widths.zone_width = choose_width(request.zone_width,
                                                max_collection_length(ragged.zone_offsets));
        output.widths.entity_width = choose_width(request.entity_width,
                                                  max_collection_length(ragged.entity_offsets));
        output.widths.relationship_width = choose_width(
            request.relationship_width, max_collection_length(ragged.relationship_offsets));
        output.widths.chain_link_width = choose_width(
            request.chain_link_width, max_collection_length(ragged.chain_link_offsets));
        output.widths.visible_event_width = choose_width(
            request.visible_event_width, max_collection_length(ragged.visible_event_offsets));
        output.widths.decision_context_reference_width = choose_width(
            request.decision_context_reference_width,
            max_collection_length(ragged.decision_context_reference_offsets));
        output.widths.public_locator_token_width = choose_width(
            request.public_locator_token_width,
            max_collection_length(ragged.public_locator_token_offsets));
        output.widths.life_point_width = choose_width(
            request.life_point_width, max_collection_length(ragged.life_point_offsets));
        output.widths.own_deck_passcode_width = choose_width(
            request.own_deck_passcode_width,
            max_collection_length(ragged.own_deck_passcode_offsets));
        output.widths.opponent_deck_passcode_width = choose_width(
            request.opponent_deck_passcode_width,
            max_collection_length(ragged.opponent_deck_passcode_offsets));
        output.widths.own_extra_deck_passcode_width = choose_width(
            request.own_extra_deck_passcode_width,
            max_collection_length(ragged.own_extra_deck_passcode_offsets));
        output.widths.opponent_extra_deck_passcode_width = choose_width(
            request.opponent_extra_deck_passcode_width,
            max_collection_length(ragged.opponent_extra_deck_passcode_offsets));

        EncodedCandidate candidate_padding;
        candidate_padding.submits_engine_response = false;
        fill_padded_collection(ragged.candidate_rows, ragged.candidate_offsets,
                               ragged.batch_size, output.widths.candidate_width,
                               candidate_padding, output.candidate_features_padded,
                               output.candidate_row_mask);
        CandidateOptionalPresenceV1 presence_padding;
        fill_padded_collection(ragged.candidate_optional_presence_masks,
                               ragged.candidate_offsets, ragged.batch_size,
                               output.widths.candidate_width, presence_padding,
                               output.candidate_optional_presence_masks_padded,
                               output.candidate_row_mask);
        fill_padded_collection(ragged.candidate_routing_keys, ragged.candidate_offsets,
                               ragged.batch_size, output.widths.candidate_width, std::string{},
                               output.candidate_routing_keys_padded,
                               output.candidate_row_mask);
        fill_padded_collection(ragged.zones, ragged.zone_offsets, ragged.batch_size,
                               output.widths.zone_width, EncodedZone{}, output.zones_padded,
                               output.zone_row_mask);
        EncodedEntity entity_padding;
        entity_padding.card_vocabulary_id = 0;
        fill_padded_collection(ragged.entities, ragged.entity_offsets, ragged.batch_size,
                               output.widths.entity_width, entity_padding,
                               output.entities_padded, output.entity_row_mask);
        fill_padded_collection(ragged.relationships, ragged.relationship_offsets,
                               ragged.batch_size, output.widths.relationship_width,
                               EncodedRelationship{}, output.relationships_padded,
                               output.relationship_row_mask);
        fill_padded_collection(ragged.chain_links, ragged.chain_link_offsets,
                               ragged.batch_size, output.widths.chain_link_width,
                               EncodedChainLink{}, output.chain_links_padded,
                               output.chain_link_row_mask);
        fill_padded_collection(ragged.visible_events, ragged.visible_event_offsets,
                               ragged.batch_size, output.widths.visible_event_width,
                               EncodedVisibleEvent{}, output.visible_events_padded,
                               output.visible_event_row_mask);
        fill_padded_collection(
            ragged.decision_context_reference_ordinals,
            ragged.decision_context_reference_offsets, ragged.batch_size,
            output.widths.decision_context_reference_width, std::uint32_t{0},
            output.decision_context_reference_ordinals_padded,
            output.decision_context_reference_row_mask);
        fill_padded_collection(ragged.public_locator_tokens,
                               ragged.public_locator_token_offsets, ragged.batch_size,
                               output.widths.public_locator_token_width, std::string{},
                               output.public_locator_tokens_padded,
                               output.public_locator_token_row_mask);
        fill_padded_collection(ragged.life_points, ragged.life_point_offsets,
                               ragged.batch_size, output.widths.life_point_width,
                               std::uint32_t{0}, output.life_points_padded,
                               output.life_point_row_mask);
        fill_padded_collection(ragged.own_deck_passcode_ids,
                               ragged.own_deck_passcode_offsets, ragged.batch_size,
                               output.widths.own_deck_passcode_width, std::uint32_t{0},
                               output.own_deck_passcode_ids_padded,
                               output.own_deck_passcode_row_mask);
        fill_padded_collection(ragged.opponent_deck_passcode_ids,
                               ragged.opponent_deck_passcode_offsets, ragged.batch_size,
                               output.widths.opponent_deck_passcode_width, std::uint32_t{0},
                               output.opponent_deck_passcode_ids_padded,
                               output.opponent_deck_passcode_row_mask);
        fill_padded_collection(
            ragged.own_extra_deck_passcode_ids,
            ragged.own_extra_deck_passcode_offsets, ragged.batch_size,
            output.widths.own_extra_deck_passcode_width, std::uint32_t{0},
            output.own_extra_deck_passcode_ids_padded,
            output.own_extra_deck_passcode_row_mask);
        fill_padded_collection(
            ragged.opponent_extra_deck_passcode_ids,
            ragged.opponent_extra_deck_passcode_offsets, ragged.batch_size,
            output.widths.opponent_extra_deck_passcode_width, std::uint32_t{0},
            output.opponent_extra_deck_passcode_ids_padded,
            output.opponent_extra_deck_passcode_row_mask);
        return {std::optional<PaddedModelBatchV1>(std::move(output)), std::nullopt};
    } catch (const LayoutFailure& error) {
        return failure_with_diagnostic<PaddedModelBatchV1>(error.code());
    } catch (const std::bad_alloc&) {
        return failure_with_diagnostic<PaddedModelBatchV1>(
            ModelBatchLayoutErrorCode::InternalFailure);
    } catch (...) {
        return failure_with_diagnostic<PaddedModelBatchV1>(
            ModelBatchLayoutErrorCode::InternalFailure);
    }
}

RaggedModelBatchResult unpad_model_batch_v1(
    const PaddedModelBatchV1& padded) noexcept {
    try {
        validate_padded_headers(padded);
        validate_padded_sizes(padded);
        RaggedModelBatchV1 output;
        output.batch_size = padded.batch_size;
        output.samples = padded.samples;
        collect_padded_collection(
            padded.candidate_features_padded, padded.candidate_row_mask,
            padded.batch_size, padded.widths.candidate_width, is_padding_candidate,
            output.candidate_rows, output.candidate_offsets);
        if (padded.candidate_optional_presence_masks_padded.size() !=
            padded.candidate_features_padded.size()) {
            fail(ModelBatchLayoutErrorCode::InvalidPaddedLayout);
        }
        output.candidate_optional_presence_masks.clear();
        for (std::size_t index = 0; index < padded.candidate_features_padded.size(); ++index) {
            if (padded.candidate_row_mask[index] == 0) {
                const CandidateOptionalPresenceV1 empty;
                if (!same_presence(padded.candidate_optional_presence_masks_padded[index],
                                   empty)) {
                    fail(ModelBatchLayoutErrorCode::OptionalPresenceMismatch);
                }
            } else if (padded.candidate_row_mask[index] == 1) {
                if (!same_presence(padded.candidate_optional_presence_masks_padded[index],
                                   candidate_presence(padded.candidate_features_padded[index]))) {
                    fail(ModelBatchLayoutErrorCode::OptionalPresenceMismatch);
                }
                output.candidate_optional_presence_masks.push_back(
                    padded.candidate_optional_presence_masks_padded[index]);
            } else {
                fail(ModelBatchLayoutErrorCode::MaskMismatch);
            }
        }
        collect_padded_collection(
            padded.candidate_routing_keys_padded, padded.candidate_row_mask,
            padded.batch_size, padded.widths.candidate_width,
            [](const std::string& value) { return value.empty(); },
            output.candidate_routing_keys, output.candidate_offsets);
        collect_padded_collection(padded.zones_padded, padded.zone_row_mask,
                                  padded.batch_size, padded.widths.zone_width,
                                  is_padding_zone, output.zones, output.zone_offsets);
        collect_padded_collection(padded.entities_padded, padded.entity_row_mask,
                                  padded.batch_size, padded.widths.entity_width,
                                  is_padding_entity, output.entities, output.entity_offsets);
        collect_padded_collection(
            padded.relationships_padded, padded.relationship_row_mask, padded.batch_size,
            padded.widths.relationship_width, is_padding_relationship,
            output.relationships, output.relationship_offsets);
        collect_padded_collection(
            padded.chain_links_padded, padded.chain_link_row_mask, padded.batch_size,
            padded.widths.chain_link_width, is_padding_chain_link, output.chain_links,
            output.chain_link_offsets);
        collect_padded_collection(
            padded.visible_events_padded, padded.visible_event_row_mask, padded.batch_size,
            padded.widths.visible_event_width, is_padding_event, output.visible_events,
            output.visible_event_offsets);
        collect_padded_collection(
            padded.decision_context_reference_ordinals_padded,
            padded.decision_context_reference_row_mask, padded.batch_size,
            padded.widths.decision_context_reference_width,
            [](const std::uint32_t value) { return value == 0; },
            output.decision_context_reference_ordinals,
            output.decision_context_reference_offsets);
        collect_padded_collection(
            padded.public_locator_tokens_padded, padded.public_locator_token_row_mask,
            padded.batch_size, padded.widths.public_locator_token_width,
            [](const std::string& value) { return value.empty(); },
            output.public_locator_tokens, output.public_locator_token_offsets);
        collect_padded_collection(
            padded.life_points_padded, padded.life_point_row_mask, padded.batch_size,
            padded.widths.life_point_width, [](const std::uint32_t value) { return value == 0; },
            output.life_points, output.life_point_offsets);
        collect_padded_collection(
            padded.own_deck_passcode_ids_padded, padded.own_deck_passcode_row_mask,
            padded.batch_size, padded.widths.own_deck_passcode_width,
            [](const std::uint32_t value) { return value == 0; },
            output.own_deck_passcode_ids, output.own_deck_passcode_offsets);
        collect_padded_collection(
            padded.opponent_deck_passcode_ids_padded,
            padded.opponent_deck_passcode_row_mask, padded.batch_size,
            padded.widths.opponent_deck_passcode_width,
            [](const std::uint32_t value) { return value == 0; },
            output.opponent_deck_passcode_ids, output.opponent_deck_passcode_offsets);
        collect_padded_collection(
            padded.own_extra_deck_passcode_ids_padded,
            padded.own_extra_deck_passcode_row_mask, padded.batch_size,
            padded.widths.own_extra_deck_passcode_width,
            [](const std::uint32_t value) { return value == 0; },
            output.own_extra_deck_passcode_ids,
            output.own_extra_deck_passcode_offsets);
        collect_padded_collection(
            padded.opponent_extra_deck_passcode_ids_padded,
            padded.opponent_extra_deck_passcode_row_mask, padded.batch_size,
            padded.widths.opponent_extra_deck_passcode_width,
            [](const std::uint32_t value) { return value == 0; },
            output.opponent_extra_deck_passcode_ids,
            output.opponent_extra_deck_passcode_offsets);
        validate_ragged(output);
        return {std::optional<RaggedModelBatchV1>(std::move(output)), std::nullopt};
    } catch (const LayoutFailure& error) {
        return failure_with_diagnostic<RaggedModelBatchV1>(error.code());
    } catch (const std::bad_alloc&) {
        return failure_with_diagnostic<RaggedModelBatchV1>(
            ModelBatchLayoutErrorCode::InternalFailure);
    } catch (...) {
        return failure_with_diagnostic<RaggedModelBatchV1>(
            ModelBatchLayoutErrorCode::InternalFailure);
    }
}

EncodedModelInputV1 reconstruct_model_batch_sample_v1(
    const RaggedModelBatchV1& ragged, const std::size_t sample_index) {
    try {
        validate_ragged(ragged);
        return reconstruct_unchecked(ragged, sample_index);
    } catch (const LayoutFailure&) {
        throw std::invalid_argument("ragged model batch layout is invalid");
    }
}

}  // namespace ygo::model
