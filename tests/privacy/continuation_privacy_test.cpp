#include <cstdint>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "ocgapi_constants.h"
#include "ygo/protocol/message_decoder.hpp"
#include "ygo/trace/sha256.hpp"

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void append_u32(std::vector<std::uint8_t>& bytes, std::uint32_t value) {
    for (unsigned shift = 0; shift < 32; shift += 8) {
        bytes.push_back(static_cast<std::uint8_t>((value >> shift) & 0xffu));
    }
}

void append_card(std::vector<std::uint8_t>& bytes, std::uint32_t code, std::uint8_t controller,
                 std::uint8_t location, std::uint32_t sequence, std::uint32_t position) {
    append_u32(bytes, code);
    bytes.push_back(controller);
    bytes.push_back(location);
    append_u32(bytes, sequence);
    append_u32(bytes, position);
}

std::vector<std::uint8_t> frame(const std::vector<std::uint8_t>& payload) {
    std::vector<std::uint8_t> result;
    append_u32(result, static_cast<std::uint32_t>(payload.size()));
    result.insert(result.end(), payload.begin(), payload.end());
    return result;
}

ygo::protocol::SelectionContinuation continuation(const std::vector<std::uint8_t>& bytes) {
    const auto decoded = ygo::protocol::decode_messages(bytes, 77);
    require(decoded.decisions.size() == 1, "privacy fixture did not produce one decision");
    require(decoded.decisions.front().continuation.has_value(), "privacy fixture did not produce a continuation");
    const auto& decision = decoded.decisions.front();
    const std::vector<std::uint8_t> payload(bytes.begin() + 4, bytes.end());
    require(decision.raw_message_hash == ygo::trace::sha256_bytes(payload),
            "continuation raw hash did not bind the complete visible payload");
    require(decision.raw_message_hash.find("hidden") == std::string::npos,
            "continuation exposed a raw transport marker");
    return *decision.continuation;
}

void test_card_selection_privacy() {
    std::vector<std::uint8_t> payload = {MSG_SELECT_CARD, 0, 0};
    append_u32(payload, 1);
    append_u32(payload, 2);
    append_u32(payload, 2);
    append_card(payload, 1101, 0, LOCATION_HAND, 4, 0x1234);
    append_card(payload, 2202, 1, LOCATION_MZONE, 2, 0x5678);
    const auto& state = continuation(frame(payload));
    require(state.items.size() == 2, "card continuation item count changed");
    require(state.items[0].card.code == 1101 && state.items[0].card.controller == 0 &&
                state.items[0].card.location == LOCATION_HAND && state.items[0].card.sequence == 4 &&
                state.items[0].card.position == 0x1234,
            "card continuation did not preserve only visible locator fields");
    require(state.items[1].card.code == 2202 && state.items[1].card.controller == 1 &&
                state.items[1].card.location == LOCATION_MZONE && state.items[1].card.sequence == 2 &&
                state.items[1].card.position == 0x5678,
            "card continuation leaked or altered a visible locator field");
    require(state.mandatory_items.empty(), "card continuation acquired hidden mandatory items");
}

void test_tribute_privacy() {
    std::vector<std::uint8_t> payload = {MSG_SELECT_TRIBUTE, 1, 0};
    append_u32(payload, 2);
    append_u32(payload, 2);
    append_u32(payload, 2);
    append_u32(payload, 3301);
    payload.push_back(1);
    payload.push_back(LOCATION_MZONE);
    append_u32(payload, 3);
    payload.push_back(1);
    append_u32(payload, 4402);
    payload.push_back(0);
    payload.push_back(LOCATION_MZONE);
    append_u32(payload, 5);
    payload.push_back(2);
    const auto& state = continuation(frame(payload));
    require(state.items.size() == 2 && state.items[0].primary_value == 1 && state.items[1].primary_value == 2,
            "tribute continuation did not preserve visible release values");
    require(state.items[0].card.code == 3301 && state.items[1].card.code == 4402,
            "tribute continuation exposed a non-domain card");
    require(state.mandatory_items.empty(), "tribute continuation acquired hidden mandatory items");
}

void test_sum_privacy() {
    std::vector<std::uint8_t> payload = {MSG_SELECT_SUM, 0, 0};
    append_u32(payload, 7);
    append_u32(payload, 1);
    append_u32(payload, 2);
    append_u32(payload, 1);
    append_card(payload, 5501, 0, LOCATION_GRAVE, 6, 0x9abc);
    append_u32(payload, 0x00050003);
    append_u32(payload, 2);
    append_card(payload, 6601, 1, LOCATION_HAND, 1, 0xdef0);
    append_u32(payload, 0x00070004);
    append_card(payload, 7702, 1, LOCATION_HAND, 2, 0x1111);
    append_u32(payload, 9);
    const auto& state = continuation(frame(payload));
    require(state.mandatory_items.size() == 1 && state.items.size() == 2,
            "sum continuation changed its visible domains");
    require(state.mandatory_items[0].card.code == 5501 && state.mandatory_items[0].primary_value == 3 &&
                state.mandatory_items[0].secondary_value == 5,
            "sum mandatory contribution was not preserved");
    require(state.items[0].card.code == 6601 && state.items[1].card.code == 7702,
            "sum continuation exposed a card outside the visible optional domain");
}

void test_ordering_privacy() {
    std::vector<std::uint8_t> payload = {MSG_SORT_CARD, 0};
    append_u32(payload, 2);
    append_u32(payload, 8801);
    payload.push_back(0);
    append_u32(payload, LOCATION_HAND);
    append_u32(payload, 7);
    append_u32(payload, 9902);
    payload.push_back(1);
    append_u32(payload, LOCATION_MZONE);
    append_u32(payload, 8);
    const auto& state = continuation(frame(payload));
    require(state.items.size() == 2 && state.items[0].card.code == 8801 && state.items[1].card.code == 9902,
            "ordering continuation did not preserve the visible permutation domain");
    require(state.items[0].card.position == 0 && state.items[1].card.position == 0,
            "ordering continuation invented hidden position information");
}

int run() {
    test_card_selection_privacy();
    test_tribute_privacy();
    test_sum_privacy();
    test_ordering_privacy();
    std::cout << "continuation_privacy=ok\n";
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
