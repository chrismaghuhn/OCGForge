#include <cstdint>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "ocgapi_constants.h"
#include "ygo/protocol/message_decoder.hpp"
#include "ygo/protocol/protocol_error.hpp"

namespace {

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

void expect_unsupported(const std::vector<std::uint8_t>& payload) {
    try {
        (void)ygo::protocol::decode_messages(frame(payload));
        throw std::runtime_error("unsupported family was accepted");
    } catch (const ygo::protocol::ProtocolError& error) {
        if (error.code() != ygo::protocol::ProtocolErrorCode::UnsupportedDecision ||
            error.message_type() != payload.front() || error.player() != (payload.size() > 1 ? payload[1] : 255) ||
            error.raw_message() != payload) {
            throw std::runtime_error("unsupported family did not fail closed with structured context");
        }
    }
}

int run() {
    expect_unsupported({MSG_REQUEST_DECK, 0});
    expect_unsupported({MSG_ROCK_PAPER_SCISSORS, 1});
    expect_unsupported({MSG_ANNOUNCE_CARD, 0, 0});

    const auto retry = ygo::protocol::decode_messages(frame({MSG_RETRY}));
    if (!retry.retry || retry.interactive || retry.terminal || !retry.decisions.empty()) {
        throw std::runtime_error("MSG_RETRY was not classified as an explicit protocol rejection");
    }

    std::vector<std::uint8_t> option = {MSG_SELECT_OPTION, 0, 4};
    append_u64(option, 7);
    append_u64(option, 7);
    append_u64(option, 8);
    append_u64(option, 9);
    const auto option_request = ygo::protocol::decode_messages(frame(option)).decisions.front();
    if (option_request.candidates.size() != 4 || option_request.candidates[0].semantic_key ==
                                                       option_request.candidates[1].semantic_key) {
        throw std::runtime_error("option domain was truncated or duplicate payloads collapsed");
    }

    std::vector<std::uint8_t> ordering = {MSG_SORT_CARD, 0};
    append_u32(ordering, 128);
    for (std::uint32_t index = 0; index < 128; ++index) {
        append_u32(ordering, 10000 + index);
        ordering.push_back(0);
        append_u32(ordering, LOCATION_HAND);
        append_u32(ordering, index);
    }
    const auto ordering_request = ygo::protocol::decode_messages(frame(ordering)).decisions.front();
    if (!ordering_request.continuation.has_value() || ordering_request.candidates.size() != 129) {
        throw std::runtime_error("ordering domain was truncated below the pinned signed-byte boundary");
    }

    std::vector<std::uint8_t> malformed = {MSG_SELECT_OPTION, 0, 2};
    append_u64(malformed, 1);
    try {
        (void)ygo::protocol::decode_messages(frame(malformed));
        throw std::runtime_error("truncated option payload was accepted");
    } catch (const ygo::protocol::ProtocolError& error) {
        if (error.code() != ygo::protocol::ProtocolErrorCode::MalformedMessage) {
            throw std::runtime_error("truncated option payload returned the wrong error");
        }
    }

    std::cout << "decision_fail_closed=ok\n";
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
