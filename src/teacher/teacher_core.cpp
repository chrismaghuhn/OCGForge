#include "ygo/teacher/teacher_core.hpp"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "ygo/environment/episodic_environment.hpp"
#include "ygo/environment/public_action_identity.hpp"
#include "ygo/policy/policy.hpp"
#include "ygo/teacher/candidate_evaluator.hpp"
#include "ygo/teacher/candidate_features.hpp"
#include "ygo/teacher/fallback_resolver.hpp"
#include "ygo/teacher/goal_line_controller.hpp"
#include "ygo/teacher/recovery_controller.hpp"
#include "ygo/teacher/teacher_decision.hpp"

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
    const TeacherRankingStatus status,
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

struct StageBuildResult final {
    std::vector<TeacherFallbackCandidateValue> values;
    bool valid = true;
    bool total = true;
    bool matched = false;
};

StageBuildResult build_stage(
    const StrategyProfileV1& profile,
    const GoalLineSelection& selection,
    const RecoverySelection& recovery,
    const std::vector<environment::EnvironmentActionCandidate>& candidates,
    const PublicFactSnapshot& facts,
    const environment::PublicEnvironmentObservation& observation,
    const std::uint8_t owner) {
    StageBuildResult result;
    result.values.reserve(candidates.size());
    for (const auto& candidate : candidates) {
        CandidateFeatures features;
        if (!extract_candidate_features(candidate, facts, features)) {
            result.valid = false;
            TeacherFallbackCandidateValue invalid;
            invalid.public_action_key = candidate.public_action_key;
            invalid.status = CandidateEvaluationStatus::Invalid;
            result.values.push_back(std::move(invalid));
            continue;
        }
        const auto outcome = evaluate_goal_line_progress(
            profile, selection, recovery, candidate, observation, owner);
        bool value_valid = true;
        const auto value = stage_value_from_outcome(candidate, outcome, value_valid);
        result.valid = result.valid && value_valid;
        result.total = result.total &&
                       outcome.status == CandidateEvaluationStatus::Supported;
        result.matched = result.matched ||
                         (outcome.status == CandidateEvaluationStatus::Supported &&
                          std::any_of(outcome.contributions.begin(),
                                      outcome.contributions.end(), [](const auto& contribution) {
                                          return contribution.dimension ==
                                                     ScoreDimension::
                                                         ActiveGoalLineOrValidatedRecoveryProgress &&
                                                 contribution.value > 0;
                                      }));
        result.values.push_back(value);
    }
    return result;
}

const LineDefinition* find_line(const StrategyProfileV1& profile,
                                const std::string& id) noexcept {
    const auto it = std::find_if(profile.lines.begin(), profile.lines.end(),
                                 [&](const auto& line) { return line.line_id == id; });
    return it == profile.lines.end() ? nullptr : &*it;
}

const GoalDefinition* find_goal(const StrategyProfileV1& profile,
                                const std::string& id) noexcept {
    const auto it = std::find_if(profile.goals.begin(), profile.goals.end(),
                                 [&](const auto& goal) { return goal.goal_id == id; });
    return it == profile.goals.end() ? nullptr : &*it;
}

void append_sorted_unique(std::vector<std::string>& values, const std::string& value) {
    values.push_back(value);
    std::sort(values.begin(), values.end());
    values.erase(std::unique(values.begin(), values.end()), values.end());
}

void replace_current_fact(std::vector<PublicFactValue>& values,
                          const PublicFactValue& replacement) {
    values.erase(std::remove_if(values.begin(), values.end(),
                                [&](const auto& value) {
                                    return value.fact_id == replacement.fact_id;
                                }),
                 values.end());
    values.push_back(replacement);
    std::sort(values.begin(), values.end(), [](const auto& left, const auto& right) {
        return canonical_public_fact_value_bytes(left) <
               canonical_public_fact_value_bytes(right);
    });
}

std::optional<EpisodeLocalStrategyStateV1> apply_public_completion(
    const StrategyProfileV1& profile,
    const EpisodeLocalStrategyStateV1& reconciled_state,
    const PublicFactSnapshot& facts,
    const environment::PublicEnvironmentObservation& observation,
    const std::uint8_t owner) {
    auto result = reconciled_state;
    if (!result.last_accepted_decision_index.has_value() ||
        !result.last_accepted_public_action_key.has_value() ||
        !result.active_goal_id.has_value()) {
        return result;
    }

    environment::AcceptedActionTransition accepted;
    accepted.decision_index = *result.last_accepted_decision_index;
    accepted.selected_public_action_key = *result.last_accepted_public_action_key;
    const auto* goal = find_goal(profile, *result.active_goal_id);
    if (goal == nullptr) {
        return std::nullopt;
    }

    if (result.active_line_id.has_value()) {
        const auto* line = find_line(profile, *result.active_line_id);
        if (line == nullptr || line->goal_id != *result.active_goal_id) {
            return std::nullopt;
        }
        const auto line_selection = select_goal_and_line(profile, result, facts);
        if (line_selection.status == PredicateEvaluationStatus::Invalid) {
            return std::nullopt;
        }
        if (line_selection.status == PredicateEvaluationStatus::True &&
            line_selection.line_id == result.active_line_id) {
            for (const auto& node_id : line_selection.ready_node_ids) {
                if (std::binary_search(result.completed_line_node_ids.begin(),
                                       result.completed_line_node_ids.end(), node_id)) {
                    continue;
                }
                const auto node = std::find_if(
                    line->nodes.begin(), line->nodes.end(),
                    [&](const auto& value) { return value.node_id == node_id; });
                if (node == line->nodes.end()) {
                    return std::nullopt;
                }
                if (evaluate_node_completion(*node, accepted, observation, owner, profile) ==
                    PredicateEvaluationStatus::True) {
                    append_sorted_unique(result.completed_line_node_ids, node_id);
                }
            }
        }
    }

    if (evaluate_goal_completion(*goal, accepted, observation, owner, profile) ==
        PredicateEvaluationStatus::True) {
        append_sorted_unique(result.achieved_goal_ids, goal->goal_id);
        result.active_goal_id.reset();
        result.active_line_id.reset();
        result.completed_line_node_ids.clear();
    }
    return result;
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
        const auto completed = apply_public_completion(
            profile, reconciled->state, facts_result.snapshot, input.observation, owner);
        if (!completed.has_value()) {
            return invalid_result(TeacherRankingStatus::InvalidInput, input.candidates);
        }

        const auto selection = select_goal_and_line(profile, *completed, facts_result.snapshot);
        const auto recovery = select_recovery_edge(profile, state, input.observation, owner);
        if (selection.status == PredicateEvaluationStatus::Invalid ||
            recovery.status == PredicateEvaluationStatus::Invalid) {
            return invalid_result(TeacherRankingStatus::InvalidInput, input.candidates);
        }

        TeacherFallbackStageSet stages;
        const GoalLineSelection no_active_line;
        if (selection.status == PredicateEvaluationStatus::True &&
            selection.line_id.has_value()) {
            const auto active_stage = build_stage(
                profile, selection, RecoverySelection{}, input.candidates,
                facts_result.snapshot, input.observation, owner);
            if (!active_stage.valid) {
                return invalid_result(TeacherRankingStatus::InvalidInput, input.candidates);
            }
            if (active_stage.total && active_stage.matched) {
                stages.stage_evaluations[0] = active_stage.values;
            }
        }

        if (recovery.status == PredicateEvaluationStatus::True) {
            const auto recovery_stage = build_stage(
                profile, no_active_line, recovery, input.candidates,
                facts_result.snapshot, input.observation, owner);
            if (!recovery_stage.valid) {
                return invalid_result(TeacherRankingStatus::InvalidInput, input.candidates);
            }
            if (recovery_stage.total && recovery_stage.matched) {
                stages.stage_evaluations[1] = recovery_stage.values;
            }
        }

        const auto result_before_delta =
            resolve_teacher_fallback(input.candidates, stages);
        if (result_before_delta.status != TeacherRankingStatus::Selected ||
            !result_before_delta.selected_public_action_key.has_value()) {
            return result_before_delta;
        }
        auto result = result_before_delta;

        TeacherStateDeltaV1 requested;
        requested.strategy_profile_id = profile.profile_id;
        requested.base_last_accepted_decision_index = state.last_accepted_decision_index;
        requested.base_last_accepted_public_action_key =
            state.last_accepted_public_action_key;
        requested.proposed_for_public_action_key = *result.selected_public_action_key;
        requested.active_goal_id = completed->active_goal_id;
        requested.active_line_id = completed->active_line_id;
        requested.completed_line_node_ids = completed->completed_line_node_ids;
        requested.achieved_goal_ids = completed->achieved_goal_ids;
        requested.public_resource_facts = completed->public_resource_facts;
        requested.public_restriction_facts = completed->public_restriction_facts;
        requested.public_threat_facts = completed->public_threat_facts;

        if (const auto chain_fact = facts_result.snapshot.value("public.chain.length");
            chain_fact.has_value()) {
            replace_current_fact(requested.public_resource_facts, *chain_fact);
        }

        if (result.fallback_level == std::optional<TeacherFallbackLevel>{
                                    TeacherFallbackLevel::F1} &&
            recovery.status == PredicateEvaluationStatus::True &&
            recovery.target_goal_id.has_value()) {
            requested.active_goal_id = recovery.target_goal_id;
            requested.active_line_id = recovery.target_line_id;
            requested.completed_line_node_ids.clear();
        } else if (result.fallback_level == std::optional<TeacherFallbackLevel>{
                                        TeacherFallbackLevel::F0} &&
                   selection.status == PredicateEvaluationStatus::True) {
            if (requested.active_line_id != selection.line_id) {
                requested.completed_line_node_ids.clear();
            }
            requested.active_goal_id = selection.goal_id;
            requested.active_line_id = selection.line_id;
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
