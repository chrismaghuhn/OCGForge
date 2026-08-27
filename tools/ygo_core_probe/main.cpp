#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <exception>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
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
#include "ygo/simulation/canonical_simulation.hpp"
#include "ygo/trace/engine_trace.hpp"
#include "ygo/trace/sha256.hpp"

#ifndef YGO_M0_PLAYER_A
#error "YGO_M0_PLAYER_A must be supplied by CMake"
#endif
#ifndef YGO_M0_PLAYER_B
#error "YGO_M0_PLAYER_B must be supplied by CMake"
#endif
#ifndef YGO_M3_DECK_A
#error "YGO_M3_DECK_A must be supplied by CMake"
#endif
#ifndef YGO_M3_DECK_B
#error "YGO_M3_DECK_B must be supplied by CMake"
#endif
#ifndef YGO_M3_CARDSCRIPTS
#error "YGO_M3_CARDSCRIPTS must be supplied by CMake"
#endif

namespace {

std::string trim_replay_action_line(std::string line) {
    const auto first = line.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return {};
    }
    const auto last = line.find_last_not_of(" \t\r\n");
    return line.substr(first, last - first + 1);
}

struct Arguments {
    std::uint64_t seed = 0x0123456789abcdefULL;
    std::string output;
    std::uint32_t max_steps = 512;
    bool force_unsupported = false;
    bool m3_fixed_matchup = false;
    bool m3_full_game = false;
    bool mirror_seats = false;
    std::optional<std::uint8_t> starting_player;
    std::string replay_actions_path;
    std::string setup_script;
    std::vector<std::uint32_t> focus_codes;
};

std::uint64_t parse_u64(const std::string& value) {
    std::size_t consumed = 0;
    const auto result = std::stoull(value, &consumed, 0);
    if (consumed != value.size()) {
        throw std::runtime_error("invalid seed: " + value);
    }
    return static_cast<std::uint64_t>(result);
}

std::vector<std::uint32_t> parse_codes(const std::string& value) {
    std::vector<std::uint32_t> result;
    std::size_t start = 0;
    while (start < value.size()) {
        const auto end = value.find(',', start);
        const auto token = value.substr(start, end == std::string::npos ? std::string::npos : end - start);
        if (token.empty()) {
            throw std::runtime_error("invalid focus code list: " + value);
        }
        result.push_back(static_cast<std::uint32_t>(std::stoul(token, nullptr, 0)));
        if (end == std::string::npos) {
            break;
        }
        start = end + 1;
    }
    return result;
}

Arguments parse_arguments(int argc, char** argv) {
    Arguments arguments;
    for (int index = 1; index < argc; ++index) {
        const std::string argument(argv[index]);
        if (argument == "--seed" && index + 1 < argc) {
            arguments.seed = parse_u64(argv[++index]);
        } else if (argument == "--output" && index + 1 < argc) {
            arguments.output = argv[++index];
        } else if (argument == "--max-steps" && index + 1 < argc) {
            arguments.max_steps = static_cast<std::uint32_t>(std::stoul(argv[++index]));
        } else if (argument == "--force-unsupported") {
            arguments.force_unsupported = true;
        } else if (argument == "--m3-fixed-matchup") {
            arguments.m3_fixed_matchup = true;
        } else if (argument == "--m3-full-game") {
            arguments.m3_fixed_matchup = true;
            arguments.m3_full_game = true;
        } else if (argument == "--mirror-seats") {
            arguments.mirror_seats = true;
        } else if (argument == "--starting-player" && index + 1 < argc) {
            const auto value = std::stoul(argv[++index]);
            if (value > 1) {
                throw std::runtime_error("starting player must be 0 or 1");
            }
            arguments.starting_player = static_cast<std::uint8_t>(value);
        } else if (argument == "--replay-actions" && index + 1 < argc) {
            arguments.replay_actions_path = argv[++index];
        } else if (argument == "--setup" && index + 1 < argc) {
            arguments.setup_script = argv[++index];
        } else if (argument == "--focus" && index + 1 < argc) {
            arguments.focus_codes = parse_codes(argv[++index]);
        } else {
            throw std::runtime_error(
                "usage: ygo_core_probe [--seed N] [--output PATH] [--max-steps N] [--force-unsupported] [--m3-fixed-matchup] [--m3-full-game] [--mirror-seats] [--starting-player 0|1] [--replay-actions PATH] [--setup PATH] [--focus CODE[,CODE...]]");
        }
    }
    return arguments;
}

const ygo::protocol::ActionCandidate& choose_candidate(const ygo::protocol::DecisionRequest& request,
                                                       const ygo::core::SeedBundle& seed,
                                                       const std::vector<std::uint32_t>& focus_codes) {
    auto choose_min = [](const std::vector<const ygo::protocol::ActionCandidate*>& candidates)
        -> const ygo::protocol::ActionCandidate& {
        return **std::min_element(candidates.begin(), candidates.end(),
                                  [](const auto* left, const auto* right) {
                                      return left->semantic_key < right->semantic_key;
                                  });
    };
    std::vector<const ygo::protocol::ActionCandidate*> candidates;
    for (const auto& candidate : request.candidates) {
        candidates.push_back(&candidate);
    }
    for (const auto focus_code : focus_codes) {
        const auto focused = std::find_if(candidates.begin(), candidates.end(), [focus_code](const auto* candidate) {
            return candidate->source_card == focus_code || candidate->target_card == focus_code;
        });
        if (focused != candidates.end() && request.kind != ygo::protocol::DecisionRequestKind::YesNo) {
            return **focused;
        }
    }
    if (request.kind == ygo::protocol::DecisionRequestKind::IdleCommand) {
        for (const auto command : {0u, 6u, 7u, 8u, 1u, 2u, 3u, 4u, 5u}) {
            std::vector<const ygo::protocol::ActionCandidate*> matching;
            for (const auto* candidate : candidates) {
                if (candidate->phase == command) {
                    matching.push_back(candidate);
                }
            }
            if (!matching.empty()) {
                if (command == 0 && matching.size() > 1) {
                    std::sort(matching.begin(), matching.end(),
                              [](const auto* left, const auto* right) {
                                  return left->semantic_key < right->semantic_key;
                              });
                    return *matching[seed.words[0] % matching.size()];
                }
                return choose_min(matching);
            }
        }
    } else if (request.kind == ygo::protocol::DecisionRequestKind::BattleCommand) {
        for (const auto command : {1u, 2u, 3u, 0u}) {
            std::vector<const ygo::protocol::ActionCandidate*> matching;
            for (const auto* candidate : candidates) {
                if (candidate->phase == command) {
                    matching.push_back(candidate);
                }
            }
            if (!matching.empty()) {
                return choose_min(matching);
            }
        }
    } else if (request.kind == ygo::protocol::DecisionRequestKind::Chain) {
        for (const auto* candidate : candidates) {
            if (candidate->semantic_key == "chain.pass") {
                return *candidate;
            }
        }
        return choose_min(candidates);
    } else if (request.kind == ygo::protocol::DecisionRequestKind::CardSelection) {
        std::vector<const ygo::protocol::ActionCandidate*> card_candidates;
        for (const auto* candidate : candidates) {
            if (candidate->semantic_key != "card.cancel") {
                card_candidates.push_back(candidate);
            }
        }
        return choose_min(card_candidates.empty() ? candidates : card_candidates);
    } else if (request.kind == ygo::protocol::DecisionRequestKind::Place) {
        return choose_min(candidates);
    } else if (request.kind == ygo::protocol::DecisionRequestKind::Position) {
        for (const auto* candidate : candidates) {
            if (candidate->position == POS_FACEUP_ATTACK) {
                return *candidate;
            }
        }
    } else if (request.kind == ygo::protocol::DecisionRequestKind::YesNo) {
        for (const auto* candidate : candidates) {
            if (candidate->semantic_key == "yes_no.yes") {
                return *candidate;
            }
        }
    } else if (request.kind == ygo::protocol::DecisionRequestKind::UnselectCard) {
        for (const auto* candidate : candidates) {
            if (candidate->action_kind != ygo::protocol::ActionKind::Cancel &&
                candidate->action_kind != ygo::protocol::ActionKind::Finish) {
                return *candidate;
            }
        }
    }
    return choose_min(candidates);
}

std::string public_state_hash(const ygo::core::CoreHost& host, std::uint8_t perspective) {
    auto field = host.query_field();
    field.push_back(perspective);
    return ygo::trace::sha256_bytes(field);
}

ygo::trace::TraceManifest manifest(const ygo::core::CoreHost& host,
                                   const ygo::core::FixtureDeck& deck_a,
                                   const ygo::core::FixtureDeck& deck_b,
                                   const std::string& policy,
                                   bool canonical_m3) {
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
    if (canonical_m3) {
        result.format_id = std::string(ygo::m3::canonical_rules().format_id);
        result.duel_mode_name = std::string(ygo::m3::canonical_rules().duel_mode_name);
    }
    result.seed_bundle = host.config().seed.words;
    result.fixture_deck_hashes = {deck_a.sha256, deck_b.sha256};
    result.policy_identifier = policy;
    return result;
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

std::string raw_hex(const std::vector<std::uint8_t>& bytes) {
    static constexpr char hex[] = "0123456789abcdef";
    std::string result;
    result.reserve(bytes.size() * 2);
    for (const auto byte : bytes) {
        result.push_back(hex[byte >> 4]);
        result.push_back(hex[byte & 0x0f]);
    }
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

std::string canonical_summary_json(const ygo::simulation::SimulationResult& result,
                                   const ygo::simulation::CanonicalSimulationConfig& config,
                                   const ygo::simulation::SimulationJob& job) {
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

int run_canonical_full_game(const Arguments& arguments) {
    ygo::simulation::CanonicalSimulationConfig config;
    config.rules.card_scripts_root = YGO_M3_CARDSCRIPTS;
    config.rules.card_data_tsv = YGO_M0_CARD_DATA_TSV;
    config.rules.bundle_id = std::string(ygo::m3::canonical_rules().rules_bundle_id);
    config.rules.core_patchset_id = std::string(ygo::m3::canonical_rules().core_patchset_id);
    config.rules.core_patchset_sha256 = std::string(ygo::m3::canonical_rules().core_patchset_sha256);
    config.duel_flags = ygo::m3::canonical_rules().duel_flags;
    config.deck_a = ygo::core::load_fixture_deck(YGO_M3_DECK_A);
    config.deck_b = ygo::core::load_fixture_deck(YGO_M3_DECK_B);
    config.required_script_codes =
        ygo::core::canonical_required_script_codes(config.deck_a, config.deck_b);
    config.mode = ygo::simulation::SimulationMode::Conformance;
    config.observation_mode = ygo::simulation::ObservationMode::Full;

    ygo::simulation::SimulationJob job;
    job.job_id = "probe-m3-full-game";
    job.seed = arguments.seed;
    job.seat_assignment = arguments.mirror_seats ? ygo::simulation::SeatAssignment::Mirror
                                                  : ygo::simulation::SeatAssignment::Normal;
    job.starting_player = arguments.starting_player.value_or(0);
    job.max_steps = arguments.max_steps;
    job.focus_codes = arguments.focus_codes;
    job.setup_script = arguments.setup_script;
    job.force_unsupported = arguments.force_unsupported;
    job.mode = ygo::simulation::SimulationMode::Conformance;
    job.observation_mode = ygo::simulation::ObservationMode::Full;
    job.persist_trace = !arguments.output.empty();
    job.trace_output = arguments.output;
    if (!arguments.replay_actions_path.empty()) {
        std::ifstream replay_stream(arguments.replay_actions_path, std::ios::binary);
        if (!replay_stream) {
            throw std::runtime_error("cannot open replay actions: " + arguments.replay_actions_path);
        }
        std::string line;
        while (std::getline(replay_stream, line)) {
            line = trim_replay_action_line(std::move(line));
            if (!line.empty() && line.front() != '#') {
                job.replay_actions.push_back(std::move(line));
            }
        }
    }

    const auto result = ygo::simulation::run_canonical_simulation(job, config);
    if (!result.pass) {
        if (result.failure_code == "nonterminal") {
            // The legacy probe treats max_steps exhaustion as a valid, emitted
            // partial trace rather than as a protocol failure.
        } else if (result.failure_code == "forced_unsupported") {
            return 3;
        } else if (result.failure_code == "retry" || result.errors.retries != 0) {
            std::cerr << "probe error: " << result.error_message << '\n';
            return 5;
        } else if (result.errors.core_errors != 0) {
            std::cerr << "core error: " << result.error_message << '\n';
            return 4;
        } else if (result.errors.unsupported != 0 || result.errors.truncated != 0) {
            std::cerr << "protocol error: " << result.error_message << '\n';
            return 3;
        } else {
            std::cerr << "probe error: " << result.error_message << '\n';
            return 5;
        }
    }
    if (!result.trace_jsonl.has_value() || !result.trace_hash.has_value()) {
        std::cerr << "probe error: conformance result omitted canonical trace evidence\n";
        return 5;
    }

    const auto summary = canonical_summary_json(result, config, job);
    if (arguments.output.empty()) {
        std::cout << *result.trace_jsonl;
        std::cout << "SEMANTIC_GAMEPLAY_HASH " << result.gameplay_hash << "\n";
        std::cout << "TRACE_HASH " << *result.trace_hash << "\n";
        std::cout << "M3_SUMMARY " << summary << "\n";
    }
    std::cerr << "SEMANTIC_GAMEPLAY_HASH " << result.gameplay_hash << "\n";
    std::cerr << "TRACE_HASH " << *result.trace_hash << "\n";
    std::cerr << "M3_SUMMARY " << summary << "\n";
    std::cerr << "CONTINUATION_RESPONSE_BUILD_TIME_US total=" << result.response_build_time_us_total
              << " max=" << result.response_build_time_us_max << "\n";
    return 0;
}

int run(const Arguments& arguments) {
    if (arguments.m3_full_game) {
        return run_canonical_full_game(arguments);
    }
    ygo::core::CoreHostConfig config;
    config.rules.card_scripts_root = arguments.m3_fixed_matchup ? YGO_M3_CARDSCRIPTS : YGO_M0_CARDSCRIPTS;
    config.rules.card_data_tsv = YGO_M0_CARD_DATA_TSV;
    config.rules.bundle_id = std::string(ygo::m3::canonical_rules().rules_bundle_id);
    config.rules.core_patchset_id = std::string(ygo::m3::canonical_rules().core_patchset_id);
    config.rules.core_patchset_sha256 = std::string(ygo::m3::canonical_rules().core_patchset_sha256);
    config.duel_flags = ygo::m3::canonical_rules().duel_flags;
    config.starting_player = arguments.starting_player;
    config.seed = ygo::core::derive_seed_bundle(arguments.seed);
    const auto fixed_deck_a = ygo::core::load_fixture_deck(
        arguments.m3_fixed_matchup ? YGO_M3_DECK_A : YGO_M0_PLAYER_A);
    const auto fixed_deck_b = ygo::core::load_fixture_deck(
        arguments.m3_fixed_matchup ? YGO_M3_DECK_B : YGO_M0_PLAYER_B);
    const auto& deck_a = arguments.mirror_seats && arguments.m3_fixed_matchup ? fixed_deck_b : fixed_deck_a;
    const auto& deck_b = arguments.mirror_seats && arguments.m3_fixed_matchup ? fixed_deck_a : fixed_deck_b;
    if (arguments.m3_fixed_matchup) {
        config.required_script_codes = ygo::core::canonical_required_script_codes(deck_a, deck_b);
    }

    ygo::core::CoreHost host(config);
    host.load_deck(0, deck_a);
    host.load_deck(1, deck_b);
    host.start_duel();
    if (!arguments.setup_script.empty()) {
        host.load_fixture_script(arguments.setup_script);
    }

    ygo::observation::ObservationSession observation_sessions[] = {
        ygo::observation::ObservationSession(0, static_cast<std::uint32_t>(config.duel_flags)),
        ygo::observation::ObservationSession(1, static_cast<std::uint32_t>(config.duel_flags)),
    };

    ygo::trace::EngineTrace trace;
    trace.manifest = manifest(host, deck_a, deck_b,
                              arguments.m3_fixed_matchup ? "m3.deterministic_conformance.v1"
                                                         : "m0.deterministic_priority.seeded_tie.v1",
                              arguments.m3_fixed_matchup);
    const ygo::m3::DeterministicConformancePolicy m3_policy(arguments.focus_codes, false);
    std::vector<std::string> replay_actions;
    if (!arguments.replay_actions_path.empty()) {
        std::ifstream replay_stream(arguments.replay_actions_path, std::ios::binary);
        if (!replay_stream) {
            throw std::runtime_error("cannot open replay actions: " + arguments.replay_actions_path);
        }
        std::string line;
        while (std::getline(replay_stream, line)) {
            line = trim_replay_action_line(std::move(line));
            if (!line.empty() && line.front() != '#') {
                replay_actions.push_back(std::move(line));
            }
        }
    }
    std::size_t replay_action_index = 0;
    trace.manifest.trace_schema_version = "ygo.engine_trace.v2";
    if (arguments.force_unsupported) {
        const std::vector<std::uint8_t> raw_message = {
            3, 0, 0, 0, MSG_SELECT_OPTION, 0, 0,
        };
        try {
            (void)ygo::protocol::decode_messages(raw_message);
            throw std::runtime_error("forced unsupported probe unexpectedly decoded successfully");
        } catch (const ygo::protocol::ProtocolError& error) {
            emit_unsupported_diagnostic(error, 0, raw_message, config, deck_a, deck_b, trace);
            return 3;
        }
    }
    std::uint32_t decision_index = 0;
    std::uint32_t interactive_decision_count = 0;
    std::uint32_t continuation_intermediate_count = 0;
    std::uint64_t candidate_count_total = 0;
    std::size_t candidate_count_max = 0;
    std::uint64_t observation_entity_total = 0;
    std::uint64_t observation_event_total = 0;
    std::optional<std::uint8_t> first_player;
    bool terminal_reached = false;
    std::uint8_t terminal_winner = 255;
    std::uint8_t terminal_reason = 255;
    std::uint64_t response_build_time_us_total = 0;
    std::uint64_t response_build_time_us_max = 0;
    for (std::uint32_t index = 0; index < arguments.max_steps; ++index) {
        const auto result = host.process();
        try {
            observation_sessions[0].ingest(result.message, index);
            observation_sessions[1].ingest(result.message, index);
        } catch (const std::exception& error) {
            std::cerr << "observation error at engine_step=" << index << ": " << error.what()
                      << " raw=" << raw_hex(result.message) << '\n';
            throw;
        }
        try {
            const auto decoded = ygo::protocol::decode_messages(result.message, index);
            if (decoded.retry) {
                throw std::runtime_error("pinned core emitted MSG_RETRY after a submitted response");
            }
            if (decoded.terminal) {
                ygo::trace::TraceStep terminal;
                terminal.step_index = index;
                terminal.decision_index = decision_index++;
                terminal.engine_step_index = index;
                terminal.raw_message_length = static_cast<std::uint32_t>(result.message.size());
                terminal.raw_message_sha256 = ygo::trace::sha256_bytes(result.message);
                terminal.public_state_hash = public_state_hash(host, 0);
                ygo::observation::ObservationBuildConfig observation_config;
                observation_config.decision_index = decision_index;
                observation_config.engine_step_index = index;
                observation_config.visible_events = observation_sessions[0].visible_events();
                observation_config.knowledge.own_decklist_known = true;
                observation_config.own_deck.known = true;
                observation_config.own_deck.main_deck = deck_a.main_deck;
                const auto observation = ygo::observation::build_player_observation(host, 0, observation_config);
                ygo::trace::attach_observation_metadata(terminal, observation);
                observation_entity_total += observation.entities.size();
                observation_event_total += observation.visible_events.size();
                terminal.engine_advanced = true;
                terminal.terminal = true;
                terminal.winner = decoded.winner;
                terminal.win_reason = decoded.win_reason;
                trace.steps.push_back(std::move(terminal));
                terminal_reached = true;
                terminal_winner = decoded.winner;
                terminal_reason = decoded.win_reason;
                break;
            }
            if (!decoded.interactive || decoded.decisions.empty()) {
                continue;
            }
            if (decoded.decisions.size() != 1) {
                throw ygo::protocol::ProtocolError(ygo::protocol::ProtocolErrorCode::UnsupportedDecision,
                                                    "more than one interactive message in a process result");
            }
            auto request = decoded.decisions.front();
            ++interactive_decision_count;
            if (!first_player.has_value()) {
                first_player = request.player;
            }
            for (;;) {
                ygo::protocol::validate_candidate_set(request);
                candidate_count_total += request.candidates.size();
                candidate_count_max = std::max(candidate_count_max, request.candidates.size());
                const ygo::protocol::ActionCandidate* selected_candidate = nullptr;
                if (!replay_actions.empty()) {
                    if (replay_action_index >= replay_actions.size()) {
                        throw ygo::protocol::ProtocolError(
                            ygo::protocol::ProtocolErrorCode::InvalidSemanticKey,
                            "replay action stream ended before the engine reached terminal state",
                            request.engine_message_type, request.player);
                    }
                    selected_candidate = &ygo::protocol::select_candidate(
                        request, replay_actions[replay_action_index++]);
                } else {
                    selected_candidate = arguments.m3_fixed_matchup
                                             ? &m3_policy.choose(request)
                                             : &choose_candidate(request, config.seed, arguments.focus_codes);
                }
                const auto& selected = *selected_candidate;
                ygo::observation::ObservationBuildConfig observation_config;
                observation_config.decision_index = decision_index;
                observation_config.engine_step_index = request.engine_step_index;
                observation_config.visible_events = observation_sessions[request.player].visible_events();
                observation_config.knowledge.own_decklist_known = true;
                observation_config.own_deck.known = true;
                observation_config.own_deck.main_deck = request.player == 0 ? deck_a.main_deck : deck_b.main_deck;
                auto observation = ygo::observation::build_player_observation(host, request.player,
                                                                               observation_config);
                ygo::observation::attach_decision_context(observation, request);
                observation_entity_total += observation.entities.size();
                observation_event_total += observation.visible_events.size();
                if (arguments.m3_fixed_matchup) {
                    for (const auto& candidate_item : request.candidates) {
                        const bool has_snapshot_locator =
                            (candidate_item.source_card != 0 && candidate_item.source_location != 0 &&
                             candidate_item.source_location != LOCATION_DECK &&
                             candidate_item.source_location != LOCATION_EXTRA) ||
                            (candidate_item.target_card != 0 && candidate_item.target_location != 0 &&
                             candidate_item.target_location != LOCATION_DECK &&
                             candidate_item.target_location != LOCATION_EXTRA);
                        if (has_snapshot_locator &&
                            !ygo::observation::candidate_observation_consistent(observation, candidate_item)) {
                            throw ygo::protocol::ProtocolError(
                                ygo::protocol::ProtocolErrorCode::UnsupportedDecision,
                                "visible M3 candidate does not resolve against PlayerObservation: " +
                                    candidate_item.semantic_key,
                                request.engine_message_type, request.player);
                        }
                    }
                }
                auto step = ygo::trace::make_decision_step(index, result.message, request,
                                                           public_state_hash(host, request.player));
                ygo::trace::attach_observation_metadata(step, observation);
                step.decision_index = decision_index++;
                step.selected_semantic_key = selected.semantic_key;
                if (request.continuation.has_value()) {
                    const auto response_start = std::chrono::steady_clock::now();
                    const auto transition = ygo::protocol::apply_continuation_action(request, selected.semantic_key);
                    const auto response_end = std::chrono::steady_clock::now();
                    const auto response_build_time_us = static_cast<std::uint64_t>(
                        std::chrono::duration_cast<std::chrono::microseconds>(response_end - response_start).count());
                    response_build_time_us_total += response_build_time_us;
                    response_build_time_us_max = std::max(response_build_time_us_max, response_build_time_us);
                    step.engine_advanced = transition.engine_advanced;
                    if (!transition.engine_response.empty()) {
                        step.selected_response_sha256 = ygo::trace::sha256_bytes(transition.engine_response);
                    }
                    if (!transition.terminal) {
                        ++continuation_intermediate_count;
                        trace.steps.push_back(std::move(step));
                        request = std::move(transition.request);
                        continue;
                    }
                    step.final_engine_response_hash = ygo::trace::sha256_bytes(transition.engine_response);
                    step.selected_response_sha256 = step.final_engine_response_hash;
                    trace.steps.push_back(std::move(step));
                    host.submit_response(transition.engine_response);
                } else {
                    step.engine_advanced = true;
                    step.selected_response_sha256 = ygo::trace::sha256_bytes(selected.exact_response_bytes);
                    step.final_engine_response_hash = step.selected_response_sha256;
                    trace.steps.push_back(std::move(step));
                    host.submit_response(selected.exact_response_bytes);
                }
                break;
            }
        } catch (const ygo::protocol::ProtocolError& error) {
            emit_unsupported_diagnostic(error, index, result.message, config, deck_a, deck_b, trace);
            return 3;
        }
    }

    if (!replay_actions.empty() && replay_action_index != replay_actions.size()) {
        throw std::runtime_error("replay action stream contains unused semantic actions: " +
                                 std::to_string(replay_actions.size() - replay_action_index));
    }

    const auto serialized = ygo::trace::canonical_trace_jsonl_v2(trace);
    const auto hash = ygo::trace::canonical_trace_hash_v2(trace);
    const auto semantic_hash = ygo::trace::semantic_gameplay_hash(trace);
    std::size_t turn_count = 0;
    for (const auto& event : observation_sessions[0].visible_events()) {
        if (event.kind == ygo::observation::VisibleEventKind::TurnStarted) {
            ++turn_count;
        }
    }
    std::size_t battle_command_count = 0;
    for (const auto& step : trace.steps) {
        if (step.decision_request_kind == "battle_command") {
            ++battle_command_count;
        }
    }
    std::size_t visible_life_points_event_count = 0;
    std::size_t visible_destroyed_event_count = 0;
    std::size_t visible_win_event_count = 0;
    for (const auto& event : observation_sessions[0].visible_events()) {
        if (event.kind == ygo::observation::VisibleEventKind::LifePointsChanged) {
            ++visible_life_points_event_count;
        } else if (event.kind == ygo::observation::VisibleEventKind::CardDestroyed) {
            ++visible_destroyed_event_count;
        } else if (event.kind == ygo::observation::VisibleEventKind::Win) {
            ++visible_win_event_count;
        }
    }
    const auto candidate_count_mean = interactive_decision_count == 0
                                          ? 0.0
                                          : static_cast<double>(candidate_count_total) /
                                                static_cast<double>(interactive_decision_count);
    const auto effective_starting_player = config.effective_starting_player();
    std::ostringstream summary;
    summary << "{\"schema_version\":\"ocgforge.m3.game_summary.v1\",\"format_id\":"
            << json_escape(trace.manifest.format_id) << ",\"duel_mode_name\":"
            << json_escape(trace.manifest.duel_mode_name) << ",\"duel_flags\":"
            << trace.manifest.duel_flags << ",\"rules_bundle_id\":"
            << json_escape(trace.manifest.rules_bundle_id) << ",\"core_patchset_id\":"
            << json_escape(trace.manifest.core_patchset_id) << ",\"core_patchset_sha256\":"
            << json_escape(trace.manifest.core_patchset_sha256) << ",\"terminal\":"
            << (terminal_reached ? "true" : "false") << ",\"winner\":"
            << static_cast<unsigned>(terminal_winner) << ",\"win_reason\":"
            << static_cast<unsigned>(terminal_reason) << ",\"starting_player\":"
            << static_cast<unsigned>(effective_starting_player)
            << ",\"engine_steps\":" << host.process_call_count()
            << ",\"turns\":" << turn_count
            << ",\"battle_command_count\":" << battle_command_count
            << ",\"visible_life_points_event_count\":" << visible_life_points_event_count
            << ",\"visible_destroyed_event_count\":" << visible_destroyed_event_count
            << ",\"visible_win_event_count\":" << visible_win_event_count
            << ",\"interactive_decisions\":" << interactive_decision_count
            << ",\"continuation_intermediate_steps\":" << continuation_intermediate_count
            << ",\"candidate_count_max\":" << candidate_count_max
            << ",\"candidate_count_mean\":" << candidate_count_mean
            << ",\"observation_entity_total\":" << observation_entity_total
            << ",\"observation_event_total\":" << observation_event_total
            << ",\"unsupported_count\":0,\"retry_count\":0,\"automatic_decision_count\":0"
            << ",\"candidate_truncation_count\":0,\"core_error_count\":0,\"semantic_gameplay_hash\":"
            << json_escape(semantic_hash) << ",\"trace_hash\":" << json_escape(hash) << "}";
    if (arguments.output.empty()) {
        std::cout << serialized;
        std::cout << "SEMANTIC_GAMEPLAY_HASH " << semantic_hash << "\n";
        std::cout << "TRACE_HASH " << hash << "\n";
        if (arguments.m3_fixed_matchup) {
            std::cout << "M3_SUMMARY " << summary.str() << "\n";
        }
    } else {
        std::ofstream stream(arguments.output, std::ios::binary);
        if (!stream) {
            throw std::runtime_error("cannot open trace output: " + arguments.output);
        }
        stream << serialized;
        stream << "# semantic_gameplay_hash=" << semantic_hash << "\n";
        stream << "# trace_hash=" << hash << "\n";
        if (arguments.m3_fixed_matchup) {
            stream << "# m3_summary=" << summary.str() << "\n";
        }
    }
    std::cerr << "SEMANTIC_GAMEPLAY_HASH " << semantic_hash << "\n";
    std::cerr << "TRACE_HASH " << hash << "\n";
    if (arguments.m3_fixed_matchup) {
        std::cerr << "M3_SUMMARY " << summary.str() << "\n";
    }
    std::cerr << "CONTINUATION_RESPONSE_BUILD_TIME_US total=" << response_build_time_us_total
              << " max=" << response_build_time_us_max << "\n";
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        return run(parse_arguments(argc, argv));
    } catch (const ygo::protocol::ProtocolError& error) {
        std::cerr << "protocol error: " << error.what() << '\n';
        return 3;
    } catch (const ygo::core::CoreError& error) {
        std::cerr << "core error: " << error.what() << '\n';
        return 4;
    } catch (const std::exception& error) {
        std::cerr << "probe error: " << error.what() << '\n';
        return 5;
    }
}
