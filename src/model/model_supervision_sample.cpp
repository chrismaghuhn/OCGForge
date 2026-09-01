#include "ygo/model/model_supervision_sample.hpp"

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
#include "ygo/trajectory/codec.hpp"
#include "ygo/trajectory/receipt.hpp"
#include "ygo/trace/sha256.hpp"

namespace ygo::model {
namespace {

class SupervisionFailure final {
public:
    explicit SupervisionFailure(const ModelSupervisionSampleErrorCode code)
        : code_(code) {}

    ModelSupervisionSampleErrorCode code() const noexcept { return code_; }

private:
    ModelSupervisionSampleErrorCode code_;
};

[[noreturn]] void fail(const ModelSupervisionSampleErrorCode code) {
    throw SupervisionFailure(code);
}

ModelSupervisionSampleResult failure(
    const ModelSupervisionSampleErrorCode code) noexcept {
    ModelSupervisionSampleResult result;
    result.error = ModelSupervisionSampleError{code, {}};
    switch (code) {
    case ModelSupervisionSampleErrorCode::AdmissionBindingFailure:
        result.error->diagnostic = "admission binding is invalid";
        break;
    case ModelSupervisionSampleErrorCode::RecordIndexOutOfRange:
        result.error->diagnostic = "trajectory record index is out of range";
        break;
    case ModelSupervisionSampleErrorCode::InvalidDecisionRecord:
        result.error->diagnostic = "trusted decision record is invalid";
        break;
    case ModelSupervisionSampleErrorCode::PublicProjectionFailure:
        result.error->diagnostic = "public model input projection failed";
        break;
    case ModelSupervisionSampleErrorCode::MissingSelectedPublicActionKey:
        result.error->diagnostic = "selected public action key is missing";
        break;
    case ModelSupervisionSampleErrorCode::DuplicateSelectedPublicActionKey:
        result.error->diagnostic = "selected public action key is not unique";
        break;
    case ModelSupervisionSampleErrorCode::InvalidSelectedPublicActionKey:
        result.error->diagnostic = "selected public action key is invalid";
        break;
    case ModelSupervisionSampleErrorCode::ModelInputMismatch:
        result.error->diagnostic = "supplied model input does not match the public frame";
        break;
    case ModelSupervisionSampleErrorCode::CandidateOrdinalOverflow:
        result.error->diagnostic = "candidate ordinal exceeds u32";
        break;
    case ModelSupervisionSampleErrorCode::InvalidSupervisionSample:
        result.error->diagnostic = "supervision sample is invalid";
        break;
    case ModelSupervisionSampleErrorCode::InternalFailure:
        result.error->diagnostic = "supervision sample materialization failed";
        break;
    }
    return result;
}

void validate_sample(const ModelSupervisionSampleV1& sample) {
    if (sample.schema_id != kModelSupervisionSampleSchemaId ||
        !ygo::trajectory::is_canonical_identity(
            sample.model_input_identity, kModelInputIdentityPrefix) ||
        !ygo::trajectory::is_lower_hex_digest(
            sample.source_public_semantic_decision_id) ||
        !ygo::environment::is_public_action_key(sample.selected_public_action_key)) {
        fail(ModelSupervisionSampleErrorCode::InvalidSupervisionSample);
    }
}

void validate_admitted_envelope(
    const ygo::trajectory::EpisodeEnvelope& envelope,
    const ygo::trajectory::VerifiedAdmissionReceipt& admission_receipt,
    const std::size_t record_index) {
    if (record_index >= envelope.records.size()) {
        fail(ModelSupervisionSampleErrorCode::RecordIndexOutOfRange);
    }
    try {
        const auto envelope_bytes =
            ygo::trajectory::canonical_episode_envelope_bytes(envelope);
        const auto envelope_sha256 = ygo::trace::sha256_bytes(envelope_bytes);
        const auto record_id = ygo::trajectory::trajectory_record_id(envelope);
        const auto gameplay_id =
            ygo::trajectory::public_gameplay_trajectory_id(envelope);
        const auto& receipt = admission_receipt.receipt();
        (void)ygo::trajectory::canonical_admission_receipt_bytes(receipt);

        std::size_t matches = 0;
        const ygo::trajectory::AdmissionEntryCommitment* commitment = nullptr;
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
            fail(ModelSupervisionSampleErrorCode::AdmissionBindingFailure);
        }
        const auto closure_kind =
            std::holds_alternative<ygo::trajectory::TerminalClosure>(
                envelope.closure)
                ? std::uint8_t{0}
                : std::holds_alternative<ygo::trajectory::InterruptedClosure>(
                      envelope.closure)
                      ? std::uint8_t{1}
                      : std::uint8_t{255};
        if (commitment->closure_kind != closure_kind) {
            fail(ModelSupervisionSampleErrorCode::AdmissionBindingFailure);
        }
    } catch (const SupervisionFailure&) {
        throw;
    } catch (...) {
        fail(ModelSupervisionSampleErrorCode::AdmissionBindingFailure);
    }
}

ModelSupervisionSampleResult materialize_record_v1(
    const ygo::trajectory::DecisionRecord& record,
    const LogicalModelInputV1& logical,
    const EncodedModelInputV1& encoded,
    const CardVocabularyV1& vocabulary) noexcept {
    try {
        try {
            (void)ygo::trajectory::canonical_collection_decision_record_bytes(
                record);
        } catch (...) {
            fail(ModelSupervisionSampleErrorCode::InvalidDecisionRecord);
        }

        if (!ygo::environment::is_public_action_key(
                record.selected_public_action_key)) {
            fail(ModelSupervisionSampleErrorCode::InvalidSelectedPublicActionKey);
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
            fail(ModelSupervisionSampleErrorCode::MissingSelectedPublicActionKey);
        }
        if (selected_matches != 1) {
            fail(ModelSupervisionSampleErrorCode::DuplicateSelectedPublicActionKey);
        }
        if (selected_ordinal > std::numeric_limits<std::uint32_t>::max()) {
            fail(ModelSupervisionSampleErrorCode::CandidateOrdinalOverflow);
        }

        const auto projected = project_logical_model_input_v1(
            record.frame.public_observation, record.frame.request.candidates);
        if (!projected || !projected.value.has_value()) {
            fail(ModelSupervisionSampleErrorCode::PublicProjectionFailure);
        }

        try {
            if (canonical_logical_model_input_bytes(*projected.value) !=
                canonical_logical_model_input_bytes(logical)) {
                fail(ModelSupervisionSampleErrorCode::ModelInputMismatch);
            }
        } catch (const SupervisionFailure&) {
            throw;
        } catch (...) {
            fail(ModelSupervisionSampleErrorCode::ModelInputMismatch);
        }

        const auto expected_encoded =
            encode_model_input_v1(*projected.value, vocabulary);
        if (!expected_encoded || !expected_encoded.value.has_value()) {
            fail(ModelSupervisionSampleErrorCode::ModelInputMismatch);
        }
        try {
            if (canonical_encoded_model_input_bytes(*expected_encoded.value) !=
                canonical_encoded_model_input_bytes(encoded)) {
                fail(ModelSupervisionSampleErrorCode::ModelInputMismatch);
            }
        } catch (const SupervisionFailure&) {
            throw;
        } catch (...) {
            fail(ModelSupervisionSampleErrorCode::ModelInputMismatch);
        }

        ModelSupervisionSampleV1 sample;
        sample.model_input_identity = model_input_identity(logical, encoded);
        sample.source_public_semantic_decision_id =
            record.frame.public_semantic_decision_id;
        sample.selected_public_action_key = record.selected_public_action_key;
        sample.candidate_ordinal = static_cast<std::uint32_t>(selected_ordinal);
        (void)canonical_model_supervision_sample_bytes(sample);
        return {std::optional<ModelSupervisionSampleV1>(std::move(sample)),
                std::nullopt};
    } catch (const SupervisionFailure& error) {
        return failure(error.code());
    } catch (const std::bad_alloc&) {
        return failure(ModelSupervisionSampleErrorCode::InternalFailure);
    } catch (...) {
        return failure(ModelSupervisionSampleErrorCode::InternalFailure);
    }
}

}  // namespace

ModelSupervisionSampleResult materialize_model_supervision_sample_v1(
    const ygo::trajectory::EpisodeEnvelope& admitted_envelope,
    const ygo::trajectory::VerifiedAdmissionReceipt& admission_receipt,
    const std::size_t record_index,
    const LogicalModelInputV1& logical,
    const EncodedModelInputV1& encoded,
    const CardVocabularyV1& vocabulary) noexcept {
    try {
        validate_admitted_envelope(admitted_envelope, admission_receipt,
                                  record_index);
    } catch (const SupervisionFailure& error) {
        return failure(error.code());
    } catch (const std::bad_alloc&) {
        return failure(ModelSupervisionSampleErrorCode::InternalFailure);
    } catch (...) {
        return failure(ModelSupervisionSampleErrorCode::AdmissionBindingFailure);
    }
    return materialize_record_v1(admitted_envelope.records[record_index], logical,
                                 encoded, vocabulary);
}

std::vector<std::uint8_t> canonical_model_supervision_sample_bytes(
    const ModelSupervisionSampleV1& sample) {
    validate_sample(sample);
    ygo::trajectory::ByteWriter writer;
    writer.string(kModelSupervisionSampleSchemaId);
    writer.string(sample.schema_id);
    writer.string(sample.model_input_identity);
    writer.string(sample.source_public_semantic_decision_id);
    writer.string(sample.selected_public_action_key);
    writer.u32be(sample.candidate_ordinal);
    return std::move(writer).take();
}

std::string_view model_supervision_sample_error_code_name(
    const ModelSupervisionSampleErrorCode code) noexcept {
    switch (code) {
    case ModelSupervisionSampleErrorCode::AdmissionBindingFailure:
        return "admission_binding_failure";
    case ModelSupervisionSampleErrorCode::RecordIndexOutOfRange:
        return "record_index_out_of_range";
    case ModelSupervisionSampleErrorCode::InvalidDecisionRecord:
        return "invalid_decision_record";
    case ModelSupervisionSampleErrorCode::PublicProjectionFailure:
        return "public_projection_failure";
    case ModelSupervisionSampleErrorCode::MissingSelectedPublicActionKey:
        return "missing_selected_public_action_key";
    case ModelSupervisionSampleErrorCode::DuplicateSelectedPublicActionKey:
        return "duplicate_selected_public_action_key";
    case ModelSupervisionSampleErrorCode::InvalidSelectedPublicActionKey:
        return "invalid_selected_public_action_key";
    case ModelSupervisionSampleErrorCode::ModelInputMismatch:
        return "model_input_mismatch";
    case ModelSupervisionSampleErrorCode::CandidateOrdinalOverflow:
        return "candidate_ordinal_overflow";
    case ModelSupervisionSampleErrorCode::InvalidSupervisionSample:
        return "invalid_supervision_sample";
    case ModelSupervisionSampleErrorCode::InternalFailure:
        return "internal_failure";
    }
    return "internal_failure";
}

}  // namespace ygo::model
