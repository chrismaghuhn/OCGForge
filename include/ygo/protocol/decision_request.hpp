#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "ygo/protocol/action_candidate.hpp"

namespace ygo::protocol {

enum class DecisionRequestKind {
    IdleCommand,
    BattleCommand,
    Chain,
    CardSelection,
    Place,
    Position,
    YesNo,
    Unsupported,
};

struct DecisionRequest {
    DecisionRequestKind kind = DecisionRequestKind::Unsupported;
    std::uint8_t player = 0;
    std::uint8_t engine_message_type = 0;
    std::string engine_message_name;
    std::vector<ActionCandidate> candidates;
};

}  // namespace ygo::protocol
