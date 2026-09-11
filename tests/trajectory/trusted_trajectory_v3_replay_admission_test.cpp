#include "ygo/environment/episodic_environment.hpp"
#include "ygo/policy/production_provenance.hpp"
#include "ygo/policy/teacher.hpp"
#include "ygo/policy/teacher_v3.hpp"
#include "ygo/teacher/salamangreat_profile.hpp"
#include "ygo/teacher/swordsoul_tenyi_profile.hpp"
#include "ygo/trajectory/admission_v3.hpp"
#include "ygo/trajectory/codec.hpp"
#include "ygo/trajectory/codec_v3.hpp"
#include "ygo/trajectory/identity_resolver.hpp"
#include "ygo/trajectory/recorder_v3.hpp"
#include "ygo/trajectory/replay_v3.hpp"
#include "ygo/trajectory/restricted_evidence_v3.hpp"
#include "ygo/trajectory/shard_v3.hpp"
#include "ygo/trajectory/trajectory_identity_v3.hpp"
#include "ygo/trace/sha256.hpp"

#include <algorithm>
#include <array>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>

#include "test_fixtures.hpp"

namespace {

using namespace ygo;
using namespace ygo::environment;
using namespace ygo::policy;
using namespace ygo::teacher;
using namespace ygo::trajectory;

void require(const bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

template <typename T>
void require(const DecodeResult<T>& result, const std::string& message) {
    require(static_cast<bool>(result), message);
}

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

PolicyRngDecisionProvenance no_rng(const std::string& assignment_id,
                                   const std::uint64_t decision_index) {
    PolicyRngDecisionProvenance result;
    result.decision_index = decision_index;
    result.acting_policy_assignment_id = assignment_id;
    result.policy_rng_identity = kNoPolicyRngContractId;
    result.policy_rng_contract_identity = kNoPolicyRngContractId;
    result.policy_rng_stream_id = kNoPolicyRngContractId;
    result.policy_rng_initialization_identity = kNoPolicyRngContractId;
    result.mode = PolicyRngMode::None;
    return result;
}

struct CollectedV3 final {
    EpisodeEnvelopeV3 envelope;
    RestrictedReplayEvidenceV3 evidence;
    replay_v3::ReplayOptions options;
};

RunControl control_for(const char* source, const std::uint64_t semantic_budget = 10000) {
    RunControl result;
    result.engine_process_budget = 10000;
    result.semantic_action_budget = semantic_budget;
    result.cancellation.reason = "ADMINISTRATIVE_CANCEL";
    result.cancellation.source = source;
    return result;
}

CollectedV3 collect_administrative_pending(const std::uint64_t root_seed) {
    const auto config = CertifiedEnvironmentConfig::canonical_v4();
    auto spec = trajectory_test::episode_spec(root_seed);
    spec.contract_id = std::string(kEpisodicEnvironmentV4ContractId);
    const auto policy = production_v3_provenance(config, spec);
    const auto control = control_for("v3-admin-pending");

    auto factory = EpisodicEnvironment::create(config);
    require(std::holds_alternative<std::unique_ptr<EpisodicEnvironment>>(factory),
            "V4 environment factory rejected the V3 pending replay fixture");
    auto environment = std::move(std::get<std::unique_ptr<EpisodicEnvironment>>(factory));
    const auto reset = environment->reset(spec, control);
    const auto* reset_accepted = std::get_if<ResetAccepted>(&reset);
    require(reset_accepted != nullptr &&
                std::holds_alternative<DecisionFrame>(reset_accepted->next),
            "V4 pending replay fixture did not publish a frame");
    const auto pending = std::get<DecisionFrame>(reset_accepted->next);

    TrajectoryRecorderV3 recorder(
        config, spec, policy, make_production_policy_provenance_resolver());
    std::string error;
    require(recorder.on_reset_accepted(*reset_accepted, std::nullopt, &error),
            "V3 recorder rejected the V4 reset: " + error);
    const auto interrupted = environment->interrupt(InterruptRequest{
        std::string(kEpisodicEnvironmentV4ContractId),
        InterruptionReason::AdministrativeCancel});
    const auto* accepted = std::get_if<InterruptAccepted>(&interrupted);
    require(accepted != nullptr, "V4 administrative interrupt was rejected");
    require(recorder.on_interrupt_accepted(
                std::optional<DecisionFrame>{pending}, *accepted, &error),
            "V3 recorder rejected the pending interruption: " + error);
    const auto sealed = recorder.seal(&error);
    require(sealed.has_value(), "V3 recorder did not seal the interruption: " + error);

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

CollectedV3 collect_semantic_budget_action() {
    const auto config = CertifiedEnvironmentConfig::canonical_v4();
    auto spec = trajectory_test::episode_spec(4302);
    spec.contract_id = std::string(kEpisodicEnvironmentV4ContractId);
    const auto policy = production_v3_provenance(config, spec);
    const auto control = control_for("v3-semantic-budget", 1);

    auto factory = EpisodicEnvironment::create(config);
    require(std::holds_alternative<std::unique_ptr<EpisodicEnvironment>>(factory),
            "V4 environment factory rejected the V3 action replay fixture");
    auto environment = std::move(std::get<std::unique_ptr<EpisodicEnvironment>>(factory));
    const auto reset = environment->reset(spec, control);
    const auto* reset_accepted = std::get_if<ResetAccepted>(&reset);
    require(reset_accepted != nullptr &&
                std::holds_alternative<DecisionFrame>(reset_accepted->next),
            "V4 action replay fixture did not publish a frame");
    const auto frame = std::get<DecisionFrame>(reset_accepted->next);
    require(!frame.request.candidates.empty(), "V4 action replay fixture has an empty domain");

    TrajectoryRecorderV3 recorder(
        config, spec, policy, make_production_policy_provenance_resolver());
    std::string error;
    require(recorder.on_reset_accepted(*reset_accepted, std::nullopt, &error),
            "V3 recorder rejected the action reset: " + error);
    const auto assignment = std::find_if(
        policy.participant_assignments.begin(), policy.participant_assignments.end(),
        [&](const auto& value) { return value.player == frame.acting_player; });
    require(assignment != policy.participant_assignments.end(),
            "V3 action replay fixture has no acting assignment");
    const auto stepped = environment->step(ActionSelection{
        std::string(kEpisodicEnvironmentV4ContractId), frame.episode_semantic_id,
        frame.public_semantic_decision_id, frame.submission_token,
        frame.request.candidates.front().public_action_key});
    const auto* accepted = std::get_if<StepAccepted>(&stepped);
    require(accepted != nullptr, "V4 action replay fixture rejected its own public action");
    const auto* interrupted = std::get_if<EpisodeInterrupted>(&accepted->next);
    require(interrupted != nullptr &&
                interrupted->reason == InterruptionReason::SemanticActionBudget,
            "V4 action replay fixture did not close at its semantic budget");
    require(recorder.on_step_accepted(
                *accepted, no_rng(assignment->participant_policy_assignment_id,
                                  frame.decision_index),
                std::nullopt, &error),
            "V3 recorder rejected the semantic-budget action: " + error);
    const auto sealed = recorder.seal(&error);
    require(sealed.has_value() && sealed->records.size() == 1,
            "V3 action replay fixture did not seal one record: " + error);

    CollectedV3 result;
    result.envelope = *sealed;
    result.evidence.episode_semantic_id = result.envelope.manifest.episode_semantic_id;
    result.evidence.interruption_reason = interrupted->reason;
    result.evidence.engine_process_budget =
        interrupted->run_control_evidence.engine_process_budget;
    result.evidence.semantic_action_budget =
        interrupted->run_control_evidence.semantic_action_budget;
    result.evidence.observed_engine_process_count =
        interrupted->run_control_evidence.engine_process_count;
    result.evidence.observed_semantic_action_count =
        interrupted->run_control_evidence.semantic_action_count;
    result.evidence.final_engine_step_index = interrupted->final_engine_step_index;
    result.options.cancellation_source = control.cancellation.source;
    return result;
}

void test_valid_v3_replay_and_admission() {
    const auto pending = collect_administrative_pending(4301);
    const auto evidence_bytes =
        canonical_restricted_replay_evidence_bytes_v3(pending.evidence);
    require(decode_restricted_replay_evidence_v3(evidence_bytes),
            "V3 restricted replay evidence did not decode canonically");
    auto evidence_trailing = evidence_bytes;
    evidence_trailing.push_back(0x00U);
    require(!decode_restricted_replay_evidence_v3(evidence_trailing),
            "V3 restricted replay evidence accepted trailing bytes");
    require(!decode_restricted_replay_evidence_v2(evidence_bytes),
            "V2 evidence decoder accepted V3 restricted replay evidence");
    const auto pending_replay = replay_v3::replay_episode_v3(
        pending.envelope, pending.evidence, pending.options);
    require(pending_replay.accepted,
            "valid V3 administrative replay failed: " + pending_replay.error);

    std::string error;
    const auto pending_admission = admission_v3::verify_episode_for_admission_v3(
        pending.envelope, pending.evidence, pending.options,
        make_production_policy_provenance_resolver(), &error);
    require(pending_admission.has_value(), "valid V3 pending admission failed: " + error);
    require(pending_admission->public_gameplay_trajectory_id().rfind(
                "public_gameplay_trajectory.v3.", 0) == 0 &&
                pending_admission->trajectory_record_id().rfind(
                    "trajectory_record.v3.", 0) == 0,
            "V3 admission returned a non-V3 trajectory identity");

    const auto action = collect_semantic_budget_action();
    const auto action_replay = replay_v3::replay_episode_v3(
        action.envelope, action.evidence, action.options);
    require(action_replay.accepted,
            "valid V3 action replay failed: " + action_replay.error);
    require(admission_v3::verify_episode_for_admission_v3(
                action.envelope, action.evidence, action.options,
                make_production_policy_provenance_resolver(), &error)
                .has_value(),
            "valid V3 action admission failed: " + error);
}

void test_v3_tampering_and_cross_generation_rejection() {
    const auto collected = collect_semantic_budget_action();
    std::string error;

    auto tampered = collected.envelope;
    tampered.records.front().frame.public_candidate_domain_digest[0] =
        tampered.records.front().frame.public_candidate_domain_digest[0] == '0' ? '1' : '0';
    require(!replay_v3::replay_episode_v3(
                 tampered, collected.evidence, collected.options)
                  .accepted,
            "V3 replay accepted a tampered candidate-domain digest");

    tampered = collected.envelope;
    tampered.records.front().frame.public_semantic_decision_id[0] =
        tampered.records.front().frame.public_semantic_decision_id[0] == '0' ? '1' : '0';
    require(!replay_v3::replay_episode_v3(
                 tampered, collected.evidence, collected.options)
                  .accepted,
            "V3 replay accepted a tampered public decision identity");

    tampered = collected.envelope;
    tampered.records.front().selected_public_action_key =
        "public_action.v3." + std::string(64, '0');
    require(!replay_v3::replay_episode_v3(
                 tampered, collected.evidence, collected.options)
                  .accepted,
            "V3 replay accepted a tampered selected action key");

    tampered = collected.envelope;
    tampered.manifest.environment_identity_input.back() ^= 0x01U;
    require(!replay_v3::replay_episode_v3(
                 tampered, collected.evidence, collected.options)
                  .accepted,
            "V3 replay accepted tampered V4 environment identity bytes");

    auto mismatched_evidence = collected.evidence;
    mismatched_evidence.episode_semantic_id = std::string(64, '0');
    require(!replay_v3::replay_episode_v3(
                 collected.envelope, mismatched_evidence, collected.options)
                  .accepted,
            "V3 replay accepted evidence for a different episode");
    require(!replay_v3::replay_episode_v3(
                 collected.envelope, std::nullopt,
                 collected.options)
                  .accepted,
            "V3 replay accepted an interrupted envelope without evidence");

    auto failed = collected.envelope;
    failed.records.clear();
    FailedClosureV3 failed_closure;
    failed_closure.failure_code = FailureCode::CoreError;
    failed_closure.failure_stage = FailureStage::Advance;
    failed.closure = failed_closure;
    require(!admission_v3::verify_episode_for_admission_v3(
                 failed, std::nullopt, replay_v3::ReplayOptions{},
                 make_production_policy_provenance_resolver(), &error)
                  .has_value(),
            "V3 admission accepted a FailedClosureV3 envelope");

    auto quarantined = collected.envelope;
    quarantined.manifest.collection_disposition.kind =
        CollectionDispositionKind::QuarantinedAfterPolicyRejection;
    quarantined.manifest.collection_disposition.policy_rejections = {
        RejectionCode::StaleSubmissionToken};
    require(!admission_v3::verify_episode_for_admission_v3(
                 quarantined, collected.evidence, collected.options,
                 make_production_policy_provenance_resolver(), &error)
                  .has_value(),
            "V3 admission accepted a quarantined envelope");

    const auto v3_bytes = canonical_episode_envelope_bytes_v3(collected.envelope);
    auto trailing_bytes = v3_bytes;
    trailing_bytes.push_back(0x00U);
    require(!decode_episode_envelope_v3(trailing_bytes),
            "V3 trajectory decoder accepted trailing envelope bytes");
    require(!decode_episode_envelope_v2(v3_bytes),
            "V2 trajectory decoder accepted V3 envelope bytes");
    const auto v2_bytes = canonical_episode_envelope_bytes(
        trajectory_test::terminal_envelope(4304));
    require(!decode_episode_envelope_v3(v2_bytes),
            "V3 trajectory decoder accepted V2 envelope bytes");

    CandidateTrajectoryShardV3 mixed;
    mixed.entries.push_back({trace::sha256_bytes(v3_bytes), v3_bytes});
    mixed.entries.push_back({trace::sha256_bytes(v2_bytes), v2_bytes});
    bool mixed_rejected = false;
    try {
        (void)canonical_candidate_trajectory_shard_bytes_v3(mixed);
    } catch (...) {
        mixed_rejected = true;
    }
    require(mixed_rejected, "V3 shard accepted a mixed V2/V3 envelope set");
}

}  // namespace

int main() {
    try {
        test_valid_v3_replay_and_admission();
        test_v3_tampering_and_cross_generation_rejection();
        const auto stable = collect_administrative_pending(4301);
        std::cout << "V3_CANONICAL_SHA256="
                  << trace::sha256_bytes(canonical_episode_envelope_bytes_v3(stable.envelope))
                  << "\nV3_EVIDENCE_SHA256="
                  << restricted_replay_evidence_artifact_sha256_v3(stable.evidence)
                  << "\nV3_GAMEPLAY_ID="
                  << public_gameplay_trajectory_id_v3(stable.envelope)
                  << "\nV3_RECORD_ID=" << trajectory_record_id_v3(stable.envelope)
                  << "\ntrusted trajectory V3 replay/admission tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
