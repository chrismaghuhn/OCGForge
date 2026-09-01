#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "ygo/model/card_vocabulary.hpp"
#include "ygo/model/encoded_model_input.hpp"
#include "ygo/model/logical_model_input.hpp"
#include "ygo/model/model_supervision_sample.hpp"
#include "ygo/trajectory/dataset_manifest.hpp"

namespace ygo::phase6 {

inline constexpr std::string_view kPhase6DatasetMembershipContractId =
    "ocgforge.phase6.dataset_membership.v1";
inline constexpr std::string_view kPhase6DatasetSplitContractId =
    "ocgforge.phase6.dataset_split.v1";
inline constexpr std::string_view kPhase6DatasetSplitIdentityDomain =
    "ocgforge.phase6.dataset_split_identity.v1";
inline constexpr std::string_view kPhase6DatasetSplitIdentityPrefix =
    "phase6_dataset_split.v1.";
inline constexpr std::string_view kPhase6SplitPartitionIdentity =
    "ocgforge.phase6.split.fixed_80_10_10_sha256.v1";
inline constexpr std::string_view kPhase6BcSampleIdentityDomain =
    "ocgforge.phase6.bc_sample_identity.v1";
inline constexpr std::string_view kPhase6BcSampleIdentityPrefix =
    "bc_sample.v1.";

enum class Phase6DatasetPartition : std::uint8_t {
    Train = 0,
    Validation = 1,
    Test = 2,
};

enum class Phase6DataErrorCode : std::uint8_t {
    InvalidDatasetManifest,
    MissingAdmissionReceipt,
    AdmissionBindingFailure,
    MissingEpisodeEnvelope,
    UnexpectedEpisodeEnvelope,
    FailedOrQuarantinedTrajectory,
    InvalidCertifiedEnvironment,
    IneligibleTeacherPolicy,
    InvalidDecisionRecord,
    ModelInputFailure,
    CandidateDomainFailure,
    CandidateCapacityFailure,
    DuplicateSampleIdentity,
    InvalidSplit,
    InternalFailure,
};

struct Phase6DataError final {
    Phase6DataErrorCode code = Phase6DataErrorCode::InternalFailure;
    std::string diagnostic;
};

struct Phase6BcSampleV1 final {
    std::string schema_id = std::string(kPhase6BcSampleIdentityDomain);
    std::string sample_identity;
    std::string trajectory_record_id;
    std::string episode_semantic_id;
    model::ModelSupervisionSampleV1 supervision;
    model::LogicalModelInputV1 logical_model_input;
    model::EncodedModelInputV1 encoded_model_input;
};

struct TrainingDatasetSplitV1 final {
    std::string schema_id = std::string(kPhase6DatasetSplitContractId);
    std::string source_dataset_identity;
    std::string split_contract_identity = std::string(kPhase6DatasetSplitContractId);
    std::string split_seed_or_partition_identity =
        std::string(kPhase6SplitPartitionIdentity);
    std::vector<std::string> train_episode_ids;
    std::vector<std::string> validation_episode_ids;
    std::vector<std::string> test_episode_ids;
    std::string split_identity;
};

struct Phase6MaterializedDatasetV1 final {
    std::string source_dataset_identity;
    TrainingDatasetSplitV1 split;
    std::vector<Phase6BcSampleV1> train_samples;
    std::vector<Phase6BcSampleV1> validation_samples;
    std::vector<Phase6BcSampleV1> test_samples;

    std::size_t sample_count() const noexcept {
        return train_samples.size() + validation_samples.size() + test_samples.size();
    }
};

struct Phase6SampleResult final {
    std::optional<Phase6BcSampleV1> value;
    std::optional<Phase6DataError> error;

    explicit operator bool() const noexcept {
        return value.has_value() && !error.has_value();
    }
};

struct Phase6SplitResult final {
    std::optional<TrainingDatasetSplitV1> value;
    std::optional<Phase6DataError> error;

    explicit operator bool() const noexcept {
        return value.has_value() && !error.has_value();
    }
};

struct Phase6DatasetResult final {
    std::optional<Phase6MaterializedDatasetV1> value;
    std::optional<Phase6DataError> error;

    explicit operator bool() const noexcept {
        return value.has_value() && !error.has_value();
    }
};

std::string_view phase6_data_error_code_name(Phase6DataErrorCode code) noexcept;

Phase6SampleResult materialize_phase6_sample_v1(
    const trajectory::DatasetManifest& manifest,
    const std::vector<trajectory::VerifiedAdmissionReceipt>& verified_receipts,
    const trajectory::EpisodeEnvelope& admitted_envelope,
    std::size_t record_index,
    const model::CardVocabularyV1& vocabulary) noexcept;

Phase6DatasetResult materialize_phase6_dataset_v1(
    const trajectory::DatasetManifest& manifest,
    const std::vector<trajectory::VerifiedAdmissionReceipt>& verified_receipts,
    const std::vector<trajectory::EpisodeEnvelope>& admitted_envelopes,
    const model::CardVocabularyV1& vocabulary) noexcept;

std::vector<std::uint8_t> canonical_phase6_sample_identity_bytes(
    const Phase6BcSampleV1& sample);
std::string phase6_sample_identity(const Phase6BcSampleV1& sample);

std::optional<Phase6DatasetPartition> phase6_partition_for_episode(
    std::string_view episode_semantic_id) noexcept;

Phase6SplitResult make_phase6_split_v1(
    std::string source_dataset_identity,
    const std::vector<std::string>& episode_semantic_ids) noexcept;

std::vector<std::uint8_t> canonical_phase6_split_identity_bytes(
    const TrainingDatasetSplitV1& split);
std::string phase6_split_identity(const TrainingDatasetSplitV1& split);

}  // namespace ygo::phase6
