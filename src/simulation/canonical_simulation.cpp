#include "ygo/simulation/canonical_simulation.hpp"

#include <chrono>
#include <cstdint>
#include <exception>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

#include "ygo/core/core_error.hpp"
#include "ygo/environment/episode_driver.hpp"
#include "ygo/m3/conformance_policy.hpp"
#include "ygo/protocol/protocol_error.hpp"
#include "ygo/trace/engine_trace.hpp"

namespace ygo::simulation {
namespace {

using Clock = std::chrono::steady_clock;

std::uint64_t elapsed_us(const Clock::time_point start, const Clock::time_point end) {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(end - start).count());
}

std::string json_escape(const std::string& value) {
    std::ostringstream result;
    result << '"';
    for (const unsigned char character : value) {
        switch (character) {
        case '"':
            result << "\\\"";
            break;
        case '\\':
            result << "\\\\";
            break;
        case '\n':
            result << "\\n";
            break;
        case '\r':
            result << "\\r";
            break;
        case '\t':
            result << "\\t";
            break;
        default:
            if (character < 0x20) {
                constexpr char hex[] = "0123456789abcdef";
                result << "\\u00" << hex[character >> 4] << hex[character & 0xf];
            } else {
                result << static_cast<char>(character);
            }
            break;
        }
    }
    result << '"';
    return result.str();
}

SimulationResult failed_result(const SimulationJob& job, std::string failure_code, std::string message) {
    SimulationResult result;
    result.job_id = job.job_id;
    result.pass = false;
    result.failure_code = std::move(failure_code);
    result.error_message = std::move(message);
    return result;
}

void add_protocol_failure(SimulationResult& result, const protocol::ProtocolError& error) {
    if (error.code() == protocol::ProtocolErrorCode::IncompleteCandidates) {
        ++result.errors.truncated;
        result.failure_code = "candidate_truncated";
    } else {
        ++result.errors.unsupported;
        result.failure_code = "unsupported_decision";
    }
    result.error_message = error.what();
}

void add_timing(std::uint64_t& bucket, const Clock::time_point start, const Clock::time_point end,
                const bool enabled) {
    if (enabled) {
        bucket += elapsed_us(start, end);
    }
}

std::string m3_summary_json(const SimulationResult& result,
                            const CanonicalSimulationConfig& config,
                            const SimulationJob& job) {
    std::ostringstream summary;
    summary << "{\"schema_version\":\"ocgforge.m3.game_summary.v1\",\"format_id\":"
            << json_escape(config.format) << ",\"duel_mode_name\":" << json_escape(config.duel_mode)
            << ",\"duel_flags\":" << config.duel_flags << ",\"rules_bundle_id\":"
            << json_escape(config.rules_bundle_id) << ",\"core_patchset_id\":"
            << json_escape(config.patchset_id) << ",\"core_patchset_sha256\":"
            << json_escape(config.patchset_sha256) << ",\"terminal\":"
            << (result.terminal ? "true" : "false") << ",\"winner\":"
            << (result.winner.has_value() ? std::to_string(*result.winner) : "255") << ",\"win_reason\":"
            << (result.win_reason.has_value() ? std::to_string(*result.win_reason) : "255")
            << ",\"starting_player\":" << static_cast<unsigned>(job.starting_player)
            << ",\"engine_steps\":" << result.engine_steps << ",\"turns\":" << result.turns
            << ",\"battle_command_count\":" << result.battle_command_count
            << ",\"visible_life_points_event_count\":" << result.visible_life_points_event_count
            << ",\"visible_destroyed_event_count\":" << result.visible_destroyed_event_count
            << ",\"visible_win_event_count\":" << result.visible_win_event_count
            << ",\"interactive_decisions\":" << result.interactive_decisions
            << ",\"continuation_intermediate_steps\":" << result.continuation_intermediate_steps
            << ",\"candidate_count_max\":" << result.operations.candidate_max
            << ",\"candidate_count_mean\":" << result.candidate_count_mean
            << ",\"observation_entity_total\":" << result.observation_entity_total
            << ",\"observation_event_total\":" << result.observation_event_total
            << ",\"unsupported_count\":" << result.errors.unsupported
            << ",\"retry_count\":" << result.errors.retries
            << ",\"automatic_decision_count\":" << result.errors.automatic
            << ",\"candidate_truncation_count\":" << result.errors.truncated
            << ",\"core_error_count\":" << result.errors.core_errors
            << ",\"semantic_gameplay_hash\":" << json_escape(result.gameplay_hash)
            << ",\"trace_hash\":" << json_escape(result.trace_hash.value_or("")) << "}";
    return summary.str();
}

void copy_driver_metrics(SimulationResult& result, const environment::DriverMetrics& metrics) {
    result.errors.retries = metrics.errors.retries;
    result.errors.unsupported = metrics.errors.unsupported;
    result.errors.automatic = metrics.errors.automatic;
    result.errors.truncated = metrics.errors.truncated;
    result.errors.core_errors = metrics.errors.core_errors;
    result.engine_steps = static_cast<std::uint32_t>(metrics.process_call_count);
    result.interactive_decisions = metrics.interactive_decisions;
    result.semantic_action_count = metrics.semantic_action_count;
    result.continuation_intermediate_steps = metrics.continuation_intermediate_steps;
    result.turns = metrics.turns;
    result.battle_command_count = metrics.battle_command_count;
    result.visible_life_points_event_count = metrics.visible_life_points_event_count;
    result.visible_destroyed_event_count = metrics.visible_destroyed_event_count;
    result.visible_win_event_count = metrics.visible_win_event_count;
    result.observation_entity_total = metrics.observation_entity_total;
    result.observation_event_total = metrics.observation_event_total;
    result.response_build_time_us_total = metrics.response_build_time_us_total;
    result.response_build_time_us_max = metrics.response_build_time_us_max;
    result.timing.core_process_us = metrics.timing.core_process_us;
    result.timing.protocol_candidate_us = metrics.timing.protocol_candidate_us;
    result.timing.continuation_us = metrics.timing.continuation_us;
    result.timing.observation_us = metrics.timing.observation_us;
    result.timing.trace_hash_us = metrics.timing.trace_hash_us;
    result.operations.ocg_duel_process = metrics.operations.ocg_duel_process;
    result.operations.ocg_duel_query = metrics.operations.ocg_duel_query;
    result.operations.ocg_duel_query_location = metrics.operations.ocg_duel_query_location;
    result.operations.ocg_duel_query_field = metrics.operations.ocg_duel_query_field;
    result.operations.ocg_duel_query_count = metrics.operations.ocg_duel_query_count;
    result.operations.script_reader_requests = metrics.operations.script_reader_requests;
    result.operations.script_loads = metrics.operations.script_loads;
    result.operations.observations = metrics.operations.observations;
    result.operations.entities_projected = metrics.operations.entities_projected;
    result.operations.candidate_sets = metrics.operations.candidate_sets;
    result.operations.candidate_total = metrics.operations.candidate_total;
    result.operations.candidate_max = metrics.operations.candidate_max;
    result.candidate_count_mean = metrics.operations.candidate_sets == 0
                                      ? 0.0
                                      : static_cast<double>(metrics.operations.candidate_total) /
                                            static_cast<double>(metrics.operations.candidate_sets);
}

}  // namespace

SimulationResult run_canonical_simulation(const SimulationJob& job,
                                          const CanonicalSimulationConfig& config) {
    SimulationResult result;
    result.job_id = job.job_id;
#ifdef YGO_M4_PERFORMANCE_AUDIT
    ygo::observation::PerformanceAuditCollector performance_audit;
#endif
    const bool instrumentation = job.instrumentation || config.instrumentation;
    std::optional<Clock::time_point> simulation_start;
    bool simulation_timing_recorded = false;
    std::optional<Clock::time_point> persistence_start;
    bool persistence_timing_recorded = false;

    const auto record_simulation_timing = [&]() {
        if (!simulation_start.has_value() || simulation_timing_recorded) {
            return;
        }
        result.simulation_elapsed_us = elapsed_us(*simulation_start, Clock::now());
        const auto measured = result.timing.core_process_us + result.timing.protocol_candidate_us +
                              result.timing.continuation_us + result.timing.observation_us +
                              result.timing.trace_hash_us + result.timing.serialization_us;
        result.timing.other_us = result.simulation_elapsed_us >= measured
                                     ? result.simulation_elapsed_us - measured
                                     : 0;
        simulation_timing_recorded = true;
    };

    const auto record_persistence_timing = [&]() {
        if (persistence_start.has_value() && !persistence_timing_recorded) {
            result.trace_persistence_us = elapsed_us(*persistence_start, Clock::now());
            persistence_timing_recorded = true;
        }
    };

    if (!is_canonical_identity(config) || job.canonical_rules_id != config.rules_bundle_id) {
        result = failed_result(job, "canonical_identity_mismatch",
                               "simulation configuration does not match the canonical M4 identity");
#ifdef YGO_M4_PERFORMANCE_AUDIT
        result.performance_audit = performance_audit.snapshot();
#endif
        return result;
    }

    const auto& fixed_deck_a = config.deck_a;
    const auto& fixed_deck_b = config.deck_b;
    const auto& deck_a = job.seat_assignment == SeatAssignment::Mirror ? fixed_deck_b : fixed_deck_a;
    const auto& deck_b = job.seat_assignment == SeatAssignment::Mirror ? fixed_deck_a : fixed_deck_b;

    try {
        environment::EpisodeDriverConfig driver_config;
        driver_config.rules = config.rules;
        driver_config.player_zero_deck = deck_a;
        driver_config.player_one_deck = deck_b;
        driver_config.seed = job.seed;
        driver_config.duel_flags = config.duel_flags;
        driver_config.starting_player = job.starting_player;
        driver_config.engine_process_budget = job.max_steps;
        driver_config.build_full_observation = job.observation_mode == ObservationMode::Full;
        driver_config.required_script_codes = config.required_script_codes;
        driver_config.fixture_setup_script = job.setup_script;
        driver_config.instrumentation = instrumentation;
        driver_config.force_unsupported_for_test = job.force_unsupported;
#ifdef YGO_M4_PERFORMANCE_AUDIT
        driver_config.performance_audit = &performance_audit;
#endif

        simulation_start = Clock::now();
        environment::EpisodeDriver driver(std::move(driver_config));
        const m3::DeterministicConformancePolicy m3_policy(job.focus_codes, true);
        std::size_t replay_action_index = 0;
        bool hard_driver_failure = false;
        bool forced_unsupported = false;
        bool budget_exhausted = false;
        std::optional<environment::DriverGameTerminal> terminal;

        auto boundary = driver.advance_until_boundary();
        for (;;) {
            if (const auto* decision = std::get_if<environment::DriverDecisionBoundary>(&boundary)) {
                if (decision->request == nullptr) {
                    throw std::runtime_error("driver returned a null decision request");
                }
                std::string semantic_key;
                if (!job.replay_actions.empty()) {
                    if (replay_action_index >= job.replay_actions.size()) {
                        throw protocol::ProtocolError(
                            protocol::ProtocolErrorCode::InvalidSemanticKey,
                            "replay action stream ended before the engine reached terminal state",
                            decision->request->engine_message_type, decision->request->player);
                    }
                    semantic_key = job.replay_actions[replay_action_index++];
                } else {
                    semantic_key = m3_policy.choose(*decision->request).semantic_key;
                }
                auto apply_result = driver.apply_semantic_key(semantic_key);
                boundary = std::move(apply_result.next);
                continue;
            }
            if (const auto* reached_terminal = std::get_if<environment::DriverGameTerminal>(&boundary)) {
                terminal = *reached_terminal;
                break;
            }
            if (std::holds_alternative<environment::DriverProcessBudgetExceeded>(boundary)) {
                budget_exhausted = true;
                break;
            }
            const auto& failure = std::get<environment::DriverFailure>(boundary);
            result.errors.retries = failure.errors.retries;
            result.errors.unsupported = failure.errors.unsupported;
            result.errors.automatic = failure.errors.automatic;
            result.errors.truncated = failure.errors.truncated;
            result.errors.core_errors = failure.errors.core_errors;
            result.failure_code = failure.failure_code;
            result.error_message = failure.error_message;
            hard_driver_failure = true;
            forced_unsupported = failure.failure_code == "forced_unsupported";
            break;
        }

        if (!hard_driver_failure || forced_unsupported) {
            if (!forced_unsupported && !job.replay_actions.empty() &&
                replay_action_index != job.replay_actions.size()) {
                throw std::runtime_error("replay action stream contains unused semantic actions: " +
                                         std::to_string(job.replay_actions.size() - replay_action_index));
            }
            const auto& metrics = driver.metrics();
            copy_driver_metrics(result, metrics);
            result.terminal = terminal.has_value();
            if (terminal.has_value()) {
                result.winner = terminal->winner;
                result.win_reason = terminal->win_reason;
            } else if (budget_exhausted) {
                result.failure_code = "nonterminal";
                result.error_message = "canonical simulation did not reach terminal state before max_steps";
            }
            const auto trace_start = Clock::now();
            result.gameplay_hash = trace::semantic_gameplay_hash(driver.trace());
            ++result.operations.semantic_hashes;
            add_timing(result.timing.trace_hash_us, trace_start, Clock::now(), instrumentation);
            if (job.mode == SimulationMode::Conformance) {
                const auto serialization_start = Clock::now();
                const auto serialized = trace::canonical_trace_jsonl_v2(driver.trace());
                add_timing(result.timing.serialization_us, serialization_start, Clock::now(), instrumentation);
                const auto hash_start = Clock::now();
                result.trace_hash = trace::canonical_trace_hash_v2(driver.trace());
                add_timing(result.timing.trace_hash_us, hash_start, Clock::now(), instrumentation);
                result.trace_jsonl = serialized;
                result.operations.trace_bytes_serialized = serialized.size();
            }
        }

        result.pass = result.terminal && result.errors.retries == 0 && result.errors.unsupported == 0 &&
                      result.errors.automatic == 0 && result.errors.truncated == 0 &&
                      result.errors.core_errors == 0 && result.errors.worker_errors == 0;
        if (!result.pass && result.failure_code.empty()) {
            result.failure_code = "simulation_failed";
        }
        record_simulation_timing();
        const bool no_error_nonterminal =
            result.failure_code == "nonterminal" && result.errors.retries == 0 &&
            result.errors.unsupported == 0 && result.errors.automatic == 0 &&
            result.errors.truncated == 0 && result.errors.core_errors == 0;
        if ((result.pass || no_error_nonterminal) && job.persist_trace &&
            job.mode == SimulationMode::Conformance && !job.trace_output.empty()) {
            persistence_start = Clock::now();
            {
                std::ofstream stream(job.trace_output, std::ios::binary);
                if (!stream) {
                    throw std::runtime_error("cannot open trace output: " + job.trace_output.string());
                }
                stream << result.trace_jsonl.value_or("");
                stream << "# semantic_gameplay_hash=" << result.gameplay_hash << "\n";
                stream << "# trace_hash=" << result.trace_hash.value_or("") << "\n";
                stream << "# m3_summary=" << m3_summary_json(result, config, job) << "\n";
            }
            record_persistence_timing();
        }
    } catch (const protocol::ProtocolError& error) {
        result.pass = false;
        if (result.failure_code.empty()) {
            add_protocol_failure(result, error);
        }
        record_simulation_timing();
        record_persistence_timing();
    } catch (const core::CoreError& error) {
        result.pass = false;
        ++result.errors.core_errors;
        result.failure_code = "core_error";
        result.error_message = error.what();
        record_simulation_timing();
        record_persistence_timing();
    } catch (const std::exception& error) {
        result.pass = false;
        result.failure_code = result.failure_code.empty() ? "simulation_error" : result.failure_code;
        result.error_message = error.what();
        record_simulation_timing();
        record_persistence_timing();
    }

#ifdef YGO_M4_PERFORMANCE_AUDIT
    result.performance_audit = performance_audit.snapshot();
#endif
    return result;
}

}  // namespace ygo::simulation
