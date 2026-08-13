#include <cstdint>
#include <exception>
#include <iostream>
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

int run() {
    std::vector<std::uint8_t> idle = {MSG_SELECT_IDLECMD, 0};
    append_u32(idle, 1);
    append_u32(idle, 123456);
    idle.push_back(0);
    idle.push_back(LOCATION_HAND);
    append_u32(idle, 3);
    for (int i = 0; i < 5; ++i) {
        append_u32(idle, 0);
    }
    idle.push_back(1);
    idle.push_back(1);
    idle.push_back(0);
    const auto decoded = ygo::protocol::decode_messages(frame(idle));
    if (!decoded.interactive || decoded.decisions.size() != 1 || decoded.decisions[0].candidates.size() != 3) {
        std::cerr << "idle candidate preservation failed\n";
        return 1;
    }
    if (decoded.decisions[0].candidates.front().semantic_key != "idle.0.0.123456.0.2.3") {
        std::cerr << "semantic key is not deterministic: "
                  << decoded.decisions[0].candidates.front().semantic_key << "\n";
        return 1;
    }
    const auto& selected = ygo::protocol::select_candidate(decoded.decisions[0], "idle.0.0.123456.0.2.3");
    if (selected.exact_response_bytes.size() != 4 || selected.exact_response_bytes[0] != 0) {
        std::cerr << "exact response bytes missing\n";
        return 1;
    }

    std::vector<std::uint8_t> position = {MSG_SELECT_POSITION, 1};
    append_u32(position, 77);
    position.push_back(POS_FACEUP_ATTACK | POS_FACEUP_DEFENSE);
    const auto position_decoded = ygo::protocol::decode_messages(frame(position));
    if (position_decoded.decisions[0].candidates.size() != 2) {
        std::cerr << "position candidate preservation failed\n";
        return 1;
    }

    std::vector<std::uint8_t> yes_no = {MSG_SELECT_YESNO, 1};
    append_u64(yes_no, 99);
    const auto yes_no_decoded = ygo::protocol::decode_messages(frame(yes_no));
    if (yes_no_decoded.decisions[0].candidates.size() != 2) {
        std::cerr << "yes/no candidate preservation failed\n";
        return 1;
    }

    try {
        (void)ygo::protocol::select_candidate(decoded.decisions[0], "idle.0.0.stale");
        std::cerr << "stale semantic key was accepted\n";
        return 1;
    } catch (const ygo::protocol::ProtocolError& error) {
        if (error.code() != ygo::protocol::ProtocolErrorCode::InvalidSemanticKey) {
            std::cerr << "stale semantic key returned the wrong error\n";
            return 1;
        }
    }

    try {
        auto duplicate_request = decoded.decisions.front();
        duplicate_request.candidates.push_back(duplicate_request.candidates.front());
        ygo::protocol::validate_candidate_set(duplicate_request);
        std::cerr << "duplicate semantic key was accepted\n";
        return 1;
    } catch (const ygo::protocol::ProtocolError& error) {
        if (error.code() != ygo::protocol::ProtocolErrorCode::IncompleteCandidates) {
            std::cerr << "duplicate semantic key returned the wrong error\n";
            return 1;
        }
    }

    const std::vector<std::uint8_t> unsupported_payload = {MSG_SELECT_OPTION, 1, 0};
    try {
        (void)ygo::protocol::decode_messages(frame(unsupported_payload));
        std::cerr << "unsupported decision was accepted\n";
        return 1;
    } catch (const ygo::protocol::ProtocolError& error) {
        if (error.code() != ygo::protocol::ProtocolErrorCode::UnsupportedDecision ||
            error.message_type() != MSG_SELECT_OPTION || error.player() != 1 || error.raw_message() != unsupported_payload) {
            std::cerr << "unsupported decision lacked structured context\n";
            return 1;
        }
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
