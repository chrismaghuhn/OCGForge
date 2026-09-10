#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "ygo/diagnostics/task7_observer.hpp"

namespace ygo::diagnostics {

// This analysis is diagnostic-only. Its cycle key intentionally excludes
// monotonic decision/control/history identities so that repeated current
// public states can be distinguished from repeated event numbering.
// Cycle candidates are selected by fully repeated coverage, then repeat
// count, shortest period, and earliest start. A trailing partial repeat is
// never included in the reported cycle.
struct Task7LocalizationCycleResult final {
    bool found = false;
    std::size_t start_index = 0;
    std::size_t end_index = 0;
    std::size_t cycle_length = 0;
    std::size_t repeat_count = 0;
};

struct Task7LocalizationAnalysis final {
    std::uint64_t total_decisions = 0;
    std::uint64_t total_engine_processes = 0;
    std::uint64_t total_semantic_actions = 0;
    std::uint64_t total_turns_observed = 0;

    std::uint64_t distinct_public_observation_digests = 0;
    std::uint64_t distinct_public_semantic_decision_ids = 0;
    std::uint64_t distinct_current_public_state_fingerprints = 0;
    std::uint64_t distinct_selected_public_action_keys = 0;

    std::uint64_t longest_identical_public_observation_streak = 0;
    std::uint64_t longest_identical_current_public_state_streak = 0;
    std::uint64_t public_observation_changed_count = 0;
    std::uint64_t public_observation_unchanged_count = 0;
    std::uint64_t current_public_state_changed_count = 0;
    std::uint64_t current_public_state_unchanged_count = 0;
    std::uint64_t longest_same_turn_phase_streak = 0;
    std::uint64_t longest_selected_action_streak = 0;

    std::uint64_t max_decisions_without_structural_progress = 0;
    std::uint64_t max_engine_processes_without_structural_progress = 0;
    std::uint64_t max_structural_gap_start_decision = 0;
    std::uint64_t max_structural_gap_end_decision = 0;

    std::uint64_t f4_longest_consecutive_streak = 0;
    std::uint64_t f4_to_f4_transitions = 0;
    std::uint64_t f0_to_f4_transitions = 0;
    std::uint64_t f4_to_f0_transitions = 0;
    std::uint64_t f4_with_unchanged_public_observation_count = 0;
    std::uint64_t f4_with_changed_public_observation_count = 0;
    std::uint64_t f4_with_unchanged_current_state_count = 0;
    std::uint64_t f4_with_changed_current_state_count = 0;

    std::map<std::string, std::uint64_t> fallback_level_counts;
    std::map<std::string, std::uint64_t> request_counts;
    std::map<std::string, std::uint64_t> selected_action_counts;
    std::map<std::string, std::uint64_t> fallback_transition_counts;
    std::map<std::string, std::uint64_t> request_transition_counts;
    std::map<std::string, std::uint64_t> turn_counts;

    Task7LocalizationCycleResult longest_repeated_cycle;
};

Task7LocalizationCycleResult find_task7_public_state_cycle(
    const std::vector<Task7DiagnosticEvent>& events,
    std::size_t max_period = 256);

Task7LocalizationAnalysis analyze_task7_localization(
    const std::vector<Task7DiagnosticEvent>& events);

std::vector<Task7DiagnosticEvent> extract_task7_diagnostic_tail(
    const std::vector<Task7DiagnosticEvent>& events, std::size_t limit);

}  // namespace ygo::diagnostics
