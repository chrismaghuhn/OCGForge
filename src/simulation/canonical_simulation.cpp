#include "ygo/simulation/canonical_simulation.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <exception>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "ocgapi_constants.h"
#include "ygo/core/core_error.hpp"
#include "ygo/core/core_host.hpp"
#include "ygo/m3/canonical_rules.hpp"
#include "ygo/m3/conformance_policy.hpp"
#include "ygo/observation/decision_integration.hpp"
#include "ygo/observation/observation_builder.hpp"
#include "ygo/observation/observation_session.hpp"
#include "ygo/protocol/message_decoder.hpp"
#include "ygo/protocol/protocol_error.hpp"
#include "ygo/trace/engine_trace.hpp"
#include "ygo/trace/sha256.hpp"

namespace ygo::simulation {
namespace {

using Clock = std::chrono::steady_clock;

std::uint64_t elapsed_us(const Clock::time_point start, const Clock::time_point end) {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(end - start).count());
}

ygo::core::SeedBundle seed_bundle(std::uint64_t seed) {
    return {{seed, seed ^ 0x9e3779b97f4a7c15ULL, seed + 0x6a09e667f3bcc909ULL,
             (seed << 1) ^ 0xbb67ae8584caa73bULL}};
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
                const char* hex = "0123456789abcdef";
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

#ifndef YGO_M4_PERFORMANCE_AUDIT
std::string public_state_hash(const ygo::core::CoreHost& host, std::uint8_t perspective) {
    auto field = host.query_field();
    field.push_back(perspective);
    return ygo::trace::sha256_bytes(field);
}
#endif

#ifdef YGO_M4_PERFORMANCE_AUDIT
std::string public_state_hash_audited(const ygo::core::CoreHost& host, std::uint8_t perspective,
                                      ygo::observation::PerformanceAuditCollector* audit) {
    ygo::observation::PerformanceAuditCollector::AuxiliaryScope whole_scope(
        audit, ygo::observation::PerformanceAuditAuxiliaryBucket::PublicStateHash);
    ygo::observation::PerformanceAuditCollector::AuxiliaryScope query_scope(
        audit, ygo::observation::PerformanceAuditAuxiliaryBucket::PublicStateHashQueryField);
    if (audit != nullptr) {
        audit->record_query_field_call(false);
    }
    auto field = host.query_field();
    field.push_back(perspective);
    return ygo::trace::sha256_bytes(field);
}
#endif

ygo::trace::TraceManifest manifest(const ygo::core::CoreHost& host,
                                   const ygo::core::FixtureDeck& deck_a,
                                   const ygo::core::FixtureDeck& deck_b) {
    ygo::trace::TraceManifest result;
    result.rules_bundle_id = host.config().rules.bundle_id;
    result.core_repository = host.config().rules.core_repository;
    result.core_commit = host.config().rules.core_commit;
    result.core_patchset_id = host.config().rules.core_patchset_id;
    result.core_patchset_sha256 = host.config().rules.core_patchset_sha256;
    result.cardscripts_repository = host.config().rules.cardscripts_repository;
    result.cardscripts_commit = host.config().rules.cardscripts_commit;
    result.database_repository = host.config().rules.database_repository;
    result.database_commit = host.config().rules.database_commit;
    result.core_api_version = std::to_string(host.api_major()) + "." + std::to_string(host.api_minor());
#ifdef YGO_M0_COMPILER_ID
    result.compiler_identity = YGO_M0_COMPILER_ID;
#else
    result.compiler_identity = "unknown";
#endif
#ifdef YGO_M0_BUILD_TYPE
    result.build_type = YGO_M0_BUILD_TYPE;
#else
    result.build_type = "unknown";
#endif
#if defined(_WIN32)
    result.platform_identity = "windows";
#else
    result.platform_identity = "unknown";
#endif
    result.duel_flags = host.config().duel_flags;
    result.starting_player = host.config().effective_starting_player();
    result.format_id = std::string(ygo::m3::canonical_rules().format_id);
    result.duel_mode_name = std::string(ygo::m3::canonical_rules().duel_mode_name);
    result.seed_bundle = host.config().seed.words;
    result.fixture_deck_hashes = {deck_a.sha256, deck_b.sha256};
    result.policy_identifier = "m3.deterministic_conformance.v1";
    return result;
}

void emit_unsupported_diagnostic(const ygo::protocol::ProtocolError& error, std::uint32_t step_index,
                                 const std::vector<std::uint8_t>& raw_message,
                                 const ygo::core::CoreHostConfig& config,
                                 const ygo::core::FixtureDeck& deck_a,
                                 const ygo::core::FixtureDeck& deck_b,
                                 const ygo::trace::EngineTrace& trace) {
    std::cerr << "UNSUPPORTED_OR_MALFORMED_DECISION {\"message_type\":"
              << static_cast<unsigned>(error.message_type()) << ",\"player\":"
              << static_cast<unsigned>(error.player()) << ",\"step_index\":" << step_index
              << ",\"raw_message_sha256\":" << json_escape(ygo::trace::sha256_bytes(raw_message))
              << ",\"rules_bundle_id\":" << json_escape(config.rules.bundle_id) << ",\"deck_hashes\":["
              << json_escape(deck_a.sha256) << "," << json_escape(deck_b.sha256) << "],\"seed_bundle\":["
              << config.seed.words[0] << "," << config.seed.words[1] << "," << config.seed.words[2] << ","
              << config.seed.words[3] << "],\"recent_trace_context\":[";
    const auto context_start = trace.steps.size() > 8 ? trace.steps.size() - 8 : 0;
    for (std::size_t index = context_start; index < trace.steps.size(); ++index) {
        if (index != context_start) {
            std::cerr << ',';
        }
        const auto& step = trace.steps[index];
        std::cerr << "{\"step_index\":" << step.step_index << ",\"decision_request_kind\":"
                  << json_escape(step.decision_request_kind) << ",\"candidate_count\":"
                  << step.complete_candidate_count << '}';
    }
    std::cerr << "],\"error\":" << json_escape(error.what()) << "}\n";
}

SimulationResult failed_result(const SimulationJob& job, std::string failure_code, std::string message) {
    SimulationResult result;
    result.job_id = job.job_id;
    result.pass = false;
    result.failure_code = std::move(failure_code);
    result.error_message = std::move(message);
    return result;
}

void add_protocol_failure(SimulationResult& result, const ygo::protocol::ProtocolError& error) {
    if (error.code() == ygo::protocol::ProtocolErrorCode::IncompleteCandidates) {
        ++result.errors.truncated;
        result.failure_code = "candidate_truncated";
    } else {
        ++result.errors.unsupported;
        result.failure_code = "unsupported_decision";
    }
    result.error_message = error.what();
}

void add_timing(std::uint64_t& bucket, const Clock::time_point start, const Clock::time_point end,
                bool enabled) {
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

}  // namespace

SimulationResult run_canonical_simulation(const SimulationJob& job,
                                          const CanonicalSimulationConfig& config) {
    SimulationResult result;
    result.job_id = job.job_id;
#ifdef YGO_M4_PERFORMANCE_AUDIT
    ygo::observation::PerformanceAuditCollector performance_audit;
#endif
    const bool instrumentation = job.instrumentation || config.instrumentation;
    ygo::trace::EngineTrace trace;
    bool terminal_reached = false;
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
        ygo::core::CoreHostConfig host_config;
        host_config.rules = config.rules;
        host_config.duel_flags = config.duel_flags;
        host_config.starting_player = job.starting_player;
        host_config.seed = seed_bundle(job.seed);
        host_config.required_script_codes = config.required_script_codes;
#ifdef YGO_M4_PERFORMANCE_AUDIT
        host_config.performance_audit = &performance_audit;
#endif

        simulation_start = Clock::now();
        {
#ifdef YGO_M4_PERFORMANCE_AUDIT
            auto setup_scope = performance_audit.setup_scope(
                ygo::observation::PerformanceAuditSetupBucket::CoreHostSetup);
#endif
            ygo::core::CoreHost host(host_config);
            host.load_deck(0, deck_a);
            host.load_deck(1, deck_b);
            host.start_duel();
            if (!job.setup_script.empty()) {
#ifdef YGO_M4_PERFORMANCE_AUDIT
                auto fixture_script_scope = performance_audit.setup_scope(
                    ygo::observation::PerformanceAuditSetupBucket::FixtureScriptLoad);
#endif
                host.load_fixture_script(job.setup_script);
#ifdef YGO_M4_PERFORMANCE_AUDIT
                fixture_script_scope.finish();
#endif
            }
#ifdef YGO_M4_PERFORMANCE_AUDIT
            setup_scope.finish();
#endif

            ygo::observation::ObservationSession observation_sessions[] = {
                ygo::observation::ObservationSession(0, static_cast<std::uint32_t>(config.duel_flags)),
                ygo::observation::ObservationSession(1, static_cast<std::uint32_t>(config.duel_flags)),
            };

            const auto trace_manifest_start = Clock::now();
            trace.manifest = manifest(host, deck_a, deck_b);
            trace.manifest.trace_schema_version = "ygo.engine_trace.v2";
            add_timing(result.timing.trace_hash_us, trace_manifest_start, Clock::now(), instrumentation);
            const ygo::m3::DeterministicConformancePolicy m3_policy(job.focus_codes, true);
            std::size_t replay_action_index = 0;
            bool forced_unsupported = false;
            std::uint32_t decision_index = 0;
            std::uint32_t interactive_decision_count = 0;
            std::uint32_t semantic_action_count = 0;
            std::uint32_t continuation_intermediate_count = 0;
            std::uint64_t candidate_count_total = 0;
            std::size_t candidate_count_max = 0;
            std::uint64_t observation_entity_total = 0;
            std::uint64_t observation_event_total = 0;
            std::uint8_t terminal_winner = 255;
            std::uint8_t terminal_reason = 255;
            std::uint64_t response_build_time_us_total = 0;
            std::uint64_t response_build_time_us_max = 0;

            if (job.force_unsupported) {
                const std::vector<std::uint8_t> raw_message = {
                    3, 0, 0, 0, MSG_SELECT_OPTION, 0, 0,
                };
                try {
                    (void)ygo::protocol::decode_messages(raw_message);
                    throw std::runtime_error("forced unsupported probe unexpectedly decoded successfully");
                } catch (const ygo::protocol::ProtocolError& error) {
                    emit_unsupported_diagnostic(error, 0, raw_message, host_config, deck_a, deck_b, trace);
                    ++result.errors.unsupported;
                    result.failure_code = "forced_unsupported";
                    result.error_message = error.what();
                    forced_unsupported = true;
                }
            }

            for (std::uint32_t index = 0; index < job.max_steps && !forced_unsupported; ++index) {
                const auto process_start = Clock::now();
                const auto process_result = host.process();
                add_timing(result.timing.core_process_us, process_start, Clock::now(), instrumentation);

                try {
                    const auto protocol_start = Clock::now();
                    observation_sessions[0].ingest(process_result.message, index);
                    observation_sessions[1].ingest(process_result.message, index);
                    const auto decoded = ygo::protocol::decode_messages(process_result.message, index);
                    add_timing(result.timing.protocol_candidate_us, protocol_start, Clock::now(), instrumentation);
                    if (decoded.retry) {
                        ++result.errors.retries;
                        result.failure_code = "retry";
                        throw std::runtime_error("pinned core emitted MSG_RETRY after a submitted response");
                    }
                    if (decoded.terminal) {
                        const auto trace_record_prefix_start = Clock::now();
                        ygo::trace::TraceStep terminal;
                        terminal.step_index = index;
                        terminal.decision_index = decision_index++;
                        terminal.engine_step_index = index;
                        terminal.raw_message_length = static_cast<std::uint32_t>(process_result.message.size());
                        terminal.raw_message_sha256 = ygo::trace::sha256_bytes(process_result.message);
#ifdef YGO_M4_PERFORMANCE_AUDIT
                        terminal.public_state_hash = public_state_hash_audited(host, 0, &performance_audit);
#else
                        terminal.public_state_hash = public_state_hash(host, 0);
#endif
                        add_timing(result.timing.trace_hash_us, trace_record_prefix_start, Clock::now(), instrumentation);
                        Clock::time_point trace_record_suffix_start = Clock::now();
                        if (job.observation_mode == ObservationMode::Full) {
                            const auto observation_start = Clock::now();
#ifdef YGO_M4_PERFORMANCE_AUDIT
                            auto observation_scope = performance_audit.observation_scope();
#endif
                            ygo::observation::ObservationBuildConfig observation_config;
                            observation_config.decision_index = decision_index;
                            observation_config.engine_step_index = index;
                            observation_config.visible_events = observation_sessions[0].visible_events();
                            observation_config.knowledge.own_decklist_known = true;
                            observation_config.own_deck.known = true;
                            observation_config.own_deck.main_deck = deck_a.main_deck;
#ifdef YGO_M4_PERFORMANCE_AUDIT
                            observation_config.performance_audit = &performance_audit;
#endif
                            const auto observation = ygo::observation::build_player_observation(
                                host, 0, observation_config);
                            ygo::trace::attach_observation_metadata(terminal, observation);
                            add_timing(result.timing.observation_us, observation_start, Clock::now(), instrumentation);
                            observation_entity_total += observation.entities.size();
                            observation_event_total += observation.visible_events.size();
                            ++result.operations.observations;
                            result.operations.entities_projected += observation.entities.size();
                        }
                        trace_record_suffix_start = Clock::now();
                        terminal.engine_advanced = true;
                        terminal.terminal = true;
                        terminal.winner = decoded.winner;
                        terminal.win_reason = decoded.win_reason;
                        trace.steps.push_back(std::move(terminal));
                        add_timing(result.timing.trace_hash_us, trace_record_suffix_start, Clock::now(), instrumentation);
                        terminal_reached = true;
                        terminal_winner = decoded.winner;
                        terminal_reason = decoded.win_reason;
                        break;
                    }
                    if (!decoded.interactive || decoded.decisions.empty()) {
                        continue;
                    }
                    if (decoded.decisions.size() != 1) {
                        throw ygo::protocol::ProtocolError(
                            ygo::protocol::ProtocolErrorCode::UnsupportedDecision,
                            "more than one interactive message in a process result");
                    }
                    auto request = decoded.decisions.front();
                    ++interactive_decision_count;
                    for (;;) {
                        const auto candidate_start = Clock::now();
                        ygo::protocol::validate_candidate_set(request);
                        candidate_count_total += request.candidates.size();
                        candidate_count_max = std::max(candidate_count_max, request.candidates.size());
                        ++result.operations.candidate_sets;
                        result.operations.candidate_total += request.candidates.size();
                        result.operations.candidate_max = std::max<std::uint64_t>(
                            result.operations.candidate_max, request.candidates.size());
                        const ygo::protocol::ActionCandidate* selected_candidate = nullptr;
                        if (!job.replay_actions.empty()) {
                            if (replay_action_index >= job.replay_actions.size()) {
                                throw ygo::protocol::ProtocolError(
                                    ygo::protocol::ProtocolErrorCode::InvalidSemanticKey,
                                    "replay action stream ended before the engine reached terminal state",
                                    request.engine_message_type, request.player);
                            }
                            selected_candidate = &ygo::protocol::select_candidate(
                                request, job.replay_actions[replay_action_index++]);
                        } else {
                            selected_candidate = &m3_policy.choose(request);
                        }
                        const auto& selected = *selected_candidate;
                        ++semantic_action_count;
                        add_timing(result.timing.protocol_candidate_us, candidate_start, Clock::now(), instrumentation);

                        ygo::observation::PlayerObservation observation;
                        Clock::time_point observation_start{};
                        bool has_observation = false;
                        if (job.observation_mode == ObservationMode::Full) {
                            observation_start = Clock::now();
#ifdef YGO_M4_PERFORMANCE_AUDIT
                            auto observation_scope = performance_audit.observation_scope();
#endif
                            ygo::observation::ObservationBuildConfig observation_config;
                            observation_config.decision_index = decision_index;
                            observation_config.engine_step_index = request.engine_step_index;
                            observation_config.visible_events = observation_sessions[request.player].visible_events();
                            observation_config.knowledge.own_decklist_known = true;
                            observation_config.own_deck.known = true;
                            observation_config.own_deck.main_deck = request.player == 0 ? deck_a.main_deck : deck_b.main_deck;
                            observation_config.finalization =
                                ygo::observation::ObservationFinalization::Deferred;
#ifdef YGO_M4_PERFORMANCE_AUDIT
                            observation_config.performance_audit = &performance_audit;
#endif
                            observation = ygo::observation::build_player_observation(
                                host, request.player, observation_config);
                            ygo::observation::attach_decision_context(observation, request
#ifdef YGO_M4_PERFORMANCE_AUDIT
                                                                        , &performance_audit
#endif
                            );
                            has_observation = true;
                            observation_entity_total += observation.entities.size();
                            observation_event_total += observation.visible_events.size();
                            ++result.operations.observations;
                            result.operations.entities_projected += observation.entities.size();
                            if (has_observation) {
                                for (const auto& candidate_item : request.candidates) {
                                    const bool has_snapshot_locator =
                                        (candidate_item.source_card != 0 && candidate_item.source_location != 0 &&
                                         candidate_item.source_location != LOCATION_DECK &&
                                         candidate_item.source_location != LOCATION_EXTRA) ||
                                        (candidate_item.target_card != 0 && candidate_item.target_location != 0 &&
                                         candidate_item.target_location != LOCATION_DECK &&
                                         candidate_item.target_location != LOCATION_EXTRA);
#ifdef YGO_M4_PERFORMANCE_AUDIT
                                    if (has_snapshot_locator) {
                                        auto candidate_scope = performance_audit.scope(
                                            ygo::observation::PerformanceAuditBucket::CandidateConsistency);
                                        if (!ygo::observation::candidate_observation_consistent(
                                                observation, candidate_item, &performance_audit)) {
#else
                                    if (has_snapshot_locator &&
                                        !ygo::observation::candidate_observation_consistent(observation, candidate_item
                                        )) {
#endif
                                        throw ygo::protocol::ProtocolError(
                                            ygo::protocol::ProtocolErrorCode::UnsupportedDecision,
                                            "visible M3 candidate does not resolve against PlayerObservation: " +
                                                candidate_item.semantic_key,
                                            request.engine_message_type, request.player);
                                    }
#ifdef YGO_M4_PERFORMANCE_AUDIT
                                    }
#endif
                                }
                                add_timing(result.timing.observation_us, observation_start, Clock::now(), instrumentation);
                            }
                        }
                        const auto trace_record_prefix_start = Clock::now();
                        auto step = ygo::trace::make_decision_step(index, process_result.message, request,
#ifdef YGO_M4_PERFORMANCE_AUDIT
                                                                   public_state_hash_audited(host, request.player,
                                                                                             &performance_audit));
#else
                                                                   public_state_hash(host, request.player));
#endif
                        if (has_observation) {
                            ygo::trace::attach_observation_metadata(step, observation);
                        }
                        step.decision_index = decision_index++;
                        step.selected_semantic_key = selected.semantic_key;
                        add_timing(result.timing.trace_hash_us, trace_record_prefix_start, Clock::now(), instrumentation);
                        if (request.continuation.has_value()) {
                            const auto response_start = Clock::now();
                            const auto transition = ygo::protocol::apply_continuation_action(request, selected.semantic_key);
                            const auto response_end = Clock::now();
                            const auto response_build_time_us = elapsed_us(response_start, response_end);
                            response_build_time_us_total += response_build_time_us;
                            response_build_time_us_max = std::max(response_build_time_us_max, response_build_time_us);
                            add_timing(result.timing.continuation_us, response_start, response_end, instrumentation);
                            const auto trace_record_suffix_start = Clock::now();
                            step.engine_advanced = transition.engine_advanced;
                            if (!transition.engine_response.empty()) {
                                step.selected_response_sha256 = ygo::trace::sha256_bytes(transition.engine_response);
                            }
                            if (!transition.terminal) {
                                ++continuation_intermediate_count;
                                trace.steps.push_back(std::move(step));
                                add_timing(result.timing.trace_hash_us, trace_record_suffix_start, Clock::now(), instrumentation);
                                request = std::move(transition.request);
                                continue;
                            }
                            step.final_engine_response_hash = ygo::trace::sha256_bytes(transition.engine_response);
                            step.selected_response_sha256 = step.final_engine_response_hash;
                            trace.steps.push_back(std::move(step));
                            add_timing(result.timing.trace_hash_us, trace_record_suffix_start, Clock::now(), instrumentation);
                            host.submit_response(transition.engine_response);
                        } else {
                            const auto trace_record_suffix_start = Clock::now();
                            step.engine_advanced = true;
                            step.selected_response_sha256 = ygo::trace::sha256_bytes(selected.exact_response_bytes);
                            step.final_engine_response_hash = step.selected_response_sha256;
                            trace.steps.push_back(std::move(step));
                            add_timing(result.timing.trace_hash_us, trace_record_suffix_start, Clock::now(), instrumentation);
                            host.submit_response(selected.exact_response_bytes);
                        }
                        break;
                    }
                } catch (const ygo::protocol::ProtocolError& error) {
                    emit_unsupported_diagnostic(error, index, process_result.message, host_config, deck_a, deck_b, trace);
                    add_protocol_failure(result, error);
                    throw;
                }
            }

            if (!forced_unsupported && !job.replay_actions.empty() &&
                replay_action_index != job.replay_actions.size()) {
                throw std::runtime_error("replay action stream contains unused semantic actions: " +
                                         std::to_string(job.replay_actions.size() - replay_action_index));
            }

            result.engine_steps = static_cast<std::uint32_t>(host.process_call_count());
            result.interactive_decisions = interactive_decision_count;
            result.semantic_action_count = semantic_action_count;
            result.terminal = terminal_reached;
            if (terminal_reached) {
                result.winner = terminal_winner;
                result.win_reason = terminal_reason;
            } else if (!forced_unsupported) {
                result.failure_code = "nonterminal";
                result.error_message = "canonical simulation did not reach terminal state before max_steps";
            }
            result.turns = 0;
            for (const auto& event : observation_sessions[0].visible_events()) {
                if (event.kind == ygo::observation::VisibleEventKind::TurnStarted) {
                    ++result.turns;
                }
            }
            for (const auto& step : trace.steps) {
                if (step.decision_request_kind == "battle_command") {
                    ++result.battle_command_count;
                }
            }
            for (const auto& event : observation_sessions[0].visible_events()) {
                if (event.kind == ygo::observation::VisibleEventKind::LifePointsChanged) {
                    ++result.visible_life_points_event_count;
                } else if (event.kind == ygo::observation::VisibleEventKind::CardDestroyed) {
                    ++result.visible_destroyed_event_count;
                } else if (event.kind == ygo::observation::VisibleEventKind::Win) {
                    ++result.visible_win_event_count;
                }
            }
            result.observation_entity_total = observation_entity_total;
            result.observation_event_total = observation_event_total;
            result.candidate_count_mean = result.operations.candidate_sets == 0
                                              ? 0.0
                                              : static_cast<double>(candidate_count_total) /
                                                    static_cast<double>(result.operations.candidate_sets);
            result.response_build_time_us_total = response_build_time_us_total;
            result.response_build_time_us_max = response_build_time_us_max;
            result.continuation_intermediate_steps = continuation_intermediate_count;
            const auto metrics = host.metrics();
            result.operations.ocg_duel_process = metrics.duel_process_calls;
            result.operations.ocg_duel_query = metrics.duel_query_calls;
            result.operations.ocg_duel_query_location = metrics.duel_query_location_calls;
            result.operations.ocg_duel_query_field = metrics.duel_query_field_calls;
            result.operations.ocg_duel_query_count = metrics.duel_query_count_calls;
            result.operations.script_reader_requests = metrics.script_reader_requests;
            result.operations.script_loads = metrics.script_loads;
#ifdef YGO_M4_PERFORMANCE_AUDIT
            performance_audit.set_script_metrics(metrics.script_loads, metrics.script_reader_requests);
#endif

            const auto trace_start = Clock::now();
            result.gameplay_hash = ygo::trace::semantic_gameplay_hash(trace);
            ++result.operations.semantic_hashes;
            add_timing(result.timing.trace_hash_us, trace_start, Clock::now(), instrumentation);
            if (job.mode == SimulationMode::Conformance) {
                const auto serialization_start = Clock::now();
                const auto serialized = ygo::trace::canonical_trace_jsonl_v2(trace);
                add_timing(result.timing.serialization_us, serialization_start, Clock::now(), instrumentation);
                const auto hash_start = Clock::now();
                result.trace_hash = ygo::trace::canonical_trace_hash_v2(trace);
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
    } catch (const ygo::protocol::ProtocolError& error) {
        result.pass = false;
        if (result.failure_code.empty()) {
            add_protocol_failure(result, error);
        }
        record_simulation_timing();
        record_persistence_timing();
    } catch (const ygo::core::CoreError& error) {
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
