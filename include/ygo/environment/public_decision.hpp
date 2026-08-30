#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "ygo/environment/public_action_identity.hpp"

namespace ygo::environment {

enum class EnvironmentDecisionKind : std::uint8_t {
    IdleCommand,
    BattleCommand,
    Chain,
    Option,
    CardSelection,
    Tribute,
    Sum,
    Place,
    Counter,
    Ordering,
    Announcement,
    UnselectCard,
    Position,
    YesNo,
    Unsupported,
};

enum class EnvironmentActionKind : std::uint8_t {
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
    Unsupported,
};

struct EnvironmentActionCandidate final {
    EnvironmentActionKind action_kind = EnvironmentActionKind::Unsupported;
    std::string public_action_key;
    std::optional<PublicChoice> choice;
    std::optional<PublicCardReference> source_reference;
    std::optional<PublicCardReference> target_reference;
    std::optional<std::uint32_t> phase;
    std::optional<std::uint8_t> position;
    std::optional<std::uint32_t> source_index;
    std::optional<std::int32_t> amount;
    std::string continuation_operation;
    bool submits_engine_response = true;
};

struct EnvironmentContinuationView final {
    std::string continuation_kind;
    std::uint32_t continuation_step = 0;
    std::vector<std::uint32_t> selected_indices;
    std::vector<std::uint32_t> remaining_indices;
    std::vector<std::uint16_t> assigned_amounts;
    std::uint32_t min_count = 0;
    std::uint32_t max_count = 0;
    std::uint32_t target_sum = 0;
    std::uint32_t required_amount = 0;
    std::uint64_t available_mask = 0;
    std::uint64_t selected_mask = 0;
    std::uint32_t continuation_steps = 0;
    bool exact_sum = true;
    bool greater_sum = false;
    bool can_finish = false;
    bool can_cancel = false;
};

struct EnvironmentDecisionRequest final {
    EnvironmentDecisionKind kind = EnvironmentDecisionKind::Unsupported;
    std::uint8_t player = 0;
    std::vector<EnvironmentActionCandidate> candidates;
    std::optional<EnvironmentContinuationView> continuation;
};

}  // namespace ygo::environment
