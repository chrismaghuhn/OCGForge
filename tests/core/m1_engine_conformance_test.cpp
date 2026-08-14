#include <algorithm>
#include <array>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "ocgapi_constants.h"
#include "ygo/core/core_error.hpp"
#include "ygo/core/core_host.hpp"
#include "ygo/core/rules_bundle.hpp"
#include "ygo/protocol/action_candidate.hpp"
#include "ygo/protocol/continuation.hpp"
#include "ygo/protocol/message_decoder.hpp"
#include "ygo/protocol/protocol_error.hpp"
#include "ygo/trace/engine_trace.hpp"
#include "ygo/trace/sha256.hpp"

#ifndef YGO_M1_ENGINE_PLAYER_A
#error "YGO_M1_ENGINE_PLAYER_A must be supplied by CMake"
#endif
#ifndef YGO_M1_ENGINE_PLAYER_B
#error "YGO_M1_ENGINE_PLAYER_B must be supplied by CMake"
#endif
#ifndef YGO_M1_ENGINE_CARDSCRIPTS
#error "YGO_M1_ENGINE_CARDSCRIPTS must be supplied by CMake"
#endif
#ifndef YGO_M1_ENGINE_TRACE_DIR
#error "YGO_M1_ENGINE_TRACE_DIR must be supplied by CMake"
#endif

namespace {

using ygo::protocol::ActionCandidate;
using ygo::protocol::ActionKind;
using ygo::protocol::DecisionRequest;
using ygo::protocol::DecisionRequestKind;

constexpr const char* kRulesBundleId =
    "6fbbd212ae4be2df36170dcbfcdf5c46aaaa0e3091cf815c2d0261fd01640ea4";
constexpr std::uint32_t kMultiSelector = 43898403;
constexpr std::uint32_t kTargetSpell = 12148078;
constexpr std::uint32_t kSumSelector = 12148078;
constexpr std::uint32_t kSumLevel1 = 16725505;
constexpr std::uint32_t kSumLevel2 = 96708940;
constexpr std::uint32_t kSumLevel3 = 53932291;
constexpr std::uint32_t kSumLevel4 = 17328157;
constexpr std::uint32_t kSumLevel5 = 21516908;
constexpr std::uint32_t kSortSelector = 47222536;
constexpr std::uint32_t kDisfieldSelector = 90502999;
constexpr std::uint32_t kOptionSelector = 1006081;
constexpr std::uint32_t kOptionTarget = 63977008;
constexpr std::uint32_t kAlienWarrior = 97127906;
constexpr std::uint32_t kAlienDog = 15475415;
constexpr std::uint32_t kAlienTelepath = 91070115;

struct FixtureOutcome {
    std::string fixture_id;
    std::uint8_t message_type = 0;
    std::string message_name;
    std::size_t intermediate_records = 0;
    std::size_t response_calls_for_target = 0;
    std::string engine_state_hash_before;
    std::string engine_state_hash_after;
    bool final_response_accepted = false;
    ygo::trace::EngineTrace trace;
    std::string semantic_hash;
    std::string trace_hash;
};

bool contains_retry(const std::vector<std::uint8_t>& bytes) {
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        if (bytes.size() - offset < 4) {
            throw std::runtime_error("engine output has an incomplete frame length");
        }
        const auto size = static_cast<std::uint32_t>(bytes[offset]) |
                          static_cast<std::uint32_t>(bytes[offset + 1]) << 8 |
                          static_cast<std::uint32_t>(bytes[offset + 2]) << 16 |
                          static_cast<std::uint32_t>(bytes[offset + 3]) << 24;
        offset += 4;
        if (size == 0 || size > bytes.size() - offset) {
            throw std::runtime_error("engine output has an invalid frame length");
        }
        if (bytes[offset] == MSG_RETRY) {
            return true;
        }
        offset += size;
    }
    return false;
}

const ActionCandidate& choose_min(const std::vector<const ActionCandidate*>& candidates) {
    if (candidates.empty()) {
        throw std::runtime_error("fixture policy received an empty candidate domain");
    }
    return **std::min_element(candidates.begin(), candidates.end(), [](const auto* left, const auto* right) {
        return left->semantic_key < right->semantic_key;
    });
}

const ActionCandidate& choose_atomic(const DecisionRequest& request) {
    std::vector<const ActionCandidate*> candidates;
    for (const auto& candidate : request.candidates) {
        candidates.push_back(&candidate);
    }
    if (request.kind == DecisionRequestKind::IdleCommand) {
        for (const auto* candidate : candidates) {
            if (candidate->phase == 0 &&
                (candidate->source_card == kAlienWarrior || candidate->source_card == kOptionSelector)) {
                return *candidate;
            }
        }
        for (const auto* candidate : candidates) {
            if (candidate->phase == 5 &&
                (candidate->source_card == kMultiSelector || candidate->source_card == kSumSelector ||
                 candidate->source_card == kAlienTelepath || candidate->source_card == kSortSelector ||
                 candidate->source_card == kDisfieldSelector || candidate->source_card == kOptionSelector)) {
                return *candidate;
            }
        }
        for (const auto phase : {6u, 7u, 8u, 2u, 3u, 4u, 0u, 1u}) {
            std::vector<const ActionCandidate*> matching;
            for (const auto* candidate : candidates) {
                if (candidate->phase == phase) {
                    matching.push_back(candidate);
                }
            }
            if (!matching.empty()) {
                return choose_min(matching);
            }
        }
    } else if (request.kind == DecisionRequestKind::BattleCommand) {
        for (const auto phase : {2u, 3u, 1u}) {
            std::vector<const ActionCandidate*> matching;
            for (const auto* candidate : candidates) {
                if (candidate->phase == phase) {
                    matching.push_back(candidate);
                }
            }
            if (!matching.empty()) {
                return choose_min(matching);
            }
        }
    } else if (request.kind == DecisionRequestKind::Chain) {
        for (const auto* candidate : candidates) {
            if (candidate->source_card == kAlienDog) {
                return *candidate;
            }
        }
        for (const auto* candidate : candidates) {
            if (candidate->semantic_key == "chain.pass") {
                return *candidate;
            }
        }
    } else if (request.kind == DecisionRequestKind::YesNo) {
        for (const auto* candidate : candidates) {
            if (candidate->semantic_key == "yes_no.no") {
                return *candidate;
            }
        }
    } else if (request.kind == DecisionRequestKind::Position) {
        for (const auto* candidate : candidates) {
            if (candidate->position == POS_FACEUP_ATTACK) {
                return *candidate;
            }
        }
    }
    return choose_min(candidates);
}

const ActionCandidate& choose_continuation(const DecisionRequest& request) {
    for (const auto& candidate : request.candidates) {
        if (candidate.action_kind == ActionKind::Finish) {
            return candidate;
        }
    }
    std::vector<const ActionCandidate*> primitive;
    for (const auto& candidate : request.candidates) {
        if (candidate.action_kind == ActionKind::Pick || candidate.action_kind == ActionKind::AssignAmount) {
            primitive.push_back(&candidate);
        }
    }
    if (!primitive.empty()) {
        return choose_min(primitive);
    }
    throw std::runtime_error("fixture continuation has no legal pick or finish action");
}

const ActionCandidate& choose_target_continuation(const DecisionRequest& request) {
    if (!request.continuation.has_value()) {
        throw std::runtime_error("target continuation request is missing its state");
    }
    if (request.continuation->selected_indices.size() >= 2) {
        for (const auto& candidate : request.candidates) {
            if (candidate.action_kind == ActionKind::Finish) {
                return candidate;
            }
        }
        throw std::runtime_error("target continuation could not finish after selecting two cards");
    }
    std::vector<const ActionCandidate*> primitive;
    for (const auto& candidate : request.candidates) {
        if (candidate.action_kind == ActionKind::Pick) {
            primitive.push_back(&candidate);
        }
    }
    return choose_min(primitive);
}

const ActionCandidate& choose_sum_continuation(const DecisionRequest& request) {
    if (!request.continuation.has_value()) {
        throw std::runtime_error("sum continuation request is missing its state");
    }
    if (!request.continuation->selected_indices.empty()) {
        for (const auto& candidate : request.candidates) {
            if (candidate.action_kind == ActionKind::Finish) {
                return candidate;
            }
        }
    }
    std::vector<const ActionCandidate*> primitive;
    for (const auto& candidate : request.candidates) {
        if (candidate.action_kind == ActionKind::Pick) {
            primitive.push_back(&candidate);
        }
    }
    return choose_min(primitive);
}

void submit_non_target_continuation(ygo::core::CoreHost& host, DecisionRequest request) {
    for (;;) {
        ygo::protocol::validate_candidate_set(request);
        const auto& selected = choose_continuation(request);
        const auto transition = ygo::protocol::apply_continuation_action(request, selected.semantic_key);
        if (transition.engine_advanced != transition.terminal) {
            throw std::runtime_error("non-target continuation has inconsistent engine-advance semantics");
        }
        if (transition.terminal) {
            if (transition.engine_response.empty()) {
                throw std::runtime_error("non-target continuation did not produce a final response");
            }
            host.submit_response(transition.engine_response);
            return;
        }
        if (!transition.engine_response.empty() || transition.request.engine_step_index != request.engine_step_index) {
            throw std::runtime_error("non-target continuation advanced the engine before its final response");
        }
        request = transition.request;
    }
}

ygo::core::CoreHostConfig make_config() {
    ygo::core::CoreHostConfig config;
    config.rules.card_scripts_root = YGO_M1_ENGINE_CARDSCRIPTS;
    config.rules.card_data_tsv = YGO_M0_CARD_DATA_TSV;
    config.rules.bundle_id = kRulesBundleId;
    config.starting_draw_count = 5;
    config.draw_count_per_turn = 0;
    config.seed.words = {0x0123456789abcdefULL, 0xfedcba9876543210ULL,
                         0x13579bdf2468ace0ULL, 0x0eca8642fdb97531ULL};
    return config;
}

ygo::trace::TraceManifest make_manifest(const ygo::core::CoreHostConfig& config,
                                        const ygo::core::FixtureDeck& deck_a,
                                        const ygo::core::FixtureDeck& deck_b,
                                        const std::string& fixture_id) {
    ygo::trace::TraceManifest manifest;
    manifest.trace_schema_version = "ygo.engine_trace.v2";
    manifest.rules_bundle_id = config.rules.bundle_id;
    manifest.core_repository = config.rules.core_repository;
    manifest.core_commit = config.rules.core_commit;
    manifest.cardscripts_repository = config.rules.cardscripts_repository;
    manifest.cardscripts_commit = config.rules.cardscripts_commit;
    manifest.database_repository = config.rules.database_repository;
    manifest.database_commit = config.rules.database_commit;
    manifest.core_api_version = config.rules.core_api_version;
    manifest.compiler_identity = YGO_M0_COMPILER_ID;
    manifest.build_type = YGO_M0_BUILD_TYPE;
    manifest.platform_identity = "windows";
    manifest.duel_flags = config.duel_flags;
    manifest.seed_bundle = config.seed.words;
    manifest.fixture_deck_hashes = {deck_a.sha256, deck_b.sha256};
    manifest.policy_identifier = "m1.1.target-card-then-advance-v1:" + fixture_id;
    return manifest;
}

void write_trace(const FixtureOutcome& outcome) {
    const std::filesystem::path directory = YGO_M1_ENGINE_TRACE_DIR;
    std::filesystem::create_directories(directory);
    std::ofstream stream(directory / (outcome.fixture_id + ".jsonl"), std::ios::binary);
    if (!stream) {
        throw std::runtime_error("cannot write engine trace artifact");
    }
    stream << ygo::trace::canonical_trace_jsonl_v2(outcome.trace);
}

FixtureOutcome run_select_option() {
    const auto config = make_config();
    const auto deck_a = ygo::core::load_fixture_deck(YGO_M1_ENGINE_PLAYER_A);
    const auto deck_b = ygo::core::load_fixture_deck(YGO_M1_ENGINE_PLAYER_B);
    ygo::core::CoreHost host(config);
    host.load_deck(0, deck_a);
    host.load_deck(1, deck_b);
    host.start_duel();
    host.load_fixture_card(0, kOptionSelector, LOCATION_HAND, 0, POS_FACEDOWN_DEFENSE);
    host.load_fixture_card(0, kOptionTarget, LOCATION_MZONE, 0, POS_FACEUP_ATTACK);

    FixtureOutcome outcome;
    outcome.fixture_id = "m1.1.select_option";
    outcome.trace.manifest = make_manifest(config, deck_a, deck_b, outcome.fixture_id);
    bool target_seen = false;
    bool effectyn_seen = false;
    std::string effectyn_raw_message_hash;
    std::string effectyn_response_hash;
    bool awaiting_engine_process = false;
    std::uint32_t target_engine_step = 0;
    std::size_t response_count_before_target = 0;
    std::uint32_t trace_step = 0;
    std::uint32_t decision_index = 0;
    std::vector<std::string> observed;
    constexpr std::uint32_t max_engine_steps = 512;

    for (std::uint32_t engine_step = 0; engine_step < max_engine_steps; ++engine_step) {
        const auto result = host.process();
        if (contains_retry(result.message)) {
            throw std::runtime_error("pinned core emitted MSG_RETRY in select_option fixture");
        }
        if (awaiting_engine_process) {
            if (result.message.empty()) {
                continue;
            }
            outcome.final_response_accepted = true;
            break;
        }

        const auto decoded = ygo::protocol::decode_messages(result.message, engine_step);
        if (decoded.terminal) {
            throw std::runtime_error("select_option reached terminal state before acceptance");
        }
        if (!decoded.interactive || decoded.decisions.empty()) {
            continue;
        }
        if (decoded.decisions.size() != 1) {
            throw std::runtime_error("select_option emitted multiple interactive decisions");
        }
        auto request = decoded.decisions.front();
        ygo::protocol::validate_candidate_set(request);
        if (observed.size() < 32) {
            std::ostringstream description;
            description << static_cast<unsigned>(request.engine_message_type) << ':'
                        << request.engine_message_name << ":kind="
                        << ygo::protocol::decision_kind_name(request.kind) << ":candidates="
                        << request.candidates.size();
            for (const auto& candidate : request.candidates) {
                description << ":card=" << candidate.source_card << ":phase=" << candidate.phase;
            }
            observed.push_back(description.str());
        }
        if (request.engine_message_type != MSG_SELECT_OPTION) {
            if (request.engine_message_type == MSG_SELECT_EFFECTYN) {
                effectyn_seen = true;
                effectyn_raw_message_hash = ygo::trace::sha256_bytes(result.message);
            }
            if (request.continuation.has_value()) {
                submit_non_target_continuation(host, std::move(request));
                continue;
            }
            const ActionCandidate* selected_atomic = nullptr;
            if (request.engine_message_type == MSG_SELECT_EFFECTYN) {
                for (const auto& candidate : request.candidates) {
                    if (candidate.semantic_key == "yes_no.yes") {
                        selected_atomic = &candidate;
                        break;
                    }
                }
                if (selected_atomic == nullptr) {
                    throw std::runtime_error("select_option effect prompt did not expose yes");
                }
            }
            const auto& candidate = selected_atomic == nullptr ? choose_atomic(request) : *selected_atomic;
            if (!candidate.submits_engine_response || candidate.exact_response_bytes.empty()) {
                throw std::runtime_error("select_option non-target atomic candidate cannot submit an exact response");
            }
            if (request.engine_message_type == MSG_SELECT_EFFECTYN) {
                effectyn_response_hash = ygo::trace::sha256_bytes(candidate.exact_response_bytes);
                auto effectyn_trace = ygo::trace::make_decision_step(
                    trace_step++, result.message, request, ygo::trace::sha256_bytes(host.query_field()));
                effectyn_trace.decision_index = decision_index++;
                effectyn_trace.selected_semantic_key = candidate.semantic_key;
                effectyn_trace.engine_advanced = true;
                effectyn_trace.final_engine_response_hash = effectyn_response_hash;
                outcome.trace.steps.push_back(std::move(effectyn_trace));
            }
            host.submit_response(candidate.exact_response_bytes);
            continue;
        }

        if (!target_seen) {
            response_count_before_target = host.response_submission_count();
        }
        target_seen = true;
        outcome.message_type = request.engine_message_type;
        outcome.message_name = request.engine_message_name;
        target_engine_step = static_cast<std::uint32_t>(request.engine_step_index);
        if (request.kind != DecisionRequestKind::Option || request.candidates.size() != 2) {
            throw std::runtime_error("select_option did not expose the expected two-option domain");
        }
        outcome.engine_state_hash_before = ygo::trace::sha256_bytes(host.query_field());
        const auto& selected = choose_atomic(request);
        if (!selected.submits_engine_response || selected.exact_response_bytes.empty()) {
            throw std::runtime_error("select_option candidate did not contain an exact response");
        }
        auto trace_record = ygo::trace::make_decision_step(
            trace_step++, result.message, request, outcome.engine_state_hash_before);
        trace_record.decision_index = decision_index++;
        trace_record.selected_semantic_key = selected.semantic_key;
        trace_record.engine_advanced = true;
        trace_record.final_engine_response_hash = ygo::trace::sha256_bytes(selected.exact_response_bytes);
        outcome.trace.steps.push_back(std::move(trace_record));
        ++outcome.response_calls_for_target;
        host.submit_response(selected.exact_response_bytes);
        awaiting_engine_process = true;
    }

    if (!target_seen) {
        std::ostringstream error;
        error << "select_option did not reach MSG_SELECT_OPTION; observed=";
        for (std::size_t index = 0; index < observed.size(); ++index) {
            if (index != 0) {
                error << ',';
            }
            error << observed[index];
        }
        throw std::runtime_error(error.str());
    }
    if (!effectyn_seen) {
        throw std::runtime_error("select_option did not exercise the real MSG_SELECT_EFFECTYN prompt");
    }
    const auto actual_response_calls_for_target =
        host.response_submission_count() - response_count_before_target;
    if (actual_response_calls_for_target != 1 || outcome.response_calls_for_target != 1) {
        throw std::runtime_error("select_option did not submit exactly one final response");
    }
    outcome.response_calls_for_target = actual_response_calls_for_target;
    outcome.engine_state_hash_after = ygo::trace::sha256_bytes(host.query_field());
    if (outcome.engine_state_hash_after != outcome.engine_state_hash_before) {
        throw std::runtime_error("select_option engine state changed before final engine process");
    }
    if (!outcome.final_response_accepted) {
        throw std::runtime_error("select_option final response was not followed by engine output");
    }
    if (outcome.trace.steps.empty() || outcome.trace.steps.back().engine_step_index != target_engine_step) {
        throw std::runtime_error("select_option trace lost the original engine step");
    }
    outcome.semantic_hash = ygo::trace::semantic_gameplay_hash(outcome.trace);
    outcome.trace_hash = ygo::trace::canonical_trace_hash_v2(outcome.trace);
    write_trace(outcome);

    std::cout << "fixture_id=" << outcome.fixture_id << '\n'
              << "message_type=" << static_cast<unsigned>(outcome.message_type) << '\n'
              << "message_name=" << outcome.message_name << '\n'
              << "intermediate_continuation_records=" << outcome.intermediate_records << '\n'
              << "engine_state_hash_before=" << outcome.engine_state_hash_before << '\n'
              << "engine_state_hash_after=" << outcome.engine_state_hash_after << '\n'
              << "ocg_duel_set_response_calls=" << outcome.response_calls_for_target << '\n'
              << "final_response_accepted=" << (outcome.final_response_accepted ? "true" : "false") << '\n'
              << "effectyn_observed=true\n"
              << "effectyn_raw_message_sha256=" << effectyn_raw_message_hash << '\n'
              << "effectyn_response_sha256=" << effectyn_response_hash << '\n'
              << "option_count=2\n"
              << "semantic_gameplay_hash=" << outcome.semantic_hash << '\n'
              << "trace_hash=" << outcome.trace_hash << '\n';
    return outcome;
}

FixtureOutcome run_select_card_multi() {
    const auto config = make_config();
    const auto deck_a = ygo::core::load_fixture_deck(YGO_M1_ENGINE_PLAYER_A);
    const auto deck_b = ygo::core::load_fixture_deck(YGO_M1_ENGINE_PLAYER_B);
    ygo::core::CoreHost host(config);
    host.load_deck(0, deck_a);
    host.load_deck(1, deck_b);
    host.start_duel();
    // OCG_DuelNewCard is part of the pinned public setup API. Calling it after
    // OCG_StartDuel queues startup but before the first process preserves the
    // focused hand/field state without modifying core or CardScripts.
    host.load_fixture_card(0, kMultiSelector, LOCATION_HAND, 0, POS_FACEDOWN_DEFENSE);
    host.load_fixture_card(1, kTargetSpell, LOCATION_SZONE, 0, POS_FACEDOWN_DEFENSE);
    host.load_fixture_card(1, kTargetSpell, LOCATION_SZONE, 1, POS_FACEDOWN_DEFENSE);

    FixtureOutcome outcome;
    outcome.fixture_id = "m1.1.select_card_multi";
    outcome.trace.manifest = make_manifest(config, deck_a, deck_b, outcome.fixture_id);
    bool target_seen = false;
    bool awaiting_engine_process = false;
    std::uint32_t target_engine_step = 0;
    std::size_t response_count_before_target = 0;
    std::size_t process_count_at_target = 0;
    std::size_t process_count_before_resume = 0;
    std::uint32_t trace_step = 0;
    std::uint32_t decision_index = 0;
    std::vector<std::string> observed;
    constexpr std::uint32_t max_engine_steps = 512;

    for (std::uint32_t engine_step = 0; engine_step < max_engine_steps; ++engine_step) {
        const auto result = host.process();
        if (contains_retry(result.message)) {
            throw std::runtime_error("pinned core emitted MSG_RETRY in select_card_multi fixture");
        }
        if (awaiting_engine_process) {
            if (result.message.empty()) {
                continue;
            }
            outcome.final_response_accepted = true;
            break;
        }

        const auto decoded = ygo::protocol::decode_messages(result.message, engine_step);
        if (decoded.terminal) {
            throw std::runtime_error("select_card_multi reached terminal state before acceptance");
        }
        if (!decoded.interactive || decoded.decisions.empty()) {
            continue;
        }
        if (decoded.decisions.size() != 1) {
            throw std::runtime_error("select_card_multi emitted multiple interactive decisions");
        }
        auto request = decoded.decisions.front();
        ygo::protocol::validate_candidate_set(request);
        if (observed.size() < 32) {
            std::ostringstream description;
            description << static_cast<unsigned>(request.engine_message_type) << ':'
                        << request.engine_message_name << ":candidates=" << request.candidates.size();
            if (request.continuation.has_value()) {
                description << ":items=" << request.continuation->items.size() << ":min="
                            << request.continuation->min_count << ":max=" << request.continuation->max_count;
                for (const auto& item : request.continuation->items) {
                    description << ":code=" << item.card.code;
                }
            }
            if (request.kind == DecisionRequestKind::IdleCommand) {
                for (const auto& candidate : request.candidates) {
                    description << ":idle=" << candidate.phase << '/' << candidate.source_card;
                }
            }
            observed.push_back(description.str());
        }

        const bool is_target_select_card =
            request.engine_message_type == MSG_SELECT_CARD && request.continuation.has_value() &&
            request.continuation->items.size() == 2 && request.continuation->min_count == 1 &&
            request.continuation->max_count == 2 &&
            std::all_of(request.continuation->items.begin(), request.continuation->items.end(),
                        [](const auto& item) {
                            return item.card.controller == 1 && item.card.location == LOCATION_SZONE;
                        });
        if (!is_target_select_card) {
            if (request.continuation.has_value()) {
                submit_non_target_continuation(host, std::move(request));
                continue;
            }
            const auto& candidate = choose_atomic(request);
            if (!candidate.submits_engine_response || candidate.exact_response_bytes.empty()) {
                throw std::runtime_error("non-target atomic candidate cannot submit an exact response");
            }
            host.submit_response(candidate.exact_response_bytes);
            continue;
        }

        if (!target_seen) {
            response_count_before_target = host.response_submission_count();
            process_count_at_target = host.process_call_count();
        }
        target_seen = true;
        outcome.message_type = request.engine_message_type;
        outcome.message_name = request.engine_message_name;
        target_engine_step = static_cast<std::uint32_t>(request.engine_step_index);
        if (!request.continuation.has_value()) {
            throw std::runtime_error("multi-card MSG_SELECT_CARD was decoded as atomic");
        }
        const auto& original = *request.continuation;
        if (original.items.size() <= 1 || original.max_count <= 1) {
            throw std::runtime_error("select_card_multi did not emit a real multi-card constraint");
        }
        if (original.items.size() != 2 || original.min_count != 1 || original.max_count != 2) {
            throw std::runtime_error("select_card_multi fixture contract changed unexpectedly: items=" +
                                     std::to_string(original.items.size()) + " min=" +
                                     std::to_string(original.min_count) + " max=" +
                                     std::to_string(original.max_count));
        }
        if (std::any_of(request.candidates.begin(), request.candidates.end(), [](const auto& candidate) {
                return candidate.action_kind == ActionKind::Finish;
            })) {
            throw std::runtime_error("FINISH was exposed before the minimum card count was selected");
        }

        outcome.engine_state_hash_before = ygo::trace::sha256_bytes(host.query_field());
        for (;;) {
            ygo::protocol::validate_candidate_set(request);
            const auto& selected = choose_target_continuation(request);
            const auto transition = ygo::protocol::apply_continuation_action(request, selected.semantic_key);
            auto trace_record = ygo::trace::make_decision_step(
                trace_step++, result.message, request, ygo::trace::sha256_bytes(host.query_field()));
            trace_record.decision_index = decision_index++;
            trace_record.selected_semantic_key = selected.semantic_key;
            trace_record.engine_advanced = transition.engine_advanced;
            if (transition.terminal) {
                trace_record.final_engine_response_hash = ygo::trace::sha256_bytes(transition.engine_response);
            } else if (!transition.engine_response.empty() || transition.engine_advanced) {
                throw std::runtime_error("intermediate continuation produced an engine response or advance");
            }
            outcome.trace.steps.push_back(std::move(trace_record));

            if (!transition.terminal) {
                ++outcome.intermediate_records;
                if (transition.request.engine_step_index != request.engine_step_index) {
                    throw std::runtime_error("intermediate continuation changed engine step index");
                }
                if (host.process_call_count() != process_count_at_target) {
                    throw std::runtime_error("intermediate continuation called OCG_DuelProcess");
                }
                const auto after_hash = ygo::trace::sha256_bytes(host.query_field());
                if (after_hash != outcome.engine_state_hash_before) {
                    throw std::runtime_error("engine state changed during an adapter-local continuation choice");
                }
                if (outcome.intermediate_records == 1 &&
                    !std::any_of(transition.request.candidates.begin(), transition.request.candidates.end(),
                                 [](const auto& candidate) { return candidate.action_kind == ActionKind::Finish; })) {
                    throw std::runtime_error("FINISH was not exposed after the first legal card selection");
                }
                request = transition.request;
                continue;
            }
            if (!transition.engine_advanced || transition.engine_response.empty()) {
                throw std::runtime_error("final continuation did not construct an exact response");
            }
            ++outcome.response_calls_for_target;
            host.submit_response(transition.engine_response);
            process_count_before_resume = host.process_call_count();
            awaiting_engine_process = true;
            target_engine_step = static_cast<std::uint32_t>(request.engine_step_index);
            break;
        }
    }

    if (!target_seen) {
        std::ostringstream error;
        error << "select_card_multi did not reach MSG_SELECT_CARD; observed=";
        for (std::size_t index = 0; index < observed.size(); ++index) {
            if (index != 0) {
                error << ',';
            }
            error << observed[index];
        }
        throw std::runtime_error(error.str());
    }
    if (outcome.intermediate_records == 0) {
        throw std::runtime_error("select_card_multi did not produce an intermediate continuation record");
    }
    const auto actual_response_calls_for_target =
        host.response_submission_count() - response_count_before_target;
    if (actual_response_calls_for_target != 1 || outcome.response_calls_for_target != 1) {
        throw std::runtime_error("select_card_multi did not submit exactly one final response");
    }
    outcome.response_calls_for_target = actual_response_calls_for_target;
    if (outcome.engine_state_hash_before.empty()) {
        throw std::runtime_error("select_card_multi did not capture the pre-continuation engine state hash");
    }
    outcome.engine_state_hash_after = ygo::trace::sha256_bytes(host.query_field());
    if (process_count_before_resume != process_count_at_target) {
        throw std::runtime_error("select_card_multi called OCG_DuelProcess during continuation");
    }
    if (outcome.engine_state_hash_after != outcome.engine_state_hash_before) {
        throw std::runtime_error("select_card_multi engine state hash changed before final engine process");
    }
    if (!outcome.final_response_accepted) {
        throw std::runtime_error("select_card_multi final response was not followed by engine output");
    }
    if (outcome.trace.steps.empty() || outcome.trace.steps.back().engine_step_index != target_engine_step) {
        throw std::runtime_error("select_card_multi trace lost the original engine step");
    }
    outcome.semantic_hash = ygo::trace::semantic_gameplay_hash(outcome.trace);
    outcome.trace_hash = ygo::trace::canonical_trace_hash_v2(outcome.trace);
    write_trace(outcome);

    std::cout << "fixture_id=" << outcome.fixture_id << '\n'
              << "message_type=" << static_cast<unsigned>(outcome.message_type) << '\n'
              << "message_name=" << outcome.message_name << '\n'
              << "intermediate_continuation_records=" << outcome.intermediate_records << '\n'
              << "engine_state_hash_before=" << outcome.engine_state_hash_before << '\n'
              << "engine_state_hash_after=" << outcome.engine_state_hash_after << '\n'
              << "ocg_duel_set_response_calls=" << outcome.response_calls_for_target << '\n'
              << "final_response_accepted=" << (outcome.final_response_accepted ? "true" : "false") << '\n'
              << "semantic_gameplay_hash=" << outcome.semantic_hash << '\n'
              << "trace_hash=" << outcome.trace_hash << '\n';
    return outcome;
}

FixtureOutcome run_select_sum() {
    const auto config = make_config();
    const auto deck_a = ygo::core::load_fixture_deck(YGO_M1_ENGINE_PLAYER_A);
    const auto deck_b = ygo::core::load_fixture_deck(YGO_M1_ENGINE_PLAYER_B);
    ygo::core::CoreHost host(config);
    host.load_deck(0, deck_a);
    host.load_deck(1, deck_b);
    host.start_duel();
    host.load_fixture_card(0, kSumSelector, LOCATION_HAND, 0, POS_FACEDOWN_DEFENSE);
    host.load_fixture_card(0, kSumLevel1, LOCATION_HAND, 1, POS_FACEDOWN_DEFENSE);
    host.load_fixture_card(0, kSumLevel2, LOCATION_HAND, 2, POS_FACEDOWN_DEFENSE);
    host.load_fixture_card(0, kSumLevel3, LOCATION_HAND, 3, POS_FACEDOWN_DEFENSE);
    host.load_fixture_card(0, kSumLevel3, LOCATION_HAND, 4, POS_FACEDOWN_DEFENSE);
    host.load_fixture_card(0, kSumLevel4, LOCATION_HAND, 5, POS_FACEDOWN_DEFENSE);
    host.load_fixture_card(0, kSumLevel5, LOCATION_HAND, 6, POS_FACEDOWN_DEFENSE);

    FixtureOutcome outcome;
    outcome.fixture_id = "m1.1.select_sum";
    outcome.trace.manifest = make_manifest(config, deck_a, deck_b, outcome.fixture_id);
    bool target_seen = false;
    bool awaiting_engine_process = false;
    std::uint32_t target_engine_step = 0;
    std::size_t response_count_before_target = 0;
    std::size_t process_count_at_target = 0;
    std::size_t process_count_before_resume = 0;
    std::uint32_t trace_step = 0;
    std::uint32_t decision_index = 0;
    std::uint32_t sum_target = 0;
    std::uint32_t sum_min = 0;
    std::uint32_t sum_max = 0;
    std::size_t sum_mandatory_count = 0;
    bool sum_exact = false;
    bool sum_greater = false;
    std::vector<std::pair<std::uint32_t, std::uint32_t>> sum_optional_values;
    constexpr std::uint32_t max_engine_steps = 512;

    for (std::uint32_t engine_step = 0; engine_step < max_engine_steps; ++engine_step) {
        const auto result = host.process();
        if (contains_retry(result.message)) {
            throw std::runtime_error("pinned core emitted MSG_RETRY in select_sum fixture");
        }
        if (awaiting_engine_process) {
            if (result.message.empty()) {
                continue;
            }
            outcome.final_response_accepted = true;
            break;
        }

        const auto decoded = ygo::protocol::decode_messages(result.message, engine_step);
        if (decoded.terminal) {
            throw std::runtime_error("select_sum reached terminal state before acceptance");
        }
        if (!decoded.interactive || decoded.decisions.empty()) {
            continue;
        }
        if (decoded.decisions.size() != 1) {
            throw std::runtime_error("select_sum emitted multiple interactive decisions");
        }
        auto request = decoded.decisions.front();
        ygo::protocol::validate_candidate_set(request);
        const bool is_target_sum = request.engine_message_type == MSG_SELECT_SUM && request.continuation.has_value();
        if (!is_target_sum) {
            if (request.continuation.has_value()) {
                submit_non_target_continuation(host, std::move(request));
                continue;
            }
            const auto& candidate = choose_atomic(request);
            if (!candidate.submits_engine_response || candidate.exact_response_bytes.empty()) {
                throw std::runtime_error("select_sum non-target atomic candidate cannot submit an exact response");
            }
            host.submit_response(candidate.exact_response_bytes);
            continue;
        }

        if (!target_seen) {
            response_count_before_target = host.response_submission_count();
            process_count_at_target = host.process_call_count();
        }
        target_seen = true;
        outcome.message_type = request.engine_message_type;
        outcome.message_name = request.engine_message_name;
        target_engine_step = static_cast<std::uint32_t>(request.engine_step_index);
        const auto& original = *request.continuation;
        if (original.items.empty() || original.target_sum == 0 ||
            std::any_of(original.items.begin(), original.items.end(),
                        [](const auto& item) { return item.primary_value == 0; })) {
            throw std::runtime_error("select_sum did not expose complete positive contribution values");
        }
        if (original.min_count > original.max_count ||
            std::any_of(original.mandatory_items.begin(), original.mandatory_items.end(), [](const auto& item) {
                return item.card.code == 0 || item.primary_value == 0;
            })) {
            throw std::runtime_error("select_sum did not expose valid mandatory items or count bounds");
        }
        if (original.exact_sum == original.greater_sum) {
            throw std::runtime_error("select_sum did not expose a pinned sum mode");
        }
        sum_target = original.target_sum;
        sum_min = original.min_count;
        sum_max = original.max_count;
        sum_mandatory_count = original.mandatory_items.size();
        sum_exact = original.exact_sum;
        sum_greater = original.greater_sum;
        for (const auto& item : original.items) {
            sum_optional_values.emplace_back(item.primary_value, item.secondary_value);
        }
        outcome.engine_state_hash_before = ygo::trace::sha256_bytes(host.query_field());
        for (;;) {
            ygo::protocol::validate_candidate_set(request);
            const auto& selected = choose_sum_continuation(request);
            const auto transition = ygo::protocol::apply_continuation_action(request, selected.semantic_key);
            auto trace_record = ygo::trace::make_decision_step(
                trace_step++, result.message, request, ygo::trace::sha256_bytes(host.query_field()));
            trace_record.decision_index = decision_index++;
            trace_record.selected_semantic_key = selected.semantic_key;
            trace_record.engine_advanced = transition.engine_advanced;
            if (transition.terminal) {
                trace_record.final_engine_response_hash = ygo::trace::sha256_bytes(transition.engine_response);
            } else if (!transition.engine_response.empty() || transition.engine_advanced) {
                throw std::runtime_error("select_sum intermediate continuation advanced the engine");
            }
            outcome.trace.steps.push_back(std::move(trace_record));

            if (!transition.terminal) {
                ++outcome.intermediate_records;
                if (transition.request.engine_step_index != request.engine_step_index) {
                    throw std::runtime_error("select_sum intermediate changed engine step index");
                }
                if (host.process_call_count() != process_count_at_target) {
                    throw std::runtime_error("select_sum intermediate called OCG_DuelProcess");
                }
                if (ygo::trace::sha256_bytes(host.query_field()) != outcome.engine_state_hash_before) {
                    throw std::runtime_error("select_sum engine state changed during local continuation");
                }
                request = transition.request;
                continue;
            }
            if (!transition.engine_advanced || transition.engine_response.empty()) {
                throw std::runtime_error("select_sum final continuation did not construct an exact response");
            }
            ++outcome.response_calls_for_target;
            host.submit_response(transition.engine_response);
            process_count_before_resume = host.process_call_count();
            awaiting_engine_process = true;
            break;
        }
    }

    if (!target_seen) {
        throw std::runtime_error("select_sum did not reach MSG_SELECT_SUM");
    }
    if (outcome.intermediate_records == 0) {
        throw std::runtime_error("select_sum did not produce an intermediate continuation record");
    }
    const auto actual_response_calls_for_target =
        host.response_submission_count() - response_count_before_target;
    if (actual_response_calls_for_target != 1 || outcome.response_calls_for_target != 1) {
        throw std::runtime_error("select_sum did not submit exactly one final response");
    }
    outcome.response_calls_for_target = actual_response_calls_for_target;
    outcome.engine_state_hash_after = ygo::trace::sha256_bytes(host.query_field());
    if (process_count_before_resume != process_count_at_target) {
        throw std::runtime_error("select_sum called OCG_DuelProcess during continuation");
    }
    if (outcome.engine_state_hash_after != outcome.engine_state_hash_before) {
        throw std::runtime_error("select_sum engine state changed before final engine process");
    }
    if (!outcome.final_response_accepted) {
        throw std::runtime_error("select_sum final response was not followed by engine output");
    }
    if (outcome.trace.steps.empty() || outcome.trace.steps.back().engine_step_index != target_engine_step) {
        throw std::runtime_error("select_sum trace lost the original engine step");
    }
    outcome.semantic_hash = ygo::trace::semantic_gameplay_hash(outcome.trace);
    outcome.trace_hash = ygo::trace::canonical_trace_hash_v2(outcome.trace);
    write_trace(outcome);

    std::cout << "fixture_id=" << outcome.fixture_id << '\n'
              << "message_type=" << static_cast<unsigned>(outcome.message_type) << '\n'
              << "message_name=" << outcome.message_name << '\n'
              << "intermediate_continuation_records=" << outcome.intermediate_records << '\n'
              << "engine_state_hash_before=" << outcome.engine_state_hash_before << '\n'
              << "engine_state_hash_after=" << outcome.engine_state_hash_after << '\n'
              << "ocg_duel_set_response_calls=" << outcome.response_calls_for_target << '\n'
              << "final_response_accepted=" << (outcome.final_response_accepted ? "true" : "false") << '\n'
              << "sum_mode=" << (sum_exact ? "exact" : (sum_greater ? "greater" : "unknown")) << '\n'
              << "sum_target=" << sum_target << '\n'
              << "sum_min=" << sum_min << '\n'
              << "sum_max=" << sum_max << '\n'
              << "sum_mandatory_count=" << sum_mandatory_count << '\n'
              << "sum_optional_values=";
    for (std::size_t index = 0; index < sum_optional_values.size(); ++index) {
        if (index != 0) {
            std::cout << ',';
        }
        std::cout << sum_optional_values[index].first << ':' << sum_optional_values[index].second;
    }
    std::cout << '\n'
              << "semantic_gameplay_hash=" << outcome.semantic_hash << '\n'
              << "trace_hash=" << outcome.trace_hash << '\n';
    return outcome;
}

FixtureOutcome run_select_counter() {
    const auto config = make_config();
    const auto deck_a = ygo::core::load_fixture_deck(YGO_M1_ENGINE_PLAYER_A);
    const auto deck_b = ygo::core::load_fixture_deck(YGO_M1_ENGINE_PLAYER_B);
    ygo::core::CoreHost host(config);
    host.load_deck(0, deck_a);
    host.load_deck(1, deck_b);
    host.start_duel();
    host.load_fixture_card(0, kAlienWarrior, LOCATION_HAND, 0, POS_FACEDOWN_DEFENSE);
    host.load_fixture_card(0, kAlienDog, LOCATION_HAND, 1, POS_FACEDOWN_DEFENSE);
    host.load_fixture_card(0, kAlienTelepath, LOCATION_MZONE, 0, POS_FACEUP_ATTACK);
    host.load_fixture_card(1, 3732747, LOCATION_MZONE, 0, POS_FACEUP_ATTACK);
    host.load_fixture_card(1, 3606209, LOCATION_MZONE, 1, POS_FACEUP_ATTACK);
    host.load_fixture_card(1, kTargetSpell, LOCATION_SZONE, 0, POS_FACEDOWN_DEFENSE);

    FixtureOutcome outcome;
    outcome.fixture_id = "m1.1.select_counter";
    outcome.trace.manifest = make_manifest(config, deck_a, deck_b, outcome.fixture_id);
    bool target_seen = false;
    bool awaiting_engine_process = false;
    std::uint32_t target_engine_step = 0;
    std::size_t response_count_before_target = 0;
    std::size_t process_count_at_target = 0;
    std::size_t process_count_before_resume = 0;
    std::uint32_t trace_step = 0;
    std::uint32_t decision_index = 0;
    std::vector<std::string> observed;
    std::size_t alien_counter_selection_calls = 0;
    std::uint32_t counter_required = 0;
    std::vector<std::uint32_t> counter_capacities;
    constexpr std::uint32_t max_engine_steps = 512;

    for (std::uint32_t engine_step = 0; engine_step < max_engine_steps; ++engine_step) {
        const auto result = host.process();
        if (contains_retry(result.message)) {
            throw std::runtime_error("pinned core emitted MSG_RETRY in select_counter fixture");
        }
        if (awaiting_engine_process) {
            if (result.message.empty()) {
                continue;
            }
            outcome.final_response_accepted = true;
            break;
        }

        const auto decoded = ygo::protocol::decode_messages(result.message, engine_step);
        if (decoded.terminal) {
            throw std::runtime_error("select_counter reached terminal state before acceptance");
        }
        if (!decoded.interactive || decoded.decisions.empty()) {
            continue;
        }
        if (decoded.decisions.size() != 1) {
            throw std::runtime_error("select_counter emitted multiple interactive decisions");
        }
        auto request = decoded.decisions.front();
        ygo::protocol::validate_candidate_set(request);
        if (observed.size() < 40) {
            std::ostringstream description;
            description << static_cast<unsigned>(request.engine_message_type) << ':'
                        << request.engine_message_name << ":candidates=" << request.candidates.size();
            for (const auto& candidate : request.candidates) {
                description << ":candidate=" << candidate.source_card;
            }
            observed.push_back(description.str());
        }
        const bool is_target_counter =
            request.engine_message_type == MSG_SELECT_COUNTER && request.continuation.has_value();
        if (!is_target_counter) {
            if (request.continuation.has_value()) {
                submit_non_target_continuation(host, std::move(request));
                continue;
            }
            const ActionCandidate* selected_atomic = nullptr;
            const bool is_alien_counter_target_selection =
                request.kind == DecisionRequestKind::CardSelection && request.candidates.size() == 2 &&
                std::all_of(request.candidates.begin(), request.candidates.end(), [](const auto& item) {
                    return item.source_controller == 1 && item.source_location == LOCATION_MZONE;
                });
            if (is_alien_counter_target_selection) {
                const auto candidate_it = alien_counter_selection_calls % 2 == 0
                                              ? std::min_element(
                                                    request.candidates.begin(), request.candidates.end(),
                                                    [](const auto& left, const auto& right) {
                                                        return left.semantic_key < right.semantic_key;
                                                    })
                                              : std::max_element(
                                                    request.candidates.begin(), request.candidates.end(),
                                                    [](const auto& left, const auto& right) {
                                                        return left.semantic_key < right.semantic_key;
                                                    });
                selected_atomic = &*candidate_it;
                ++alien_counter_selection_calls;
            }
            const auto& candidate = selected_atomic == nullptr ? choose_atomic(request) : *selected_atomic;
            if (!candidate.submits_engine_response || candidate.exact_response_bytes.empty()) {
                throw std::runtime_error("select_counter non-target atomic candidate cannot submit an exact response");
            }
            host.submit_response(candidate.exact_response_bytes);
            continue;
        }

        if (!target_seen) {
            response_count_before_target = host.response_submission_count();
            process_count_at_target = host.process_call_count();
        }
        target_seen = true;
        outcome.message_type = request.engine_message_type;
        outcome.message_name = request.engine_message_name;
        target_engine_step = static_cast<std::uint32_t>(request.engine_step_index);
        const auto& original = *request.continuation;
        if (original.items.size() != 2 || original.required_amount != 1 ||
            std::any_of(original.items.begin(), original.items.end(),
                        [](const auto& item) { return item.capacity != 1; })) {
            throw std::runtime_error("select_counter did not expose the expected two-card capacity domain");
        }
        counter_required = original.required_amount;
        for (const auto& item : original.items) {
            counter_capacities.push_back(item.capacity);
        }
        outcome.engine_state_hash_before = ygo::trace::sha256_bytes(host.query_field());
        for (;;) {
            ygo::protocol::validate_candidate_set(request);
            const auto& selected = choose_continuation(request);
            const auto transition = ygo::protocol::apply_continuation_action(request, selected.semantic_key);
            auto trace_record = ygo::trace::make_decision_step(
                trace_step++, result.message, request, ygo::trace::sha256_bytes(host.query_field()));
            trace_record.decision_index = decision_index++;
            trace_record.selected_semantic_key = selected.semantic_key;
            trace_record.engine_advanced = transition.engine_advanced;
            if (transition.terminal) {
                trace_record.final_engine_response_hash = ygo::trace::sha256_bytes(transition.engine_response);
            } else if (!transition.engine_response.empty() || transition.engine_advanced) {
                throw std::runtime_error("select_counter intermediate continuation advanced the engine");
            }
            outcome.trace.steps.push_back(std::move(trace_record));

            if (!transition.terminal) {
                ++outcome.intermediate_records;
                if (transition.request.engine_step_index != request.engine_step_index) {
                    throw std::runtime_error("select_counter intermediate changed engine step index");
                }
                if (host.process_call_count() != process_count_at_target) {
                    throw std::runtime_error("select_counter intermediate called OCG_DuelProcess");
                }
                if (ygo::trace::sha256_bytes(host.query_field()) != outcome.engine_state_hash_before) {
                    throw std::runtime_error("select_counter engine state changed during local continuation");
                }
                request = transition.request;
                continue;
            }
            if (!transition.engine_advanced || transition.engine_response.empty()) {
                throw std::runtime_error("select_counter final continuation did not construct an exact response");
            }
            ++outcome.response_calls_for_target;
            host.submit_response(transition.engine_response);
            process_count_before_resume = host.process_call_count();
            awaiting_engine_process = true;
            break;
        }
    }

    if (!target_seen) {
        std::ostringstream error;
        error << "select_counter did not reach MSG_SELECT_COUNTER; observed=";
        for (std::size_t index = 0; index < observed.size(); ++index) {
            if (index != 0) {
                error << ',';
            }
            error << observed[index];
        }
        throw std::runtime_error(error.str());
    }
    if (outcome.intermediate_records == 0) {
        throw std::runtime_error("select_counter did not produce an intermediate continuation record");
    }
    const auto actual_response_calls_for_target =
        host.response_submission_count() - response_count_before_target;
    if (actual_response_calls_for_target != 1 || outcome.response_calls_for_target != 1) {
        throw std::runtime_error("select_counter did not submit exactly one final response");
    }
    outcome.response_calls_for_target = actual_response_calls_for_target;
    outcome.engine_state_hash_after = ygo::trace::sha256_bytes(host.query_field());
    if (process_count_before_resume != process_count_at_target) {
        throw std::runtime_error("select_counter called OCG_DuelProcess during continuation");
    }
    if (outcome.engine_state_hash_after != outcome.engine_state_hash_before) {
        throw std::runtime_error("select_counter engine state changed before final engine process");
    }
    if (!outcome.final_response_accepted) {
        throw std::runtime_error("select_counter final response was not followed by engine output");
    }
    if (outcome.trace.steps.empty() || outcome.trace.steps.back().engine_step_index != target_engine_step) {
        throw std::runtime_error("select_counter trace lost the original engine step");
    }
    outcome.semantic_hash = ygo::trace::semantic_gameplay_hash(outcome.trace);
    outcome.trace_hash = ygo::trace::canonical_trace_hash_v2(outcome.trace);
    write_trace(outcome);

    std::cout << "fixture_id=" << outcome.fixture_id << '\n'
              << "message_type=" << static_cast<unsigned>(outcome.message_type) << '\n'
              << "message_name=" << outcome.message_name << '\n'
              << "intermediate_continuation_records=" << outcome.intermediate_records << '\n'
              << "engine_state_hash_before=" << outcome.engine_state_hash_before << '\n'
              << "engine_state_hash_after=" << outcome.engine_state_hash_after << '\n'
              << "ocg_duel_set_response_calls=" << outcome.response_calls_for_target << '\n'
              << "final_response_accepted=" << (outcome.final_response_accepted ? "true" : "false") << '\n'
              << "counter_required=" << counter_required << '\n'
              << "counter_capacities=";
    for (std::size_t index = 0; index < counter_capacities.size(); ++index) {
        if (index != 0) {
            std::cout << ',';
        }
        std::cout << counter_capacities[index];
    }
    std::cout << '\n'
              << "semantic_gameplay_hash=" << outcome.semantic_hash << '\n'
              << "trace_hash=" << outcome.trace_hash << '\n';
    return outcome;
}

FixtureOutcome run_sort_card() {
    const auto config = make_config();
    const auto deck_a = ygo::core::load_fixture_deck(YGO_M1_ENGINE_PLAYER_A);
    const auto deck_b = ygo::core::load_fixture_deck(YGO_M1_ENGINE_PLAYER_B);
    ygo::core::CoreHost host(config);
    host.load_deck(0, deck_a);
    host.load_deck(1, deck_b);
    host.start_duel();
    host.load_fixture_card(0, kSortSelector, LOCATION_HAND, 0, POS_FACEDOWN_DEFENSE);

    FixtureOutcome outcome;
    outcome.fixture_id = "m1.1.sort_card";
    outcome.trace.manifest = make_manifest(config, deck_a, deck_b, outcome.fixture_id);
    bool target_seen = false;
    bool awaiting_engine_process = false;
    std::uint32_t target_engine_step = 0;
    std::size_t response_count_before_target = 0;
    std::size_t process_count_at_target = 0;
    std::size_t process_count_before_resume = 0;
    std::uint32_t trace_step = 0;
    std::uint32_t decision_index = 0;
    std::vector<std::string> observed;
    std::vector<std::uint32_t> sort_selected_indices;
    constexpr std::uint32_t max_engine_steps = 512;

    for (std::uint32_t engine_step = 0; engine_step < max_engine_steps; ++engine_step) {
        const auto result = host.process();
        if (contains_retry(result.message)) {
            throw std::runtime_error("pinned core emitted MSG_RETRY in sort_card fixture");
        }
        if (awaiting_engine_process) {
            if (result.message.empty()) {
                continue;
            }
            outcome.final_response_accepted = true;
            break;
        }

        const auto decoded = ygo::protocol::decode_messages(result.message, engine_step);
        if (decoded.terminal) {
            throw std::runtime_error("sort_card reached terminal state before acceptance");
        }
        if (!decoded.interactive || decoded.decisions.empty()) {
            continue;
        }
        if (decoded.decisions.size() != 1) {
            throw std::runtime_error("sort_card emitted multiple interactive decisions");
        }
        auto request = decoded.decisions.front();
        ygo::protocol::validate_candidate_set(request);
        if (observed.size() < 32) {
            std::ostringstream description;
            description << static_cast<unsigned>(request.engine_message_type) << ':'
                        << request.engine_message_name << ":candidates=" << request.candidates.size();
            for (const auto& candidate : request.candidates) {
                description << ":candidate=" << candidate.source_card << "/" << candidate.phase;
            }
            observed.push_back(description.str());
        }
        const bool is_target_sort = request.engine_message_type == MSG_SORT_CARD && request.continuation.has_value();
        if (!is_target_sort) {
            if (request.continuation.has_value()) {
                submit_non_target_continuation(host, std::move(request));
                continue;
            }
            const auto& candidate = choose_atomic(request);
            if (!candidate.submits_engine_response || candidate.exact_response_bytes.empty()) {
                throw std::runtime_error("sort_card non-target atomic candidate cannot submit an exact response");
            }
            host.submit_response(candidate.exact_response_bytes);
            continue;
        }

        if (!target_seen) {
            response_count_before_target = host.response_submission_count();
            process_count_at_target = host.process_call_count();
        }
        target_seen = true;
        outcome.message_type = request.engine_message_type;
        outcome.message_name = request.engine_message_name;
        target_engine_step = static_cast<std::uint32_t>(request.engine_step_index);
        const auto& original = *request.continuation;
        if (original.items.size() != 3) {
            throw std::runtime_error("sort_card fixture did not expose exactly three deck candidates");
        }
        std::vector<std::uint32_t> source_indices;
        for (const auto& item : original.items) {
            source_indices.push_back(item.source_index);
        }
        std::sort(source_indices.begin(), source_indices.end());
        if (source_indices != std::vector<std::uint32_t>{0, 1, 2}) {
            throw std::runtime_error("sort_card candidate source indices are not a complete ordered domain");
        }
        outcome.engine_state_hash_before = ygo::trace::sha256_bytes(host.query_field());
        for (;;) {
            ygo::protocol::validate_candidate_set(request);
            const auto& selected = choose_continuation(request);
            const auto transition = ygo::protocol::apply_continuation_action(request, selected.semantic_key);
            auto trace_record = ygo::trace::make_decision_step(
                trace_step++, result.message, request, ygo::trace::sha256_bytes(host.query_field()));
            trace_record.decision_index = decision_index++;
            trace_record.selected_semantic_key = selected.semantic_key;
            trace_record.engine_advanced = transition.engine_advanced;
            if (transition.terminal) {
                trace_record.final_engine_response_hash = ygo::trace::sha256_bytes(transition.engine_response);
            } else if (!transition.engine_response.empty() || transition.engine_advanced) {
                throw std::runtime_error("sort_card intermediate continuation advanced the engine");
            }
            outcome.trace.steps.push_back(std::move(trace_record));

            if (!transition.terminal) {
                ++outcome.intermediate_records;
                if (transition.request.engine_step_index != request.engine_step_index) {
                    throw std::runtime_error("sort_card intermediate changed engine step index");
                }
                if (host.process_call_count() != process_count_at_target) {
                    throw std::runtime_error("sort_card intermediate called OCG_DuelProcess");
                }
                if (ygo::trace::sha256_bytes(host.query_field()) != outcome.engine_state_hash_before) {
                    throw std::runtime_error("sort_card engine state changed during local continuation");
                }
                request = transition.request;
                continue;
            }
            if (!transition.engine_advanced || transition.engine_response.empty()) {
                throw std::runtime_error("sort_card final continuation did not construct an exact response");
            }
            if (!transition.request.continuation.has_value()) {
                throw std::runtime_error("sort_card final transition lost continuation state");
            }
            sort_selected_indices = transition.request.continuation->selected_indices;
            ++outcome.response_calls_for_target;
            host.submit_response(transition.engine_response);
            process_count_before_resume = host.process_call_count();
            awaiting_engine_process = true;
            break;
        }
    }

    if (!target_seen) {
        std::ostringstream error;
        error << "sort_card did not reach MSG_SORT_CARD; observed=";
        for (std::size_t index = 0; index < observed.size(); ++index) {
            if (index != 0) {
                error << ',';
            }
            error << observed[index];
        }
        throw std::runtime_error(error.str());
    }
    if (outcome.intermediate_records < 2) {
        throw std::runtime_error("sort_card did not exercise the shrinking ordered continuation");
    }
    const auto actual_response_calls_for_target =
        host.response_submission_count() - response_count_before_target;
    if (actual_response_calls_for_target != 1 || outcome.response_calls_for_target != 1) {
        throw std::runtime_error("sort_card did not submit exactly one final response");
    }
    outcome.response_calls_for_target = actual_response_calls_for_target;
    outcome.engine_state_hash_after = ygo::trace::sha256_bytes(host.query_field());
    if (process_count_before_resume != process_count_at_target) {
        throw std::runtime_error("sort_card called OCG_DuelProcess during continuation");
    }
    if (outcome.engine_state_hash_after != outcome.engine_state_hash_before) {
        throw std::runtime_error("sort_card engine state changed before final engine process");
    }
    if (!outcome.final_response_accepted) {
        throw std::runtime_error("sort_card final response was not followed by engine output");
    }
    if (outcome.trace.steps.empty() || outcome.trace.steps.back().engine_step_index != target_engine_step) {
        throw std::runtime_error("sort_card trace lost the original engine step");
    }
    outcome.semantic_hash = ygo::trace::semantic_gameplay_hash(outcome.trace);
    outcome.trace_hash = ygo::trace::canonical_trace_hash_v2(outcome.trace);
    write_trace(outcome);

    std::cout << "fixture_id=" << outcome.fixture_id << '\n'
              << "message_type=" << static_cast<unsigned>(outcome.message_type) << '\n'
              << "message_name=" << outcome.message_name << '\n'
              << "intermediate_continuation_records=" << outcome.intermediate_records << '\n'
              << "engine_state_hash_before=" << outcome.engine_state_hash_before << '\n'
              << "engine_state_hash_after=" << outcome.engine_state_hash_after << '\n'
              << "ocg_duel_set_response_calls=" << outcome.response_calls_for_target << '\n'
              << "final_response_accepted=" << (outcome.final_response_accepted ? "true" : "false") << '\n'
              << "ordered_source_indices=";
    for (std::size_t index = 0; index < sort_selected_indices.size(); ++index) {
        if (index != 0) {
            std::cout << ',';
        }
        std::cout << sort_selected_indices[index];
    }
    std::cout << '\n'
              << "semantic_gameplay_hash=" << outcome.semantic_hash << '\n'
              << "trace_hash=" << outcome.trace_hash << '\n';
    return outcome;
}

FixtureOutcome run_select_disfield() {
    const auto config = make_config();
    const auto deck_a = ygo::core::load_fixture_deck(YGO_M1_ENGINE_PLAYER_A);
    const auto deck_b = ygo::core::load_fixture_deck(YGO_M1_ENGINE_PLAYER_B);
    ygo::core::CoreHost host(config);
    host.load_deck(0, deck_a);
    host.load_deck(1, deck_b);
    host.start_duel();
    host.load_fixture_card(0, kDisfieldSelector, LOCATION_HAND, 0, POS_FACEDOWN_DEFENSE);

    FixtureOutcome outcome;
    outcome.fixture_id = "m1.1.select_disfield";
    outcome.trace.manifest = make_manifest(config, deck_a, deck_b, outcome.fixture_id);
    bool target_seen = false;
    bool awaiting_engine_process = false;
    std::uint32_t target_engine_step = 0;
    std::size_t response_count_before_target = 0;
    std::size_t process_count_at_target = 0;
    std::size_t process_count_before_resume = 0;
    std::uint32_t trace_step = 0;
    std::uint32_t decision_index = 0;
    constexpr std::uint32_t max_engine_steps = 512;

    for (std::uint32_t engine_step = 0; engine_step < max_engine_steps; ++engine_step) {
        const auto result = host.process();
        if (contains_retry(result.message)) {
            throw std::runtime_error("pinned core emitted MSG_RETRY in select_disfield fixture");
        }
        if (awaiting_engine_process) {
            if (result.message.empty()) {
                continue;
            }
            outcome.final_response_accepted = true;
            break;
        }

        const auto decoded = ygo::protocol::decode_messages(result.message, engine_step);
        if (decoded.terminal) {
            throw std::runtime_error("select_disfield reached terminal state before acceptance");
        }
        if (!decoded.interactive || decoded.decisions.empty()) {
            continue;
        }
        if (decoded.decisions.size() != 1) {
            throw std::runtime_error("select_disfield emitted multiple interactive decisions");
        }
        auto request = decoded.decisions.front();
        ygo::protocol::validate_candidate_set(request);
        const bool is_target_disfield =
            request.engine_message_type == MSG_SELECT_DISFIELD && request.continuation.has_value();
        if (!is_target_disfield) {
            if (request.continuation.has_value()) {
                submit_non_target_continuation(host, std::move(request));
                continue;
            }
            const auto& candidate = choose_atomic(request);
            if (!candidate.submits_engine_response || candidate.exact_response_bytes.empty()) {
                throw std::runtime_error("select_disfield non-target atomic candidate cannot submit an exact response");
            }
            host.submit_response(candidate.exact_response_bytes);
            continue;
        }

        if (!target_seen) {
            response_count_before_target = host.response_submission_count();
            process_count_at_target = host.process_call_count();
        }
        target_seen = true;
        outcome.message_type = request.engine_message_type;
        outcome.message_name = request.engine_message_name;
        target_engine_step = static_cast<std::uint32_t>(request.engine_step_index);
        const auto& original = *request.continuation;
        if (original.min_count != 2 || original.max_count != 2 || original.items.size() < 2) {
            throw std::runtime_error("select_disfield did not expose a two-zone selection contract");
        }
        std::vector<std::string> zone_keys;
        for (const auto& item : original.items) {
            zone_keys.push_back(std::to_string(item.card.controller) + ":" +
                                std::to_string(item.card.location) + ":" +
                                std::to_string(item.card.sequence));
        }
        auto unique_zone_keys = zone_keys;
        std::sort(unique_zone_keys.begin(), unique_zone_keys.end());
        unique_zone_keys.erase(std::unique(unique_zone_keys.begin(), unique_zone_keys.end()), unique_zone_keys.end());
        if (unique_zone_keys.size() != zone_keys.size()) {
            throw std::runtime_error("select_disfield exposed a duplicate zone candidate");
        }
        outcome.engine_state_hash_before = ygo::trace::sha256_bytes(host.query_field());
        for (;;) {
            ygo::protocol::validate_candidate_set(request);
            const auto& selected = choose_continuation(request);
            const auto transition = ygo::protocol::apply_continuation_action(request, selected.semantic_key);
            auto trace_record = ygo::trace::make_decision_step(
                trace_step++, result.message, request, ygo::trace::sha256_bytes(host.query_field()));
            trace_record.decision_index = decision_index++;
            trace_record.selected_semantic_key = selected.semantic_key;
            trace_record.engine_advanced = transition.engine_advanced;
            if (transition.terminal) {
                trace_record.final_engine_response_hash = ygo::trace::sha256_bytes(transition.engine_response);
            } else if (!transition.engine_response.empty() || transition.engine_advanced) {
                throw std::runtime_error("select_disfield intermediate continuation advanced the engine");
            }
            outcome.trace.steps.push_back(std::move(trace_record));

            if (!transition.terminal) {
                ++outcome.intermediate_records;
                if (transition.request.engine_step_index != request.engine_step_index) {
                    throw std::runtime_error("select_disfield intermediate changed engine step index");
                }
                if (host.process_call_count() != process_count_at_target) {
                    throw std::runtime_error("select_disfield intermediate called OCG_DuelProcess");
                }
                if (ygo::trace::sha256_bytes(host.query_field()) != outcome.engine_state_hash_before) {
                    throw std::runtime_error("select_disfield engine state changed during local continuation");
                }
                request = transition.request;
                continue;
            }
            if (!transition.engine_advanced || transition.engine_response.empty()) {
                throw std::runtime_error("select_disfield final continuation did not construct an exact response");
            }
            ++outcome.response_calls_for_target;
            host.submit_response(transition.engine_response);
            process_count_before_resume = host.process_call_count();
            awaiting_engine_process = true;
            break;
        }
    }

    if (!target_seen) {
        throw std::runtime_error("select_disfield did not reach MSG_SELECT_DISFIELD");
    }
    if (outcome.intermediate_records == 0) {
        throw std::runtime_error("select_disfield did not produce an intermediate continuation record");
    }
    const auto actual_response_calls_for_target =
        host.response_submission_count() - response_count_before_target;
    if (actual_response_calls_for_target != 1 || outcome.response_calls_for_target != 1) {
        throw std::runtime_error("select_disfield did not submit exactly one final response");
    }
    outcome.response_calls_for_target = actual_response_calls_for_target;
    outcome.engine_state_hash_after = ygo::trace::sha256_bytes(host.query_field());
    if (process_count_before_resume != process_count_at_target) {
        throw std::runtime_error("select_disfield called OCG_DuelProcess during continuation");
    }
    if (outcome.engine_state_hash_after != outcome.engine_state_hash_before) {
        throw std::runtime_error("select_disfield engine state changed before final engine process");
    }
    if (!outcome.final_response_accepted) {
        throw std::runtime_error("select_disfield final response was not followed by engine output");
    }
    if (outcome.trace.steps.empty() || outcome.trace.steps.back().engine_step_index != target_engine_step) {
        throw std::runtime_error("select_disfield trace lost the original engine step");
    }
    outcome.semantic_hash = ygo::trace::semantic_gameplay_hash(outcome.trace);
    outcome.trace_hash = ygo::trace::canonical_trace_hash_v2(outcome.trace);
    write_trace(outcome);

    std::cout << "fixture_id=" << outcome.fixture_id << '\n'
              << "message_type=" << static_cast<unsigned>(outcome.message_type) << '\n'
              << "message_name=" << outcome.message_name << '\n'
              << "intermediate_continuation_records=" << outcome.intermediate_records << '\n'
              << "engine_state_hash_before=" << outcome.engine_state_hash_before << '\n'
              << "engine_state_hash_after=" << outcome.engine_state_hash_after << '\n'
              << "ocg_duel_set_response_calls=" << outcome.response_calls_for_target << '\n'
              << "final_response_accepted=" << (outcome.final_response_accepted ? "true" : "false") << '\n'
              << "semantic_gameplay_hash=" << outcome.semantic_hash << '\n'
              << "trace_hash=" << outcome.trace_hash << '\n';
    return outcome;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        if (argc != 2) {
            std::cerr << "usage: m1_engine_conformance_test <select_option|select_card_multi|select_sum|select_counter|sort_card|select_disfield>\n";
            return 2;
        }
        const std::string fixture = argv[1];
        if (fixture == "select_option") {
            (void)run_select_option();
        } else if (fixture == "select_card_multi") {
            (void)run_select_card_multi();
        } else if (fixture == "select_sum") {
            (void)run_select_sum();
        } else if (fixture == "select_counter") {
            (void)run_select_counter();
        } else if (fixture == "sort_card") {
            (void)run_sort_card();
        } else if (fixture == "select_disfield") {
            (void)run_select_disfield();
        } else {
            std::cerr << "unknown fixture: " << fixture << '\n';
            return 2;
        }
        return 0;
    } catch (const ygo::protocol::ProtocolError& error) {
        std::cerr << "protocol error: " << error.what() << " type="
                  << static_cast<unsigned>(error.message_type()) << " player="
                  << static_cast<unsigned>(error.player()) << '\n';
        return 1;
    } catch (const ygo::core::CoreError& error) {
        std::cerr << "core error: " << error.what() << '\n';
        return 1;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
