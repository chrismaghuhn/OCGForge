#include "task4_numeric_projection.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <optional>
#include <stdexcept>
#include <utility>

namespace ygo::phase6::task4 {
namespace {

constexpr float kU32Scale = 1.0F / 4294967295.0F;
constexpr float kU16Scale = 1.0F / 65535.0F;
constexpr float kU8Scale = 1.0F / 255.0F;

float normalized_u32(const std::uint32_t value) noexcept {
    return static_cast<float>(value) * kU32Scale;
}

float normalized_u16(const std::uint16_t value) noexcept {
    return static_cast<float>(value) * kU16Scale;
}

float normalized_u8(const std::uint8_t value) noexcept {
    return static_cast<float>(value) * kU8Scale;
}

std::pair<float, float> normalized_u64(const std::uint64_t value) noexcept {
    return {normalized_u32(static_cast<std::uint32_t>(value >> 32)),
            normalized_u32(static_cast<std::uint32_t>(value))};
}

std::array<float, 8> row(const std::uint32_t tag,
                         const bool present,
                         const std::initializer_list<float> values = {}) {
    if (values.size() > 6) {
        throw std::invalid_argument("numeric projection row has too many values");
    }
    std::array<float, 8> output{};
    output[0] = static_cast<float>(tag);
    output[1] = present ? 1.0F : 0.0F;
    std::size_t index = 2;
    for (const auto value : values) {
        if (index >= output.size()) break;
        output[index++] = value;
    }
    return output;
}

void append_u8(std::vector<StateNumericRow>& output,
               const std::uint32_t tag,
               const std::uint8_t value) {
    output.push_back(row(tag, true, {normalized_u8(value)}));
}

void append_u32(std::vector<StateNumericRow>& output,
                const std::uint32_t tag,
                const std::uint32_t value) {
    output.push_back(row(tag, true, {normalized_u32(value)}));
}

void append_u64(std::vector<StateNumericRow>& output,
                const std::uint32_t tag,
                const std::uint64_t value) {
    const auto parts = normalized_u64(value);
    output.push_back(row(tag, true, {parts.first, parts.second}));
}

void append_bool(std::vector<StateNumericRow>& output,
                 const std::uint32_t tag,
                 const bool value) {
    output.push_back(row(tag, true, {value ? 1.0F : 0.0F}));
}

void append_optional_u8(std::vector<StateNumericRow>& output,
                        const std::uint32_t tag,
                        const std::optional<std::uint8_t>& value) {
    output.push_back(value.has_value()
                         ? row(tag, true, {normalized_u8(*value)})
                         : row(tag, false));
}

void append_optional_u16(std::vector<StateNumericRow>& output,
                         const std::uint32_t tag,
                         const std::optional<std::uint16_t>& value) {
    output.push_back(value.has_value()
                         ? row(tag, true, {normalized_u16(*value)})
                         : row(tag, false));
}

void append_optional_u32(std::vector<StateNumericRow>& output,
                         const std::uint32_t tag,
                         const std::optional<std::uint32_t>& value) {
    output.push_back(value.has_value()
                         ? row(tag, true, {normalized_u32(*value)})
                         : row(tag, false));
}

void append_optional_u64(std::vector<StateNumericRow>& output,
                         const std::uint32_t tag,
                         const std::optional<std::uint64_t>& value) {
    if (!value.has_value()) {
        output.push_back(row(tag, false));
        return;
    }
    const auto parts = normalized_u64(*value);
    output.push_back(row(tag, true, {parts.first, parts.second}));
}

void append_optional_i32(std::vector<StateNumericRow>& output,
                         const std::uint32_t tag,
                         const std::optional<std::int32_t>& value) {
    output.push_back(value.has_value()
                         ? row(tag, true, {normalized_u32(static_cast<std::uint32_t>(*value))})
                         : row(tag, false));
}

void append_count(std::vector<StateNumericRow>& output,
                  const std::uint32_t tag,
                  const std::size_t count) {
    if (count > std::numeric_limits<std::uint32_t>::max()) {
        throw std::length_error("numeric projection count exceeds u32");
    }
    output.push_back(row(tag, true, {normalized_u32(static_cast<std::uint32_t>(count))}));
}

void append_reference(std::vector<StateNumericRow>& output,
                      const std::uint32_t tag,
                      const std::uint32_t locator_ordinal,
                      const std::optional<std::uint32_t>& current_entity_ordinal) {
    output.push_back(row(
        tag, true,
        {normalized_u32(locator_ordinal), current_entity_ordinal.has_value() ? 1.0F : 0.0F,
         current_entity_ordinal.has_value() ? normalized_u32(*current_entity_ordinal) : 0.0F}));
}

void append_properties(std::vector<StateNumericRow>& output,
                       const std::uint32_t base,
                       const std::optional<model::EncodedCardProperties>& properties) {
    output.push_back(row(base, properties.has_value()));
    if (!properties.has_value()) return;
    append_optional_u32(output, base + 1, properties->type);
    append_optional_u32(output, base + 2, properties->attribute);
    append_optional_u64(output, base + 3, properties->race);
    append_optional_i32(output, base + 4, properties->attack);
    append_optional_i32(output, base + 5, properties->defense);
    append_optional_i32(output, base + 6, properties->base_attack);
    append_optional_i32(output, base + 7, properties->base_defense);
    append_optional_u32(output, base + 8, properties->level);
    append_optional_u32(output, base + 9, properties->rank);
    append_optional_u32(output, base + 10, properties->link_rating);
    append_count(output, base + 11, properties->link_marker_codes.size());
    for (std::size_t index = 0; index < properties->link_marker_codes.size(); ++index) {
        output.push_back(row(base + 12, true,
                             {normalized_u32(static_cast<std::uint32_t>(index)),
                              normalized_u8(properties->link_marker_codes[index])}));
    }
    append_optional_u32(output, base + 13, properties->left_scale);
    append_optional_u32(output, base + 14, properties->right_scale);
    append_optional_u32(output, base + 15, properties->status_flags);
    append_count(output, base + 16, properties->counters.size());
    for (std::size_t index = 0; index < properties->counters.size(); ++index) {
        output.push_back(row(base + 17, true,
                             {normalized_u32(static_cast<std::uint32_t>(index)),
                              normalized_u32(properties->counters[index].type),
                              normalized_u32(properties->counters[index].count)}));
    }
}

}  // namespace

std::vector<StateNumericRow> project_state_numeric_rows(
    const Phase6BcStateInputV1& state) {
    if (state.schema_id != kPhase6BcStateInputSchemaId) {
        throw std::invalid_argument("numeric projection received an unknown state schema");
    }
    if ((!state.match_context.own_deck.known &&
         (!state.match_context.own_deck.main_deck.empty() ||
          !state.match_context.own_deck.extra_deck.empty())) ||
        (!state.match_context.opponent_deck.known &&
         (!state.match_context.opponent_deck.main_deck.empty() ||
          !state.match_context.opponent_deck.extra_deck.empty()))) {
        throw std::invalid_argument("numeric projection received hidden deck identities");
    }
    std::vector<StateNumericRow> output;
    output.reserve(32 + state.zones.size() + state.entities.size() * 8 +
                   state.relationships.size() * 3 + state.chain.links.size() * 4 +
                   state.visible_events.size() * 4);

    append_u8(output, 1, state.perspective_player);
    append_u64(output, 2, state.decision_index);
    append_optional_u16(output, 3, state.public_observation_context_kind_code);
    append_optional_u8(output, 4, state.public_observation_context_player);
    append_count(output, 5, state.public_locator_table.size());
    append_count(output, 6, state.observation_context_reference_ordinals.size());
    for (std::size_t index = 0; index < state.observation_context_reference_ordinals.size(); ++index) {
        output.push_back(row(7, true,
                             {normalized_u32(static_cast<std::uint32_t>(index)),
                              normalized_u32(state.observation_context_reference_ordinals[index])}));
    }

    append_u64(output, 10, state.globals.duel_flags);
    append_count(output, 11, state.globals.life_points.size());
    for (std::size_t index = 0; index < state.globals.life_points.size(); ++index) {
        output.push_back(row(12, true,
                             {normalized_u32(static_cast<std::uint32_t>(index)),
                              normalized_u32(state.globals.life_points[index])}));
    }
    append_optional_u8(output, 13, state.globals.player_to_act);
    append_optional_u8(output, 14, state.globals.turn_player);
    append_optional_u32(output, 15, state.globals.turn_count);
    append_optional_u32(output, 16, state.globals.phase);
    append_u32(output, 17, state.globals.chain_length);
    append_optional_u8(output, 18, state.globals.winner);
    append_optional_u8(output, 19, state.globals.win_reason);
    append_bool(output, 20, state.globals.terminal);

    append_count(output, 30, state.zones.size());
    for (std::size_t index = 0; index < state.zones.size(); ++index) {
        const auto& zone = state.zones[index];
        output.push_back(row(31, true,
                             {normalized_u32(static_cast<std::uint32_t>(index)),
                              normalized_u8(zone.player), normalized_u8(zone.kind_code),
                              normalized_u32(zone.total_count),
                              normalized_u32(zone.public_identity_count),
                              normalized_u32(zone.hidden_count)}));
        append_bool(output, 32, zone.player_observable_order);
    }

    append_count(output, 40, state.entities.size());
    for (std::size_t index = 0; index < state.entities.size(); ++index) {
        const auto& entity = state.entities[index];
        output.push_back(row(41, true,
                             {normalized_u32(static_cast<std::uint32_t>(index)),
                              normalized_u32(entity.public_locator_ordinal),
                              normalized_u32(static_cast<std::uint32_t>(index)),
                              entity.identity_known ? 1.0F : 0.0F,
                              normalized_u32(entity.card_vocabulary_id),
                              normalized_u8(entity.zone_code)}));
        append_optional_u8(output, 42, entity.owner);
        append_optional_u8(output, 43, entity.controller);
        append_optional_u32(output, 44, entity.sequence);
        append_optional_u32(output, 45, entity.overlay_sequence);
        append_u8(output, 46, entity.position_code);
        append_bool(output, 47, entity.face_up);
        append_bool(output, 48, entity.face_down);
        append_properties(output, 50, entity.printed);
        append_properties(output, 80, entity.current);
    }

    append_count(output, 110, state.relationships.size());
    for (const auto& relationship : state.relationships) {
        append_u8(output, 111, relationship.kind_code);
        append_reference(output, 112, relationship.source.public_locator_ordinal,
                         relationship.source.current_entity_ordinal);
        append_reference(output, 113, relationship.target.public_locator_ordinal,
                         relationship.target.current_entity_ordinal);
    }

    append_u32(output, 120, state.chain.length);
    append_count(output, 121, state.chain.links.size());
    for (const auto& link : state.chain.links) {
        append_u32(output, 122, link.index);
        append_optional_u8(output, 123, link.activating_player);
        if (link.source.has_value()) {
            append_reference(output, 124, link.source->public_locator_ordinal,
                             link.source->current_entity_ordinal);
        } else {
            output.push_back(row(124, false));
        }
        append_optional_u8(output, 125, link.activation_zone_code);
        append_optional_u64(output, 126, link.effect_description);
        append_count(output, 127, link.targets.size());
        for (const auto& target : link.targets) {
            append_reference(output, 128, target.public_locator_ordinal,
                             target.current_entity_ordinal);
        }
    }

    append_count(output, 140, state.visible_events.size());
    for (const auto& event : state.visible_events) {
        output.push_back(row(141, true, {normalized_u64(event.event_index).first,
                                         normalized_u64(event.event_index).second,
                                         normalized_u8(event.kind_code)}));
        append_optional_u8(output, 142, event.player);
        if (event.public_locator_ordinal.has_value()) {
            output.push_back(row(143, true, {normalized_u32(*event.public_locator_ordinal)}));
        } else {
            output.push_back(row(143, false));
        }
        append_optional_u32(output, 144, event.public_card_vocabulary_id);
        append_optional_u8(output, 145, event.from_zone_code);
        append_optional_u8(output, 146, event.to_zone_code);
        append_optional_u32(output, 147, event.count);
        append_optional_i32(output, 148, event.amount);
        append_optional_u32(output, 149, event.counter_type);
        append_optional_u32(output, 150, event.phase);
        append_optional_u8(output, 151, event.winner);
        append_optional_u8(output, 152, event.win_reason);
        append_optional_u64(output, 153, event.effect_description);
        append_count(output, 154, event.target_public_locator_ordinals.size());
        for (const auto ordinal : event.target_public_locator_ordinals) {
            append_u32(output, 155, ordinal);
        }
    }

    append_u8(output, 170, state.match_context.perspective_player);
    append_u64(output, 171, state.match_context.duel_flags);
    append_bool(output, 172, state.match_context.own_decklist_known);
    append_bool(output, 173, state.match_context.opponent_decklist_known);
    const auto append_deck = [&output](const std::uint32_t tag,
                                       const model::EncodedDeck& deck) {
        append_bool(output, tag, deck.known);
        append_count(output, tag + 1, deck.main_deck.size());
        for (const auto value : deck.main_deck) append_u32(output, tag + 2, value);
        append_count(output, tag + 3, deck.extra_deck.size());
        for (const auto value : deck.extra_deck) append_u32(output, tag + 4, value);
    };
    append_deck(180, state.match_context.own_deck);
    append_deck(190, state.match_context.opponent_deck);
    return output;
}

CandidateNumericRow project_candidate_numeric_row(
    const Phase6BcCandidateInputV1& candidate) {
    CandidateNumericRow output{};
    output[0] = normalized_u16(candidate.action_kind_code);
    output[1] = candidate.choice.has_value() ? 1.0F : 0.0F;
    if (candidate.choice.has_value()) {
        output[2] = normalized_u8(candidate.choice->kind_code);
        const auto value = normalized_u64(candidate.choice->value);
        output[3] = value.first;
        output[4] = value.second;
        output[5] = candidate.choice->response_index.has_value() ? 1.0F : 0.0F;
        output[6] = candidate.choice->response_index.has_value()
                        ? normalized_u32(*candidate.choice->response_index)
                        : 0.0F;
    }
    const auto encode_reference = [&output](const std::size_t offset,
                                             const std::optional<Phase6BcCandidateReferenceV1>& reference) {
        output[offset] = reference.has_value() ? 1.0F : 0.0F;
        if (!reference.has_value()) return;
        if (reference->locator_namespace != Phase6BcLocatorNamespace::State &&
            reference->locator_namespace != Phase6BcLocatorNamespace::CandidateOnly) {
            throw std::invalid_argument("numeric projection received an unknown locator namespace");
        }
        if (reference->locator_namespace == Phase6BcLocatorNamespace::CandidateOnly &&
            reference->current_entity_ordinal.has_value()) {
            throw std::invalid_argument("candidate-only reference has a current entity ordinal");
        }
        output[offset + 1] = normalized_u8(reference->kind_code);
        output[offset + 2] = reference->locator_namespace == Phase6BcLocatorNamespace::State
                                 ? 0.0F
                                 : 1.0F;
        output[offset + 3] = normalized_u32(reference->locator_ordinal);
        output[offset + 4] = reference->current_entity_ordinal.has_value() ? 1.0F : 0.0F;
        output[offset + 5] = reference->current_entity_ordinal.has_value()
                                 ? normalized_u32(*reference->current_entity_ordinal)
                                 : 0.0F;
    };
    encode_reference(7, candidate.source_reference);
    encode_reference(13, candidate.target_reference);
    output[19] = candidate.phase.has_value() ? 1.0F : 0.0F;
    output[20] = candidate.phase.has_value() ? normalized_u32(*candidate.phase) : 0.0F;
    output[21] = candidate.position.has_value() ? 1.0F : 0.0F;
    output[22] = candidate.position.has_value() ? normalized_u8(*candidate.position) : 0.0F;
    output[23] = candidate.source_index.has_value() ? 1.0F : 0.0F;
    output[24] = candidate.source_index.has_value() ? normalized_u32(*candidate.source_index) : 0.0F;
    output[25] = candidate.amount.has_value() ? 1.0F : 0.0F;
    output[26] = candidate.amount.has_value()
                     ? normalized_u32(static_cast<std::uint32_t>(*candidate.amount))
                     : 0.0F;
    output[27] = normalized_u16(static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(candidate.continuation_operation_code) * 2U +
        (candidate.submits_engine_response ? 1U : 0U)));
    return output;
}

}  // namespace ygo::phase6::task4
