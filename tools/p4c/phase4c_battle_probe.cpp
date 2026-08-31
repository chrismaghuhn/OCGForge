#include "ygo/teacher/public_battle_snapshot.hpp"
#include "ygo/teacher/provable_lethal.hpp"

#include <cstdint>
#include <iomanip>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "ocgapi_constants.h"
#include "episodic_environment_test_access.hpp"
#include "ygo/environment/episodic_environment.hpp"
#include "ygo/environment/public_action_identity.hpp"
#include "ygo/observation/decision_integration.hpp"
#include "ygo/observation/player_observation.hpp"
#include "ygo/protocol/message_decoder.hpp"

namespace {

using ygo::environment::CertifiedEnvironmentConfig;
using ygo::environment::EnvironmentActionCandidate;
using ygo::environment::EnvironmentActionKind;
using ygo::environment::EpisodicEnvironment;
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

constexpr std::uint32_t kFirstHiddenCode = 0xdeadbeef;
constexpr std::uint32_t kSecondHiddenCode = 0xcafebabe;

void require(const bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

ObservedCard known_card(const std::string& locator, const std::uint32_t passcode,
                        const std::uint8_t controller, const std::uint32_t sequence,
                        const Position position, const std::int32_t attack,
                        const std::int32_t defense) {
    ObservedCard card;
    card.locator = {locator};
    card.identity_known = true;
    card.passcode = passcode;
    card.owner = controller;
    card.controller = controller;
    card.zone = SemanticZone::MonsterZone;
    card.sequence = sequence;
    card.position = position;
    card.face_up = position == Position::FaceUpAttack ||
                   position == Position::FaceUpDefense;
    card.current.emplace();
    card.current->attack = attack;
    card.current->defense = defense;
    return card;
}

ObservedCard hidden_card() {
    ObservedCard card;
    card.locator = {"p1:MONSTER_ZONE:0"};
    card.identity_known = false;
    card.owner = 1;
    card.controller = 1;
    card.zone = SemanticZone::MonsterZone;
    card.sequence = 0;
    card.position = Position::FaceDownDefense;
    card.face_down = true;
    return card;
}

PlayerObservation snapshot_observation() {
    PlayerObservation observation;
    observation.schema_version = "ygo.player_observation.v1";
    observation.perspective_player = 0;
    observation.decision_index = 37;
    observation.globals.life_points = {8000, 3500};
    observation.globals.phase = 0x80;
    observation.match_context.perspective_player = 0;
    observation.decision_context.kind = "battle_command";
    observation.decision_context.player = 0;
    observation.entities = {
        known_card("p0:MONSTER_ZONE:0", 11111111, 0, 0,
                   Position::FaceUpAttack, 2500, 1800),
        hidden_card(),
        known_card("p0:MONSTER_ZONE:2", 33333333, 0, 2,
                   Position::Unknown, 1500, 1200),
    };
    return observation;
}

PublicCardReference visible_reference(const std::string& locator) {
    return {PublicCardReferenceKind::VisibleCard, locator};
}

PublicCardReference redacted_reference(const std::string& locator) {
    return {PublicCardReferenceKind::RedactedSlot, locator};
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

std::string hex_bytes(const std::vector<std::uint8_t>& bytes) {
    std::ostringstream output;
    output << std::hex << std::setfill('0');
    for (const auto byte : bytes) {
        output << std::setw(2) << static_cast<unsigned int>(byte);
    }
    return output.str();
}

std::string lethal_status_name(
    const ygo::teacher::ProvableLethalStatus status) {
    switch (status) {
    case ygo::teacher::ProvableLethalStatus::NotApplicable:
        return "NOT_APPLICABLE";
    case ygo::teacher::ProvableLethalStatus::ProvenLethal:
        return "PROVEN_LETHAL";
    case ygo::teacher::ProvableLethalStatus::NotProven:
        return "NOT_PROVEN";
    case ygo::teacher::ProvableLethalStatus::Unsupported:
        return "UNSUPPORTED";
    case ygo::teacher::ProvableLethalStatus::Invalid:
        return "INVALID";
    }
    return "UNKNOWN";
}

std::string join_reasons(const std::vector<std::string>& reasons) {
    std::ostringstream output;
    for (std::size_t index = 0; index < reasons.size(); ++index) {
        if (index != 0) {
            output << ',';
        }
        output << reasons[index];
    }
    return output.str();
}

void append_u32le(std::vector<std::uint8_t>& bytes, const std::uint32_t value) {
    bytes.push_back(static_cast<std::uint8_t>(value));
    bytes.push_back(static_cast<std::uint8_t>(value >> 8));
    bytes.push_back(static_cast<std::uint8_t>(value >> 16));
    bytes.push_back(static_cast<std::uint8_t>(value >> 24));
}

void append_u64le(std::vector<std::uint8_t>& bytes, const std::uint64_t value) {
    for (std::size_t shift = 0; shift < 64; shift += 8) {
        bytes.push_back(static_cast<std::uint8_t>(value >> shift));
    }
}

std::vector<std::uint8_t> hidden_battle_message(const std::uint32_t hidden_code) {
    std::vector<std::uint8_t> frame;
    frame.push_back(MSG_SELECT_BATTLECMD);
    frame.push_back(0);
    append_u32le(frame, 1);
    append_u32le(frame, hidden_code);
    frame.push_back(1);
    frame.push_back(LOCATION_MZONE);
    append_u32le(frame, 0);
    append_u64le(frame, 0);
    frame.push_back(0);
    append_u32le(frame, 0);
    frame.push_back(0);
    frame.push_back(0);
    std::vector<std::uint8_t> bytes;
    append_u32le(bytes, static_cast<std::uint32_t>(frame.size()));
    bytes.insert(bytes.end(), frame.begin(), frame.end());
    return bytes;
}

PlayerObservation hidden_world_observation() {
    PlayerObservation observation;
    observation.schema_version = "ygo.player_observation.v1";
    observation.perspective_player = 0;
    observation.decision_index = 11;
    observation.engine_step_index = 91;
    observation.globals.life_points = {8000, 8000};
    observation.globals.phase = 0x80;
    observation.match_context.perspective_player = 0;
    return observation;
}

std::unique_ptr<EpisodicEnvironment> make_environment() {
    auto factory =
        EpisodicEnvironment::create(CertifiedEnvironmentConfig::canonical());
    require(std::holds_alternative<std::unique_ptr<EpisodicEnvironment>>(factory),
            "probe could not create the certified environment");
    return std::move(std::get<std::unique_ptr<EpisodicEnvironment>>(factory));
}

void require_same_candidate(const EnvironmentActionCandidate& left,
                            const EnvironmentActionCandidate& right) {
    const auto same_choice = [](const auto& first, const auto& second) {
        if (first.has_value() != second.has_value()) {
            return false;
        }
        return !first.has_value() ||
               (first->kind == second->kind && first->value == second->value &&
                first->response_index == second->response_index);
    };
    const auto same_reference = [](const auto& first, const auto& second) {
        if (first.has_value() != second.has_value()) {
            return false;
        }
        return !first.has_value() ||
               (first->kind == second->kind &&
                first->observation_locator == second->observation_locator);
    };
    require(left.action_kind == right.action_kind &&
                left.public_action_key == right.public_action_key &&
                same_choice(left.choice, right.choice) &&
                same_reference(left.source_reference, right.source_reference) &&
                same_reference(left.target_reference, right.target_reference) &&
                left.phase == right.phase &&
                left.position == right.position &&
                left.source_index == right.source_index &&
                left.amount == right.amount &&
                left.continuation_operation == right.continuation_operation &&
                left.submits_engine_response == right.submits_engine_response,
            "paired public candidates differ");
}

int run_snapshot_corpus() {
    const auto observation =
        ygo::environment::project_public_observation(snapshot_observation());
    const std::vector<EnvironmentActionCandidate> candidates = {
        candidate(EnvironmentActionKind::IdleCommand),
        candidate(EnvironmentActionKind::BattleCommand,
                  visible_reference("p0:MONSTER_ZONE:0"), std::nullopt, 1, 0),
        candidate(EnvironmentActionKind::BattleCommand, std::nullopt,
                  std::nullopt, 2),
        candidate(EnvironmentActionKind::BattleCommand,
                  redacted_reference("p1:MONSTER_ZONE:0"), std::nullopt, 1, 1),
        candidate(EnvironmentActionKind::BattleCommand,
                  visible_reference("p0:MONSTER_ZONE:2"), std::nullopt, 1, 2),
    };
    const auto result =
        ygo::teacher::extract_public_battle_snapshot(observation, candidates);
    require(result && result.snapshot.candidate_facts.size() == candidates.size(),
            "snapshot corpus extraction failed");
    const auto bytes =
        ygo::teacher::canonical_public_battle_snapshot_bytes(result.snapshot);
    std::cout << "MODE=snapshot-corpus\n";
    std::cout << "CANDIDATE_COUNT=" << candidates.size() << '\n';
    std::cout << "SNAPSHOT_VALID=PASS\n";
    std::cout << "SNAPSHOT_BYTES_HEX=" << hex_bytes(bytes) << '\n';
    return 0;
}

int run_lethal_corpus() {
    auto source = snapshot_observation();
    source.globals.life_points = {8000, 1};
    source.entities.front().current->attack = 999999;
    const auto observation =
        ygo::environment::project_public_observation(source);
    const std::vector<EnvironmentActionCandidate> candidates = {
        candidate(EnvironmentActionKind::IdleCommand),
        candidate(EnvironmentActionKind::BattleCommand,
                  visible_reference("p0:MONSTER_ZONE:0"), std::nullopt, 1, 0),
        candidate(EnvironmentActionKind::BattleCommand, std::nullopt,
                  std::nullopt, 2),
        candidate(EnvironmentActionKind::BattleCommand,
                  redacted_reference("p1:MONSTER_ZONE:0"), std::nullopt, 1, 1),
        candidate(EnvironmentActionKind::BattleCommand,
                  visible_reference("p0:MONSTER_ZONE:2"), std::nullopt, 1, 2),
    };
    const auto snapshot =
        ygo::teacher::extract_public_battle_snapshot(observation, candidates);
    require(snapshot.valid, "lethal corpus snapshot extraction failed");
    const auto result =
        ygo::teacher::evaluate_provable_lethal(snapshot.snapshot);
    require(result.valid, "lethal corpus evaluation failed");
    require(result.candidates.size() == candidates.size(),
            "lethal corpus changed the complete candidate count");

    std::cout << "MODE=lethal-corpus\n";
    std::cout << "CANDIDATE_COUNT=" << result.candidates.size() << '\n';
    std::cout << "EVALUATION_VALID=PASS\n";
    for (std::size_t index = 0; index < result.candidates.size(); ++index) {
        const auto& value = result.candidates[index];
        const auto bytes =
            ygo::teacher::canonical_provable_lethal_candidate_bytes(value);
        std::cout << "INDEX=" << index << '\n';
        std::cout << "PUBLIC_ACTION_KEY=" << value.public_action_key << '\n';
        std::cout << "LETHAL_STATUS=" << lethal_status_name(value.status)
                  << '\n';
        std::cout << "BOUND_PRESENT="
                  << (value.guaranteed_opponent_lp_loss_lower_bound.has_value()
                          ? "1"
                          : "0")
                  << '\n';
        if (value.guaranteed_opponent_lp_loss_lower_bound.has_value()) {
            std::cout << "BOUND_VALUE="
                      << *value.guaranteed_opponent_lp_loss_lower_bound << '\n';
        } else {
            std::cout << "BOUND_VALUE=ABSENT\n";
        }
        std::cout << "PROOF_REASONS="
                  << join_reasons(value.proof_reason_ids) << '\n';
        std::cout << "LETHAL_BYTES_HEX=" << hex_bytes(bytes) << '\n';
    }
    return 0;
}

int run_paired_world() {
    auto environment = make_environment();
    auto first_observation = hidden_world_observation();
    auto second_observation = hidden_world_observation();
    const auto first_decoded =
        ygo::protocol::decode_messages(hidden_battle_message(kFirstHiddenCode), 91);
    const auto second_decoded =
        ygo::protocol::decode_messages(hidden_battle_message(kSecondHiddenCode), 91);
    require(first_decoded.decisions.size() == 1 &&
                second_decoded.decisions.size() == 1,
            "paired battle messages did not decode");
    const auto& first_request = first_decoded.decisions.front();
    const auto& second_request = second_decoded.decisions.front();
    first_observation.entities = {hidden_card()};
    second_observation.entities = {hidden_card()};
    ygo::observation::attach_decision_context(first_observation, first_request);
    ygo::observation::attach_decision_context(second_observation, second_request);

    const auto first_frame =
        ygo::environment::detail::EpisodicEnvironmentTestAccess::project_frame_for_test(
            *environment, first_request, first_observation, std::string(64, 'a'), 11);
    const auto second_frame =
        ygo::environment::detail::EpisodicEnvironmentTestAccess::project_frame_for_test(
            *environment, second_request, second_observation, std::string(64, 'a'), 11);

    require(ygo::environment::canonical_public_environment_observation_bytes(
                first_frame.public_observation) ==
                ygo::environment::canonical_public_environment_observation_bytes(
                    second_frame.public_observation),
            "private paired worlds changed public observation");
    require(first_frame.request.candidates.size() ==
                second_frame.request.candidates.size(),
            "private paired worlds changed public candidate count");
    for (std::size_t index = 0; index < first_frame.request.candidates.size();
         ++index) {
        require_same_candidate(first_frame.request.candidates[index],
                               second_frame.request.candidates[index]);
    }

    const auto first_snapshot =
        ygo::teacher::extract_public_battle_snapshot(
            first_frame.public_observation, first_frame.request.candidates);
    const auto second_snapshot =
        ygo::teacher::extract_public_battle_snapshot(
            second_frame.public_observation, second_frame.request.candidates);
    require(first_snapshot && second_snapshot,
            "paired public snapshots were not extractable");
    const auto first_bytes =
        ygo::teacher::canonical_public_battle_snapshot_bytes(
            first_snapshot.snapshot);
    const auto second_bytes =
        ygo::teacher::canonical_public_battle_snapshot_bytes(
            second_snapshot.snapshot);
    require(first_bytes == second_bytes,
            "private paired worlds changed canonical battle snapshot");

    const auto first_lethal =
        ygo::teacher::evaluate_provable_lethal(first_snapshot.snapshot);
    const auto second_lethal =
        ygo::teacher::evaluate_provable_lethal(second_snapshot.snapshot);
    require(first_lethal && second_lethal &&
                first_lethal.candidates == second_lethal.candidates,
            "private paired worlds changed lethal evaluation");
    require(first_lethal.candidates.size() == second_lethal.candidates.size(),
            "paired lethal evaluation changed candidate count");
    for (std::size_t index = 0; index < first_lethal.candidates.size();
         ++index) {
        require(ygo::teacher::canonical_provable_lethal_candidate_bytes(
                    first_lethal.candidates[index]) ==
                    ygo::teacher::canonical_provable_lethal_candidate_bytes(
                        second_lethal.candidates[index]),
                "paired lethal candidate bytes differ");
    }

    std::ostringstream output;
    output << "MODE=paired-world\n";
    output << "PUBLIC_OBSERVATION_EQUAL=PASS\n";
    output << "PUBLIC_CANDIDATES_EQUAL=PASS\n";
    output << "SNAPSHOT_EQUAL=PASS\n";
    output << "LETHAL_EQUAL=PASS\n";
    output << "SNAPSHOT_BYTES_HEX=" << hex_bytes(first_bytes) << '\n';
    output << "HIDDEN_VALUES_IN_OUTPUT=NONE\n";
    const auto text = output.str();
    require(text.find(std::to_string(kFirstHiddenCode)) == std::string::npos &&
                text.find(std::to_string(kSecondHiddenCode)) == std::string::npos,
            "paired probe output contains a hidden code");
    std::cout << text;
    return 0;
}

}  // namespace

int main(const int argc, char** argv) {
    try {
        if (argc != 2) {
            throw std::runtime_error("expected one probe mode");
        }
        const std::string_view mode(argv[1]);
        if (mode == "--snapshot-corpus") {
            return run_snapshot_corpus();
        }
        if (mode == "--lethal-corpus") {
            return run_lethal_corpus();
        }
        if (mode == "--paired-world") {
            return run_paired_world();
        }
        throw std::runtime_error("unknown probe mode");
    } catch (const std::exception& error) {
        std::cerr << "phase4c_battle_probe: " << error.what() << '\n';
        return 1;
    }
}
