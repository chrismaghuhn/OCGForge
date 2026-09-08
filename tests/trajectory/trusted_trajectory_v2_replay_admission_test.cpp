#include "ygo/trajectory/admission_v2.hpp"
#include "ygo/trajectory/codec.hpp"
#include "ygo/trajectory/recorder_v2.hpp"
#include "ygo/trajectory/restricted_evidence.hpp"
#include "ygo/trajectory/shard.hpp"
#include "ygo/trace/sha256.hpp"

#include <algorithm>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>

#include "test_fixtures.hpp"

namespace {

using namespace ygo;
using namespace ygo::environment;
using namespace ygo::trajectory;

void require(const bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

struct CollectedV2 final {
    EpisodeEnvelopeV2 envelope;
    RestrictedReplayEvidenceV2 evidence;
    replay_v2::ReplayOptions options;
};

RestrictedReplayEvidenceV2 evidence_for(
    const EpisodeInterrupted& interruption,
    const std::string& episode_id) {
    RestrictedReplayEvidenceV2 result;
    result.episode_semantic_id = episode_id;
    result.interruption_reason = interruption.reason;
    result.engine_process_budget = interruption.run_control_evidence.engine_process_budget;
    result.semantic_action_budget = interruption.run_control_evidence.semantic_action_budget;
    result.observed_engine_process_count = interruption.run_control_evidence.engine_process_count;
    result.observed_semantic_action_count = interruption.run_control_evidence.semantic_action_count;
    result.final_engine_step_index = interruption.final_engine_step_index;
    return result;
}

CollectedV2 collect_administrative_pending() {
    const auto config = CertifiedEnvironmentConfig::canonical_v3();
    auto spec = trajectory_test::episode_spec(4301);
    spec.contract_id = std::string(kEpisodicEnvironmentV3ContractId);
    const auto policy = trajectory_test::provenance();

    auto factory = EpisodicEnvironment::create(config);
    require(std::holds_alternative<std::unique_ptr<EpisodicEnvironment>>(factory),
            "V3 environment factory rejected the pending replay fixture");
    auto environment = std::move(std::get<std::unique_ptr<EpisodicEnvironment>>(factory));

    RunControl control;
    control.engine_process_budget = 10000;
    control.semantic_action_budget = 10000;
    control.cancellation.source = "a3-admin-pending";
    const auto reset = environment->reset(spec, control);
    const auto* reset_accepted = std::get_if<ResetAccepted>(&reset);
    require(reset_accepted != nullptr &&
                std::holds_alternative<DecisionFrame>(reset_accepted->next),
            "V3 pending replay fixture did not publish a frame");
    const auto pending = std::get<DecisionFrame>(reset_accepted->next);

    TrajectoryRecorderV2 recorder(config, spec, policy,
                                 trajectory_test::test_provenance_resolver());
    std::string error;
    require(recorder.on_reset_accepted(*reset_accepted, std::nullopt, &error),
            "V2 recorder rejected the pending V3 reset: " + error);
    const auto interrupted = environment->interrupt(InterruptRequest{
        std::string(kEpisodicEnvironmentV3ContractId),
        InterruptionReason::AdministrativeCancel});
    const auto* accepted = std::get_if<InterruptAccepted>(&interrupted);
    require(accepted != nullptr, "V3 administrative interrupt was rejected");
    require(recorder.on_interrupt_accepted(pending, *accepted, &error),
            "V2 recorder rejected the pending interruption: " + error);
    const auto sealed = recorder.seal(&error);
    require(sealed.has_value(), "V2 recorder did not seal the pending interruption: " + error);

    CollectedV2 result;
    result.envelope = *sealed;
    result.evidence = evidence_for(accepted->interruption,
                                   result.envelope.manifest.episode_semantic_id);
    result.options.cancellation_source = control.cancellation.source;
    return result;
}

CollectedV2 collect_semantic_budget_action() {
    const auto config = CertifiedEnvironmentConfig::canonical_v3();
    auto spec = trajectory_test::episode_spec(4302);
    spec.contract_id = std::string(kEpisodicEnvironmentV3ContractId);
    const auto policy = trajectory_test::provenance();

    auto factory = EpisodicEnvironment::create(config);
    require(std::holds_alternative<std::unique_ptr<EpisodicEnvironment>>(factory),
            "V3 environment factory rejected the action replay fixture");
    auto environment = std::move(std::get<std::unique_ptr<EpisodicEnvironment>>(factory));

    RunControl control;
    control.engine_process_budget = 10000;
    control.semantic_action_budget = 1;
    control.cancellation.source = "a3-semantic-budget";
    const auto reset = environment->reset(spec, control);
    const auto* reset_accepted = std::get_if<ResetAccepted>(&reset);
    require(reset_accepted != nullptr &&
                std::holds_alternative<DecisionFrame>(reset_accepted->next),
            "V3 action replay fixture did not publish a frame");
    const auto frame = std::get<DecisionFrame>(reset_accepted->next);
    require(!frame.request.candidates.empty(), "V3 action replay fixture has an empty domain");

    TrajectoryRecorderV2 recorder(config, spec, policy,
                                 trajectory_test::test_provenance_resolver());
    std::string error;
    require(recorder.on_reset_accepted(*reset_accepted, std::nullopt, &error),
            "V2 recorder rejected the action V3 reset: " + error);
    const auto assignment = std::find_if(
        policy.participant_assignments.begin(), policy.participant_assignments.end(),
        [&](const auto& value) { return value.player == frame.acting_player; });
    require(assignment != policy.participant_assignments.end(),
            "action replay fixture has no acting assignment");
    const auto stepped = environment->step(ActionSelection{
        std::string(kEpisodicEnvironmentV3ContractId), frame.episode_semantic_id,
        frame.public_semantic_decision_id, frame.submission_token,
        frame.request.candidates.front().public_action_key});
    const auto* accepted = std::get_if<StepAccepted>(&stepped);
    require(accepted != nullptr, "V3 action replay fixture rejected its own public action");
    const auto* interrupted = std::get_if<EpisodeInterrupted>(&accepted->next);
    require(interrupted != nullptr &&
                interrupted->reason == InterruptionReason::SemanticActionBudget,
            "V3 action replay fixture did not close at its semantic budget");
    require(recorder.on_step_accepted(
                *accepted,
                trajectory_test::no_rng(assignment->participant_policy_assignment_id,
                                        frame.decision_index),
                std::nullopt, &error),
            "V2 recorder rejected the semantic-budget action: " + error);
    const auto sealed = recorder.seal(&error);
    require(sealed.has_value() && sealed->records.size() == 1,
            "V2 action replay fixture did not seal one record: " + error);

    CollectedV2 result;
    result.envelope = *sealed;
    result.evidence = evidence_for(*interrupted,
                                   result.envelope.manifest.episode_semantic_id);
    result.options.cancellation_source = control.cancellation.source;
    return result;
}

void test_valid_v2_replay_and_admission() {
    const auto pending = collect_administrative_pending();
    const auto pending_replay = replay_v2::replay_episode_v2(
        pending.envelope, pending.evidence, pending.options);
    require(pending_replay.accepted,
            "valid V2 administrative replay failed: " + pending_replay.error);

    std::string error;
    const auto pending_admission = admission_v2::verify_episode_for_admission_v2(
        pending.envelope, pending.evidence, pending.options,
        trajectory_test::test_provenance_resolver(), &error);
    require(pending_admission.has_value(), "valid V2 pending admission failed: " + error);
    require(pending_admission->public_gameplay_trajectory_id().rfind(
                "public_gameplay_trajectory.v2.", 0) == 0,
            "V2 admission returned a non-V2 gameplay identity");
    require(pending_admission->trajectory_record_id().rfind(
                "trajectory_record.v2.", 0) == 0,
            "V2 admission returned a non-V2 record identity");

    const auto action = collect_semantic_budget_action();
    const auto action_replay = replay_v2::replay_episode_v2(
        action.envelope, action.evidence, action.options);
    require(action_replay.accepted,
            "valid V2 action replay failed: " + action_replay.error);
    const auto action_admission = admission_v2::verify_episode_for_admission_v2(
        action.envelope, action.evidence, action.options,
        trajectory_test::test_provenance_resolver(), &error);
    require(action_admission.has_value(), "valid V2 action admission failed: " + error);
}

void test_v2_identity_and_domain_tampering_fails_closed() {
    const auto collected = collect_semantic_budget_action();
    auto tampered = collected.envelope;
    tampered.records.front().frame.public_candidate_domain_digest[0] =
        tampered.records.front().frame.public_candidate_domain_digest[0] == '0' ? '1' : '0';
    require(!replay_v2::replay_episode_v2(
                 tampered, collected.evidence, collected.options)
                  .accepted,
            "V2 replay accepted a tampered candidate-domain digest");

    tampered = collected.envelope;
    tampered.records.front().frame.public_semantic_decision_id[0] =
        tampered.records.front().frame.public_semantic_decision_id[0] == '0' ? '1' : '0';
    require(!replay_v2::replay_episode_v2(
                 tampered, collected.evidence, collected.options)
                  .accepted,
            "V2 replay accepted a tampered public decision identity");

    tampered = collected.envelope;
    tampered.records.front().selected_public_action_key =
        "public_action.v2." + std::string(64, '0');
    require(!replay_v2::replay_episode_v2(
                 tampered, collected.evidence, collected.options)
                  .accepted,
            "V2 replay accepted a tampered selected action key");

    tampered = collected.envelope;
    tampered.manifest.environment_identity_input.back() ^= 0x01U;
    require(!replay_v2::replay_episode_v2(
                 tampered, collected.evidence, collected.options)
                  .accepted,
            "V2 replay accepted tampered V3 environment identity bytes");

    tampered = collected.envelope;
    tampered.manifest.environment_identity_input =
        environment::canonical_environment_identity_bytes(
            environment::CertifiedEnvironmentConfig::canonical());
    tampered.manifest.environment_semantic_id =
        environment::CertifiedEnvironmentConfig::canonical().environment_semantic_id;
    require(!replay_v2::replay_episode_v2(
                 tampered, collected.evidence, collected.options)
                  .accepted,
            "V2 replay accepted a V2 environment identity in a V3 envelope");

    auto mismatched_evidence = collected.evidence;
    mismatched_evidence.episode_semantic_id = std::string(64, '0');
    require(!replay_v2::replay_episode_v2(
                 collected.envelope, mismatched_evidence, collected.options)
                  .accepted,
            "V2 replay accepted evidence for a different episode");

    std::string error;
    require(!admission_v2::verify_episode_for_admission_v2(
                 tampered, collected.evidence, collected.options,
                 trajectory_test::test_provenance_resolver(), &error)
                  .has_value(),
            "V2 admission accepted a V2 environment identity mismatch");

    auto quarantined = collected.envelope;
    quarantined.manifest.collection_disposition.kind =
        CollectionDispositionKind::QuarantinedAfterPolicyRejection;
    require(!admission_v2::verify_episode_for_admission_v2(
                 quarantined, collected.evidence, collected.options,
                 trajectory_test::test_provenance_resolver(), &error)
                  .has_value(),
            "V2 admission accepted a quarantined envelope");
}

void test_version_rejection_and_downstream_v1_boundary() {
    const auto collected = collect_administrative_pending();
    const auto v2_bytes = canonical_episode_envelope_bytes_v2(collected.envelope);
    const auto v2_evidence = canonical_restricted_replay_evidence_bytes_v2(collected.evidence);
    require(!decode_episode_envelope(v2_bytes),
            "historical V1 envelope decoder accepted V2 trajectory bytes");
    require(!decode_candidate_trajectory_shard(v2_bytes),
            "historical V1 shard decoder accepted V2 envelope bytes");
    require(!decode_restricted_replay_evidence(v2_evidence),
            "historical V1 replay evidence decoder accepted V2 evidence bytes");
    require(!decode_restricted_collection_evidence_bundle(v2_evidence),
            "historical V1 evidence bundle decoder accepted V2 evidence bytes");

    const auto v1_bytes = canonical_episode_envelope_bytes(
        trajectory_test::terminal_envelope(4303));
    require(!decode_episode_envelope_v2(v1_bytes),
            "V2 trajectory decoder accepted historical V1 envelope bytes");
}

void test_public_identity_is_stable_against_private_replay_inputs() {
    const auto first = collect_administrative_pending();
    const auto second = collect_administrative_pending();
    require(canonical_episode_envelope_bytes_v2(first.envelope) ==
                canonical_episode_envelope_bytes_v2(second.envelope),
            "paired public V2 replay inputs produced different canonical bytes");
    require(public_gameplay_trajectory_id_v2(first.envelope) ==
                public_gameplay_trajectory_id_v2(second.envelope),
            "paired public V2 replay inputs produced different gameplay identities");
    require(trajectory_record_id_v2(first.envelope) ==
                trajectory_record_id_v2(second.envelope),
            "paired public V2 replay inputs produced different record identities");
}

}  // namespace

int main() {
    try {
        test_valid_v2_replay_and_admission();
        test_v2_identity_and_domain_tampering_fails_closed();
        test_version_rejection_and_downstream_v1_boundary();
        test_public_identity_is_stable_against_private_replay_inputs();
        const auto stable = collect_administrative_pending();
        std::cout << "V2_CANONICAL_SHA256="
                  << trace::sha256_bytes(canonical_episode_envelope_bytes_v2(stable.envelope))
                  << "\nV2_GAMEPLAY_ID="
                  << public_gameplay_trajectory_id_v2(stable.envelope)
                  << "\nV2_RECORD_ID="
                  << trajectory_record_id_v2(stable.envelope)
                  << "\ntrusted trajectory V2 replay/admission tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
