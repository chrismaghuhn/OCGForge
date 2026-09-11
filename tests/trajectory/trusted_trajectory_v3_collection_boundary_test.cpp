#include "ygo/environment/episodic_environment.hpp"
#include "ygo/policy/production_provenance.hpp"
#include "ygo/policy/teacher.hpp"
#include "ygo/policy/teacher_v3.hpp"
#include "ygo/teacher/salamangreat_profile.hpp"
#include "ygo/teacher/swordsoul_tenyi_profile.hpp"
#include "ygo/trajectory/admission_v3.hpp"
#include "ygo/trajectory/codec_v3.hpp"
#include "ygo/trajectory/dataset_manifest.hpp"
#include "ygo/trajectory/dataset_manifest_v2.hpp"
#include "ygo/trajectory/dataset_manifest_v3.hpp"
#include "ygo/trajectory/recorder_v3.hpp"
#include "ygo/trajectory/recorder_v2.hpp"
#include "ygo/trajectory/receipt.hpp"
#include "ygo/trajectory/receipt_v2.hpp"
#include "ygo/trajectory/receipt_v3.hpp"
#include "ygo/trajectory/replay_v3.hpp"
#include "ygo/trajectory/restricted_evidence.hpp"
#include "ygo/trajectory/restricted_evidence_v2.hpp"
#include "ygo/trajectory/restricted_evidence_v3.hpp"
#include "ygo/trajectory/shard.hpp"
#include "ygo/trajectory/shard_v2.hpp"
#include "ygo/trajectory/shard_v3.hpp"
#include "ygo/trajectory/trajectory_identity_v3.hpp"
#include "ygo/trace/sha256.hpp"

#include <algorithm>
#include <array>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "test_fixtures.hpp"

namespace {

using namespace ygo::environment;
using namespace ygo::policy;
using namespace ygo::teacher;
using namespace ygo::trajectory;

void require(const bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

struct Collection final {
    CandidateTrajectoryShardV3 shard;
    RestrictedCollectionEvidenceBundleV3 evidence;
    admission_v3::AdmissionVerification verification;
    VerifiedAdmissionReceiptV3 receipt;
    dataset_v3::DatasetManifestV3 manifest;
};

PolicyProvenanceEnvelope production_v3_provenance(
    const CertifiedEnvironmentConfig& config,
    const EpisodeSpec& spec) {
    const auto swordsoul = make_teacher_policy_artifact_v3(
        make_swordsoul_tenyi_profile());
    const auto salamangreat = make_teacher_policy_artifact_v3(
        make_salamangreat_profile());
    PolicyProvenanceEnvelope result;
    result.policy_artifacts = {swordsoul, salamangreat};
    std::sort(result.policy_artifacts.begin(), result.policy_artifacts.end(),
              [](const auto& left, const auto& right) {
                  return left.policy_artifact_id < right.policy_artifact_id;
              });
    result.participant_assignments = make_teacher_participant_assignments(
        swordsoul, salamangreat, config, spec.seat_assignment, spec.starting_player,
        {PolicyRole::Behavior, PolicyRole::Opponent});
    return result;
}

struct CollectedV3 final {
    EpisodeEnvelopeV3 envelope;
    RestrictedReplayEvidenceV3 evidence;
    replay_v3::ReplayOptions options;
};

CollectedV3 collect_administrative_pending() {
    const auto config = CertifiedEnvironmentConfig::canonical_v4();
    auto spec = trajectory_test::episode_spec(4301);
    spec.contract_id = std::string(kEpisodicEnvironmentV4ContractId);
    const auto policy = production_v3_provenance(config, spec);
    RunControl control;
    control.engine_process_budget = 10000;
    control.semantic_action_budget = 10000;
    control.cancellation.reason = "ADMINISTRATIVE_CANCEL";
    control.cancellation.source = "v3-collection-boundary";

    auto factory = EpisodicEnvironment::create(config);
    require(std::holds_alternative<std::unique_ptr<EpisodicEnvironment>>(factory),
            "V4 collection boundary fixture rejected environment creation");
    auto environment = std::move(std::get<std::unique_ptr<EpisodicEnvironment>>(factory));
    const auto reset = environment->reset(spec, control);
    const auto* reset_accepted = std::get_if<ResetAccepted>(&reset);
    require(reset_accepted != nullptr &&
                std::holds_alternative<DecisionFrame>(reset_accepted->next),
            "V4 collection boundary fixture did not publish a frame");
    const auto pending = std::get<DecisionFrame>(reset_accepted->next);

    TrajectoryRecorderV3 recorder(
        config, spec, policy, make_production_policy_provenance_resolver());
    std::string error;
    require(recorder.on_reset_accepted(*reset_accepted, std::nullopt, &error),
            "V3 collection boundary recorder rejected reset: " + error);
    const auto interrupted = environment->interrupt(InterruptRequest{
        std::string(kEpisodicEnvironmentV4ContractId),
        InterruptionReason::AdministrativeCancel});
    const auto* accepted = std::get_if<InterruptAccepted>(&interrupted);
    require(accepted != nullptr, "V4 collection boundary interrupt was rejected");
    require(recorder.on_interrupt_accepted(
                std::optional<DecisionFrame>{pending}, *accepted, &error),
            "V3 collection boundary recorder rejected interruption: " + error);
    const auto sealed = recorder.seal(&error);
    require(sealed.has_value(), "V3 collection boundary recorder did not seal: " + error);

    CollectedV3 result;
    result.envelope = *sealed;
    result.evidence.episode_semantic_id = result.envelope.manifest.episode_semantic_id;
    result.evidence.interruption_reason = accepted->interruption.reason;
    result.evidence.engine_process_budget =
        accepted->interruption.run_control_evidence.engine_process_budget;
    result.evidence.semantic_action_budget =
        accepted->interruption.run_control_evidence.semantic_action_budget;
    result.evidence.observed_engine_process_count =
        accepted->interruption.run_control_evidence.engine_process_count;
    result.evidence.observed_semantic_action_count =
        accepted->interruption.run_control_evidence.semantic_action_count;
    result.evidence.final_engine_step_index = accepted->interruption.final_engine_step_index;
    result.options.cancellation_source = control.cancellation.source;
    return result;
}

Collection collect_collection() {
    const auto collected = collect_administrative_pending();
    const auto envelope_bytes = canonical_episode_envelope_bytes_v3(collected.envelope);
    CandidateTrajectoryShardV3 shard;
    shard.entries.push_back({ygo::trace::sha256_bytes(envelope_bytes), envelope_bytes});
    const auto shard_id = candidate_shard_artifact_sha256_v3(shard);

    RestrictedCollectionEvidenceBundleV3 evidence;
    evidence.candidate_shard_artifact_sha256 = shard_id;
    evidence.interrupted_episodes.push_back(
        {shard.entries.front().episode_envelope_sha256, collected.evidence});
    const auto evidence_id = restricted_collection_evidence_artifact_sha256_v3(evidence);

    std::string error;
    const auto verification = admission_v3::verify_collection_for_admission_v3(
        shard, evidence, shard_id, evidence_id, collected.options,
        ygo::policy::make_production_policy_provenance_resolver(), &error);
    require(verification.has_value(), "V3 collection admission failed: " + error);
    const auto receipt = issue_admission_receipt_v3(*verification, &error);
    require(receipt.has_value(), "V3 receipt issuance failed: " + error);

    dataset_v3::DatasetManifestV3 manifest;
    const auto& commitments = verification->entries();
    require(commitments.size() == 1, "V3 collection fixture did not produce one commitment");
    std::vector<std::string> record_ids;
    for (const auto& commitment : commitments) {
        record_ids.push_back(commitment.trajectory_record_id);
        manifest.members.push_back({
            commitment.trajectory_record_id,
            commitment.public_gameplay_trajectory_id,
            admission_receipt_id_v3(receipt->receipt()),
            shard_id,
            commitment.episode_envelope_sha256});
    }
    manifest.dataset_semantic_id = dataset_v3::dataset_semantic_id_v3(record_ids);
    require(dataset_v3::validate_dataset_manifest_v3(
                manifest, std::vector<VerifiedAdmissionReceiptV3>{*receipt}, &error),
            "V3 dataset manifest validation failed: " + error);
    return Collection{std::move(shard), std::move(evidence), std::move(*verification),
                      std::move(*receipt), std::move(manifest)};
}

EpisodeEnvelopeV2 collect_v2_interrupted_envelope() {
    const auto config = CertifiedEnvironmentConfig::canonical_v3();
    auto spec = trajectory_test::episode_spec(4305);
    spec.contract_id = std::string(kEpisodicEnvironmentV3ContractId);
    RunControl control;
    control.engine_process_budget = 10000;
    control.semantic_action_budget = 10000;
    control.cancellation.reason = "ADMINISTRATIVE_CANCEL";
    control.cancellation.source = "v2-cross-generation";

    auto factory = EpisodicEnvironment::create(config);
    require(std::holds_alternative<std::unique_ptr<EpisodicEnvironment>>(factory),
            "V2 cross-generation fixture rejected environment creation");
    auto environment = std::move(std::get<std::unique_ptr<EpisodicEnvironment>>(factory));
    const auto reset = environment->reset(spec, control);
    const auto* reset_accepted = std::get_if<ResetAccepted>(&reset);
    require(reset_accepted != nullptr &&
                std::holds_alternative<DecisionFrame>(reset_accepted->next),
            "V2 cross-generation fixture did not publish a frame");
    const auto pending = std::get<DecisionFrame>(reset_accepted->next);

    TrajectoryRecorderV2 recorder(
        config, spec, trajectory_test::provenance(),
        trajectory_test::test_provenance_resolver());
    std::string error;
    require(recorder.on_reset_accepted(*reset_accepted, std::nullopt, &error),
            "V2 cross-generation recorder rejected reset: " + error);
    const auto interrupted = environment->interrupt(InterruptRequest{
        std::string(kEpisodicEnvironmentV3ContractId),
        InterruptionReason::AdministrativeCancel});
    const auto* accepted = std::get_if<InterruptAccepted>(&interrupted);
    require(accepted != nullptr && recorder.on_interrupt_accepted(
                                      std::optional<DecisionFrame>{pending}, *accepted, &error),
            "V2 cross-generation recorder rejected interruption: " + error);
    const auto sealed = recorder.seal(&error);
    require(sealed.has_value(), "V2 cross-generation recorder did not seal: " + error);
    return *sealed;
}

void test_complete_v3_collection_chain() {
    const auto collection = collect_collection();
    const auto shard_bytes = canonical_candidate_trajectory_shard_bytes_v3(collection.shard);
    const auto evidence_bytes =
        canonical_restricted_collection_evidence_bundle_bytes_v3(collection.evidence);
    const auto receipt_bytes = canonical_admission_receipt_bytes_v3(
        collection.receipt.receipt());
    const auto manifest_bytes =
        dataset_v3::canonical_dataset_manifest_bytes_v3(collection.manifest);

    require(static_cast<bool>(decode_candidate_trajectory_shard_v3(shard_bytes)),
            "V3 shard did not decode canonically");
    require(static_cast<bool>(decode_restricted_collection_evidence_bundle_v3(evidence_bytes)),
            "V3 evidence bundle did not decode canonically");
    require(static_cast<bool>(decode_admission_receipt_v3(receipt_bytes)),
            "V3 receipt did not decode canonically");
    require(static_cast<bool>(dataset_v3::decode_dataset_manifest_v3(manifest_bytes)),
            "V3 dataset manifest did not decode canonically");
    require(!decode_candidate_trajectory_shard(shard_bytes),
            "V1 shard decoder accepted a V3 shard");
    require(!decode_restricted_collection_evidence_bundle(evidence_bytes),
            "V1 evidence decoder accepted a V3 evidence bundle");
    require(!decode_admission_receipt(receipt_bytes),
            "V1 receipt decoder accepted a V3 receipt");
    require(!dataset::decode_dataset_manifest(manifest_bytes),
            "V1 dataset decoder accepted a V3 manifest");
}

void test_v3_receipt_and_dataset_bindings_fail_closed() {
    const auto collection = collect_collection();
    std::string error;

    RestrictedCollectionEvidenceBundleV3 missing_evidence;
    missing_evidence.candidate_shard_artifact_sha256 =
        candidate_shard_artifact_sha256_v3(collection.shard);
    require(!admission_v3::verify_collection_for_admission_v3(
                 collection.shard, missing_evidence,
                 candidate_shard_artifact_sha256_v3(collection.shard),
                 restricted_collection_evidence_artifact_sha256_v3(missing_evidence),
                 replay_v3::ReplayOptions{},
                 ygo::policy::make_production_policy_provenance_resolver(), &error)
                  .has_value(),
            "V3 admission accepted an interrupted envelope without evidence");
    require(!admission_v3::verify_collection_for_admission_v3(
                 collection.shard, collection.evidence,
                 candidate_shard_artifact_sha256_v3(collection.shard),
                 std::string(64, '0'), replay_v3::ReplayOptions{},
                 ygo::policy::make_production_policy_provenance_resolver(), &error)
                  .has_value(),
            "V3 admission accepted a mismatched evidence artifact digest");

    auto tampered_shard = collection.shard;
    tampered_shard.entries.front().episode_envelope_sha256[0] =
        tampered_shard.entries.front().episode_envelope_sha256[0] == '0' ? '1' : '0';
    require(!admission_v3::verify_collection_for_admission_v3(
                 tampered_shard, collection.evidence,
                 candidate_shard_artifact_sha256_v3(collection.shard),
                 restricted_collection_evidence_artifact_sha256_v3(collection.evidence),
                 replay_v3::ReplayOptions{},
                 ygo::policy::make_production_policy_provenance_resolver(), &error)
                  .has_value(),
            "V3 admission accepted a tampered envelope digest");

    auto extra_evidence = collection.evidence;
    extra_evidence.interrupted_episodes.push_back(
        {std::string(64, 'f'), collection.evidence.interrupted_episodes.front().evidence});
    std::sort(extra_evidence.interrupted_episodes.begin(),
              extra_evidence.interrupted_episodes.end(),
              [](const auto& left, const auto& right) {
                  return left.episode_envelope_sha256 < right.episode_envelope_sha256;
              });
    require(!admission_v3::verify_collection_for_admission_v3(
                 collection.shard, extra_evidence,
                 candidate_shard_artifact_sha256_v3(collection.shard),
                 restricted_collection_evidence_artifact_sha256_v3(extra_evidence),
                 replay_v3::ReplayOptions{},
                 ygo::policy::make_production_policy_provenance_resolver(), &error)
                  .has_value(),
            "V3 admission accepted unreferenced interruption evidence");

    auto tampered_manifest = collection.manifest;
    tampered_manifest.members.front().trajectory_record_id =
        tampered_manifest.members.front().public_gameplay_trajectory_id;
    require(!dataset_v3::validate_dataset_manifest_v3(
                 tampered_manifest,
                 std::vector<VerifiedAdmissionReceiptV3>{collection.receipt}, &error),
            "V3 dataset validation accepted a tampered trajectory identity");

    auto duplicate_receipts = std::vector<VerifiedAdmissionReceiptV3>{
        collection.receipt, collection.receipt};
    require(!dataset_v3::validate_dataset_manifest_v3(
                 collection.manifest, duplicate_receipts, &error),
            "V3 dataset validation accepted duplicate receipt inputs");

    const auto v3_shard_bytes = canonical_candidate_trajectory_shard_bytes_v3(collection.shard);
    const auto v3_evidence_bytes =
        canonical_restricted_collection_evidence_bundle_bytes_v3(collection.evidence);
    const auto v3_receipt_bytes = canonical_admission_receipt_bytes_v3(
        collection.receipt.receipt());
    const auto v3_manifest_bytes =
        dataset_v3::canonical_dataset_manifest_bytes_v3(collection.manifest);

    const auto v2_envelope = collect_v2_interrupted_envelope();
    const auto v2_envelope_bytes = canonical_episode_envelope_bytes_v2(v2_envelope);
    CandidateTrajectoryShardV2 v2_shard;
    v2_shard.entries.push_back({ygo::trace::sha256_bytes(v2_envelope_bytes), v2_envelope_bytes});
    const auto v2_shard_bytes = canonical_candidate_trajectory_shard_bytes_v2(v2_shard);
    require(!decode_candidate_trajectory_shard_v3(v2_shard_bytes),
            "V3 shard decoder accepted a V2 shard");
    require(!decode_candidate_trajectory_shard(v3_shard_bytes),
            "V1 shard decoder accepted a V3 shard");

    RestrictedCollectionEvidenceBundleV2 v2_evidence;
    v2_evidence.candidate_shard_artifact_sha256 = std::string(64, '0');
    const auto v2_evidence_bytes =
        canonical_restricted_collection_evidence_bundle_bytes_v2(v2_evidence);
    require(!decode_restricted_collection_evidence_bundle_v3(v2_evidence_bytes),
            "V3 evidence decoder accepted a V2 evidence bundle");
    require(!decode_restricted_collection_evidence_bundle_v2(v3_evidence_bytes),
            "V2 evidence decoder accepted a V3 evidence bundle");

    AdmissionReceiptV2 v2_receipt;
    v2_receipt.candidate_shard_artifact_sha256 = std::string(64, '0');
    v2_receipt.restricted_evidence_artifact_sha256 = std::string(64, '1');
    const auto v2_receipt_bytes = canonical_admission_receipt_bytes_v2(v2_receipt);
    require(!decode_admission_receipt_v3(v2_receipt_bytes),
            "V3 receipt decoder accepted a V2 receipt");
    require(!decode_admission_receipt_v2(v3_receipt_bytes),
            "V2 receipt decoder accepted a V3 receipt");

    DatasetManifestV2 v2_manifest;
    v2_manifest.dataset_semantic_id = dataset_v2::dataset_semantic_id_v2({});
    const auto v2_manifest_bytes = dataset_v2::canonical_dataset_manifest_bytes_v2(v2_manifest);
    require(!dataset_v3::decode_dataset_manifest_v3(v2_manifest_bytes),
            "V3 dataset decoder accepted a V2 manifest");
    require(!dataset_v2::decode_dataset_manifest_v2(v3_manifest_bytes),
            "V2 dataset decoder accepted a V3 manifest");
}

void test_v3_collection_determinism() {
    const auto first = collect_collection();
    const auto second = collect_collection();
    require(canonical_candidate_trajectory_shard_bytes_v3(first.shard) ==
                canonical_candidate_trajectory_shard_bytes_v3(second.shard),
            "paired V3 shards produced different canonical bytes");
    require(canonical_restricted_collection_evidence_bundle_bytes_v3(first.evidence) ==
                canonical_restricted_collection_evidence_bundle_bytes_v3(second.evidence),
            "paired V3 evidence bundles produced different canonical bytes");
    require(admission_receipt_id_v3(first.receipt.receipt()) ==
                admission_receipt_id_v3(second.receipt.receipt()),
            "paired V3 receipts produced different identities");
    require(first.manifest.dataset_semantic_id == second.manifest.dataset_semantic_id,
            "paired V3 datasets produced different identities");
}

}  // namespace

int main() {
    try {
        test_complete_v3_collection_chain();
        test_v3_receipt_and_dataset_bindings_fail_closed();
        test_v3_collection_determinism();
        const auto collection = collect_collection();
        std::cout << "V3_SHARD_SHA256="
                  << candidate_shard_artifact_sha256_v3(collection.shard)
                  << "\nV3_EVIDENCE_SHA256="
                  << restricted_collection_evidence_artifact_sha256_v3(collection.evidence)
                  << "\nV3_RECEIPT_ID="
                  << admission_receipt_id_v3(collection.receipt.receipt())
                  << "\nV3_DATASET_ID=" << collection.manifest.dataset_semantic_id
                  << "\ntrusted trajectory V3 collection boundary tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
