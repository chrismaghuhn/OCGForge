#include "ygo/teacher/public_battle_snapshot.hpp"

#include <cstdint>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "ocgapi_constants.h"
#include "episodic_environment_test_access.hpp"
#include "ygo/environment/episodic_environment.hpp"
#include "ygo/observation/decision_integration.hpp"
#include "ygo/observation/player_observation.hpp"
#include "ygo/protocol/message_decoder.hpp"

namespace {

using ygo::environment::CertifiedEnvironmentConfig;
using ygo::environment::DecisionFrame;
using ygo::environment::EnvironmentActionKind;
using ygo::environment::EpisodicEnvironment;
using ygo::observation::ObservedCard;
using ygo::observation::PlayerObservation;
using ygo::observation::Position;
using ygo::observation::SemanticZone;

void require(const bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void append_u8(std::vector<std::uint8_t>& bytes, const std::uint8_t value) {
    bytes.push_back(value);
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

std::vector<std::uint8_t> battle_message() {
    std::vector<std::uint8_t> frame;
    append_u8(frame, MSG_SELECT_BATTLECMD);
    append_u8(frame, 0);

    append_u32le(frame, 1);
    append_u32le(frame, 4444444);
    append_u8(frame, 0);
    append_u8(frame, LOCATION_MZONE);
    append_u32le(frame, 0);
    append_u64le(frame, 0);
    append_u8(frame, 0);

    append_u32le(frame, 1);
    append_u32le(frame, 5555555);
    append_u8(frame, 0);
    append_u8(frame, LOCATION_MZONE);
    append_u8(frame, 1);
    append_u8(frame, 0);

    append_u8(frame, 1);
    append_u8(frame, 1);
    std::vector<std::uint8_t> bytes;
    append_u32le(bytes, static_cast<std::uint32_t>(frame.size()));
    bytes.insert(bytes.end(), frame.begin(), frame.end());
    return bytes;
}

ObservedCard known_card(const std::string& locator, const std::uint32_t passcode,
                        const std::uint32_t sequence, const std::int32_t attack) {
    ObservedCard card;
    card.locator = {locator};
    card.identity_known = true;
    card.passcode = passcode;
    card.owner = 0;
    card.controller = 0;
    card.zone = SemanticZone::MonsterZone;
    card.sequence = sequence;
    card.position = Position::FaceUpAttack;
    card.face_up = true;
    card.current.emplace();
    card.current->attack = attack;
    card.current->defense = 1000;
    return card;
}

PlayerObservation battle_observation() {
    PlayerObservation observation;
    observation.schema_version = "ygo.player_observation.v1";
    observation.perspective_player = 0;
    observation.decision_index = 12;
    observation.engine_step_index = 91;
    observation.globals.life_points = {8000, 8000};
    observation.globals.phase = 0x80;
    observation.match_context.perspective_player = 0;
    observation.entities = {
        known_card("p0:MONSTER_ZONE:0", 4444444, 0, 2500),
        known_card("p0:MONSTER_ZONE:1", 5555555, 1, 1800),
    };
    return observation;
}

void test_live_battle_decoder_shape() {
    const auto decoded = ygo::protocol::decode_messages(battle_message(), 91);
    require(decoded.decisions.size() == 1,
            "MSG_SELECT_BATTLECMD did not produce one decision request");
    const auto& request = decoded.decisions.front();
    require(request.kind == ygo::protocol::DecisionRequestKind::BattleCommand &&
                request.player == 0 && request.candidates.size() == 4,
            "battle decoder did not produce the expected four candidates");

    auto observation = battle_observation();
    ygo::observation::attach_decision_context(observation, request);
    auto environment_factory =
        EpisodicEnvironment::create(CertifiedEnvironmentConfig::canonical());
    require(std::holds_alternative<std::unique_ptr<EpisodicEnvironment>>(
                environment_factory),
            "could not create certified environment for shape projection");
    auto environment =
        std::move(std::get<std::unique_ptr<EpisodicEnvironment>>(environment_factory));
    const DecisionFrame frame =
        ygo::environment::detail::EpisodicEnvironmentTestAccess::project_frame_for_test(
            *environment, request, observation, std::string(64, 'a'), 12);

    require(frame.request.candidates.size() == 4,
            "public projection changed the BattleCommand candidate count");
    const auto& first = frame.request.candidates[0];
    const auto& second = frame.request.candidates[1];
    const auto& third = frame.request.candidates[2];
    const auto& fourth = frame.request.candidates[3];
    for (const auto& candidate : frame.request.candidates) {
        require(candidate.action_kind == EnvironmentActionKind::BattleCommand,
                "projected battle candidate changed action family");
        require(!candidate.source_index.has_value(),
                "BattleCommand published source_index as a classifier");
        require(!candidate.target_reference.has_value(),
                "fixture unexpectedly published a BattleCommand target reference");
    }

    require(first.phase == std::optional<std::uint32_t>(0) &&
                second.phase == std::optional<std::uint32_t>(1) &&
                third.phase == std::optional<std::uint32_t>(2) &&
                fourth.phase == std::optional<std::uint32_t>(3),
            "BattleCommand list markers were not projected as 0,1,2,3");
    require(first.source_reference.has_value() &&
                first.source_reference->kind ==
                    ygo::environment::PublicCardReferenceKind::VisibleCard &&
                second.source_reference.has_value() &&
                second.source_reference->kind ==
                    ygo::environment::PublicCardReferenceKind::VisibleCard &&
                !third.source_reference.has_value() && !fourth.source_reference.has_value(),
            "BattleCommand source-bearing/control shapes were not preserved");
    require(first.choice.has_value() &&
                first.choice->kind == ygo::environment::PublicChoiceKind::EffectChoice &&
                first.choice->value == 0 &&
                second.choice.has_value() &&
                second.choice->kind == ygo::environment::PublicChoiceKind::EffectChoice &&
                second.choice->value == 0 &&
                !third.choice.has_value() && !fourth.choice.has_value(),
            "BattleCommand choice metadata was not projected exactly");

    const auto snapshot =
        ygo::teacher::extract_public_battle_snapshot(frame.public_observation,
                                                     frame.request.candidates);
    require(snapshot.valid,
            "live BattleCommand public shape was rejected by snapshot extractor");
    for (const auto& facts : snapshot.snapshot.candidate_facts) {
        require(facts.battle_candidate_class ==
                    ygo::teacher::PublicBattleCandidateClass::BattleCommandUnclassified &&
                    facts.status ==
                        ygo::teacher::PublicBattleCandidateStatus::Unsupported,
                "Task 2 made an unsupported BattleCommand subtype claim");
    }
}

}  // namespace

int main() {
    try {
        test_live_battle_decoder_shape();
        std::cout << "battle_command_shape_test: PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "battle_command_shape_test: FAIL: " << error.what() << '\n';
        return 1;
    }
}
