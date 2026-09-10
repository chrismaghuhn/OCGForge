#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace ygo::diagnostics {

// This schema is diagnostic-only. It is deliberately not consumed by any
// gameplay, trajectory, replay, admission, or dataset identity path.
inline constexpr char kTask7ForensicDiagnosticsSchemaId[] =
    "ocgforge.phase6.task7.forensic_performance_diagnostics.v1";

inline constexpr std::size_t kTask7DiagnosticScoreDimensionCount = 9;

struct Task7DiagnosticScoreContribution final {
    std::uint8_t dimension = 0;
    std::int32_t value = 0;
};

// This is a non-authoritative, public-safe copy of one V2 Teacher evaluation.
// It is populated only for the bounded trigger-characterization window.
struct Task7DiagnosticCandidateEvaluation final {
    std::string public_action_key;
    std::uint8_t action_kind = 255;
    std::uint8_t card_selection_operation = 0;
    std::string source_reference;
    std::string target_reference;
    std::string continuation_operation;
    bool submits_engine_response = true;
    std::uint8_t status = 255;
    bool score_present = false;
    std::array<std::int64_t, kTask7DiagnosticScoreDimensionCount> score_values{};
    std::vector<Task7DiagnosticScoreContribution> score_contributions;
    std::vector<std::string> matched_intent_ids;
    std::vector<std::string> matched_goal_ids;
    std::vector<std::string> matched_line_ids;
    std::vector<std::string> matched_node_ids;
    std::vector<std::string> reason_ids;
};

struct Task7DiagnosticEvent final {
    std::string phase;
    bool completed = true;
    std::uint64_t duration_us = 0;
    std::uint64_t call_count = 1;
    std::uint64_t max_single_call_us = 0;

    // Public/audit-safe workload counters only.
    std::uint64_t engine_process_count = 0;
    // For accepted TEACHER events this includes the selected action. The
    // event is emitted only after the corresponding environment step and
    // recorder commit succeed.
    std::uint64_t semantic_action_count = 0;
    std::uint64_t decision_index = 0;
    std::uint64_t engine_step_index = 0;
    std::uint8_t acting_player = 0;
    std::uint64_t candidate_count = 0;
    std::uint64_t candidate_total = 0;
    std::uint64_t candidate_max = 0;
    std::uint64_t observation_count = 0;
    std::uint64_t continuation_decision_count = 0;
    std::uint64_t continuation_action_count = 0;
    std::uint64_t step_accepted_count = 0;
    std::uint64_t step_rejected_count = 0;

    std::uint64_t supported_evaluations = 0;
    std::uint64_t not_applicable_evaluations = 0;
    std::uint64_t unsupported_evaluations = 0;
    std::uint64_t invalid_evaluations = 0;
    std::uint64_t f0_count = 0;
    std::uint64_t f1_count = 0;
    std::uint64_t f2_count = 0;
    std::uint64_t f3_count = 0;
    std::uint64_t f4_count = 0;

    std::string decision_family;
    std::string request_kind;
    std::string public_observation_digest;
    // Diagnostic-only current-state fingerprint. Unlike the observation
    // digest, this excludes decision/history identity and visible-event
    // history; it is never an authoritative gameplay identity.
    std::string public_current_state_fingerprint;
    std::string public_candidate_domain_digest;
    std::string public_semantic_decision_id;
    std::string selected_public_action_key;
    std::uint8_t fallback_level = 255;
    bool public_turn_count_present = false;
    std::uint32_t public_turn_count = 0;
    std::uint8_t public_turn_player = 255;
    std::string public_phase;
    bool public_life_points_present = false;
    std::uint64_t public_life_points_p0 = 0;
    std::uint64_t public_life_points_p1 = 0;
    std::uint64_t public_entity_count = 0;
    std::uint64_t public_visible_event_count = 0;
    std::uint64_t public_chain_length = 0;
    bool continuation_present = false;
    std::string continuation_kind;
    std::uint32_t continuation_step = 0;
    std::uint64_t continuation_selected_count = 0;
    std::uint64_t continuation_remaining_count = 0;
    std::uint32_t continuation_min_count = 0;
    std::uint32_t continuation_max_count = 0;
    bool continuation_can_finish = false;
    bool continuation_can_cancel = false;

    // Detailed Teacher ranking evidence is diagnostic-only and intentionally
    // absent outside the narrow trigger-characterization window.
    bool teacher_ranking_detail_present = false;
    std::uint8_t teacher_ranking_status = 255;
    std::optional<std::string> teacher_effective_goal_id;
    std::optional<std::string> teacher_effective_line_id;
    std::vector<std::string> teacher_ready_node_ids;
    bool teacher_native_unselect = false;
    bool teacher_reconciled_continuation_commitment = false;
    bool teacher_f0_applicable = false;
    bool teacher_f1_applicable = false;
    bool teacher_selected_score_present = false;
    std::array<std::int64_t, kTask7DiagnosticScoreDimensionCount>
        teacher_selected_score_values{};
    std::vector<std::string> teacher_candidate_public_action_keys;
    std::vector<Task7DiagnosticCandidateEvaluation> teacher_candidate_evaluations;
    std::string closure_kind;
    std::string failure_code;
    std::string failure_stage;
};

using Task7DiagnosticObserver = std::function<void(const Task7DiagnosticEvent&)>;

}  // namespace ygo::diagnostics
