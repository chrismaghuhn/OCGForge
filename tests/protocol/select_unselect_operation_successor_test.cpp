#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "ocgapi_constants.h"
#include "ygo/protocol/action_candidate.hpp"
#include "ygo/protocol/message_decoder.hpp"

namespace {

using ygo::protocol::ActionCandidate;
using ygo::protocol::ActionKind;
using ygo::protocol::CardSelectionOperation;
using ygo::protocol::DecodedMessage;
using ygo::protocol::DecisionRequest;

void require(const bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void append_u32(std::vector<std::uint8_t>& bytes, const std::uint32_t value) {
    for (unsigned shift = 0; shift < 32; shift += 8) {
        bytes.push_back(static_cast<std::uint8_t>((value >> shift) & 0xffu));
    }
}

void append_u64(std::vector<std::uint8_t>& bytes, const std::uint64_t value) {
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

void append_card(std::vector<std::uint8_t>& payload,
                 const std::uint32_t code,
                 const std::uint8_t controller,
                 const std::uint32_t location,
                 const std::uint32_t sequence,
                 const std::uint32_t position = 0) {
    append_u32(payload, code);
    payload.push_back(controller);
    payload.push_back(static_cast<std::uint8_t>(location));
    append_u32(payload, sequence);
    append_u32(payload, position);
}

std::vector<std::uint8_t> select_unselect_payload() {
    std::vector<std::uint8_t> payload = {MSG_SELECT_UNSELECT_CARD, 0, 1, 0};
    append_u32(payload, 0);
    append_u32(payload, 2);
    append_u32(payload, 1);
    append_card(payload, 801, 0, LOCATION_HAND, 0);
    append_u32(payload, 1);
    append_card(payload, 802, 1, LOCATION_HAND, 1);
    return payload;
}

std::vector<std::uint8_t> option_payload() {
    std::vector<std::uint8_t> payload = {MSG_SELECT_OPTION, 0, 3};
    append_u64(payload, 100);
    append_u64(payload, 100);
    append_u64(payload, 200);
    return payload;
}

std::vector<std::uint8_t> yes_no_payload() {
    std::vector<std::uint8_t> payload = {MSG_SELECT_YESNO, 0};
    append_u64(payload, 501);
    return payload;
}

void require_candidate_equal_except_operation(const ActionCandidate& historical,
                                              const ActionCandidate& corrected) {
    require(historical.action_kind == corrected.action_kind,
            "action kind changed between historical and corrected decoder");
    require(historical.semantic_key == corrected.semantic_key,
            "semantic key changed between historical and corrected decoder");
    require(historical.source_card == corrected.source_card &&
                historical.source_controller == corrected.source_controller &&
                historical.source_location == corrected.source_location &&
                historical.source_sequence == corrected.source_sequence &&
                historical.source_position == corrected.source_position,
            "source descriptor changed between historical and corrected decoder");
    require(historical.target_card == corrected.target_card &&
                historical.target_controller == corrected.target_controller &&
                historical.target_location == corrected.target_location &&
                historical.target_sequence == corrected.target_sequence,
            "target descriptor changed between historical and corrected decoder");
    require(historical.phase == corrected.phase && historical.position == corrected.position &&
                historical.source_index == corrected.source_index &&
                historical.amount == corrected.amount,
            "candidate scalar metadata changed between historical and corrected decoder");
    require(historical.choice_value == corrected.choice_value &&
                historical.choice_index == corrected.choice_index &&
                historical.continuation_id == corrected.continuation_id &&
                historical.continuation_operation == corrected.continuation_operation,
            "candidate auxiliary metadata changed between historical and corrected decoder");
    require(historical.submits_engine_response == corrected.submits_engine_response &&
                historical.exact_response_bytes == corrected.exact_response_bytes,
            "exact response metadata changed between historical and corrected decoder");
}

void require_request_shape_equal_except_operation(const DecisionRequest& historical,
                                                  const DecisionRequest& corrected) {
    require(historical.kind == corrected.kind &&
                historical.engine_message_type == corrected.engine_message_type &&
                historical.engine_message_name == corrected.engine_message_name &&
                historical.player == corrected.player &&
                historical.candidates.size() == corrected.candidates.size(),
            "request shape changed between historical and corrected decoder");
    require(historical.continuation.has_value() == corrected.continuation.has_value(),
            "continuation presence changed between historical and corrected decoder");
    for (std::size_t index = 0; index < historical.candidates.size(); ++index) {
        require_candidate_equal_except_operation(historical.candidates[index],
                                                 corrected.candidates[index]);
    }
}

void test_corrected_select_unselect_mapping() {
    const auto bytes = frame(select_unselect_payload());
    const auto historical = ygo::protocol::decode_messages(bytes, 17);
    const auto corrected = ygo::protocol::decode_messages_v4(bytes, 17);
    require(historical.decisions.size() == 1 && corrected.decisions.size() == 1,
            "select-unselect decoder did not emit exactly one decision");
    require_request_shape_equal_except_operation(historical.decisions.front(),
                                                 corrected.decisions.front());

    const auto& historical_request = historical.decisions.front();
    const auto& corrected_request = corrected.decisions.front();
    require(historical_request.candidates.size() == 3 && corrected_request.candidates.size() == 3,
            "select-unselect candidate count changed");
    require(historical_request.candidates[0].card_selection_operation ==
                CardSelectionOperation::Unselect &&
                historical_request.candidates[1].card_selection_operation ==
                    CardSelectionOperation::Select &&
                historical_request.candidates[2].card_selection_operation ==
                    CardSelectionOperation::None,
            "historical decoder direction changed");
    require(corrected_request.candidates[0].card_selection_operation ==
                CardSelectionOperation::Select &&
                corrected_request.candidates[1].card_selection_operation ==
                    CardSelectionOperation::Unselect &&
                corrected_request.candidates[2].card_selection_operation ==
                    CardSelectionOperation::None,
            "corrected decoder did not map first list to Select and second list to Unselect");
    require(corrected_request.candidates[2].action_kind == ActionKind::Finish &&
                corrected_request.candidates[2].card_selection_operation ==
                    CardSelectionOperation::None,
            "Finish did not retain None operation metadata");
}

void test_non_unselect_full_decoder_equivalence() {
    for (const auto& payload : {option_payload(), yes_no_payload()}) {
        const auto bytes = frame(payload);
        const auto historical = ygo::protocol::decode_messages(bytes, 23);
        const auto corrected = ygo::protocol::decode_messages_v4(bytes, 23);
        require(historical.interactive == corrected.interactive &&
                    historical.terminal == corrected.terminal &&
                    historical.retry == corrected.retry &&
                    historical.message_type == corrected.message_type &&
                    historical.winner == corrected.winner &&
                    historical.win_reason == corrected.win_reason &&
                    historical.decisions.size() == corrected.decisions.size(),
                "non-unselect full decoder status changed");
        require_request_shape_equal_except_operation(historical.decisions.front(),
                                                     corrected.decisions.front());
        for (const auto& candidate : corrected.decisions.front().candidates) {
            require(candidate.card_selection_operation == CardSelectionOperation::None,
                    "non-unselect candidate acquired operation metadata");
        }
    }
}

}  // namespace

int main() {
    try {
        test_corrected_select_unselect_mapping();
        test_non_unselect_full_decoder_equivalence();
        std::cout << "select/unselect protocol successor tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
