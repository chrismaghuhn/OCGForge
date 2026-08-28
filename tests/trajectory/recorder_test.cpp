#include "ygo/trajectory/recorder.hpp"

#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "ygo/environment/public_action_identity.hpp"
#include "ygo/environment/public_environment_observation.hpp"
#include "ygo/observation/player_observation.hpp"

namespace {

using namespace ygo;
using namespace ygo::environment;
using namespace ygo::trajectory;

void require(const bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

PublicEnvironmentObservation observation(const std::uint8_t player,
                                         const std::uint64_t decision_index,
                                         const bool continuation = false) {
    observation::PlayerObservation source;
    source.perspective_player = player;
    source.decision_index = decision_index;
    source.match_context.perspective_player = player;
    source.match_context.own_deck.known = true;
    source.match_context.opponent_deck.known = false;
    source.decision_context.kind = continuation ? "card_selection" : "yes_no";
    source.decision_context.player = player;
    return project_public_observation(source);
}

PolicyArtifact artifact() {
    PolicyArtifact value;
    value.policy_kind = PolicyKind::DeterministicHeuristic;
    value.producer_implementation_identity = "ocgforge.test.producer.v1";
    value.inference_adapter_identity = "ocgforge.test.inference.v1";
    value.observation_adapter_identity = "ocgforge.test.observation.v1";
    value.action_adapter_identity = "ocgforge.test.action.v1";
    value.sampling_contract_identity = "ocgforge.test.deterministic_sampling.v1";
    value.policy_rng_contract_identity = kNoPolicyRngContractId;
    value.policy_artifact_id = compute_policy_artifact_id(value);
    return value;
}

ParticipantPolicyAssignment assignment(const PolicyArtifact& policy, const std::uint8_t player) {
    ParticipantPolicyAssignment value;
    value.player = player;
    value.seat_role = player == 0 ? SeatRole::StartingPlayer : SeatRole::NonStartingPlayer;
    value.deck_role = player == 0 ? DeckRole::FirstLockedDeck : DeckRole::SecondLockedDeck;
    value.resolved_locked_deck_id = player == 0 ? "ocgforge.swordsoul_tenyi.ml_v1"
                                                : "ocgforge.salamangreat.ml_v1";
    value.resolved_locked_deck_sha256 =
        player == 0 ? "8ee4b699de19ff256e388d46f35b8696a60ff6ec59f0324f060a2468876711b7"
                    : "6041abe0a59463d0715ae1da9100090ad487de02a02794e8ec0686d4c0513188";
    value.policy_artifact_id = policy.policy_artifact_id;
    value.participant_policy_assignment_id = compute_participant_policy_assignment_id(value);
    return value;
}

PolicyProvenanceEnvelope provenance() {
    const auto policy = artifact();
    auto player_zero = assignment(policy, 0);
    auto player_one = assignment(policy, 1);
    PolicyProvenanceEnvelope value;
    value.policy_artifacts = {policy};
    value.participant_assignments = {player_zero, player_one};
    std::sort(value.participant_assignments.begin(), value.participant_assignments.end(),
              [](const auto& left, const auto& right) {
                  return left.participant_policy_assignment_id <
                         right.participant_policy_assignment_id;
              });
    return value;
}

EpisodeSpec episode_spec() {
    EpisodeSpec value;
    value.root_seed = 23;
    value.starting_player = 0;
    value.seat_assignment = SeatAssignment::Normal;
    return value;
}

EnvironmentActionCandidate candidate(const EnvironmentActionKind action_kind,
                                     const std::string& continuation_operation,
                                     const bool submits_engine_response) {
    PublicActionKeyInput key_input;
    key_input.action_kind = std::string(environment_action_kind_name(action_kind));
    if (action_kind == EnvironmentActionKind::Pick ||
        action_kind == EnvironmentActionKind::AssignAmount) {
        key_input.source_index = 0;
    }
    if (action_kind == EnvironmentActionKind::AssignAmount) {
        key_input.amount = 0;
    }
    key_input.continuation_operation = continuation_operation;
    EnvironmentActionCandidate value;
    value.action_kind = action_kind;
    value.source_index = key_input.source_index;
    value.amount = key_input.amount;
    value.continuation_operation = continuation_operation;
    value.submits_engine_response = submits_engine_response;
    value.public_action_key = public_action_key(key_input);
    return value;
}

PublicFrameSnapshot frame(const CertifiedEnvironmentConfig& config, const EpisodeSpec& spec,
                          const std::uint64_t decision_index,
                          const EnvironmentActionCandidate& selected,
                          const bool continuation) {
    const auto episode_id = episode_semantic_id(config, spec);
    PublicFrameSnapshot value;
    value.episode_semantic_id = episode_id;
    value.decision_index = decision_index;
    value.acting_player = 0;
    value.public_observation = observation(0, decision_index, continuation);
    value.public_observation_digest = public_observation_digest(value.public_observation);
    value.request.kind = continuation ? EnvironmentDecisionKind::CardSelection
                                      : EnvironmentDecisionKind::YesNo;
    value.request.player = 0;
    if (continuation) {
        EnvironmentContinuationView view;
        view.continuation_kind = "unordered";
        view.continuation_step = static_cast<std::uint32_t>(decision_index);
        view.remaining_indices = {0, 1};
        view.min_count = 1;
        view.max_count = 1;
        view.available_mask = 3;
        view.continuation_steps = 2;
        view.can_finish = selected.continuation_operation == "finish";
        value.request.continuation = view;
    }
    value.request.candidates.push_back(selected);
    value.public_candidate_domain_digest = public_candidate_domain_digest(
        std::string(environment_decision_kind_name(value.request.kind)),
        {selected.public_action_key});
    PublicSemanticDecisionIdentityInput identity;
    identity.episode_semantic_id = value.episode_semantic_id;
    identity.decision_index = value.decision_index;
    identity.acting_player = value.acting_player;
    identity.request_kind = std::string(environment_decision_kind_name(value.request.kind));
    identity.public_observation_digest = value.public_observation_digest;
    identity.public_candidate_domain_digest = value.public_candidate_domain_digest;
    value.public_semantic_decision_id = public_semantic_decision_id(identity);
    return value;
}

DecisionFrame v2_frame(const PublicFrameSnapshot& value, const std::uint64_t engine_step_index) {
    return DecisionFrame{std::string(kEpisodicEnvironmentV2ContractId),
                         value.episode_semantic_id,
                         value.public_semantic_decision_id,
                         SubmissionToken{1, value.decision_index + 1},
                         value.decision_index,
                         engine_step_index,
                         value.acting_player,
                         value.public_observation,
                         value.request,
                         value.public_observation_digest,
                         value.public_candidate_domain_digest};
}

PolicyRngDecisionProvenance no_rng(const std::string& assignment_id,
                                   const std::uint64_t decision_index) {
    PolicyRngDecisionProvenance value;
    value.decision_index = decision_index;
    value.acting_policy_assignment_id = assignment_id;
    value.policy_rng_identity = kNoPolicyRngContractId;
    value.policy_rng_contract_identity = kNoPolicyRngContractId;
    value.policy_rng_stream_id = kNoPolicyRngContractId;
    value.policy_rng_initialization_identity = kNoPolicyRngContractId;
    value.mode = PolicyRngMode::None;
    return value;
}

TerminalViews terminal_views(const std::uint64_t decision_index) {
    return TerminalViews{observation(0, decision_index), observation(1, decision_index)};
}

EpisodeTerminal terminal(const std::string& episode_id, const std::uint64_t action_count) {
    EpisodeTerminal value;
    value.contract_id = std::string(kEpisodicEnvironmentV2ContractId);
    value.episode_semantic_id = episode_id;
    value.winner = 0;
    value.win_reason = 1;
    value.semantic_action_count = action_count;
    if (action_count != 0) {
        value.last_decision_index = action_count - 1;
    }
    return value;
}

EpisodeInterrupted interrupted(const std::string& episode_id,
                               const InterruptionReason reason,
                               const std::uint64_t action_count,
                               const std::optional<PublicFrameSnapshot>& pending) {
    EpisodeInterrupted value;
    value.contract_id = std::string(kEpisodicEnvironmentV2ContractId);
    value.episode_semantic_id = episode_id;
    value.reason = reason;
    value.semantic_action_count = action_count;
    if (pending.has_value()) {
        value.last_public_semantic_decision_id = pending->public_semantic_decision_id;
        value.last_decision_index = pending->decision_index;
    } else if (action_count != 0) {
        value.last_public_semantic_decision_id = "record-prefix";
        value.last_decision_index = action_count - 1;
    }
    return value;
}

void test_atomic_record_and_rejection() {
    const auto config = CertifiedEnvironmentConfig::canonical();
    const auto spec = episode_spec();
    const auto policy_provenance = provenance();
    TrajectoryRecorder recorder(config, spec, policy_provenance);
    const auto policy_assignment = std::find_if(
        policy_provenance.participant_assignments.begin(),
        policy_provenance.participant_assignments.end(),
        [](const auto& value) { return value.player == 0; });
    const auto yes = candidate(EnvironmentActionKind::YesNo, "", true);
    const auto initial = frame(config, spec, 0, yes, false);
    require(recorder.on_reset_accepted(ResetAccepted{v2_frame(initial, 7)}),
            "recorder rejected reset frame");
    require(recorder.lifecycle() == RecorderLifecycle::AwaitingAction,
            "recorder did not await the initial action");

    StepRejected rejected;
    rejected.contract_id = std::string(kEpisodicEnvironmentV2ContractId);
    rejected.rejection_code = RejectionCode::StaleSubmissionToken;
    rejected.current_episode_semantic_id = initial.episode_semantic_id;
    rejected.current_public_semantic_decision_id = initial.public_semantic_decision_id;
    rejected.current_public_candidate_domain_digest = initial.public_candidate_domain_digest;
    rejected.submitted_episode_semantic_id = "restricted-submission";
    rejected.submitted_public_action_key = "restricted-action";
    rejected.submitted_submission_token.episode_incarnation = 99;
    rejected.submitted_submission_token.frame_generation = 99;

    TrajectoryRecorder invalid_rejection(config, spec, policy_provenance);
    require(invalid_rejection.on_reset_accepted(ResetAccepted{v2_frame(initial, 7)}),
            "recorder rejected reset for invalid rejection invariant test");
    auto changed_state = rejected;
    changed_state.authoritative_state_unchanged = false;
    std::string invalid_error;
    require(!invalid_rejection.on_step_rejected(changed_state, true, &invalid_error),
            "recorder accepted a StepRejected that violated the unchanged-state invariant");
    require(invalid_rejection.lifecycle() == RecorderLifecycle::AwaitingAction &&
                invalid_rejection.records().empty() && !invalid_rejection.closure().has_value() &&
                invalid_rejection.manifest().collection_disposition.kind ==
                    CollectionDispositionKind::Clean,
            "invalid StepRejected encoded failure or quarantine mutation");

    require(recorder.on_step_rejected(rejected, true), "recorder rejected policy rejection");
    require(recorder.on_step_rejected(rejected, true),
            "recorder rejected a second observed policy rejection");
    require(recorder.records().empty(), "rejected action created a record");
    require(recorder.manifest().collection_disposition.kind ==
                CollectionDispositionKind::QuarantinedAfterPolicyRejection,
            "policy rejection did not quarantine the collection");
    require(recorder.manifest().collection_disposition.policy_rejections.size() == 2 &&
                recorder.manifest().collection_disposition.policy_rejections[0] ==
                    RejectionCode::StaleSubmissionToken &&
                recorder.manifest().collection_disposition.policy_rejections[1] ==
                    RejectionCode::StaleSubmissionToken,
            "observed equal policy rejection classifications were deduplicated or reordered");

    const auto episode_id = episode_semantic_id(config, spec);
    StepAccepted accepted;
    accepted.transition.episode_semantic_id = episode_id;
    accepted.transition.public_semantic_decision_id = initial.public_semantic_decision_id;
    accepted.transition.decision_index = 0;
    accepted.transition.selected_public_action_key = yes.public_action_key;
    accepted.transition.core_response_submitted = true;
    accepted.next = terminal(episode_id, 1);
    require(recorder.on_step_accepted(accepted, no_rng(policy_assignment->participant_policy_assignment_id, 0),
                                     terminal_views(1)),
            "recorder rejected accepted retry");
    require(recorder.records().size() == 1, "accepted retry did not create exactly one record");
    const auto sealed = recorder.seal();
    require(sealed.has_value(), "quarantined recorder did not seal");
    bool record_identity_rejected = false;
    try {
        (void)trajectory_record_id(*sealed);
    } catch (const std::exception&) {
        record_identity_rejected = true;
    }
    require(record_identity_rejected, "quarantined episode received a record identity");
}

void test_continuation_and_terminal() {
    const auto config = CertifiedEnvironmentConfig::canonical();
    const auto spec = episode_spec();
    const auto policy_provenance = provenance();
    TrajectoryRecorder recorder(config, spec, policy_provenance);
    const auto policy_assignment = std::find_if(
        policy_provenance.participant_assignments.begin(),
        policy_provenance.participant_assignments.end(),
        [](const auto& value) { return value.player == 0; });
    const auto pick = candidate(EnvironmentActionKind::Pick, "pick", false);
    const auto finish = candidate(EnvironmentActionKind::Finish, "finish", true);
    const auto initial = frame(config, spec, 0, pick, true);
    const auto next = frame(config, spec, 1, finish, true);
    require(recorder.on_reset_accepted(ResetAccepted{v2_frame(initial, 7)}),
            "continuation reset was rejected");

    StepAccepted intermediate;
    intermediate.transition = AcceptedActionTransition{
        initial.episode_semantic_id, initial.public_semantic_decision_id, 0,
        pick.public_action_key, false, std::nullopt};
    intermediate.next = v2_frame(next, 8);
    require(recorder.on_step_accepted(
                intermediate, no_rng(policy_assignment->participant_policy_assignment_id, 0)),
            "intermediate continuation was rejected");
    require(recorder.records().size() == 1 &&
                recorder.records().front().transition_class ==
                    TransitionClass::IntermediateContinuation,
            "intermediate continuation classification was not persisted");

    StepAccepted final;
    final.transition = AcceptedActionTransition{
        next.episode_semantic_id, next.public_semantic_decision_id, 1,
        finish.public_action_key, true, std::nullopt};
    final.next = terminal(next.episode_semantic_id, 2);
    require(recorder.on_step_accepted(
                final, no_rng(policy_assignment->participant_policy_assignment_id, 1),
                terminal_views(2)),
            "final continuation was rejected");
    require(recorder.records().size() == 2 &&
                recorder.records().back().transition_class ==
                    TransitionClass::FinalContinuationResponse,
            "final continuation classification was not persisted");
    const auto sealed = recorder.seal();
    require(sealed.has_value() && std::holds_alternative<TerminalClosure>(sealed->closure),
            "continuation recorder did not close terminally");
    require(std::get<TerminalClosure>(sealed->closure).semantic_action_count == 2,
            "continuation terminal count is wrong");
}

void test_administrative_pending_frame_and_failure() {
    const auto config = CertifiedEnvironmentConfig::canonical();
    const auto spec = episode_spec();
    const auto policy_provenance = provenance();
    const auto initial = frame(config, spec, 0, candidate(EnvironmentActionKind::YesNo, "", true),
                               false);
    TrajectoryRecorder recorder(config, spec, policy_provenance);
    require(recorder.on_reset_accepted(ResetAccepted{v2_frame(initial, 7)}),
            "administrative reset was rejected");
    const auto episode_id = episode_semantic_id(config, spec);
    const auto interruption = interrupted(episode_id, InterruptionReason::AdministrativeCancel, 0,
                                          initial);
    require(recorder.on_interrupt_accepted(
                v2_frame(initial, 7), InterruptAccepted{interruption}),
            "administrative interruption was rejected");
    const auto sealed = recorder.seal();
    require(sealed.has_value() && recorder.records().empty() &&
                std::get<InterruptedClosure>(sealed->closure).pending_unacted_frame.has_value(),
            "administrative interruption fabricated an action");

    TrajectoryRecorder mismatched_pending(config, spec, policy_provenance);
    require(mismatched_pending.on_reset_accepted(ResetAccepted{v2_frame(initial, 7)}),
            "mismatched-pending reset was rejected");
    auto changed_pending = v2_frame(initial, 7);
    changed_pending.request.candidates.front().submits_engine_response = false;
    std::string mismatch_error;
    require(!mismatched_pending.on_interrupt_accepted(
                changed_pending, InterruptAccepted{interruption}, &mismatch_error),
            "administrative interruption accepted a semantically changed pending frame");
    require(mismatched_pending.lifecycle() == RecorderLifecycle::Closed,
            "mismatched pending frame did not fail closed");

    TrajectoryRecorder failed(config, spec, policy_provenance);
    require(failed.on_reset_accepted(ResetAccepted{v2_frame(initial, 7)}),
            "failure reset was rejected");
    EpisodeFailure failure;
    failure.contract_id = std::string(kEpisodicEnvironmentV2ContractId);
    failure.episode_semantic_id = episode_id;
    failure.failure_code = FailureCode::CoreError;
    failure.failure_stage = FailureStage::Advance;
    failure.last_public_semantic_decision_id = initial.public_semantic_decision_id;
    require(failed.on_failure(failure), "failure closure was rejected");
    const auto failed_envelope = failed.seal();
    require(failed_envelope.has_value() && std::holds_alternative<FailedClosure>(failed_envelope->closure),
            "failure closure was not persisted");
    bool no_public_identity = false;
    try {
        (void)public_gameplay_trajectory_id(*failed_envelope);
    } catch (const std::exception&) {
        no_public_identity = true;
    }
    require(no_public_identity, "failed episode received a gameplay identity");
}

}  // namespace

int main() {
    try {
        test_atomic_record_and_rejection();
        test_continuation_and_terminal();
        test_administrative_pending_frame_and_failure();
        std::cout << "trajectory recorder tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "trajectory recorder tests failed: " << error.what() << '\n';
        return 1;
    }
}
