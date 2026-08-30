#include "ygo/trajectory/recorder.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

#include "ygo/trajectory/identity_resolver.hpp"
#include "ygo/trajectory/policy_provenance.hpp"

namespace ygo::trajectory {
namespace {

bool same_optional_id(const std::optional<std::string>& actual,
                     const std::string& expected) noexcept {
    return !actual.has_value() || *actual == expected;
}

std::optional<std::string> assignment_for(
    const PolicyProvenanceEnvelope& provenance, const std::uint8_t player,
    const std::uint64_t decision_index) {
    std::optional<const ParticipantPolicyAssignment*> result;
    for (const auto& assignment : provenance.participant_assignments) {
        if (assignment.player != player ||
            assignment.effective_from_decision_index > decision_index) {
            continue;
        }
        if (result == std::nullopt ||
            assignment.effective_from_decision_index >
                (*result)->effective_from_decision_index) {
            result = &assignment;
        }
    }
    if (!result.has_value()) {
        return std::nullopt;
    }
    return (*result)->participant_policy_assignment_id;
}

const ParticipantPolicyAssignment* assignment_by_id(
    const PolicyProvenanceEnvelope& provenance, const std::string_view assignment_id) noexcept {
    const auto it = std::find_if(
        provenance.participant_assignments.begin(), provenance.participant_assignments.end(),
        [assignment_id](const ParticipantPolicyAssignment& assignment) {
            return assignment.participant_policy_assignment_id == assignment_id;
        });
    return it == provenance.participant_assignments.end() ? nullptr : &*it;
}

const PolicyArtifact* artifact_by_id(const PolicyProvenanceEnvelope& provenance,
                                     const std::string_view artifact_id) noexcept {
    const auto it = std::find_if(
        provenance.policy_artifacts.begin(), provenance.policy_artifacts.end(),
        [artifact_id](const PolicyArtifact& artifact) {
            return artifact.policy_artifact_id == artifact_id;
        });
    return it == provenance.policy_artifacts.end() ? nullptr : &*it;
}

bool validate_record_rng_attribution(const PolicyProvenanceEnvelope& provenance,
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
        if (!artifact_uses_rng || attribution.policy_rng_contract_identity !=
                                      artifact->policy_rng_contract_identity) {
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
        } else if (!descriptor->state_is_canonical || !attribution.pre_state.has_value() ||
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
    return views.player_0.perspective_player == 0 && views.player_1.perspective_player == 1;
}

}  // namespace

TrajectoryRecorder::TrajectoryRecorder(environment::CertifiedEnvironmentConfig config,
                                       environment::EpisodeSpec spec,
                                       PolicyProvenanceEnvelope policy_provenance)
    : TrajectoryRecorder(std::move(config), std::move(spec), std::move(policy_provenance),
                         ProvenanceResolver{}) {}

TrajectoryRecorder::TrajectoryRecorder(environment::CertifiedEnvironmentConfig config,
                                       environment::EpisodeSpec spec,
                                       PolicyProvenanceEnvelope policy_provenance,
                                       const ProvenanceResolver& resolver)
    : config_(std::move(config)), spec_(std::move(spec)), resolver_(resolver) {
    if (!is_current_certified_environment(config_)) {
        throw std::invalid_argument("trajectory recorder requires the current certified environment");
    }
    const auto expected_environment_id = environment::environment_semantic_id(config_);
    const auto expected_episode_id = environment::episode_semantic_id(config_, spec_);
    if (config_.environment_semantic_id != expected_environment_id) {
        throw std::invalid_argument("trajectory recorder environment identity mismatch");
    }
    manifest_.environment_semantic_id = expected_environment_id;
    manifest_.environment_identity_input = environment::canonical_environment_identity_bytes(config_);
    manifest_.episode_semantic_id = expected_episode_id;
    manifest_.episode_identity_input = environment::canonical_episode_identity_bytes(config_, spec_);
    manifest_.policy_provenance = std::move(policy_provenance);
    std::string error;
    if (!resolver.validate(manifest_.policy_provenance, config_, spec_, &error)) {
        throw std::invalid_argument("trajectory recorder provenance is invalid: " + error);
    }
    (void)canonical_episode_manifest_bytes(manifest_);
}

bool TrajectoryRecorder::set_error(std::string* error, std::string message) const {
    if (error != nullptr) {
        *error = std::move(message);
    }
    return false;
}

bool TrajectoryRecorder::fail_closed(environment::FailureCode code,
                                     environment::FailureStage stage,
                                     const bool mutation_may_have_occurred,
                                     std::string* error,
                                     std::string message) {
    FailedClosure closure;
    closure.failure_code = code;
    closure.failure_stage = stage;
    closure.mutation_may_have_occurred = mutation_may_have_occurred;
    closure.record_count = records_.size();
    closure_ = std::move(closure);
    current_frame_.reset();
    lifecycle_ = RecorderLifecycle::Closed;
    return set_error(error, std::move(message));
}

bool TrajectoryRecorder::capture_frame(const environment::DecisionFrame& frame,
                                       std::optional<PublicFrameSnapshot>& output,
                                       const std::uint64_t expected_decision_index,
                                       std::string* error) const {
    try {
        if (frame.contract_id != environment::kEpisodicEnvironmentV2ContractId ||
            frame.episode_semantic_id != manifest_.episode_semantic_id ||
            frame.decision_index != expected_decision_index) {
            return set_error(error, "V2 frame does not match recorder boundary");
        }
        PublicFrameSnapshot snapshot;
        snapshot.v2_contract_id = frame.contract_id;
        snapshot.episode_semantic_id = frame.episode_semantic_id;
        snapshot.public_semantic_decision_id = frame.public_semantic_decision_id;
        snapshot.decision_index = frame.decision_index;
        snapshot.acting_player = frame.acting_player;
        snapshot.public_observation = frame.public_observation;
        snapshot.public_observation_digest = frame.public_observation_digest;
        snapshot.request = frame.request;
        snapshot.public_candidate_domain_digest = frame.public_candidate_domain_digest;
        (void)canonical_public_frame_snapshot_bytes(snapshot);
        output = std::move(snapshot);
        return true;
    } catch (const std::exception& exception) {
        return set_error(error, exception.what());
    } catch (...) {
        return set_error(error, "capturing V2 public frame threw");
    }
}

bool TrajectoryRecorder::capture_terminal(const environment::EpisodeTerminal& terminal,
                                          const TerminalViews& views,
                                          std::string* error) {
    try {
        if (terminal.contract_id != environment::kEpisodicEnvironmentV2ContractId ||
            terminal.episode_semantic_id != manifest_.episode_semantic_id ||
            terminal.semantic_action_count != records_.size() ||
            !validate_terminal_views(views)) {
            return set_error(error, "V2 terminal does not match recorder boundary");
        }
        if (records_.empty() ? terminal.last_decision_index.has_value()
                             : !terminal.last_decision_index.has_value() ||
                                   *terminal.last_decision_index + 1 != records_.size()) {
            return set_error(error, "V2 terminal decision count is inconsistent");
        }
        TerminalClosure closure;
        closure.winner = terminal.winner;
        closure.win_reason = terminal.win_reason;
        closure.semantic_action_count = terminal.semantic_action_count;
        closure.last_decision_index = terminal.last_decision_index;
        closure.terminal_view_player_0 = views.player_0;
        closure.terminal_view_player_0_digest = environment::public_observation_digest(views.player_0);
        closure.terminal_view_player_1 = views.player_1;
        closure.terminal_view_player_1_digest = environment::public_observation_digest(views.player_1);
        (void)canonical_episode_closure_bytes(closure);
        closure_ = std::move(closure);
        return true;
    } catch (const std::exception& exception) {
        return set_error(error, exception.what());
    } catch (...) {
        return set_error(error, "capturing V2 terminal threw");
    }
}

bool TrajectoryRecorder::capture_interruption(const environment::EpisodeInterrupted& interruption,
                                              std::string* error) {
    if (interruption.contract_id != environment::kEpisodicEnvironmentV2ContractId ||
        interruption.episode_semantic_id != manifest_.episode_semantic_id ||
        interruption.semantic_action_count != records_.size()) {
        return set_error(error, "V2 interruption does not match recorder boundary");
    }
    const auto expected_last = records_.empty()
                                   ? std::optional<std::uint64_t>{}
                                   : std::optional<std::uint64_t>{records_.size() - 1};
    if (interruption.last_decision_index != expected_last ||
        interruption.last_public_semantic_decision_id.has_value() != !records_.empty() ||
        (interruption.last_public_semantic_decision_id.has_value() &&
         *interruption.last_public_semantic_decision_id !=
             records_.back().frame.public_semantic_decision_id)) {
        return set_error(error, "V2 interruption count or prefix is inconsistent");
    }
    InterruptedClosure closure;
    closure.record_count = records_.size();
    closure_ = std::move(closure);
    return true;
}

bool TrajectoryRecorder::capture_failure(const environment::EpisodeFailure& failure,
                                         std::string* error) {
    const auto expected_last_id = current_frame_.has_value()
                                      ? std::optional<std::string>{
                                            current_frame_->public_semantic_decision_id}
                                      : (records_.empty()
                                             ? std::optional<std::string>{}
                                             : std::optional<std::string>{
                                                   records_.back().frame.public_semantic_decision_id});
    if (failure.contract_id != environment::kEpisodicEnvironmentV2ContractId ||
        !same_optional_id(failure.episode_semantic_id, manifest_.episode_semantic_id) ||
        failure.semantic_action_count != records_.size() ||
        failure.last_public_semantic_decision_id != expected_last_id) {
        return set_error(error, "V2 failure does not match recorder boundary");
    }
    FailedClosure closure;
    closure.failure_code = failure.failure_code;
    closure.failure_stage = failure.failure_stage;
    closure.mutation_may_have_occurred = failure.mutation_may_have_occurred;
    closure.record_count = records_.size();
    try {
        (void)canonical_episode_closure_bytes(closure);
    } catch (const std::exception& exception) {
        return set_error(error, exception.what());
    }
    closure_ = std::move(closure);
    return true;
}

bool TrajectoryRecorder::on_reset_accepted(
    const environment::ResetAccepted& accepted,
    const std::optional<TerminalViews>& terminal_views,
    std::string* error) {
    if (lifecycle_ != RecorderLifecycle::Empty || !records_.empty() || closure_.has_value()) {
        return set_error(error, "recorder reset is not at EMPTY");
    }
    if (const auto* frame = std::get_if<environment::DecisionFrame>(&accepted.next)) {
        if (!capture_frame(*frame, current_frame_, records_.size(), error)) {
            return fail_closed(environment::FailureCode::PublicFrameInvariant,
                               environment::FailureStage::Projection, false, error,
                               error != nullptr ? *error : "invalid initial frame");
        }
        lifecycle_ = RecorderLifecycle::AwaitingAction;
        return true;
    }
    if (const auto* terminal = std::get_if<environment::EpisodeTerminal>(&accepted.next)) {
        if (!terminal_views.has_value()) {
            return fail_closed(environment::FailureCode::ObservationFailure,
                               environment::FailureStage::Projection, false, error,
                               "terminal views are required for a trusted terminal closure");
        }
        if (!capture_terminal(*terminal, *terminal_views, error)) {
            return fail_closed(environment::FailureCode::ObservationFailure,
                               environment::FailureStage::Projection, false, error,
                               error != nullptr ? *error : "invalid terminal closure");
        }
        lifecycle_ = RecorderLifecycle::Closed;
        return true;
    }
    if (const auto* interrupted = std::get_if<environment::EpisodeInterrupted>(&accepted.next)) {
        if (!capture_interruption(*interrupted, error)) {
            return fail_closed(environment::FailureCode::PublicFrameInvariant,
                               environment::FailureStage::Interruption, false, error,
                               error != nullptr ? *error : "invalid interruption closure");
        }
        lifecycle_ = RecorderLifecycle::Closed;
        return true;
    }
    const auto* failure = std::get_if<environment::EpisodeFailure>(&accepted.next);
    if (failure == nullptr || !capture_failure(*failure, error)) {
        return fail_closed(environment::FailureCode::InvalidAuthoritativeState,
                           environment::FailureStage::Construction, false, error,
                           error != nullptr ? *error : "invalid reset failure closure");
    }
    lifecycle_ = RecorderLifecycle::Closed;
    return true;
}

bool TrajectoryRecorder::on_step_accepted(
    const environment::StepAccepted& accepted,
    const PolicyRngDecisionProvenance& attribution,
    const std::optional<TerminalViews>& terminal_views,
    std::string* error) {
    if (lifecycle_ != RecorderLifecycle::AwaitingAction || !current_frame_.has_value()) {
        return set_error(error, "recorder accepted action outside an awaiting frame");
    }
    const auto current = *current_frame_;
    if (accepted.transition.episode_semantic_id != current.episode_semantic_id ||
        accepted.transition.public_semantic_decision_id != current.public_semantic_decision_id ||
        accepted.transition.decision_index != current.decision_index) {
        return fail_closed(environment::FailureCode::PublicFrameInvariant,
                           environment::FailureStage::Action, true, error,
                           "accepted transition does not match current frame");
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
                           "accepted transition selected an unknown public key");
    }
    TransitionClass transition_class;
    if (!current.request.continuation.has_value()) {
        if (!selected->submits_engine_response || !accepted.transition.core_response_submitted) {
            return fail_closed(environment::FailureCode::ResponseInconsistency,
                               environment::FailureStage::Advance, true, error,
                               "atomic transition response classification is inconsistent");
        }
        transition_class = TransitionClass::AtomicEngineResponse;
    } else if (selected->submits_engine_response) {
        if (!accepted.transition.core_response_submitted) {
            return fail_closed(environment::FailureCode::ResponseInconsistency,
                               environment::FailureStage::Advance, true, error,
                               "final continuation response was not submitted");
        }
        transition_class = TransitionClass::FinalContinuationResponse;
    } else {
        if (accepted.transition.core_response_submitted) {
            return fail_closed(environment::FailureCode::ResponseInconsistency,
                               environment::FailureStage::Advance, true, error,
                               "intermediate continuation submitted a response");
        }
        transition_class = TransitionClass::IntermediateContinuation;
    }
    const auto expected_assignment = assignment_for(
        manifest_.policy_provenance, current.acting_player, current.decision_index);
    if (!expected_assignment.has_value() ||
        attribution.acting_policy_assignment_id != *expected_assignment ||
        attribution.decision_index != current.decision_index) {
        return fail_closed(environment::FailureCode::PublicFrameInvariant,
                           environment::FailureStage::Action, true, error,
                           "policy attribution does not resolve to the acting assignment");
    }
    std::string attribution_error;
    if (!validate_record_rng_attribution(manifest_.policy_provenance, resolver_,
                                         *expected_assignment, attribution,
                                         attribution_error)) {
        return fail_closed(environment::FailureCode::PublicFrameInvariant,
                           environment::FailureStage::Action, true, error,
                           std::move(attribution_error));
    }
    try {
        (void)canonical_policy_rng_decision_provenance_bytes(attribution);
    } catch (const std::exception& exception) {
        return fail_closed(environment::FailureCode::PublicFrameInvariant,
                           environment::FailureStage::Action, true, error, exception.what());
    }

    DecisionRecord record;
    record.frame = current;
    record.selected_public_action_key = selected_key;
    record.transition_class = transition_class;
    record.acting_policy_assignment_id = *expected_assignment;
    record.policy_rng_decision_provenance = attribution;

    std::optional<PublicFrameSnapshot> next_frame;
    std::optional<EpisodeClosure> next_closure;
    if (const auto* frame = std::get_if<environment::DecisionFrame>(&accepted.next)) {
        if (!capture_frame(*frame, next_frame, records_.size() + 1, error) ||
            !next_frame.has_value()) {
            return fail_closed(environment::FailureCode::PublicFrameInvariant,
                               environment::FailureStage::Projection, true, error,
                               error != nullptr ? *error : "invalid successor frame");
        }
        NextFrameTarget target;
        target.kind = NextFrameTargetKind::NextDecisionRecord;
        target.next_decision_index = next_frame->decision_index;
        target.next_public_semantic_decision_id = next_frame->public_semantic_decision_id;
        record.successor.kind = SuccessorKind::NextFrame;
        record.successor.next_frame = std::move(target);
    } else if (const auto* terminal = std::get_if<environment::EpisodeTerminal>(&accepted.next)) {
        if (!terminal_views.has_value()) {
            return fail_closed(environment::FailureCode::ObservationFailure,
                               environment::FailureStage::Projection, true, error,
                               "terminal views are required for a trusted terminal closure");
        }
        TerminalClosure closure;
        if (terminal->contract_id != environment::kEpisodicEnvironmentV2ContractId ||
            terminal->episode_semantic_id != manifest_.episode_semantic_id ||
            terminal->semantic_action_count != records_.size() + 1 ||
            !validate_terminal_views(*terminal_views)) {
            return fail_closed(environment::FailureCode::PublicFrameInvariant,
                               environment::FailureStage::Projection, true, error,
                               "terminal successor does not match accepted action count");
        }
        if (!terminal->last_decision_index.has_value() ||
            *terminal->last_decision_index + 1 != records_.size() + 1) {
            return fail_closed(environment::FailureCode::PublicFrameInvariant,
                               environment::FailureStage::Projection, true, error,
                               "terminal successor index is inconsistent");
        }
        closure.winner = terminal->winner;
        closure.win_reason = terminal->win_reason;
        closure.semantic_action_count = terminal->semantic_action_count;
        closure.last_decision_index = terminal->last_decision_index;
        closure.terminal_view_player_0 = terminal_views->player_0;
        closure.terminal_view_player_0_digest = environment::public_observation_digest(
            closure.terminal_view_player_0);
        closure.terminal_view_player_1 = terminal_views->player_1;
        closure.terminal_view_player_1_digest = environment::public_observation_digest(
            closure.terminal_view_player_1);
        record.successor.kind = SuccessorKind::Terminal;
        next_closure = std::move(closure);
    } else if (const auto* interrupted = std::get_if<environment::EpisodeInterrupted>(&accepted.next)) {
        if (interrupted->contract_id != environment::kEpisodicEnvironmentV2ContractId ||
            interrupted->episode_semantic_id != manifest_.episode_semantic_id ||
            interrupted->semantic_action_count != records_.size() + 1 ||
            interrupted->last_decision_index !=
                std::optional<std::uint64_t>{current.decision_index} ||
            interrupted->last_public_semantic_decision_id !=
                std::optional<std::string>{current.public_semantic_decision_id}) {
            return fail_closed(environment::FailureCode::PublicFrameInvariant,
                               environment::FailureStage::Interruption, true, error,
                               "interruption successor count is inconsistent");
        }
        InterruptedClosure closure;
        closure.record_count = records_.size() + 1;
        record.successor.kind = SuccessorKind::Interrupted;
        next_closure = std::move(closure);
    } else {
        const auto* failure = std::get_if<environment::EpisodeFailure>(&accepted.next);
        if (failure == nullptr || failure->contract_id !=
                                      environment::kEpisodicEnvironmentV2ContractId ||
            !same_optional_id(failure->episode_semantic_id, manifest_.episode_semantic_id) ||
            failure->semantic_action_count != records_.size() + 1 ||
            failure->last_public_semantic_decision_id !=
                std::optional<std::string>{current.public_semantic_decision_id}) {
            return fail_closed(environment::FailureCode::InvalidAuthoritativeState,
                               environment::FailureStage::Advance, true, error,
                               "accepted action failure successor is inconsistent");
        }
        FailedClosure closure;
        closure.failure_code = failure->failure_code;
        closure.failure_stage = failure->failure_stage;
        closure.mutation_may_have_occurred = failure->mutation_may_have_occurred;
        closure.record_count = records_.size() + 1;
        record.successor.kind = SuccessorKind::Failed;
        next_closure = std::move(closure);
    }

    try {
        (void)canonical_collection_decision_record_bytes(record);
        if (next_closure.has_value()) {
            (void)canonical_episode_closure_bytes(*next_closure);
        }
    } catch (const std::exception& exception) {
        return fail_closed(environment::FailureCode::PublicFrameInvariant,
                           environment::FailureStage::Projection, true, error, exception.what());
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

bool TrajectoryRecorder::on_step_rejected(const environment::StepRejected& rejected,
                                          const bool policy_origin,
                                          std::string* error) {
    if (lifecycle_ != RecorderLifecycle::AwaitingAction || !current_frame_.has_value()) {
        return set_error(error, "recorder rejection is outside an awaiting frame");
    }
    if (rejected.contract_id != environment::kEpisodicEnvironmentV2ContractId ||
        rejected.current_episode_semantic_id != current_frame_->episode_semantic_id ||
        rejected.current_public_semantic_decision_id != current_frame_->public_semantic_decision_id ||
        rejected.current_public_candidate_domain_digest !=
            current_frame_->public_candidate_domain_digest) {
        return fail_closed(environment::FailureCode::PublicFrameInvariant,
                           environment::FailureStage::Action, false, error,
                           "rejection current-frame evidence does not match recorder");
    }
    if (!rejected.authoritative_state_unchanged) {
        return set_error(error, "StepRejected violates the V2 unchanged-state invariant");
    }
    if (policy_origin) {
        if (manifest_.collection_disposition.kind == CollectionDispositionKind::Clean) {
            manifest_.collection_disposition.kind =
                CollectionDispositionKind::QuarantinedAfterPolicyRejection;
        }
        manifest_.collection_disposition.policy_rejections.push_back(rejected.rejection_code);
    }
    return true;
}

bool TrajectoryRecorder::on_interrupt_accepted(
    const std::optional<environment::DecisionFrame>& pending_frame,
    const environment::InterruptAccepted& accepted,
    std::string* error) {
    if (lifecycle_ != RecorderLifecycle::AwaitingAction || !current_frame_.has_value() ||
        !pending_frame.has_value()) {
        return set_error(error, "administrative interruption lacks its pending frame");
    }
    const auto& interruption = accepted.interruption;
    if (interruption.contract_id != environment::kEpisodicEnvironmentV2ContractId ||
        interruption.episode_semantic_id != manifest_.episode_semantic_id ||
        interruption.reason != environment::InterruptionReason::AdministrativeCancel ||
        interruption.semantic_action_count != records_.size() ||
        interruption.last_decision_index !=
            std::optional<std::uint64_t>{current_frame_->decision_index} ||
        interruption.last_public_semantic_decision_id !=
            std::optional<std::string>{current_frame_->public_semantic_decision_id}) {
        return fail_closed(environment::FailureCode::PublicFrameInvariant,
                           environment::FailureStage::Interruption, true, error,
                           "administrative interruption metadata is inconsistent");
    }
    std::optional<PublicFrameSnapshot> pending;
    if (!capture_frame(*pending_frame, pending, records_.size(), error) || !pending.has_value() ||
        pending->decision_index != records_.size() ||
        canonical_public_frame_snapshot_bytes(*pending) !=
            canonical_public_frame_snapshot_bytes(*current_frame_)) {
        return fail_closed(environment::FailureCode::PublicFrameInvariant,
                           environment::FailureStage::Interruption, true, error,
                           "administrative pending frame does not match current frame");
    }
    InterruptedClosure closure;
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
        (void)canonical_episode_closure_bytes(closure);
        if (!records_.empty()) {
            (void)canonical_collection_decision_record_bytes(records_.back());
        }
    } catch (const std::exception& exception) {
        return fail_closed(environment::FailureCode::PublicFrameInvariant,
                           environment::FailureStage::Interruption, true, error, exception.what());
    }
    closure_ = std::move(closure);
    current_frame_.reset();
    lifecycle_ = RecorderLifecycle::Closed;
    return true;
}

bool TrajectoryRecorder::on_failure(const environment::EpisodeFailure& failure,
                                    std::string* error) {
    if (lifecycle_ != RecorderLifecycle::AwaitingAction || !current_frame_.has_value()) {
        return set_error(error, "recorder failure is outside an awaiting frame");
    }
    if (!capture_failure(failure, error)) {
        return fail_closed(environment::FailureCode::InvalidAuthoritativeState,
                           environment::FailureStage::Advance, true, error,
                           error != nullptr ? *error : "invalid failure closure");
    }
    current_frame_.reset();
    lifecycle_ = RecorderLifecycle::Closed;
    return true;
}

std::optional<EpisodeEnvelope> TrajectoryRecorder::seal(std::string* error) const {
    if (!closure_.has_value()) {
        if (error != nullptr) {
            *error = "recorder has no closure";
        }
        return std::nullopt;
    }
    try {
        EpisodeEnvelope envelope;
        envelope.manifest = manifest_;
        envelope.records = records_;
        envelope.closure = *closure_;
        (void)canonical_episode_envelope_bytes(envelope);
        return envelope;
    } catch (const std::exception& exception) {
        if (error != nullptr) {
            *error = exception.what();
        }
        return std::nullopt;
    } catch (...) {
        if (error != nullptr) {
            *error = "sealing trajectory recorder threw";
        }
        return std::nullopt;
    }
}

}  // namespace ygo::trajectory
