#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "ygo/teacher/public_battle_snapshot.hpp"

namespace ygo::teacher {

inline constexpr std::string_view kProvableLethalSchemaId =
    "ocgforge.provable_lethal.v1";

enum class ProvableLethalStatus : std::uint8_t {
    NotApplicable = 0,
    ProvenLethal = 1,
    NotProven = 2,
    Unsupported = 3,
    Invalid = 4,
};

struct ProvableLethalCandidateV1 final {
    std::string schema_id = std::string(kProvableLethalSchemaId);
    std::string public_action_key;
    ProvableLethalStatus status = ProvableLethalStatus::Invalid;
    std::optional<std::uint64_t>
        guaranteed_opponent_lp_loss_lower_bound;
    std::vector<std::string> proof_reason_ids;

    bool operator==(const ProvableLethalCandidateV1& other) const noexcept {
        return schema_id == other.schema_id &&
               public_action_key == other.public_action_key &&
               status == other.status &&
               guaranteed_opponent_lp_loss_lower_bound ==
                   other.guaranteed_opponent_lp_loss_lower_bound &&
               proof_reason_ids == other.proof_reason_ids;
    }

    bool operator!=(const ProvableLethalCandidateV1& other) const noexcept {
        return !(*this == other);
    }
};

struct ProvableLethalEvaluationResult final {
    bool valid = false;
    std::vector<ProvableLethalCandidateV1> candidates;

    explicit operator bool() const noexcept { return valid; }
};

ProvableLethalEvaluationResult evaluate_provable_lethal(
    const PublicBattleSnapshotV1& snapshot) noexcept;

std::vector<std::uint8_t> canonical_provable_lethal_candidate_bytes(
    const ProvableLethalCandidateV1& value);

}  // namespace ygo::teacher
