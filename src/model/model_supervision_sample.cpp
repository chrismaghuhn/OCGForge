#include "ygo/model/model_supervision_sample.hpp"

#include <cstddef>
#include <cstdint>
#include <exception>
#include <limits>
#include <new>
#include <string>
#include <utility>
#include <vector>

#include "ygo/environment/public_action_identity.hpp"
#include "ygo/trajectory/codec.hpp"

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

}  // namespace

ModelSupervisionSampleResult materialize_model_supervision_sample_v1(
    const ygo::trajectory::DecisionRecord& record,
    const LogicalModelInputV1& logical,
    const EncodedModelInputV1& encoded,
    const CardVocabularyV1& vocabulary) noexcept {
    try {
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

        try {
            (void)ygo::trajectory::canonical_public_decision_record_bytes(record);
        } catch (...) {
            fail(ModelSupervisionSampleErrorCode::InvalidDecisionRecord);
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
