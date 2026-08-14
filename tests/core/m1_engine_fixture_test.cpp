#include <algorithm>
#include <cstdint>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <sstream>
#include <vector>

#include "ocgapi_constants.h"
#include "ygo/core/core_error.hpp"
#include "ygo/core/core_host.hpp"
#include "ygo/protocol/message_decoder.hpp"
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

int run() {
    ygo::core::CoreHostConfig config;
    config.rules.card_scripts_root = YGO_M1_CARDSCRIPTS;
    config.rules.card_data_tsv = YGO_M0_CARD_DATA_TSV;
    config.rules.bundle_id = "6fbbd212ae4be2df36170dcbfcdf5c46aaaa0e3091cf815c2d0261fd01640ea4";
    config.starting_draw_count = 40;
    config.draw_count_per_turn = 0;
    config.seed.words = {0x0123456789abcdefULL, 0xfedcba9876543210ULL, 0x13579bdf2468ace0ULL,
                         0x0eca8642fdb97531ULL};

    const auto deck_a = ygo::core::load_fixture_deck(YGO_M1_PLAYER_A);
    const auto deck_b = ygo::core::load_fixture_deck(YGO_M1_PLAYER_B);
    ygo::core::CoreHost host(config);
    host.load_deck(0, deck_a);
    host.load_deck(1, deck_b);
    host.start_duel();

    bool saw_tribute = false;
    std::uint32_t tribute_intermediate_steps = 0;
    std::size_t response_count_before_tribute = 0;
    std::size_t process_count_at_tribute = 0;
    bool awaiting_engine_process = false;
    std::vector<std::string> observed_decisions;
    constexpr std::uint32_t max_steps = 512;
    for (std::uint32_t engine_step = 0; engine_step < max_steps; ++engine_step) {
        const auto result = host.process();
        const auto decoded = ygo::protocol::decode_messages(result.message, engine_step);
        if (decoded.retry) {
            throw std::runtime_error("pinned core emitted MSG_RETRY in tribute fixture");
        }
        if (awaiting_engine_process) {
            awaiting_engine_process = false;
            if (decoded.message_type == MSG_RETRY) {
                throw std::runtime_error("pinned core rejected the final tribute response with MSG_RETRY");
            }
            if (decoded.terminal || decoded.interactive) {
                // The response was accepted if the next engine process completed
                // and produced a valid protocol message; stop at this checkpoint.
                if (saw_tribute) {
                    if (tribute_intermediate_steps == 0) {
                        throw std::runtime_error("tribute fixture did not exercise an adapter-local continuation");
                    }
                    if (host.response_submission_count() - response_count_before_tribute != 1) {
                        throw std::runtime_error("tribute fixture submitted more than one target response");
                    }
                    std::cout << "m1_engine_fixture=ok\n"
                              << "tribute_intermediate_steps=" << tribute_intermediate_steps << '\n'
                              << "tribute_target_response_calls=1\n";
                    return 0;
                }
            }
            if (saw_tribute) {
                if (tribute_intermediate_steps == 0) {
                    throw std::runtime_error("tribute fixture did not exercise an adapter-local continuation");
                }
                if (host.response_submission_count() - response_count_before_tribute != 1) {
                    throw std::runtime_error("tribute fixture submitted more than one target response");
                }
                std::cout << "m1_engine_fixture=ok\n"
                          << "tribute_intermediate_steps=" << tribute_intermediate_steps << '\n'
                          << "tribute_target_response_calls=1\n";
                return 0;
            }
        }
        if (decoded.terminal) {
            break;
        }
        if (!decoded.interactive || decoded.decisions.empty()) {
            continue;
        }
        if (decoded.decisions.size() != 1) {
            throw std::runtime_error("engine fixture emitted multiple interactive decisions");
        }
        auto request = decoded.decisions.front();
        ygo::protocol::validate_candidate_set(request);
        if (observed_decisions.size() < 24) {
            std::ostringstream description;
            description << static_cast<unsigned>(request.engine_message_type) << ':'
                        << request.engine_message_name << ':' << request.candidates.size();
            observed_decisions.push_back(description.str());
        }
        if (request.engine_message_type == MSG_SELECT_TRIBUTE) {
            if (!saw_tribute) {
                response_count_before_tribute = host.response_submission_count();
                process_count_at_tribute = host.process_call_count();
            }
            saw_tribute = true;
        }

        if (!request.continuation.has_value()) {
            const auto& candidate = choose_atomic(request);
            if (!candidate.submits_engine_response || candidate.exact_response_bytes.empty()) {
                throw std::runtime_error("atomic fixture candidate was not engine-submittable");
            }
            host.submit_response(candidate.exact_response_bytes);
            if (saw_tribute) {
                awaiting_engine_process = true;
            }
            continue;
        }

        const auto original_engine_step = request.engine_step_index;
        for (;;) {
            const auto& candidate = choose_continuation(request);
            const auto transition = ygo::protocol::apply_continuation_action(request, candidate.semantic_key);
            if (!transition.terminal) {
                if (request.engine_message_type == MSG_SELECT_TRIBUTE) {
                    ++tribute_intermediate_steps;
                }
                if (transition.engine_advanced || !transition.engine_response.empty() ||
                    transition.request.engine_step_index != original_engine_step) {
                    throw std::runtime_error("continuation intermediate action advanced the engine");
                }
                if (request.engine_message_type == MSG_SELECT_TRIBUTE &&
                    host.process_call_count() != process_count_at_tribute) {
                    throw std::runtime_error("tribute continuation called OCG_DuelProcess");
                }
                request = transition.request;
                continue;
            }
            if (!transition.engine_advanced || transition.engine_response.empty()) {
                throw std::runtime_error("continuation terminal action did not produce one response");
            }
            host.submit_response(transition.engine_response);
            if (saw_tribute) {
                awaiting_engine_process = true;
            }
            break;
        }
    }
    std::ostringstream failure;
    failure << "pinned engine fixture did not reach MSG_SELECT_TRIBUTE; observed=";
    for (std::size_t index = 0; index < observed_decisions.size(); ++index) {
        if (index != 0) {
            failure << ',';
        }
        failure << observed_decisions[index];
    }
    throw std::runtime_error(failure.str());
}

}  // namespace

int main() {
    try {
        return run();
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
