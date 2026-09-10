#include "ygo/policy/teacher_runner_v3_trajectory.hpp"

#include <algorithm>
#include <chrono>
#include <exception>
#include <limits>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "runner_shared.hpp"
#include "ygo/environment/public_action_identity.hpp"
#include "ygo/environment/public_safe_state.hpp"
#include "ygo/policy/production_provenance.hpp"
#include "ygo/trace/sha256.hpp"
#include "ygo/trajectory/identity_resolver.hpp"

namespace ygo::policy {
namespace {

using DiagnosticClock = std::chrono::steady_clock;

constexpr std::uint64_t kHiitaDiagnosticWindowFirst = 220;
constexpr std::uint64_t kHiitaDiagnosticWindowLast = 240;

bool in_hiita_diagnostic_window(const std::uint64_t decision_index) noexcept {
    return decision_index >= kHiitaDiagnosticWindowFirst &&
           decision_index <= kHiitaDiagnosticWindowLast;
}

std::uint64_t diagnostic_elapsed_us(const DiagnosticClock::time_point start,
                                    const DiagnosticClock::time_point end) noexcept {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(end - start).count());
}

void emit_diagnostic(const diagnostics::Task7DiagnosticObserver& observer,
                     std::string_view phase, const std::uint64_t duration_us,
                     const std::uint64_t call_count = 1,
                     const std::uint64_t max_single_call_us = 0) noexcept {
    if (!observer) return;
    try {
        diagnostics::Task7DiagnosticEvent event;
        event.phase = std::string(phase);
        event.duration_us = duration_us;
        event.call_count = call_count;
        event.max_single_call_us = max_single_call_us;
        observer(event);
    } catch (...) {
        // Diagnostics are strictly non-authoritative. Observer failures must
        // never affect the production trajectory path.
    }
}

void add_ranking_diagnostics(diagnostics::Task7DiagnosticEvent& event,
                             const teacher::TeacherRankingResultV2& ranking) {
    for (const auto& evaluation : ranking.evaluations) {
        switch (evaluation.status) {
        case teacher::CandidateEvaluationStatus::Supported:
            ++event.supported_evaluations;
            break;
        case teacher::CandidateEvaluationStatus::NotApplicable:
            ++event.not_applicable_evaluations;
            break;
        case teacher::CandidateEvaluationStatus::Unsupported:
            ++event.unsupported_evaluations;
            break;
        case teacher::CandidateEvaluationStatus::Invalid:
            ++event.invalid_evaluations;
            break;
        }
    }
    if (ranking.fallback_level.has_value()) {
        event.fallback_level = static_cast<std::uint8_t>(*ranking.fallback_level);
        switch (*ranking.fallback_level) {
        case teacher::TeacherFallbackLevel::F0:
            ++event.f0_count;
            break;
        case teacher::TeacherFallbackLevel::F1:
            ++event.f1_count;
            break;
        case teacher::TeacherFallbackLevel::F2:
            ++event.f2_count;
            break;
        case teacher::TeacherFallbackLevel::F3:
            ++event.f3_count;
            break;
        case teacher::TeacherFallbackLevel::F4:
            ++event.f4_count;
            break;
        }
    }
}

void add_public_frame_diagnostics(
    diagnostics::Task7DiagnosticEvent& event,
    const environment::DecisionFrame& frame,
    const std::optional<PolicySelectionResult>& selection,
    const bool include_detail = false) {
    event.decision_index = frame.decision_index;
    event.engine_step_index = frame.engine_step_index;
    event.engine_process_count = frame.engine_step_index + 1;
    event.semantic_action_count =
        frame.decision_index == (std::numeric_limits<std::uint64_t>::max)()
            ? frame.decision_index
            : frame.decision_index + 1;
    event.acting_player = frame.acting_player;
    event.candidate_count = frame.request.candidates.size();
    event.request_kind = std::string(
        environment::environment_decision_kind_name(frame.request.kind));
    event.decision_family = event.request_kind;
    event.public_observation_digest = frame.public_observation_digest;
    event.public_candidate_domain_digest = frame.public_candidate_domain_digest;
    event.public_semantic_decision_id = frame.public_semantic_decision_id;
    if (selection.has_value()) {
        event.selected_public_action_key = selection->public_action_key;
    }
    if (frame.request.continuation.has_value()) {
        event.continuation_present = true;
        event.continuation_kind = frame.request.continuation->continuation_kind;
        event.continuation_step = frame.request.continuation->continuation_step;
        event.continuation_selected_count =
            frame.request.continuation->selected_indices.size();
        event.continuation_remaining_count =
            frame.request.continuation->remaining_indices.size();
        event.continuation_min_count = frame.request.continuation->min_count;
        event.continuation_max_count = frame.request.continuation->max_count;
        event.continuation_can_finish = frame.request.continuation->can_finish;
        event.continuation_can_cancel = frame.request.continuation->can_cancel;
    }
    if (include_detail) {
        event.teacher_ranking_detail_present = true;
        event.teacher_candidate_public_action_keys.reserve(frame.request.candidates.size());
        for (const auto& candidate : frame.request.candidates) {
            event.teacher_candidate_public_action_keys.push_back(
                candidate.public_action_key);
        }
    }
    const auto safe = environment::decode_canonical_public_safe_state(
        frame.public_observation.canonical_safe_state_bytes());
    if (!safe) return;
    event.public_current_state_fingerprint = trace::sha256_bytes(
        environment::diagnostic_public_current_state_bytes(*safe.value));
    const auto& globals = safe.value->globals();
    if (globals.turn_count.has_value()) {
        event.public_turn_count_present = true;
        event.public_turn_count = *globals.turn_count;
    }
    if (globals.turn_player.has_value()) event.public_turn_player = *globals.turn_player;
    if (globals.phase.has_value()) event.public_phase = std::to_string(*globals.phase);
    if (globals.life_points.size() >= 2) {
        event.public_life_points_present = true;
        event.public_life_points_p0 = globals.life_points[0];
        event.public_life_points_p1 = globals.life_points[1];
    }
    event.public_entity_count = safe.value->entities().size();
    event.public_visible_event_count = safe.value->visible_events().size();
    event.public_chain_length = safe.value->chain().length;
}

void add_teacher_ranking_detail(
    diagnostics::Task7DiagnosticEvent& event,
    const environment::DecisionFrame& frame,
    const teacher::TeacherRankingDiagnosticsV2& ranking_diagnostics) {
    event.teacher_ranking_detail_present = true;
    event.teacher_ranking_status = static_cast<std::uint8_t>(ranking_diagnostics.status);
    event.teacher_effective_goal_id = ranking_diagnostics.effective_goal_id;
    event.teacher_effective_line_id = ranking_diagnostics.effective_line_id;
    event.teacher_ready_node_ids = ranking_diagnostics.ready_node_ids;
    event.teacher_native_unselect = ranking_diagnostics.native_unselect;
    event.teacher_reconciled_continuation_commitment =
        ranking_diagnostics.reconciled_continuation_commitment;
    event.teacher_f0_applicable = ranking_diagnostics.f0_applicable;
    event.teacher_f1_applicable = ranking_diagnostics.f1_applicable;
    event.teacher_selected_score_present = ranking_diagnostics.selected_score_vector.has_value();
    if (ranking_diagnostics.selected_score_vector.has_value()) {
        event.teacher_selected_score_values = ranking_diagnostics.selected_score_vector->values;
    }

    if (event.teacher_candidate_public_action_keys.empty()) {
        event.teacher_candidate_public_action_keys.reserve(frame.request.candidates.size());
        for (const auto& candidate : frame.request.candidates) {
            event.teacher_candidate_public_action_keys.push_back(
                candidate.public_action_key);
        }
    }
    event.teacher_candidate_evaluations.reserve(ranking_diagnostics.evaluations.size());
    for (const auto& evaluation : ranking_diagnostics.evaluations) {
        diagnostics::Task7DiagnosticCandidateEvaluation detail;
        detail.public_action_key = evaluation.public_action_key;
        detail.status = static_cast<std::uint8_t>(evaluation.status);
        if (evaluation.score.has_value()) {
            detail.score_present = true;
            detail.score_values = evaluation.score->values;
        }
        detail.score_contributions.reserve(evaluation.score_contributions.size());
        for (const auto& contribution : evaluation.score_contributions) {
            detail.score_contributions.push_back({
                static_cast<std::uint8_t>(contribution.dimension), contribution.value});
        }
        detail.matched_intent_ids = evaluation.matched_intent_ids;
        detail.matched_goal_ids = evaluation.matched_goal_ids;
        detail.matched_line_ids = evaluation.matched_line_ids;
        detail.matched_node_ids = evaluation.matched_node_ids;
        detail.reason_ids = evaluation.reason_ids;
        const auto candidate = std::find_if(
            frame.request.candidates.begin(), frame.request.candidates.end(),
            [&](const auto& value) {
                return value.public_action_key == evaluation.public_action_key;
            });
        if (candidate != frame.request.candidates.end()) {
            detail.action_kind = static_cast<std::uint8_t>(candidate->action_kind);
            detail.card_selection_operation = static_cast<std::uint8_t>(
                candidate->card_selection_operation);
            if (candidate->source_reference.has_value()) {
                detail.source_reference = candidate->source_reference->observation_locator;
            }
            if (candidate->target_reference.has_value()) {
                detail.target_reference = candidate->target_reference->observation_locator;
            }
            detail.continuation_operation = candidate->continuation_operation;
            detail.submits_engine_response = candidate->submits_engine_response;
        }
        event.teacher_candidate_evaluations.push_back(std::move(detail));
    }
}

using Boundary = std::variant<environment::DecisionFrame, environment::EpisodeTerminal,
                              environment::EpisodeInterrupted, environment::EpisodeFailure>;

const trajectory::ParticipantPolicyAssignment* assignment_for_player(
    const trajectory::PolicyProvenanceEnvelope& provenance,
    const std::uint8_t player,
    std::string& error) {
    const trajectory::ParticipantPolicyAssignment* result = nullptr;
    for (const auto& assignment : provenance.participant_assignments) {
        if (assignment.player != player) {
            continue;
        }
        if (result != nullptr) {
            error = "V3 trajectory runner requires one assignment per player";
            return nullptr;
        }
        result = &assignment;
    }
    if (result == nullptr) {
        error = "V3 trajectory runner lacks an assignment for a player";
    }
    return result;
}

bool session_matches_assignment(const TeacherPolicySessionV2& session,
                                const trajectory::ParticipantPolicyAssignment& assignment,
                                const std::uint8_t player,
                                std::string& error) {
    if (session.assignment.player != player ||
        session.assignment.participant_policy_assignment_id !=
            assignment.participant_policy_assignment_id ||
        session.assignment.policy_artifact_id != assignment.policy_artifact_id ||
        session.artifact.policy_artifact_id != assignment.policy_artifact_id ||
        session.policy.participant() != player ||
        session.policy.participant_policy_assignment_id() !=
            assignment.participant_policy_assignment_id) {
        error = "V3 trajectory session does not match policy provenance";
        return false;
    }
    return true;
}

trajectory::RestrictedReplayEvidenceV2 evidence_for_interruption(
    const environment::EpisodeInterrupted& interruption) {
    trajectory::RestrictedReplayEvidenceV2 result;
    result.episode_semantic_id = interruption.episode_semantic_id;
    result.interruption_reason = interruption.reason;
    result.engine_process_budget = interruption.run_control_evidence.engine_process_budget;
    result.semantic_action_budget = interruption.run_control_evidence.semantic_action_budget;
    result.observed_engine_process_count = interruption.run_control_evidence.engine_process_count;
    result.observed_semantic_action_count = interruption.run_control_evidence.semantic_action_count;
    result.final_engine_step_index = interruption.final_engine_step_index;
    return result;
}

TeacherRunnerV3TrajectoryRunResult failed_result(
    std::string message, std::optional<PolicyError> policy_error = std::nullopt) {
    TeacherRunnerV3TrajectoryRunResult result;
    result.error = std::move(policy_error);
    result.diagnostic = std::move(message);
    return result;
}

TeacherRunnerV3TrajectoryRunResult finalize_v2_collection(
    TeacherRunnerV3TrajectoryRunResult result,
    const TeacherRunnerV3TrajectoryConfig& config) {
    if (!result.envelope.has_value() || result.quarantined ||
        std::holds_alternative<trajectory::FailedClosureV2>(result.envelope->closure)) {
        return result;
    }
    try {
        const auto finalization_start = DiagnosticClock::now();
        const auto envelope_bytes = trajectory::canonical_episode_envelope_bytes_v2(
            *result.envelope);
        emit_diagnostic(config.diagnostic_observer, "EPISODE_FINALIZATION",
                        diagnostic_elapsed_us(finalization_start, DiagnosticClock::now()));
        const auto shard_start = DiagnosticClock::now();
        trajectory::CandidateTrajectoryShardV2 shard;
        shard.entries.push_back({trace::sha256_bytes(envelope_bytes), envelope_bytes});
        const auto shard_id = trajectory::candidate_shard_artifact_sha256_v2(shard);
        emit_diagnostic(config.diagnostic_observer, "SHARD_CONSTRUCTION",
                        diagnostic_elapsed_us(shard_start, DiagnosticClock::now()));

        const auto evidence_start = DiagnosticClock::now();
        trajectory::RestrictedCollectionEvidenceBundleV2 evidence;
        evidence.candidate_shard_artifact_sha256 = shard_id;
        if (std::holds_alternative<trajectory::InterruptedClosureV2>(result.envelope->closure)) {
            if (!result.replay_evidence.has_value()) {
                return failed_result("V2 interrupted collection lacks replay evidence");
            }
            evidence.interrupted_episodes.push_back({
                trace::sha256_bytes(envelope_bytes), *result.replay_evidence});
        }
        const auto evidence_id =
            trajectory::restricted_collection_evidence_artifact_sha256_v2(evidence);
        emit_diagnostic(config.diagnostic_observer, "REPLAY_OR_RESTRICTED_EVIDENCE",
                        diagnostic_elapsed_us(evidence_start, DiagnosticClock::now()));

        trajectory::replay_v2::ReplayOptions replay_options;
        if (std::holds_alternative<trajectory::TerminalClosureV2>(result.envelope->closure)) {
            replay_options.terminal_run_control = config.run_control;
        } else {
            replay_options.cancellation_source = config.run_control.cancellation.source;
        }
        std::string error;
        const auto admission_start = DiagnosticClock::now();
        const auto verification = trajectory::admission_v2::verify_collection_for_admission_v2(
            shard, evidence, shard_id, evidence_id, replay_options,
            make_production_policy_provenance_resolver(), &error);
        emit_diagnostic(config.diagnostic_observer, "ADMISSION_VERIFICATION",
                        diagnostic_elapsed_us(admission_start, DiagnosticClock::now()));
        if (!verification.has_value()) {
            return failed_result("V2 collection admission failed: " + error);
        }
        const auto receipt_start = DiagnosticClock::now();
        auto receipt = trajectory::issue_admission_receipt_v2(*verification, &error);
        emit_diagnostic(config.diagnostic_observer, "ADMISSION_RECEIPT",
                        diagnostic_elapsed_us(receipt_start, DiagnosticClock::now()));
        if (!receipt.has_value()) {
            return failed_result("V2 admission receipt issuance failed: " + error);
        }
        trajectory::DatasetManifestV2 manifest;
        for (const auto& entry : verification->entries()) {
            manifest.members.push_back({
                entry.trajectory_record_id,
                entry.public_gameplay_trajectory_id,
                trajectory::admission_receipt_id_v2(receipt->receipt()),
                shard_id,
                entry.episode_envelope_sha256});
        }
        std::sort(manifest.members.begin(), manifest.members.end(),
                  [](const auto& left, const auto& right) {
                      return left.trajectory_record_id < right.trajectory_record_id;
                  });
        std::vector<std::string> record_ids;
        record_ids.reserve(manifest.members.size());
        for (const auto& member : manifest.members) {
            record_ids.push_back(member.trajectory_record_id);
        }
        manifest.dataset_semantic_id = trajectory::dataset_v2::dataset_semantic_id_v2(record_ids);
        const auto manifest_start = DiagnosticClock::now();
        if (!trajectory::dataset_v2::validate_dataset_manifest_v2(
                manifest, std::vector<trajectory::VerifiedAdmissionReceiptV2>{*receipt},
                &error)) {
            return failed_result("V2 dataset manifest validation failed: " + error);
        }
        emit_diagnostic(config.diagnostic_observer, "DATASET_MANIFEST",
                        diagnostic_elapsed_us(manifest_start, DiagnosticClock::now()));
        result.candidate_shard = std::move(shard);
        result.restricted_collection_evidence = std::move(evidence);
        result.admission_verification = *verification;
        result.admission_receipt = std::move(*receipt);
        result.dataset_manifest = std::move(manifest);
        return result;
    } catch (const std::exception& exception) {
        return failed_result(exception.what());
    } catch (...) {
        return failed_result("V2 collection finalization threw");
    }
}

environment::DecisionFrame test_continuation_frame(
    const environment::DecisionFrame& current) {
    auto result = current;
    result.request.kind = environment::EnvironmentDecisionKind::UnselectCard;
    result.request.player = current.acting_player;
    result.request.continuation.reset();
    environment::EnvironmentContinuationView continuation;
    continuation.continuation_kind = "unordered";
    continuation.remaining_indices = {0};
    continuation.available_mask = 1;
    continuation.max_count = 1;
    continuation.can_finish = true;
    result.request.continuation = continuation;
    environment::EnvironmentActionCandidate finish;
    finish.action_kind = environment::EnvironmentActionKind::Finish;
    finish.continuation_operation = "finish";
    finish.submits_engine_response = true;
    environment::PublicActionKeyInput key;
    key.action_kind = "finish";
    key.continuation_operation = "finish";
    finish.public_action_key = environment::public_action_key_v2(key);
    result.request.candidates = {finish};
    std::vector<std::string> keys = {finish.public_action_key};
    result.public_candidate_domain_digest = environment::public_candidate_domain_digest_v2(
        "unselect_card", keys);
    environment::PublicSemanticDecisionIdentityInput identity;
    identity.episode_semantic_id = result.episode_semantic_id;
    identity.decision_index = result.decision_index;
    identity.acting_player = result.acting_player;
    identity.request_kind = "unselect_card";
    identity.public_observation_digest = result.public_observation_digest;
    identity.public_candidate_domain_digest = result.public_candidate_domain_digest;
    result.public_semantic_decision_id = environment::public_semantic_decision_id_v2(identity);
    return result;
}

trajectory::TerminalViews test_terminal_views(
    const environment::DecisionFrame& frame) {
    auto player_zero = frame.public_observation;
    auto player_one = frame.public_observation;
    player_zero.perspective_player = 0;
    player_zero.decision_context.player = std::uint8_t{0};
    player_one.perspective_player = 1;
    player_one.decision_context.player = std::uint8_t{1};
    return trajectory::TerminalViews{player_zero, player_one};
}

environment::EpisodeTerminal test_terminal(
    const environment::DecisionFrame& frame) {
    environment::EpisodeTerminal result;
    result.contract_id = std::string(environment::kEpisodicEnvironmentV3ContractId);
    result.episode_semantic_id = frame.episode_semantic_id;
    result.winner = 0;
    result.win_reason = 1;
    result.semantic_action_count = 1;
    result.last_decision_index = frame.decision_index;
    return result;
}

environment::EpisodeFailure test_failure(
    const environment::DecisionFrame& frame) {
    environment::EpisodeFailure result;
    result.contract_id = std::string(environment::kEpisodicEnvironmentV3ContractId);
    result.episode_semantic_id = frame.episode_semantic_id;
    result.failure_code = environment::FailureCode::CoreError;
    result.failure_stage = environment::FailureStage::Advance;
    result.semantic_action_count = 1;
    result.last_public_semantic_decision_id = frame.public_semantic_decision_id;
    return result;
}

environment::StepRejected test_rejection(
    const environment::DecisionFrame& frame) {
    environment::StepRejected result;
    result.contract_id = std::string(environment::kEpisodicEnvironmentV3ContractId);
    result.rejection_code = environment::RejectionCode::StaleSubmissionToken;
    result.current_episode_semantic_id = frame.episode_semantic_id;
    result.current_public_semantic_decision_id = frame.public_semantic_decision_id;
    result.current_public_candidate_domain_digest = frame.public_candidate_domain_digest;
    result.authoritative_state_unchanged = true;
    return result;
}

environment::EpisodeInterrupted test_interruption(
    const environment::DecisionFrame& frame,
    const environment::RunControl& control) {
    environment::EpisodeInterrupted result;
    result.contract_id = std::string(environment::kEpisodicEnvironmentV3ContractId);
    result.episode_semantic_id = frame.episode_semantic_id;
    result.reason = environment::InterruptionReason::SemanticActionBudget;
    result.semantic_action_count = 1;
    result.last_decision_index = frame.decision_index;
    result.last_public_semantic_decision_id = frame.public_semantic_decision_id;
    result.final_engine_step_index = frame.engine_step_index + 1;
    result.run_control_evidence.engine_process_budget = control.engine_process_budget;
    result.run_control_evidence.semantic_action_budget = control.semantic_action_budget;
    result.run_control_evidence.engine_process_count = frame.engine_step_index + 1;
    result.run_control_evidence.semantic_action_count = 1;
    return result;
}

}  // namespace

TeacherRunnerV3TrajectoryCreateResult TeacherRunnerV3TrajectoryRunner::create(
    TeacherRunnerV3TrajectoryConfig config) noexcept {
    try {
        if (config.environment_config.contract_id !=
                environment::kEpisodicEnvironmentV3ContractId ||
            config.episode_spec.contract_id != environment::kEpisodicEnvironmentV3ContractId ||
            !trajectory::is_current_certified_environment_v3(config.environment_config)) {
            return {std::nullopt,
                    PolicyError{PolicyErrorCode::InvalidConfiguration,
                                "V3 trajectory runner requires the current V3 environment"}};
        }
        const auto resolver = make_production_policy_provenance_resolver();
        std::string error;
        if (!resolver.validate(config.policy_provenance, config.environment_config,
                               config.episode_spec, &error)) {
            return {std::nullopt,
                    PolicyError{PolicyErrorCode::InvalidConfiguration, std::move(error)}};
        }
        for (std::uint8_t player = 0; player < 2; ++player) {
            if (!config.runner_config.sessions[player].has_value()) {
                return {std::nullopt,
                        PolicyError{PolicyErrorCode::InvalidConfiguration,
                                    "V3 trajectory runner lacks a session for a player"}};
            }
            const auto* assignment = assignment_for_player(
                config.policy_provenance, player, error);
            if (assignment == nullptr ||
                !session_matches_assignment(*config.runner_config.sessions[player], *assignment,
                                            player, error)) {
                return {std::nullopt,
                        PolicyError{PolicyErrorCode::InvalidConfiguration,
                                    error.empty() ? "V3 session/provenance mismatch" : error}};
            }
        }

        auto runner_result = TeacherRunnerV3::create(std::move(config.runner_config));
        if (!runner_result) {
            return {std::nullopt, runner_result.error};
        }
        auto factory = environment::EpisodicEnvironment::create(config.environment_config);
        auto* environment_value =
            std::get_if<std::unique_ptr<environment::EpisodicEnvironment>>(&factory);
        if (environment_value == nullptr || *environment_value == nullptr) {
            return {std::nullopt,
                    PolicyError{PolicyErrorCode::LifecycleFailure,
                                "V3 environment factory rejected the environment"}};
        }
        (*environment_value)->set_diagnostic_observer(config.diagnostic_observer);
        auto recorder = std::make_unique<trajectory::TrajectoryRecorderV2>(
            config.environment_config, config.episode_spec, config.policy_provenance, resolver);
        return {std::optional<TeacherRunnerV3TrajectoryRunner>(
                    TeacherRunnerV3TrajectoryRunner(std::move(config),
                                                    std::move(*runner_result.value),
                                                    std::move(*environment_value),
                                                    std::move(recorder))),
                std::nullopt};
    } catch (const std::exception& exception) {
        return {std::nullopt,
                PolicyError{PolicyErrorCode::InvalidConfiguration, exception.what()}};
    } catch (...) {
        return {std::nullopt,
                PolicyError{PolicyErrorCode::InvalidConfiguration,
                            "V3 trajectory runner construction threw"}};
    }
}

TeacherRunnerV3TrajectoryRunResult TeacherRunnerV3TrajectoryRunner::failure(
    std::string message, std::optional<PolicyError> policy_error) noexcept {
    return failed_result(std::move(message), std::move(policy_error));
}

TeacherRunnerV3TrajectoryRunResult TeacherRunnerV3TrajectoryRunner::run_impl(
    const std::optional<detail::TeacherRunnerV3TrajectoryTestScenario> test_scenario,
    const std::optional<std::uint64_t> decision_limit) noexcept {
    if (has_run_) {
        return failure("V3 trajectory runner can only execute one run");
    }
    has_run_ = true;
    try {
        const auto reset_start = DiagnosticClock::now();
        const auto reset = environment_->reset(config_.episode_spec, config_.run_control);
        emit_diagnostic(config_.diagnostic_observer, "ENVIRONMENT_RESET",
                        diagnostic_elapsed_us(reset_start, DiagnosticClock::now()));
        const auto* reset_accepted = std::get_if<environment::ResetAccepted>(&reset);
        if (reset_accepted == nullptr) {
            return failure("V3 reset was rejected");
        }
        Boundary boundary = reset_accepted->next;
        environment::ResetAccepted recording_reset = *reset_accepted;
        if (test_scenario.has_value() &&
            *test_scenario == detail::TeacherRunnerV3TrajectoryTestScenario::Continuation) {
            const auto* reset_frame = std::get_if<environment::DecisionFrame>(&boundary);
            if (reset_frame == nullptr) {
                return failure("V3 continuation test scenario lacks an initial frame");
            }
            const auto synthetic_frame = test_continuation_frame(*reset_frame);
            recording_reset.next = synthetic_frame;
            boundary = synthetic_frame;
        }
        std::optional<trajectory::TerminalViews> terminal_views;
        if (std::holds_alternative<environment::EpisodeTerminal>(boundary)) {
            terminal_views = detail::terminal_views_for_environment(*environment_);
            if (!terminal_views.has_value()) {
                return failure("V3 terminal reset lacks both public terminal views");
            }
        }
        std::string recorder_error;
        const auto finish = [&](TeacherRunnerV3TrajectoryRunResult result) {
            return test_scenario.has_value()
                       ? result
                       : finalize_v2_collection(std::move(result), config_);
        };
        const auto recorder_reset_start = DiagnosticClock::now();
        const bool recorded_reset = recorder_->on_reset_accepted(
            recording_reset, terminal_views, &recorder_error);
        emit_diagnostic(config_.diagnostic_observer, "TRAJECTORY_RECORDING",
                        diagnostic_elapsed_us(recorder_reset_start,
                                              DiagnosticClock::now()));
        if (!recorded_reset) {
            return failure("V2 recorder rejected V3 reset: " + recorder_error);
        }
        if (recorder_->lifecycle() == trajectory::RecorderLifecycle::Closed) {
            TeacherRunnerV3TrajectoryRunResult result;
            const auto seal_start = DiagnosticClock::now();
            result.envelope = recorder_->seal(&recorder_error);
            emit_diagnostic(config_.diagnostic_observer, "EPISODE_FINALIZATION",
                            diagnostic_elapsed_us(seal_start, DiagnosticClock::now()));
            result.quarantined = recorder_->manifest().collection_disposition.kind !=
                                 trajectory::CollectionDispositionKind::Clean;
            if (!result.envelope.has_value()) {
                return failure("V2 recorder could not seal reset boundary: " + recorder_error);
            }
            if (const auto* interrupted =
                    std::get_if<environment::EpisodeInterrupted>(&boundary)) {
                result.replay_evidence = evidence_for_interruption(*interrupted);
            }
            return finish(std::move(result));
        }

        for (;;) {
            const auto* frame = std::get_if<environment::DecisionFrame>(&boundary);
            if (frame == nullptr || frame->contract_id !=
                                         environment::kEpisodicEnvironmentV3ContractId ||
                frame->acting_player > 1 ||
                frame->public_observation.perspective_player != frame->acting_player ||
                !frame->submission_token.valid()) {
                return failure("V3 trajectory runner reached an invalid public frame");
            }
            if (test_scenario.has_value() &&
                *test_scenario == detail::TeacherRunnerV3TrajectoryTestScenario::Continuation &&
                recorder_->records().empty()) {
                const auto synthetic_frame = test_continuation_frame(*frame);
                const auto selection = runner_.select(synthetic_frame);
                if (!selection || selection.value->rng_cursor.has_value()) {
                    return failure("V3 continuation test scenario could not select a synthetic frame",
                                   selection.error);
                }
                const auto* session = runner_.session(synthetic_frame.acting_player);
                if (session == nullptr) {
                    return failure("V3 continuation test scenario lacks the acting session");
                }
                environment::StepAccepted accepted;
                accepted.transition.episode_semantic_id = synthetic_frame.episode_semantic_id;
                accepted.transition.public_semantic_decision_id =
                    synthetic_frame.public_semantic_decision_id;
                accepted.transition.decision_index = synthetic_frame.decision_index;
                accepted.transition.selected_public_action_key =
                    selection.value->public_action_key;
                accepted.transition.core_response_submitted = true;
                accepted.next = test_interruption(synthetic_frame, config_.run_control);
                if (!runner_.commit(accepted)) {
                    return failure("V3 continuation test scenario could not commit");
                }
                const auto attribution = detail::make_policy_rng_attribution(
                    synthetic_frame, session->execution_binding(), *selection.value);
                if (!recorder_->on_step_accepted(
                        accepted, attribution, std::nullopt, &recorder_error)) {
                    return failure("V2 recorder rejected synthetic continuation: " +
                                   recorder_error);
                }
                TeacherRunnerV3TrajectoryRunResult result;
                result.envelope = recorder_->seal(&recorder_error);
                result.replay_evidence = evidence_for_interruption(
                    std::get<environment::EpisodeInterrupted>(accepted.next));
                if (!result.envelope.has_value()) {
                    return failure("V2 recorder could not seal synthetic continuation: " +
                                   recorder_error);
                }
                return finish(std::move(result));
            }

            const auto teacher_start = DiagnosticClock::now();
            const bool capture_teacher_detail =
                config_.diagnostic_observer != nullptr &&
                in_hiita_diagnostic_window(frame->decision_index);
            teacher::TeacherRankingDiagnosticsV2 teacher_ranking_diagnostics;
            const auto selection = capture_teacher_detail
                                       ? runner_.select_with_diagnostics(
                                             *frame, teacher_ranking_diagnostics)
                                       : runner_.select(*frame);
            const auto teacher_elapsed = diagnostic_elapsed_us(
                teacher_start, DiagnosticClock::now());
            if (!selection) {
                emit_diagnostic(config_.diagnostic_observer, "TEACHER",
                                teacher_elapsed);
                return failure(
                    selection.error.has_value() ? selection.error->message
                                                : "V3 Teacher returned no selection",
                    selection.error);
            }
            if (selection.value->rng_cursor.has_value()) {
                return failure(
                    "V2 Teacher returned a policy RNG cursor",
                    PolicyError{PolicyErrorCode::InvalidConfiguration,
                                "V2 Teacher returned a policy RNG cursor"});
            }
            const auto* session = runner_.session(frame->acting_player);
            if (session == nullptr || selection.value->public_action_key.empty() ||
                !environment::is_public_action_key_v2(selection.value->public_action_key)) {
                return failure("V3 trajectory runner lacks the acting V2 session");
            }
            std::optional<diagnostics::Task7DiagnosticEvent> accepted_teacher_event;
            if (config_.diagnostic_observer) {
                diagnostics::Task7DiagnosticEvent event;
                event.phase = "TEACHER";
                event.duration_us = teacher_elapsed;
                add_public_frame_diagnostics(event, *frame, selection.value,
                                             capture_teacher_detail);
                if (const auto ranking = session->policy.pending_ranking_result();
                    ranking.has_value()) {
                    add_ranking_diagnostics(event, *ranking);
                }
                if (capture_teacher_detail) {
                    add_teacher_ranking_detail(event, *frame,
                                               teacher_ranking_diagnostics);
                }
                accepted_teacher_event = std::move(event);
            }
            const auto action = environment::ActionSelection{
                std::string(environment::kEpisodicEnvironmentV3ContractId),
                frame->episode_semantic_id, frame->public_semantic_decision_id,
                frame->submission_token, selection.value->public_action_key};
            const auto pre_rejection_frame = *frame;
            environment::StepResult stepped;
            if (test_scenario.has_value() && recorder_->records().empty() &&
                *test_scenario == detail::TeacherRunnerV3TrajectoryTestScenario::StepRejected) {
                stepped = test_rejection(*frame);
            } else if (test_scenario.has_value() && recorder_->records().empty() &&
                       *test_scenario == detail::TeacherRunnerV3TrajectoryTestScenario::Terminal) {
                environment::StepAccepted synthetic;
                synthetic.transition.episode_semantic_id = frame->episode_semantic_id;
                synthetic.transition.public_semantic_decision_id =
                    frame->public_semantic_decision_id;
                synthetic.transition.decision_index = frame->decision_index;
                synthetic.transition.selected_public_action_key =
                    selection.value->public_action_key;
                synthetic.transition.core_response_submitted = true;
                synthetic.next = test_terminal(*frame);
                stepped = std::move(synthetic);
            } else if (test_scenario.has_value() && recorder_->records().empty() &&
                       *test_scenario == detail::TeacherRunnerV3TrajectoryTestScenario::Failure) {
                environment::StepAccepted synthetic;
                synthetic.transition.episode_semantic_id = frame->episode_semantic_id;
                synthetic.transition.public_semantic_decision_id =
                    frame->public_semantic_decision_id;
                synthetic.transition.decision_index = frame->decision_index;
                synthetic.transition.selected_public_action_key =
                    selection.value->public_action_key;
                synthetic.transition.core_response_submitted = true;
                synthetic.next = test_failure(*frame);
                stepped = std::move(synthetic);
            } else {
                const auto environment_step_start = DiagnosticClock::now();
                stepped = environment_->step(action);
                emit_diagnostic(config_.diagnostic_observer, "ENVIRONMENT_STEP",
                                diagnostic_elapsed_us(environment_step_start,
                                                      DiagnosticClock::now()));
            }
            if (const auto* rejected = std::get_if<environment::StepRejected>(&stepped)) {
                emit_diagnostic(config_.diagnostic_observer, "STEP_REJECTED", 0);
                if (!runner_.reject_pending_proposal() ||
                    !recorder_->on_step_rejected(*rejected, true, &recorder_error)) {
                    return failure("V2 StepRejected handling failed: " + recorder_error);
                }
                const auto interrupted = environment_->interrupt(environment::InterruptRequest{
                    std::string(environment::kEpisodicEnvironmentV3ContractId),
                    environment::InterruptionReason::AdministrativeCancel});
                const auto* accepted_interrupt =
                    std::get_if<environment::InterruptAccepted>(&interrupted);
                if (accepted_interrupt == nullptr ||
                    !recorder_->on_interrupt_accepted(
                        std::optional<environment::DecisionFrame>{pre_rejection_frame},
                        *accepted_interrupt, &recorder_error)) {
                    return failure("V2 StepRejected quarantine could not close: " + recorder_error);
                }
                TeacherRunnerV3TrajectoryRunResult result;
                result.envelope = recorder_->seal(&recorder_error);
                result.replay_evidence = evidence_for_interruption(
                    accepted_interrupt->interruption);
                result.quarantined = true;
                if (!result.envelope.has_value()) {
                    return failure("V2 recorder could not seal StepRejected closure: " +
                                   recorder_error);
                }
                return finish(std::move(result));
            }

            const auto* accepted = std::get_if<environment::StepAccepted>(&stepped);
            const auto commit_start = DiagnosticClock::now();
            const bool committed = accepted != nullptr && runner_.commit(*accepted);
            emit_diagnostic(config_.diagnostic_observer, "TEACHER_COMMIT",
                            diagnostic_elapsed_us(commit_start, DiagnosticClock::now()));
            if (!committed) {
                return failure("V3 accepted step did not commit the pending V2 proposal");
            }
            terminal_views.reset();
            if (std::holds_alternative<environment::EpisodeTerminal>(accepted->next)) {
                if (test_scenario.has_value() && recorder_->records().empty() &&
                    *test_scenario == detail::TeacherRunnerV3TrajectoryTestScenario::Terminal) {
                    terminal_views = test_terminal_views(*frame);
                } else {
                    terminal_views = detail::terminal_views_for_environment(*environment_);
                }
                if (!terminal_views.has_value()) {
                    return failure("V3 terminal step lacks both public terminal views");
                }
            }
            const auto attribution = detail::make_policy_rng_attribution(
                *frame, session->execution_binding(), *selection.value);
            const auto recorder_start = DiagnosticClock::now();
            const bool recorded = recorder_->on_step_accepted(
                *accepted, attribution, terminal_views, &recorder_error);
            emit_diagnostic(config_.diagnostic_observer, "TRAJECTORY_RECORDING",
                            diagnostic_elapsed_us(recorder_start, DiagnosticClock::now()));
            if (!recorded) {
                return failure("V2 recorder rejected accepted V3 step: " + recorder_error);
            }
            if (accepted_teacher_event.has_value()) {
                try {
                    config_.diagnostic_observer(*accepted_teacher_event);
                } catch (...) {
                    // Diagnostics are strictly non-authoritative.
                }
            }
            if (decision_limit.has_value() &&
                std::holds_alternative<environment::DecisionFrame>(accepted->next) &&
                frame->decision_index + 1 >= *decision_limit) {
                const auto interrupted = environment_->interrupt(environment::InterruptRequest{
                    std::string(environment::kEpisodicEnvironmentV3ContractId),
                    environment::InterruptionReason::AdministrativeCancel});
                const auto* accepted_interrupt =
                    std::get_if<environment::InterruptAccepted>(&interrupted);
                const auto* pending_frame =
                    std::get_if<environment::DecisionFrame>(&accepted->next);
                if (accepted_interrupt == nullptr || pending_frame == nullptr ||
                    !recorder_->on_interrupt_accepted(
                        std::optional<environment::DecisionFrame>{*pending_frame},
                        *accepted_interrupt, &recorder_error)) {
                    return failure("V2 diagnostic prefix could not close: " + recorder_error);
                }
                TeacherRunnerV3TrajectoryRunResult result;
                result.envelope = recorder_->seal(&recorder_error);
                result.replay_evidence = evidence_for_interruption(
                    accepted_interrupt->interruption);
                if (!result.envelope.has_value()) {
                    return failure("V2 diagnostic prefix could not seal: " + recorder_error);
                }
                return finish(std::move(result));
            }
            if (const auto* interrupted =
                    std::get_if<environment::EpisodeInterrupted>(&accepted->next)) {
                if (recorder_->lifecycle() != trajectory::RecorderLifecycle::Closed) {
                    return failure("V2 recorder did not close the interrupted boundary");
                }
                TeacherRunnerV3TrajectoryRunResult result;
                result.envelope = recorder_->seal(&recorder_error);
                result.replay_evidence = evidence_for_interruption(*interrupted);
                if (!result.envelope.has_value()) {
                    return failure("V2 recorder could not seal interruption: " + recorder_error);
                }
                return finish(std::move(result));
            }
            if (recorder_->lifecycle() == trajectory::RecorderLifecycle::Closed) {
                TeacherRunnerV3TrajectoryRunResult result;
                result.envelope = recorder_->seal(&recorder_error);
                if (!result.envelope.has_value()) {
                    return failure("V2 recorder could not seal V3 closure: " + recorder_error);
                }
                return finish(std::move(result));
            }
            boundary = accepted->next;
        }
    } catch (const std::exception& exception) {
        return failure(exception.what());
    } catch (...) {
        return failure("V3 trajectory runner execution threw");
    }
}

TeacherRunnerV3TrajectoryRunResult TeacherRunnerV3TrajectoryRunner::run() noexcept {
    return run_impl(std::nullopt, std::nullopt);
}

}  // namespace ygo::policy
