#include "ygo/phase6/task7_input_materialization_v2.hpp"

#include <algorithm>
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

using Task7MaterializationSourceBatchV2 =
    detail::Task7MaterializationSourceBatchV2;
using Task7MaterializationSourceSampleV2 =
    detail::Task7MaterializationSourceSampleV2;

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

void validate_source_identity_strings(const Task7MaterializationSourceBatchV2& source) {
    if (!valid_identity(source.source_task7_authority_identity,
                       "phase6_task7_dataset_authority.v3.")) {
        fail(Task7MaterializationErrorCodeV2::InvalidAuthorityBinding);
    }
    if (!valid_identity(source.source_dataset_manifest_identity,
                        kTask7V2DatasetManifestIdentityPrefix) ||
        !lower_hex(source.source_dataset_semantic_identity, 64)) {
        fail(Task7MaterializationErrorCodeV2::InvalidDatasetBinding);
    }
    if (!valid_identity(source.source_training_dataset_split_identity,
                        "phase6_dataset_split.v1.")) {
        fail(Task7MaterializationErrorCodeV2::InvalidSplitBinding);
    }
}

void validate_source_binding(const Task7MaterializationSourceBatchV2& source) {
    validate_source_identity_strings(source);
    if (source.vocabulary == nullptr ||
        source.vocabulary->identity() != source.source_card_vocabulary_identity) {
        fail(Task7MaterializationErrorCodeV2::InvalidVocabularyBinding);
    }
}

void validate_sample_binding(const Task7MaterializationSourceBatchV2& source,
                             const Task7MaterializationSourceSampleV2& value,
                             const model::EncodedModelInputV2& reconstructed) {
    if (value.sample == nullptr ||
        value.source_task7_authority_identity != source.source_task7_authority_identity ||
        value.source_dataset_manifest_identity != source.source_dataset_manifest_identity ||
        value.source_dataset_semantic_identity != source.source_dataset_semantic_identity ||
        value.source_training_dataset_split_identity !=
            source.source_training_dataset_split_identity ||
        value.source_card_vocabulary_identity != source.source_card_vocabulary_identity) {
        fail(Task7MaterializationErrorCodeV2::InvalidSource);
    }
    const auto& sample = *value.sample;
    const auto expected_encoded = ygo::model::encode_model_input_v2(
        sample.logical_model_input, *source.vocabulary);
    if (sample.schema_id != kPhase6BcSampleIdentityDomainV2 ||
        phase6_sample_identity_v2(sample) != sample.sample_identity ||
        !valid_identity(sample.trajectory_record_id, "trajectory_record.v3.") ||
        !lower_hex(sample.episode_semantic_id, 64) ||
        sample.encoded_model_input.card_vocabulary_identity !=
            source.source_card_vocabulary_identity ||
        !expected_encoded || !expected_encoded.value.has_value() ||
        ygo::model::canonical_encoded_model_input_bytes(*expected_encoded.value) !=
            ygo::model::canonical_encoded_model_input_bytes(sample.encoded_model_input) ||
        ygo::model::canonical_encoded_model_input_bytes(sample.encoded_model_input) !=
            ygo::model::canonical_encoded_model_input_bytes(reconstructed)) {
        fail(Task7MaterializationErrorCodeV2::ModelInputMismatch);
    }
    (void)ygo::model::canonical_logical_model_input_bytes(sample.logical_model_input);
    (void)ygo::model::canonical_model_supervision_sample_bytes_v2(sample.supervision);
    if (sample.supervision.schema_id != ygo::model::kModelSupervisionSampleV2SchemaId ||
        sample.supervision.model_input_identity !=
            ygo::model::model_input_identity_v2(sample.logical_model_input,
                                                sample.encoded_model_input) ||
        sample.supervision.selected_public_action_key.empty() ||
        sample.supervision.candidate_ordinal >= reconstructed.candidate_features.size() ||
        reconstructed.routing_keys[sample.supervision.candidate_ordinal] !=
            sample.supervision.selected_public_action_key) {
        fail(Task7MaterializationErrorCodeV2::CandidateDomainMismatch);
    }
}

Task7MaterializationErrorV2 make_error(
    const Task7MaterializationErrorCodeV2 code) {
    Task7MaterializationErrorV2 result;
    result.code = code;
    result.diagnostic = std::string(task7_materialization_error_code_name_v2(code));
    return result;
}

Task7MaterializationResultV2 failure(const Task7MaterializationErrorCodeV2 code) noexcept {
    Task7MaterializationResultV2 result;
    result.error = make_error(code);
    return result;
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
        writer.string(sample.supervision.source_public_semantic_decision_id);
        writer.string(sample.supervision.model_input_identity);
        writer.string(sample.supervision.selected_public_action_key);
        writer.u32be(sample.supervision.candidate_ordinal);
        writer.string(sample.encoded_model_input.public_observation_digest);
        writer.u8(sample.encoded_model_input.public_candidate_domain_digest.has_value() ? 1 : 0);
        if (sample.encoded_model_input.public_candidate_domain_digest.has_value()) {
            writer.string(*sample.encoded_model_input.public_candidate_domain_digest);
        }
        writer.bytes(ygo::model::canonical_logical_model_input_bytes(
            sample.logical_model_input));
        writer.bytes(ygo::model::canonical_encoded_model_input_bytes(
            sample.encoded_model_input));
        writer.u32be(static_cast<std::uint32_t>(sample.routing_keys.size()));
        for (const auto& key : sample.routing_keys) writer.string(key);
        return std::move(writer).take();
    } catch (const MaterializationFailure&) {
        throw;
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
    validate_source_identity_strings(Task7MaterializationSourceBatchV2{
        nullptr,
        nullptr,
        batch.source_task7_authority_identity,
        batch.source_dataset_manifest_identity,
        batch.source_dataset_semantic_identity,
        batch.source_training_dataset_split_identity,
        batch.source_card_vocabulary_identity,
        {}});
    ByteWriter writer;
    writer.string(batch.schema_id);
    writer.string(batch.configuration_identity);
    writer.string(batch.source_task7_authority_identity);
    writer.string(batch.source_dataset_manifest_identity);
    writer.string(batch.source_dataset_semantic_identity);
    writer.string(batch.source_training_dataset_split_identity);
    writer.string(batch.source_card_vocabulary_identity);
    writer.u32be(static_cast<std::uint32_t>(batch.samples.size()));
    for (std::size_t index = 0; index < batch.samples.size(); ++index) {
        const auto& sample = batch.samples[index];
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
            ygo::model::canonical_encoded_model_input_bytes(
                sample.encoded_model_input) !=
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

Task7MaterializationResultV2 detail::materialize_task7_input_v2(
    const Task7MaterializationSourceBatchV2& source) noexcept {
    try {
        if (source.ragged == nullptr || source.samples.empty() ||
            source.samples.size() != source.ragged->batch_size) {
            fail(Task7MaterializationErrorCodeV2::InvalidSource);
        }
        validate_source_binding(source);
        const auto& ragged = *source.ragged;
        (void)ygo::model::canonical_model_batch_layout_bytes_v2(ragged);
        Task7MaterializedBatchV2 output;
        output.configuration_identity = task7_materialization_config_identity_v2();
        output.source_task7_authority_identity = source.source_task7_authority_identity;
        output.source_dataset_manifest_identity = source.source_dataset_manifest_identity;
        output.source_dataset_semantic_identity = source.source_dataset_semantic_identity;
        output.source_training_dataset_split_identity =
            source.source_training_dataset_split_identity;
        output.source_card_vocabulary_identity = source.source_card_vocabulary_identity;
        output.ragged = ragged;
        output.samples.reserve(source.samples.size());
        for (std::size_t index = 0; index < source.samples.size(); ++index) {
            const auto reconstructed =
                ygo::model::reconstruct_model_batch_sample_v2(ragged, index);
            validate_sample_binding(source, source.samples[index], reconstructed);
            const auto& source_sample = *source.samples[index].sample;
            Task7MaterializedSampleV2 sample;
            sample.source_task7_authority_identity =
                source.source_task7_authority_identity;
            sample.source_dataset_manifest_identity = source.source_dataset_manifest_identity;
            sample.source_dataset_semantic_identity = source.source_dataset_semantic_identity;
            sample.source_training_dataset_split_identity =
                source.source_training_dataset_split_identity;
            sample.source_card_vocabulary_identity = source.source_card_vocabulary_identity;
            sample.source_trajectory_record_id = source_sample.trajectory_record_id;
            sample.source_episode_semantic_id = source_sample.episode_semantic_id;
            sample.supervision = source_sample.supervision;
            sample.logical_model_input = source_sample.logical_model_input;
            sample.encoded_model_input = reconstructed;
            sample.routing_keys = reconstructed.routing_keys;
            sample.canonical_bytes = canonical_task7_materialized_sample_bytes_v2(sample);
            sample.sample_identity = materialized_sample_identity_v2(sample);
            output.samples.push_back(std::move(sample));
        }
        output.canonical_bytes = canonical_task7_materialized_batch_bytes_v2(output);
        return {std::optional<Task7MaterializedBatchV2>(std::move(output)), std::nullopt};
    } catch (const MaterializationFailure& error) {
        return failure(error.code());
    } catch (const std::bad_alloc&) {
        return failure(Task7MaterializationErrorCodeV2::InternalFailure);
    } catch (...) {
        return failure(Task7MaterializationErrorCodeV2::InternalFailure);
    }
}

}  // namespace ygo::phase6
