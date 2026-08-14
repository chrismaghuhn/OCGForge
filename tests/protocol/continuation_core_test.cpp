#include <cstdint>
#include <exception>
#include <iostream>
#include <string>
#include <vector>

#include "ygo/protocol/continuation.hpp"
#include "ygo/protocol/message_decoder.hpp"
#include "ygo/protocol/protocol_error.hpp"

namespace {

using ygo::protocol::ActionKind;
using ygo::protocol::ContinuationItem;
using ygo::protocol::ContinuationTransition;
using ygo::protocol::DecisionRequest;

ContinuationItem item(std::uint32_t code, std::uint32_t index) {
    ContinuationItem result;
    result.card.code = code;
    result.card.controller = 0;
    result.card.location = 2;
    result.card.sequence = index;
    result.source_index = index;
    return result;
}

const ygo::protocol::ActionCandidate& find_candidate(const DecisionRequest& request, ActionKind kind) {
    for (const auto& candidate : request.candidates) {
        if (candidate.action_kind == kind) {
            return candidate;
        }
    }
    throw std::runtime_error("expected continuation candidate kind was not present");
}

int run() {
    const auto request = ygo::protocol::begin_unordered_continuation(
        0, 15, "MSG_SELECT_CARD", "raw-hash", {item(100, 0), item(200, 1), item(300, 2)}, 2, 3, false);
    if (!request.continuation.has_value() || request.candidates.size() != 2) {
        std::cerr << "initial continuation did not expose every non-dead primitive pick\n";
        return 1;
    }
    if (request.continuation->continuation_steps != 0 ||
        request.continuation->peak_candidate_count < request.candidates.size() ||
        request.continuation->terminal_solution_count != 0) {
        std::cerr << "initial continuation diagnostics were not initialized from the legal domain\n";
        return 1;
    }
    try {
        ygo::protocol::validate_candidate_set(request);
    } catch (const ygo::protocol::ProtocolError& error) {
        std::cerr << "intermediate candidates were rejected: " << error.what() << '\n';
        return 1;
    }
    for (const auto& candidate : request.candidates) {
        if (candidate.action_kind != ActionKind::Pick || candidate.submits_engine_response ||
            !candidate.exact_response_bytes.empty()) {
            std::cerr << "initial picks must be adapter-local actions\n";
            return 1;
        }
    }

    const auto first = ygo::protocol::apply_continuation_action(request, request.candidates[0].semantic_key);
    if (first.terminal || first.engine_advanced || !first.engine_response.empty() ||
        first.request.candidates.size() != 2 || first.request.continuation->continuation_step != 1) {
        std::cerr << "intermediate continuation advanced the engine or lost candidates\n";
        return 1;
    }
    if (first.request.continuation->continuation_steps != 1 ||
        first.request.continuation->peak_candidate_count < first.request.candidates.size()) {
        std::cerr << "continuation diagnostics did not evolve with the adapter-local step\n";
        return 1;
    }
    if (first.request.candidates[0].semantic_key == request.candidates[0].semantic_key) {
        std::cerr << "continuation state did not change its semantic identity\n";
        return 1;
    }

    try {
        (void)ygo::protocol::apply_continuation_action(first.request, request.candidates[1].semantic_key);
        std::cerr << "stale continuation action was accepted\n";
        return 1;
    } catch (const ygo::protocol::ProtocolError& error) {
        if (error.code() != ygo::protocol::ProtocolErrorCode::InvalidSemanticKey) {
            std::cerr << "stale action returned the wrong error\n";
            return 1;
        }
    }

    const auto second = ygo::protocol::apply_continuation_action(
        first.request, find_candidate(first.request, ActionKind::Pick).semantic_key);
    const auto& finish = find_candidate(second.request, ActionKind::Finish);
    if (second.terminal || finish.exact_response_bytes.empty() || !finish.submits_engine_response) {
        std::cerr << "legal finish was not exposed as a terminal candidate\n";
        return 1;
    }
    if (second.request.continuation->terminal_solution_count == 0) {
        std::cerr << "terminal solution diagnostic did not count the legal finish\n";
        return 1;
    }

    const auto final = ygo::protocol::apply_continuation_action(second.request, finish.semantic_key);
    if (!final.terminal || !final.engine_advanced || final.engine_response != finish.exact_response_bytes) {
        std::cerr << "finish did not produce exactly one final response\n";
        return 1;
    }
    return 0;
}

}  // namespace

int main() {
    try {
        return run();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
