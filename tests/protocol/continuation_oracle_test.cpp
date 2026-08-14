#include <algorithm>
#include <cstdint>
#include <functional>
#include <iostream>
#include <numeric>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

#include "ygo/protocol/continuation.hpp"
#include "ygo/protocol/response_builder.hpp"

namespace {

using ygo::protocol::ActionCandidate;
using ygo::protocol::ActionKind;
using ygo::protocol::ContinuationItem;
using ygo::protocol::ContinuationKind;
using ygo::protocol::DecisionRequest;
using ygo::protocol::DecisionRequestKind;
using ygo::protocol::SelectionContinuation;

void require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

bool contains(const std::vector<std::uint32_t>& values, std::uint32_t value) {
    return std::find(values.begin(), values.end(), value) != values.end();
}

const ContinuationItem& item_by_source(const SelectionContinuation& continuation, std::uint32_t source_index) {
    const auto it = std::find_if(continuation.items.begin(), continuation.items.end(),
                                 [source_index](const ContinuationItem& item) {
                                     return item.source_index == source_index;
                                 });
    if (it == continuation.items.end()) {
        throw std::runtime_error("oracle referenced an unknown continuation item");
    }
    return *it;
}

std::size_t bit_count(std::size_t value) {
    std::size_t count = 0;
    while (value != 0) {
        value &= value - 1;
        ++count;
    }
    return count;
}

const ActionCandidate& find_action(const DecisionRequest& request, ActionKind kind) {
    const auto it = std::find_if(request.candidates.begin(), request.candidates.end(),
                                 [kind](const ActionCandidate& candidate) {
                                     return candidate.action_kind == kind;
                                 });
    if (it == request.candidates.end()) {
        throw std::runtime_error("expected continuation action was not exposed");
    }
    return *it;
}

const ActionCandidate& find_pick(const DecisionRequest& request, std::uint32_t source_index) {
    const auto it = std::find_if(request.candidates.begin(), request.candidates.end(),
                                 [source_index](const ActionCandidate& candidate) {
                                     return candidate.action_kind == ActionKind::Pick &&
                                            candidate.source_index == source_index;
                                 });
    if (it == request.candidates.end()) {
        throw std::runtime_error("expected continuation pick was not exposed");
    }
    return *it;
}

std::vector<std::uint32_t> pick_indices(const DecisionRequest& request) {
    std::vector<std::uint32_t> result;
    for (const auto& candidate : request.candidates) {
        if (candidate.action_kind == ActionKind::Pick) {
            result.push_back(candidate.source_index);
        }
    }
    std::sort(result.begin(), result.end());
    return result;
}

std::vector<std::uint32_t> all_indices_after(const SelectionContinuation& continuation, std::uint32_t index) {
    std::vector<std::uint32_t> result;
    for (const auto& item : continuation.items) {
        if (item.source_index > index && !contains(continuation.selected_indices, item.source_index)) {
            result.push_back(item.source_index);
        }
    }
    return result;
}

bool generic_subset_can_finish(const SelectionContinuation& continuation, std::uint32_t picked_index) {
    const auto tail = all_indices_after(continuation, picked_index);
    const auto selected_count = continuation.selected_indices.size() + 1;
    if (selected_count > continuation.max_count) {
        return false;
    }
    const auto subset_count = std::size_t{1} << tail.size();
    for (std::size_t mask = 0; mask < subset_count; ++mask) {
        const auto count = selected_count + bit_count(mask);
        if (count >= continuation.min_count && count <= continuation.max_count) {
            return true;
        }
    }
    return false;
}

void assert_intermediate(const ygo::protocol::ContinuationTransition& transition) {
    require(!transition.terminal, "a pick/assignment advanced the engine");
    require(!transition.engine_advanced, "an intermediate continuation advanced the engine");
    require(transition.engine_response.empty(), "an intermediate continuation emitted an engine response");
}

void assert_metrics(const SelectionContinuation& state, const DecisionRequest& request) {
    std::size_t terminal_candidates = 0;
    for (const auto& candidate : request.candidates) {
        if (candidate.action_kind == ActionKind::Finish || candidate.action_kind == ActionKind::Cancel) {
            ++terminal_candidates;
        }
    }
    require(state.continuation_steps == state.continuation_step,
            "continuation step metric did not match the immutable state step");
    require(state.peak_candidate_count >= request.candidates.size(),
            "continuation peak candidate metric is below the current legal domain");
    require(state.terminal_solution_count == terminal_candidates,
            "terminal solution metric did not count the exposed terminal actions");
}

void exhaustive_generic(ContinuationKind kind, DecisionRequestKind request_kind, std::uint8_t message_type,
                        std::uint32_t min_count, std::uint32_t max_count, std::vector<ContinuationItem> items,
                        const std::function<void(const SelectionContinuation&, const std::vector<std::uint8_t>&)>&
                            terminal_response_check,
                        std::size_t expected_states) {
    SelectionContinuation continuation;
    continuation.continuation_kind = kind;
    continuation.original_message_type = message_type;
    continuation.raw_message_hash = "oracle.generic";
    continuation.items = std::move(items);
    continuation.min_count = min_count;
    continuation.max_count = max_count;
    auto request = ygo::protocol::make_continuation_request(request_kind, 0, message_type, "oracle", 7,
                                                            std::move(continuation));
    std::set<std::string> visited;
    std::function<void(const DecisionRequest&)> visit = [&](const DecisionRequest& current) {
        const auto& state = *current.continuation;
        if (!visited.insert(state.continuation_id).second) {
            return;
        }
        assert_metrics(state, current);
        std::vector<std::uint32_t> expected_picks;
        const auto last = state.selected_indices.empty() ? 0u : state.selected_indices.back();
        for (const auto& item : state.items) {
            if (contains(state.selected_indices, item.source_index) ||
                (kind == ContinuationKind::AnnouncementMask && state.selected_indices.size() >= min_count) ||
                (!state.selected_indices.empty() && item.source_index <= last) ||
                !generic_subset_can_finish(state, item.source_index)) {
                continue;
            }
            expected_picks.push_back(item.source_index);
        }
        std::sort(expected_picks.begin(), expected_picks.end());
        require(pick_indices(current) == expected_picks, "generic continuation pruned or added a legal pick");

        const bool should_finish = state.selected_indices.size() >= min_count &&
                                   state.selected_indices.size() <= max_count;
        const auto finish_it = std::find_if(current.candidates.begin(), current.candidates.end(),
                                            [](const ActionCandidate& candidate) {
                                                return candidate.action_kind == ActionKind::Finish;
                                            });
        require((finish_it != current.candidates.end()) == should_finish,
                "generic continuation finish legality disagrees with the oracle");
        if (finish_it != current.candidates.end()) {
            const auto transition = ygo::protocol::apply_continuation_action(current, finish_it->semantic_key);
            require(transition.terminal && transition.engine_advanced,
                    "generic finish did not produce a terminal engine transition");
            terminal_response_check(state, transition.engine_response);
        }
        for (const auto source_index : expected_picks) {
            const auto transition = ygo::protocol::apply_continuation_action(
                current, find_pick(current, source_index).semantic_key);
            assert_intermediate(transition);
            visit(transition.request);
        }
    };
    visit(request);
    if (visited.size() != expected_states) {
        throw std::runtime_error("generic oracle did not reach every reachable subset state for kind " +
                                 std::to_string(static_cast<int>(kind)) + ": " + std::to_string(visited.size()));
    }
}

void test_unordered_and_zone_and_mask() {
    std::vector<ContinuationItem> items;
    for (std::uint32_t index = 0; index < 5; ++index) {
        ContinuationItem item;
        item.source_index = index;
        item.card.code = 1000 + index;
        item.card.controller = 0;
        item.card.location = 4;
        item.card.sequence = index;
        items.push_back(item);
    }
    exhaustive_generic(ContinuationKind::UnorderedSelection, DecisionRequestKind::CardSelection, 15, 2, 3,
                       items, [](const SelectionContinuation& state, const std::vector<std::uint8_t>& response) {
                           const auto expected = ygo::protocol::encode_card_index_response(state.selected_indices);
                           require(response == expected, "unordered terminal response did not preserve indices");
                       }, 25);

    for (auto& item : items) {
        item.card.location = 8;
        item.card.sequence += 10;
    }
    exhaustive_generic(ContinuationKind::ZonePlacement, DecisionRequestKind::Place, 18, 2, 3, items,
                       [](const SelectionContinuation& state, const std::vector<std::uint8_t>& response) {
                           std::vector<ygo::protocol::ZoneSlot> zones;
                           for (const auto index : state.selected_indices) {
                               const auto& item = item_by_source(state, index);
                               zones.push_back({item.card.controller, static_cast<std::uint8_t>(item.card.location),
                                                static_cast<std::uint8_t>(item.card.sequence)});
                           }
                           require(response == ygo::protocol::encode_zone_response(zones),
                                   "zone terminal response did not preserve slots");
                       }, 25);

    std::vector<ContinuationItem> mask_items;
    for (const auto bit : {0u, 2u, 5u, 7u, 9u}) {
        ContinuationItem item;
        item.source_index = bit;
        item.mask_value = 1ull << bit;
        mask_items.push_back(item);
    }
    exhaustive_generic(ContinuationKind::AnnouncementMask, DecisionRequestKind::Announcement, 140, 2, 2,
                       mask_items, [](const SelectionContinuation& state, const std::vector<std::uint8_t>& response) {
                           std::uint64_t mask = 0;
                           for (const auto index : state.selected_indices) {
                               mask |= item_by_source(state, index).mask_value;
                           }
                           require(response == ygo::protocol::encode_uint64_response(mask),
                                   "announcement mask response did not preserve selected bits");
                       }, 15);
}

std::uint64_t tribute_value(const SelectionContinuation& state, const std::vector<std::uint32_t>& indices) {
    std::uint64_t total = 0;
    for (const auto index : indices) {
        total += state.items[index].primary_value;
    }
    return total;
}

bool tribute_can_finish_after(const SelectionContinuation& state, std::uint32_t picked_index) {
    auto selected_indices = state.selected_indices;
    selected_indices.push_back(picked_index);
    const auto tail = all_indices_after(state, picked_index);
    const auto subset_count = std::size_t{1} << tail.size();
    for (std::size_t mask = 0; mask < subset_count; ++mask) {
        auto candidate = selected_indices;
        for (std::size_t bit = 0; bit < tail.size(); ++bit) {
            if ((mask & (std::size_t{1} << bit)) != 0) {
                candidate.push_back(tail[bit]);
            }
        }
        if (candidate.size() <= state.max_count && tribute_value(state, candidate) >= state.required_amount) {
            return true;
        }
    }
    return false;
}

void test_tribute() {
    SelectionContinuation continuation;
    continuation.continuation_kind = ContinuationKind::Tribute;
    continuation.original_message_type = 20;
    continuation.raw_message_hash = "oracle.tribute";
    continuation.required_amount = 3;
    continuation.max_count = 3;
    for (const auto value : {1u, 2u, 1u, 3u}) {
        ContinuationItem item;
        item.source_index = static_cast<std::uint32_t>(continuation.items.size());
        item.primary_value = value;
        continuation.items.push_back(item);
    }
    auto request = ygo::protocol::make_continuation_request(DecisionRequestKind::Tribute, 0, 20, "tribute", 8,
                                                            std::move(continuation));
    std::set<std::string> visited;
    std::function<void(const DecisionRequest&)> visit = [&](const DecisionRequest& current) {
        const auto& state = *current.continuation;
        if (!visited.insert(state.continuation_id).second) {
            return;
        }
        assert_metrics(state, current);
        std::vector<std::uint32_t> expected;
        const auto last = state.selected_indices.empty() ? 0u : state.selected_indices.back();
        for (const auto& item : state.items) {
            if (contains(state.selected_indices, item.source_index) ||
                (!state.selected_indices.empty() && item.source_index <= last) ||
                !tribute_can_finish_after(state, item.source_index)) {
                continue;
            }
            expected.push_back(item.source_index);
        }
        require(pick_indices(current) == expected, "tribute weighted feasibility disagrees with the oracle");
        const bool can_finish = state.selected_indices.size() <= state.max_count &&
                                tribute_value(state, state.selected_indices) >= state.required_amount;
        const auto finish = std::find_if(current.candidates.begin(), current.candidates.end(),
                                         [](const ActionCandidate& candidate) {
                                             return candidate.action_kind == ActionKind::Finish;
                                         });
        require((finish != current.candidates.end()) == can_finish,
                "tribute finish legality disagrees with the oracle");
        if (finish != current.candidates.end()) {
            const auto transition = ygo::protocol::apply_continuation_action(current, finish->semantic_key);
            require(transition.engine_response == ygo::protocol::encode_card_index_response(state.selected_indices),
                    "tribute terminal response did not preserve the chosen cards");
        }
        for (const auto index : expected) {
            const auto transition = ygo::protocol::apply_continuation_action(current, find_pick(current, index).semantic_key);
            assert_intermediate(transition);
            visit(transition.request);
        }
    };
    visit(request);
    if (visited.size() != 15) {
        throw std::runtime_error("tribute oracle did not reach every feasible subset state: " +
                                 std::to_string(visited.size()));
    }
}

bool exact_values_reachable(const std::vector<ContinuationItem>& items, std::size_t position,
                            std::int64_t remaining) {
    if (remaining <= 0 || position >= items.size()) {
        return false;
    }
    const auto& item = items[position];
    if (position + 1 == items.size()) {
        return remaining == item.primary_value ||
               (item.secondary_value > 0 && remaining == item.secondary_value);
    }
    if (remaining > item.primary_value &&
        exact_values_reachable(items, position + 1, remaining - item.primary_value)) {
        return true;
    }
    return item.secondary_value > 0 && remaining > item.secondary_value &&
           exact_values_reachable(items, position + 1, remaining - item.secondary_value);
}

bool sum_terminal_oracle(const SelectionContinuation& state) {
    std::vector<ContinuationItem> all = state.mandatory_items;
    for (const auto index : state.selected_indices) {
        all.push_back(state.items[index]);
    }
    if (all.empty()) {
        return false;
    }
    if (state.greater_sum) {
        std::int64_t minimum_sum = 0;
        std::int64_t maximum_sum = 0;
        std::uint32_t minimum_value = UINT32_MAX;
        for (const auto& item : all) {
            const auto minimum = item.secondary_value == 0
                                     ? item.primary_value
                                     : std::min(item.primary_value, item.secondary_value);
            const auto maximum = std::max(item.primary_value, item.secondary_value);
            minimum_sum += minimum;
            maximum_sum += maximum;
            minimum_value = std::min(minimum_value, minimum);
        }
        return maximum_sum >= state.target_sum && minimum_sum - minimum_value < state.target_sum;
    }
    const auto count = static_cast<std::uint32_t>(state.selected_indices.size());
    return count >= state.min_count && count <= state.max_count &&
           exact_values_reachable(all, 0, state.target_sum);
}

bool sum_can_finish_after(const SelectionContinuation& state, std::uint32_t picked_index) {
    std::vector<std::uint32_t> tail;
    for (const auto& item : state.items) {
        if (item.source_index > picked_index && !contains(state.selected_indices, item.source_index)) {
            tail.push_back(item.source_index);
        }
    }
    auto selected_indices = state.selected_indices;
    selected_indices.push_back(picked_index);
    const auto subset_count = std::size_t{1} << tail.size();
    for (std::size_t mask = 0; mask < subset_count; ++mask) {
        auto candidate = state;
        candidate.selected_indices = selected_indices;
        for (std::size_t bit = 0; bit < tail.size(); ++bit) {
            if ((mask & (std::size_t{1} << bit)) != 0) {
                candidate.selected_indices.push_back(tail[bit]);
            }
        }
        if (candidate.greater_sum || candidate.selected_indices.size() <= candidate.max_count) {
            if (sum_terminal_oracle(candidate)) {
                return true;
            }
        }
    }
    return false;
}

void test_sum(bool greater_sum) {
    SelectionContinuation continuation;
    continuation.continuation_kind = ContinuationKind::Sum;
    continuation.original_message_type = 23;
    continuation.raw_message_hash = greater_sum ? "oracle.sum.greater" : "oracle.sum.exact";
    continuation.target_sum = greater_sum ? 6 : 5;
    continuation.min_count = greater_sum ? 0 : 1;
    continuation.max_count = greater_sum ? 0 : 3;
    continuation.exact_sum = !greater_sum;
    continuation.greater_sum = greater_sum;
    ContinuationItem mandatory;
    mandatory.source_index = 0;
    mandatory.primary_value = 1;
    mandatory.secondary_value = 2;
    continuation.mandatory_items.push_back(mandatory);
    for (const auto values : {std::pair{2u, 3u}, std::pair{1u, 1u}, std::pair{4u, 0u}, std::pair{3u, 5u}}) {
        ContinuationItem item;
        item.source_index = static_cast<std::uint32_t>(continuation.items.size());
        item.primary_value = values.first;
        item.secondary_value = values.second;
        continuation.items.push_back(item);
    }
    auto request = ygo::protocol::make_continuation_request(DecisionRequestKind::Sum, 0, 23, "sum", 9,
                                                            std::move(continuation));
    std::set<std::string> visited;
    std::function<void(const DecisionRequest&)> visit = [&](const DecisionRequest& current) {
        const auto& state = *current.continuation;
        if (!visited.insert(state.continuation_id).second) {
            return;
        }
        assert_metrics(state, current);
        std::vector<std::uint32_t> expected;
        const auto last = state.selected_indices.empty() ? 0u : state.selected_indices.back();
        for (const auto& item : state.items) {
            if (contains(state.selected_indices, item.source_index) ||
                (!state.selected_indices.empty() && item.source_index <= last) ||
                !sum_can_finish_after(state, item.source_index)) {
                continue;
            }
            expected.push_back(item.source_index);
        }
        require(pick_indices(current) == expected, "sum feasibility disagrees with the oracle");
        const auto finish = std::find_if(current.candidates.begin(), current.candidates.end(),
                                         [](const ActionCandidate& candidate) {
                                             return candidate.action_kind == ActionKind::Finish;
                                         });
        require((finish != current.candidates.end()) == sum_terminal_oracle(state),
                "sum finish legality disagrees with the oracle");
        if (finish != current.candidates.end()) {
            const auto transition = ygo::protocol::apply_continuation_action(current, finish->semantic_key);
            require(transition.terminal && transition.engine_advanced,
                    "sum finish did not produce a terminal transition");
        }
        for (const auto index : expected) {
            const auto transition = ygo::protocol::apply_continuation_action(current, find_pick(current, index).semantic_key);
            assert_intermediate(transition);
            visit(transition.request);
        }
    };
    visit(request);
    require(!visited.empty(), "sum oracle did not visit an initial state");
}

void test_counter() {
    SelectionContinuation continuation;
    continuation.continuation_kind = ContinuationKind::CounterAllocation;
    continuation.original_message_type = 22;
    continuation.raw_message_hash = "oracle.counter";
    continuation.required_amount = 3;
    for (const auto capacity : {0u, 1u, 2u, 2u}) {
        ContinuationItem item;
        item.source_index = static_cast<std::uint32_t>(continuation.items.size());
        item.capacity = capacity;
        continuation.items.push_back(item);
    }
    auto request = ygo::protocol::make_continuation_request(DecisionRequestKind::Counter, 0, 22, "counter", 10,
                                                            std::move(continuation));
    std::set<std::string> visited;
    std::function<void(const DecisionRequest&)> visit = [&](const DecisionRequest& current) {
        const auto& state = *current.continuation;
        if (!visited.insert(state.continuation_id).second) {
            return;
        }
        assert_metrics(state, current);
        const auto assigned = state.assigned_amounts.size();
        std::vector<std::uint32_t> expected;
        if (assigned < state.items.size()) {
            const auto used = std::accumulate(state.assigned_amounts.begin(), state.assigned_amounts.end(), 0u);
            require(used <= state.required_amount, "counter state exceeded the requested amount");
            const auto remaining = state.required_amount - used;
            std::uint32_t future_capacity = 0;
            for (std::size_t index = assigned + 1; index < state.items.size(); ++index) {
                future_capacity += state.items[index].capacity;
            }
            const auto upper = std::min(state.items[assigned].capacity, remaining);
            for (std::uint32_t amount = 0; amount <= upper; ++amount) {
                if (remaining - amount <= future_capacity) {
                    expected.push_back(amount);
                }
            }
        }
        std::vector<std::uint32_t> actual;
        for (const auto& candidate : current.candidates) {
            if (candidate.action_kind == ActionKind::AssignAmount) {
                actual.push_back(static_cast<std::uint32_t>(candidate.amount));
            }
        }
        require(actual == expected, "counter amount domain disagrees with the oracle");
        const bool should_finish = assigned == state.items.size() &&
                                   std::accumulate(state.assigned_amounts.begin(), state.assigned_amounts.end(), 0u) ==
                                       state.required_amount;
        const auto finish = std::find_if(current.candidates.begin(), current.candidates.end(),
                                         [](const ActionCandidate& candidate) {
                                             return candidate.action_kind == ActionKind::Finish;
                                         });
        require((finish != current.candidates.end()) == should_finish,
                "counter finish legality disagrees with the oracle");
        if (finish != current.candidates.end()) {
            const auto transition = ygo::protocol::apply_continuation_action(current, finish->semantic_key);
            require(transition.engine_response == ygo::protocol::encode_counter_response(state.assigned_amounts),
                    "counter terminal response disagrees with the oracle");
        }
        for (const auto& candidate : current.candidates) {
            if (candidate.action_kind != ActionKind::AssignAmount) {
                continue;
            }
            const auto transition = ygo::protocol::apply_continuation_action(current, candidate.semantic_key);
            assert_intermediate(transition);
            visit(transition.request);
        }
    };
    visit(request);
    require(!visited.empty(), "counter oracle did not visit an initial state");
}

void test_ordering() {
    SelectionContinuation continuation;
    continuation.continuation_kind = ContinuationKind::Ordering;
    continuation.original_message_type = 25;
    continuation.raw_message_hash = "oracle.ordering";
    for (std::uint32_t index = 0; index < 4; ++index) {
        ContinuationItem item;
        item.source_index = index;
        continuation.items.push_back(item);
    }
    auto request = ygo::protocol::make_continuation_request(DecisionRequestKind::Ordering, 0, 25, "ordering", 11,
                                                            std::move(continuation));
    std::set<std::string> visited;
    std::function<void(const DecisionRequest&)> visit = [&](const DecisionRequest& current) {
        const auto& state = *current.continuation;
        if (!visited.insert(state.continuation_id).second) {
            return;
        }
        assert_metrics(state, current);
        std::vector<std::uint32_t> remaining;
        for (const auto& item : state.items) {
            if (!contains(state.selected_indices, item.source_index)) {
                remaining.push_back(item.source_index);
            }
        }
        std::vector<std::uint32_t> actual;
        for (const auto& candidate : current.candidates) {
            if (candidate.action_kind == ActionKind::Pick) {
                actual.push_back(candidate.source_index);
            }
        }
        require(actual == remaining, "ordering omitted or duplicated a remaining candidate");
        const auto bypass = find_action(current, ActionKind::Cancel);
        require(bypass.exact_response_bytes == ygo::protocol::encode_order_bypass_response(),
                "ordering bypass response was not the pinned signed-byte sentinel");
        const bool should_finish = remaining.empty();
        const auto finish = std::find_if(current.candidates.begin(), current.candidates.end(),
                                         [](const ActionCandidate& candidate) {
                                             return candidate.action_kind == ActionKind::Finish;
                                         });
        require((finish != current.candidates.end()) == should_finish,
                "ordering finish legality disagrees with the oracle");
        if (finish != current.candidates.end()) {
            const auto transition = ygo::protocol::apply_continuation_action(current, finish->semantic_key);
            require(transition.engine_response == ygo::protocol::encode_order_response(state.selected_indices),
                    "ordering terminal response did not preserve the permutation");
        }
        const auto bypass_transition = ygo::protocol::apply_continuation_action(current, bypass.semantic_key);
        require(bypass_transition.terminal && bypass_transition.engine_advanced &&
                    bypass_transition.engine_response == ygo::protocol::encode_order_bypass_response(),
                "ordering bypass was not terminal");
        for (const auto index : remaining) {
            const auto transition = ygo::protocol::apply_continuation_action(current, find_pick(current, index).semantic_key);
            assert_intermediate(transition);
            visit(transition.request);
        }
    };
    visit(request);
    require(visited.size() == 65, "ordering oracle did not reach every partial permutation");
}

int run() {
    test_unordered_and_zone_and_mask();
    test_tribute();
    test_sum(false);
    test_sum(true);
    test_counter();
    test_ordering();
    std::cout << "continuation_oracle=ok\n";
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
