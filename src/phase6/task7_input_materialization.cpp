#include "ygo/phase6/task7_input_materialization.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <limits>
#include <optional>
#include <set>
#include <string>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

#include "ygo/trace/sha256.hpp"
#include "ygo/trajectory/codec.hpp"
#include "ygo/environment/public_action_identity.hpp"

namespace ygo::phase6 {
namespace {

using ygo::trajectory::ByteWriter;

struct ReferenceComponentDescriptor final {
    std::string_view name;
    std::string_view source_type;
    std::string_view presence_rule;
};

struct ReferenceDescriptor final {
    std::string_view name;
    std::vector<ReferenceComponentDescriptor> components;
};

struct ColumnDescriptor final {
    std::string_view name;
    std::string_view source_type;
    std::uint8_t limb_count;
    std::string_view presence_rule;
    std::string_view padding_rule;
};

struct TableDescriptor final {
    std::string_view name;
    std::string_view kind;
    std::string_view row_order;
    std::optional<std::string_view> parent;
    std::optional<std::string_view> parent_offset;
    std::string_view row_mask_rule;
    std::vector<ColumnDescriptor> columns;
};

struct RuleDescriptor final {
    std::string_view id;
    std::string_view value;
};

ColumnDescriptor column(const std::string_view name,
                        const std::string_view source_type,
                        const std::uint8_t limb_count,
                        const std::string_view presence,
                        const std::string_view padding) {
    return {name, source_type, limb_count, presence, padding};
}

std::vector<ReferenceDescriptor> reference_descriptors() {
    return {
        {"R", {{"public_locator_ordinal", "U32", "required"},
                {"current_entity_ordinal", "P<U32>", "optional"}}},
        {"OR", {{"present", "Bool", "required"},
                 {"reference", "R", "composite_defined"}}},
        {"CR", {{"present", "Bool", "required"},
                 {"kind_code", "U8", "composite_defined"},
                 {"reference", "R", "composite_defined"}}},
        {"HR", {{"present", "Bool", "required"},
                 {"public_locator_ordinal", "U32", "composite_defined"}}},
    };
}

std::vector<TableDescriptor> table_descriptors() {
    constexpr std::string_view required = "required";
    constexpr std::string_view optional = "optional";
    constexpr std::string_view composite = "composite_defined";
    constexpr std::string_view zero = "zero";
    constexpr std::string_view false_padding = "false";
    constexpr std::string_view pad_id = "pad_id_zero";
    constexpr std::string_view not_applicable = "not_applicable";
    constexpr std::string_view singleton_mask = "singleton_all_true";
    constexpr std::string_view real_mask = "real_rows_true";

    return {
        {"sample_header", "singleton", "sample_order", std::nullopt,
         std::nullopt, singleton_mask,
         {column("perspective_player", "U8", 1, required, zero),
          column("decision_index", "U64", 4, required, zero),
          column("public_observation_context_kind_code", "P<U16>", 1,
                 optional, zero),
          column("public_observation_context_player", "P<U8>", 1,
                 optional, zero),
          column("public_locator_count", "U32", 2, required, zero),
          column("candidate_count", "U32", 2, required, zero)}},
        {"globals", "singleton", "sample_order", std::nullopt, std::nullopt,
         singleton_mask,
         {column("duel_flags", "U64", 4, required, zero),
          column("player_to_act", "P<U8>", 1, optional, zero),
          column("turn_player", "P<U8>", 1, optional, zero),
          column("turn_count", "P<U32>", 2, optional, zero),
          column("phase", "P<U32>", 2, optional, zero),
          column("chain_length", "U32", 2, required, zero),
          column("winner", "P<U8>", 1, optional, zero),
          column("win_reason", "P<U8>", 1, optional, zero),
          column("terminal", "Bool", 0, required, false_padding)}},
        {"chain_state", "singleton", "sample_order", std::nullopt,
         std::nullopt, singleton_mask,
         {column("length", "U32", 2, required, zero)}},
        {"match_context", "singleton", "sample_order", std::nullopt,
         std::nullopt, singleton_mask,
         {column("perspective_player", "U8", 1, required, zero),
          column("duel_flags", "U64", 4, required, zero),
          column("own_decklist_known", "Bool", 0, required, false_padding),
          column("opponent_decklist_known", "Bool", 0, required,
                 false_padding),
          column("own_deck_known", "Bool", 0, required, false_padding),
          column("opponent_deck_known", "Bool", 0, required,
                 false_padding)}},
        {"life_points", "ragged", "life_point_source_order", std::nullopt,
         std::nullopt, real_mask,
         {column("value", "U32", 2, required, zero)}},
        {"decision_context_references", "ragged",
         "public_observation_context_reference_order", std::nullopt,
         std::nullopt, real_mask,
         {column("public_locator_ordinal", "U32", 2, required, zero)}},
        {"zones", "ragged", "public_safe_state_zone_order", std::nullopt,
         std::nullopt, real_mask,
         {column("player", "U8", 1, required, zero),
          column("kind_code", "U8", 1, required, zero),
          column("total_count", "U32", 2, required, zero),
          column("public_identity_count", "U32", 2, required, zero),
          column("hidden_count", "U32", 2, required, zero),
          column("player_observable_order", "Bool", 0, required,
                 false_padding)}},
        {"entities", "ragged", "canonical_locator_order", std::nullopt,
         std::nullopt, real_mask,
         {column("public_locator_ordinal", "U32", 2, required, zero),
          column("identity_known", "Bool", 0, required, false_padding),
          column("card_vocabulary_id", "U32", 2, required, pad_id),
          column("owner", "P<U8>", 1, optional, zero),
          column("controller", "P<U8>", 1, optional, zero),
          column("zone_code", "U8", 1, required, zero),
          column("sequence", "P<U32>", 2, optional, zero),
          column("overlay_sequence", "P<U32>", 2, optional, zero),
          column("position_code", "U8", 1, required, zero),
          column("face_up", "Bool", 0, required, false_padding),
          column("face_down", "Bool", 0, required, false_padding)}},
        {"entity_properties", "child", "entity_property_role_order",
         std::string_view{"entities"},
         std::string_view{"entity_property_offsets"}, real_mask,
         {column("property_role", "U8", 1, required, zero),
          column("property_present", "Bool", 0, required, false_padding),
          column("type", "P<U32>", 2, optional, zero),
          column("attribute", "P<U32>", 2, optional, zero),
          column("race", "P<U64>", 4, optional, zero),
          column("attack", "P<I32>", 2, optional, zero),
          column("defense", "P<I32>", 2, optional, zero),
          column("base_attack", "P<I32>", 2, optional, zero),
          column("base_defense", "P<I32>", 2, optional, zero),
          column("level", "P<U32>", 2, optional, zero),
          column("rank", "P<U32>", 2, optional, zero),
          column("link_rating", "P<U32>", 2, optional, zero),
          column("left_scale", "P<U32>", 2, optional, zero),
          column("right_scale", "P<U32>", 2, optional, zero),
          column("status_flags", "P<U32>", 2, optional, zero)}},
        {"property_link_markers", "child",
         "property_link_marker_source_order", std::string_view{"entity_properties"},
         std::string_view{"property_link_marker_offsets"}, real_mask,
         {column("link_marker_code", "U8", 1, required, zero)}},
        {"property_counters", "child", "property_counter_source_order",
         std::string_view{"entity_properties"},
         std::string_view{"property_counter_offsets"}, real_mask,
         {column("type", "U32", 2, required, zero),
          column("count", "U32", 2, required, zero)}},
        {"relationships", "ragged", "relationship_source_order", std::nullopt,
         std::nullopt, real_mask,
         {column("kind_code", "U8", 1, required, zero),
          column("source", "R", 0, composite, not_applicable),
          column("target", "R", 0, composite, not_applicable)}},
        {"chain_links", "ragged", "chain_link_source_order", std::nullopt,
         std::nullopt, real_mask,
         {column("index", "U32", 2, required, zero),
          column("activating_player", "P<U8>", 1, optional, zero),
          column("source", "OR", 0, composite, not_applicable),
          column("activation_zone_code", "P<U8>", 1, optional, zero),
          column("effect_description", "P<U64>", 4, optional, zero)}},
        {"chain_targets", "child", "chain_target_source_order",
         std::string_view{"chain_links"}, std::string_view{"chain_target_offsets"},
         real_mask,
         {column("target", "R", 0, composite, not_applicable)}},
        {"visible_events", "ragged", "visible_event_source_order", std::nullopt,
         std::nullopt, real_mask,
         {column("event_index", "U64", 4, required, zero),
          column("kind_code", "U8", 1, required, zero),
          column("player", "P<U8>", 1, optional, zero),
          column("entity", "HR", 0, composite, not_applicable),
          column("public_card_vocabulary_id", "P<U32>", 2, optional, zero),
          column("from_zone_code", "P<U8>", 1, optional, zero),
          column("to_zone_code", "P<U8>", 1, optional, zero),
          column("count", "P<U32>", 2, optional, zero),
          column("amount", "P<I32>", 2, optional, zero),
          column("counter_type", "P<U32>", 2, optional, zero),
          column("phase", "P<U32>", 2, optional, zero),
          column("winner", "P<U8>", 1, optional, zero),
          column("win_reason", "P<U8>", 1, optional, zero),
          column("effect_description", "P<U64>", 4, optional, zero)}},
        {"visible_event_targets", "child",
         "visible_event_target_source_order", std::string_view{"visible_events"},
         std::string_view{"visible_event_target_offsets"}, real_mask,
         {column("public_locator_ordinal", "U32", 2, required, zero)}},
        {"own_main_deck_ids", "ragged", "deck_public_safe_order", std::nullopt,
         std::nullopt, real_mask,
         {column("card_vocabulary_id", "U32", 2, required, pad_id)}},
        {"opponent_main_deck_ids", "ragged", "deck_public_safe_order",
         std::nullopt, std::nullopt, real_mask,
         {column("card_vocabulary_id", "U32", 2, required, pad_id)}},
        {"own_extra_deck_ids", "ragged", "deck_public_safe_order", std::nullopt,
         std::nullopt, real_mask,
         {column("card_vocabulary_id", "U32", 2, required, pad_id)}},
        {"opponent_extra_deck_ids", "ragged", "deck_public_safe_order",
         std::nullopt, std::nullopt, real_mask,
         {column("card_vocabulary_id", "U32", 2, required, pad_id)}},
        {"public_locator_control_sidecar", "control_sidecar",
         "public_locator_token_order", std::nullopt, std::nullopt, real_mask,
         {column("public_locator_token", "String", 0, required,
                 not_applicable)}},
        {"candidates", "candidate", "candidate_source_order", std::nullopt,
         std::nullopt, real_mask,
         {column("action_kind_code", "U16", 1, required, zero),
          column("choice_present", "Bool", 0, required, false_padding),
          column("choice_kind_code", "U8", 1, required, zero),
          column("choice_value", "U64", 4, required, zero),
          column("choice_response_index", "P<U32>", 2, optional, zero),
          column("source_reference", "CR", 0, composite, not_applicable),
          column("target_reference", "CR", 0, composite, not_applicable),
          column("phase", "P<U32>", 2, optional, zero),
          column("position", "P<U8>", 1, optional, zero),
          column("source_index", "P<U32>", 2, optional, zero),
          column("amount", "P<I32>", 2, optional, zero),
          column("continuation_operation_code", "U8", 1, required, zero),
          column("submits_engine_response", "Bool", 0, required,
                 false_padding)}},
        {"routing_key_control_sidecar", "control_sidecar",
         "candidate_source_order", std::nullopt, std::nullopt, real_mask,
         {column("public_action_key", "String", 0, required,
                 not_applicable)}},
    };
}

std::vector<RuleDescriptor> rule_descriptors() {
    return {
        {"candidate_cardinality", "N_TO_N"},
        {"candidate_order", "SOURCE_ORDER"},
        {"candidate_split", "FORBIDDEN"},
        {"routing_key_learned_feature", "NO"},
        {"raw_locator_learned_feature", "NO"},
        {"padding_semantic", "NO"},
        {"ragged_authority", "RAGGED_FIRST"},
        {"padded_equivalence", "EXACT_UNPAD"},
        {"globals_chain_length_source", "DISTINCT"},
        {"chain_state_length_source", "DISTINCT"},
    };
}

template <typename T, typename Write>
void write_vector(ByteWriter& writer, const std::vector<T>& values,
                  const Write& write) {
    if (values.size() > std::numeric_limits<std::uint32_t>::max()) {
        throw std::length_error("Task7 descriptor vector exceeds u32");
    }
    writer.u32be(static_cast<std::uint32_t>(values.size()));
    for (const auto& value : values) write(writer, value);
}

void write_optional_string(ByteWriter& writer,
                           const std::optional<std::string_view>& value) {
    writer.u8(value.has_value() ? 1 : 0);
    if (value.has_value()) writer.string(*value);
}

void write_reference_descriptor(ByteWriter& writer,
                                const ReferenceDescriptor& descriptor) {
    writer.string(descriptor.name);
    write_vector(writer, descriptor.components,
                 [](ByteWriter& output,
                    const ReferenceComponentDescriptor& component) {
                     output.string(component.name);
                     output.string(component.source_type);
                     output.string(component.presence_rule);
                 });
}

void write_table_descriptor(ByteWriter& writer,
                            const TableDescriptor& descriptor) {
    writer.string(descriptor.name);
    writer.string(descriptor.kind);
    writer.string(descriptor.row_order);
    write_optional_string(writer, descriptor.parent);
    write_optional_string(writer, descriptor.parent_offset);
    writer.string(descriptor.row_mask_rule);
    write_vector(writer, descriptor.columns,
                 [](ByteWriter& output, const ColumnDescriptor& column) {
                     output.string(column.name);
                     output.string(column.source_type);
                     output.u8(column.limb_count);
                     output.string(column.presence_rule);
                     output.string(column.padding_rule);
                 });
}

void write_rule_descriptor(ByteWriter& writer, const RuleDescriptor& descriptor) {
    writer.string(descriptor.id);
    writer.string(descriptor.value);
}

void write_string_vector(ByteWriter& writer,
                         const std::vector<std::string_view>& values) {
    write_vector(writer, values,
                 [](ByteWriter& output, const std::string_view value) {
                     output.string(value);
                 });
}

void write_u64_vector(ByteWriter& writer,
                      const std::vector<std::uint64_t>& values) {
    write_vector(writer, values,
                 [](ByteWriter& output, const std::uint64_t value) {
                     output.u64be(value);
                 });
}

void write_bool_vector(ByteWriter& writer, const std::size_t count,
                       const bool value) {
    if (count > std::numeric_limits<std::uint32_t>::max()) {
        throw std::length_error("Task7 row mask exceeds u32");
    }
    writer.u32be(static_cast<std::uint32_t>(count));
    for (std::size_t index = 0; index < count; ++index) {
        writer.boolean(value);
    }
}

void write_u8_limb(ByteWriter& writer, const std::uint8_t value) {
    writer.u16be(value);
}

void write_u16_limb(ByteWriter& writer, const std::uint16_t value) {
    writer.u16be(value);
}

void write_u32_limbs(ByteWriter& writer, const std::uint32_t value) {
    const auto limbs = task7_u32_limbs(value);
    for (const auto limb : limbs) writer.u16be(limb);
}

void write_u64_limbs(ByteWriter& writer, const std::uint64_t value) {
    const auto limbs = task7_u64_limbs(value);
    for (const auto limb : limbs) writer.u16be(limb);
}

void write_i32_limbs(ByteWriter& writer, const std::int32_t value) {
    const auto limbs = task7_i32_limbs(value);
    for (const auto limb : limbs) writer.u16be(limb);
}

void write_optional_u8(ByteWriter& writer,
                       const std::optional<std::uint8_t>& value) {
    writer.boolean(value.has_value());
    if (value.has_value()) write_u8_limb(writer, *value);
}

void write_optional_u16(ByteWriter& writer,
                        const std::optional<std::uint16_t>& value) {
    writer.boolean(value.has_value());
    if (value.has_value()) write_u16_limb(writer, *value);
}

void write_optional_u32(ByteWriter& writer,
                        const std::optional<std::uint32_t>& value) {
    writer.boolean(value.has_value());
    if (value.has_value()) write_u32_limbs(writer, *value);
}

void write_optional_u64(ByteWriter& writer,
                        const std::optional<std::uint64_t>& value) {
    writer.boolean(value.has_value());
    if (value.has_value()) write_u64_limbs(writer, *value);
}

void write_optional_i32(ByteWriter& writer,
                        const std::optional<std::int32_t>& value) {
    writer.boolean(value.has_value());
    if (value.has_value()) write_i32_limbs(writer, *value);
}

void write_current_reference(ByteWriter& writer,
                             const model::EncodedCurrentReference& value) {
    write_u32_limbs(writer, value.public_locator_ordinal);
    write_optional_u32(writer, value.current_entity_ordinal);
}

void write_optional_current_reference(
    ByteWriter& writer,
    const std::optional<model::EncodedCurrentReference>& value) {
    writer.boolean(value.has_value());
    if (value.has_value()) write_current_reference(writer, *value);
}

void write_optional_card_reference(
    ByteWriter& writer,
    const std::optional<model::EncodedCardReference>& value) {
    writer.boolean(value.has_value());
    if (value.has_value()) {
        write_u8_limb(writer, value->kind_code);
        write_current_reference(writer, value->reference);
    }
}

void write_historical_reference(
    ByteWriter& writer,
    const std::optional<std::uint32_t>& value) {
    writer.boolean(value.has_value());
    if (value.has_value()) write_u32_limbs(writer, *value);
}

void require_u32_count(const std::size_t value, const char* message) {
    if (value > std::numeric_limits<std::uint32_t>::max()) {
        throw std::length_error(message);
    }
}

void require_executable_offset(const std::uint64_t value) {
    if (value > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
        throw std::overflow_error("Task7 offset exceeds executable range");
    }
}

std::uint64_t checked_offset_add(const std::uint64_t base,
                                 const std::size_t amount) {
    if (amount > std::numeric_limits<std::uint64_t>::max() - base) {
        throw std::overflow_error("Task7 offset exceeds u64 range");
    }
    return base + static_cast<std::uint64_t>(amount);
}

std::size_t checked_size_add(const std::size_t base, const std::size_t amount) {
    if (amount > std::numeric_limits<std::size_t>::max() - base) {
        throw std::overflow_error("Task7 collection size exceeds executable range");
    }
    return base + amount;
}

std::size_t checked_size_double(const std::size_t value) {
    if (value > std::numeric_limits<std::size_t>::max() / 2) {
        throw std::overflow_error("Task7 collection size exceeds executable range");
    }
    return value * 2;
}

void validate_offset_vector(const std::vector<std::uint64_t>& offsets,
                            const std::size_t flat_size) {
    if (offsets.empty() || offsets.front() != 0 ||
        offsets.back() != static_cast<std::uint64_t>(flat_size)) {
        throw std::invalid_argument("Task7 ragged offsets are invalid");
    }
    if (offsets.size() == 1 && flat_size != 0) {
        throw std::invalid_argument("Task7 ragged offsets are invalid");
    }
    for (std::size_t index = 0; index < offsets.size(); ++index) {
        require_executable_offset(offsets[index]);
        if (index != 0 && offsets[index - 1] > offsets[index]) {
            throw std::invalid_argument("Task7 ragged offsets are not monotonic");
        }
    }
}

void validate_all_offsets(const model::RaggedModelBatchV1& ragged) {
    validate_offset_vector(ragged.candidate_offsets, ragged.candidate_rows.size());
    validate_offset_vector(ragged.zone_offsets, ragged.zones.size());
    validate_offset_vector(ragged.entity_offsets, ragged.entities.size());
    validate_offset_vector(ragged.relationship_offsets, ragged.relationships.size());
    validate_offset_vector(ragged.chain_link_offsets, ragged.chain_links.size());
    validate_offset_vector(ragged.visible_event_offsets, ragged.visible_events.size());
    validate_offset_vector(ragged.decision_context_reference_offsets,
                           ragged.decision_context_reference_ordinals.size());
    validate_offset_vector(ragged.public_locator_token_offsets,
                           ragged.public_locator_tokens.size());
    validate_offset_vector(ragged.life_point_offsets, ragged.life_points.size());
    validate_offset_vector(ragged.own_deck_passcode_offsets,
                           ragged.own_deck_passcode_ids.size());
    validate_offset_vector(ragged.opponent_deck_passcode_offsets,
                           ragged.opponent_deck_passcode_ids.size());
    validate_offset_vector(ragged.own_extra_deck_passcode_offsets,
                           ragged.own_extra_deck_passcode_ids.size());
    validate_offset_vector(ragged.opponent_extra_deck_passcode_offsets,
                           ragged.opponent_extra_deck_passcode_ids.size());
}

std::vector<std::uint64_t> sample_offsets(const std::size_t row_count) {
    require_u32_count(row_count, "Task7 sample row count exceeds u32");
    return {0, static_cast<std::uint64_t>(row_count)};
}

std::vector<std::uint64_t> property_offsets(
    const model::EncodedModelInputV1& encoded,
    const bool link_markers) {
    std::vector<std::uint64_t> offsets;
    offsets.reserve(checked_size_add(checked_size_double(encoded.entities.size()), 1));
    offsets.push_back(0);
    for (const auto& entity : encoded.entities) {
        const std::array<const std::optional<model::EncodedCardProperties>*, 2> properties = {
            &entity.printed, &entity.current};
        for (const auto* value : properties) {
            const auto count = value->has_value()
                                   ? (link_markers
                                          ? value->value().link_marker_codes.size()
                                          : value->value().counters.size())
                                   : 0;
            offsets.push_back(checked_offset_add(offsets.back(), count));
        }
    }
    return offsets;
}

std::vector<std::uint64_t> entity_property_offsets(
    const model::EncodedModelInputV1& encoded) {
    std::vector<std::uint64_t> offsets;
    offsets.reserve(encoded.entities.size() + 1);
    offsets.push_back(0);
    for (std::size_t index = 0; index < encoded.entities.size(); ++index) {
        offsets.push_back(checked_offset_add(offsets.back(), 2));
    }
    return offsets;
}

std::vector<std::uint64_t> chain_target_offsets(
    const model::EncodedModelInputV1& encoded) {
    std::vector<std::uint64_t> offsets;
    offsets.reserve(encoded.chain.links.size() + 1);
    offsets.push_back(0);
    for (const auto& link : encoded.chain.links) {
        offsets.push_back(checked_offset_add(offsets.back(), link.targets.size()));
    }
    return offsets;
}

std::vector<std::uint64_t> visible_event_target_offsets(
    const model::EncodedModelInputV1& encoded) {
    std::vector<std::uint64_t> offsets;
    offsets.reserve(encoded.visible_events.size() + 1);
    offsets.push_back(0);
    for (const auto& event : encoded.visible_events) {
        offsets.push_back(checked_offset_add(
            offsets.back(), event.target_public_locator_ordinals.size()));
    }
    return offsets;
}

const std::optional<model::EncodedCardProperties>& property_at(
    const model::EncodedModelInputV1& encoded, const std::size_t row,
    std::uint8_t& role) {
    if (row >= checked_size_double(encoded.entities.size())) {
        throw std::out_of_range("Task7 property row is out of range");
    }
    const auto entity_index = row / 2;
    role = row % 2 == 0 ? 1 : 2;
    return role == 1 ? encoded.entities[entity_index].printed
                     : encoded.entities[entity_index].current;
}

std::uint8_t property_link_marker_at(const model::EncodedModelInputV1& encoded,
                                     std::size_t row) {
    for (const auto& entity : encoded.entities) {
        const std::array<const std::optional<model::EncodedCardProperties>*, 2> properties = {
            &entity.printed, &entity.current};
        for (const auto* property : properties) {
            if (!property->has_value()) continue;
            if (row < property->value().link_marker_codes.size()) {
                return property->value().link_marker_codes[row];
            }
            row -= property->value().link_marker_codes.size();
        }
    }
    throw std::out_of_range("Task7 link-marker row is out of range");
}

model::EncodedCardProperties::Counter counter_at(
    const model::EncodedModelInputV1& encoded, std::size_t row) {
    for (const auto& entity : encoded.entities) {
        const std::array<const std::optional<model::EncodedCardProperties>*, 2> properties = {
            &entity.printed, &entity.current};
        for (const auto* property : properties) {
            if (!property->has_value()) continue;
            if (row < property->value().counters.size()) {
                return property->value().counters[row];
            }
            row -= property->value().counters.size();
        }
    }
    throw std::out_of_range("Task7 counter row is out of range");
}

model::EncodedCurrentReference chain_target_at(
    const model::EncodedModelInputV1& encoded, std::size_t row) {
    for (const auto& link : encoded.chain.links) {
        if (row < link.targets.size()) return link.targets[row];
        row -= link.targets.size();
    }
    throw std::out_of_range("Task7 chain-target row is out of range");
}

std::uint32_t visible_event_target_at(
    const model::EncodedModelInputV1& encoded, std::size_t row) {
    for (const auto& event : encoded.visible_events) {
        if (row < event.target_public_locator_ordinals.size()) {
            return event.target_public_locator_ordinals[row];
        }
        row -= event.target_public_locator_ordinals.size();
    }
    throw std::out_of_range("Task7 visible-event-target row is out of range");
}

std::size_t table_row_count(const TableDescriptor& table,
                            const model::EncodedModelInputV1& encoded) {
    if (table.name == "sample_header" || table.name == "globals" ||
        table.name == "chain_state" || table.name == "match_context") {
        return 1;
    }
    if (table.name == "life_points") return encoded.globals.life_points.size();
    if (table.name == "decision_context_references") {
        return encoded.observation_context_reference_ordinals.size();
    }
    if (table.name == "zones") return encoded.zones.size();
    if (table.name == "entities") return encoded.entities.size();
    if (table.name == "entity_properties") return checked_size_double(encoded.entities.size());
    if (table.name == "property_link_markers") {
        std::size_t count = 0;
        for (const auto& entity : encoded.entities) {
            if (entity.printed.has_value()) {
                count = checked_size_add(count, entity.printed->link_marker_codes.size());
            }
            if (entity.current.has_value()) {
                count = checked_size_add(count, entity.current->link_marker_codes.size());
            }
        }
        return count;
    }
    if (table.name == "property_counters") {
        std::size_t count = 0;
        for (const auto& entity : encoded.entities) {
            if (entity.printed.has_value()) {
                count = checked_size_add(count, entity.printed->counters.size());
            }
            if (entity.current.has_value()) {
                count = checked_size_add(count, entity.current->counters.size());
            }
        }
        return count;
    }
    if (table.name == "relationships") return encoded.relationships.size();
    if (table.name == "chain_links") return encoded.chain.links.size();
    if (table.name == "chain_targets") {
        std::size_t count = 0;
        for (const auto& link : encoded.chain.links) {
            count = checked_size_add(count, link.targets.size());
        }
        return count;
    }
    if (table.name == "visible_events") return encoded.visible_events.size();
    if (table.name == "visible_event_targets") {
        std::size_t count = 0;
        for (const auto& event : encoded.visible_events) {
            count = checked_size_add(count, event.target_public_locator_ordinals.size());
        }
        return count;
    }
    if (table.name == "own_main_deck_ids") {
        return encoded.match_context.own_deck.main_deck.size();
    }
    if (table.name == "opponent_main_deck_ids") {
        return encoded.match_context.opponent_deck.main_deck.size();
    }
    if (table.name == "own_extra_deck_ids") {
        return encoded.match_context.own_deck.extra_deck.size();
    }
    if (table.name == "opponent_extra_deck_ids") {
        return encoded.match_context.opponent_deck.extra_deck.size();
    }
    if (table.name == "public_locator_control_sidecar") {
        return encoded.public_locator_table.size();
    }
    if (table.name == "candidates") return encoded.candidate_features.size();
    if (table.name == "routing_key_control_sidecar") return encoded.routing_keys.size();
    throw std::invalid_argument("Task7 table identity is unknown");
}

std::vector<std::uint64_t> child_offsets_for(
    const TableDescriptor& table, const model::EncodedModelInputV1& encoded) {
    if (table.name == "entity_properties") return entity_property_offsets(encoded);
    if (table.name == "property_link_markers") return property_offsets(encoded, true);
    if (table.name == "property_counters") return property_offsets(encoded, false);
    if (table.name == "chain_targets") return chain_target_offsets(encoded);
    if (table.name == "visible_event_targets") {
        return visible_event_target_offsets(encoded);
    }
    return {};
}

void write_property_column(
    ByteWriter& writer, const std::optional<model::EncodedCardProperties>& property,
    const std::string_view column_name) {
    if (column_name == "property_present") {
        writer.boolean(property.has_value());
        return;
    }
    if (!property.has_value()) {
        if (column_name == "type" || column_name == "attribute" ||
            column_name == "race" || column_name == "attack" ||
            column_name == "defense" || column_name == "base_attack" ||
            column_name == "base_defense" || column_name == "level" ||
            column_name == "rank" || column_name == "link_rating" ||
            column_name == "left_scale" || column_name == "right_scale" ||
            column_name == "status_flags") {
            writer.boolean(false);
            return;
        }
        throw std::invalid_argument("Task7 property column is unknown");
    }
    const auto& value = *property;
    if (column_name == "type") {
        write_optional_u32(writer, value.type);
    } else if (column_name == "attribute") {
        write_optional_u32(writer, value.attribute);
    } else if (column_name == "race") {
        write_optional_u64(writer, value.race);
    } else if (column_name == "attack") {
        write_optional_i32(writer, value.attack);
    } else if (column_name == "defense") {
        write_optional_i32(writer, value.defense);
    } else if (column_name == "base_attack") {
        write_optional_i32(writer, value.base_attack);
    } else if (column_name == "base_defense") {
        write_optional_i32(writer, value.base_defense);
    } else if (column_name == "level") {
        write_optional_u32(writer, value.level);
    } else if (column_name == "rank") {
        write_optional_u32(writer, value.rank);
    } else if (column_name == "link_rating") {
        write_optional_u32(writer, value.link_rating);
    } else if (column_name == "left_scale") {
        write_optional_u32(writer, value.left_scale);
    } else if (column_name == "right_scale") {
        write_optional_u32(writer, value.right_scale);
    } else if (column_name == "status_flags") {
        write_optional_u32(writer, value.status_flags);
    } else {
        throw std::invalid_argument("Task7 property column is unknown");
    }
}

void write_encoded_column(ByteWriter& writer, const std::string_view table_name,
                          const std::string_view column_name,
                          const model::EncodedModelInputV1& encoded,
                          const std::size_t row) {
    if (table_name == "sample_header") {
        if (column_name == "perspective_player") {
            write_u8_limb(writer, encoded.perspective_player);
        } else if (column_name == "decision_index") {
            write_u64_limbs(writer, encoded.decision_index);
        } else if (column_name == "public_observation_context_kind_code") {
            write_optional_u16(writer, encoded.public_observation_context_kind_code);
        } else if (column_name == "public_observation_context_player") {
            write_optional_u8(writer, encoded.public_observation_context_player);
        } else if (column_name == "public_locator_count") {
            write_u32_limbs(writer,
                            static_cast<std::uint32_t>(encoded.public_locator_table.size()));
        } else if (column_name == "candidate_count") {
            write_u32_limbs(writer,
                            static_cast<std::uint32_t>(encoded.candidate_features.size()));
        } else {
            throw std::invalid_argument("Task7 sample-header column is unknown");
        }
        return;
    }
    if (table_name == "globals") {
        if (column_name == "duel_flags") write_u64_limbs(writer, encoded.globals.duel_flags);
        else if (column_name == "player_to_act") write_optional_u8(writer, encoded.globals.player_to_act);
        else if (column_name == "turn_player") write_optional_u8(writer, encoded.globals.turn_player);
        else if (column_name == "turn_count") write_optional_u32(writer, encoded.globals.turn_count);
        else if (column_name == "phase") write_optional_u32(writer, encoded.globals.phase);
        else if (column_name == "chain_length") write_u32_limbs(writer, encoded.globals.chain_length);
        else if (column_name == "winner") write_optional_u8(writer, encoded.globals.winner);
        else if (column_name == "win_reason") write_optional_u8(writer, encoded.globals.win_reason);
        else if (column_name == "terminal") writer.boolean(encoded.globals.terminal);
        else throw std::invalid_argument("Task7 globals column is unknown");
        return;
    }
    if (table_name == "chain_state") {
        if (column_name != "length") throw std::invalid_argument("Task7 chain-state column is unknown");
        write_u32_limbs(writer, encoded.chain.length);
        return;
    }
    if (table_name == "match_context") {
        if (column_name == "perspective_player") write_u8_limb(writer, encoded.match_context.perspective_player);
        else if (column_name == "duel_flags") write_u64_limbs(writer, encoded.match_context.duel_flags);
        else if (column_name == "own_decklist_known") writer.boolean(encoded.match_context.own_decklist_known);
        else if (column_name == "opponent_decklist_known") writer.boolean(encoded.match_context.opponent_decklist_known);
        else if (column_name == "own_deck_known") writer.boolean(encoded.match_context.own_deck.known);
        else if (column_name == "opponent_deck_known") writer.boolean(encoded.match_context.opponent_deck.known);
        else throw std::invalid_argument("Task7 match-context column is unknown");
        return;
    }
    if (table_name == "life_points") {
        if (column_name != "value") throw std::invalid_argument("Task7 life-point column is unknown");
        write_u32_limbs(writer, encoded.globals.life_points.at(row));
        return;
    }
    if (table_name == "decision_context_references") {
        if (column_name != "public_locator_ordinal") throw std::invalid_argument("Task7 context-reference column is unknown");
        write_u32_limbs(writer, encoded.observation_context_reference_ordinals.at(row));
        return;
    }
    if (table_name == "zones") {
        const auto& value = encoded.zones.at(row);
        if (column_name == "player") write_u8_limb(writer, value.player);
        else if (column_name == "kind_code") write_u8_limb(writer, value.kind_code);
        else if (column_name == "total_count") write_u32_limbs(writer, value.total_count);
        else if (column_name == "public_identity_count") write_u32_limbs(writer, value.public_identity_count);
        else if (column_name == "hidden_count") write_u32_limbs(writer, value.hidden_count);
        else if (column_name == "player_observable_order") writer.boolean(value.player_observable_order);
        else throw std::invalid_argument("Task7 zone column is unknown");
        return;
    }
    if (table_name == "entities") {
        const auto& value = encoded.entities.at(row);
        if (column_name == "public_locator_ordinal") write_u32_limbs(writer, value.public_locator_ordinal);
        else if (column_name == "identity_known") writer.boolean(value.identity_known);
        else if (column_name == "card_vocabulary_id") write_u32_limbs(writer, value.card_vocabulary_id);
        else if (column_name == "owner") write_optional_u8(writer, value.owner);
        else if (column_name == "controller") write_optional_u8(writer, value.controller);
        else if (column_name == "zone_code") write_u8_limb(writer, value.zone_code);
        else if (column_name == "sequence") write_optional_u32(writer, value.sequence);
        else if (column_name == "overlay_sequence") write_optional_u32(writer, value.overlay_sequence);
        else if (column_name == "position_code") write_u8_limb(writer, value.position_code);
        else if (column_name == "face_up") writer.boolean(value.face_up);
        else if (column_name == "face_down") writer.boolean(value.face_down);
        else throw std::invalid_argument("Task7 entity column is unknown");
        return;
    }
    if (table_name == "entity_properties") {
        std::uint8_t role = 0;
        const auto& property = property_at(encoded, row, role);
        if (column_name == "property_role") write_u8_limb(writer, role);
        else write_property_column(writer, property, column_name);
        return;
    }
    if (table_name == "property_link_markers") {
        if (column_name != "link_marker_code") throw std::invalid_argument("Task7 link-marker column is unknown");
        write_u8_limb(writer, property_link_marker_at(encoded, row));
        return;
    }
    if (table_name == "property_counters") {
        const auto value = counter_at(encoded, row);
        if (column_name == "type") write_u32_limbs(writer, value.type);
        else if (column_name == "count") write_u32_limbs(writer, value.count);
        else throw std::invalid_argument("Task7 counter column is unknown");
        return;
    }
    if (table_name == "relationships") {
        const auto& value = encoded.relationships.at(row);
        if (column_name == "kind_code") write_u8_limb(writer, value.kind_code);
        else if (column_name == "source") write_current_reference(writer, value.source);
        else if (column_name == "target") write_current_reference(writer, value.target);
        else throw std::invalid_argument("Task7 relationship column is unknown");
        return;
    }
    if (table_name == "chain_links") {
        const auto& value = encoded.chain.links.at(row);
        if (column_name == "index") write_u32_limbs(writer, value.index);
        else if (column_name == "activating_player") write_optional_u8(writer, value.activating_player);
        else if (column_name == "source") write_optional_current_reference(writer, value.source);
        else if (column_name == "activation_zone_code") write_optional_u8(writer, value.activation_zone_code);
        else if (column_name == "effect_description") write_optional_u64(writer, value.effect_description);
        else throw std::invalid_argument("Task7 chain-link column is unknown");
        return;
    }
    if (table_name == "chain_targets") {
        if (column_name != "target") throw std::invalid_argument("Task7 chain-target column is unknown");
        write_current_reference(writer, chain_target_at(encoded, row));
        return;
    }
    if (table_name == "visible_events") {
        const auto& value = encoded.visible_events.at(row);
        if (column_name == "event_index") write_u64_limbs(writer, value.event_index);
        else if (column_name == "kind_code") write_u8_limb(writer, value.kind_code);
        else if (column_name == "player") write_optional_u8(writer, value.player);
        else if (column_name == "entity") write_historical_reference(writer, value.public_locator_ordinal);
        else if (column_name == "public_card_vocabulary_id") write_optional_u32(writer, value.public_card_vocabulary_id);
        else if (column_name == "from_zone_code") write_optional_u8(writer, value.from_zone_code);
        else if (column_name == "to_zone_code") write_optional_u8(writer, value.to_zone_code);
        else if (column_name == "count") write_optional_u32(writer, value.count);
        else if (column_name == "amount") write_optional_i32(writer, value.amount);
        else if (column_name == "counter_type") write_optional_u32(writer, value.counter_type);
        else if (column_name == "phase") write_optional_u32(writer, value.phase);
        else if (column_name == "winner") write_optional_u8(writer, value.winner);
        else if (column_name == "win_reason") write_optional_u8(writer, value.win_reason);
        else if (column_name == "effect_description") write_optional_u64(writer, value.effect_description);
        else throw std::invalid_argument("Task7 visible-event column is unknown");
        return;
    }
    if (table_name == "visible_event_targets") {
        if (column_name != "public_locator_ordinal") throw std::invalid_argument("Task7 visible-event-target column is unknown");
        write_u32_limbs(writer, visible_event_target_at(encoded, row));
        return;
    }
    if (table_name == "own_main_deck_ids") {
        if (column_name != "card_vocabulary_id") throw std::invalid_argument("Task7 own-main-deck column is unknown");
        write_u32_limbs(writer, encoded.match_context.own_deck.main_deck.at(row));
        return;
    }
    if (table_name == "opponent_main_deck_ids") {
        if (column_name != "card_vocabulary_id") throw std::invalid_argument("Task7 opponent-main-deck column is unknown");
        write_u32_limbs(writer, encoded.match_context.opponent_deck.main_deck.at(row));
        return;
    }
    if (table_name == "own_extra_deck_ids") {
        if (column_name != "card_vocabulary_id") throw std::invalid_argument("Task7 own-extra-deck column is unknown");
        write_u32_limbs(writer, encoded.match_context.own_deck.extra_deck.at(row));
        return;
    }
    if (table_name == "opponent_extra_deck_ids") {
        if (column_name != "card_vocabulary_id") throw std::invalid_argument("Task7 opponent-extra-deck column is unknown");
        write_u32_limbs(writer, encoded.match_context.opponent_deck.extra_deck.at(row));
        return;
    }
    if (table_name == "public_locator_control_sidecar") {
        if (column_name != "public_locator_token") throw std::invalid_argument("Task7 locator-sidecar column is unknown");
        writer.string(encoded.public_locator_table.at(row));
        return;
    }
    if (table_name == "candidates") {
        const auto& value = encoded.candidate_features.at(row);
        if (column_name == "action_kind_code") write_u16_limb(writer, value.action_kind_code);
        else if (column_name == "choice_present") writer.boolean(value.choice.has_value());
        else if (column_name == "choice_kind_code") write_u8_limb(writer, value.choice.has_value() ? value.choice->kind_code : 0);
        else if (column_name == "choice_value") write_u64_limbs(writer, value.choice.has_value() ? value.choice->value : 0);
        else if (column_name == "choice_response_index") write_optional_u32(writer, value.choice.has_value() ? value.choice->response_index : std::nullopt);
        else if (column_name == "source_reference") write_optional_card_reference(writer, value.source_reference);
        else if (column_name == "target_reference") write_optional_card_reference(writer, value.target_reference);
        else if (column_name == "phase") write_optional_u32(writer, value.phase);
        else if (column_name == "position") write_optional_u8(writer, value.position);
        else if (column_name == "source_index") write_optional_u32(writer, value.source_index);
        else if (column_name == "amount") write_optional_i32(writer, value.amount);
        else if (column_name == "continuation_operation_code") write_u8_limb(writer, value.continuation_operation_code);
        else if (column_name == "submits_engine_response") writer.boolean(value.submits_engine_response);
        else throw std::invalid_argument("Task7 candidate column is unknown");
        return;
    }
    if (table_name == "routing_key_control_sidecar") {
        if (column_name != "public_action_key") throw std::invalid_argument("Task7 routing-sidecar column is unknown");
        writer.string(encoded.routing_keys.at(row));
        return;
    }
    throw std::invalid_argument("Task7 table/column pair is unknown");
}

void write_table(ByteWriter& writer, const TableDescriptor& table,
                 const model::EncodedModelInputV1& encoded) {
    const auto rows = table_row_count(table, encoded);
    require_u32_count(rows, "Task7 table row count exceeds u32");
    writer.string(table.name);
    writer.u64be(static_cast<std::uint64_t>(rows));
    if (table.kind != "singleton") write_u64_vector(writer, sample_offsets(rows));
    if (table.parent_offset.has_value()) {
        const auto offsets = child_offsets_for(table, encoded);
        validate_offset_vector(offsets, rows);
        write_u64_vector(writer, offsets);
    }
    std::vector<std::string_view> column_names;
    column_names.reserve(table.columns.size());
    for (const auto& column : table.columns) column_names.push_back(column.name);
    write_string_vector(writer, column_names);
    for (const auto& column : table.columns) {
        for (std::size_t row = 0; row < rows; ++row) {
            write_encoded_column(writer, table.name, column.name, encoded, row);
        }
    }
    write_bool_vector(writer, rows, true);
}

std::vector<std::uint8_t> canonical_sample_bytes(
    const model::EncodedModelInputV1& encoded,
    const std::string_view model_input_identity,
    const std::string_view card_vocabulary_identity,
    const std::string_view configuration_identity) {
    ByteWriter writer;
    writer.string(kTask7MaterializationSchemaId);
    writer.string(configuration_identity);
    writer.string(model_input_identity);
    const std::vector<std::string_view> phase5_contracts = {
        "ocgforge.model_logical_input.v1",
        "ocgforge.model_encoded_input.v1",
        "ocgforge.model_card_vocabulary.v1",
        "ocgforge.model_input_identity.v1",
        "ocgforge.model_batch_layout.v1",
    };
    write_string_vector(writer, phase5_contracts);
    writer.string(card_vocabulary_identity);
    writer.string(encoded.public_observation_digest);
    writer.u8(encoded.public_candidate_domain_digest.has_value() ? 1 : 0);
    if (encoded.public_candidate_domain_digest.has_value()) {
        writer.string(*encoded.public_candidate_domain_digest);
    }
    for (const auto& table : table_descriptors()) write_table(writer, table, encoded);
    return std::move(writer).take();
}

void validate_executable_source(const Task7MaterializationSourceBatchV1& source) {
    if (source.ragged == nullptr || source.samples.empty() ||
        source.ragged->batch_size != source.samples.size()) {
        throw std::invalid_argument("Task7 source association is invalid");
    }
    const auto& ragged = *source.ragged;
    if (ragged.schema_id != model::kModelBatchLayoutSchemaId ||
        ragged.samples.size() != source.samples.size()) {
        throw std::invalid_argument("Task7 source schema is invalid");
    }
    validate_all_offsets(ragged);
    const auto expected_offset_count = source.samples.size() + 1;
    const auto require_batch_offsets = [expected_offset_count](
                                           const std::vector<std::uint64_t>& offsets) {
        if (offsets.size() != expected_offset_count) {
            throw std::invalid_argument("Task7 ragged offsets do not match batch size");
        }
    };
    require_batch_offsets(ragged.candidate_offsets);
    require_batch_offsets(ragged.zone_offsets);
    require_batch_offsets(ragged.entity_offsets);
    require_batch_offsets(ragged.relationship_offsets);
    require_batch_offsets(ragged.chain_link_offsets);
    require_batch_offsets(ragged.visible_event_offsets);
    require_batch_offsets(ragged.decision_context_reference_offsets);
    require_batch_offsets(ragged.public_locator_token_offsets);
    require_batch_offsets(ragged.life_point_offsets);
    require_batch_offsets(ragged.own_deck_passcode_offsets);
    require_batch_offsets(ragged.opponent_deck_passcode_offsets);
    require_batch_offsets(ragged.own_extra_deck_passcode_offsets);
    require_batch_offsets(ragged.opponent_extra_deck_passcode_offsets);
}

void validate_routing_sidecar(const model::RaggedModelBatchV1& ragged) {
    for (std::size_t sample = 0; sample + 1 < ragged.candidate_offsets.size();
         ++sample) {
        const auto begin = static_cast<std::size_t>(ragged.candidate_offsets[sample]);
        const auto end = static_cast<std::size_t>(ragged.candidate_offsets[sample + 1]);
        std::set<std::string> routing_keys;
        for (std::size_t index = begin; index < end; ++index) {
            const auto& key = ragged.candidate_routing_keys[index];
            if (!ygo::environment::is_public_action_key(key) ||
                !routing_keys.insert(key).second) {
                throw std::invalid_argument("Task7 routing sidecar is invalid");
            }
        }
    }
}

bool candidate_presence_matches(const model::CandidateOptionalPresenceV1& mask,
                                const model::EncodedCandidate& candidate) noexcept {
    return mask.choice <= 1 && mask.source_reference <= 1 &&
           mask.target_reference <= 1 && mask.phase <= 1 && mask.position <= 1 &&
           mask.source_index <= 1 && mask.amount <= 1 &&
           mask.choice == static_cast<std::uint8_t>(candidate.choice.has_value()) &&
           mask.source_reference ==
               static_cast<std::uint8_t>(candidate.source_reference.has_value()) &&
           mask.target_reference ==
               static_cast<std::uint8_t>(candidate.target_reference.has_value()) &&
           mask.phase == static_cast<std::uint8_t>(candidate.phase.has_value()) &&
           mask.position == static_cast<std::uint8_t>(candidate.position.has_value()) &&
           mask.source_index == static_cast<std::uint8_t>(candidate.source_index.has_value()) &&
           mask.amount == static_cast<std::uint8_t>(candidate.amount.has_value());
}

void validate_candidate_presence_masks(const model::RaggedModelBatchV1& ragged) {
    for (std::size_t index = 0; index < ragged.candidate_rows.size(); ++index) {
        if (!candidate_presence_matches(ragged.candidate_optional_presence_masks[index],
                                        ragged.candidate_rows[index])) {
            throw std::invalid_argument("Task7 candidate presence mask is invalid");
        }
    }
}

bool vocabulary_id_is_known(const model::CardVocabularyV1& vocabulary,
                            const std::uint32_t id) noexcept {
    if (id < 2) return false;
    const auto index = static_cast<std::uint64_t>(id) - 2;
    return index < vocabulary.ascending_passcodes().size();
}

bool encoded_vocabulary_ids_match(
    const model::EncodedModelInputV1& encoded,
    const model::CardVocabularyV1& vocabulary) noexcept {
    for (const auto& entity : encoded.entities) {
        if (entity.identity_known &&
            !vocabulary_id_is_known(vocabulary, entity.card_vocabulary_id)) {
            return false;
        }
    }
    const auto validate_deck = [&vocabulary](const model::EncodedDeck& deck) {
        for (const auto id : deck.main_deck) {
            if (!vocabulary_id_is_known(vocabulary, id)) return false;
        }
        for (const auto id : deck.extra_deck) {
            if (!vocabulary_id_is_known(vocabulary, id)) return false;
        }
        return true;
    };
    if (!validate_deck(encoded.match_context.own_deck) ||
        !validate_deck(encoded.match_context.opponent_deck)) {
        return false;
    }
    for (const auto& event : encoded.visible_events) {
        if (event.public_card_vocabulary_id.has_value() &&
            !vocabulary_id_is_known(vocabulary, *event.public_card_vocabulary_id)) {
            return false;
        }
    }
    return true;
}

}  // namespace

std::vector<std::uint8_t> canonical_task7_materialization_config_bytes() {
    ByteWriter writer;
    writer.string(kTask7MaterializationConfigSchemaId);
    writer.string(kTask7MaterializationSchemaId);
    const std::vector<std::string_view> phase5_contracts = {
        "ocgforge.model_logical_input.v1",
        "ocgforge.model_encoded_input.v1",
        "ocgforge.model_card_vocabulary.v1",
        "ocgforge.model_input_identity.v1",
        "ocgforge.model_batch_layout.v1",
    };
    write_vector(writer, phase5_contracts,
                 [](ByteWriter& output, const std::string_view value) {
                     output.string(value);
                 });
    writer.string("u16_most_significant_first");
    writer.string("torch.int64");
    writer.string("torch.bool");
    const auto references = reference_descriptors();
    write_vector(writer, references,
                 [](ByteWriter& output, const ReferenceDescriptor& value) {
                     write_reference_descriptor(output, value);
                 });
    const auto tables = table_descriptors();
    write_vector(writer, tables,
                 [](ByteWriter& output, const TableDescriptor& value) {
                     write_table_descriptor(output, value);
                 });
    const auto rules = rule_descriptors();
    write_vector(writer, rules,
                 [](ByteWriter& output, const RuleDescriptor& value) {
                     write_rule_descriptor(output, value);
                 });
    return std::move(writer).take();
}

std::string task7_materialization_config_identity() {
    return std::string(kTask7MaterializationConfigIdentityPrefix) +
           ygo::trace::sha256_bytes(canonical_task7_materialization_config_bytes());
}

std::array<std::uint16_t, 1> task7_u8_limbs(const std::uint8_t value) noexcept {
    return {value};
}

std::array<std::uint16_t, 1> task7_u16_limbs(const std::uint16_t value) noexcept {
    return {value};
}

std::array<std::uint16_t, 2> task7_u32_limbs(const std::uint32_t value) noexcept {
    return {static_cast<std::uint16_t>(value >> 16),
            static_cast<std::uint16_t>(value)};
}

std::array<std::uint16_t, 4> task7_u64_limbs(const std::uint64_t value) noexcept {
    return {static_cast<std::uint16_t>(value >> 48),
            static_cast<std::uint16_t>(value >> 32),
            static_cast<std::uint16_t>(value >> 16),
            static_cast<std::uint16_t>(value)};
}

std::array<std::uint16_t, 2> task7_i32_limbs(const std::int32_t value) noexcept {
    return task7_u32_limbs(static_cast<std::uint32_t>(value));
}

std::string_view task7_materialization_error_code_name(
    const Task7MaterializationErrorCode code) noexcept {
    switch (code) {
    case Task7MaterializationErrorCode::UnknownSchema: return "unknown_schema";
    case Task7MaterializationErrorCode::SourceAssociationMismatch: return "source_association_mismatch";
    case Task7MaterializationErrorCode::ModelInputIdentityMismatch: return "model_input_identity_mismatch";
    case Task7MaterializationErrorCode::CardVocabularyMismatch: return "card_vocabulary_mismatch";
    case Task7MaterializationErrorCode::RaggedReconstructionMismatch: return "ragged_reconstruction_mismatch";
    case Task7MaterializationErrorCode::InvalidOffset: return "invalid_offset";
    case Task7MaterializationErrorCode::OffsetOverflow: return "offset_overflow";
    case Task7MaterializationErrorCode::CandidateCountMismatch: return "candidate_count_mismatch";
    case Task7MaterializationErrorCode::RoutingSidecarMismatch: return "routing_sidecar_mismatch";
    case Task7MaterializationErrorCode::OptionalPresenceMismatch: return "optional_presence_mismatch";
    case Task7MaterializationErrorCode::ReferenceTypeMismatch: return "reference_type_mismatch";
    case Task7MaterializationErrorCode::ChainStateMismatch: return "chain_state_mismatch";
    case Task7MaterializationErrorCode::InvalidLimb: return "invalid_limb";
    case Task7MaterializationErrorCode::InvalidBoolean: return "invalid_boolean";
    case Task7MaterializationErrorCode::InvalidPadding: return "invalid_padding";
    case Task7MaterializationErrorCode::PadOnRealRow: return "pad_on_real_row";
    case Task7MaterializationErrorCode::ForbiddenSource: return "forbidden_source";
    case Task7MaterializationErrorCode::CanonicalizationFailure: return "canonicalization_failure";
    case Task7MaterializationErrorCode::InternalFailure: return "internal_failure";
    }
    return "internal_failure";
}

Task7MaterializationResult materialize_task7_input_v1(
    const Task7MaterializationSourceBatchV1& source) noexcept {
    const auto failure = [](const Task7MaterializationErrorCode code,
                            const char* diagnostic) {
        Task7MaterializationResult result;
        result.error = Task7MaterializationError{code, diagnostic};
        return result;
    };
    try {
        if (source.ragged == nullptr || source.samples.empty()) {
            return failure(Task7MaterializationErrorCode::SourceAssociationMismatch,
                           "Task7 source association is invalid");
        }
        if (source.ragged->schema_id != model::kModelBatchLayoutSchemaId) {
            return failure(Task7MaterializationErrorCode::UnknownSchema,
                           "Task7 batch schema is unknown");
        }
        if (source.ragged->batch_size != source.samples.size() ||
            source.ragged->samples.size() != source.samples.size()) {
            return failure(Task7MaterializationErrorCode::SourceAssociationMismatch,
                           "Task7 source association is invalid");
        }
        const auto& ragged = *source.ragged;
        try {
            validate_executable_source(source);
        } catch (const std::overflow_error&) {
            return failure(Task7MaterializationErrorCode::OffsetOverflow,
                           "Task7 offset exceeds executable range");
        } catch (const std::exception&) {
            return failure(Task7MaterializationErrorCode::InvalidOffset,
                           "Task7 ragged layout is invalid");
        }
        if (ragged.candidate_rows.size() != ragged.candidate_routing_keys.size()) {
            return failure(Task7MaterializationErrorCode::RoutingSidecarMismatch,
                           "Task7 routing sidecar cardinality is detached");
        }
        if (ragged.candidate_rows.size() !=
            ragged.candidate_optional_presence_masks.size()) {
            return failure(Task7MaterializationErrorCode::CandidateCountMismatch,
                           "Task7 candidate presence cardinality is detached");
        }
        try {
            validate_routing_sidecar(ragged);
        } catch (const std::exception&) {
            return failure(Task7MaterializationErrorCode::RoutingSidecarMismatch,
                           "Task7 routing sidecar is detached");
        }
        try {
            validate_candidate_presence_masks(ragged);
        } catch (const std::exception&) {
            return failure(Task7MaterializationErrorCode::OptionalPresenceMismatch,
                           "Task7 candidate presence mask is detached");
        }

        Task7MaterializedBatchV1 output;
        output.configuration_identity = task7_materialization_config_identity();
        output.samples.reserve(source.samples.size());
        for (std::size_t index = 0; index < source.samples.size(); ++index) {
            const auto& association = source.samples[index];
            if (association.logical == nullptr || association.encoded == nullptr ||
                association.vocabulary == nullptr ||
                association.expected_model_input_identity.empty() ||
                association.expected_card_vocabulary_identity.empty()) {
                return failure(Task7MaterializationErrorCode::SourceAssociationMismatch,
                               "Task7 source association is invalid");
            }
            const auto& logical = *association.logical;
            const auto& encoded = *association.encoded;
            const auto& vocabulary = *association.vocabulary;
            if (logical.schema_id != model::kLogicalModelInputSchemaId ||
                encoded.schema_id != model::kEncodedModelInputSchemaId) {
                return failure(Task7MaterializationErrorCode::UnknownSchema,
                               "Task7 Phase-5 schema is unknown");
            }
            if (logical.candidate_features.size() != encoded.candidate_features.size() ||
                logical.candidate_routing.size() != encoded.routing_keys.size()) {
                return failure(Task7MaterializationErrorCode::CandidateCountMismatch,
                               "Task7 candidate cardinality is detached");
            }
            std::string computed_model_input_identity;
            std::string computed_vocabulary_identity;
            std::vector<std::uint8_t> encoded_bytes;
            try {
                computed_model_input_identity =
                    model::model_input_identity(logical, encoded);
                computed_vocabulary_identity = vocabulary.identity();
                encoded_bytes = model::canonical_encoded_model_input_bytes(encoded);
            } catch (const std::exception&) {
                return failure(Task7MaterializationErrorCode::CanonicalizationFailure,
                               "Task7 Phase-5 value is not canonical");
            }
            if (association.expected_model_input_identity !=
                computed_model_input_identity) {
                return failure(Task7MaterializationErrorCode::ModelInputIdentityMismatch,
                               "Task7 model-input identity is detached");
            }
            if (encoded.card_vocabulary_identity != computed_vocabulary_identity) {
                return failure(Task7MaterializationErrorCode::CardVocabularyMismatch,
                               "Task7 encoded vocabulary identity is detached");
            }
            if (association.expected_card_vocabulary_identity !=
                computed_vocabulary_identity) {
                return failure(Task7MaterializationErrorCode::CardVocabularyMismatch,
                               "Task7 CardVocabulary identity is detached");
            }
            if (!encoded_vocabulary_ids_match(encoded, vocabulary)) {
                return failure(Task7MaterializationErrorCode::CardVocabularyMismatch,
                               "Task7 CardVocabulary mapping is detached");
            }
            if (index >= ragged.samples.size()) {
                return failure(Task7MaterializationErrorCode::SourceAssociationMismatch,
                               "Task7 sample association is out of range");
            }
            model::EncodedModelInputV1 reconstructed;
            try {
                reconstructed = model::reconstruct_model_batch_sample_v1(ragged, index);
            } catch (const std::exception&) {
                return failure(Task7MaterializationErrorCode::RaggedReconstructionMismatch,
                               "Task7 ragged sample reconstruction failed");
            }
            std::vector<std::uint8_t> reconstructed_bytes;
            try {
                reconstructed_bytes =
                    model::canonical_encoded_model_input_bytes(reconstructed);
            } catch (const std::exception&) {
                return failure(Task7MaterializationErrorCode::RaggedReconstructionMismatch,
                               "Task7 reconstructed sample is not canonical");
            }
            if (reconstructed_bytes != encoded_bytes) {
                return failure(Task7MaterializationErrorCode::RaggedReconstructionMismatch,
                               "Task7 ragged sample differs from its source");
            }
            if (encoded.candidate_features.empty() ||
                encoded.candidate_features.size() != encoded.routing_keys.size() ||
                encoded.candidate_features.size() >
                    std::numeric_limits<std::uint32_t>::max()) {
                return failure(Task7MaterializationErrorCode::CandidateCountMismatch,
                               "Task7 candidate cardinality is invalid");
            }
            std::vector<std::uint8_t> bytes;
            try {
                bytes = canonical_sample_bytes(
                    encoded, computed_model_input_identity,
                    computed_vocabulary_identity, output.configuration_identity);
            } catch (const std::exception&) {
                return failure(Task7MaterializationErrorCode::CanonicalizationFailure,
                               "Task7 canonical materialization failed");
            }
            Task7MaterializedSampleV1 materialized;
            materialized.model_input_identity = computed_model_input_identity;
            materialized.card_vocabulary_identity = computed_vocabulary_identity;
            materialized.public_observation_digest = encoded.public_observation_digest;
            materialized.public_candidate_domain_digest =
                encoded.public_candidate_domain_digest;
            materialized.candidate_count = static_cast<std::uint32_t>(
                encoded.candidate_features.size());
            materialized.canonical_bytes = std::move(bytes);
            output.samples.push_back(std::move(materialized));
        }
        return {std::optional<Task7MaterializedBatchV1>(std::move(output)),
                std::nullopt};
    } catch (const std::bad_alloc&) {
        return failure(Task7MaterializationErrorCode::InternalFailure,
                       "Task7 materialization failed");
    } catch (...) {
        return failure(Task7MaterializationErrorCode::InternalFailure,
                       "Task7 materialization failed");
    }
}

}  // namespace ygo::phase6
