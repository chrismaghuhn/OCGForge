#include "ygo/policy/teacher.hpp"

#include <algorithm>
#include <cstddef>
#include <exception>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "ygo/environment/public_action_identity.hpp"
#include "ygo/policy/production_provenance.hpp"
#include "ygo/teacher/candidate_evaluator.hpp"
#include "ygo/teacher/fallback_resolver.hpp"
#include "ygo/teacher/goal_line_controller.hpp"
#include "ygo/teacher/recovery_controller.hpp"
#include "ygo/teacher/teacher_decision.hpp"
#include "ygo/teacher/teacher_explanation_codec.hpp"
#include "ygo/trace/sha256.hpp"
#include "ygo/trajectory/codec.hpp"

namespace ygo::teacher {
namespace {

bool valid_candidate_domain(
    const std::vector<environment::EnvironmentActionCandidate>& candidates) noexcept {
    if (candidates.empty()) {
        return false;
    }
    for (std::size_t index = 0; index < candidates.size(); ++index) {
        if (!environment::is_public_action_key(candidates[index].public_action_key)) {
            return false;
        }
        for (std::size_t previous = 0; previous < index; ++previous) {
            if (candidates[previous].public_action_key == candidates[index].public_action_key) {
                return false;
            }
        }
    }
    return true;
}

TeacherRankingResult invalid_result(
    TeacherRankingStatus status,
    const std::vector<environment::EnvironmentActionCandidate>& candidates) {
    TeacherRankingResult result;
    result.status = status;
    result.evaluations.reserve(candidates.size());
    for (const auto& candidate : candidates) {
        CandidateEvaluation evaluation;
        evaluation.public_action_key = candidate.public_action_key;
        evaluation.status = CandidateEvaluationStatus::Invalid;
        result.evaluations.push_back(std::move(evaluation));
    }
    return result;
}

bool compose_score(const PublicEvaluatorOutcome& outcome,
                   ScoreVector& score) noexcept {
    for (const auto& contribution : outcome.contributions) {
        if (!add_score_contribution(score, contribution.dimension, contribution.value)) {
            return false;
        }
    }
    return true;
}

CandidateEvaluation evaluation_from_outcome(
    const environment::EnvironmentActionCandidate& candidate,
    const PublicEvaluatorOutcome& outcome,
    bool& valid) {
    CandidateEvaluation result;
    result.public_action_key = candidate.public_action_key;
    result.status = outcome.status;
    result.matched_intent_ids = outcome.matched_intent_ids;
    result.matched_goal_ids = outcome.matched_goal_ids;
    result.matched_line_ids = outcome.matched_line_ids;
    result.reason_ids = outcome.reason_ids;
    if (outcome.public_action_key != candidate.public_action_key) {
        valid = false;
        result.status = CandidateEvaluationStatus::Invalid;
        return result;
    }
    if (outcome.status == CandidateEvaluationStatus::Supported) {
        ScoreVector score;
        if (!compose_score(outcome, score)) {
            valid = false;
            result.status = CandidateEvaluationStatus::Invalid;
            return result;
        }
        result.score = score;
    } else if (!outcome.contributions.empty()) {
        valid = false;
        result.status = CandidateEvaluationStatus::Invalid;
    }
    return result;
}

TeacherFallbackCandidateValue stage_value_from_outcome(
    const environment::EnvironmentActionCandidate& candidate,
    const PublicEvaluatorOutcome& outcome,
    bool& valid) {
    TeacherFallbackCandidateValue result;
    result.public_action_key = candidate.public_action_key;
    result.status = outcome.status;
    result.matched_intent_ids = outcome.matched_intent_ids;
    result.matched_goal_ids = outcome.matched_goal_ids;
    result.matched_line_ids = outcome.matched_line_ids;
    result.reason_ids = outcome.reason_ids;
    result.contributions = outcome.contributions;
    if (outcome.public_action_key != candidate.public_action_key) {
        valid = false;
        result.status = CandidateEvaluationStatus::Invalid;
        result.contributions.clear();
        return result;
    }
    if (outcome.status == CandidateEvaluationStatus::Supported) {
        result.score = ScoreVector{};
        for (const auto& contribution : result.contributions) {
            if (!add_score_contribution(*result.score, contribution.dimension,
                                        contribution.value)) {
                valid = false;
                result.status = CandidateEvaluationStatus::Invalid;
                result.score.reset();
                result.contributions.clear();
                return result;
            }
        }
    } else if (!result.contributions.empty()) {
        valid = false;
        result.status = CandidateEvaluationStatus::Invalid;
        result.contributions.clear();
    }
    return result;
}

std::optional<PublicFactValue> current_chain_fact(const PublicFactSnapshot& facts) {
    return facts.value("public.chain.length");
}

}  // namespace

TeacherRankingResult TeacherCore::propose(
    const ygo::policy::PolicyInput& input,
    const StrategyProfileV1& profile,
    const EpisodeLocalStrategyStateV1& state) const {
    try {
        if (!valid_candidate_domain(input.candidates) ||
            !validate_strategy_profile(profile) ||
            !validate_strategy_state(state) ||
            state.strategy_profile_id != profile.profile_id ||
            input.observation.perspective_player > 1) {
            return invalid_result(TeacherRankingStatus::InvalidInput, input.candidates);
        }

        const auto facts_result = extract_public_fact_snapshot(input.observation);
        if (!facts_result.valid) {
            return invalid_result(TeacherRankingStatus::InvalidInput, input.candidates);
        }
        const auto owner = input.observation.perspective_player;
        const auto reconciled = reconcile_strategy_state_with_evidence(
            state, owner, input.observation);
        if (!reconciled.has_value()) {
            return invalid_result(TeacherRankingStatus::InvalidInput, input.candidates);
        }
        const auto selection = select_goal_and_line(
            profile, reconciled->state, facts_result.snapshot);
        const auto recovery = select_recovery_edge(profile, state, input.observation, owner);
        if (selection.status == PredicateEvaluationStatus::Invalid ||
            recovery.status == PredicateEvaluationStatus::Invalid) {
            return invalid_result(TeacherRankingStatus::InvalidInput, input.candidates);
        }

        std::vector<TeacherFallbackCandidateValue> stage;
        std::vector<CandidateEvaluation> evaluations;
        stage.reserve(input.candidates.size());
        evaluations.reserve(input.candidates.size());
        bool all_supported = true;
        bool valid = true;
        for (const auto& candidate : input.candidates) {
            CandidateFeatures features;
            if (!extract_candidate_features(candidate, facts_result.snapshot, features)) {
                valid = false;
                CandidateEvaluation evaluation;
                evaluation.public_action_key = candidate.public_action_key;
                evaluation.status = CandidateEvaluationStatus::Invalid;
                evaluations.push_back(std::move(evaluation));
                TeacherFallbackCandidateValue invalid_stage;
                invalid_stage.public_action_key = candidate.public_action_key;
                invalid_stage.status = CandidateEvaluationStatus::Invalid;
                stage.push_back(std::move(invalid_stage));
                continue;
            }
            const auto outcome = evaluate_goal_line_progress(
                profile, selection, recovery, candidate, input.observation, owner);
            auto evaluation = evaluation_from_outcome(candidate, outcome, valid);
            if (evaluation.status == CandidateEvaluationStatus::Invalid) {
                valid = false;
            }
            if (outcome.status != CandidateEvaluationStatus::Supported) {
                all_supported = false;
            }
            evaluations.push_back(std::move(evaluation));
            stage.push_back(stage_value_from_outcome(candidate, outcome, valid));
        }
        if (!valid) {
            TeacherRankingResult result;
            result.status = TeacherRankingStatus::InvalidInput;
            result.evaluations = std::move(evaluations);
            return result;
        }

        TeacherRankingResult result;
        if (all_supported) {
            TeacherFallbackStageSet stages;
            stages.stage_evaluations[0] = std::move(stage);
            result = resolve_teacher_fallback(input.candidates, stages);
        } else {
            // F4 is the only total stage that does not assert an unavailable
            // strategic meaning. It operates on the unchanged domain.
            result = resolve_teacher_fallback(input.candidates, TeacherFallbackStageSet{});
        }
        if (result.status != TeacherRankingStatus::Selected ||
            !result.selected_public_action_key.has_value()) {
            return result;
        }

        TeacherStateDeltaV1 requested;
        requested.strategy_profile_id = profile.profile_id;
        requested.base_last_accepted_decision_index = state.last_accepted_decision_index;
        requested.base_last_accepted_public_action_key =
            state.last_accepted_public_action_key;
        requested.proposed_for_public_action_key = *result.selected_public_action_key;
        if (recovery.status == PredicateEvaluationStatus::True &&
            recovery.target_goal_id.has_value()) {
            requested.active_goal_id = recovery.target_goal_id;
            requested.active_line_id = recovery.target_line_id;
        } else if (selection.status == PredicateEvaluationStatus::True) {
            requested.active_goal_id = selection.goal_id;
            requested.active_line_id = selection.line_id;
        }
        requested.achieved_goal_ids = reconciled->state.achieved_goal_ids;
        if (const auto chain_fact = current_chain_fact(facts_result.snapshot);
            chain_fact.has_value()) {
            requested.public_resource_facts.push_back(*chain_fact);
        }

        const auto delta = propose_teacher_state_delta(
            state, input.observation, owner, profile, requested);
        if (!delta.has_value()) {
            return invalid_result(TeacherRankingStatus::InvalidInput, input.candidates);
        }
        result.proposed_state_delta = *delta;
        std::string diagnostic;
        if (!validate_teacher_ranking_result(result, &diagnostic)) {
            return invalid_result(TeacherRankingStatus::InvalidInput, input.candidates);
        }
        return result;
    } catch (...) {
        return invalid_result(TeacherRankingStatus::InvalidInput, input.candidates);
    }
}

}  // namespace ygo::teacher

namespace ygo::policy {

teacher::TeacherPolicyBindingV1 make_teacher_policy_binding(
    const teacher::StrategyProfileV1& profile) {
    if (!teacher::validate_strategy_profile(profile)) {
        throw std::invalid_argument("Teacher binding requires a validated profile");
    }
    teacher::TeacherPolicyBindingV1 binding;
    binding.teacher_core_artifact_identity =
        std::string(kTeacherProducerImplementationIdentity);
    binding.strategy_profile_id = profile.profile_id;
    binding.score_contract_identity = std::string(teacher::kTeacherScoreContractId);
    binding.fallback_contract_identity = std::string(teacher::kTeacherFallbackContractId);
    binding.tie_break_contract_identity = std::string(teacher::kTeacherTieBreakContractId);
    binding.diagnostic_contract_identity =
        std::string(teacher::kTeacherDiagnosticContractId);
    binding.teacher_policy_binding_id = teacher::teacher_policy_binding_id(binding);
    return binding;
}

trajectory::PolicyArtifact make_teacher_policy_artifact(
    const teacher::StrategyProfileV1& profile) {
    const auto binding = make_teacher_policy_binding(profile);
    trajectory::PolicyArtifact artifact;
    artifact.policy_kind = trajectory::PolicyKind::DeterministicHeuristic;
    artifact.producer_implementation_identity =
        std::string(kTeacherProducerImplementationIdentity);
    artifact.inference_adapter_identity =
        std::string(kDirectExecutionInferenceAdapterIdentity);
    artifact.observation_adapter_identity =
        std::string(kPublicObservationAdapterIdentity);
    artifact.action_adapter_identity = std::string(kPublicActionKeyAdapterIdentity);
    artifact.sampling_contract_identity =
        std::string(kTeacherDeterministicSamplingContractIdentity);
    artifact.policy_rng_contract_identity = trajectory::kNoPolicyRngContractId;
    artifact.model_checkpoint_identity.reset();
    artifact.search_contract_identity.reset();
    artifact.demonstration_source_identity.reset();
    artifact.artifact_metadata_identity = binding.teacher_policy_binding_id;
    artifact.policy_artifact_id = trajectory::compute_policy_artifact_id(artifact);
    return artifact;
}

std::vector<trajectory::ParticipantPolicyAssignment>
make_teacher_participant_assignments(
    const trajectory::PolicyArtifact& swordsoul_artifact,
    const trajectory::PolicyArtifact& salamangreat_artifact,
    const environment::CertifiedEnvironmentConfig& config,
    const environment::SeatAssignment seat_assignment,
    const std::uint8_t starting_player,
    const std::array<trajectory::PolicyRole, 2>& policy_roles) {
    if (starting_player > 1 || config.locked_decks.size() != 2 ||
        (seat_assignment != environment::SeatAssignment::Normal &&
         seat_assignment != environment::SeatAssignment::Mirror)) {
        throw std::invalid_argument("invalid Teacher assignment configuration");
    }
    if (swordsoul_artifact.policy_kind != trajectory::PolicyKind::DeterministicHeuristic ||
        salamangreat_artifact.policy_kind != trajectory::PolicyKind::DeterministicHeuristic ||
        swordsoul_artifact.policy_artifact_id == salamangreat_artifact.policy_artifact_id) {
        throw std::invalid_argument("Teacher assignments require two distinct artifacts");
    }
    (void)trajectory::canonical_policy_artifact_bytes(swordsoul_artifact);
    (void)trajectory::canonical_policy_artifact_bytes(salamangreat_artifact);

    std::vector<trajectory::ParticipantPolicyAssignment> result;
    result.reserve(2);
    for (std::uint8_t player = 0; player < 2; ++player) {
        const auto deck_index = seat_assignment == environment::SeatAssignment::Mirror
                                    ? 1u - static_cast<unsigned int>(player)
                                    : static_cast<unsigned int>(player);
        const auto& artifact = deck_index == 0 ? swordsoul_artifact : salamangreat_artifact;
        trajectory::ParticipantPolicyAssignment assignment;
        assignment.player = player;
        assignment.seat_role = player == starting_player
                                    ? trajectory::SeatRole::StartingPlayer
                                    : trajectory::SeatRole::NonStartingPlayer;
        assignment.deck_role = deck_index == 0 ? trajectory::DeckRole::FirstLockedDeck
                                               : trajectory::DeckRole::SecondLockedDeck;
        assignment.resolved_locked_deck_id = config.locked_decks[deck_index].id;
        assignment.resolved_locked_deck_sha256 = config.locked_decks[deck_index].sha256;
        assignment.policy_role = policy_roles[player];
        assignment.policy_artifact_id = artifact.policy_artifact_id;
        assignment.assignment_epoch = 0;
        assignment.effective_from_decision_index = 0;
        assignment.league_context.reset();
        assignment.participant_policy_assignment_id =
            trajectory::compute_participant_policy_assignment_id(assignment);
        (void)trajectory::canonical_participant_policy_assignment_bytes(assignment);
        result.push_back(std::move(assignment));
    }
    std::sort(result.begin(), result.end(), [](const auto& left, const auto& right) {
        return left.participant_policy_assignment_id < right.participant_policy_assignment_id;
    });
    return result;
}

DeterministicTeacherPolicy::DeterministicTeacherPolicy(
    teacher::StrategyProfileV1 profile,
    teacher::TeacherPolicyBindingV1 policy_binding,
    const std::uint8_t participant,
    std::string participant_policy_assignment_id)
    : profile_(std::move(profile)),
      policy_binding_(std::move(policy_binding)),
      participant_(participant),
      participant_policy_assignment_id_(std::move(participant_policy_assignment_id)) {
    if (participant_ > 1 ||
        !teacher::validate_teacher_policy_binding(policy_binding_, profile_) ||
        !trajectory::is_canonical_identity(
            participant_policy_assignment_id_,
            "participant_policy_assignment.v1.")) {
        throw std::invalid_argument("invalid Teacher policy session identity");
    }
    const auto reset = teacher::reset_strategy_state(profile_);
    if (!reset.has_value()) {
        throw std::invalid_argument("Teacher policy state reset failed");
    }
    state_ = *reset;
}

PolicySelection DeterministicTeacherPolicy::failure(
    const PolicyErrorCode code, std::string message) noexcept {
    PolicySelection result;
    result.error = PolicyError{code, std::move(message)};
    return result;
}

PolicySelection DeterministicTeacherPolicy::select(const PolicyInput& input) noexcept {
    try {
        if (pending_.has_value()) {
            return failure(PolicyErrorCode::LifecycleFailure,
                           "Teacher has an unresolved pending proposal");
        }
        if (input.observation.perspective_player != participant_) {
            return failure(PolicyErrorCode::InvalidConfiguration,
                           "Teacher received an observation for another participant");
        }
        teacher::TeacherCore core;
        auto ranking = core.propose(input, profile_, state_);
        auto selection = teacher::teacher_policy_selection_from_result(ranking);
        if (!selection || !selection.value.has_value() ||
            !ranking.proposed_state_delta.has_value()) {
            return selection;
        }
        if (selection.value->rng_cursor.has_value()) {
            return failure(PolicyErrorCode::InvalidConfiguration,
                           "Teacher unexpectedly produced a policy RNG cursor");
        }
        pending_ = PendingProposal{input.observation, std::move(ranking), *selection.value};
        return selection;
    } catch (const std::exception& error) {
        return failure(PolicyErrorCode::InvalidConfiguration, error.what());
    } catch (...) {
        return failure(PolicyErrorCode::InvalidConfiguration,
                       "Teacher selection failed");
    }
}

bool DeterministicTeacherPolicy::commit(
    const environment::AcceptedActionTransition& accepted_transition) noexcept {
    if (!pending_.has_value()) {
        return false;
    }
    const auto pending = std::move(*pending_);
    pending_.reset();
    return teacher::commit_teacher_state_delta(
        state_, pending.ranking, profile_, participant_, pending.observation,
        accepted_transition);
}

void DeterministicTeacherPolicy::reject_pending_proposal() noexcept {
    pending_.reset();
}

TeacherPolicySessionCreateResult create_teacher_policy_session(
    const teacher::StrategyProfileV1& profile,
    const teacher::TeacherPolicyBindingV1& policy_binding,
    const trajectory::PolicyArtifact& artifact,
    const trajectory::ParticipantPolicyAssignment& assignment) noexcept {
    try {
        std::string diagnostic;
        if (!teacher::validate_teacher_policy_binding(policy_binding, profile, &diagnostic) ||
            artifact.policy_kind != trajectory::PolicyKind::DeterministicHeuristic ||
            artifact.producer_implementation_identity !=
                kTeacherProducerImplementationIdentity ||
            artifact.sampling_contract_identity !=
                kTeacherDeterministicSamplingContractIdentity ||
            artifact.policy_rng_contract_identity != trajectory::kNoPolicyRngContractId ||
            artifact.artifact_metadata_identity !=
                std::optional<std::string>{policy_binding.teacher_policy_binding_id} ||
            assignment.policy_artifact_id != artifact.policy_artifact_id ||
            assignment.player > 1 ||
            static_cast<std::uint8_t>(assignment.deck_role) != profile.own_deck_role ||
            assignment.resolved_locked_deck_id != profile.own_deck_id ||
            assignment.resolved_locked_deck_sha256 != profile.own_deck_sha256) {
            return {std::nullopt,
                    PolicyError{PolicyErrorCode::InvalidConfiguration,
                                diagnostic.empty() ? "invalid Teacher session binding"
                                                    : diagnostic}};
        }
        (void)trajectory::canonical_policy_artifact_bytes(artifact);
        (void)trajectory::canonical_participant_policy_assignment_bytes(assignment);
        TeacherPolicySession session{
            DeterministicTeacherPolicy(profile, policy_binding, assignment.player,
                                       assignment.participant_policy_assignment_id),
            artifact,
            assignment};
        return {std::optional<TeacherPolicySession>(std::move(session)), std::nullopt};
    } catch (const std::exception& error) {
        return {std::nullopt,
                PolicyError{PolicyErrorCode::InvalidConfiguration, error.what()}};
    } catch (...) {
        return {std::nullopt,
                PolicyError{PolicyErrorCode::InvalidConfiguration,
                            "Teacher policy session construction failed"}};
    }
}

}  // namespace ygo::policy
