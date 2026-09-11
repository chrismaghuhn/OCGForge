#include "ygo/phase6/supervision_dataset_v2.hpp"

#include <algorithm>
#include <cstddef>
#include <set>
#include <stdexcept>
#include <utility>

#include "ygo/model/model_supervision_sample_v2.hpp"
#include "ygo/trace/sha256.hpp"
#include "ygo/trajectory/codec_v3.hpp"
#include "ygo/trajectory/trajectory_identity_v3.hpp"

namespace ygo::phase6 {
namespace {

class Phase6Failure final {
public:
    explicit Phase6Failure(const Phase6DataErrorCode code) : code_(code) {}
    Phase6DataErrorCode code() const noexcept { return code_; }
private:
    Phase6DataErrorCode code_;
};

[[noreturn]] void fail(const Phase6DataErrorCode code) {
    throw Phase6Failure(code);
}

Phase6DataError make_error(const Phase6DataErrorCode code) {
    Phase6DataError result;
    result.code = code;
    result.diagnostic = "Task7 V3 supervision materialization failed";
    return result;
}

Phase6SampleResultV2 sample_failure(const Phase6DataErrorCode code) noexcept {
    Phase6SampleResultV2 result;
    result.error = make_error(code);
    return result;
}

Phase6DatasetResultV2 dataset_failure(const Phase6DataErrorCode code) noexcept {
    Phase6DatasetResultV2 result;
    result.error = make_error(code);
    return result;
}

const trajectory::dataset_v3::DatasetManifestMemberV3* manifest_member_for(
    const trajectory::dataset_v3::DatasetManifestV3& manifest,
    const std::string_view record_id) {
    const auto it = std::find_if(
        manifest.members.begin(), manifest.members.end(),
        [record_id](const auto& member) { return member.trajectory_record_id == record_id; });
    return it == manifest.members.end() ? nullptr : &*it;
}

const trajectory::AdmissionReceiptV3* receipt_for(
    const std::vector<trajectory::VerifiedAdmissionReceiptV3>& receipts,
    const std::string_view receipt_id) {
    for (const auto& verified : receipts) {
        if (trajectory::admission_receipt_id_v3(verified.receipt()) == receipt_id) {
            return &verified.receipt();
        }
    }
    return nullptr;
}

void validate_manifest_envelope(
    const trajectory::dataset_v3::DatasetManifestV3& manifest,
    const std::vector<trajectory::VerifiedAdmissionReceiptV3>& receipts,
    const trajectory::EpisodeEnvelopeV3& envelope,
    std::string& record_id) {
    const auto envelope_bytes = trajectory::canonical_episode_envelope_bytes_v3(envelope);
    const auto envelope_digest = ygo::trace::sha256_bytes(envelope_bytes);
    record_id = trajectory::trajectory_record_id_v3(envelope);
    const auto* member = manifest_member_for(manifest, record_id);
    if (member == nullptr || member->episode_envelope_sha256 != envelope_digest) {
        fail(Phase6DataErrorCode::AdmissionBindingFailure);
    }
    const auto* receipt = receipt_for(receipts, member->admission_receipt_id);
    if (receipt == nullptr) fail(Phase6DataErrorCode::MissingAdmissionReceipt);
    const auto commitment = std::find_if(
        receipt->entries.begin(), receipt->entries.end(),
        [&](const auto& entry) { return entry.trajectory_record_id == record_id; });
    if (commitment == receipt->entries.end() ||
        commitment->public_gameplay_trajectory_id != member->public_gameplay_trajectory_id ||
        commitment->episode_semantic_id != envelope.manifest.episode_semantic_id ||
        commitment->episode_envelope_sha256 != envelope_digest) {
        fail(Phase6DataErrorCode::AdmissionBindingFailure);
    }
}

void validate_public_model_input(
    const model::LogicalModelInputV2& logical,
    const model::EncodedModelInputV2& encoded) {
    (void)model::canonical_logical_model_input_bytes(logical);
    (void)model::canonical_encoded_model_input_bytes(encoded);
}

}  // namespace

std::vector<std::uint8_t> canonical_phase6_sample_identity_bytes_v2(
    const Phase6BcSampleV2& sample) {
    trajectory::ByteWriter writer;
    writer.string(kPhase6BcSampleIdentityDomainV2);
    writer.string(kPhase6BcSampleIdentityDomainV2);
    writer.string(sample.trajectory_record_id);
    writer.string(sample.episode_semantic_id);
    writer.string(sample.supervision.model_input_identity);
    writer.string(sample.supervision.selected_public_action_key);
    writer.string(sample.supervision.source_public_semantic_decision_id);
    writer.u32be(sample.supervision.candidate_ordinal);
    return std::move(writer).take();
}

std::string phase6_sample_identity_v2(const Phase6BcSampleV2& sample) {
    return std::string(kPhase6BcSampleIdentityPrefixV2) +
           ygo::trace::sha256_bytes(canonical_phase6_sample_identity_bytes_v2(sample));
}

Phase6SampleResultV2 materialize_phase6_sample_v2(
    const trajectory::dataset_v3::DatasetManifestV3& manifest,
    const std::vector<trajectory::VerifiedAdmissionReceiptV3>& receipts,
    const trajectory::EpisodeEnvelopeV3& envelope,
    const std::size_t record_index,
    const model::CardVocabularyV1& vocabulary) noexcept {
    try {
        std::string manifest_error;
        if (!trajectory::dataset_v3::validate_dataset_manifest_v3(
                manifest, receipts, &manifest_error)) {
            fail(Phase6DataErrorCode::InvalidDatasetManifest);
        }
        std::string record_id;
        validate_manifest_envelope(manifest, receipts, envelope, record_id);
        if (record_index >= envelope.records.size()) fail(Phase6DataErrorCode::InvalidDecisionRecord);
        if (std::holds_alternative<trajectory::FailedClosureV3>(envelope.closure) ||
            envelope.manifest.collection_disposition.kind != trajectory::CollectionDispositionKind::Clean) {
            fail(Phase6DataErrorCode::FailedOrQuarantinedTrajectory);
        }
        const auto& record = envelope.records[record_index];
        const auto projected = model::project_logical_model_input_v2(
            record.frame.public_observation, record.frame.request.candidates);
        if (!projected || !projected.value.has_value()) fail(Phase6DataErrorCode::ModelInputFailure);
        const auto encoded = model::encode_model_input_v2(*projected.value, vocabulary);
        if (!encoded || !encoded.value.has_value()) fail(Phase6DataErrorCode::ModelInputFailure);
        validate_public_model_input(*projected.value, *encoded.value);
        const auto* member = manifest_member_for(manifest, record_id);
        const auto* receipt = receipt_for(receipts, member->admission_receipt_id);
        if (receipt == nullptr) fail(Phase6DataErrorCode::MissingAdmissionReceipt);
        const auto verified_it = std::find_if(
            receipts.begin(), receipts.end(),
            [&](const auto& value) { return &value.receipt() == receipt; });
        if (verified_it == receipts.end()) fail(Phase6DataErrorCode::MissingAdmissionReceipt);
        const auto sample = model::materialize_model_supervision_sample_v2(
            envelope, *verified_it, record_index, *projected.value, *encoded.value, vocabulary);
        if (!sample || !sample.value.has_value()) fail(Phase6DataErrorCode::ModelInputFailure);
        Phase6BcSampleV2 result;
        result.trajectory_record_id = record_id;
        result.episode_semantic_id = envelope.manifest.episode_semantic_id;
        result.logical_model_input = *projected.value;
        result.encoded_model_input = *encoded.value;
        result.supervision = *sample.value;
        result.sample_identity = phase6_sample_identity_v2(result);
        return {std::optional<Phase6BcSampleV2>(std::move(result)), std::nullopt};
    } catch (const Phase6Failure& error) {
        return sample_failure(error.code());
    } catch (...) {
        return sample_failure(Phase6DataErrorCode::InternalFailure);
    }
}

Phase6DatasetResultV2 materialize_phase6_dataset_v2(
    const trajectory::dataset_v3::DatasetManifestV3& manifest,
    const std::vector<trajectory::VerifiedAdmissionReceiptV3>& receipts,
    const std::vector<trajectory::EpisodeEnvelopeV3>& envelopes,
    const model::CardVocabularyV1& vocabulary) noexcept {
    try {
        std::string error;
        if (!trajectory::dataset_v3::validate_dataset_manifest_v3(manifest, receipts, &error)) {
            fail(Phase6DataErrorCode::InvalidDatasetManifest);
        }
        if (envelopes.size() != manifest.members.size()) {
            fail(envelopes.size() < manifest.members.size()
                     ? Phase6DataErrorCode::MissingEpisodeEnvelope
                     : Phase6DataErrorCode::UnexpectedEpisodeEnvelope);
        }
        std::vector<std::string> episode_ids;
        std::set<std::string> record_ids;
        Phase6MaterializedDatasetV2 result;
        result.source_dataset_identity = manifest.dataset_semantic_id;
        for (const auto& envelope : envelopes) {
            std::string record_id;
            validate_manifest_envelope(manifest, receipts, envelope, record_id);
            if (!record_ids.insert(record_id).second) fail(Phase6DataErrorCode::UnexpectedEpisodeEnvelope);
            episode_ids.push_back(envelope.manifest.episode_semantic_id);
        }
        const auto split = make_phase6_split_v1(manifest.dataset_semantic_id, episode_ids);
        if (!split || !split.value.has_value()) fail(Phase6DataErrorCode::InvalidSplit);
        result.split = *split.value;
        for (const auto& envelope : envelopes) {
            const auto partition = phase6_partition_for_episode(envelope.manifest.episode_semantic_id);
            if (!partition.has_value()) fail(Phase6DataErrorCode::InvalidSplit);
            for (std::size_t index = 0; index < envelope.records.size(); ++index) {
                const auto sample = materialize_phase6_sample_v2(manifest, receipts, envelope, index, vocabulary);
                if (!sample || !sample.value.has_value()) fail(Phase6DataErrorCode::ModelInputFailure);
                switch (*partition) {
                case Phase6DatasetPartition::Train: result.train_samples.push_back(std::move(*sample.value)); break;
                case Phase6DatasetPartition::Validation: result.validation_samples.push_back(std::move(*sample.value)); break;
                case Phase6DatasetPartition::Test: result.test_samples.push_back(std::move(*sample.value)); break;
                }
            }
        }
        if (result.train_samples.empty() || result.validation_samples.empty() ||
            result.test_samples.empty()) fail(Phase6DataErrorCode::InvalidSplit);
        return {std::optional<Phase6MaterializedDatasetV2>(std::move(result)), std::nullopt};
    } catch (const Phase6Failure& error) {
        return dataset_failure(error.code());
    } catch (...) {
        return dataset_failure(Phase6DataErrorCode::InternalFailure);
    }
}

}  // namespace ygo::phase6
