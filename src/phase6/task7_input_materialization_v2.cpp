#include "ygo/phase6/task7_input_materialization_v2.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <limits>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "ygo/phase6/task7_input_materialization.hpp"
#include "ygo/trace/sha256.hpp"
#include "ygo/trajectory/codec.hpp"

namespace ygo::phase6 {
namespace {

using ygo::trajectory::ByteReader;
using ygo::trajectory::ByteWriter;

class MaterializationFailure final {
public:
    explicit MaterializationFailure(const Task7MaterializationErrorCodeV2 code)
        : code_(code) {}

    Task7MaterializationErrorCodeV2 code() const noexcept { return code_; }

private:
    Task7MaterializationErrorCodeV2 code_;
};

[[noreturn]] void fail(const Task7MaterializationErrorCodeV2 code) {
    throw MaterializationFailure(code);
}

bool lower_hex(const std::string_view value, const std::size_t width) noexcept {
    return value.size() == width && std::all_of(value.begin(), value.end(), [](const char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
    });
}

bool valid_identity(const std::string_view value, const std::string_view prefix) noexcept {
    return value.size() == prefix.size() + 64 && value.substr(0, prefix.size()) == prefix &&
           lower_hex(value.substr(prefix.size()), 64);
}

bool valid_vocabulary_identity(const std::string_view value) noexcept {
    return valid_identity(value, "model_card_vocabulary.v1.");
}

void require_count(const std::uint32_t count, const std::uint32_t maximum,
                   const Task7MaterializationErrorCodeV2 code) {
    if (count > maximum) fail(code);
}

struct ReferenceComponentDescriptor final {
    std::string name;
    std::string source_type;
    std::string presence_rule;
};

struct ReferenceDescriptor final {
    std::string name;
    std::vector<ReferenceComponentDescriptor> components;
};

struct ColumnDescriptor final {
    std::string name;
    std::string source_type;
    std::uint8_t limb_count = 0;
    std::string presence_rule;
    std::string padding_rule;
};

struct TableDescriptor final {
    std::string name;
    std::string kind;
    std::string row_order;
    std::optional<std::string> parent;
    std::optional<std::string> parent_offset;
    std::string row_mask_rule;
    std::vector<ColumnDescriptor> columns;
};

struct RuleDescriptor final {
    std::string identifier;
    std::string value;
};

std::optional<std::string> read_optional_string(ByteReader& reader) {
    std::uint8_t present = 0;
    if (!reader.u8(present) || present > 1) {
        fail(Task7MaterializationErrorCodeV2::CanonicalizationFailure);
    }
    if (present == 0) return std::nullopt;
    std::string value;
    if (!reader.string(value)) fail(Task7MaterializationErrorCodeV2::CanonicalizationFailure);
    return value;
}

std::vector<ReferenceDescriptor> read_reference_descriptors(ByteReader& reader) {
    std::uint32_t descriptor_count = 0;
    if (!reader.u32be(descriptor_count)) {
        fail(Task7MaterializationErrorCodeV2::CanonicalizationFailure);
    }
    require_count(descriptor_count, 32,
                  Task7MaterializationErrorCodeV2::CanonicalizationFailure);
    std::vector<ReferenceDescriptor> result;
    result.reserve(descriptor_count);
    for (std::uint32_t index = 0; index < descriptor_count; ++index) {
        ReferenceDescriptor descriptor;
        std::uint32_t component_count = 0;
        if (!reader.string(descriptor.name) || !reader.u32be(component_count)) {
            fail(Task7MaterializationErrorCodeV2::CanonicalizationFailure);
        }
        require_count(component_count, 32,
                      Task7MaterializationErrorCodeV2::CanonicalizationFailure);
        descriptor.components.reserve(component_count);
        for (std::uint32_t component = 0; component < component_count; ++component) {
            ReferenceComponentDescriptor value;
            if (!reader.string(value.name) || !reader.string(value.source_type) ||
                !reader.string(value.presence_rule)) {
                fail(Task7MaterializationErrorCodeV2::CanonicalizationFailure);
            }
            descriptor.components.push_back(std::move(value));
        }
        result.push_back(std::move(descriptor));
    }
    return result;
}

std::vector<TableDescriptor> read_table_descriptors(ByteReader& reader) {
    std::uint32_t count = 0;
    if (!reader.u32be(count)) fail(Task7MaterializationErrorCodeV2::CanonicalizationFailure);
    require_count(count, 64, Task7MaterializationErrorCodeV2::CanonicalizationFailure);
    std::vector<TableDescriptor> result;
    result.reserve(count);
    for (std::uint32_t index = 0; index < count; ++index) {
        TableDescriptor descriptor;
        std::uint32_t column_count = 0;
        if (!reader.string(descriptor.name) || !reader.string(descriptor.kind) ||
            !reader.string(descriptor.row_order)) {
            fail(Task7MaterializationErrorCodeV2::CanonicalizationFailure);
        }
        descriptor.parent = read_optional_string(reader);
        descriptor.parent_offset = read_optional_string(reader);
        if (!reader.string(descriptor.row_mask_rule) || !reader.u32be(column_count)) {
            fail(Task7MaterializationErrorCodeV2::CanonicalizationFailure);
        }
        require_count(column_count, 128,
                      Task7MaterializationErrorCodeV2::CanonicalizationFailure);
        descriptor.columns.reserve(column_count);
        for (std::uint32_t column = 0; column < column_count; ++column) {
            ColumnDescriptor value;
            if (!reader.string(value.name) || !reader.string(value.source_type) ||
                !reader.u8(value.limb_count) || !reader.string(value.presence_rule) ||
                !reader.string(value.padding_rule)) {
                fail(Task7MaterializationErrorCodeV2::CanonicalizationFailure);
            }
            descriptor.columns.push_back(std::move(value));
        }
        result.push_back(std::move(descriptor));
    }
    return result;
}

std::vector<RuleDescriptor> read_rule_descriptors(ByteReader& reader) {
    std::uint32_t count = 0;
    if (!reader.u32be(count)) fail(Task7MaterializationErrorCodeV2::CanonicalizationFailure);
    require_count(count, 64, Task7MaterializationErrorCodeV2::CanonicalizationFailure);
    std::vector<RuleDescriptor> result;
    result.reserve(count);
    for (std::uint32_t index = 0; index < count; ++index) {
        RuleDescriptor value;
        if (!reader.string(value.identifier) || !reader.string(value.value)) {
            fail(Task7MaterializationErrorCodeV2::CanonicalizationFailure);
        }
        result.push_back(std::move(value));
    }
    return result;
}

void write_reference_descriptors(ByteWriter& writer,
                                 const std::vector<ReferenceDescriptor>& values) {
    writer.u32be(static_cast<std::uint32_t>(values.size()));
    for (const auto& value : values) {
        writer.string(value.name);
        writer.u32be(static_cast<std::uint32_t>(value.components.size()));
        for (const auto& component : value.components) {
            writer.string(component.name);
            writer.string(component.source_type);
            writer.string(component.presence_rule);
        }
    }
}

void write_table_descriptors(ByteWriter& writer,
                             const std::vector<TableDescriptor>& values) {
    writer.u32be(static_cast<std::uint32_t>(values.size()));
    for (const auto& value : values) {
        writer.string(value.name);
        writer.string(value.kind);
        writer.string(value.row_order);
        writer.u8(value.parent.has_value() ? 1 : 0);
        if (value.parent.has_value()) writer.string(*value.parent);
        writer.u8(value.parent_offset.has_value() ? 1 : 0);
        if (value.parent_offset.has_value()) writer.string(*value.parent_offset);
        writer.string(value.row_mask_rule);
        writer.u32be(static_cast<std::uint32_t>(value.columns.size()));
        for (const auto& column : value.columns) {
            writer.string(column.name);
            writer.string(column.source_type);
            writer.u8(column.limb_count);
            writer.string(column.presence_rule);
            writer.string(column.padding_rule);
        }
    }
}

void write_rule_descriptors(ByteWriter& writer,
                            const std::vector<RuleDescriptor>& values) {
    writer.u32be(static_cast<std::uint32_t>(values.size()));
    for (const auto& value : values) {
        writer.string(value.identifier);
        writer.string(value.value);
    }
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

std::vector<std::uint8_t> build_configuration_bytes_v2() {

    const auto v1 = canonical_task7_materialization_config_bytes();
    if (v1.size() != 8133 ||
        ygo::trace::sha256_bytes(v1) !=
            "20f394c888e959446fa263c3520f3dd3b1f48b3a23e58373da7153a691ab1e7a") {
        fail(Task7MaterializationErrorCodeV2::CanonicalizationFailure);
    }

    ByteReader reader(v1);
    std::string ignored;
    if (!reader.string(ignored) || !reader.string(ignored)) {
        fail(Task7MaterializationErrorCodeV2::CanonicalizationFailure);
    }
    std::uint32_t source_count = 0;
    if (!reader.u32be(source_count)) fail(Task7MaterializationErrorCodeV2::CanonicalizationFailure);
    require_count(source_count, 32, Task7MaterializationErrorCodeV2::CanonicalizationFailure);
    for (std::uint32_t index = 0; index < source_count; ++index) {
        if (!reader.string(ignored)) fail(Task7MaterializationErrorCodeV2::CanonicalizationFailure);
    }
    std::string limb_order;
    std::string integer_type;
    std::string boolean_type;
    if (!reader.string(limb_order) || !reader.string(integer_type) ||
        !reader.string(boolean_type)) {
        fail(Task7MaterializationErrorCodeV2::CanonicalizationFailure);
    }
    const auto references = read_reference_descriptors(reader);
    auto tables = read_table_descriptors(reader);
    const auto rules = read_rule_descriptors(reader);
    if (!reader.at_end()) fail(Task7MaterializationErrorCodeV2::CanonicalizationFailure);

    const auto candidate = std::find_if(
        tables.begin(), tables.end(), [](const auto& value) { return value.name == "candidates"; });
    if (candidate == tables.end() || candidate->columns.empty() ||
        candidate->columns.front().name != "action_kind_code") {
        fail(Task7MaterializationErrorCodeV2::CanonicalizationFailure);
    }
    candidate->columns.insert(candidate->columns.begin() + 1,
                              ColumnDescriptor{"card_selection_operation_code", "U8", 1,
                                               "required", "zero"});

    static constexpr std::string_view source_contracts[] = {
        "ocgforge.model_logical_input.v2",
        "ocgforge.model_encoded_input.v2",
        "ocgforge.model_supervision_sample.v2",
        "ocgforge.model_card_vocabulary.v1",
        "ocgforge.model_input_identity.v2",
        "ocgforge.model_batch_layout.v2",
    };
    ByteWriter writer;
    writer.string(kTask7V2MaterializationConfigSchemaId);
    writer.string(kTask7V2MaterializationSchemaId);
    writer.string(ygo::model::kModelBatchLayoutV2SchemaId);
    writer.string(kTask7V2BackendTensorBundleSchemaId);
    writer.u32be(static_cast<std::uint32_t>(
        sizeof(source_contracts) / sizeof(source_contracts[0])));
    for (const auto value : source_contracts) writer.string(value);
    writer.string(limb_order);
    writer.string(integer_type);
    writer.string(boolean_type);
    write_reference_descriptors(writer, references);
    write_table_descriptors(writer, tables);
    write_rule_descriptors(writer, rules);
    return std::move(writer).take();
}

void validate_batch_identity_strings(const Task7MaterializedBatchV2& batch) {
    if (!valid_identity(batch.source_task7_authority_identity,
                       "phase6_task7_dataset_authority.v3.")) {
        fail(Task7MaterializationErrorCodeV2::InvalidAuthorityBinding);
    }
    if (!valid_identity(batch.source_dataset_manifest_identity,
                        kTask7V2DatasetManifestIdentityPrefix) ||
        !lower_hex(batch.source_dataset_semantic_identity, 64)) {
        fail(Task7MaterializationErrorCodeV2::InvalidDatasetBinding);
    }
    if (!valid_identity(batch.source_training_dataset_split_identity,
                        "phase6_dataset_split.v1.")) {
        fail(Task7MaterializationErrorCodeV2::InvalidSplitBinding);
    }
    if (!valid_vocabulary_identity(batch.source_card_vocabulary_identity)) {
        fail(Task7MaterializationErrorCodeV2::InvalidVocabularyBinding);
    }
}

void validate_materialized_sample(const Task7MaterializedSampleV2& sample) {
    if (sample.schema_id != kTask7V2MaterializationSchemaId ||
        !valid_identity(sample.source_task7_authority_identity,
                        "phase6_task7_dataset_authority.v3.") ||
        !valid_identity(sample.source_dataset_manifest_identity,
                        kTask7V2DatasetManifestIdentityPrefix) ||
        !lower_hex(sample.source_dataset_semantic_identity, 64) ||
         !valid_identity(sample.source_training_dataset_split_identity,
                         "phase6_dataset_split.v1.") ||
         !valid_vocabulary_identity(sample.source_card_vocabulary_identity) ||
         !valid_identity(sample.source_trajectory_record_id, "trajectory_record.v3.") ||
         !lower_hex(sample.source_episode_semantic_id, 64) ||
         !ygo::trajectory::is_lower_hex_digest(sample.source_public_semantic_decision_id) ||
         sample.source_public_semantic_decision_id !=
             sample.supervision.source_public_semantic_decision_id ||
         sample.source_model_input_identity_v2 !=
             sample.supervision.model_input_identity ||
         sample.supervision.schema_id != ygo::model::kModelSupervisionSampleV2SchemaId ||
         sample.supervision.model_input_identity !=
             ygo::model::model_input_identity_v2(sample.logical_model_input,
                                                 sample.encoded_model_input) ||
         sample.routing_keys != sample.encoded_model_input.routing_keys) {
        fail(Task7MaterializationErrorCodeV2::SampleIdentityMismatch);
    }
    (void)ygo::model::canonical_model_supervision_sample_bytes_v2(sample.supervision);
    if (sample.supervision.candidate_ordinal >=
            sample.encoded_model_input.candidate_features.size() ||
        sample.encoded_model_input.routing_keys[sample.supervision.candidate_ordinal] !=
            sample.supervision.selected_public_action_key) {
        fail(Task7MaterializationErrorCodeV2::CandidateDomainMismatch);
    }
}

}  // namespace

std::string_view task7_materialization_error_code_name_v2(
    const Task7MaterializationErrorCodeV2 code) noexcept {
    switch (code) {
    case Task7MaterializationErrorCodeV2::InvalidSource: return "invalid_source";
    case Task7MaterializationErrorCodeV2::InvalidAuthorityBinding:
        return "invalid_authority_binding";
    case Task7MaterializationErrorCodeV2::InvalidDatasetBinding:
        return "invalid_dataset_binding";
    case Task7MaterializationErrorCodeV2::InvalidSplitBinding:
        return "invalid_split_binding";
    case Task7MaterializationErrorCodeV2::InvalidVocabularyBinding:
        return "invalid_vocabulary_binding";
    case Task7MaterializationErrorCodeV2::InvalidBatch: return "invalid_batch";
    case Task7MaterializationErrorCodeV2::ModelInputMismatch:
        return "model_input_mismatch";
    case Task7MaterializationErrorCodeV2::CandidateDomainMismatch:
        return "candidate_domain_mismatch";
    case Task7MaterializationErrorCodeV2::SampleIdentityMismatch:
        return "sample_identity_mismatch";
    case Task7MaterializationErrorCodeV2::CanonicalizationFailure:
        return "canonicalization_failure";
    case Task7MaterializationErrorCodeV2::InternalFailure: return "internal_failure";
    }
    return "internal_failure";
}

std::vector<std::uint8_t> canonical_task7_materialization_config_bytes_v2() {
    const auto bytes = build_configuration_bytes_v2();
    const std::vector<std::uint8_t> expected_prefix = {
        0x00, 0x00, 0x00, 0x35, 0x6f, 0x63, 0x67, 0x66, 0x6f, 0x72, 0x67, 0x65,
        0x2e, 0x70, 0x68, 0x61, 0x73, 0x65, 0x36, 0x2e, 0x74, 0x61, 0x73, 0x6b,
        0x37, 0x2e, 0x69, 0x6e, 0x70, 0x75, 0x74, 0x5f, 0x6d, 0x61, 0x74, 0x65,
        0x72, 0x69, 0x61, 0x6c, 0x69, 0x7a, 0x61, 0x74, 0x69, 0x6f, 0x6e, 0x5f};
    const std::vector<std::uint8_t> expected_suffix = {
        0x49, 0x53, 0x54, 0x49, 0x4e, 0x43, 0x54, 0x00, 0x00, 0x00, 0x19, 0x63,
        0x68, 0x61, 0x69, 0x6e, 0x5f, 0x73, 0x74, 0x61, 0x74, 0x65, 0x5f, 0x6c,
        0x65, 0x6e, 0x67, 0x74, 0x68, 0x5f, 0x73, 0x6f, 0x75, 0x72, 0x63, 0x65,
        0x00, 0x00, 0x00, 0x08, 0x44, 0x49, 0x53, 0x54, 0x49, 0x4e, 0x43, 0x54};
    if (bytes.size() != 8317 ||
        ygo::trace::sha256_bytes(bytes) !=
            "ce39fdd472614f4fa9e622d93fb5628549dd3705e501e9d287e679fa307063b9" ||
        bytes.size() < expected_prefix.size() + expected_suffix.size() ||
        std::vector<std::uint8_t>(bytes.begin(), bytes.begin() + expected_prefix.size()) !=
            expected_prefix ||
        std::vector<std::uint8_t>(bytes.end() - expected_suffix.size(), bytes.end()) !=
            expected_suffix) {
        throw std::invalid_argument("Task7 V2 materialization configuration KAT mismatch");
    }
    return bytes;
}

std::string task7_materialization_config_identity_v2() {
    return std::string(kTask7V2MaterializationConfigIdentityPrefix) +
           ygo::trace::sha256_bytes(canonical_task7_materialization_config_bytes_v2());
}

std::vector<TableDescriptor> physical_table_descriptors_v2() {
    const auto v1 = canonical_task7_materialization_config_bytes();
    if (v1.size() != 8133 ||
        ygo::trace::sha256_bytes(v1) !=
            "20f394c888e959446fa263c3520f3dd3b1f48b3a23e58373da7153a691ab1e7a") {
        fail(Task7MaterializationErrorCodeV2::CanonicalizationFailure);
    }
    ByteReader reader(v1);
    std::string ignored;
    std::uint32_t source_count = 0;
    if (!reader.string(ignored) || !reader.string(ignored) ||
        !reader.u32be(source_count)) {
        fail(Task7MaterializationErrorCodeV2::CanonicalizationFailure);
    }
    require_count(source_count, 32,
                  Task7MaterializationErrorCodeV2::CanonicalizationFailure);
    for (std::uint32_t index = 0; index < source_count; ++index) {
        if (!reader.string(ignored)) {
            fail(Task7MaterializationErrorCodeV2::CanonicalizationFailure);
        }
    }
    if (!reader.string(ignored) || !reader.string(ignored) ||
        !reader.string(ignored)) {
        fail(Task7MaterializationErrorCodeV2::CanonicalizationFailure);
    }
    (void)read_reference_descriptors(reader);
    auto tables = read_table_descriptors(reader);
    (void)read_rule_descriptors(reader);
    if (!reader.at_end()) fail(Task7MaterializationErrorCodeV2::CanonicalizationFailure);
    const auto candidate = std::find_if(
        tables.begin(), tables.end(), [](const auto& value) { return value.name == "candidates"; });
    if (candidate == tables.end() || candidate->columns.empty() ||
        candidate->columns.front().name != "action_kind_code") {
        fail(Task7MaterializationErrorCodeV2::CanonicalizationFailure);
    }
    candidate->columns.insert(candidate->columns.begin() + 1,
                              ColumnDescriptor{"card_selection_operation_code", "U8", 1,
                                               "required", "zero"});
    return tables;
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

std::vector<std::uint64_t> sample_offsets(const std::size_t row_count) {
    require_u32_count(row_count, "Task7 sample row count exceeds u32");
    return {0, static_cast<std::uint64_t>(row_count)};
}

std::vector<std::uint64_t> property_offsets(
    const model::EncodedModelInputV2& encoded,
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
    const model::EncodedModelInputV2& encoded) {
    std::vector<std::uint64_t> offsets;
    offsets.reserve(encoded.entities.size() + 1);
    offsets.push_back(0);
    for (std::size_t index = 0; index < encoded.entities.size(); ++index) {
        offsets.push_back(checked_offset_add(offsets.back(), 2));
    }
    return offsets;
}

std::vector<std::uint64_t> chain_target_offsets(
    const model::EncodedModelInputV2& encoded) {
    std::vector<std::uint64_t> offsets;
    offsets.reserve(encoded.chain.links.size() + 1);
    offsets.push_back(0);
    for (const auto& link : encoded.chain.links) {
        offsets.push_back(checked_offset_add(offsets.back(), link.targets.size()));
    }
    return offsets;
}

std::vector<std::uint64_t> visible_event_target_offsets(
    const model::EncodedModelInputV2& encoded) {
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
    const model::EncodedModelInputV2& encoded, const std::size_t row,
    std::uint8_t& role) {
    if (row >= checked_size_double(encoded.entities.size())) {
        throw std::out_of_range("Task7 property row is out of range");
    }
    const auto entity_index = row / 2;
    role = row % 2 == 0 ? 1 : 2;
    return role == 1 ? encoded.entities[entity_index].printed
                     : encoded.entities[entity_index].current;
}

std::uint8_t property_link_marker_at(const model::EncodedModelInputV2& encoded,
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
    const model::EncodedModelInputV2& encoded, std::size_t row) {
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
    const model::EncodedModelInputV2& encoded, std::size_t row) {
    for (const auto& link : encoded.chain.links) {
        if (row < link.targets.size()) return link.targets[row];
        row -= link.targets.size();
    }
    throw std::out_of_range("Task7 chain-target row is out of range");
}

std::uint32_t visible_event_target_at(
    const model::EncodedModelInputV2& encoded, std::size_t row) {
    for (const auto& event : encoded.visible_events) {
        if (row < event.target_public_locator_ordinals.size()) {
            return event.target_public_locator_ordinals[row];
        }
        row -= event.target_public_locator_ordinals.size();
    }
    throw std::out_of_range("Task7 visible-event-target row is out of range");
}

std::size_t table_row_count_v2(const TableDescriptor& table,
                            const model::EncodedModelInputV2& encoded) {
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

std::vector<std::uint64_t> child_offsets_for_v2(
    const TableDescriptor& table, const model::EncodedModelInputV2& encoded) {
    if (table.name == "entity_properties") return entity_property_offsets(encoded);
    if (table.name == "property_link_markers") return property_offsets(encoded, true);
    if (table.name == "property_counters") return property_offsets(encoded, false);
    if (table.name == "chain_targets") return chain_target_offsets(encoded);
    if (table.name == "visible_event_targets") {
        return visible_event_target_offsets(encoded);
    }
    return {};
}

void write_property_column_v2(
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

void write_encoded_column_v2(ByteWriter& writer, const std::string_view table_name,
                          const std::string_view column_name,
                          const model::EncodedModelInputV2& encoded,
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
        else write_property_column_v2(writer, property, column_name);
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
        else if (column_name == "card_selection_operation_code") write_u8_limb(writer, value.card_selection_operation_code);
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

void write_table_v2(ByteWriter& writer, const TableDescriptor& table,
                 const model::EncodedModelInputV2& encoded) {
    const auto rows = table_row_count_v2(table, encoded);
    require_u32_count(rows, "Task7 table row count exceeds u32");
    writer.string(table.name);
    writer.u64be(static_cast<std::uint64_t>(rows));
    if (table.kind != "singleton") write_u64_vector(writer, sample_offsets(rows));
    if (table.parent_offset.has_value()) {
        const auto offsets = child_offsets_for_v2(table, encoded);
        validate_offset_vector(offsets, rows);
        write_u64_vector(writer, offsets);
    }
    std::vector<std::string_view> column_names;
    column_names.reserve(table.columns.size());
    for (const auto& column : table.columns) column_names.push_back(column.name);
    write_string_vector(writer, column_names);
    for (const auto& column : table.columns) {
        for (std::size_t row = 0; row < rows; ++row) {
            write_encoded_column_v2(writer, table.name, column.name, encoded, row);
        }
    }
    write_bool_vector(writer, rows, true);
}


std::vector<std::uint8_t> canonical_task7_materialized_sample_bytes_v2(
    const Task7MaterializedSampleV2& sample) {
    try {
        validate_materialized_sample(sample);
        ByteWriter writer;
        writer.string(sample.schema_id);
        writer.string(task7_materialization_config_identity_v2());
        writer.string(sample.source_task7_authority_identity);
        writer.string(sample.source_dataset_manifest_identity);
        writer.string(sample.source_dataset_semantic_identity);
        writer.string(sample.source_training_dataset_split_identity);
         writer.string(sample.source_card_vocabulary_identity);
         writer.string(sample.source_trajectory_record_id);
         writer.string(sample.source_episode_semantic_id);
         writer.string(sample.source_public_semantic_decision_id);
         writer.string(sample.source_model_input_identity_v2);
         writer.string(sample.supervision.schema_id);
         writer.string(sample.supervision.model_input_identity);
         writer.string(sample.supervision.source_public_semantic_decision_id);
         writer.string(sample.supervision.selected_public_action_key);
         writer.u32be(sample.supervision.candidate_ordinal);
         writer.string(sample.encoded_model_input.public_observation_digest);
         write_optional_string(
             writer,
             sample.encoded_model_input.public_candidate_domain_digest.has_value()
                 ? std::optional<std::string_view>{
                       *sample.encoded_model_input.public_candidate_domain_digest}
                 : std::nullopt);
         for (const auto& table : physical_table_descriptors_v2()) {
             write_table_v2(writer, table, sample.encoded_model_input);
         }
         return std::move(writer).take();
    } catch (const MaterializationFailure&) {
        throw;
    } catch (const std::exception& exception) {
        throw std::invalid_argument(std::string("Task7 V2 materialized sample is not canonical: ") +
                                    exception.what());
    } catch (...) {
        throw std::invalid_argument("Task7 V2 materialized sample is not canonical");
    }
}

std::string materialized_sample_identity_v2(
    const Task7MaterializedSampleV2& sample) {
    return std::string(kTask7V2MaterializedSampleIdentityPrefix) +
           ygo::trace::sha256_bytes(canonical_task7_materialized_sample_bytes_v2(sample));
}

std::vector<std::uint8_t> canonical_task7_materialized_batch_bytes_v2(
    const Task7MaterializedBatchV2& batch) {
    if (batch.schema_id != kTask7V2MaterializationSchemaId ||
        batch.configuration_identity != task7_materialization_config_identity_v2() ||
        batch.samples.empty() || batch.ragged.batch_size != batch.samples.size()) {
        throw std::invalid_argument("Task7 V2 materialized batch is not canonical");
    }
    validate_batch_identity_strings(batch);
    ByteWriter writer;
    writer.string(batch.schema_id);
    writer.string(batch.configuration_identity);
    writer.string(batch.source_task7_authority_identity);
    writer.string(batch.source_dataset_manifest_identity);
    writer.string(batch.source_dataset_semantic_identity);
    writer.string(batch.source_training_dataset_split_identity);
    writer.string(batch.source_card_vocabulary_identity);
    writer.u32be(static_cast<std::uint32_t>(batch.samples.size()));
    std::set<std::string> sample_identities;
    for (std::size_t index = 0; index < batch.samples.size(); ++index) {
        const auto& sample = batch.samples[index];
        if (!sample_identities.insert(sample.sample_identity).second) {
            throw std::invalid_argument("Task7 V2 materialized batch contains duplicate sample identity");
        }
        if (sample.source_task7_authority_identity !=
                batch.source_task7_authority_identity ||
            sample.source_dataset_manifest_identity !=
                batch.source_dataset_manifest_identity ||
            sample.source_dataset_semantic_identity !=
                batch.source_dataset_semantic_identity ||
            sample.source_training_dataset_split_identity !=
                batch.source_training_dataset_split_identity ||
            sample.source_card_vocabulary_identity !=
                batch.source_card_vocabulary_identity ||
            ygo::model::canonical_encoded_model_input_bytes(sample.encoded_model_input) !=
                ygo::model::canonical_encoded_model_input_bytes(
                    ygo::model::reconstruct_model_batch_sample_v2(batch.ragged, index))) {
            throw std::invalid_argument("Task7 V2 materialized sample/batch binding mismatch");
        }
        const auto sample_bytes = canonical_task7_materialized_sample_bytes_v2(sample);
        if (sample.canonical_bytes != sample_bytes ||
            sample.sample_identity != materialized_sample_identity_v2(sample)) {
            throw std::invalid_argument("Task7 V2 materialized sample bytes are detached");
        }
        writer.string(sample.sample_identity);
        writer.bytes(sample_bytes);
    }
    return std::move(writer).take();
}

std::string materialized_batch_identity_v2(
    const Task7MaterializedBatchV2& batch) {
    return std::string(kTask7V2MaterializedBatchIdentityPrefix) +
           ygo::trace::sha256_bytes(canonical_task7_materialized_batch_bytes_v2(batch));
}

}  // namespace ygo::phase6
