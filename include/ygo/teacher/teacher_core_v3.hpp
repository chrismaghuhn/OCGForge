#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "ygo/teacher/candidate_features.hpp"
#include "ygo/teacher/strategy_profile.hpp"
#include "ygo/teacher/strategy_state_v3.hpp"
#include "ygo/teacher/teacher_decision_v3.hpp"

namespace ygo::policy {
struct PolicyInput;
}

namespace ygo::teacher {

// Non-authoritative diagnostic data for a single V3 proposal. This type is
// never serialized into policy, trajectory, replay, or dataset contracts.
struct TeacherCandidateEvaluationDiagnosticsV3 final {
    std::string public_action_key;
    CandidateEvaluationStatus status = CandidateEvaluationStatus::Invalid;
    std::optional<ScoreVector> score;
    std::vector<EvaluatorScoreContribution> score_contributions;
    std::vector<std::string> matched_intent_ids;
    std::vector<std::string> matched_goal_ids;
    std::vector<std::string> matched_line_ids;
    std::vector<std::string> matched_node_ids;
    std::vector<std::string> reason_ids;
};

struct TeacherRankingDiagnosticsV3 final {
    TeacherRankingStatus status = TeacherRankingStatus::InvalidInput;
    std::optional<std::string> effective_goal_id;
    std::optional<std::string> effective_line_id;
    std::vector<std::string> ready_node_ids;
    bool native_unselect = false;
    bool reconciled_continuation_commitment = false;
    bool f0_applicable = false;
    bool f1_applicable = false;
    std::optional<TeacherFallbackLevel> fallback_level;
    std::optional<std::string> selected_public_action_key;
    std::optional<ScoreVector> selected_score_vector;
    std::vector<TeacherCandidateEvaluationDiagnosticsV3> evaluations;
};

class TeacherCoreV3 final {
public:
    TeacherCoreV3() = default;

    TeacherRankingResultV3 propose(
        const ygo::policy::PolicyInput& input,
        const StrategyProfileV1& profile,
        const EpisodeLocalStrategyStateV3& state,
        TeacherRankingDiagnosticsV3* diagnostics = nullptr) const;
};

}  // namespace ygo::teacher
