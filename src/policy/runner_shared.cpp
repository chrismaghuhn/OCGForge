#include "runner_shared.hpp"

namespace ygo::policy::detail {

std::optional<trajectory::TerminalViews> terminal_views_for_environment(
    const environment::EpisodicEnvironment& environment) {
    const auto player_zero = environment.perspective_terminal_view(0);
    const auto player_one = environment.perspective_terminal_view(1);
    if (!player_zero.has_value() || !player_one.has_value()) {
        return std::nullopt;
    }
    return trajectory::TerminalViews{*player_zero, *player_one};
}

trajectory::RestrictedReplayEvidence restricted_replay_evidence_for_interruption(
    const environment::EpisodeInterrupted& interruption) {
    trajectory::RestrictedReplayEvidence result;
    result.v2_contract_id = interruption.contract_id;
    result.episode_semantic_id = interruption.episode_semantic_id;
    result.interruption_reason = interruption.reason;
    result.engine_process_budget = interruption.run_control_evidence.engine_process_budget;
    result.semantic_action_budget = interruption.run_control_evidence.semantic_action_budget;
    result.observed_engine_process_count =
        interruption.run_control_evidence.engine_process_count;
    result.observed_semantic_action_count =
        interruption.run_control_evidence.semantic_action_count;
    result.final_engine_step_index = interruption.final_engine_step_index;
    return result;
}

trajectory::PolicyRngDecisionProvenance make_policy_rng_attribution(
    const environment::DecisionFrame& frame,
    const PolicyExecutionBinding& binding,
    const PolicySelectionResult& selection) noexcept {
    trajectory::PolicyRngDecisionProvenance result;
    result.decision_index = frame.decision_index;
    result.acting_policy_assignment_id = binding.participant_policy_assignment_id;
    if (!selection.rng_cursor.has_value()) {
        result.policy_rng_identity = trajectory::kNoPolicyRngContractId;
        result.policy_rng_contract_identity = trajectory::kNoPolicyRngContractId;
        result.policy_rng_stream_id = trajectory::kNoPolicyRngContractId;
        result.policy_rng_initialization_identity = trajectory::kNoPolicyRngContractId;
        result.mode = trajectory::PolicyRngMode::None;
        return result;
    }
    result.policy_rng_identity = binding.policy_rng_identity;
    result.policy_rng_contract_identity = binding.policy_rng_contract_identity;
    result.policy_rng_stream_id = binding.policy_rng_stream_id;
    result.policy_rng_initialization_identity =
        binding.policy_rng_initialization_identity;
    result.mode = trajectory::PolicyRngMode::Cursor;
    result.pre_cursor = selection.rng_cursor->pre_cursor;
    result.post_cursor = selection.rng_cursor->post_cursor;
    return result;
}

}  // namespace ygo::policy::detail
