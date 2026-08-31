#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "ygo/environment/public_decision.hpp"
#include "ygo/environment/public_environment_observation.hpp"
#include "ygo/observation/observed_zone.hpp"

namespace ygo::teacher {

inline constexpr std::string_view kPublicBattleSnapshotSchemaId =
    "ocgforge.public_battle_snapshot.v1";

enum class PublicBattleCandidateStatus : std::uint8_t {
    NotApplicable = 0,
    Supported = 1,
    Unsupported = 2,
    Invalid = 3,
};

enum class PublicBattleCandidateClass : std::uint8_t {
    NonBattleCandidate = 0,
    BattleCommandUnclassified = 1,
};

struct PublicBattleCandidateFactsV1 final {
    std::string public_action_key;
    PublicBattleCandidateStatus status =
        PublicBattleCandidateStatus::Invalid;
    PublicBattleCandidateClass battle_candidate_class =
        PublicBattleCandidateClass::NonBattleCandidate;
    std::optional<std::int32_t> source_current_attack;
    std::optional<std::int32_t> source_current_defense;
    std::optional<ygo::observation::Position> source_position;
    std::optional<std::int32_t> target_current_attack;
    std::optional<std::int32_t> target_current_defense;
    std::optional<ygo::observation::Position> target_position;
    std::optional<std::int32_t> public_stat_margin;
    std::vector<std::string> reason_ids;

    bool operator==(const PublicBattleCandidateFactsV1& other) const noexcept {
        return public_action_key == other.public_action_key &&
               status == other.status &&
               battle_candidate_class == other.battle_candidate_class &&
               source_current_attack == other.source_current_attack &&
               source_current_defense == other.source_current_defense &&
               source_position == other.source_position &&
               target_current_attack == other.target_current_attack &&
               target_current_defense == other.target_current_defense &&
               target_position == other.target_position &&
               public_stat_margin == other.public_stat_margin &&
               reason_ids == other.reason_ids;
    }

    bool operator!=(const PublicBattleCandidateFactsV1& other) const noexcept {
        return !(*this == other);
    }
};

struct PublicBattleSnapshotV1 final {
    std::string schema_id = std::string(kPublicBattleSnapshotSchemaId);
    std::uint8_t perspective_player = 0;
    std::uint64_t decision_index = 0;
    std::optional<std::string> decision_context_kind;
    std::optional<std::uint32_t> turn_phase;
    std::optional<std::uint32_t> self_life_points;
    std::optional<std::uint32_t> opponent_life_points;
    std::vector<PublicBattleCandidateFactsV1> candidate_facts;

    bool operator==(const PublicBattleSnapshotV1& other) const noexcept {
        return schema_id == other.schema_id &&
               perspective_player == other.perspective_player &&
               decision_index == other.decision_index &&
               decision_context_kind == other.decision_context_kind &&
               turn_phase == other.turn_phase &&
               self_life_points == other.self_life_points &&
               opponent_life_points == other.opponent_life_points &&
               candidate_facts == other.candidate_facts;
    }

    bool operator!=(const PublicBattleSnapshotV1& other) const noexcept {
        return !(*this == other);
    }
};

struct PublicBattleSnapshotExtractionResult final {
    bool valid = false;
    PublicBattleSnapshotV1 snapshot;

    explicit operator bool() const noexcept { return valid; }
};

PublicBattleSnapshotExtractionResult extract_public_battle_snapshot(
    const environment::PublicEnvironmentObservation& observation,
    const std::vector<environment::EnvironmentActionCandidate>& candidates) noexcept;

bool validate_public_battle_snapshot(
    const PublicBattleSnapshotV1& value) noexcept;

std::vector<std::uint8_t> canonical_public_battle_snapshot_bytes(
    const PublicBattleSnapshotV1& value);

}  // namespace ygo::teacher
