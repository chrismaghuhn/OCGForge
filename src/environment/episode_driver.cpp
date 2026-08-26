#include "ygo/environment/episode_driver.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <exception>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "ocgapi_constants.h"
#include "ygo/core/core_error.hpp"
#include "ygo/core/core_host.hpp"
#include "ygo/m3/canonical_rules.hpp"
#include "ygo/observation/decision_integration.hpp"
#include "ygo/observation/observation_builder.hpp"
#include "ygo/observation/observation_session.hpp"
#include "ygo/protocol/message_decoder.hpp"
#include "ygo/protocol/protocol_error.hpp"
#include "ygo/trace/sha256.hpp"

namespace ygo::environment {
namespace {

using Clock = std::chrono::steady_clock;

std::uint64_t elapsed_us(const Clock::time_point start, const Clock::time_point end) {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(end - start).count());
}

void add_timing(std::uint64_t& bucket, const Clock::time_point start, const Clock::time_point end,
                const bool enabled) {
    if (enabled) {
        bucket += elapsed_us(start, end);
    }
}

core::SeedBundle seed_bundle(const std::uint64_t seed) {
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

#ifndef YGO_M4_PERFORMANCE_AUDIT
std::string public_state_hash(const core::CoreHost& host, const std::uint8_t perspective) {
    auto field = host.query_field();
    field.push_back(perspective);
    return trace::sha256_bytes(field);
}
#else
std::string public_state_hash(const core::CoreHost& host, const std::uint8_t perspective,
                              observation::PerformanceAuditCollector* audit) {
    observation::PerformanceAuditCollector::AuxiliaryScope whole_scope(
        audit, observation::PerformanceAuditAuxiliaryBucket::PublicStateHash);
    observation::PerformanceAuditCollector::AuxiliaryScope query_scope(
        audit, observation::PerformanceAuditAuxiliaryBucket::PublicStateHashQueryField);
    if (audit != nullptr) {
        audit->record_query_field_call(false);
    }
    auto field = host.query_field();
    field.push_back(perspective);
    return trace::sha256_bytes(field);
}
#endif

trace::TraceManifest make_manifest(const core::CoreHost& host, const core::FixtureDeck& deck_a,
                                   const core::FixtureDeck& deck_b) {
    trace::TraceManifest result;
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
    result.format_id = std::string(m3::canonical_rules().format_id);
    result.duel_mode_name = std::string(m3::canonical_rules().duel_mode_name);
    result.seed_bundle = host.config().seed.words;
    result.fixture_deck_hashes = {deck_a.sha256, deck_b.sha256};
    result.policy_identifier = "m3.deterministic_conformance.v1";
    result.trace_schema_version = "ygo.engine_trace.v2";
    return result;
}

void emit_unsupported_diagnostic(const protocol::ProtocolError& error, const std::uint32_t step_index,
                                const std::vector<std::uint8_t>& raw_message,
                                const core::CoreHost& host, const core::FixtureDeck& deck_a,
                                const core::FixtureDeck& deck_b, const trace::EngineTrace& trace) {
    const auto& config = host.config();
    std::cerr << "UNSUPPORTED_OR_MALFORMED_DECISION {\"message_type\":"
              << static_cast<unsigned>(error.message_type()) << ",\"player\":"
              << static_cast<unsigned>(error.player()) << ",\"step_index\":" << step_index
              << ",\"raw_message_sha256\":" << json_escape(trace::sha256_bytes(raw_message))
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

bool has_snapshot_locator(const protocol::ActionCandidate& candidate) {
    return (candidate.source_card != 0 && candidate.source_location != 0 &&
            candidate.source_location != LOCATION_DECK && candidate.source_location != LOCATION_EXTRA) ||
           (candidate.target_card != 0 && candidate.target_location != 0 &&
            candidate.target_location != LOCATION_DECK && candidate.target_location != LOCATION_EXTRA);
}

}  // namespace

struct EpisodeDriver::Impl final {
    enum class Lifecycle {
        Advancing,
        AwaitingDecision,
        Terminal,
        BudgetExhausted,
        Failed,
    };

    explicit Impl(EpisodeDriverConfig value) : config(std::move(value)) {
        core::CoreHostConfig host_config;
        host_config.rules = config.rules;
        host_config.duel_flags = config.duel_flags;
        host_config.starting_player = config.starting_player;
        host_config.seed = seed_bundle(config.seed);
        host_config.required_script_codes = config.required_script_codes;
#ifdef YGO_M4_PERFORMANCE_AUDIT
        host_config.performance_audit = config.performance_audit;
#endif

#ifdef YGO_M4_PERFORMANCE_AUDIT
        auto setup_scope = observation::PerformanceAuditCollector::SetupScope(
            config.performance_audit, observation::PerformanceAuditSetupBucket::CoreHostSetup);
#endif
        host = std::make_unique<core::CoreHost>(host_config);
        host->load_deck(0, config.player_zero_deck);
        host->load_deck(1, config.player_one_deck);
        host->start_duel();
        if (!config.fixture_setup_script.empty()) {
#ifdef YGO_M4_PERFORMANCE_AUDIT
            auto fixture_script_scope = observation::PerformanceAuditCollector::SetupScope(
                config.performance_audit, observation::PerformanceAuditSetupBucket::FixtureScriptLoad);
#endif
            host->load_fixture_script(config.fixture_setup_script);
#ifdef YGO_M4_PERFORMANCE_AUDIT
            fixture_script_scope.finish();
#endif
        }
#ifdef YGO_M4_PERFORMANCE_AUDIT
        setup_scope.finish();
#endif

        observation_sessions[0] = std::make_unique<observation::ObservationSession>(
            0, static_cast<std::uint32_t>(config.duel_flags));
        observation_sessions[1] = std::make_unique<observation::ObservationSession>(
            1, static_cast<std::uint32_t>(config.duel_flags));
        trace_state.manifest = make_manifest(*host, config.player_zero_deck, config.player_one_deck);
        refresh_host_metrics();
    }

    EpisodeDriverConfig config;
    std::unique_ptr<core::CoreHost> host;
    std::array<std::unique_ptr<observation::ObservationSession>, 2> observation_sessions;
    std::vector<std::uint8_t> current_raw_message;
    std::optional<protocol::DecisionRequest> current_request;
    std::optional<observation::PlayerObservation> current_observation;
    trace::EngineTrace trace_state;
    DriverMetrics driver_metrics;
    Lifecycle lifecycle = Lifecycle::Advancing;
    std::uint32_t next_process_index = 0;
    std::uint32_t trace_decision_index = 0;

    void refresh_derived_metrics() noexcept {
        driver_metrics.turns = 0;
        driver_metrics.battle_command_count = 0;
        driver_metrics.visible_life_points_event_count = 0;
        driver_metrics.visible_destroyed_event_count = 0;
        driver_metrics.visible_win_event_count = 0;
        if (observation_sessions[0] != nullptr) {
            for (const auto& event : observation_sessions[0]->visible_events()) {
                if (event.kind == observation::VisibleEventKind::TurnStarted) {
                    ++driver_metrics.turns;
                } else if (event.kind == observation::VisibleEventKind::LifePointsChanged) {
                    ++driver_metrics.visible_life_points_event_count;
                } else if (event.kind == observation::VisibleEventKind::CardDestroyed) {
                    ++driver_metrics.visible_destroyed_event_count;
                } else if (event.kind == observation::VisibleEventKind::Win) {
                    ++driver_metrics.visible_win_event_count;
                }
            }
        }
        for (const auto& step : trace_state.steps) {
            if (step.decision_request_kind == "battle_command") {
                ++driver_metrics.battle_command_count;
            }
        }
    }

    void refresh_host_metrics() noexcept {
        if (host == nullptr) {
            refresh_derived_metrics();
            return;
        }
        driver_metrics.process_call_count = host->process_call_count();
        driver_metrics.response_submission_count = host->response_submission_count();
        const auto core_metrics = host->metrics();
        driver_metrics.operations.ocg_duel_process = core_metrics.duel_process_calls;
        driver_metrics.operations.ocg_duel_query = core_metrics.duel_query_calls;
        driver_metrics.operations.ocg_duel_query_location = core_metrics.duel_query_location_calls;
        driver_metrics.operations.ocg_duel_query_field = core_metrics.duel_query_field_calls;
        driver_metrics.operations.ocg_duel_query_count = core_metrics.duel_query_count_calls;
        driver_metrics.operations.script_reader_requests = core_metrics.script_reader_requests;
        driver_metrics.operations.script_loads = core_metrics.script_loads;
#ifdef YGO_M4_PERFORMANCE_AUDIT
        if (config.performance_audit != nullptr) {
            config.performance_audit->set_script_metrics(core_metrics.script_loads, core_metrics.script_reader_requests);
        }
#endif
        refresh_derived_metrics();
    }

    DriverFailure failure_value() const {
        return DriverFailure{driver_metrics.errors, failure_code, failure_message};
    }

    std::string failure_code;
    std::string failure_message;

    DriverBoundary close_with_failure(std::string code, std::string message) {
        failure_code = std::move(code);
        failure_message = std::move(message);
        refresh_host_metrics();
        current_request.reset();
        current_observation.reset();
        lifecycle = Lifecycle::Failed;
        host.reset();
        return failure_value();
    }

    DriverBoundary protocol_failure(const protocol::ProtocolError& error) {
        if (error.code() == protocol::ProtocolErrorCode::IncompleteCandidates) {
            ++driver_metrics.errors.truncated;
            return close_with_failure("candidate_truncated", error.what());
        }
        ++driver_metrics.errors.unsupported;
        return close_with_failure("unsupported_decision", error.what());
    }

    void emit_protocol_diagnostic(const protocol::ProtocolError& error) const {
        if (host != nullptr) {
            const auto step_index = next_process_index == 0 ? 0 : next_process_index - 1;
            emit_unsupported_diagnostic(error, step_index, current_raw_message, *host,
                                        config.player_zero_deck, config.player_one_deck, trace_state);
        }
    }

    void publish_current_request() {
        if (!current_request.has_value()) {
            throw std::logic_error("driver has no request to publish");
        }
        protocol::validate_candidate_set(*current_request);
        driver_metrics.operations.candidate_total += current_request->candidates.size();
        driver_metrics.operations.candidate_max =
            std::max<std::uint64_t>(driver_metrics.operations.candidate_max, current_request->candidates.size());
        ++driver_metrics.operations.candidate_sets;

        current_observation.reset();
        if (config.build_full_observation) {
            const auto observation_start = Clock::now();
#ifdef YGO_M4_PERFORMANCE_AUDIT
            auto observation_scope = observation::PerformanceAuditCollector::ObservationScope(
                config.performance_audit);
#endif
            observation::ObservationBuildConfig observation_config;
            observation_config.decision_index = trace_decision_index;
            observation_config.engine_step_index = current_request->engine_step_index;
            observation_config.visible_events =
                observation_sessions[current_request->player]->visible_events();
            observation_config.knowledge.own_decklist_known = true;
            observation_config.own_deck.known = true;
            observation_config.own_deck.main_deck = current_request->player == 0
                                                         ? config.player_zero_deck.main_deck
                                                         : config.player_one_deck.main_deck;
            observation_config.finalization = observation::ObservationFinalization::Deferred;
#ifdef YGO_M4_PERFORMANCE_AUDIT
            observation_config.performance_audit = config.performance_audit;
#endif
            current_observation = observation::build_player_observation(
                *host, current_request->player, observation_config);
            observation::attach_decision_context(*current_observation, *current_request
#ifdef YGO_M4_PERFORMANCE_AUDIT
                                                 , config.performance_audit
#endif
            );
            driver_metrics.observation_entity_total += current_observation->entities.size();
            driver_metrics.observation_event_total += current_observation->visible_events.size();
            ++driver_metrics.operations.observations;
            driver_metrics.operations.entities_projected += current_observation->entities.size();
            for (const auto& candidate : current_request->candidates) {
                if (!has_snapshot_locator(candidate)) {
                    continue;
                }
#ifdef YGO_M4_PERFORMANCE_AUDIT
                auto candidate_scope = config.performance_audit == nullptr
                                           ? observation::PerformanceAuditCollector::Scope()
                                           : config.performance_audit->scope(
                                                 observation::PerformanceAuditBucket::CandidateConsistency);
                const bool consistent = observation::candidate_observation_consistent(
                    *current_observation, candidate, config.performance_audit);
#else
                const bool consistent = observation::candidate_observation_consistent(*current_observation, candidate);
#endif
                if (!consistent) {
                    throw protocol::ProtocolError(
                        protocol::ProtocolErrorCode::UnsupportedDecision,
                        "visible M3 candidate does not resolve against PlayerObservation: " + candidate.semantic_key,
                        current_request->engine_message_type, current_request->player);
                }
            }
            add_timing(driver_metrics.timing.observation_us, observation_start, Clock::now(), config.instrumentation);
        }
        lifecycle = Lifecycle::AwaitingDecision;
    }

    void append_terminal(const protocol::DecodedMessage& decoded, const std::uint32_t engine_step) {
        const auto trace_prefix_start = Clock::now();
        trace::TraceStep terminal;
        terminal.step_index = engine_step;
        terminal.decision_index = trace_decision_index++;
        terminal.engine_step_index = engine_step;
        terminal.raw_message_length = static_cast<std::uint32_t>(current_raw_message.size());
        terminal.raw_message_sha256 = trace::sha256_bytes(current_raw_message);
#ifdef YGO_M4_PERFORMANCE_AUDIT
        terminal.public_state_hash = public_state_hash(*host, 0, config.performance_audit);
#else
        terminal.public_state_hash = public_state_hash(*host, 0);
#endif
        add_timing(driver_metrics.timing.trace_hash_us, trace_prefix_start, Clock::now(), config.instrumentation);
        auto trace_suffix_start = Clock::now();
        if (config.build_full_observation) {
            const auto observation_start = Clock::now();
#ifdef YGO_M4_PERFORMANCE_AUDIT
            auto observation_scope = observation::PerformanceAuditCollector::ObservationScope(
                config.performance_audit);
#endif
            observation::ObservationBuildConfig observation_config;
            observation_config.decision_index = trace_decision_index;
            observation_config.engine_step_index = engine_step;
            observation_config.visible_events = observation_sessions[0]->visible_events();
            observation_config.knowledge.own_decklist_known = true;
            observation_config.own_deck.known = true;
            observation_config.own_deck.main_deck = config.player_zero_deck.main_deck;
#ifdef YGO_M4_PERFORMANCE_AUDIT
            observation_config.performance_audit = config.performance_audit;
#endif
            const auto observation = observation::build_player_observation(*host, 0, observation_config);
            trace::attach_observation_metadata(terminal, observation);
            add_timing(driver_metrics.timing.observation_us, observation_start, Clock::now(), config.instrumentation);
            driver_metrics.observation_entity_total += observation.entities.size();
            driver_metrics.observation_event_total += observation.visible_events.size();
            ++driver_metrics.operations.observations;
            driver_metrics.operations.entities_projected += observation.entities.size();
        }
        trace_suffix_start = Clock::now();
        terminal.engine_advanced = true;
        terminal.terminal = true;
        terminal.winner = decoded.winner;
        terminal.win_reason = decoded.win_reason;
        trace_state.steps.push_back(std::move(terminal));
        add_timing(driver_metrics.timing.trace_hash_us, trace_suffix_start, Clock::now(), config.instrumentation);
        lifecycle = Lifecycle::Terminal;
    }

    DriverBoundary advance_until_boundary() {
        if (lifecycle != Lifecycle::Advancing) {
            throw std::logic_error("EpisodeDriver advance called outside advancing state");
        }
        if (config.force_unsupported_for_test) {
            const std::vector<std::uint8_t> raw_message = {3, 0, 0, 0, MSG_SELECT_OPTION, 0, 0};
            current_raw_message = raw_message;
            try {
                (void)protocol::decode_messages(raw_message);
                return close_with_failure("simulation_error", "forced unsupported probe unexpectedly decoded successfully");
            } catch (const protocol::ProtocolError& error) {
                emit_protocol_diagnostic(error);
                ++driver_metrics.errors.unsupported;
                return close_with_failure("forced_unsupported", error.what());
            }
        }

        while (next_process_index < config.engine_process_budget) {
            const auto process_start = Clock::now();
            core::ProcessResult process_result;
            try {
                process_result = host->process();
            } catch (const core::CoreError& error) {
                ++driver_metrics.errors.core_errors;
                return close_with_failure("core_error", error.what());
            } catch (const std::exception& error) {
                return close_with_failure(failure_code.empty() ? "simulation_error" : failure_code, error.what());
            }
            const auto engine_step = next_process_index++;
            current_raw_message = process_result.message;
            add_timing(driver_metrics.timing.core_process_us, process_start, Clock::now(), config.instrumentation);
            try {
                const auto protocol_start = Clock::now();
                observation_sessions[0]->ingest(current_raw_message, engine_step);
                observation_sessions[1]->ingest(current_raw_message, engine_step);
                const auto decoded = protocol::decode_messages(current_raw_message, engine_step);
                add_timing(driver_metrics.timing.protocol_candidate_us, protocol_start, Clock::now(), config.instrumentation);
                if (decoded.retry) {
                    ++driver_metrics.errors.retries;
                    return close_with_failure("retry", "pinned core emitted MSG_RETRY after a submitted response");
                }
                if (decoded.terminal) {
                    append_terminal(decoded, engine_step);
                    refresh_host_metrics();
                    return DriverGameTerminal{decoded.winner, decoded.win_reason};
                }
                if (!decoded.interactive) {
                    continue;
                }
                if (decoded.decisions.size() != 1) {
                    throw protocol::ProtocolError(
                        protocol::ProtocolErrorCode::UnsupportedDecision,
                        "more than one interactive message in a process result");
                }
                current_request = decoded.decisions.front();
                ++driver_metrics.interactive_decisions;
                publish_current_request();
                refresh_host_metrics();
                return DriverDecisionBoundary{&*current_request,
                                              current_observation.has_value() ? &*current_observation : nullptr};
            } catch (const protocol::ProtocolError& error) {
                emit_protocol_diagnostic(error);
                return protocol_failure(error);
            } catch (const core::CoreError& error) {
                ++driver_metrics.errors.core_errors;
                return close_with_failure("core_error", error.what());
            } catch (const std::exception& error) {
                return close_with_failure(failure_code.empty() ? "simulation_error" : failure_code, error.what());
            }
        }
        lifecycle = Lifecycle::BudgetExhausted;
        refresh_host_metrics();
        return DriverProcessBudgetExceeded{static_cast<std::uint32_t>(driver_metrics.process_call_count)};
    }

    DriverBoundary apply_semantic_key(const std::string& semantic_key) {
        if (lifecycle != Lifecycle::AwaitingDecision || !current_request.has_value() || host == nullptr) {
            throw std::logic_error("EpisodeDriver apply called without an awaiting decision");
        }
        const auto& selected = protocol::select_candidate(*current_request, semantic_key);
        const auto trace_prefix_start = Clock::now();
#ifdef YGO_M4_PERFORMANCE_AUDIT
        auto step = trace::make_decision_step(
            static_cast<std::uint32_t>(next_process_index - 1), current_raw_message, *current_request,
            public_state_hash(*host, current_request->player, config.performance_audit));
#else
        auto step = trace::make_decision_step(
            static_cast<std::uint32_t>(next_process_index - 1), current_raw_message, *current_request,
            public_state_hash(*host, current_request->player));
#endif
        if (current_observation.has_value()) {
            trace::attach_observation_metadata(step, *current_observation);
        }
        step.decision_index = trace_decision_index++;
        step.selected_semantic_key = selected.semantic_key;
        add_timing(driver_metrics.timing.trace_hash_us, trace_prefix_start, Clock::now(), config.instrumentation);
        ++driver_metrics.semantic_action_count;

        try {
            if (current_request->continuation.has_value()) {
                const auto response_start = Clock::now();
                const auto transition = protocol::apply_continuation_action(*current_request, selected.semantic_key);
                const auto response_end = Clock::now();
                const auto response_time = elapsed_us(response_start, response_end);
                driver_metrics.response_build_time_us_total += response_time;
                driver_metrics.response_build_time_us_max =
                    std::max(driver_metrics.response_build_time_us_max, response_time);
                add_timing(driver_metrics.timing.continuation_us, response_start, response_end, config.instrumentation);
                const auto trace_suffix_start = Clock::now();
                step.engine_advanced = transition.engine_advanced;
                if (!transition.engine_response.empty()) {
                    step.selected_response_sha256 = trace::sha256_bytes(transition.engine_response);
                }
                if (!transition.terminal) {
                    ++driver_metrics.continuation_intermediate_steps;
                    trace_state.steps.push_back(std::move(step));
                    add_timing(driver_metrics.timing.trace_hash_us, trace_suffix_start, Clock::now(), config.instrumentation);
                    current_observation.reset();
                    current_request = std::move(transition.request);
                    lifecycle = Lifecycle::Advancing;
                    publish_current_request();
                    refresh_host_metrics();
                    return DriverDecisionBoundary{&*current_request,
                                                  current_observation.has_value() ? &*current_observation : nullptr};
                }
                step.final_engine_response_hash = trace::sha256_bytes(transition.engine_response);
                step.selected_response_sha256 = step.final_engine_response_hash;
                trace_state.steps.push_back(std::move(step));
                add_timing(driver_metrics.timing.trace_hash_us, trace_suffix_start, Clock::now(), config.instrumentation);
                current_request.reset();
                current_observation.reset();
                lifecycle = Lifecycle::Advancing;
                host->submit_response(transition.engine_response);
                refresh_host_metrics();
                return advance_until_boundary();
            }

            const auto response = selected.exact_response_bytes;
            const auto trace_suffix_start = Clock::now();
            step.engine_advanced = true;
            step.selected_response_sha256 = trace::sha256_bytes(response);
            step.final_engine_response_hash = step.selected_response_sha256;
            trace_state.steps.push_back(std::move(step));
            add_timing(driver_metrics.timing.trace_hash_us, trace_suffix_start, Clock::now(), config.instrumentation);
            current_request.reset();
            current_observation.reset();
            lifecycle = Lifecycle::Advancing;
            host->submit_response(response);
            refresh_host_metrics();
            return advance_until_boundary();
        } catch (const protocol::ProtocolError& error) {
            emit_protocol_diagnostic(error);
            return protocol_failure(error);
        } catch (const core::CoreError& error) {
            ++driver_metrics.errors.core_errors;
            return close_with_failure("core_error", error.what());
        } catch (const std::exception& error) {
            return close_with_failure(failure_code.empty() ? "simulation_error" : failure_code, error.what());
        }
    }
};

EpisodeDriver::EpisodeDriver(EpisodeDriverConfig config) : impl_(std::make_unique<Impl>(std::move(config))) {}

EpisodeDriver::~EpisodeDriver() = default;

DriverBoundary EpisodeDriver::advance_until_boundary() { return impl_->advance_until_boundary(); }

DriverBoundary EpisodeDriver::apply_semantic_key(const std::string& semantic_key) {
    return impl_->apply_semantic_key(semantic_key);
}

const trace::EngineTrace& EpisodeDriver::trace() const noexcept { return impl_->trace_state; }

const DriverMetrics& EpisodeDriver::metrics() const noexcept { return impl_->driver_metrics; }

}  // namespace ygo::environment
