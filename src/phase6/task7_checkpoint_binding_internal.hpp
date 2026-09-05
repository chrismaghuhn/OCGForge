#pragma once

#include <memory>
#include <string>
#include <utility>

namespace ygo::phase6::task5c::detail {

// Source-only state emitted by the accepted future checkpoint/inference
// loader after canonical artifact validation.  This header is intentionally
// not part of the installed public Task5C API.  The current implementation
// does not load a checkpoint artifact.
class MeaningfulCheckpointBindingStateV1 final {
public:
    MeaningfulCheckpointBindingStateV1(const MeaningfulCheckpointBindingStateV1&) = delete;
    MeaningfulCheckpointBindingStateV1& operator=(
        const MeaningfulCheckpointBindingStateV1&) = delete;
    MeaningfulCheckpointBindingStateV1(MeaningfulCheckpointBindingStateV1&&) = delete;
    MeaningfulCheckpointBindingStateV1& operator=(
        MeaningfulCheckpointBindingStateV1&&) = delete;

    bool manifest_validated() const noexcept { return manifest_validated_; }
    const std::string& checkpoint_identity() const noexcept { return checkpoint_identity_; }
    const std::string& model_architecture_config_identity() const noexcept {
        return model_architecture_config_identity_;
    }
    const std::string& phase5_logical_model_input_contract_identity() const noexcept {
        return phase5_logical_model_input_contract_identity_;
    }
    const std::string& phase5_encoded_model_input_contract_identity() const noexcept {
        return phase5_encoded_model_input_contract_identity_;
    }
    const std::string& phase5_batch_layout_contract_identity() const noexcept {
        return phase5_batch_layout_contract_identity_;
    }
    const std::string& card_vocabulary_identity() const noexcept {
        return card_vocabulary_identity_;
    }
    const std::string& dataset_identity() const noexcept { return dataset_identity_; }
    const std::string& dataset_split_identity() const noexcept {
        return dataset_split_identity_;
    }
    const std::string& training_contract_identity() const noexcept {
        return training_contract_identity_;
    }
    const std::string& canonical_weight_export_codec_identity() const noexcept {
        return canonical_weight_export_codec_identity_;
    }
    const std::string& canonical_weight_content_identity() const noexcept {
        return canonical_weight_content_identity_;
    }
    const std::string& task7_materialization_schema_id() const noexcept {
        return task7_materialization_schema_id_;
    }
    const std::string& task7_materialization_config_identity() const noexcept {
        return task7_materialization_config_identity_;
    }

private:
    MeaningfulCheckpointBindingStateV1(
        bool manifest_validated,
        std::string checkpoint_identity,
        std::string model_architecture_config_identity,
        std::string phase5_logical_model_input_contract_identity,
        std::string phase5_encoded_model_input_contract_identity,
        std::string phase5_batch_layout_contract_identity,
        std::string card_vocabulary_identity,
        std::string dataset_identity,
        std::string dataset_split_identity,
        std::string training_contract_identity,
        std::string canonical_weight_export_codec_identity,
        std::string canonical_weight_content_identity,
        std::string task7_materialization_schema_id,
        std::string task7_materialization_config_identity)
        : manifest_validated_(manifest_validated),
          checkpoint_identity_(std::move(checkpoint_identity)),
          model_architecture_config_identity_(std::move(model_architecture_config_identity)),
          phase5_logical_model_input_contract_identity_(
              std::move(phase5_logical_model_input_contract_identity)),
          phase5_encoded_model_input_contract_identity_(
              std::move(phase5_encoded_model_input_contract_identity)),
          phase5_batch_layout_contract_identity_(
              std::move(phase5_batch_layout_contract_identity)),
          card_vocabulary_identity_(std::move(card_vocabulary_identity)),
          dataset_identity_(std::move(dataset_identity)),
          dataset_split_identity_(std::move(dataset_split_identity)),
          training_contract_identity_(std::move(training_contract_identity)),
          canonical_weight_export_codec_identity_(
              std::move(canonical_weight_export_codec_identity)),
          canonical_weight_content_identity_(std::move(canonical_weight_content_identity)),
          task7_materialization_schema_id_(std::move(task7_materialization_schema_id)),
          task7_materialization_config_identity_(
              std::move(task7_materialization_config_identity)) {}

    friend class MeaningfulCheckpointBindingStateFactoryV1;

    bool manifest_validated_ = false;
    std::string checkpoint_identity_;
    std::string model_architecture_config_identity_;
    std::string phase5_logical_model_input_contract_identity_;
    std::string phase5_encoded_model_input_contract_identity_;
    std::string phase5_batch_layout_contract_identity_;
    std::string card_vocabulary_identity_;
    std::string dataset_identity_;
    std::string dataset_split_identity_;
    std::string training_contract_identity_;
    std::string canonical_weight_export_codec_identity_;
    std::string canonical_weight_content_identity_;
    std::string task7_materialization_schema_id_;
    std::string task7_materialization_config_identity_;
};

// Source-only factory for the loader-owned state.  It performs no artifact
// validation itself; the accepted loader must call it only after all frozen
// checkpoint-manifest and export/content checks succeed.
class MeaningfulCheckpointBindingStateFactoryV1 final {
public:
    static std::shared_ptr<const MeaningfulCheckpointBindingStateV1>
    from_loader_validated_artifact(
        bool manifest_validated,
        std::string checkpoint_identity,
        std::string model_architecture_config_identity,
        std::string phase5_logical_model_input_contract_identity,
        std::string phase5_encoded_model_input_contract_identity,
        std::string phase5_batch_layout_contract_identity,
        std::string card_vocabulary_identity,
        std::string dataset_identity,
        std::string dataset_split_identity,
        std::string training_contract_identity,
        std::string canonical_weight_export_codec_identity,
        std::string canonical_weight_content_identity,
        std::string task7_materialization_schema_id,
        std::string task7_materialization_config_identity) {
        return std::shared_ptr<const MeaningfulCheckpointBindingStateV1>(
            new MeaningfulCheckpointBindingStateV1(
                manifest_validated, std::move(checkpoint_identity),
                std::move(model_architecture_config_identity),
                std::move(phase5_logical_model_input_contract_identity),
                std::move(phase5_encoded_model_input_contract_identity),
                std::move(phase5_batch_layout_contract_identity),
                std::move(card_vocabulary_identity), std::move(dataset_identity),
                std::move(dataset_split_identity), std::move(training_contract_identity),
                std::move(canonical_weight_export_codec_identity),
                std::move(canonical_weight_content_identity),
                std::move(task7_materialization_schema_id),
                std::move(task7_materialization_config_identity)));
    }
};

}  // namespace ygo::phase6::task5c::detail
