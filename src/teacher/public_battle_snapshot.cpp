#include "ygo/teacher/public_battle_snapshot.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include "ygo/environment/public_action_identity.hpp"
#include "ygo/environment/public_safe_state.hpp"
#include "ygo/trajectory/codec.hpp"
#include "teacher_validation.hpp"

namespace ygo::teacher {
namespace {

using environment::EnvironmentActionCandidate;
using environment::EnvironmentActionKind;
using environment::PublicCardReference;
using environment::PublicCardReferenceKind;
using environment::PublicEnvironmentObservation;
using environment::PublicSafeStateView;
using observation::ObservedCard;
using observation::Position;
using observation::SemanticZone;

constexpr std::string_view kSubtypeUnprovenReason =
    "battle.command_subtype_unproven";
constexpr std::string_view kCandidateMetadataInvalidReason =
    "battle.candidate_metadata_invalid";

bool valid_public_context_token(const std::string_view value) noexcept {
    if (value.empty()) {
        return false;
    }
    return std::all_of(value.begin(), value.end(), [](const unsigned char character) {
        return (character >= 'a' && character <= 'z') ||
               (character >= '0' && character <= '9') || character == '_';
    });
}

bool valid_action_kind(const EnvironmentActionKind value) noexcept {
    const auto encoded = static_cast<std::uint8_t>(value);
    return encoded <= static_cast<std::uint8_t>(EnvironmentActionKind::Unsupported) &&
           value != EnvironmentActionKind::Unsupported;
}

bool valid_reference_kind(const PublicCardReferenceKind value) noexcept {
    return value == PublicCardReferenceKind::VisibleCard ||
           value == PublicCardReferenceKind::RedactedSlot;
}

bool valid_locator(const std::string_view value) noexcept {
    return !value.empty() &&
           std::all_of(value.begin(), value.end(), [](const unsigned char character) {
               return character >= 0x20 && character != 0x7f;
           });
}

bool valid_position(const Position value) noexcept {
    switch (value) {
    case Position::FaceUpAttack:
    case Position::FaceDownAttack:
    case Position::FaceUpDefense:
    case Position::FaceDownDefense:
        return true;
    case Position::Unknown:
        return false;
    }
    return false;
}

bool valid_status(const PublicBattleCandidateStatus value) noexcept {
    return static_cast<std::uint8_t>(value) <=
           static_cast<std::uint8_t>(PublicBattleCandidateStatus::Invalid);
}

bool valid_candidate_class(const PublicBattleCandidateClass value) noexcept {
    return static_cast<std::uint8_t>(value) <=
           static_cast<std::uint8_t>(
               PublicBattleCandidateClass::BattleCommandUnclassified);
}

bool valid_reason_vector(const std::vector<std::string>& values) noexcept {
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (!detail::canonical_token(values[index]) ||
            (index > 0 && !(values[index - 1] < values[index]))) {
            return false;
        }
    }
    return true;
}

bool valid_optional_position(
    const std::optional<Position>& value) noexcept {
    return !value.has_value() || valid_position(*value);
}

bool no_battle_values(const PublicBattleCandidateFactsV1& value) noexcept {
    return !value.source_current_attack.has_value() &&
           !value.source_current_defense.has_value() &&
           !value.source_position.has_value() &&
           !value.target_current_attack.has_value() &&
           !value.target_current_defense.has_value() &&
           !value.target_position.has_value() &&
           !value.public_stat_margin.has_value() &&
           value.reason_ids.empty();
}

bool valid_candidate_fact(const PublicBattleCandidateFactsV1& value) noexcept {
    if (!environment::is_public_action_key(value.public_action_key) ||
        !valid_status(value.status) ||
        !valid_candidate_class(value.battle_candidate_class) ||
        !valid_optional_position(value.source_position) ||
        !valid_optional_position(value.target_position) ||
        value.public_stat_margin.has_value() ||
        !valid_reason_vector(value.reason_ids)) {
        return false;
    }

    if (value.battle_candidate_class ==
        PublicBattleCandidateClass::NonBattleCandidate) {
        return value.status == PublicBattleCandidateStatus::NotApplicable &&
               no_battle_values(value);
    }

    return value.status != PublicBattleCandidateStatus::NotApplicable &&
           value.status != PublicBattleCandidateStatus::Supported;
}

bool valid_snapshot(const PublicBattleSnapshotV1& value) noexcept {
    if (value.schema_id != kPublicBattleSnapshotSchemaId ||
        value.perspective_player > 1 ||
        (value.decision_context_kind.has_value() &&
         !valid_public_context_token(*value.decision_context_kind)) ||
        value.candidate_facts.empty() ||
        value.candidate_facts.size() >
            std::numeric_limits<std::uint32_t>::max()) {
        return false;
    }

    for (std::size_t index = 0; index < value.candidate_facts.size(); ++index) {
        const auto& candidate = value.candidate_facts[index];
        if (!valid_candidate_fact(candidate) ||
            candidate.reason_ids.size() >
                std::numeric_limits<std::uint32_t>::max()) {
            return false;
        }
        for (std::size_t previous = 0; previous < index; ++previous) {
            if (candidate.public_action_key ==
                value.candidate_facts[previous].public_action_key) {
                return false;
            }
        }
    }
    return true;
}

void add_reason(PublicBattleCandidateFactsV1& facts,
                const std::string_view reason) {
    facts.reason_ids.emplace_back(reason);
}

const ObservedCard* resolve_unique_entity(const PublicSafeStateView& view,
                                          const std::string_view locator,
                                          bool& duplicate) noexcept {
    const ObservedCard* result = nullptr;
    duplicate = false;
    for (const auto& entity : view.entities()) {
        if (entity.locator.value != locator) {
            continue;
        }
        if (result != nullptr) {
            duplicate = true;
            return nullptr;
        }
        result = &entity;
    }
    return result;
}

void append_reference_reason(PublicBattleCandidateFactsV1& facts,
                             const bool source,
                             const std::string_view suffix) {
    const std::string prefix = source ? "battle.source_" : "battle.target_";
    add_reason(facts, prefix + std::string(suffix));
}

void inspect_reference(const std::optional<PublicCardReference>& reference,
                       const PublicSafeStateView& view,
                       const bool source,
                       PublicBattleCandidateFactsV1& facts,
                       bool& invalid,
                       bool& unsupported) {
    if (!reference.has_value()) {
        return;
    }

    if (!valid_reference_kind(reference->kind) ||
        !valid_locator(reference->observation_locator)) {
        invalid = true;
        add_reason(facts, kCandidateMetadataInvalidReason);
        return;
    }

    if (reference->kind == PublicCardReferenceKind::RedactedSlot) {
        unsupported = true;
        append_reference_reason(facts, source, "redacted");
        return;
    }

    bool duplicate = false;
    const auto* entity =
        resolve_unique_entity(view, reference->observation_locator, duplicate);
    if (entity == nullptr) {
        invalid = true;
        append_reference_reason(facts, source,
                                duplicate ? "locator_duplicate"
                                          : "locator_unresolved");
        return;
    }

    if (!entity->identity_known || !entity->passcode.has_value()) {
        invalid = true;
        append_reference_reason(facts, source, "identity_inconsistent");
        return;
    }

    if (entity->zone == SemanticZone::Unknown) {
        unsupported = true;
        append_reference_reason(facts, source, "zone_unknown");
    }
    if (!entity->controller.has_value()) {
        unsupported = true;
        append_reference_reason(facts, source, "controller_unavailable");
    }

    const auto& current = entity->current;
    if (!current.has_value() || !current->attack.has_value()) {
        unsupported = true;
        append_reference_reason(facts, source, "attack_unavailable");
    } else if (source) {
        facts.source_current_attack = current->attack;
    } else {
        facts.target_current_attack = current->attack;
    }

    if (!current.has_value() || !current->defense.has_value()) {
        unsupported = true;
        append_reference_reason(facts, source, "defense_unavailable");
    } else if (source) {
        facts.source_current_defense = current->defense;
    } else {
        facts.target_current_defense = current->defense;
    }

    if (entity->position == Position::Unknown) {
        unsupported = true;
        append_reference_reason(facts, source, "position_unknown");
    } else {
        if (source) {
            facts.source_position = entity->position;
        } else {
            facts.target_position = entity->position;
        }
    }
}

PublicBattleCandidateFactsV1 extract_candidate_fact(
    const EnvironmentActionCandidate& candidate,
    const PublicSafeStateView& view) {
    PublicBattleCandidateFactsV1 facts;
    facts.public_action_key = candidate.public_action_key;

    if (candidate.action_kind != EnvironmentActionKind::BattleCommand) {
        facts.status = PublicBattleCandidateStatus::NotApplicable;
        facts.battle_candidate_class =
            PublicBattleCandidateClass::NonBattleCandidate;
        if ((candidate.source_reference.has_value() &&
             (!valid_reference_kind(candidate.source_reference->kind) ||
              !valid_locator(
                  candidate.source_reference->observation_locator))) ||
            (candidate.target_reference.has_value() &&
             (!valid_reference_kind(candidate.target_reference->kind) ||
              !valid_locator(
                  candidate.target_reference->observation_locator)))) {
            facts.status = PublicBattleCandidateStatus::Invalid;
            add_reason(facts, kCandidateMetadataInvalidReason);
        }
        return facts;
    }

    facts.status = PublicBattleCandidateStatus::Unsupported;
    facts.battle_candidate_class =
        PublicBattleCandidateClass::BattleCommandUnclassified;
    add_reason(facts, kSubtypeUnprovenReason);

    bool invalid = false;
    bool unsupported = false;
    inspect_reference(candidate.source_reference, view, true, facts, invalid,
                      unsupported);
    inspect_reference(candidate.target_reference, view, false, facts, invalid,
                      unsupported);

    facts.status = invalid ? PublicBattleCandidateStatus::Invalid
                           : PublicBattleCandidateStatus::Unsupported;
    std::sort(facts.reason_ids.begin(), facts.reason_ids.end());
    facts.reason_ids.erase(
        std::unique(facts.reason_ids.begin(), facts.reason_ids.end()),
        facts.reason_ids.end());
    return facts;
}

void append_optional_string(trajectory::ByteWriter& writer,
                            const std::optional<std::string>& value) {
    writer.boolean(value.has_value());
    if (value.has_value()) {
        writer.string(*value);
    }
}

void append_optional_u32(trajectory::ByteWriter& writer,
                         const std::optional<std::uint32_t>& value) {
    writer.boolean(value.has_value());
    if (value.has_value()) {
        writer.u32be(*value);
    }
}

void append_optional_i32(trajectory::ByteWriter& writer,
                         const std::optional<std::int32_t>& value) {
    writer.boolean(value.has_value());
    if (value.has_value()) {
        writer.i32(*value);
    }
}

void append_optional_position(trajectory::ByteWriter& writer,
                             const std::optional<Position>& value) {
    writer.boolean(value.has_value());
    if (value.has_value()) {
        writer.u8(static_cast<std::uint8_t>(*value));
    }
}

void append_reason_vector(trajectory::ByteWriter& writer,
                          const std::vector<std::string>& values) {
    writer.u32be(static_cast<std::uint32_t>(values.size()));
    for (const auto& value : values) {
        writer.string(value);
    }
}

}  // namespace

PublicBattleSnapshotExtractionResult extract_public_battle_snapshot(
    const PublicEnvironmentObservation& observation,
    const std::vector<EnvironmentActionCandidate>& candidates) noexcept {
    PublicBattleSnapshotExtractionResult result;
    try {
        if (observation.perspective_player > 1 || candidates.empty()) {
            return result;
        }

        const auto decoded = environment::decode_canonical_public_safe_state(
            observation.canonical_safe_state_bytes());
        if (!decoded ||
            decoded.value->match_context().perspective_player !=
                observation.perspective_player) {
            return result;
        }

        if (observation.decision_context.kind.has_value() &&
            !valid_public_context_token(*observation.decision_context.kind)) {
            return result;
        }

        for (std::size_t index = 0; index < candidates.size(); ++index) {
            const auto& candidate = candidates[index];
            if (!valid_action_kind(candidate.action_kind) ||
                !environment::is_public_action_key(
                    candidate.public_action_key)) {
                return result;
            }
            for (std::size_t previous = 0; previous < index; ++previous) {
                if (candidate.public_action_key ==
                    candidates[previous].public_action_key) {
                    return result;
                }
            }
        }

        result.snapshot.perspective_player = observation.perspective_player;
        result.snapshot.decision_index = observation.decision_index;
        result.snapshot.decision_context_kind =
            observation.decision_context.kind;
        result.snapshot.turn_phase = decoded.value->globals().phase;

        const auto& life_points = decoded.value->globals().life_points;
        const auto perspective = observation.perspective_player;
        const auto opponent = static_cast<std::uint8_t>(1 - perspective);
        if (perspective < life_points.size()) {
            result.snapshot.self_life_points = life_points[perspective];
        }
        if (opponent < life_points.size()) {
            result.snapshot.opponent_life_points = life_points[opponent];
        }

        result.snapshot.candidate_facts.reserve(candidates.size());
        for (const auto& candidate : candidates) {
            result.snapshot.candidate_facts.push_back(
                extract_candidate_fact(candidate, *decoded.value));
        }

        if (!valid_snapshot(result.snapshot)) {
            return PublicBattleSnapshotExtractionResult{};
        }
        result.valid = true;
        return result;
    } catch (...) {
        return PublicBattleSnapshotExtractionResult{};
    }
}

bool validate_public_battle_snapshot(
    const PublicBattleSnapshotV1& value) noexcept {
    try {
        return valid_snapshot(value);
    } catch (...) {
        return false;
    }
}

std::vector<std::uint8_t> canonical_public_battle_snapshot_bytes(
    const PublicBattleSnapshotV1& value) {
    if (!validate_public_battle_snapshot(value)) {
        throw std::invalid_argument("public battle snapshot is not canonical");
    }

    trajectory::ByteWriter writer;
    writer.string(kPublicBattleSnapshotSchemaId);
    writer.string(kPublicBattleSnapshotSchemaId);
    writer.u8(value.perspective_player);
    writer.u64be(value.decision_index);
    append_optional_string(writer, value.decision_context_kind);
    append_optional_u32(writer, value.turn_phase);
    append_optional_u32(writer, value.self_life_points);
    append_optional_u32(writer, value.opponent_life_points);
    writer.u32be(static_cast<std::uint32_t>(value.candidate_facts.size()));

    for (const auto& candidate : value.candidate_facts) {
        writer.string(candidate.public_action_key);
        writer.u8(static_cast<std::uint8_t>(candidate.status));
        writer.u8(static_cast<std::uint8_t>(
            candidate.battle_candidate_class));
        append_optional_i32(writer, candidate.source_current_attack);
        append_optional_i32(writer, candidate.source_current_defense);
        append_optional_position(writer, candidate.source_position);
        append_optional_i32(writer, candidate.target_current_attack);
        append_optional_i32(writer, candidate.target_current_defense);
        append_optional_position(writer, candidate.target_position);
        append_optional_i32(writer, candidate.public_stat_margin);
        append_reason_vector(writer, candidate.reason_ids);
    }
    return std::move(writer).take();
}

}  // namespace ygo::teacher
