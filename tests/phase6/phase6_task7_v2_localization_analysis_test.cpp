#include "ygo/diagnostics/task7_localization_analysis.hpp"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "ygo/environment/public_environment_observation.hpp"
#include "ygo/environment/public_safe_state.hpp"
#include "ygo/observation/player_observation.hpp"
#include "ygo/trace/sha256.hpp"

namespace {

void require(const bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

ygo::diagnostics::Task7DiagnosticEvent event(const std::uint64_t decision_index,
                                              const std::string& state,
                                              const std::string& request = "idle_command",
                                              const std::string& action = "action",
                                              const std::string& domain = "domain",
                                              const std::uint8_t fallback = 0) {
    ygo::diagnostics::Task7DiagnosticEvent value;
    value.phase = "TEACHER";
    value.decision_index = decision_index;
    value.engine_step_index = decision_index * 2;
    value.engine_process_count = value.engine_step_index + 1;
    value.semantic_action_count = decision_index + 1;
    value.acting_player = 1;
    value.request_kind = request;
    value.public_observation_digest = "history-" + std::to_string(decision_index);
    value.public_semantic_decision_id = "decision-" + std::to_string(decision_index);
    value.public_current_state_fingerprint = state;
    value.public_candidate_domain_digest = domain;
    value.selected_public_action_key = action;
    value.fallback_level = fallback;
    value.public_turn_count_present = true;
    value.public_turn_count = 16;
    value.public_turn_player = 1;
    value.public_phase = "4";
    value.public_life_points_present = true;
    value.public_life_points_p0 = 8000;
    value.public_life_points_p1 = 8000;
    value.public_entity_count = 40;
    value.public_chain_length = 0;
    return value;
}

void test_exact_cycles_and_monotonic_non_cycle() {
    using ygo::diagnostics::find_task7_public_state_cycle;

    const auto one = find_task7_public_state_cycle(
        {event(10, "S", "idle", "A"), event(11, "S", "idle", "A"),
         event(12, "S", "idle", "A")});
    require(one.found && one.cycle_length == 1 && one.repeat_count == 3,
            "period-one cycle was not detected exactly");

    const auto two = find_task7_public_state_cycle(
        {event(0, "S", "idle", "A"), event(1, "U", "unselect", "B"),
         event(2, "S", "idle", "A"), event(3, "U", "unselect", "B"),
         event(4, "S", "idle", "A"), event(5, "U", "unselect", "B")});
    require(two.found && two.cycle_length == 2 && two.repeat_count == 3 &&
                two.start_index == 0 && two.end_index == 5,
            "period-two cycle was not selected deterministically");

    const auto greater_than_two = find_task7_public_state_cycle(
        {event(0, "A", "r0", "x"), event(1, "B", "r1", "y"),
         event(2, "C", "r2", "z"), event(3, "A", "r0", "x"),
         event(4, "B", "r1", "y"), event(5, "C", "r2", "z"),
         event(6, "A", "r0", "x"), event(7, "B", "r1", "y"),
         event(8, "C", "r2", "z")});
    require(greater_than_two.found && greater_than_two.cycle_length == 3 &&
                greater_than_two.repeat_count == 3,
            "cycle longer than two was not detected");

    const auto monotonic = find_task7_public_state_cycle(
        {event(0, "S0", "idle", "A"), event(1, "S1", "idle", "A"),
         event(2, "S2", "idle", "A"), event(3, "S3", "idle", "A")});
    require(!monotonic.found, "monotonic progression was reported as a cycle");

    const auto partial = find_task7_public_state_cycle(
        {event(0, "S", "idle", "A"), event(1, "U", "unselect", "B"),
         event(2, "S", "idle", "A")});
    require(!partial.found, "partial trailing pattern was overclaimed as a cycle");

    const auto tie = find_task7_public_state_cycle(
        {event(0, "A", "idle", "x"), event(1, "A", "idle", "x"),
         event(2, "A", "idle", "x"), event(3, "B", "idle", "y"),
         event(4, "B", "idle", "y"), event(5, "B", "idle", "y")});
    require(tie.found && tie.cycle_length == 1 && tie.repeat_count == 3 &&
                tie.start_index == 0 && tie.end_index == 2,
            "cycle tie-breaking was not earliest and deterministic");

    const auto first = find_task7_public_state_cycle(
        {event(0, "S", "idle", "A"), event(1, "S", "idle", "A"),
         event(2, "S", "idle", "A")});
    const auto second = find_task7_public_state_cycle(
        {event(0, "S", "idle", "A"), event(1, "S", "idle", "A"),
         event(2, "S", "idle", "A")});
    require(first.found == second.found && first.start_index == second.start_index &&
                first.end_index == second.end_index &&
                first.cycle_length == second.cycle_length &&
                first.repeat_count == second.repeat_count,
            "cycle selection was not deterministic");
}

void test_current_state_cycle_key_ignores_history_identity() {
    auto left = event(500, "same-state", "idle", "A");
    auto right = event(502, "same-state", "idle", "A");
    right.public_observation_digest = "different-history";
    right.public_semantic_decision_id = "different-decision-id";
    right.engine_step_index = 90002;
    right.engine_process_count = 90003;
    right.semantic_action_count = 90003;
    const auto same_state = ygo::diagnostics::find_task7_public_state_cycle({left, right});
    require(same_state.found && same_state.cycle_length == 1,
            "decision/history identity prevented current-state equality");

    right.public_current_state_fingerprint = "different-state";
    const auto different_state =
        ygo::diagnostics::find_task7_public_state_cycle({left, right});
    require(!different_state.found, "different current public states shared a cycle key");
}

void test_analysis_tail_structural_gap_fallback_and_turn_phase() {
    std::vector<ygo::diagnostics::Task7DiagnosticEvent> samples;
    samples.push_back(event(0, "A", "idle_command", "x", "d0", 0));
    samples.push_back(event(1, "A", "idle_command", "x", "d0", 4));
    samples.push_back(event(2, "A", "unselect", "y", "d1", 4));
    samples.push_back(event(3, "A", "idle_command", "x", "d0", 0));
    auto changed_turn = event(4, "B", "idle_command", "x", "d0", 4);
    changed_turn.public_turn_count = 17;
    changed_turn.public_phase = "5";
    samples.push_back(changed_turn);

    const auto analysis = ygo::diagnostics::analyze_task7_localization(samples);
    require(analysis.total_decisions == 5, "analysis decision count was wrong");
    require(analysis.distinct_current_public_state_fingerprints == 2,
            "current-state distinct count was wrong");
    require(analysis.longest_identical_current_public_state_streak == 4,
            "current-state streak was wrong");
    require(analysis.max_decisions_without_structural_progress == 3,
            "structural progress gap was wrong");
    require(analysis.max_engine_processes_without_structural_progress == 9,
            "structural process gap was wrong");
    require(analysis.fallback_level_counts.at("0") == 2 &&
                analysis.fallback_level_counts.at("4") == 3,
            "fallback aggregation was wrong");
    require(analysis.f4_longest_consecutive_streak == 2,
            "F4 streak aggregation was wrong");
    require(analysis.f4_to_f4_transitions == 1 && analysis.f0_to_f4_transitions == 2 &&
                analysis.f4_to_f0_transitions == 1,
            "fallback transitions were wrong");
    require(analysis.turn_counts.at("16") == 4 && analysis.turn_counts.at("17") == 1,
            "turn aggregation was wrong");
    require(analysis.request_counts.find("idle") == analysis.request_counts.end(),
            "unexpected request key was inserted");
    require(analysis.request_counts.at("idle_command") == 4 &&
                analysis.request_counts.at("unselect") == 1,
            "request-family aggregation was wrong");

    const auto tail = ygo::diagnostics::extract_task7_diagnostic_tail(samples, 3);
    require(tail.size() == 3 && tail.front().decision_index == 2 &&
                tail.back().decision_index == 4,
            "tail extraction was wrong");

    std::vector<ygo::diagnostics::Task7DiagnosticEvent> long_samples;
    for (std::uint64_t index = 0; index < 300; ++index) {
        long_samples.push_back(event(index, "state-" + std::to_string(index)));
    }
    const auto long_tail = ygo::diagnostics::extract_task7_diagnostic_tail(long_samples, 256);
    require(long_tail.size() == 256 && long_tail.front().decision_index == 44,
            "256-entry tail was not bounded deterministically");
}

void test_privacy_allowlist_and_current_state_fingerprint() {
    ygo::observation::PlayerObservation source;
    source.schema_version = "ygo.player_observation.v1";
    source.perspective_player = 0;
    source.globals.life_points = {8000, 8000};
    source.match_context.perspective_player = 0;

    ygo::observation::VisibleGameEvent first;
    first.event_index = 1;
    first.kind = ygo::observation::VisibleEventKind::CardMoved;
    source.visible_events.push_back(first);
    const auto first_bytes = ygo::environment::canonical_public_safe_state_bytes(source);
    const auto first_view = ygo::environment::decode_canonical_public_safe_state(first_bytes);
    require(first_view && first_view.value.has_value(), "first public state did not decode");

    auto second = source;
    auto additional = first;
    additional.event_index = 2;
    additional.kind = ygo::observation::VisibleEventKind::PhaseChanged;
    second.visible_events.push_back(additional);
    const auto second_bytes = ygo::environment::canonical_public_safe_state_bytes(second);
    const auto second_view = ygo::environment::decode_canonical_public_safe_state(second_bytes);
    require(second_view && second_view.value.has_value(), "second public state did not decode");

    const auto first_projection =
        ygo::environment::diagnostic_public_current_state_bytes(*first_view.value);
    const auto second_projection =
        ygo::environment::diagnostic_public_current_state_bytes(*second_view.value);
    require(first_projection == second_projection,
            "visible event history leaked into current-state projection");
    require(ygo::trace::sha256_bytes(first_projection) ==
                ygo::trace::sha256_bytes(second_projection),
            "history-only change altered current-state fingerprint");

    auto internal_metadata = source;
    internal_metadata.engine_step_index = 9001;
    internal_metadata.visible_events.front().engine_step_index = 123456789;
    const auto internal_bytes =
        ygo::environment::canonical_public_safe_state_bytes(internal_metadata);
    const auto internal_view =
        ygo::environment::decode_canonical_public_safe_state(internal_bytes);
    require(internal_view && internal_view.value.has_value(),
            "internal-metadata public state did not decode");
    const auto internal_projection =
        ygo::environment::diagnostic_public_current_state_bytes(*internal_view.value);
    require(first_projection == internal_projection,
            "internal metadata leaked into current-state projection");

    auto changed = source;
    changed.globals.life_points[0] = 7999;
    const auto changed_bytes = ygo::environment::canonical_public_safe_state_bytes(changed);
    const auto changed_view = ygo::environment::decode_canonical_public_safe_state(changed_bytes);
    require(changed_view && changed_view.value.has_value(), "changed public state did not decode");
    const auto changed_projection =
        ygo::environment::diagnostic_public_current_state_bytes(*changed_view.value);
    require(first_projection != changed_projection &&
                ygo::trace::sha256_bytes(first_projection) !=
                    ygo::trace::sha256_bytes(changed_projection),
            "current public state change did not change projection");
}

}  // namespace

int main() {
    try {
        test_exact_cycles_and_monotonic_non_cycle();
        test_current_state_cycle_key_ignores_history_identity();
        test_analysis_tail_structural_gap_fallback_and_turn_phase();
        test_privacy_allowlist_and_current_state_fingerprint();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "phase6_task7_v2_localization_analysis_test: " << error.what() << '\n';
        return 1;
    }
}
