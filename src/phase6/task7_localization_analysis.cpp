#include "ygo/diagnostics/task7_localization_analysis.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ygo::diagnostics {
namespace {

void append_key_field(std::string& key, const std::string_view value) {
    key += std::to_string(value.size());
    key.push_back(':');
    key.append(value.data(), value.size());
    key.push_back('|');
}

std::optional<std::string> current_cycle_key(const Task7DiagnosticEvent& event) {
    if (event.public_current_state_fingerprint.empty()) return std::nullopt;

    std::string key;
    key.reserve(128);
    append_key_field(key, std::to_string(event.acting_player));
    append_key_field(key, event.request_kind);
    append_key_field(key, event.public_current_state_fingerprint);
    append_key_field(key, event.public_candidate_domain_digest);
    append_key_field(key, event.selected_public_action_key);
    append_key_field(key, event.continuation_present ? "1" : "0");
    if (event.continuation_present) {
        append_key_field(key, event.continuation_kind);
        append_key_field(key, std::to_string(event.continuation_step));
    }
    return key;
}

bool blocks_equal(const std::vector<std::optional<std::string>>& keys,
                  const std::size_t left, const std::size_t right,
                  const std::size_t length) {
    for (std::size_t offset = 0; offset < length; ++offset) {
        if (!keys[left + offset].has_value() || !keys[right + offset].has_value() ||
            *keys[left + offset] != *keys[right + offset]) {
            return false;
        }
    }
    return true;
}

std::string coarse_structural_progress_key(const Task7DiagnosticEvent& event) {
    std::string key;
    append_key_field(key, event.public_turn_count_present ? "1" : "0");
    append_key_field(key, std::to_string(event.public_turn_count));
    append_key_field(key, std::to_string(event.public_turn_player));
    append_key_field(key, event.public_phase);
    append_key_field(key, event.public_life_points_present ? "1" : "0");
    append_key_field(key, std::to_string(event.public_life_points_p0));
    append_key_field(key, std::to_string(event.public_life_points_p1));
    append_key_field(key, std::to_string(event.public_entity_count));
    append_key_field(key, std::to_string(event.public_chain_length));
    return key;
}

bool current_state_equal(const Task7DiagnosticEvent& left,
                         const Task7DiagnosticEvent& right) {
    return !left.public_current_state_fingerprint.empty() &&
           !right.public_current_state_fingerprint.empty() &&
           left.public_current_state_fingerprint == right.public_current_state_fingerprint;
}

}  // namespace

Task7LocalizationCycleResult find_task7_public_state_cycle(
    const std::vector<Task7DiagnosticEvent>& events, const std::size_t max_period) {
    Task7LocalizationCycleResult best;
    if (events.size() < 2 || max_period == 0) return best;

    std::vector<std::optional<std::string>> keys;
    keys.reserve(events.size());
    for (const auto& event : events) keys.push_back(current_cycle_key(event));

    std::size_t best_coverage = 0;
    for (std::size_t period = 1;
         period <= max_period && period <= events.size() / 2;
         ++period) {
        for (std::size_t start = 0; start + period * 2 <= events.size(); ++start) {
            if (!blocks_equal(keys, start, start + period, period)) continue;

            std::size_t repeats = 2;
            while (start + (repeats + 1) * period <= events.size() &&
                   blocks_equal(keys, start + (repeats - 1) * period,
                                start + repeats * period, period)) {
                ++repeats;
            }

            const auto coverage = repeats * period;
            const bool better = !best.found || coverage > best_coverage ||
                                (coverage == best_coverage &&
                                 repeats > best.repeat_count) ||
                                (coverage == best_coverage &&
                                 repeats == best.repeat_count &&
                                 period < best.cycle_length) ||
                                (coverage == best_coverage &&
                                 repeats == best.repeat_count &&
                                 period == best.cycle_length &&
                                 start < best.start_index);
            if (!better) continue;

            best.found = true;
            best.start_index = start;
            best.end_index = start + coverage - 1;
            best.cycle_length = period;
            best.repeat_count = repeats;
            best_coverage = coverage;
        }
    }
    return best;
}

Task7LocalizationAnalysis analyze_task7_localization(
    const std::vector<Task7DiagnosticEvent>& events) {
    Task7LocalizationAnalysis result;
    result.total_decisions = events.size();
    if (events.empty()) return result;

    std::set<std::string> observations;
    std::set<std::string> public_decisions;
    std::set<std::string> current_states;
    std::set<std::string> selected_actions;
    std::set<std::uint32_t> distinct_turns;

    std::uint64_t same_observation_streak = 1;
    std::uint64_t same_current_state_streak =
        events.front().public_current_state_fingerprint.empty() ? 0 : 1;
    std::uint64_t same_turn_phase_streak = 1;
    std::uint64_t selected_action_streak = 1;
    std::uint64_t f4_streak = 0;
    std::uint64_t no_structural_progress = 0;
    std::size_t last_progress_index = 0;
    std::uint64_t last_progress_process = 0;

    result.longest_identical_public_observation_streak = 1;
    result.longest_identical_current_public_state_streak = same_current_state_streak;
    result.longest_same_turn_phase_streak = 1;
    result.longest_selected_action_streak = 1;

    for (std::size_t index = 0; index < events.size(); ++index) {
        const auto& event = events[index];
        result.total_engine_processes =
            (std::max)(result.total_engine_processes, event.engine_process_count);
        result.total_semantic_actions =
            (std::max)(result.total_semantic_actions, event.semantic_action_count);
        observations.insert(event.public_observation_digest);
        public_decisions.insert(event.public_semantic_decision_id);
        if (!event.public_current_state_fingerprint.empty()) {
            current_states.insert(event.public_current_state_fingerprint);
        }
        selected_actions.insert(event.selected_public_action_key);
        ++result.request_counts[event.request_kind];
        ++result.selected_action_counts[event.selected_public_action_key];
        if (event.fallback_level != 255) {
            ++result.fallback_level_counts[std::to_string(event.fallback_level)];
        }
        if (event.public_turn_count_present) {
            distinct_turns.insert(event.public_turn_count);
            ++result.turn_counts[std::to_string(event.public_turn_count)];
        }

        if (event.fallback_level == 4) {
            ++f4_streak;
            result.f4_longest_consecutive_streak =
                (std::max)(result.f4_longest_consecutive_streak, f4_streak);
        } else {
            f4_streak = 0;
        }

        if (index == 0) continue;
        const auto& previous = events[index - 1];

        if (previous.public_observation_digest == event.public_observation_digest) {
            ++same_observation_streak;
            ++result.public_observation_unchanged_count;
        } else {
            same_observation_streak = 1;
            ++result.public_observation_changed_count;
        }
        result.longest_identical_public_observation_streak =
            (std::max)(result.longest_identical_public_observation_streak,
                       same_observation_streak);

        if (current_state_equal(previous, event)) {
            ++same_current_state_streak;
            ++result.current_public_state_unchanged_count;
        } else {
            same_current_state_streak =
                event.public_current_state_fingerprint.empty() ? 0 : 1;
            if (!previous.public_current_state_fingerprint.empty() &&
                !event.public_current_state_fingerprint.empty()) {
                ++result.current_public_state_changed_count;
            }
        }
        result.longest_identical_current_public_state_streak =
            (std::max)(result.longest_identical_current_public_state_streak,
                       same_current_state_streak);

        if (previous.selected_public_action_key == event.selected_public_action_key) {
            ++selected_action_streak;
        } else {
            selected_action_streak = 1;
        }
        result.longest_selected_action_streak =
            (std::max)(result.longest_selected_action_streak, selected_action_streak);

        const auto previous_structural = coarse_structural_progress_key(previous);
        const auto current_structural = coarse_structural_progress_key(event);
        if (previous_structural == current_structural) {
            ++same_turn_phase_streak;
            ++no_structural_progress;
        } else {
            same_turn_phase_streak = 1;
            if (no_structural_progress > result.max_decisions_without_structural_progress) {
                result.max_decisions_without_structural_progress = no_structural_progress;
                result.max_structural_gap_start_decision =
                    events[last_progress_index].decision_index + 1;
                result.max_structural_gap_end_decision = events[index - 1].decision_index;
                result.max_engine_processes_without_structural_progress =
                    event.engine_process_count - last_progress_process;
            }
            no_structural_progress = 0;
            last_progress_index = index;
            last_progress_process = event.engine_process_count;
        }
        result.longest_same_turn_phase_streak =
            (std::max)(result.longest_same_turn_phase_streak, same_turn_phase_streak);

        if (previous.request_kind != event.request_kind) {
            ++result.request_transition_counts[previous.request_kind + "->" + event.request_kind];
        }
        if (previous.fallback_level != event.fallback_level) {
            ++result.fallback_transition_counts[
                std::to_string(previous.fallback_level) + "->" +
                std::to_string(event.fallback_level)];
        }
        if (previous.fallback_level == 4 && event.fallback_level == 4) {
            ++result.f4_to_f4_transitions;
        }
        if (previous.fallback_level == 0 && event.fallback_level == 4) {
            ++result.f0_to_f4_transitions;
        }
        if (previous.fallback_level == 4 && event.fallback_level == 0) {
            ++result.f4_to_f0_transitions;
        }
        if (event.fallback_level == 4) {
            if (previous.public_observation_digest == event.public_observation_digest) {
                ++result.f4_with_unchanged_public_observation_count;
            } else {
                ++result.f4_with_changed_public_observation_count;
            }
            if (current_state_equal(previous, event)) {
                ++result.f4_with_unchanged_current_state_count;
            } else if (!previous.public_current_state_fingerprint.empty() &&
                       !event.public_current_state_fingerprint.empty()) {
                ++result.f4_with_changed_current_state_count;
            }
        }
    }

    if (no_structural_progress > result.max_decisions_without_structural_progress) {
        result.max_decisions_without_structural_progress = no_structural_progress;
        result.max_structural_gap_start_decision =
            events[last_progress_index].decision_index + 1;
        result.max_structural_gap_end_decision = events.back().decision_index;
        result.max_engine_processes_without_structural_progress =
            events.back().engine_process_count - last_progress_process;
    }

    result.total_turns_observed = distinct_turns.size();
    result.distinct_public_observation_digests = observations.size();
    result.distinct_public_semantic_decision_ids = public_decisions.size();
    result.distinct_current_public_state_fingerprints = current_states.size();
    result.distinct_selected_public_action_keys = selected_actions.size();
    result.longest_repeated_cycle = find_task7_public_state_cycle(events);
    return result;
}

std::vector<Task7DiagnosticEvent> extract_task7_diagnostic_tail(
    const std::vector<Task7DiagnosticEvent>& events, const std::size_t limit) {
    if (limit == 0 || events.empty()) return {};
    const auto start = events.size() > limit ? events.size() - limit : 0;
    return {events.begin() + static_cast<std::ptrdiff_t>(start), events.end()};
}

}  // namespace ygo::diagnostics
