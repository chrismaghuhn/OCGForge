#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "ygo/model/encoded_model_input.hpp"

namespace ygo::trajectory {
class VerifiedAdmissionReceipt;
struct EpisodeEnvelope;
}

namespace ygo::model {

inline constexpr std::string_view kModelSupervisionSampleSchemaId =
    "ocgforge.model_supervision_sample.v1";

enum class ModelSupervisionSampleErrorCode : std::uint8_t {
    AdmissionBindingFailure,
    RecordIndexOutOfRange,
    InvalidDecisionRecord,
    PublicProjectionFailure,
    MissingSelectedPublicActionKey,
    DuplicateSelectedPublicActionKey,
    InvalidSelectedPublicActionKey,
    ModelInputMismatch,
    CandidateOrdinalOverflow,
    InvalidSupervisionSample,
    InternalFailure,
};

struct ModelSupervisionSampleError final {
    ModelSupervisionSampleErrorCode code =
        ModelSupervisionSampleErrorCode::InternalFailure;
    std::string diagnostic;
};

struct ModelSupervisionSampleV1 final {
    std::string schema_id = std::string(kModelSupervisionSampleSchemaId);
    std::string model_input_identity;
    std::string source_public_semantic_decision_id;
    std::string selected_public_action_key;
    std::uint32_t candidate_ordinal = 0;
};

using ModelSupervisionSample = ModelSupervisionSampleV1;

struct ModelSupervisionSampleResult final {
    std::optional<ModelSupervisionSampleV1> value;
    std::optional<ModelSupervisionSampleError> error;

    explicit operator bool() const noexcept {
        return value.has_value() && !error.has_value();
    }
};

std::string_view model_supervision_sample_error_code_name(
    ModelSupervisionSampleErrorCode code) noexcept;

ModelSupervisionSampleResult materialize_model_supervision_sample_v1(
    const ygo::trajectory::EpisodeEnvelope& admitted_envelope,
    const ygo::trajectory::VerifiedAdmissionReceipt& admission_receipt,
    std::size_t record_index,
    const LogicalModelInputV1& logical,
    const EncodedModelInputV1& encoded,
    const CardVocabularyV1& vocabulary) noexcept;

inline ModelSupervisionSampleResult materialize_model_supervision_sample(
    const ygo::trajectory::EpisodeEnvelope& admitted_envelope,
    const ygo::trajectory::VerifiedAdmissionReceipt& admission_receipt,
    std::size_t record_index,
    const LogicalModelInputV1& logical,
    const EncodedModelInputV1& encoded,
    const CardVocabularyV1& vocabulary) noexcept {
    return materialize_model_supervision_sample_v1(
        admitted_envelope, admission_receipt, record_index, logical, encoded,
        vocabulary);
}

// Derived, non-authoritative bytes for a supervision value. This is not a
// trajectory identity, dataset identity, or model-input identity.
std::vector<std::uint8_t> canonical_model_supervision_sample_bytes(
    const ModelSupervisionSampleV1& sample);

}  // namespace ygo::model
