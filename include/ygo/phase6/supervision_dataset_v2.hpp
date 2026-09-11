#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "ygo/model/card_vocabulary.hpp"
#include "ygo/model/encoded_model_input_v2.hpp"
#include "ygo/model/logical_model_input_v2.hpp"
#include "ygo/model/model_supervision_sample_v2.hpp"
#include "ygo/phase6/supervision_dataset.hpp"
#include "ygo/trajectory/dataset_manifest_v3.hpp"
#include "ygo/trajectory/receipt_v3.hpp"

namespace ygo::phase6 {

inline constexpr std::string_view kPhase6BcSampleIdentityDomainV2 =
    "ocgforge.phase6.bc_sample_identity.v2";
inline constexpr std::string_view kPhase6BcSampleIdentityPrefixV2 =
    "bc_sample.v2.";

struct Phase6BcSampleV2 final {
    std::string schema_id = std::string(kPhase6BcSampleIdentityDomainV2);
    std::string sample_identity;
    std::string trajectory_record_id;
    std::string episode_semantic_id;
    model::ModelSupervisionSampleV2 supervision;
    model::LogicalModelInputV2 logical_model_input;
    model::EncodedModelInputV2 encoded_model_input;
};

struct Phase6MaterializedDatasetV2 final {
    std::string source_dataset_identity;
    TrainingDatasetSplitV1 split;
    std::vector<Phase6BcSampleV2> train_samples;
    std::vector<Phase6BcSampleV2> validation_samples;
    std::vector<Phase6BcSampleV2> test_samples;

    std::size_t sample_count() const noexcept {
        return train_samples.size() + validation_samples.size() + test_samples.size();
    }
};

struct Phase6SampleResultV2 final {
    std::optional<Phase6BcSampleV2> value;
    std::optional<Phase6DataError> error;

    explicit operator bool() const noexcept {
        return value.has_value() && !error.has_value();
    }
};

struct Phase6DatasetResultV2 final {
    std::optional<Phase6MaterializedDatasetV2> value;
    std::optional<Phase6DataError> error;

    explicit operator bool() const noexcept {
        return value.has_value() && !error.has_value();
    }
};

std::string_view phase6_data_error_code_name(Phase6DataErrorCode code) noexcept;

Phase6SampleResultV2 materialize_phase6_sample_v2(
    const trajectory::dataset_v3::DatasetManifestV3& manifest,
    const std::vector<trajectory::VerifiedAdmissionReceiptV3>& verified_receipts,
    const trajectory::EpisodeEnvelopeV3& admitted_envelope,
    std::size_t record_index,
    const model::CardVocabularyV1& vocabulary) noexcept;

Phase6DatasetResultV2 materialize_phase6_dataset_v2(
    const trajectory::dataset_v3::DatasetManifestV3& manifest,
    const std::vector<trajectory::VerifiedAdmissionReceiptV3>& verified_receipts,
    const std::vector<trajectory::EpisodeEnvelopeV3>& admitted_envelopes,
    const model::CardVocabularyV1& vocabulary) noexcept;

std::vector<std::uint8_t> canonical_phase6_sample_identity_bytes_v2(
    const Phase6BcSampleV2& sample);
std::string phase6_sample_identity_v2(const Phase6BcSampleV2& sample);

std::optional<Phase6DatasetPartition> phase6_partition_for_episode(
    std::string_view episode_semantic_id) noexcept;

Phase6SplitResult make_phase6_split_v1(
    std::string source_dataset_identity,
    const std::vector<std::string>& episode_semantic_ids) noexcept;

std::vector<std::uint8_t> canonical_phase6_split_identity_bytes(
    const TrainingDatasetSplitV1& split);
std::string phase6_split_identity(const TrainingDatasetSplitV1& split);

}  // namespace ygo::phase6
