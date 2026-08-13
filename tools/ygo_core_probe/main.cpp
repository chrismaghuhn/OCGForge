#include <algorithm>
#include <array>
#include <cstdint>
#include <exception>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

#include "ocgapi_constants.h"
#include "ygo/core/core_error.hpp"
#include "ygo/core/core_host.hpp"
#include "ygo/protocol/message_decoder.hpp"
#include "ygo/protocol/protocol_error.hpp"
#include "ygo/trace/engine_trace.hpp"
#include "ygo/trace/sha256.hpp"

#ifndef YGO_M0_PLAYER_A
#error "YGO_M0_PLAYER_A must be supplied by CMake"
#endif

namespace {

struct Arguments {
    std::uint64_t seed = 0x0123456789abcdefULL;
    std::string output;
    std::uint32_t max_steps = 512;
    bool force_unsupported = false;
};

std::uint64_t parse_u64(const std::string& value) {
    std::size_t consumed = 0;
    const auto result = std::stoull(value, &consumed, 0);
    if (consumed != value.size()) {
        throw std::runtime_error("invalid seed: " + value);
    }
    return static_cast<std::uint64_t>(result);
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
        } else {
            throw std::runtime_error(
                "usage: ygo_core_probe [--seed N] [--output PATH] [--max-steps N] [--force-unsupported]");
        }
    }
    return arguments;
}

ygo::core::SeedBundle seed_bundle(std::uint64_t seed) {
    return {{seed, seed ^ 0x9e3779b97f4a7c15ULL, seed + 0x6a09e667f3bcc909ULL,
             (seed << 1) ^ 0xbb67ae8584caa73bULL}};
}

const ygo::protocol::ActionCandidate& choose_candidate(const ygo::protocol::DecisionRequest& request,
                                                       const ygo::core::SeedBundle& seed) {
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
                                   const std::string& policy) {
    ygo::trace::TraceManifest result;
    result.rules_bundle_id = host.config().rules.bundle_id;
    result.core_repository = host.config().rules.core_repository;
    result.core_commit = host.config().rules.core_commit;
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

int run(const Arguments& arguments) {
    ygo::core::CoreHostConfig config;
    config.rules.card_scripts_root = YGO_M0_CARDSCRIPTS;
    config.rules.card_data_tsv = YGO_M0_CARD_DATA_TSV;
    config.rules.bundle_id = "6fbbd212ae4be2df36170dcbfcdf5c46aaaa0e3091cf815c2d0261fd01640ea4";
    config.seed = seed_bundle(arguments.seed);
    const auto deck_a = ygo::core::load_fixture_deck(YGO_M0_PLAYER_A);
    const auto deck_b = ygo::core::load_fixture_deck(YGO_M0_PLAYER_B);

    ygo::core::CoreHost host(config);
    host.load_deck(0, deck_a);
    host.load_deck(1, deck_b);
    host.start_duel();

    ygo::trace::EngineTrace trace;
    trace.manifest = manifest(host, deck_a, deck_b, "m0.deterministic_priority.seeded_tie.v1");
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
    for (std::uint32_t index = 0; index < arguments.max_steps; ++index) {
        const auto result = host.process();
        try {
            const auto decoded = ygo::protocol::decode_messages(result.message);
            if (decoded.terminal) {
                ygo::trace::TraceStep terminal;
                terminal.step_index = index;
                terminal.raw_message_length = static_cast<std::uint32_t>(result.message.size());
                terminal.raw_message_sha256 = ygo::trace::sha256_bytes(result.message);
                terminal.public_state_hash = public_state_hash(host, 0);
                terminal.terminal = true;
                terminal.winner = decoded.winner;
                terminal.win_reason = decoded.win_reason;
                trace.steps.push_back(std::move(terminal));
                break;
            }
            if (!decoded.interactive || decoded.decisions.empty()) {
                continue;
            }
            if (decoded.decisions.size() != 1) {
                throw ygo::protocol::ProtocolError(ygo::protocol::ProtocolErrorCode::UnsupportedDecision,
                                                    "more than one interactive message in a process result");
            }
            const auto& request = decoded.decisions.front();
            ygo::protocol::validate_candidate_set(request);
            const auto& candidate = choose_candidate(request, config.seed);
            const auto& selected = ygo::protocol::select_candidate(request, candidate.semantic_key);
            auto step = ygo::trace::make_decision_step(index, result.message, request,
                                                       public_state_hash(host, request.player));
            step.selected_semantic_key = selected.semantic_key;
            step.selected_response_sha256 = ygo::trace::sha256_bytes(selected.exact_response_bytes);
            trace.steps.push_back(std::move(step));
            host.submit_response(selected.exact_response_bytes);
        } catch (const ygo::protocol::ProtocolError& error) {
            emit_unsupported_diagnostic(error, index, result.message, config, deck_a, deck_b, trace);
            return 3;
        }
    }

    const auto serialized = ygo::trace::canonical_trace_jsonl(trace);
    const auto hash = ygo::trace::canonical_trace_hash(trace);
    if (arguments.output.empty()) {
        std::cout << serialized;
        std::cout << "TRACE_HASH " << hash << "\n";
    } else {
        std::ofstream stream(arguments.output, std::ios::binary);
        if (!stream) {
            throw std::runtime_error("cannot open trace output: " + arguments.output);
        }
        stream << serialized;
        stream << "# trace_hash=" << hash << "\n";
    }
    std::cerr << "TRACE_HASH " << hash << "\n";
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
