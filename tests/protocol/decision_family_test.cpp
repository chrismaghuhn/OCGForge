#include <cstdint>
#include <exception>
#include <iostream>
#include <string>
#include <vector>

#include "ocgapi_constants.h"
#include "ygo/protocol/continuation.hpp"
#include "ygo/protocol/message_decoder.hpp"
#include "ygo/protocol/protocol_error.hpp"

namespace {

using ygo::protocol::ActionKind;

void append_u16(std::vector<std::uint8_t>& bytes, std::uint16_t value) {
    bytes.push_back(static_cast<std::uint8_t>(value & 0xffu));
    bytes.push_back(static_cast<std::uint8_t>((value >> 8) & 0xffu));
}

void append_u32(std::vector<std::uint8_t>& bytes, std::uint32_t value) {
    for (unsigned shift = 0; shift < 32; shift += 8) {
        bytes.push_back(static_cast<std::uint8_t>((value >> shift) & 0xffu));
    }
}

void append_u64(std::vector<std::uint8_t>& bytes, std::uint64_t value) {
    for (unsigned shift = 0; shift < 64; shift += 8) {
        bytes.push_back(static_cast<std::uint8_t>((value >> shift) & 0xffu));
    }
}

std::vector<std::uint8_t> frame(const std::vector<std::uint8_t>& payload) {
    std::vector<std::uint8_t> result;
    append_u32(result, static_cast<std::uint32_t>(payload.size()));
    result.insert(result.end(), payload.begin(), payload.end());
    return result;
}

void append_card(std::vector<std::uint8_t>& payload, std::uint32_t code, std::uint32_t sequence,
                 std::uint32_t position = 0) {
    append_u32(payload, code);
    payload.push_back(0);
    payload.push_back(LOCATION_HAND);
    append_u32(payload, sequence);
    append_u32(payload, position);
}

void append_sort_card(std::vector<std::uint8_t>& payload, std::uint32_t code, std::uint8_t controller,
                      std::uint32_t location, std::uint32_t sequence) {
    append_u32(payload, code);
    payload.push_back(controller);
    append_u32(payload, location);
    append_u32(payload, sequence);
}

const ygo::protocol::ActionCandidate& find_kind(const ygo::protocol::DecisionRequest& request,
                                                ygo::protocol::ActionKind kind) {
    for (const auto& candidate : request.candidates) {
        if (candidate.action_kind == kind) {
            return candidate;
        }
    }
    throw std::runtime_error("expected action kind was not found: " +
                             std::to_string(static_cast<int>(kind)));
}

const ygo::protocol::ActionCandidate& find_amount(const ygo::protocol::DecisionRequest& request,
                                                  std::int32_t amount) {
    for (const auto& candidate : request.candidates) {
        if (candidate.action_kind == ygo::protocol::ActionKind::AssignAmount && candidate.amount == amount) {
            return candidate;
        }
    }
    throw std::runtime_error("expected amount action was not found");
}

int run() {
    std::vector<std::uint8_t> option = {MSG_SELECT_OPTION, 0, 3};
    append_u64(option, 100);
    append_u64(option, 100);
    append_u64(option, 200);
    const auto option_request = ygo::protocol::decode_messages(frame(option)).decisions.front();
    if (option_request.candidates.size() != 3 || option_request.candidates[0].semantic_key ==
                                                   option_request.candidates[1].semantic_key ||
        option_request.candidates[1].exact_response_bytes[0] != 1) {
        std::cerr << "select-option candidates were not preserved\n";
        return 1;
    }

    std::vector<std::uint8_t> cards = {MSG_SELECT_CARD, 0, 0};
    append_u32(cards, 2);
    append_u32(cards, 3);
    append_u32(cards, 3);
    append_card(cards, 101, 0);
    append_card(cards, 202, 1);
    append_card(cards, 303, 2);
    auto card_request = ygo::protocol::decode_messages(frame(cards)).decisions.front();
    const auto first_card = card_request.candidates.front().semantic_key;
    auto card_step = ygo::protocol::apply_continuation_action(card_request, first_card);
    const auto second_card = find_kind(card_step.request, ygo::protocol::ActionKind::Pick).semantic_key;
    card_step = ygo::protocol::apply_continuation_action(card_step.request, second_card);
    const auto& card_finish = find_kind(card_step.request, ygo::protocol::ActionKind::Finish);
    const auto card_final = ygo::protocol::apply_continuation_action(card_step.request, card_finish.semantic_key);
    if (!card_final.terminal || card_final.engine_response.size() != 16 || card_final.engine_response[4] != 2) {
        std::cerr << "multi-card continuation did not encode a legal final set\n";
        return 1;
    }

    std::vector<std::uint8_t> tribute = {MSG_SELECT_TRIBUTE, 0, 0};
    append_u32(tribute, 2);
    append_u32(tribute, 2);
    append_u32(tribute, 3);
    append_u32(tribute, 401);
    tribute.push_back(0);
    tribute.push_back(LOCATION_MZONE);
    append_u32(tribute, 0);
    tribute.push_back(1);
    append_u32(tribute, 402);
    tribute.push_back(0);
    tribute.push_back(LOCATION_MZONE);
    append_u32(tribute, 1);
    tribute.push_back(2);
    append_u32(tribute, 403);
    tribute.push_back(0);
    tribute.push_back(LOCATION_MZONE);
    append_u32(tribute, 2);
    tribute.push_back(1);
    auto tribute_request = ygo::protocol::decode_messages(frame(tribute)).decisions.front();
    if (tribute_request.candidates.size() != 2 || tribute_request.candidates.front().action_kind != ActionKind::Pick) {
        std::cerr << "tribute completion pruning was incorrect\n";
        return 1;
    }
    auto tribute_step = ygo::protocol::apply_continuation_action(
        tribute_request, tribute_request.candidates.front().semantic_key);
    tribute_step = ygo::protocol::apply_continuation_action(
        tribute_step.request, find_kind(tribute_step.request, ActionKind::Pick).semantic_key);
    if (find_kind(tribute_step.request, ActionKind::Finish).exact_response_bytes.size() == 0) {
        std::cerr << "tribute finish was not exposed\n";
        return 1;
    }

    std::vector<std::uint8_t> weighted_single = {MSG_SELECT_TRIBUTE, 0, 0};
    append_u32(weighted_single, 3);
    append_u32(weighted_single, 3);
    append_u32(weighted_single, 1);
    append_u32(weighted_single, 404);
    weighted_single.push_back(0);
    weighted_single.push_back(LOCATION_MZONE);
    append_u32(weighted_single, 0);
    weighted_single.push_back(3);
    const auto weighted_single_request = ygo::protocol::decode_messages(frame(weighted_single)).decisions.front();
    if (!weighted_single_request.continuation.has_value() || weighted_single_request.candidates.size() != 1) {
        std::cerr << "tribute max-card-count was confused with release weight\n";
        return 1;
    }

    std::vector<std::uint8_t> sum = {MSG_SELECT_SUM, 0, 0};
    append_u32(sum, 5);
    append_u32(sum, 2);
    append_u32(sum, 2);
    append_u32(sum, 0);
    append_u32(sum, 3);
    append_card(sum, 501, 0);
    append_u32(sum, 0x00030002);
    append_card(sum, 502, 1);
    append_u32(sum, 2);
    append_card(sum, 503, 2);
    append_u32(sum, 3);
    auto sum_request = ygo::protocol::decode_messages(frame(sum)).decisions.front();
    auto sum_step = ygo::protocol::apply_continuation_action(sum_request, sum_request.candidates.front().semantic_key);
    sum_step = ygo::protocol::apply_continuation_action(sum_step.request,
                                                        find_kind(sum_step.request, ActionKind::Pick).semantic_key);
    if (find_kind(sum_step.request, ActionKind::Finish).exact_response_bytes.empty()) {
        std::cerr << "exact sum finish was not exposed\n";
        return 1;
    }

    std::vector<std::uint8_t> place = {MSG_SELECT_PLACE, 0, 2};
    append_u32(place, 0xfffffffc);
    auto place_request = ygo::protocol::decode_messages(frame(place)).decisions.front();
    if (!place_request.continuation.has_value() || place_request.candidates.empty()) {
        std::cerr << "multi-zone placement was not represented as a continuation\n";
        return 1;
    }
    std::vector<std::uint8_t> disfield = {MSG_SELECT_DISFIELD, 0, 2};
    append_u32(disfield, 0xfffffffc);
    const auto disfield_request = ygo::protocol::decode_messages(frame(disfield)).decisions.front();
    if (!disfield_request.continuation.has_value()) {
        std::cerr << "select-disfield did not use typed zone continuation\n";
        return 1;
    }

    std::vector<std::uint8_t> counter = {MSG_SELECT_COUNTER, 0};
    append_u16(counter, 7);
    append_u16(counter, 3);
    append_u32(counter, 2);
    append_u32(counter, 601);
    counter.push_back(0);
    counter.push_back(LOCATION_MZONE);
    counter.push_back(0);
    append_u16(counter, 2);
    append_u32(counter, 602);
    counter.push_back(0);
    counter.push_back(LOCATION_MZONE);
    counter.push_back(1);
    append_u16(counter, 2);
    auto counter_request = ygo::protocol::decode_messages(frame(counter)).decisions.front();
    auto counter_step = ygo::protocol::apply_continuation_action(
        counter_request, find_amount(counter_request, 1).semantic_key);
    counter_step = ygo::protocol::apply_continuation_action(
        counter_step.request, find_amount(counter_step.request, 2).semantic_key);
    counter_step = ygo::protocol::apply_continuation_action(
        counter_step.request, find_kind(counter_step.request, ActionKind::Finish).semantic_key);
    if (!counter_step.terminal || counter_step.engine_response != std::vector<std::uint8_t>({1, 0, 2, 0})) {
        std::cerr << "counter allocation response was not exact\n";
        return 1;
    }

    std::vector<std::uint8_t> sort = {MSG_SORT_CARD, 0};
    append_u32(sort, 3);
    append_sort_card(sort, 701, 0, LOCATION_HAND, 0);
    append_sort_card(sort, 702, 0, LOCATION_HAND, 1);
    append_sort_card(sort, 703, 0, LOCATION_HAND, 2);
    auto sort_request = ygo::protocol::decode_messages(frame(sort)).decisions.front();
    auto sort_step = ygo::protocol::apply_continuation_action(sort_request,
                                                              sort_request.candidates[2].semantic_key);
    sort_step = ygo::protocol::apply_continuation_action(sort_step.request,
                                                        sort_step.request.candidates[0].semantic_key);
    sort_step = ygo::protocol::apply_continuation_action(sort_step.request,
                                                        sort_step.request.candidates[0].semantic_key);
    sort_step = ygo::protocol::apply_continuation_action(
        sort_step.request, find_kind(sort_step.request, ActionKind::Finish).semantic_key);
    if (!sort_step.terminal || sort_step.engine_response != std::vector<std::uint8_t>({2, 0, 1})) {
        std::cerr << "ordering response did not preserve permutation\n";
        return 1;
    }

    std::vector<std::uint8_t> number = {MSG_ANNOUNCE_NUMBER, 0, 3};
    append_u64(number, 7);
    append_u64(number, 42);
    append_u64(number, 7);
    const auto number_request = ygo::protocol::decode_messages(frame(number)).decisions.front();
    if (number_request.candidates.size() != 3) {
        std::cerr << "announce-number option preservation failed\n";
        return 1;
    }

    std::vector<std::uint8_t> race = {MSG_ANNOUNCE_RACE, 0, 2};
    append_u64(race, 0x15);
    auto race_request = ygo::protocol::decode_messages(frame(race)).decisions.front();
    auto race_step = ygo::protocol::apply_continuation_action(race_request, race_request.candidates.front().semantic_key);
    race_step = ygo::protocol::apply_continuation_action(race_step.request,
                                                         find_kind(race_step.request, ActionKind::Pick).semantic_key);
    if (!find_kind(race_step.request, ActionKind::Finish).submits_engine_response) {
        std::cerr << "announce-race mask finish was not exposed\n";
        return 1;
    }

    std::vector<std::uint8_t> unselect = {MSG_SELECT_UNSELECT_CARD, 0, 1, 0};
    append_u32(unselect, 0);
    append_u32(unselect, 2);
    append_u32(unselect, 1);
    append_card(unselect, 801, 0);
    append_u32(unselect, 1);
    append_card(unselect, 802, 1);
    const auto unselect_request = ygo::protocol::decode_messages(frame(unselect)).decisions.front();
    if (unselect_request.continuation.has_value() || unselect_request.candidates.size() != 3 ||
        find_kind(unselect_request, ActionKind::Finish).exact_response_bytes.empty()) {
        std::cerr << "select-unselect-card was merged into the wrong protocol\n";
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
