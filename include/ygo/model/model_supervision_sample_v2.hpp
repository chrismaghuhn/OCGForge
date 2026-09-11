#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "ygo/model/encoded_model_input_v2.hpp"

namespace ygo::trajectory {
class VerifiedAdmissionReceiptV3;
struct EpisodeEnvelopeV3;
}

namespace ygo::model {

inline constexpr std::string_view kModelSupervisionSampleV2SchemaId =
    "ocgforge.model_supervision_sample.v2";

enum class ModelSupervisionSampleErrorCodeV2 : std::uint8_t {
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

struct ModelSupervisionSampleErrorV2 final {
    ModelSupervisionSampleErrorCodeV2 code =
        ModelSupervisionSampleErrorCodeV2::InternalFailure;
    std::string diagnostic;
};

struct ModelSupervisionSampleV2 final {
    std::string schema_id = std::string(kModelSupervisionSampleV2SchemaId);
    std::string model_input_identity;
    std::string source_public_semantic_decision_id;
    std::string selected_public_action_key;
    std::uint32_t candidate_ordinal = 0;
};

struct ModelSupervisionSampleResultV2 final {
    std::optional<ModelSupervisionSampleV2> value;
    std::optional<ModelSupervisionSampleErrorV2> error;

    explicit operator bool() const noexcept {
        return value.has_value() && !error.has_value();
    }
};

std::string_view model_supervision_sample_error_code_name(
    ModelSupervisionSampleErrorCodeV2 code) noexcept;

ModelSupervisionSampleResultV2 materialize_model_supervision_sample_v2(
    const ygo::trajectory::EpisodeEnvelopeV3& admitted_envelope,
    const ygo::trajectory::VerifiedAdmissionReceiptV3& admission_receipt,
    std::size_t record_index,
    const LogicalModelInputV2& logical,
    const EncodedModelInputV2& encoded,
    const CardVocabularyV1& vocabulary) noexcept;

// Derived, non-authoritative bytes for a supervision value. This is not a
// trajectory identity, dataset identity, or model-input identity.
std::vector<std::uint8_t> canonical_model_supervision_sample_bytes_v2(
    const ModelSupervisionSampleV2& sample);

}  // namespace ygo::model
