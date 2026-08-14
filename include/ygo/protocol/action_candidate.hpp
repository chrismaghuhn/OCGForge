#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace ygo::protocol {

enum class ActionKind {
    IdleCommand,
    BattleCommand,
    Chain,
    Option,
    CardSelection,
    Announcement,
    Place,
    Position,
    YesNo,
    Pick,
    Finish,
    Cancel,
    AssignAmount,
};

struct ActionCandidate {
    ActionKind action_kind = ActionKind::IdleCommand;
    std::string semantic_key;
    std::uint32_t source_card = 0;
    std::uint8_t source_controller = 0;
    std::uint32_t source_location = 0;
    std::uint32_t source_sequence = 0;
    std::uint32_t target_card = 0;
    std::uint8_t target_controller = 0;
    std::uint32_t target_location = 0;
    std::uint32_t target_sequence = 0;
    std::uint32_t phase = 0;
    std::uint8_t position = 0;
    std::uint32_t source_position = 0;
    std::uint32_t source_index = 0;
    std::int32_t amount = -1;
    std::string continuation_id;
    bool submits_engine_response = true;
    std::vector<std::uint8_t> exact_response_bytes;
};

}  // namespace ygo::protocol
