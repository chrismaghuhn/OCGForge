#include "ygo/teacher/public_battle_snapshot.hpp"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "ygo/environment/episodic_environment.hpp"
#include "ygo/environment/public_action_identity.hpp"
#include "ygo/environment/public_environment_observation.hpp"
#include "ygo/observation/player_observation.hpp"

namespace {

using ygo::environment::EnvironmentActionCandidate;
using ygo::environment::EnvironmentActionKind;
using ygo::environment::PublicActionKeyInput;
using ygo::environment::PublicCardReference;
using ygo::environment::PublicCardReferenceKind;
using ygo::environment::PublicChoice;
using ygo::environment::PublicChoiceKind;
using ygo::environment::PublicEnvironmentObservation;
using ygo::observation::ObservedCard;
using ygo::observation::PlayerObservation;
using ygo::observation::Position;
using ygo::observation::SemanticZone;
using ygo::teacher::PublicBattleCandidateClass;
using ygo::teacher::PublicBattleCandidateStatus;
using ygo::teacher::PublicBattleCandidateFactsV1;
using ygo::teacher::PublicBattleSnapshotV1;

void require(const bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

ObservedCard known_card(const std::string& locator, const std::uint32_t passcode,
                        const std::uint8_t controller, const SemanticZone zone,
                        const Position position, const std::int32_t attack,
                        const std::int32_t defense) {
    ObservedCard card;
    card.locator = {locator};
    card.identity_known = true;
    card.passcode = passcode;
    card.owner = controller;
    card.controller = controller;
    card.zone = zone;
    card.sequence = 0;
    card.position = position;
    card.face_up = position == Position::FaceUpAttack ||
                   position == Position::FaceUpDefense;
    card.current.emplace();
    card.current->attack = attack;
    card.current->defense = defense;
    return card;
}

ObservedCard redacted_card(const std::string& locator, const std::uint8_t controller,
                           const SemanticZone zone, const Position position) {
    ObservedCard card;
    card.locator = {locator};
    card.identity_known = false;
    card.owner = controller;
    card.controller = controller;
    card.zone = zone;
    card.sequence = 1;
    card.position = position;
    card.face_down = position == Position::FaceDownAttack ||
                     position == Position::FaceDownDefense;
    return card;
}

PlayerObservation observation_with_standard_entities() {
    PlayerObservation observation;
    observation.schema_version = "ygo.player_observation.v1";
    observation.perspective_player = 0;
    observation.decision_index = 37;
    observation.globals.life_points = {8000, 3500};
    observation.globals.phase = 0x80;
    observation.globals.chain_length = 0;
    observation.globals.terminal = false;
    observation.match_context.perspective_player = 0;
    observation.match_context.duel_flags = observation.globals.duel_flags;
    observation.decision_context.kind = "battle_command";
    observation.decision_context.player = 0;
    observation.entities = {
        known_card("p0:MONSTER_ZONE:0", 11111111, 0, SemanticZone::MonsterZone,
                   Position::FaceUpAttack, 2500, 1800),
        known_card("p1:MONSTER_ZONE:0", 22222222, 1, SemanticZone::MonsterZone,
                   Position::FaceUpDefense, 2000, 2200),
        redacted_card("p1:MONSTER_ZONE:1", 0, SemanticZone::MonsterZone,
                      Position::FaceDownDefense),
        known_card("p0:MONSTER_ZONE:2", 33333333, 0, SemanticZone::MonsterZone,
                   Position::Unknown, 1500, 1200),
    };
    return observation;
}

PublicEnvironmentObservation public_observation(const PlayerObservation& value) {
    return ygo::environment::project_public_observation(value);
}

EnvironmentActionCandidate candidate(
    const EnvironmentActionKind action_kind,
    const std::optional<PublicCardReference>& source = std::nullopt,
    const std::optional<PublicCardReference>& target = std::nullopt,
    const std::optional<std::uint32_t>& phase = std::nullopt,
    const std::optional<std::uint32_t>& choice_index = std::nullopt) {
    EnvironmentActionCandidate result;
    result.action_kind = action_kind;
    result.source_reference = source;
    result.target_reference = target;
    result.phase = phase;
    if (choice_index.has_value()) {
        result.choice = PublicChoice{PublicChoiceKind::EffectChoice, *choice_index,
                                     std::nullopt};
    }

    PublicActionKeyInput key;
    key.action_kind =
        std::string(ygo::environment::environment_action_kind_name(action_kind));
    key.choice = result.choice;
    key.source_reference = result.source_reference;
    key.target_reference = result.target_reference;
    key.phase = result.phase;
    key.position = result.position;
    key.source_index = result.source_index;
    key.amount = result.amount;
    key.continuation_operation = result.continuation_operation;
    result.public_action_key = ygo::environment::public_action_key(key);
    return result;
}

PublicCardReference visible_reference(const std::string& locator) {
    return {PublicCardReferenceKind::VisibleCard, locator};
}

PublicCardReference redacted_reference(const std::string& locator) {
    return {PublicCardReferenceKind::RedactedSlot, locator};
}

bool has_reason(const PublicBattleCandidateFactsV1& value,
                const std::string_view reason) {
    return std::find(value.reason_ids.begin(), value.reason_ids.end(), reason) !=
           value.reason_ids.end();
}

bool throws_invalid(const PublicBattleSnapshotV1& value) {
    try {
        (void)ygo::teacher::canonical_public_battle_snapshot_bytes(value);
    } catch (const std::exception&) {
        return true;
    } catch (...) {
        return true;
    }
    return false;
}

void test_extracts_public_shape_and_preserves_domain() {
    const auto public_view = public_observation(observation_with_standard_entities());
    const auto candidates = std::vector<EnvironmentActionCandidate>{
        candidate(EnvironmentActionKind::IdleCommand),
        candidate(EnvironmentActionKind::BattleCommand,
                  visible_reference("p0:MONSTER_ZONE:0"),
                  visible_reference("p1:MONSTER_ZONE:0"), 1, 0),
        candidate(EnvironmentActionKind::BattleCommand,
                  redacted_reference("p1:MONSTER_ZONE:1"), std::nullopt, 1, 1),
        candidate(EnvironmentActionKind::BattleCommand, std::nullopt, std::nullopt, 2),
        candidate(EnvironmentActionKind::BattleCommand,
                  visible_reference("p0:MONSTER_ZONE:2"), std::nullopt, 1, 2),
    };

    const auto first =
        ygo::teacher::extract_public_battle_snapshot(public_view, candidates);
    const auto second =
        ygo::teacher::extract_public_battle_snapshot(public_view, candidates);
    require(first.valid, "valid public battle input was rejected");
    require(second.valid && first.snapshot == second.snapshot,
            "repeated public snapshot extraction was not deterministic");

    const auto& snapshot = first.snapshot;
    require(snapshot.schema_id ==
                ygo::teacher::kPublicBattleSnapshotSchemaId,
            "snapshot schema identity is wrong");
    require(snapshot.perspective_player == 0 && snapshot.decision_index == 37,
            "snapshot frame identity was not copied");
    require(snapshot.decision_context_kind == std::optional<std::string>("battle_command"),
            "snapshot decision context was not copied");
    require(snapshot.turn_phase == std::optional<std::uint32_t>(0x80),
            "snapshot phase was not copied");
    require(snapshot.self_life_points == std::optional<std::uint32_t>(8000) &&
                snapshot.opponent_life_points == std::optional<std::uint32_t>(3500),
            "snapshot life points were not mapped by perspective");
    require(snapshot.candidate_facts.size() == candidates.size(),
            "snapshot did not preserve the complete candidate domain");

    const auto& non_battle = snapshot.candidate_facts[0];
    require(non_battle.public_action_key == candidates[0].public_action_key &&
                non_battle.status == PublicBattleCandidateStatus::NotApplicable &&
                non_battle.battle_candidate_class ==
                    PublicBattleCandidateClass::NonBattleCandidate &&
                !non_battle.source_current_attack.has_value() &&
                !non_battle.source_current_defense.has_value() &&
                !non_battle.source_position.has_value() &&
                !non_battle.public_stat_margin.has_value() &&
                non_battle.reason_ids.empty(),
            "non-battle candidate was not represented as NOT_APPLICABLE");

    const auto& visible = snapshot.candidate_facts[1];
    require(visible.public_action_key == candidates[1].public_action_key &&
                visible.status == PublicBattleCandidateStatus::Unsupported &&
                visible.battle_candidate_class ==
                    PublicBattleCandidateClass::BattleCommandUnclassified &&
                visible.source_current_attack == std::optional<std::int32_t>(2500) &&
                visible.source_current_defense == std::optional<std::int32_t>(1800) &&
                visible.source_position == std::optional<Position>(Position::FaceUpAttack) &&
                visible.target_current_attack == std::optional<std::int32_t>(2000) &&
                visible.target_current_defense == std::optional<std::int32_t>(2200) &&
                visible.target_position == std::optional<Position>(Position::FaceUpDefense) &&
                !visible.public_stat_margin.has_value() &&
                has_reason(visible, "battle.command_subtype_unproven"),
            "visible BattleCommand facts were not copied with fail-closed status");

    const auto& redacted = snapshot.candidate_facts[2];
    require(redacted.status == PublicBattleCandidateStatus::Unsupported &&
                !redacted.source_current_attack.has_value() &&
                !redacted.source_current_defense.has_value() &&
                !redacted.source_position.has_value() &&
                has_reason(redacted, "battle.source_redacted"),
            "RedactedSlot received stat evidence");

    const auto& control = snapshot.candidate_facts[3];
    require(control.status == PublicBattleCandidateStatus::Unsupported &&
                control.battle_candidate_class ==
                    PublicBattleCandidateClass::BattleCommandUnclassified &&
                !control.source_current_attack.has_value() &&
                !control.target_current_attack.has_value(),
            "source-less BattleCommand control was misclassified");

    const auto& unknown_position = snapshot.candidate_facts[4];
    require(unknown_position.source_current_attack ==
                std::optional<std::int32_t>(1500) &&
                unknown_position.source_current_defense ==
                    std::optional<std::int32_t>(1200) &&
                !unknown_position.source_position.has_value() &&
                has_reason(unknown_position, "battle.source_position_unknown"),
            "Position::Unknown was encoded as a concrete position");

    require(ygo::teacher::validate_public_battle_snapshot(snapshot),
            "extracted snapshot failed canonical validation");
    const auto bytes = ygo::teacher::canonical_public_battle_snapshot_bytes(snapshot);
    require(!bytes.empty(), "canonical snapshot bytes are empty");
    require(bytes == ygo::teacher::canonical_public_battle_snapshot_bytes(snapshot),
            "canonical snapshot bytes changed on repeated encoding");
    const std::string byte_text(bytes.begin(), bytes.end());
    require(byte_text.find("p0:MONSTER_ZONE:0") == std::string::npos &&
                byte_text.find("11111111") == std::string::npos &&
                byte_text.find("22222222") == std::string::npos,
            "snapshot bytes contain a locator or passcode");
}

void test_rejects_malformed_inputs_without_domain_repair() {
    const auto public_view = public_observation(observation_with_standard_entities());
    const auto valid = candidate(EnvironmentActionKind::BattleCommand,
                                 visible_reference("p0:MONSTER_ZONE:0"),
                                 std::nullopt, 1, 0);

    require(!ygo::teacher::extract_public_battle_snapshot(public_view, {}).valid,
            "empty actionable candidate domain was accepted");

    auto invalid_key = valid;
    invalid_key.public_action_key = "not-a-public-action-key";
    require(!ygo::teacher::extract_public_battle_snapshot(public_view, {invalid_key}).valid,
            "invalid public action key was accepted");

    require(!ygo::teacher::extract_public_battle_snapshot(public_view, {valid, valid}).valid,
            "duplicate public action key was deduplicated");

    PublicEnvironmentObservation malformed;
    require(!ygo::teacher::extract_public_battle_snapshot(malformed, {valid}).valid,
            "malformed public observation was accepted");
    malformed.perspective_player = 2;
    require(!ygo::teacher::extract_public_battle_snapshot(malformed, {valid}).valid,
            "invalid observation perspective was accepted");

    auto unresolved = valid;
    unresolved.source_reference = visible_reference("p0:MONSTER_ZONE:99");
    const auto unresolved_result =
        ygo::teacher::extract_public_battle_snapshot(public_view, {unresolved});
    require(unresolved_result && unresolved_result.snapshot.candidate_facts.size() == 1 &&
                unresolved_result.snapshot.candidate_facts.front().status ==
                    PublicBattleCandidateStatus::Invalid,
            "missing visible locator did not remain as an INVALID N-record");

    auto contradictory_observation = observation_with_standard_entities();
    contradictory_observation.entities.front() =
        redacted_card("p0:MONSTER_ZONE:0", 0, SemanticZone::MonsterZone,
                      Position::FaceDownDefense);
    const auto contradictory_public = public_observation(contradictory_observation);
    const auto contradictory_result =
        ygo::teacher::extract_public_battle_snapshot(contradictory_public, {valid});
    require(contradictory_result &&
                contradictory_result.snapshot.candidate_facts.front().status ==
                    PublicBattleCandidateStatus::Invalid,
            "VisibleCard/public-identity contradiction was not INVALID");

    auto duplicate_entities = observation_with_standard_entities();
    duplicate_entities.entities.push_back(duplicate_entities.entities.front());
    bool duplicate_rejected = false;
    try {
        (void)public_observation(duplicate_entities);
    } catch (...) {
        duplicate_rejected = true;
    }
    require(duplicate_rejected,
            "public observation boundary allowed duplicate visible locators");
}

void test_unknown_zone_and_missing_controller_fail_closed() {
    auto unknown_zone_observation = observation_with_standard_entities();
    unknown_zone_observation.entities.front().zone = SemanticZone::Unknown;
    const auto unknown_zone_public = public_observation(unknown_zone_observation);
    const auto unknown_zone_candidate = candidate(
        EnvironmentActionKind::BattleCommand, visible_reference("p0:MONSTER_ZONE:0"),
        std::nullopt, 1, 0);
    const auto unknown_zone_result = ygo::teacher::extract_public_battle_snapshot(
        unknown_zone_public, {unknown_zone_candidate});
    require(unknown_zone_result &&
                has_reason(unknown_zone_result.snapshot.candidate_facts.front(),
                           "battle.source_zone_unknown"),
            "SemanticZone::Unknown was not fail-closed");

    auto no_controller_observation = observation_with_standard_entities();
    no_controller_observation.entities.front().controller.reset();
    const auto no_controller_public = public_observation(no_controller_observation);
    const auto no_controller_result = ygo::teacher::extract_public_battle_snapshot(
        no_controller_public, {unknown_zone_candidate});
    require(no_controller_result &&
                has_reason(no_controller_result.snapshot.candidate_facts.front(),
                           "battle.source_controller_unavailable"),
            "absent public controller was not fail-closed");
}

void test_snapshot_validator_rejects_noncanonical_values() {
    const auto public_view = public_observation(observation_with_standard_entities());
    const auto source = candidate(EnvironmentActionKind::BattleCommand,
                                  visible_reference("p0:MONSTER_ZONE:0"),
                                  std::nullopt, 1, 0);
    auto snapshot =
        ygo::teacher::extract_public_battle_snapshot(public_view, {source}).snapshot;
    require(ygo::teacher::validate_public_battle_snapshot(snapshot),
            "baseline snapshot was not valid");

    auto wrong_schema = snapshot;
    wrong_schema.schema_id = "ocgforge.other.v1";
    require(!ygo::teacher::validate_public_battle_snapshot(wrong_schema) &&
                throws_invalid(wrong_schema),
            "wrong snapshot schema was accepted");

    auto supported_battle = snapshot;
    supported_battle.candidate_facts.front().status =
        PublicBattleCandidateStatus::Supported;
    require(!ygo::teacher::validate_public_battle_snapshot(supported_battle),
            "BattleCommand + SUPPORTED was accepted in Task 2");

    auto invalid_enum = snapshot;
    invalid_enum.candidate_facts.front().battle_candidate_class =
        static_cast<PublicBattleCandidateClass>(9);
    require(!ygo::teacher::validate_public_battle_snapshot(invalid_enum),
            "unknown candidate class was accepted");

    auto unknown_position = snapshot;
    unknown_position.candidate_facts.front().source_position =
        static_cast<Position>(0);
    require(!ygo::teacher::validate_public_battle_snapshot(unknown_position),
            "Position::Unknown was stored as a concrete optional value");

    auto margin = snapshot;
    margin.candidate_facts.front().public_stat_margin = 1;
    require(!ygo::teacher::validate_public_battle_snapshot(margin),
            "speculative public stat margin was accepted");

    auto unsorted_reasons = snapshot;
    unsorted_reasons.candidate_facts.front().reason_ids = {"b", "a"};
    require(!ygo::teacher::validate_public_battle_snapshot(unsorted_reasons),
            "unsorted reasons were accepted");

    auto duplicate_reasons = snapshot;
    duplicate_reasons.candidate_facts.front().reason_ids = {"a", "a"};
    require(!ygo::teacher::validate_public_battle_snapshot(duplicate_reasons),
            "duplicate reasons were accepted");

    auto duplicate_keys = snapshot;
    duplicate_keys.candidate_facts.push_back(duplicate_keys.candidate_facts.front());
    require(!ygo::teacher::validate_public_battle_snapshot(duplicate_keys),
            "duplicate candidate keys were accepted by the snapshot validator");

    auto malformed_non_battle = snapshot;
    malformed_non_battle.candidate_facts.front().battle_candidate_class =
        PublicBattleCandidateClass::NonBattleCandidate;
    malformed_non_battle.candidate_facts.front().status =
        PublicBattleCandidateStatus::Unsupported;
    require(!ygo::teacher::validate_public_battle_snapshot(malformed_non_battle),
            "non-battle candidate with UNSUPPORTED status was accepted");
}

}  // namespace

int main() {
    try {
        test_extracts_public_shape_and_preserves_domain();
        test_rejects_malformed_inputs_without_domain_repair();
        test_unknown_zone_and_missing_controller_fail_closed();
        test_snapshot_validator_rejects_noncanonical_values();
        std::cout << "public_battle_snapshot_test: PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "public_battle_snapshot_test: FAIL: " << error.what() << '\n';
        return 1;
    }
}
