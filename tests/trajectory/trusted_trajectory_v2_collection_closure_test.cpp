#include "ygo/policy/production_provenance.hpp"
#include "ygo/policy/teacher.hpp"
#include "ygo/policy/teacher_runner_v3_trajectory.hpp"
#include "ygo/policy/teacher_v2.hpp"
#include "ygo/teacher/salamangreat_profile.hpp"
#include "ygo/teacher/swordsoul_tenyi_profile.hpp"
#include "ygo/trajectory/admission_v2.hpp"
#include "ygo/trajectory/dataset_manifest_v2.hpp"
#include "ygo/trajectory/dataset_manifest.hpp"
#include "ygo/trajectory/receipt_v2.hpp"
#include "ygo/trajectory/receipt.hpp"
#include "ygo/trajectory/restricted_evidence_v2.hpp"
#include "ygo/trajectory/restricted_evidence.hpp"
#include "ygo/trajectory/shard_v2.hpp"
#include "ygo/trajectory/shard.hpp"
#include "ygo/trace/sha256.hpp"

#include <algorithm>
#include <array>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>

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

struct Fixture final {
    CertifiedEnvironmentConfig environment_config = CertifiedEnvironmentConfig::canonical_v3();
    EpisodeSpec episode_spec;
    RunControl run_control;
    PolicyProvenanceEnvelope policy_provenance;
    TeacherRunnerV3Config runner_config;
};

const ParticipantPolicyAssignment& assignment_for_player(
    const std::vector<ParticipantPolicyAssignment>& assignments,
    const std::uint8_t player) {
    const auto it = std::find_if(
        assignments.begin(), assignments.end(),
        [player](const auto& assignment) { return assignment.player == player; });
    require(it != assignments.end(), "A5 fixture lacks a participant assignment");
    return *it;
}

Fixture fixture(const std::uint64_t root_seed = 2) {
    Fixture result;
    result.episode_spec.contract_id = std::string(kEpisodicEnvironmentV3ContractId);
    result.episode_spec.root_seed = root_seed;
    result.episode_spec.seat_assignment = SeatAssignment::Normal;
    result.episode_spec.starting_player = 0;
    result.run_control.engine_process_budget = 512;
    result.run_control.semantic_action_budget = 1;
    result.run_control.cancellation.reason = "ADMINISTRATIVE_CANCEL";
    result.run_control.cancellation.source = "a5-collection-closure";

    const auto swordsoul = make_swordsoul_tenyi_profile();
    const auto salamangreat = make_salamangreat_profile();
    const auto swordsoul_artifact = make_teacher_policy_artifact_v2(swordsoul);
    const auto salamangreat_artifact = make_teacher_policy_artifact_v2(salamangreat);
    const std::array<PolicyRole, 2> roles = {
        PolicyRole::Behavior, PolicyRole::Opponent};
    result.policy_provenance.policy_artifacts = {
        swordsoul_artifact, salamangreat_artifact};
    std::sort(result.policy_provenance.policy_artifacts.begin(),
              result.policy_provenance.policy_artifacts.end(),
              [](const auto& left, const auto& right) {
                  return left.policy_artifact_id < right.policy_artifact_id;
              });
    result.policy_provenance.participant_assignments = make_teacher_participant_assignments(
        swordsoul_artifact, salamangreat_artifact, result.environment_config,
        result.episode_spec.seat_assignment, result.episode_spec.starting_player, roles);
    for (std::uint8_t player = 0; player < 2; ++player) {
        const auto& assignment = assignment_for_player(
            result.policy_provenance.participant_assignments, player);
        const auto& profile = assignment.deck_role == DeckRole::FirstLockedDeck
                                  ? swordsoul
                                  : salamangreat;
        const auto& artifact = assignment.deck_role == DeckRole::FirstLockedDeck
                                   ? swordsoul_artifact
                                   : salamangreat_artifact;
        auto session = create_teacher_policy_session_v2(
            profile, make_teacher_policy_binding_v2(profile), artifact, assignment);
        require(static_cast<bool>(session), "A5 fixture session creation failed");
        result.runner_config.sessions[player] = std::move(*session.value);
    }
    return result;
}

struct Collected final {
    Fixture fixture;
    TeacherRunnerV3TrajectoryRunResult result;
    replay_v2::ReplayOptions replay_options;
};

Collected collect(const std::uint64_t root_seed = 2) {
    auto value = fixture(root_seed);
    replay_v2::ReplayOptions options;
    options.cancellation_source = value.run_control.cancellation.source;
    auto created = TeacherRunnerV3TrajectoryRunner::create(
        TeacherRunnerV3TrajectoryConfig{value.environment_config, value.episode_spec,
                                        value.run_control, value.policy_provenance,
                                        std::move(value.runner_config)});
    require(static_cast<bool>(created), "A5 could not create the real V3 runner");
    auto result = created.value->run();
    require(static_cast<bool>(result), "A5 real V3 run failed: " + result.diagnostic);
    require(result.envelope.has_value() && result.candidate_shard.has_value() &&
                result.restricted_collection_evidence.has_value() &&
                result.admission_verification.has_value() && result.admission_receipt.has_value() &&
                result.dataset_manifest.has_value(),
            "A4 result was not finalized into the complete V2 collection chain");
    return Collected{std::move(value), std::move(result), std::move(options)};
}

TeacherRunnerV3TrajectoryRunResult run_adapter_scenario(
    const ygo::policy::detail::TeacherRunnerV3TrajectoryTestScenario scenario) {
    auto value = fixture();
    auto created = TeacherRunnerV3TrajectoryRunner::create(
        TeacherRunnerV3TrajectoryConfig{value.environment_config, value.episode_spec,
                                        value.run_control, value.policy_provenance,
                                        std::move(value.runner_config)});
    require(static_cast<bool>(created), "A5 adapter scenario could not create the V3 runner");
    return ygo::policy::detail::TeacherRunnerV3TrajectoryTestAccess::run_with_scenario(
        *created.value, scenario);
}

CandidateTrajectoryShardV2 shard_for_envelope(const EpisodeEnvelopeV2& envelope) {
    const auto bytes = canonical_episode_envelope_bytes_v2(envelope);
    const auto decoded = decode_episode_envelope_v2(bytes);
    require(static_cast<bool>(decoded),
            "A5 V2 envelope did not decode before shard construction: " +
                (decoded.error.has_value() ? decoded.error->message : "unknown error"));
    CandidateTrajectoryShardV2 shard;
    shard.entries.push_back({ygo::trace::sha256_bytes(bytes), bytes});
    return shard;
}

RestrictedCollectionEvidenceBundleV2 evidence_for_result(
    const TeacherRunnerV3TrajectoryRunResult& result,
    const CandidateTrajectoryShardV2& shard) {
    require(result.envelope.has_value(), "A5 adapter scenario did not seal an envelope");
    RestrictedCollectionEvidenceBundleV2 evidence;
    evidence.candidate_shard_artifact_sha256 = candidate_shard_artifact_sha256_v2(shard);
    if (std::holds_alternative<InterruptedClosureV2>(result.envelope->closure)) {
        require(result.replay_evidence.has_value(),
                "A5 interrupted adapter scenario lacks replay evidence");
        evidence.interrupted_episodes.push_back(
            {shard.entries.front().episode_envelope_sha256, *result.replay_evidence});
    }
    return evidence;
}

void test_clean_real_collection_chain() {
    const auto collected = collect();
    const auto& result = collected.result;
    require(result.envelope->records.size() == 1,
            "A5 clean real run did not retain its accepted V2 record");
    require(result.candidate_shard->entries.size() == 1,
            "A5 V2 shard did not retain exactly one envelope");
    require(result.admission_receipt->receipt().entries.size() == 1,
            "A5 V2 receipt did not retain exactly one commitment");
    std::string error;
    require(dataset_v2::validate_dataset_manifest_v2(
                *result.dataset_manifest,
                std::vector<VerifiedAdmissionReceiptV2>{*result.admission_receipt}, &error),
            "A5 V2 dataset manifest validation failed: " + error);
    require(static_cast<bool>(decode_candidate_trajectory_shard_v2(
                canonical_candidate_trajectory_shard_bytes_v2(*result.candidate_shard))),
            "A5 V2 shard did not decode canonically");
    require(static_cast<bool>(decode_admission_receipt_v2(
                canonical_admission_receipt_bytes_v2(result.admission_receipt->receipt()))),
            "A5 V2 receipt did not decode canonically");
    require(static_cast<bool>(dataset_v2::decode_dataset_manifest_v2(
                dataset_v2::canonical_dataset_manifest_bytes_v2(*result.dataset_manifest))),
            "A5 V2 dataset manifest did not decode canonically");
}

void test_historical_and_mixed_downstream_rejection() {
    const auto collected = collect();
    const auto& result = collected.result;
    const auto shard_bytes = canonical_candidate_trajectory_shard_bytes_v2(*result.candidate_shard);
    const auto evidence_bytes = canonical_restricted_collection_evidence_bundle_bytes_v2(
        *result.restricted_collection_evidence);
    const auto receipt_bytes = canonical_admission_receipt_bytes_v2(
        result.admission_receipt->receipt());
    const auto manifest_bytes =
        dataset_v2::canonical_dataset_manifest_bytes_v2(*result.dataset_manifest);
    require(!static_cast<bool>(decode_candidate_trajectory_shard(shard_bytes)),
            "V1 shard decoder accepted a V2 shard");
    require(!static_cast<bool>(decode_restricted_collection_evidence_bundle(evidence_bytes)),
            "V1 evidence decoder accepted a V2 evidence bundle");
    require(!static_cast<bool>(decode_admission_receipt(receipt_bytes)),
            "V1 receipt decoder accepted a V2 receipt");
    require(!static_cast<bool>(dataset::decode_dataset_manifest(manifest_bytes)),
            "V1 dataset decoder accepted a V2 manifest");

    CandidateTrajectoryShardV2 mixed = *result.candidate_shard;
    const auto v1_envelope = trajectory_test::terminal_envelope(4306);
    const auto v1_bytes = canonical_episode_envelope_bytes(v1_envelope);
    mixed.entries.push_back({ygo::trace::sha256_bytes(v1_bytes), v1_bytes});
    std::sort(mixed.entries.begin(), mixed.entries.end(),
              [](const auto& left, const auto& right) {
                  return left.episode_envelope_sha256 < right.episode_envelope_sha256;
              });
    bool rejected = false;
    try {
        (void)canonical_candidate_trajectory_shard_bytes_v2(mixed);
    } catch (const std::exception&) {
        rejected = true;
    }
    require(rejected, "V2 shard accepted a mixed V1/V2 envelope set");
}

void test_tamper_and_missing_evidence_rejection() {
    const auto collected = collect();
    const auto& result = collected.result;
    auto tampered_shard = *result.candidate_shard;
    tampered_shard.entries.front().episode_envelope_sha256[0] =
        tampered_shard.entries.front().episode_envelope_sha256[0] == '0' ? '1' : '0';
    std::string error;
    require(!admission_v2::verify_collection_for_admission_v2(
                 tampered_shard, *result.restricted_collection_evidence,
                 candidate_shard_artifact_sha256_v2(*result.candidate_shard),
                 restricted_collection_evidence_artifact_sha256_v2(
                     *result.restricted_collection_evidence),
                 collected.replay_options, make_production_policy_provenance_resolver(), &error)
                  .has_value(),
            "V2 admission accepted a tampered shard digest");

    auto missing_evidence = RestrictedCollectionEvidenceBundleV2{};
    missing_evidence.candidate_shard_artifact_sha256 =
        candidate_shard_artifact_sha256_v2(*result.candidate_shard);
    require(!admission_v2::verify_collection_for_admission_v2(
                 *result.candidate_shard, missing_evidence,
                 candidate_shard_artifact_sha256_v2(*result.candidate_shard),
                 restricted_collection_evidence_artifact_sha256_v2(missing_evidence),
                 collected.replay_options, make_production_policy_provenance_resolver(), &error)
                  .has_value(),
            "V2 admission accepted an interrupted envelope without evidence");

    auto failed = *result.envelope;
    failed.records.clear();
    FailedClosureV2 failed_closure;
    failed_closure.failure_code = FailureCode::CoreError;
    failed_closure.failure_stage = FailureStage::Advance;
    failed.closure = failed_closure;
    const auto failed_bytes = canonical_episode_envelope_bytes_v2(failed);
    CandidateTrajectoryShardV2 failed_shard;
    failed_shard.entries.push_back({ygo::trace::sha256_bytes(failed_bytes), failed_bytes});
    RestrictedCollectionEvidenceBundleV2 failed_evidence;
    failed_evidence.candidate_shard_artifact_sha256 =
        candidate_shard_artifact_sha256_v2(failed_shard);
    require(!admission_v2::verify_collection_for_admission_v2(
                 failed_shard, failed_evidence,
                 candidate_shard_artifact_sha256_v2(failed_shard),
                 restricted_collection_evidence_artifact_sha256_v2(failed_evidence),
                 collected.replay_options, make_production_policy_provenance_resolver(), &error)
                  .has_value(),
            "V2 admission accepted a FailedClosureV2 envelope");

    auto quarantined = *result.envelope;
    quarantined.manifest.collection_disposition.kind =
        CollectionDispositionKind::QuarantinedAfterPolicyRejection;
    quarantined.manifest.collection_disposition.policy_rejections = {
        RejectionCode::StaleSubmissionToken};
    const auto quarantined_bytes = canonical_episode_envelope_bytes_v2(quarantined);
    CandidateTrajectoryShardV2 quarantined_shard;
    quarantined_shard.entries.push_back(
        {ygo::trace::sha256_bytes(quarantined_bytes), quarantined_bytes});
    RestrictedCollectionEvidenceBundleV2 quarantined_evidence;
    quarantined_evidence.candidate_shard_artifact_sha256 =
        candidate_shard_artifact_sha256_v2(quarantined_shard);
    require(!admission_v2::verify_collection_for_admission_v2(
                 quarantined_shard, quarantined_evidence,
                 candidate_shard_artifact_sha256_v2(quarantined_shard),
                 restricted_collection_evidence_artifact_sha256_v2(quarantined_evidence),
                 collected.replay_options, make_production_policy_provenance_resolver(), &error)
                  .has_value(),
             "V2 admission accepted a quarantined envelope");

    auto duplicate_receipts = std::vector<VerifiedAdmissionReceiptV2>{
        *result.admission_receipt, *result.admission_receipt};
    require(!dataset_v2::validate_dataset_manifest_v2(
                 *result.dataset_manifest, duplicate_receipts, &error),
            "V2 dataset validation accepted duplicate receipt inputs");

    auto tampered_manifest = *result.dataset_manifest;
    tampered_manifest.members.front().trajectory_record_id =
        tampered_manifest.members.front().public_gameplay_trajectory_id;
    require(!dataset_v2::validate_dataset_manifest_v2(
                 tampered_manifest,
                 std::vector<VerifiedAdmissionReceiptV2>{*result.admission_receipt}, &error),
            "V2 dataset validation accepted a tampered trajectory identity");
}

void test_multi_episode_admission_order_and_evidence_binding() {
    const auto first = collect(2);
    const auto second = collect(3);

    CandidateTrajectoryShardV2 shard;
    shard.entries.push_back(first.result.candidate_shard->entries.front());
    shard.entries.push_back(second.result.candidate_shard->entries.front());
    std::sort(shard.entries.begin(), shard.entries.end(),
              [](const auto& left, const auto& right) {
                  return left.episode_envelope_sha256 < right.episode_envelope_sha256;
              });

    std::vector<std::string> record_ids_in_shard_order;
    for (const auto& entry : shard.entries) {
        const auto decoded = decode_episode_envelope_v2(entry.envelope_bytes);
        require(static_cast<bool>(decoded),
                "A5 multi-episode fixture contains an undecodable V2 envelope");
        record_ids_in_shard_order.push_back(trajectory_record_id_v2(*decoded.value));
    }
    require(record_ids_in_shard_order.size() == 2 &&
                record_ids_in_shard_order[0] > record_ids_in_shard_order[1],
            "A5 multi-episode fixture did not separate shard and record-ID ordering");
    require(first.result.replay_evidence.has_value() &&
                second.result.replay_evidence.has_value(),
            "A5 multi-episode fixture lacks restricted replay evidence");

    RestrictedCollectionEvidenceBundleV2 evidence;
    evidence.candidate_shard_artifact_sha256 = candidate_shard_artifact_sha256_v2(shard);
    evidence.interrupted_episodes = {
        {first.result.candidate_shard->entries.front().episode_envelope_sha256,
         *first.result.replay_evidence},
        {second.result.candidate_shard->entries.front().episode_envelope_sha256,
         *second.result.replay_evidence}};
    std::sort(evidence.interrupted_episodes.begin(), evidence.interrupted_episodes.end(),
              [](const auto& left, const auto& right) {
                  return left.episode_envelope_sha256 < right.episode_envelope_sha256;
              });

    const auto shard_id = candidate_shard_artifact_sha256_v2(shard);
    const auto evidence_id = restricted_collection_evidence_artifact_sha256_v2(evidence);
    std::string error;
    const auto verification = admission_v2::verify_collection_for_admission_v2(
        shard, evidence, shard_id, evidence_id, first.replay_options,
        make_production_policy_provenance_resolver(), &error);
    require(verification.has_value(), "A5 multi-episode admission rejected valid entries: " + error);
    require(verification->entries().size() == 2 &&
                verification->entries()[0].trajectory_record_id <
                    verification->entries()[1].trajectory_record_id,
            "A5 collection commitments were not sorted by trajectory record identity");

    auto extra_evidence = evidence;
    extra_evidence.interrupted_episodes.push_back(
        {std::string(64, 'f'), *first.result.replay_evidence});
    std::sort(extra_evidence.interrupted_episodes.begin(),
              extra_evidence.interrupted_episodes.end(),
              [](const auto& left, const auto& right) {
                  return left.episode_envelope_sha256 < right.episode_envelope_sha256;
              });
    const auto extra_evidence_id =
        restricted_collection_evidence_artifact_sha256_v2(extra_evidence);
    require(!admission_v2::verify_collection_for_admission_v2(
                 shard, extra_evidence, shard_id, extra_evidence_id, first.replay_options,
                 make_production_policy_provenance_resolver(), &error)
                  .has_value(),
            "A5 admission accepted unreferenced restricted evidence");
}

void test_adapter_failure_and_quarantine_boundaries() {
    const auto failed = run_adapter_scenario(
        ygo::policy::detail::TeacherRunnerV3TrajectoryTestScenario::Failure);
    require(failed.envelope.has_value() &&
                std::holds_alternative<FailedClosureV2>(failed.envelope->closure),
            "A5 adapter failure path did not produce a V2 failed envelope");
    const auto failed_shard = shard_for_envelope(*failed.envelope);
    const auto failed_evidence = evidence_for_result(failed, failed_shard);
    std::string error;
    require(!admission_v2::verify_collection_for_admission_v2(
                 failed_shard, failed_evidence,
                 candidate_shard_artifact_sha256_v2(failed_shard),
                 restricted_collection_evidence_artifact_sha256_v2(failed_evidence),
                 replay_v2::ReplayOptions{}, make_production_policy_provenance_resolver(), &error)
                  .has_value(),
            "A5 admission accepted an adapter-produced failed envelope");

    const auto rejected = run_adapter_scenario(
        ygo::policy::detail::TeacherRunnerV3TrajectoryTestScenario::StepRejected);
    require(rejected.envelope.has_value() && rejected.quarantined &&
                rejected.envelope->records.empty(),
            "A5 adapter rejection path did not produce a quarantined zero-record envelope");
    const auto rejected_shard = shard_for_envelope(*rejected.envelope);
    const auto rejected_evidence = evidence_for_result(rejected, rejected_shard);
    require(!admission_v2::verify_collection_for_admission_v2(
                 rejected_shard, rejected_evidence,
                 candidate_shard_artifact_sha256_v2(rejected_shard),
                 restricted_collection_evidence_artifact_sha256_v2(rejected_evidence),
                 replay_v2::ReplayOptions{}, make_production_policy_provenance_resolver(), &error)
                  .has_value(),
            "A5 admission accepted an adapter-produced quarantined envelope");
}

}  // namespace

int main() {
    try {
        test_clean_real_collection_chain();
        test_historical_and_mixed_downstream_rejection();
        test_tamper_and_missing_evidence_rejection();
        test_multi_episode_admission_order_and_evidence_binding();
        test_adapter_failure_and_quarantine_boundaries();
        const auto collected = collect();
        std::cout << "A5_SHARD_SHA256="
                  << candidate_shard_artifact_sha256_v2(*collected.result.candidate_shard)
                  << "\nA5_RECEIPT_ID="
                  << admission_receipt_id_v2(collected.result.admission_receipt->receipt())
                  << "\nA5_DATASET_ID="
                  << collected.result.dataset_manifest->dataset_semantic_id
                  << "\ntrusted trajectory V2 collection closure tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
