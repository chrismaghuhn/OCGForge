#include "ygo/environment/episode_driver.hpp"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "ocgapi_constants.h"
#include "ygo/protocol/action_candidate.hpp"
#include "ygo/protocol/continuation.hpp"
#include "ygo/protocol/protocol_error.hpp"

#ifndef YGO_M1_PLAYER_A
#error "YGO_M1_PLAYER_A must be supplied by CMake"
#endif
#ifndef YGO_M1_PLAYER_B
#error "YGO_M1_PLAYER_B must be supplied by CMake"
#endif
#ifndef YGO_M1_CARDSCRIPTS
#error "YGO_M1_CARDSCRIPTS must be supplied by CMake"
#endif

namespace {

using ygo::protocol::ActionCandidate;
using ygo::protocol::ActionKind;
using ygo::protocol::DecisionRequest;
using ygo::protocol::DecisionRequestKind;

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
        for (const auto phase : {0u, 6u, 7u, 8u, 1u, 2u, 3u, 4u, 5u}) {
            std::vector<const ActionCandidate*> matching;
            for (const auto* candidate : candidates) {
                if (candidate->phase == phase) {
                    matching.push_back(candidate);
                }
            }
            if (!matching.empty()) {
                if (phase == 0) {
                    for (const auto* candidate : matching) {
                        if (candidate->source_card == 14556954) {
                            return *candidate;
                        }
                    }
                }
                return choose_min(matching);
            }
        }
    } else if (request.kind == DecisionRequestKind::Chain) {
        for (const auto* candidate : candidates) {
            if (candidate->semantic_key == "chain.pass") {
                return *candidate;
            }
        }
    } else if (request.kind == DecisionRequestKind::YesNo) {
        for (const auto* candidate : candidates) {
            if (candidate->semantic_key == "yes_no.yes") {
                return *candidate;
            }
        }
    } else if (request.kind == DecisionRequestKind::Position) {
        for (const auto* candidate : candidates) {
            if (candidate->position == POS_FACEUP_ATTACK) {
                return *candidate;
            }
        }
    } else if (request.kind == DecisionRequestKind::CardSelection) {
        std::vector<const ActionCandidate*> non_cancel;
        for (const auto* candidate : candidates) {
            if (candidate->action_kind != ActionKind::Cancel) {
                non_cancel.push_back(candidate);
            }
        }
        if (!non_cancel.empty()) {
            return choose_min(non_cancel);
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
    for (const auto& candidate : request.candidates) {
        if (candidate.action_kind == ActionKind::Cancel) {
            return candidate;
        }
    }
    throw std::runtime_error("continuation fixture policy found no legal transition");
}

ygo::environment::EpisodeDriverConfig make_config() {
    ygo::environment::EpisodeDriverConfig config;
    config.rules.card_scripts_root = YGO_M1_CARDSCRIPTS;
    config.rules.card_data_tsv = YGO_M0_CARD_DATA_TSV;
    config.rules.bundle_id =
        "6fbbd212ae4be2df36170dcbfcdf5c46aaaa0e3091cf815c2d0261fd01640ea4";
    config.player_zero_deck = ygo::core::load_fixture_deck(YGO_M1_PLAYER_A);
    config.player_one_deck = ygo::core::load_fixture_deck(YGO_M1_PLAYER_B);
    config.seed = 0x0123456789abcdefULL;
    config.starting_draw_count = 40;
    config.draw_count_per_turn = 0;
    config.engine_process_budget = 512;
    config.build_full_observation = false;
    return config;
}

}  // namespace

int main() {
    try {
        ygo::environment::EpisodeDriver driver(make_config());
        auto boundary = driver.advance_until_boundary();
        bool saw_tribute = false;
        bool saw_terminal_tribute_response = false;
        bool saw_stale_continuation_rejection = false;
        std::uint32_t tribute_intermediate_steps = 0;
        std::uint64_t response_count_before_tribute = 0;
        std::uint64_t process_count_before_tribute = 0;

        for (std::uint32_t iteration = 0; iteration < 512; ++iteration) {
            bool is_tribute = false;
            bool is_continuation = false;
            std::uint64_t process_before_apply = 0;
            std::uint64_t response_before_apply = 0;
            std::string semantic_key;
            {
                const auto* decision = std::get_if<ygo::environment::DriverDecisionBoundary>(&boundary);
                if (decision == nullptr || decision->request == nullptr) {
                    if (std::holds_alternative<ygo::environment::DriverGameTerminal>(boundary)) {
                        break;
                    }
                    if (std::holds_alternative<ygo::environment::DriverProcessBudgetExceeded>(boundary)) {
                        throw std::runtime_error("tribute driver reached the process budget");
                    }
                    throw std::runtime_error("tribute driver returned a failure");
                }

                const auto& request = *decision->request;
                is_tribute = request.engine_message_type == MSG_SELECT_TRIBUTE;
                if (is_tribute && !saw_tribute) {
                    saw_tribute = true;
                    response_count_before_tribute = driver.metrics().response_submission_count;
                    process_count_before_tribute = driver.metrics().process_call_count;
                }
                is_continuation = request.continuation.has_value();
                process_before_apply = driver.metrics().process_call_count;
                response_before_apply = driver.metrics().response_submission_count;
                semantic_key = is_continuation ? choose_continuation(request).semantic_key
                                               : choose_atomic(request).semantic_key;
            }
            boundary = driver.apply_semantic_key(semantic_key);

            if (saw_tribute && is_continuation) {
                const auto process_after_apply = driver.metrics().process_call_count;
                const auto response_after_apply = driver.metrics().response_submission_count;
                if (process_after_apply == process_before_apply && response_after_apply == response_before_apply) {
                    ++tribute_intermediate_steps;
                    if (!std::holds_alternative<ygo::environment::DriverDecisionBoundary>(boundary)) {
                        throw std::runtime_error("intermediate tribute action did not publish a new boundary");
                    }
                    if (!saw_stale_continuation_rejection) {
                        std::string decision_id_before_stale;
                        std::size_t candidate_count_before_stale = 0;
                        ygo::environment::DriverMetrics metrics_before_stale;
                        std::size_t trace_steps_before_stale = 0;
                        {
                            const auto* next_decision =
                                std::get_if<ygo::environment::DriverDecisionBoundary>(&boundary);
                            if (next_decision == nullptr || next_decision->request == nullptr) {
                                throw std::runtime_error(
                                    "intermediate tribute action lost its next decision boundary");
                            }
                            decision_id_before_stale = std::string(next_decision->request->decision_id);
                            candidate_count_before_stale = next_decision->request->candidates.size();
                            metrics_before_stale = driver.metrics();
                            trace_steps_before_stale = driver.trace().steps.size();
                        }
                        bool rejected = false;
                        try {
                            (void)driver.apply_semantic_key(semantic_key);
                        } catch (const ygo::protocol::ProtocolError& error) {
                            rejected = true;
                            if (error.code() != ygo::protocol::ProtocolErrorCode::InvalidSemanticKey) {
                                throw;
                            }
                        }
                        if (!rejected) {
                            throw std::runtime_error("stale continuation semantic key was accepted");
                        }
                        const auto& metrics_after_invalid = driver.metrics();
                        if (metrics_after_invalid.process_call_count != metrics_before_stale.process_call_count ||
                            metrics_after_invalid.response_submission_count !=
                                metrics_before_stale.response_submission_count ||
                            metrics_after_invalid.semantic_action_count != metrics_before_stale.semantic_action_count ||
                            metrics_after_invalid.operations.candidate_sets !=
                                metrics_before_stale.operations.candidate_sets ||
                            metrics_after_invalid.operations.candidate_total !=
                                metrics_before_stale.operations.candidate_total ||
                            metrics_after_invalid.operations.candidate_max !=
                                metrics_before_stale.operations.candidate_max ||
                            driver.trace().steps.size() != trace_steps_before_stale) {
                            throw std::runtime_error("stale continuation rejection mutated decision " +
                                                     decision_id_before_stale + " with " +
                                                     std::to_string(candidate_count_before_stale) + " candidates");
                        }
                        saw_stale_continuation_rejection = true;
                    }
                } else {
                    if (response_after_apply != response_before_apply + 1 ||
                        process_after_apply <= process_before_apply) {
                        throw std::runtime_error("terminal tribute action did not submit once before processing");
                    }
                    saw_terminal_tribute_response = true;
                    break;
                }
            }
        }

        if (!saw_tribute || tribute_intermediate_steps == 0 || !saw_terminal_tribute_response ||
            !saw_stale_continuation_rejection) {
            throw std::runtime_error(
                "driver tribute fixture did not prove intermediate, stale-key, and terminal continuation steps");
        }
        if (driver.metrics().response_submission_count - response_count_before_tribute != 1) {
            throw std::runtime_error("driver tribute fixture submitted more than one target response");
        }
        if (driver.metrics().process_call_count <= process_count_before_tribute) {
            throw std::runtime_error("driver tribute fixture did not process after the final response");
        }
        std::cout << "episode_driver_tribute=ok\n"
                  << "tribute_intermediate_steps=" << tribute_intermediate_steps << "\n"
                  << "tribute_target_response_calls=1\n";
        return 0;
    } catch (const ygo::protocol::ProtocolError& error) {
        std::cerr << "protocol error: " << error.what() << '\n';
        return 1;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
