#include "ygo/phase6/supervision_dataset.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <limits>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "ygo/environment/episodic_environment.hpp"
#include "ygo/trace/sha256.hpp"
#include "ygo/trajectory/codec.hpp"
#include "ygo/trajectory/identity_resolver.hpp"
#include "ygo/trajectory/receipt.hpp"

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
    switch (code) {
    case Phase6DataErrorCode::InvalidDatasetManifest:
        result.diagnostic = "dataset manifest is invalid";
        break;
    case Phase6DataErrorCode::MissingAdmissionReceipt:
        result.diagnostic = "dataset member lacks a verified admission receipt";
        break;
    case Phase6DataErrorCode::AdmissionBindingFailure:
        result.diagnostic = "admitted envelope does not match its receipt commitment";
        break;
    case Phase6DataErrorCode::MissingEpisodeEnvelope:
        result.diagnostic = "dataset member lacks its admitted episode envelope";
        break;
    case Phase6DataErrorCode::UnexpectedEpisodeEnvelope:
        result.diagnostic = "episode envelope is not a DatasetManifest member";
        break;
    case Phase6DataErrorCode::FailedOrQuarantinedTrajectory:
        result.diagnostic = "failed or quarantined trajectory is not learner eligible";
        break;
    case Phase6DataErrorCode::InvalidCertifiedEnvironment:
        result.diagnostic = "episode does not bind to the current certified environment";
        break;
    case Phase6DataErrorCode::IneligibleTeacherPolicy:
        result.diagnostic = "trajectory policy is not an eligible accepted Teacher source";
        break;
    case Phase6DataErrorCode::InvalidDecisionRecord:
        result.diagnostic = "trusted decision record is invalid";
        break;
    case Phase6DataErrorCode::ModelInputFailure:
        result.diagnostic = "Phase-5 public model input derivation failed";
        break;
    case Phase6DataErrorCode::CandidateDomainFailure:
        result.diagnostic = "public candidate domain was changed or is incomplete";
        break;
    case Phase6DataErrorCode::CandidateCapacityFailure:
        result.diagnostic = "physical candidate capacity is smaller than the supplied domain";
        break;
    case Phase6DataErrorCode::DuplicateSampleIdentity:
        result.diagnostic = "derived sample identity is duplicated with conflicting membership";
        break;
    case Phase6DataErrorCode::InvalidSplit:
        result.diagnostic = "deterministic dataset split is invalid";
        break;
    case Phase6DataErrorCode::InternalFailure:
        result.diagnostic = "Phase-6 dataset materialization failed";
        break;
    }
    return result;
}

Phase6SampleResult sample_failure(const Phase6DataErrorCode code) noexcept {
    Phase6SampleResult result;
    result.error = make_error(code);
    return result;
}

Phase6SplitResult split_failure(const Phase6DataErrorCode code) noexcept {
    Phase6SplitResult result;
    result.error = make_error(code);
    return result;
}

Phase6DatasetResult dataset_failure(const Phase6DataErrorCode code) noexcept {
    Phase6DatasetResult result;
    result.error = make_error(code);
    return result;
}

struct TeacherSource final {
    std::string_view profile_id;
    std::string_view binding_id;
    std::string_view artifact_id;
    std::string_view deck_id;
    std::string_view deck_sha256;
};

constexpr TeacherSource kSwordsoulSource = {
    "ocgforge.strategy_profile.v1.7a96ab091b52b8988a6873beb3b7d58575d5ea6f0e0aa7bf5059a1c87a748f74",
    "ocgforge.teacher_policy_binding.v1.4f78a100a75f98b8c5a7845198984a8ea34db8b6a75b6fde396c19d2b3ca6d0c",
    "policy_artifact.v1.52f56b550a2a674430439d3db104a0b2281b69df79891573e4d71967e3d4310d",
    "ocgforge.swordsoul_tenyi.ml_v1",
    "8ee4b699de19ff256e388d46f35b8696a60ff6ec59f0324f060a2468876711b7"};

constexpr TeacherSource kSalamangreatSource = {
    "ocgforge.strategy_profile.v1.3499e34962230eda64e9ef52af53433272cda5ca45ffae61258e0809dbfefa55",
    "ocgforge.teacher_policy_binding.v1.ecbf2ae56dab29e93f319399a08930a3700466cd3d9ab553ef964fc109846c56",
    "policy_artifact.v1.a68642ee28f0dd53ebe4908994664f178b3d5cea6fb7c06421990729cd9c4527",
    "ocgforge.salamangreat.ml_v1",
    "6041abe0a59463d0715ae1da9100090ad487de02a02794e8ec0686d4c0513188"};

constexpr std::string_view kTeacherProducer = "ocgforge.policy.teacher_core.v1";
constexpr std::string_view kTeacherSampling =
    "ocgforge.policy.deterministic_lexicographic_argmax.v1";
constexpr std::string_view kDirectExecution = "ocgforge.policy.direct_execution.v1";
constexpr std::string_view kPublicObservation = "ocgforge.policy.public_observation.v1";
constexpr std::string_view kPublicActionKey = "ocgforge.policy.public_action_key.v1";

const TeacherSource& source_for_deck(const trajectory::DeckRole role) {
    switch (role) {
    case trajectory::DeckRole::FirstLockedDeck:
        return kSwordsoulSource;
    case trajectory::DeckRole::SecondLockedDeck:
        return kSalamangreatSource;
    }
    fail(Phase6DataErrorCode::IneligibleTeacherPolicy);
}

const trajectory::PolicyArtifact* artifact_for(
    const trajectory::PolicyProvenanceEnvelope& provenance,
    const std::string_view artifact_id) noexcept {
    const auto found = std::find_if(
        provenance.policy_artifacts.begin(), provenance.policy_artifacts.end(),
        [artifact_id](const auto& artifact) {
            return artifact.policy_artifact_id == artifact_id;
        });
    return found == provenance.policy_artifacts.end() ? nullptr : &*found;
}

const trajectory::ParticipantPolicyAssignment* assignment_for(
    const trajectory::PolicyProvenanceEnvelope& provenance,
    const std::string_view assignment_id) noexcept {
    const auto found = std::find_if(
        provenance.participant_assignments.begin(),
        provenance.participant_assignments.end(),
        [assignment_id](const auto& assignment) {
            return assignment.participant_policy_assignment_id == assignment_id;
        });
    return found == provenance.participant_assignments.end() ? nullptr : &*found;
}

void validate_teacher_artifact(const trajectory::PolicyArtifact& artifact,
                               const TeacherSource& expected) {
    if (artifact.policy_artifact_id != expected.artifact_id ||
        artifact.policy_kind != trajectory::PolicyKind::DeterministicHeuristic ||
        artifact.producer_implementation_identity != kTeacherProducer ||
        artifact.inference_adapter_identity != kDirectExecution ||
        artifact.observation_adapter_identity != kPublicObservation ||
        artifact.action_adapter_identity != kPublicActionKey ||
        artifact.sampling_contract_identity != kTeacherSampling ||
        artifact.policy_rng_contract_identity != trajectory::kNoPolicyRngContractId ||
        artifact.model_checkpoint_identity.has_value() ||
        artifact.search_contract_identity.has_value() ||
        artifact.demonstration_source_identity.has_value() ||
        artifact.artifact_metadata_identity !=
            std::optional<std::string>{std::string(expected.binding_id)}) {
        fail(Phase6DataErrorCode::IneligibleTeacherPolicy);
    }
}

void validate_teacher_provenance(const trajectory::EpisodeEnvelope& envelope) {
    const auto& provenance = envelope.manifest.policy_provenance;
    if (provenance.policy_artifacts.size() != 2 ||
        provenance.participant_assignments.size() != 2) {
        fail(Phase6DataErrorCode::IneligibleTeacherPolicy);
    }

    bool saw_swordsoul = false;
    bool saw_salamangreat = false;
    for (const auto& artifact : provenance.policy_artifacts) {
        if (artifact.policy_artifact_id == kSwordsoulSource.artifact_id) {
            if (saw_swordsoul) fail(Phase6DataErrorCode::IneligibleTeacherPolicy);
            saw_swordsoul = true;
            validate_teacher_artifact(artifact, kSwordsoulSource);
        } else if (artifact.policy_artifact_id == kSalamangreatSource.artifact_id) {
            if (saw_salamangreat) fail(Phase6DataErrorCode::IneligibleTeacherPolicy);
            saw_salamangreat = true;
            validate_teacher_artifact(artifact, kSalamangreatSource);
        } else {
            fail(Phase6DataErrorCode::IneligibleTeacherPolicy);
        }
    }
    if (!saw_swordsoul || !saw_salamangreat) {
        fail(Phase6DataErrorCode::IneligibleTeacherPolicy);
    }

    std::array<bool, 2> players{};
    for (const auto& assignment : provenance.participant_assignments) {
        if (assignment.player > 1 || players[assignment.player]) {
            fail(Phase6DataErrorCode::IneligibleTeacherPolicy);
        }
        players[assignment.player] = true;
        const auto& expected = source_for_deck(assignment.deck_role);
        if (assignment.policy_artifact_id != expected.artifact_id ||
            assignment.resolved_locked_deck_id != expected.deck_id ||
            assignment.resolved_locked_deck_sha256 != expected.deck_sha256 ||
            (assignment.policy_role != trajectory::PolicyRole::Behavior &&
             assignment.policy_role != trajectory::PolicyRole::Opponent) ||
            artifact_for(provenance, assignment.policy_artifact_id) == nullptr) {
            fail(Phase6DataErrorCode::IneligibleTeacherPolicy);
        }
    }
    if (!players[0] || !players[1]) {
        fail(Phase6DataErrorCode::IneligibleTeacherPolicy);
    }

    for (const auto& record : envelope.records) {
        const auto* assignment = assignment_for(
            provenance, record.acting_policy_assignment_id);
        if (assignment == nullptr || assignment->player != record.frame.acting_player ||
            (assignment->policy_role != trajectory::PolicyRole::Behavior &&
             assignment->policy_role != trajectory::PolicyRole::Opponent)) {
            fail(Phase6DataErrorCode::IneligibleTeacherPolicy);
        }
        const auto* artifact = artifact_for(provenance, assignment->policy_artifact_id);
        if (artifact == nullptr) fail(Phase6DataErrorCode::IneligibleTeacherPolicy);
        validate_teacher_artifact(*artifact, source_for_deck(assignment->deck_role));
    }
}

void validate_current_episode(const trajectory::EpisodeEnvelope& envelope) {
    const auto config = trajectory::decode_environment_identity_input(
        envelope.manifest.environment_identity_input);
    if (!config || !config.value.has_value() ||
        !trajectory::is_current_certified_environment(*config.value) ||
        config.value->environment_semantic_id !=
            envelope.manifest.environment_semantic_id ||
        environment::environment_semantic_id(*config.value) !=
            envelope.manifest.environment_semantic_id) {
        fail(Phase6DataErrorCode::InvalidCertifiedEnvironment);
    }
    const auto spec = trajectory::decode_episode_identity_input(
        envelope.manifest.episode_identity_input, *config.value);
    if (!spec || !spec.value.has_value() ||
        environment::episode_semantic_id(*config.value, *spec.value) !=
            envelope.manifest.episode_semantic_id) {
        fail(Phase6DataErrorCode::InvalidCertifiedEnvironment);
    }
}

const trajectory::DatasetManifestMember* manifest_member_for(
    const trajectory::DatasetManifest& manifest,
    const std::string_view trajectory_record_id) noexcept {
    const auto found = std::find_if(
        manifest.members.begin(), manifest.members.end(),
        [trajectory_record_id](const auto& member) {
            return member.trajectory_record_id == trajectory_record_id;
        });
    return found == manifest.members.end() ? nullptr : &*found;
}

const trajectory::VerifiedAdmissionReceipt* receipt_for(
    const std::vector<trajectory::VerifiedAdmissionReceipt>& receipts,
    const std::string_view receipt_id) {
    for (const auto& verified : receipts) {
        if (trajectory::admission_receipt_id(verified.receipt()) == receipt_id) {
            return &verified;
        }
    }
    return nullptr;
}

const trajectory::AdmissionEntryCommitment* commitment_for(
    const trajectory::AdmissionReceipt& receipt,
    const std::string_view trajectory_record_id) noexcept {
    const auto found = std::find_if(
        receipt.entries.begin(), receipt.entries.end(),
        [trajectory_record_id](const auto& commitment) {
            return commitment.trajectory_record_id == trajectory_record_id;
        });
    return found == receipt.entries.end() ? nullptr : &*found;
}

const trajectory::DatasetManifestMember& validate_manifest_envelope(
    const trajectory::DatasetManifest& manifest,
    const std::vector<trajectory::VerifiedAdmissionReceipt>& verified_receipts,
    const trajectory::EpisodeEnvelope& envelope,
    std::string& trajectory_record_id) {
    if (envelope.manifest.collection_disposition.kind !=
            trajectory::CollectionDispositionKind::Clean ||
        std::holds_alternative<trajectory::FailedClosure>(envelope.closure)) {
        fail(Phase6DataErrorCode::FailedOrQuarantinedTrajectory);
    }
    const auto envelope_bytes = trajectory::canonical_episode_envelope_bytes(envelope);
    trajectory_record_id = trajectory::trajectory_record_id(envelope);
    const auto* member = manifest_member_for(manifest, trajectory_record_id);
    if (member == nullptr) {
        fail(Phase6DataErrorCode::UnexpectedEpisodeEnvelope);
    }
    const auto envelope_sha256 = trace::sha256_bytes(envelope_bytes);
    const auto gameplay_id = trajectory::public_gameplay_trajectory_id(envelope);
    if (member->public_gameplay_trajectory_id != gameplay_id ||
        member->episode_envelope_sha256 != envelope_sha256) {
        fail(Phase6DataErrorCode::AdmissionBindingFailure);
    }
    const auto* verified = receipt_for(verified_receipts, member->admission_receipt_id);
    if (verified == nullptr) {
        fail(Phase6DataErrorCode::MissingAdmissionReceipt);
    }
    const auto* commitment = commitment_for(verified->receipt(), trajectory_record_id);
    if (commitment == nullptr ||
        commitment->public_gameplay_trajectory_id != gameplay_id ||
        commitment->environment_semantic_id != envelope.manifest.environment_semantic_id ||
        commitment->episode_semantic_id != envelope.manifest.episode_semantic_id ||
        commitment->episode_envelope_sha256 != envelope_sha256 ||
        member->candidate_shard_artifact_sha256 !=
            verified->receipt().candidate_shard_artifact_sha256 ||
        commitment->closure_kind !=
            (std::holds_alternative<trajectory::TerminalClosure>(envelope.closure) ? 0 : 1)) {
        fail(Phase6DataErrorCode::AdmissionBindingFailure);
    }
    validate_current_episode(envelope);
    validate_teacher_provenance(envelope);
    return *member;
}

void validate_dataset_manifest(
    const trajectory::DatasetManifest& manifest,
    const std::vector<trajectory::VerifiedAdmissionReceipt>& verified_receipts) {
    std::string error;
    if (!trajectory::dataset::validate_dataset_manifest(
            manifest, verified_receipts, &error)) {
        fail(Phase6DataErrorCode::InvalidDatasetManifest);
    }
}

void validate_sample(const Phase6BcSampleV1& sample) {
    if (sample.schema_id != kPhase6BcSampleIdentityDomain ||
        !trajectory::is_canonical_identity(sample.trajectory_record_id,
                                            "trajectory_record.v1.") ||
        !trajectory::is_lower_hex_digest(sample.episode_semantic_id) ||
        sample.supervision.schema_id != model::kModelSupervisionSampleSchemaId ||
        !trajectory::is_lower_hex_digest(
            sample.supervision.source_public_semantic_decision_id) ||
        !environment::is_public_action_key(sample.supervision.selected_public_action_key) ||
        !trajectory::is_canonical_identity(
            sample.supervision.model_input_identity, model::kModelInputIdentityPrefix)) {
        fail(Phase6DataErrorCode::InvalidDecisionRecord);
    }
    if (sample.logical_model_input.candidate_count() == 0 ||
        sample.logical_model_input.candidate_routing.size() !=
            sample.logical_model_input.candidate_features.size() ||
        sample.encoded_model_input.candidate_count() !=
            sample.logical_model_input.candidate_count() ||
        sample.encoded_model_input.routing_keys.size() !=
            sample.logical_model_input.candidate_count()) {
        fail(Phase6DataErrorCode::CandidateDomainFailure);
    }
    std::set<std::string> keys;
    for (std::size_t index = 0;
         index < sample.logical_model_input.candidate_count(); ++index) {
        const auto& key = sample.logical_model_input.candidate_routing[index].public_action_key;
        if (!environment::is_public_action_key(key) || !keys.insert(key).second ||
            sample.encoded_model_input.routing_keys[index] != key) {
            fail(Phase6DataErrorCode::CandidateDomainFailure);
        }
    }
    if (sample.supervision.candidate_ordinal >=
            sample.logical_model_input.candidate_count() ||
        sample.supervision.selected_public_action_key !=
            sample.logical_model_input.candidate_routing[
                sample.supervision.candidate_ordinal].public_action_key ||
        model::model_input_identity(sample.logical_model_input,
                                    sample.encoded_model_input) !=
            sample.supervision.model_input_identity) {
        fail(Phase6DataErrorCode::ModelInputFailure);
    }
    try {
        (void)model::canonical_logical_model_input_bytes(sample.logical_model_input);
        (void)model::canonical_encoded_model_input_bytes(sample.encoded_model_input);
        (void)model::canonical_model_supervision_sample_bytes(sample.supervision);
    } catch (...) {
        fail(Phase6DataErrorCode::ModelInputFailure);
    }
}

Phase6BcSampleV1 materialize_sample_unchecked(
    const trajectory::EpisodeEnvelope& envelope,
    const trajectory::VerifiedAdmissionReceipt& receipt,
    const std::size_t record_index,
    const std::string_view trajectory_record_id,
    const model::CardVocabularyV1& vocabulary) {
    if (record_index >= envelope.records.size()) {
        fail(Phase6DataErrorCode::InvalidDecisionRecord);
    }
    const auto& record = envelope.records[record_index];
    const auto logical = model::project_logical_model_input_v1(
        record.frame.public_observation, record.frame.request.candidates);
    if (!logical || !logical.value.has_value()) {
        fail(Phase6DataErrorCode::ModelInputFailure);
    }
    const auto encoded = model::encode_model_input_v1(*logical.value, vocabulary);
    if (!encoded || !encoded.value.has_value()) {
        fail(Phase6DataErrorCode::ModelInputFailure);
    }
    const auto supervision = model::materialize_model_supervision_sample_v1(
        envelope, receipt, record_index, *logical.value, *encoded.value, vocabulary);
    if (!supervision || !supervision.value.has_value()) {
        if (supervision.error.has_value() &&
            supervision.error->code ==
                model::ModelSupervisionSampleErrorCode::AdmissionBindingFailure) {
            fail(Phase6DataErrorCode::AdmissionBindingFailure);
        }
        fail(Phase6DataErrorCode::InvalidDecisionRecord);
    }

    Phase6BcSampleV1 sample;
    sample.trajectory_record_id = std::string(trajectory_record_id);
    sample.episode_semantic_id = envelope.manifest.episode_semantic_id;
    sample.supervision = *supervision.value;
    sample.logical_model_input = *logical.value;
    sample.encoded_model_input = *encoded.value;
    validate_sample(sample);
    sample.sample_identity = phase6_sample_identity(sample);
    return sample;
}

std::uint8_t hex_value(const char value) noexcept {
    if (value >= '0' && value <= '9') return static_cast<std::uint8_t>(value - '0');
    if (value >= 'a' && value <= 'f') return static_cast<std::uint8_t>(value - 'a' + 10);
    return 0;
}

bool strict_sorted_unique(const std::vector<std::string>& values) noexcept {
    return std::adjacent_find(values.begin(), values.end(),
                               [](const auto& left, const auto& right) {
                                   return left >= right;
                               }) == values.end();
}

void validate_split_shape(const TrainingDatasetSplitV1& split) {
    if (split.schema_id != kPhase6DatasetSplitContractId ||
        split.split_contract_identity != kPhase6DatasetSplitContractId ||
        split.split_seed_or_partition_identity != kPhase6SplitPartitionIdentity ||
        !trajectory::is_lower_hex_digest(split.source_dataset_identity) ||
        !strict_sorted_unique(split.train_episode_ids) ||
        !strict_sorted_unique(split.validation_episode_ids) ||
        !strict_sorted_unique(split.test_episode_ids)) {
        fail(Phase6DataErrorCode::InvalidSplit);
    }
    if (split.train_episode_ids.size() > std::numeric_limits<std::uint32_t>::max() ||
        split.validation_episode_ids.size() > std::numeric_limits<std::uint32_t>::max() ||
        split.test_episode_ids.size() > std::numeric_limits<std::uint32_t>::max()) {
        fail(Phase6DataErrorCode::InvalidSplit);
    }
    std::set<std::string> all;
    for (const auto* group : {&split.train_episode_ids,
                              &split.validation_episode_ids,
                              &split.test_episode_ids}) {
        for (const auto& id : *group) {
            if (!trajectory::is_lower_hex_digest(id) || !all.insert(id).second) {
                fail(Phase6DataErrorCode::InvalidSplit);
            }
        }
    }
}

Phase6DatasetPartition partition_for_episode_unchecked(
    const std::string_view episode_semantic_id) {
    trajectory::ByteWriter writer;
    writer.string(kPhase6SplitPartitionIdentity);
    writer.string(episode_semantic_id);
    const auto digest = trace::sha256_bytes(writer.data());
    std::uint64_t value = 0;
    for (std::size_t index = 0; index < 16; ++index) {
        value = (value << 4) | hex_value(digest[index]);
    }
    const auto bucket = value % 1000;
    if (bucket < 800) return Phase6DatasetPartition::Train;
    if (bucket < 900) return Phase6DatasetPartition::Validation;
    return Phase6DatasetPartition::Test;
}

}  // namespace

std::string_view phase6_data_error_code_name(
    const Phase6DataErrorCode code) noexcept {
    switch (code) {
    case Phase6DataErrorCode::InvalidDatasetManifest:
        return "invalid_dataset_manifest";
    case Phase6DataErrorCode::MissingAdmissionReceipt:
        return "missing_admission_receipt";
    case Phase6DataErrorCode::AdmissionBindingFailure:
        return "admission_binding_failure";
    case Phase6DataErrorCode::MissingEpisodeEnvelope:
        return "missing_episode_envelope";
    case Phase6DataErrorCode::UnexpectedEpisodeEnvelope:
        return "unexpected_episode_envelope";
    case Phase6DataErrorCode::FailedOrQuarantinedTrajectory:
        return "failed_or_quarantined_trajectory";
    case Phase6DataErrorCode::InvalidCertifiedEnvironment:
        return "invalid_certified_environment";
    case Phase6DataErrorCode::IneligibleTeacherPolicy:
        return "ineligible_teacher_policy";
    case Phase6DataErrorCode::InvalidDecisionRecord:
        return "invalid_decision_record";
    case Phase6DataErrorCode::ModelInputFailure:
        return "model_input_failure";
    case Phase6DataErrorCode::CandidateDomainFailure:
        return "candidate_domain_failure";
    case Phase6DataErrorCode::CandidateCapacityFailure:
        return "candidate_capacity_failure";
    case Phase6DataErrorCode::DuplicateSampleIdentity:
        return "duplicate_sample_identity";
    case Phase6DataErrorCode::InvalidSplit:
        return "invalid_split";
    case Phase6DataErrorCode::InternalFailure:
        return "internal_failure";
    }
    return "internal_failure";
}

std::vector<std::uint8_t> canonical_phase6_sample_identity_bytes(
    const Phase6BcSampleV1& sample) {
    validate_sample(sample);
    trajectory::ByteWriter writer;
    writer.string(kPhase6BcSampleIdentityDomain);
    writer.string(kPhase6BcSampleIdentityDomain);
    writer.string(sample.trajectory_record_id);
    writer.string(sample.episode_semantic_id);
    writer.string(sample.supervision.source_public_semantic_decision_id);
    writer.string(sample.supervision.model_input_identity);
    writer.string(sample.supervision.selected_public_action_key);
    writer.u32be(sample.supervision.candidate_ordinal);
    return std::move(writer).take();
}

std::string phase6_sample_identity(const Phase6BcSampleV1& sample) {
    return std::string(kPhase6BcSampleIdentityPrefix) +
           trace::sha256_bytes(canonical_phase6_sample_identity_bytes(sample));
}

std::optional<Phase6DatasetPartition> phase6_partition_for_episode(
    const std::string_view episode_semantic_id) noexcept {
    if (!trajectory::is_lower_hex_digest(episode_semantic_id)) {
        return std::nullopt;
    }
    try {
        return partition_for_episode_unchecked(episode_semantic_id);
    } catch (...) {
        return std::nullopt;
    }
}

std::vector<std::uint8_t> canonical_phase6_split_identity_bytes(
    const TrainingDatasetSplitV1& split) {
    validate_split_shape(split);
    trajectory::ByteWriter writer;
    writer.string(kPhase6DatasetSplitIdentityDomain);
    writer.string(kPhase6DatasetSplitIdentityDomain);
    writer.string(split.source_dataset_identity);
    writer.string(split.split_contract_identity);
    writer.string(split.split_seed_or_partition_identity);
    writer.u32be(static_cast<std::uint32_t>(split.train_episode_ids.size()));
    for (const auto& id : split.train_episode_ids) writer.string(id);
    writer.u32be(static_cast<std::uint32_t>(split.validation_episode_ids.size()));
    for (const auto& id : split.validation_episode_ids) writer.string(id);
    writer.u32be(static_cast<std::uint32_t>(split.test_episode_ids.size()));
    for (const auto& id : split.test_episode_ids) writer.string(id);
    return std::move(writer).take();
}

std::string phase6_split_identity(const TrainingDatasetSplitV1& split) {
    return std::string(kPhase6DatasetSplitIdentityPrefix) +
           trace::sha256_bytes(canonical_phase6_split_identity_bytes(split));
}

Phase6SplitResult make_phase6_split_v1(
    std::string source_dataset_identity,
    const std::vector<std::string>& episode_semantic_ids) noexcept {
    try {
        if (!trajectory::is_lower_hex_digest(source_dataset_identity)) {
            fail(Phase6DataErrorCode::InvalidSplit);
        }
        auto sorted_ids = episode_semantic_ids;
        std::sort(sorted_ids.begin(), sorted_ids.end());
        if (!strict_sorted_unique(sorted_ids)) {
            fail(Phase6DataErrorCode::InvalidSplit);
        }
        TrainingDatasetSplitV1 split;
        split.source_dataset_identity = std::move(source_dataset_identity);
        for (const auto& id : sorted_ids) {
            if (!trajectory::is_lower_hex_digest(id)) {
                fail(Phase6DataErrorCode::InvalidSplit);
            }
            switch (partition_for_episode_unchecked(id)) {
            case Phase6DatasetPartition::Train:
                split.train_episode_ids.push_back(id);
                break;
            case Phase6DatasetPartition::Validation:
                split.validation_episode_ids.push_back(id);
                break;
            case Phase6DatasetPartition::Test:
                split.test_episode_ids.push_back(id);
                break;
            }
        }
        validate_split_shape(split);
        split.split_identity = phase6_split_identity(split);
        return {std::optional<TrainingDatasetSplitV1>(std::move(split)), std::nullopt};
    } catch (const Phase6Failure& error) {
        return split_failure(error.code());
    } catch (const std::bad_alloc&) {
        return split_failure(Phase6DataErrorCode::InternalFailure);
    } catch (...) {
        return split_failure(Phase6DataErrorCode::InvalidSplit);
    }
}

Phase6SampleResult materialize_phase6_sample_v1(
    const trajectory::DatasetManifest& manifest,
    const std::vector<trajectory::VerifiedAdmissionReceipt>& verified_receipts,
    const trajectory::EpisodeEnvelope& admitted_envelope,
    const std::size_t record_index,
    const model::CardVocabularyV1& vocabulary) noexcept {
    try {
        validate_dataset_manifest(manifest, verified_receipts);
        std::string record_id;
        const auto& member = validate_manifest_envelope(
            manifest, verified_receipts, admitted_envelope, record_id);
        const auto* receipt = receipt_for(verified_receipts, member.admission_receipt_id);
        if (receipt == nullptr) fail(Phase6DataErrorCode::MissingAdmissionReceipt);
        return {std::optional<Phase6BcSampleV1>(materialize_sample_unchecked(
                    admitted_envelope, *receipt, record_index, record_id, vocabulary)),
                std::nullopt};
    } catch (const Phase6Failure& error) {
        return sample_failure(error.code());
    } catch (const std::bad_alloc&) {
        return sample_failure(Phase6DataErrorCode::InternalFailure);
    } catch (...) {
        return sample_failure(Phase6DataErrorCode::InternalFailure);
    }
}

Phase6DatasetResult materialize_phase6_dataset_v1(
    const trajectory::DatasetManifest& manifest,
    const std::vector<trajectory::VerifiedAdmissionReceipt>& verified_receipts,
    const std::vector<trajectory::EpisodeEnvelope>& admitted_envelopes,
    const model::CardVocabularyV1& vocabulary) noexcept {
    try {
        validate_dataset_manifest(manifest, verified_receipts);
        if (admitted_envelopes.size() != manifest.members.size()) {
            fail(admitted_envelopes.size() < manifest.members.size()
                     ? Phase6DataErrorCode::MissingEpisodeEnvelope
                     : Phase6DataErrorCode::UnexpectedEpisodeEnvelope);
        }

        struct EnvelopeIndex final {
            std::string trajectory_record_id;
            std::size_t index = 0;
        };
        std::vector<EnvelopeIndex> indexes;
        indexes.reserve(admitted_envelopes.size());
        std::vector<std::string> episode_ids;
        for (std::size_t index = 0; index < admitted_envelopes.size(); ++index) {
            std::string record_id;
            (void)validate_manifest_envelope(
                manifest, verified_receipts, admitted_envelopes[index], record_id);
            if (std::any_of(indexes.begin(), indexes.end(),
                            [&](const auto& value) {
                                return value.trajectory_record_id == record_id;
                            })) {
                fail(Phase6DataErrorCode::UnexpectedEpisodeEnvelope);
            }
            indexes.push_back({record_id, index});
            episode_ids.push_back(admitted_envelopes[index].manifest.episode_semantic_id);
        }
        std::sort(episode_ids.begin(), episode_ids.end());
        episode_ids.erase(std::unique(episode_ids.begin(), episode_ids.end()), episode_ids.end());
        const auto split = make_phase6_split_v1(
            manifest.dataset_semantic_id, episode_ids);
        if (!split || !split.value.has_value()) {
            fail(Phase6DataErrorCode::InvalidSplit);
        }

        Phase6MaterializedDatasetV1 dataset;
        dataset.source_dataset_identity = manifest.dataset_semantic_id;
        dataset.split = *split.value;
        std::set<std::string> sample_ids;
        for (const auto& member : manifest.members) {
            const auto info = std::find_if(
                indexes.begin(), indexes.end(), [&](const auto& value) {
                    return value.trajectory_record_id == member.trajectory_record_id;
                });
            if (info == indexes.end()) fail(Phase6DataErrorCode::MissingEpisodeEnvelope);
            const auto& envelope = admitted_envelopes[info->index];
            const auto* receipt = receipt_for(verified_receipts, member.admission_receipt_id);
            if (receipt == nullptr) fail(Phase6DataErrorCode::MissingAdmissionReceipt);
            const auto partition = phase6_partition_for_episode(
                envelope.manifest.episode_semantic_id);
            if (!partition.has_value()) fail(Phase6DataErrorCode::InvalidSplit);
            for (std::size_t record_index = 0;
                 record_index < envelope.records.size(); ++record_index) {
                auto sample = materialize_sample_unchecked(
                    envelope, *receipt, record_index, info->trajectory_record_id,
                    vocabulary);
                if (!sample_ids.insert(sample.sample_identity).second) {
                    fail(Phase6DataErrorCode::DuplicateSampleIdentity);
                }
                switch (*partition) {
                case Phase6DatasetPartition::Train:
                    dataset.train_samples.push_back(std::move(sample));
                    break;
                case Phase6DatasetPartition::Validation:
                    dataset.validation_samples.push_back(std::move(sample));
                    break;
                case Phase6DatasetPartition::Test:
                    dataset.test_samples.push_back(std::move(sample));
                    break;
                }
            }
        }
        return {std::optional<Phase6MaterializedDatasetV1>(std::move(dataset)), std::nullopt};
    } catch (const Phase6Failure& error) {
        return dataset_failure(error.code());
    } catch (const std::bad_alloc&) {
        return dataset_failure(Phase6DataErrorCode::InternalFailure);
    } catch (...) {
        return dataset_failure(Phase6DataErrorCode::InternalFailure);
    }
}

}  // namespace ygo::phase6
