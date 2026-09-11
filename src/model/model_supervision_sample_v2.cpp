#include "ygo/model/model_supervision_sample_v2.hpp"

#include <cstddef>
#include <cstdint>
#include <exception>
#include <limits>
#include <new>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "ygo/environment/public_action_identity.hpp"
#include "ygo/trajectory/codec_v3.hpp"
#include "ygo/trajectory/receipt_v3.hpp"
#include "ygo/trajectory/trajectory_identity_v3.hpp"
#include "ygo/trace/sha256.hpp"

namespace ygo::model {
namespace {

class SupervisionFailure final {
public:
    explicit SupervisionFailure(const ModelSupervisionSampleErrorCodeV2 code)
        : code_(code) {}

    ModelSupervisionSampleErrorCodeV2 code() const noexcept { return code_; }

private:
    ModelSupervisionSampleErrorCodeV2 code_;
};

[[noreturn]] void fail(const ModelSupervisionSampleErrorCodeV2 code) {
    throw SupervisionFailure(code);
}

ModelSupervisionSampleResultV2 failure(
    const ModelSupervisionSampleErrorCodeV2 code) noexcept {
    ModelSupervisionSampleResultV2 result;
    result.error = ModelSupervisionSampleErrorV2{code, {}};
    switch (code) {
    case ModelSupervisionSampleErrorCodeV2::AdmissionBindingFailure:
        result.error->diagnostic = "admission binding is invalid";
        break;
    case ModelSupervisionSampleErrorCodeV2::RecordIndexOutOfRange:
        result.error->diagnostic = "trajectory record index is out of range";
        break;
    case ModelSupervisionSampleErrorCodeV2::InvalidDecisionRecord:
        result.error->diagnostic = "trusted decision record is invalid";
        break;
    case ModelSupervisionSampleErrorCodeV2::PublicProjectionFailure:
        result.error->diagnostic = "public model input projection failed";
        break;
    case ModelSupervisionSampleErrorCodeV2::MissingSelectedPublicActionKey:
        result.error->diagnostic = "selected public action key is missing";
        break;
    case ModelSupervisionSampleErrorCodeV2::DuplicateSelectedPublicActionKey:
        result.error->diagnostic = "selected public action key is not unique";
        break;
    case ModelSupervisionSampleErrorCodeV2::InvalidSelectedPublicActionKey:
        result.error->diagnostic = "selected public action key is invalid";
        break;
    case ModelSupervisionSampleErrorCodeV2::ModelInputMismatch:
        result.error->diagnostic = "supplied model input does not match the public frame";
        break;
    case ModelSupervisionSampleErrorCodeV2::CandidateOrdinalOverflow:
        result.error->diagnostic = "candidate ordinal exceeds u32";
        break;
    case ModelSupervisionSampleErrorCodeV2::InvalidSupervisionSample:
        result.error->diagnostic = "supervision sample is invalid";
        break;
    case ModelSupervisionSampleErrorCodeV2::InternalFailure:
        result.error->diagnostic = "supervision sample materialization failed";
        break;
    }
    return result;
}

void validate_sample(const ModelSupervisionSampleV2& sample) {
    if (sample.schema_id != kModelSupervisionSampleV2SchemaId ||
        !ygo::trajectory::is_canonical_identity(
            sample.model_input_identity, kModelInputIdentityV2Prefix) ||
        !ygo::trajectory::is_lower_hex_digest(
            sample.source_public_semantic_decision_id) ||
        !ygo::environment::is_public_action_key_v3(sample.selected_public_action_key)) {
        fail(ModelSupervisionSampleErrorCodeV2::InvalidSupervisionSample);
    }
}

void validate_admitted_envelope(
    const ygo::trajectory::EpisodeEnvelopeV3& envelope,
    const ygo::trajectory::VerifiedAdmissionReceiptV3& admission_receipt,
    const std::size_t record_index) {
    if (record_index >= envelope.records.size()) {
        fail(ModelSupervisionSampleErrorCodeV2::RecordIndexOutOfRange);
    }
    try {
        const auto envelope_bytes =
            ygo::trajectory::canonical_episode_envelope_bytes_v3(envelope);
        const auto envelope_sha256 = ygo::trace::sha256_bytes(envelope_bytes);
        const auto record_id = ygo::trajectory::trajectory_record_id_v3(envelope);
        const auto gameplay_id =
            ygo::trajectory::public_gameplay_trajectory_id_v3(envelope);
        const auto& receipt = admission_receipt.receipt();
        (void)ygo::trajectory::canonical_admission_receipt_bytes_v3(receipt);

        std::size_t matches = 0;
        const ygo::trajectory::AdmissionEntryCommitmentV3* commitment = nullptr;
        for (const auto& entry : receipt.entries) {
            if (entry.trajectory_record_id == record_id) {
                ++matches;
                commitment = &entry;
            }
        }
        if (matches != 1 || commitment == nullptr ||
            commitment->public_gameplay_trajectory_id != gameplay_id ||
            commitment->environment_semantic_id !=
                envelope.manifest.environment_semantic_id ||
            commitment->episode_semantic_id !=
                envelope.manifest.episode_semantic_id ||
            commitment->episode_envelope_sha256 != envelope_sha256) {
            fail(ModelSupervisionSampleErrorCodeV2::AdmissionBindingFailure);
        }
        const auto closure_kind =
            std::holds_alternative<ygo::trajectory::TerminalClosureV3>(
                envelope.closure)
                ? std::uint8_t{0}
                : std::holds_alternative<ygo::trajectory::InterruptedClosureV3>(
                      envelope.closure)
                      ? std::uint8_t{1}
                      : std::uint8_t{255};
        if (commitment->closure_kind != closure_kind) {
            fail(ModelSupervisionSampleErrorCodeV2::AdmissionBindingFailure);
        }
    } catch (const SupervisionFailure&) {
        throw;
    } catch (...) {
        fail(ModelSupervisionSampleErrorCodeV2::AdmissionBindingFailure);
    }
}

ModelSupervisionSampleResultV2 materialize_record_v2(
    const ygo::trajectory::DecisionRecordV3& record,
    const LogicalModelInputV2& logical,
    const EncodedModelInputV2& encoded,
    const CardVocabularyV1& vocabulary) noexcept {
    try {
        try {
            (void)ygo::trajectory::canonical_collection_decision_record_bytes_v3(
                record);
        } catch (...) {
            fail(ModelSupervisionSampleErrorCodeV2::InvalidDecisionRecord);
        }

        if (!ygo::environment::is_public_action_key_v3(
                record.selected_public_action_key)) {
            fail(ModelSupervisionSampleErrorCodeV2::InvalidSelectedPublicActionKey);
        }

        std::size_t selected_matches = 0;
        std::size_t selected_ordinal = 0;
        for (std::size_t index = 0;
             index < record.frame.request.candidates.size(); ++index) {
            if (record.frame.request.candidates[index].public_action_key ==
                record.selected_public_action_key) {
                ++selected_matches;
                selected_ordinal = index;
            }
        }
        if (selected_matches == 0) {
            fail(ModelSupervisionSampleErrorCodeV2::MissingSelectedPublicActionKey);
        }
        if (selected_matches != 1) {
            fail(ModelSupervisionSampleErrorCodeV2::DuplicateSelectedPublicActionKey);
        }
        if (selected_ordinal > std::numeric_limits<std::uint32_t>::max()) {
            fail(ModelSupervisionSampleErrorCodeV2::CandidateOrdinalOverflow);
        }

        const auto projected = project_logical_model_input_v2(
            record.frame.public_observation, record.frame.request.candidates);
        if (!projected || !projected.value.has_value()) {
            fail(ModelSupervisionSampleErrorCodeV2::PublicProjectionFailure);
        }

        try {
            if (canonical_logical_model_input_bytes(*projected.value) !=
                canonical_logical_model_input_bytes(logical)) {
                fail(ModelSupervisionSampleErrorCodeV2::ModelInputMismatch);
            }
        } catch (const SupervisionFailure&) {
            throw;
        } catch (...) {
            fail(ModelSupervisionSampleErrorCodeV2::ModelInputMismatch);
        }

        const auto expected_encoded =
            encode_model_input_v2(*projected.value, vocabulary);
        if (!expected_encoded || !expected_encoded.value.has_value()) {
            fail(ModelSupervisionSampleErrorCodeV2::ModelInputMismatch);
        }
        try {
            if (canonical_encoded_model_input_bytes(*expected_encoded.value) !=
                canonical_encoded_model_input_bytes(encoded)) {
                fail(ModelSupervisionSampleErrorCodeV2::ModelInputMismatch);
            }
        } catch (const SupervisionFailure&) {
            throw;
        } catch (...) {
            fail(ModelSupervisionSampleErrorCodeV2::ModelInputMismatch);
        }

        ModelSupervisionSampleV2 sample;
        sample.model_input_identity = model_input_identity_v2(logical, encoded);
        sample.source_public_semantic_decision_id =
            record.frame.public_semantic_decision_id;
        sample.selected_public_action_key = record.selected_public_action_key;
        sample.candidate_ordinal = static_cast<std::uint32_t>(selected_ordinal);
        (void)canonical_model_supervision_sample_bytes_v2(sample);
        return {std::optional<ModelSupervisionSampleV2>(std::move(sample)),
                std::nullopt};
    } catch (const SupervisionFailure& error) {
        return failure(error.code());
    } catch (const std::bad_alloc&) {
        return failure(ModelSupervisionSampleErrorCodeV2::InternalFailure);
    } catch (...) {
        return failure(ModelSupervisionSampleErrorCodeV2::InternalFailure);
    }
}

}  // namespace

ModelSupervisionSampleResultV2 materialize_model_supervision_sample_v2(
    const ygo::trajectory::EpisodeEnvelopeV3& admitted_envelope,
    const ygo::trajectory::VerifiedAdmissionReceiptV3& admission_receipt,
    const std::size_t record_index,
    const LogicalModelInputV2& logical,
    const EncodedModelInputV2& encoded,
    const CardVocabularyV1& vocabulary) noexcept {
    try {
        validate_admitted_envelope(admitted_envelope, admission_receipt,
                                  record_index);
    } catch (const SupervisionFailure& error) {
        return failure(error.code());
    } catch (const std::bad_alloc&) {
        return failure(ModelSupervisionSampleErrorCodeV2::InternalFailure);
    } catch (...) {
        return failure(ModelSupervisionSampleErrorCodeV2::AdmissionBindingFailure);
    }
    return materialize_record_v2(admitted_envelope.records[record_index], logical,
                                 encoded, vocabulary);
}

std::vector<std::uint8_t> canonical_model_supervision_sample_bytes_v2(
    const ModelSupervisionSampleV2& sample) {
    validate_sample(sample);
    ygo::trajectory::ByteWriter writer;
    writer.string(kModelSupervisionSampleV2SchemaId);
    writer.string(sample.schema_id);
    writer.string(sample.model_input_identity);
    writer.string(sample.source_public_semantic_decision_id);
    writer.string(sample.selected_public_action_key);
    writer.u32be(sample.candidate_ordinal);
    return std::move(writer).take();
}

std::string_view model_supervision_sample_error_code_name(
    const ModelSupervisionSampleErrorCodeV2 code) noexcept {
    switch (code) {
    case ModelSupervisionSampleErrorCodeV2::AdmissionBindingFailure:
        return "admission_binding_failure";
    case ModelSupervisionSampleErrorCodeV2::RecordIndexOutOfRange:
        return "record_index_out_of_range";
    case ModelSupervisionSampleErrorCodeV2::InvalidDecisionRecord:
        return "invalid_decision_record";
    case ModelSupervisionSampleErrorCodeV2::PublicProjectionFailure:
        return "public_projection_failure";
    case ModelSupervisionSampleErrorCodeV2::MissingSelectedPublicActionKey:
        return "missing_selected_public_action_key";
    case ModelSupervisionSampleErrorCodeV2::DuplicateSelectedPublicActionKey:
        return "duplicate_selected_public_action_key";
    case ModelSupervisionSampleErrorCodeV2::InvalidSelectedPublicActionKey:
        return "invalid_selected_public_action_key";
    case ModelSupervisionSampleErrorCodeV2::ModelInputMismatch:
        return "model_input_mismatch";
    case ModelSupervisionSampleErrorCodeV2::CandidateOrdinalOverflow:
        return "candidate_ordinal_overflow";
    case ModelSupervisionSampleErrorCodeV2::InvalidSupervisionSample:
        return "invalid_supervision_sample";
    case ModelSupervisionSampleErrorCodeV2::InternalFailure:
        return "internal_failure";
    }
    return "internal_failure";
}

}  // namespace ygo::model
