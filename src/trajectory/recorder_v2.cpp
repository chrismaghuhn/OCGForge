#include "ygo/trajectory/recorder_v2.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

#include "ygo/trajectory/identity_resolver.hpp"
#include "ygo/trajectory/policy_provenance.hpp"

namespace ygo::trajectory {
namespace {

std::optional<std::string> assignment_for(
    const PolicyProvenanceEnvelope& provenance,
    const std::uint8_t player,
    const std::uint64_t decision_index) {
    std::optional<const ParticipantPolicyAssignment*> result;
    for (const auto& assignment : provenance.participant_assignments) {
        if (assignment.player != player ||
            assignment.effective_from_decision_index > decision_index) {
            continue;
        }
        if (!result.has_value() ||
            assignment.effective_from_decision_index >
                (*result)->effective_from_decision_index) {
            result = &assignment;
        }
    }
    return result.has_value()
               ? std::optional<std::string>{(*result)->participant_policy_assignment_id}
               : std::nullopt;
}

const ParticipantPolicyAssignment* assignment_by_id(
    const PolicyProvenanceEnvelope& provenance,
    const std::string_view assignment_id) noexcept {
    const auto it = std::find_if(
        provenance.participant_assignments.begin(),
        provenance.participant_assignments.end(),
        [assignment_id](const auto& assignment) {
            return assignment.participant_policy_assignment_id == assignment_id;
        });
    return it == provenance.participant_assignments.end() ? nullptr : &*it;
}

const PolicyArtifact* artifact_by_id(
    const PolicyProvenanceEnvelope& provenance,
    const std::string_view artifact_id) noexcept {
    const auto it = std::find_if(
        provenance.policy_artifacts.begin(),
        provenance.policy_artifacts.end(),
        [artifact_id](const auto& artifact) {
            return artifact.policy_artifact_id == artifact_id;
        });
    return it == provenance.policy_artifacts.end() ? nullptr : &*it;
}

bool validate_record_rng_attribution(
    const PolicyProvenanceEnvelope& provenance,
    const ProvenanceResolver& resolver,
    const std::string_view assignment_id,
    const PolicyRngDecisionProvenance& attribution,
    std::string& error) {
    try {
        const auto* assignment = assignment_by_id(provenance, assignment_id);
        if (assignment == nullptr) {
            error = "policy attribution references an unknown participant assignment";
            return false;
        }
        const auto* artifact = artifact_by_id(provenance, assignment->policy_artifact_id);
        if (artifact == nullptr) {
            error = "policy attribution references an unknown policy artifact";
            return false;
        }
        const bool artifact_uses_rng =
            artifact->policy_rng_contract_identity != kNoPolicyRngContractId;
        if (attribution.mode == PolicyRngMode::None) {
            if (artifact_uses_rng) {
                error = "stochastic policy record has NONE RNG attribution";
                return false;
            }
            return true;
        }
        if (!artifact_uses_rng ||
            attribution.policy_rng_contract_identity != artifact->policy_rng_contract_identity) {
            error = "record RNG attribution disagrees with its policy artifact";
            return false;
        }
        const auto* descriptor = resolver.policy_rng_contract_descriptor(
            attribution.policy_rng_contract_identity);
        if (descriptor == nullptr) {
            error = "record RNG attribution lacks a typed contract descriptor";
            return false;
        }
        PolicyRngStreamIdentity stream;
        stream.policy_artifact_id = artifact->policy_artifact_id;
        stream.participant_policy_assignment_id = assignment->participant_policy_assignment_id;
        stream.policy_rng_contract_identity = attribution.policy_rng_contract_identity;
        stream.policy_rng_stream_id = attribution.policy_rng_stream_id;
        stream.policy_rng_initialization_identity =
            attribution.policy_rng_initialization_identity;
        if (compute_policy_rng_stream_id(stream) != attribution.policy_rng_identity) {
            error = "record RNG stream identity does not recompute from provenance";
            return false;
        }
        if (attribution.mode == PolicyRngMode::Cursor) {
            if (!descriptor->cursor_is_unique) {
                error = "CURSOR RNG provenance lacks a typed uniqueness authority";
                return false;
            }
        } else if (!descriptor->state_is_canonical ||
                   !attribution.pre_state.has_value() ||
                   !attribution.post_state.has_value() ||
                   !descriptor->state_is_canonical(*attribution.pre_state) ||
                   !descriptor->state_is_canonical(*attribution.post_state)) {
            error = "STATE RNG provenance is not canonical for its contract";
            return false;
        }
        return true;
    } catch (const std::exception& exception) {
        error = exception.what();
        return false;
    } catch (...) {
        error = "policy RNG attribution validation threw";
        return false;
    }
}

bool validate_terminal_views(const TerminalViews& views) noexcept {
    return views.player_0.perspective_player == 0 &&
           views.player_1.perspective_player == 1;
}

}  // namespace

TrajectoryRecorderV2::TrajectoryRecorderV2(
    environment::CertifiedEnvironmentConfig config,
    environment::EpisodeSpec spec,
    PolicyProvenanceEnvelope policy_provenance)
    : TrajectoryRecorderV2(std::move(config), std::move(spec),
                           std::move(policy_provenance), ProvenanceResolver{}) {}

TrajectoryRecorderV2::TrajectoryRecorderV2(
    environment::CertifiedEnvironmentConfig config,
    environment::EpisodeSpec spec,
    PolicyProvenanceEnvelope policy_provenance,
    const ProvenanceResolver& resolver)
    : config_(std::move(config)), spec_(std::move(spec)), resolver_(resolver) {
    if (!is_current_certified_environment_v3(config_) ||
        config_.contract_id != environment::kEpisodicEnvironmentV3ContractId ||
        spec_.contract_id != environment::kEpisodicEnvironmentV3ContractId) {
        throw std::invalid_argument("V2 trajectory recorder requires current V3 environment");
    }
    const auto expected_environment_id = environment::environment_semantic_id(config_);
    const auto expected_episode_id = environment::episode_semantic_id(config_, spec_);
    if (!config_.environment_semantic_id.empty() &&
        config_.environment_semantic_id != expected_environment_id) {
        throw std::invalid_argument("V2 recorder environment identity mismatch");
    }
    manifest_.environment_semantic_id = expected_environment_id;
    manifest_.environment_identity_input =
        environment::canonical_environment_identity_bytes(config_);
    manifest_.episode_semantic_id = expected_episode_id;
    manifest_.episode_identity_input =
        environment::canonical_episode_identity_bytes(config_, spec_);
    manifest_.policy_provenance = std::move(policy_provenance);
    std::string error;
    if (!resolver_.validate(manifest_.policy_provenance, config_, spec_, &error)) {
        throw std::invalid_argument("V2 recorder provenance is invalid: " + error);
    }
    (void)canonical_episode_manifest_bytes_v2(manifest_);
}

bool TrajectoryRecorderV2::set_error(std::string* error, std::string message) const {
    if (error != nullptr) {
        *error = std::move(message);
    }
    return false;
}

bool TrajectoryRecorderV2::fail_closed(
    const environment::FailureCode code,
    const environment::FailureStage stage,
    const bool mutation_may_have_occurred,
    std::string* error,
    std::string message) {
    FailedClosureV2 closure;
    closure.failure_code = code;
    closure.failure_stage = stage;
    closure.mutation_may_have_occurred = mutation_may_have_occurred;
    closure.record_count = records_.size();
    closure_ = std::move(closure);
    current_frame_.reset();
    lifecycle_ = RecorderLifecycle::Closed;
    return set_error(error, std::move(message));
}

bool TrajectoryRecorderV2::capture_frame(
    const environment::DecisionFrame& frame,
    std::optional<PublicFrameSnapshotV2>& output,
    const std::uint64_t expected_decision_index,
    std::string* error) const {
    try {
        if (frame.contract_id != environment::kEpisodicEnvironmentV3ContractId ||
            frame.episode_semantic_id != manifest_.episode_semantic_id ||
            frame.decision_index != expected_decision_index) {
            return set_error(error, "V3 frame does not match V2 recorder boundary");
        }
        PublicFrameSnapshotV2 snapshot;
        snapshot.episodic_environment_contract_id = frame.contract_id;
        snapshot.episode_semantic_id = frame.episode_semantic_id;
        snapshot.public_semantic_decision_id = frame.public_semantic_decision_id;
        snapshot.decision_index = frame.decision_index;
        snapshot.acting_player = frame.acting_player;
        snapshot.public_observation = frame.public_observation;
        snapshot.public_observation_digest = frame.public_observation_digest;
        snapshot.request = frame.request;
        snapshot.public_candidate_domain_digest = frame.public_candidate_domain_digest;
        (void)canonical_public_frame_snapshot_bytes_v2(snapshot);
        output = std::move(snapshot);
        return true;
    } catch (const std::exception& exception) {
        return set_error(error, exception.what());
    } catch (...) {
        return set_error(error, "capturing V3 public frame threw");
    }
}

bool TrajectoryRecorderV2::capture_terminal(
    const environment::EpisodeTerminal& terminal,
    const TerminalViews& views,
    std::string* error) {
    try {
        if (terminal.contract_id != environment::kEpisodicEnvironmentV3ContractId ||
            terminal.episode_semantic_id != manifest_.episode_semantic_id ||
            terminal.semantic_action_count != records_.size() ||
            !validate_terminal_views(views)) {
            return set_error(error, "V3 terminal does not match V2 recorder boundary");
        }
        if (records_.empty() ? terminal.last_decision_index.has_value()
                             : !terminal.last_decision_index.has_value() ||
                                   *terminal.last_decision_index + 1 != records_.size()) {
            return set_error(error, "V3 terminal decision count is inconsistent");
        }
        TerminalClosureV2 closure;
        closure.winner = terminal.winner;
        closure.win_reason = terminal.win_reason;
        closure.semantic_action_count = terminal.semantic_action_count;
        closure.last_decision_index = terminal.last_decision_index;
        closure.terminal_view_player_0 = views.player_0;
        closure.terminal_view_player_0_digest =
            environment::public_observation_digest(views.player_0);
        closure.terminal_view_player_1 = views.player_1;
        closure.terminal_view_player_1_digest =
            environment::public_observation_digest(views.player_1);
        (void)canonical_episode_closure_bytes_v2(closure);
        closure_ = std::move(closure);
        return true;
    } catch (const std::exception& exception) {
        return set_error(error, exception.what());
    } catch (...) {
        return set_error(error, "capturing V3 terminal threw");
    }
}

bool TrajectoryRecorderV2::capture_interruption(
    const environment::EpisodeInterrupted& interruption,
    std::string* error) {
    if (interruption.contract_id != environment::kEpisodicEnvironmentV3ContractId ||
        interruption.episode_semantic_id != manifest_.episode_semantic_id ||
        interruption.semantic_action_count != records_.size()) {
        return set_error(error, "V3 interruption does not match V2 recorder boundary");
    }
    const auto expected_last = records_.empty()
                                   ? std::optional<std::uint64_t>{}
                                   : std::optional<std::uint64_t>{records_.size() - 1};
    if (interruption.last_decision_index != expected_last ||
        interruption.last_public_semantic_decision_id.has_value() != !records_.empty() ||
        (interruption.last_public_semantic_decision_id.has_value() &&
         *interruption.last_public_semantic_decision_id !=
             records_.back().frame.public_semantic_decision_id)) {
        return set_error(error, "V3 interruption count or prefix is inconsistent");
    }
    InterruptedClosureV2 closure;
    closure.record_count = records_.size();
    closure_ = std::move(closure);
    return true;
}

bool TrajectoryRecorderV2::capture_failure(
    const environment::EpisodeFailure& failure,
    std::string* error) {
    const auto expected_last_id = current_frame_.has_value()
                                      ? std::optional<std::string>{
                                            current_frame_->public_semantic_decision_id}
                                      : (records_.empty()
                                             ? std::optional<std::string>{}
                                             : std::optional<std::string>{
                                                   records_.back().frame.public_semantic_decision_id});
    if (failure.contract_id != environment::kEpisodicEnvironmentV3ContractId ||
        (failure.episode_semantic_id.has_value() &&
         *failure.episode_semantic_id != manifest_.episode_semantic_id) ||
        failure.semantic_action_count != records_.size() ||
        failure.last_public_semantic_decision_id != expected_last_id) {
        return set_error(error, "V3 failure does not match V2 recorder boundary");
    }
    FailedClosureV2 closure;
    closure.failure_code = failure.failure_code;
    closure.failure_stage = failure.failure_stage;
    closure.mutation_may_have_occurred = failure.mutation_may_have_occurred;
    closure.record_count = records_.size();
    try {
        (void)canonical_episode_closure_bytes_v2(closure);
    } catch (const std::exception& exception) {
        return set_error(error, exception.what());
    }
    closure_ = std::move(closure);
    return true;
}

bool TrajectoryRecorderV2::on_reset_accepted(
    const environment::ResetAccepted& accepted,
    const std::optional<TerminalViews>& terminal_views,
    std::string* error) {
    if (lifecycle_ != RecorderLifecycle::Empty || !records_.empty() ||
        closure_.has_value()) {
        return set_error(error, "V2 recorder reset is not at EMPTY");
    }
    if (const auto* frame = std::get_if<environment::DecisionFrame>(&accepted.next)) {
        if (!capture_frame(*frame, current_frame_, records_.size(), error)) {
            return fail_closed(environment::FailureCode::PublicFrameInvariant,
                               environment::FailureStage::Projection, false, error,
                               error != nullptr ? *error : "invalid initial V3 frame");
        }
        lifecycle_ = RecorderLifecycle::AwaitingAction;
        return true;
    }
    if (const auto* terminal = std::get_if<environment::EpisodeTerminal>(&accepted.next)) {
        if (!terminal_views.has_value() || !capture_terminal(*terminal, *terminal_views, error)) {
            return fail_closed(environment::FailureCode::ObservationFailure,
                               environment::FailureStage::Projection, false, error,
                               error != nullptr ? *error : "invalid V3 terminal closure");
        }
        lifecycle_ = RecorderLifecycle::Closed;
        return true;
    }
    if (const auto* interrupted =
            std::get_if<environment::EpisodeInterrupted>(&accepted.next)) {
        if (!capture_interruption(*interrupted, error)) {
            return fail_closed(environment::FailureCode::PublicFrameInvariant,
                               environment::FailureStage::Interruption, false, error,
                               error != nullptr ? *error : "invalid V3 interruption closure");
        }
        lifecycle_ = RecorderLifecycle::Closed;
        return true;
    }
    const auto* failure = std::get_if<environment::EpisodeFailure>(&accepted.next);
    if (failure == nullptr || !capture_failure(*failure, error)) {
        return fail_closed(environment::FailureCode::InvalidAuthoritativeState,
                           environment::FailureStage::Construction, false, error,
                           error != nullptr ? *error : "invalid V3 failure closure");
    }
    lifecycle_ = RecorderLifecycle::Closed;
    return true;
}

bool TrajectoryRecorderV2::on_step_accepted(
    const environment::StepAccepted& accepted,
    const PolicyRngDecisionProvenance& attribution,
    const std::optional<TerminalViews>& terminal_views,
    std::string* error) {
    if (lifecycle_ != RecorderLifecycle::AwaitingAction || !current_frame_.has_value()) {
        return set_error(error, "V2 recorder accepted action outside an awaiting frame");
    }
    const auto current = *current_frame_;
    if (accepted.transition.episode_semantic_id != current.episode_semantic_id ||
        accepted.transition.public_semantic_decision_id != current.public_semantic_decision_id ||
        accepted.transition.decision_index != current.decision_index) {
        return fail_closed(environment::FailureCode::PublicFrameInvariant,
                           environment::FailureStage::Action, true, error,
                           "V3 accepted transition does not match current frame");
    }
    const auto& selected_key = accepted.transition.selected_public_action_key;
    const environment::EnvironmentActionCandidate* selected = nullptr;
    std::size_t matches = 0;
    for (const auto& candidate : current.request.candidates) {
        if (candidate.public_action_key == selected_key) {
            selected = &candidate;
            ++matches;
        }
    }
    if (matches != 1 || selected == nullptr) {
        return fail_closed(environment::FailureCode::PublicFrameInvariant,
                           environment::FailureStage::Action, true, error,
                           "V3 transition selected an unknown public key");
    }
    TransitionClass transition_class;
    if (!current.request.continuation.has_value()) {
        if (!selected->submits_engine_response ||
            !accepted.transition.core_response_submitted) {
            return fail_closed(environment::FailureCode::ResponseInconsistency,
                               environment::FailureStage::Advance, true, error,
                               "V2 atomic transition classification is inconsistent");
        }
        transition_class = TransitionClass::AtomicEngineResponse;
    } else if (selected->submits_engine_response) {
        if (!accepted.transition.core_response_submitted) {
            return fail_closed(environment::FailureCode::ResponseInconsistency,
                               environment::FailureStage::Advance, true, error,
                               "V2 final continuation response was not submitted");
        }
        transition_class = TransitionClass::FinalContinuationResponse;
    } else {
        if (accepted.transition.core_response_submitted) {
            return fail_closed(environment::FailureCode::ResponseInconsistency,
                               environment::FailureStage::Advance, true, error,
                               "V2 intermediate continuation submitted a response");
        }
        transition_class = TransitionClass::IntermediateContinuation;
    }
    const auto expected_assignment =
        assignment_for(manifest_.policy_provenance, current.acting_player,
                       current.decision_index);
    if (!expected_assignment.has_value() ||
        attribution.acting_policy_assignment_id != *expected_assignment ||
        attribution.decision_index != current.decision_index) {
        return fail_closed(environment::FailureCode::PublicFrameInvariant,
                           environment::FailureStage::Action, true, error,
                           "V2 policy attribution does not resolve to acting assignment");
    }
    std::string attribution_error;
    if (!validate_record_rng_attribution(manifest_.policy_provenance, resolver_,
                                         *expected_assignment, attribution,
                                         attribution_error)) {
        return fail_closed(environment::FailureCode::PublicFrameInvariant,
                           environment::FailureStage::Action, true, error,
                           std::move(attribution_error));
    }

    DecisionRecordV2 record;
    record.frame = current;
    record.selected_public_action_key = selected_key;
    record.transition_class = transition_class;
    record.acting_policy_assignment_id = *expected_assignment;
    record.policy_rng_decision_provenance = attribution;

    std::optional<PublicFrameSnapshotV2> next_frame;
    std::optional<EpisodeClosureV2> next_closure;
    if (const auto* frame = std::get_if<environment::DecisionFrame>(&accepted.next)) {
        if (!capture_frame(*frame, next_frame, records_.size() + 1, error) ||
            !next_frame.has_value()) {
            return fail_closed(environment::FailureCode::PublicFrameInvariant,
                               environment::FailureStage::Projection, true, error,
                               error != nullptr ? *error : "invalid V3 successor frame");
        }
        NextFrameTarget target;
        target.kind = NextFrameTargetKind::NextDecisionRecord;
        target.next_decision_index = next_frame->decision_index;
        target.next_public_semantic_decision_id =
            next_frame->public_semantic_decision_id;
        record.successor.kind = SuccessorKind::NextFrame;
        record.successor.next_frame = std::move(target);
    } else if (const auto* terminal =
                   std::get_if<environment::EpisodeTerminal>(&accepted.next)) {
        if (!terminal_views.has_value() ||
            terminal->contract_id != environment::kEpisodicEnvironmentV3ContractId ||
            terminal->episode_semantic_id != manifest_.episode_semantic_id ||
            terminal->semantic_action_count != records_.size() + 1 ||
            !terminal->last_decision_index.has_value() ||
            *terminal->last_decision_index + 1 != records_.size() + 1 ||
            !validate_terminal_views(*terminal_views)) {
            return fail_closed(environment::FailureCode::PublicFrameInvariant,
                               environment::FailureStage::Projection, true, error,
                               "V3 terminal successor is inconsistent");
        }
        TerminalClosureV2 closure;
        closure.winner = terminal->winner;
        closure.win_reason = terminal->win_reason;
        closure.semantic_action_count = terminal->semantic_action_count;
        closure.last_decision_index = terminal->last_decision_index;
        closure.terminal_view_player_0 = terminal_views->player_0;
        closure.terminal_view_player_0_digest =
            environment::public_observation_digest(terminal_views->player_0);
        closure.terminal_view_player_1 = terminal_views->player_1;
        closure.terminal_view_player_1_digest =
            environment::public_observation_digest(terminal_views->player_1);
        record.successor.kind = SuccessorKind::Terminal;
        next_closure = std::move(closure);
    } else if (const auto* interrupted =
                   std::get_if<environment::EpisodeInterrupted>(&accepted.next)) {
        if (interrupted->contract_id != environment::kEpisodicEnvironmentV3ContractId ||
            interrupted->episode_semantic_id != manifest_.episode_semantic_id ||
            interrupted->semantic_action_count != records_.size() + 1 ||
            interrupted->last_decision_index !=
                std::optional<std::uint64_t>{current.decision_index} ||
            interrupted->last_public_semantic_decision_id !=
                std::optional<std::string>{current.public_semantic_decision_id}) {
            return fail_closed(environment::FailureCode::PublicFrameInvariant,
                               environment::FailureStage::Interruption, true, error,
                               "V3 interruption successor is inconsistent");
        }
        InterruptedClosureV2 closure;
        closure.record_count = records_.size() + 1;
        record.successor.kind = SuccessorKind::Interrupted;
        next_closure = std::move(closure);
    } else {
        const auto* failure = std::get_if<environment::EpisodeFailure>(&accepted.next);
        if (failure == nullptr ||
            failure->contract_id != environment::kEpisodicEnvironmentV3ContractId ||
            (failure->episode_semantic_id.has_value() &&
             *failure->episode_semantic_id != manifest_.episode_semantic_id) ||
            failure->semantic_action_count != records_.size() + 1 ||
            failure->last_public_semantic_decision_id !=
                std::optional<std::string>{current.public_semantic_decision_id}) {
            return fail_closed(environment::FailureCode::InvalidAuthoritativeState,
                               environment::FailureStage::Advance, true, error,
                               "V3 failure successor is inconsistent");
        }
        FailedClosureV2 closure;
        closure.failure_code = failure->failure_code;
        closure.failure_stage = failure->failure_stage;
        closure.mutation_may_have_occurred = failure->mutation_may_have_occurred;
        closure.record_count = records_.size() + 1;
        record.successor.kind = SuccessorKind::Failed;
        next_closure = std::move(closure);
    }

    try {
        (void)canonical_collection_decision_record_bytes_v2(record);
        if (next_closure.has_value()) {
            (void)canonical_episode_closure_bytes_v2(*next_closure);
        }
    } catch (const std::exception& exception) {
        return fail_closed(environment::FailureCode::PublicFrameInvariant,
                           environment::FailureStage::Projection, true, error,
                           exception.what());
    }
    records_.push_back(std::move(record));
    if (next_frame.has_value()) {
        current_frame_ = std::move(next_frame);
        lifecycle_ = RecorderLifecycle::AwaitingAction;
    } else {
        current_frame_.reset();
        closure_ = std::move(next_closure);
        lifecycle_ = RecorderLifecycle::Closed;
    }
    return true;
}

bool TrajectoryRecorderV2::on_step_rejected(
    const environment::StepRejected& rejected,
    const bool policy_origin,
    std::string* error) {
    if (lifecycle_ != RecorderLifecycle::AwaitingAction || !current_frame_.has_value()) {
        return set_error(error, "V2 recorder rejection is outside an awaiting frame");
    }
    if (rejected.contract_id != environment::kEpisodicEnvironmentV3ContractId ||
        rejected.current_episode_semantic_id != current_frame_->episode_semantic_id ||
        rejected.current_public_semantic_decision_id !=
            current_frame_->public_semantic_decision_id ||
        rejected.current_public_candidate_domain_digest !=
            current_frame_->public_candidate_domain_digest) {
        return fail_closed(environment::FailureCode::PublicFrameInvariant,
                           environment::FailureStage::Action, false, error,
                           "V3 rejection current-frame evidence does not match recorder");
    }
    if (!rejected.authoritative_state_unchanged) {
        return set_error(error, "StepRejected violates the V3 unchanged-state invariant");
    }
    if (policy_origin) {
        if (manifest_.collection_disposition.kind == CollectionDispositionKind::Clean) {
            manifest_.collection_disposition.kind =
                CollectionDispositionKind::QuarantinedAfterPolicyRejection;
        }
        manifest_.collection_disposition.policy_rejections.push_back(
            rejected.rejection_code);
    }
    return true;
}

bool TrajectoryRecorderV2::on_interrupt_accepted(
    const std::optional<environment::DecisionFrame>& pending_frame,
    const environment::InterruptAccepted& accepted,
    std::string* error) {
    if (lifecycle_ != RecorderLifecycle::AwaitingAction ||
        !current_frame_.has_value() || !pending_frame.has_value()) {
        return set_error(error, "V2 interruption lacks its pending frame");
    }
    const auto& interruption = accepted.interruption;
    if (interruption.contract_id != environment::kEpisodicEnvironmentV3ContractId ||
        interruption.episode_semantic_id != manifest_.episode_semantic_id ||
        interruption.reason != environment::InterruptionReason::AdministrativeCancel ||
        interruption.semantic_action_count != records_.size() ||
        interruption.last_decision_index !=
            std::optional<std::uint64_t>{current_frame_->decision_index} ||
        interruption.last_public_semantic_decision_id !=
            std::optional<std::string>{current_frame_->public_semantic_decision_id}) {
        return fail_closed(environment::FailureCode::PublicFrameInvariant,
                           environment::FailureStage::Interruption, true, error,
                           "V3 interruption metadata is inconsistent");
    }
    std::optional<PublicFrameSnapshotV2> pending;
    if (!capture_frame(*pending_frame, pending, records_.size(), error) ||
        !pending.has_value() ||
        canonical_public_frame_snapshot_bytes_v2(*pending) !=
            canonical_public_frame_snapshot_bytes_v2(*current_frame_)) {
        return fail_closed(environment::FailureCode::PublicFrameInvariant,
                           environment::FailureStage::Interruption, true, error,
                           "V3 pending frame does not match current frame");
    }
    InterruptedClosureV2 closure;
    closure.record_count = records_.size();
    closure.pending_unacted_frame = std::move(*pending);
    if (!records_.empty()) {
        NextFrameTarget target;
        target.kind = NextFrameTargetKind::InterruptionPendingUnactedFrame;
        target.next_decision_index = closure.pending_unacted_frame->decision_index;
        target.next_public_semantic_decision_id =
            closure.pending_unacted_frame->public_semantic_decision_id;
        records_.back().successor.kind = SuccessorKind::NextFrame;
        records_.back().successor.next_frame = std::move(target);
    }
    try {
        (void)canonical_episode_closure_bytes_v2(closure);
        if (!records_.empty()) {
            (void)canonical_collection_decision_record_bytes_v2(records_.back());
        }
    } catch (const std::exception& exception) {
        return fail_closed(environment::FailureCode::PublicFrameInvariant,
                           environment::FailureStage::Interruption, true, error,
                           exception.what());
    }
    closure_ = std::move(closure);
    current_frame_.reset();
    lifecycle_ = RecorderLifecycle::Closed;
    return true;
}

bool TrajectoryRecorderV2::on_failure(
    const environment::EpisodeFailure& failure,
    std::string* error) {
    if (lifecycle_ != RecorderLifecycle::AwaitingAction || !current_frame_.has_value()) {
        return set_error(error, "V2 recorder failure is outside an awaiting frame");
    }
    if (!capture_failure(failure, error)) {
        return fail_closed(environment::FailureCode::InvalidAuthoritativeState,
                           environment::FailureStage::Advance, true, error,
                           error != nullptr ? *error : "invalid V3 failure closure");
    }
    current_frame_.reset();
    lifecycle_ = RecorderLifecycle::Closed;
    return true;
}

std::optional<EpisodeEnvelopeV2> TrajectoryRecorderV2::seal(std::string* error) const {
    if (!closure_.has_value()) {
        if (error != nullptr) {
            *error = "V2 recorder has no closure";
        }
        return std::nullopt;
    }
    try {
        EpisodeEnvelopeV2 envelope;
        envelope.manifest = manifest_;
        envelope.records = records_;
        envelope.closure = *closure_;
        (void)canonical_episode_envelope_bytes_v2(envelope);
        return envelope;
    } catch (const std::exception& exception) {
        if (error != nullptr) {
            *error = exception.what();
        }
        return std::nullopt;
    } catch (...) {
        if (error != nullptr) {
            *error = "sealing V2 trajectory recorder threw";
        }
        return std::nullopt;
    }
}

}  // namespace ygo::trajectory
