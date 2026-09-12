#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "ygo/model/model_batch_layout_v2.hpp"
#include "ygo/phase6/supervision_dataset_v2.hpp"

namespace ygo::phase6 {

class VerifiedTask7V3Authority;

inline constexpr std::string_view kTask7V2MaterializationSchemaId =
    "ocgforge.phase6.task7.input_materialization.v2";
inline constexpr std::string_view kTask7V2MaterializationConfigSchemaId =
    "ocgforge.phase6.task7.input_materialization_config.v2";
inline constexpr std::string_view kTask7V2MaterializationConfigIdentityPrefix =
    "phase6_task7_input_materialization_config.v2.";
inline constexpr std::string_view kTask7V2MaterializedSampleIdentityPrefix =
    "phase6_task7_materialized_sample.v2.";
inline constexpr std::string_view kTask7V2MaterializedBatchIdentityPrefix =
    "phase6_task7_materialized_batch.v2.";
inline constexpr std::string_view kTask7V2DatasetManifestIdentityPrefix =
    "phase6_task7_dataset_manifest.v3.";
inline constexpr std::string_view kTask7V2BackendTensorBundleSchemaId =
    "ocgforge.phase6.task7.backend_tensor_bundle.v2";

struct Task7MaterializedSampleV2 final {
    std::string schema_id = std::string(kTask7V2MaterializationSchemaId);
    std::string sample_identity;
    std::string source_task7_authority_identity;
    std::string source_dataset_manifest_identity;
    std::string source_dataset_semantic_identity;
    std::string source_training_dataset_split_identity;
    std::string source_card_vocabulary_identity;
    std::string source_trajectory_record_id;
    std::string source_episode_semantic_id;
    std::string source_public_semantic_decision_id;
    std::string source_model_input_identity_v2;
    model::ModelSupervisionSampleV2 supervision;
    model::LogicalModelInputV2 logical_model_input;
    model::EncodedModelInputV2 encoded_model_input;
    std::vector<std::string> routing_keys;
    std::vector<std::uint8_t> canonical_bytes;
};

struct Task7MaterializedBatchV2 final {
    std::string schema_id = std::string(kTask7V2MaterializationSchemaId);
    std::string configuration_identity;
    std::string source_task7_authority_identity;
    std::string source_dataset_manifest_identity;
    std::string source_dataset_semantic_identity;
    std::string source_training_dataset_split_identity;
    std::string source_card_vocabulary_identity;
    model::RaggedModelBatchV2 ragged;
    std::vector<Task7MaterializedSampleV2> samples;
    std::vector<std::uint8_t> canonical_bytes;
};

enum class Task7MaterializationErrorCodeV2 : std::uint8_t {
    InvalidSource,
    InvalidAuthorityBinding,
    InvalidDatasetBinding,
    InvalidSplitBinding,
    InvalidVocabularyBinding,
    InvalidBatch,
    ModelInputMismatch,
    CandidateDomainMismatch,
    SampleIdentityMismatch,
    CanonicalizationFailure,
    InternalFailure,
};

struct Task7MaterializationErrorV2 final {
    Task7MaterializationErrorCodeV2 code =
        Task7MaterializationErrorCodeV2::InternalFailure;
    std::string diagnostic;
};

struct Task7MaterializationResultV2 final {
    std::optional<Task7MaterializedBatchV2> value;
    std::optional<Task7MaterializationErrorV2> error;

    explicit operator bool() const noexcept {
        return value.has_value() && !error.has_value();
    }
};

std::string_view task7_materialization_error_code_name_v2(
    Task7MaterializationErrorCodeV2 code) noexcept;

std::vector<std::uint8_t> canonical_task7_materialization_config_bytes_v2();
std::string task7_materialization_config_identity_v2();

std::vector<std::uint8_t> canonical_task7_materialized_sample_bytes_v2(
    const Task7MaterializedSampleV2& sample);
std::string materialized_sample_identity_v2(
    const Task7MaterializedSampleV2& sample);

std::vector<std::uint8_t> canonical_task7_materialized_batch_bytes_v2(
    const Task7MaterializedBatchV2& batch);
std::string materialized_batch_identity_v2(
    const Task7MaterializedBatchV2& batch);

Task7MaterializationResultV2 materialize_task7_input_v2(
    const VerifiedTask7V3Authority& authority) noexcept;

}  // namespace ygo::phase6
