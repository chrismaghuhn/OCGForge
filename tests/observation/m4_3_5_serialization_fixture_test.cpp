#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "ygo/observation/player_observation.hpp"
#include "ygo/observation/serialization.hpp"
#include "ygo/trace/sha256.hpp"

namespace {

using ygo::observation::CardProperties;
using ygo::observation::ChainLink;
using ygo::observation::LinkMarker;
using ygo::observation::ObservationLocator;
using ygo::observation::ObservedCard;
using ygo::observation::PlayerObservation;
using ygo::observation::Relationship;
using ygo::observation::RelationshipKind;
using ygo::observation::SemanticZone;

void require(const bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

CardProperties printed_properties() {
    CardProperties properties;
    properties.type = 0x1234;
    properties.attribute = 0x20;
    properties.race = 0x100000001ULL;
    properties.attack = 2500;
    properties.defense = 2100;
    properties.base_attack = 2000;
    properties.base_defense = 1800;
    properties.level = 8;
    properties.rank = 4;
    properties.link_rating = 3;
    properties.link_markers = {LinkMarker::TopRight, LinkMarker::BottomLeft, LinkMarker::Left};
    properties.left_scale = 1;
    properties.right_scale = 8;
    properties.status_flags = 0x55AA;
    properties.counters = {{9, 2}, {1, 7}};
    return properties;
}

CardProperties current_properties() {
    CardProperties properties;
    properties.type = 0x5678;
    properties.attack = 2600;
    properties.defense = 1900;
    properties.link_rating = 2;
    properties.link_markers = {LinkMarker::Right, LinkMarker::TopLeft};
    properties.counters = {{4, 1}, {2, 5}};
    return properties;
}

ObservedCard known_card(const ObservationLocator& locator) {
    ObservedCard card;
    card.locator = locator;
    card.identity_known = true;
    card.passcode = 12345678;
    card.owner = 1;
    card.controller = 1;
    card.zone = SemanticZone::MonsterZone;
    card.sequence = 2;
    card.position = ygo::observation::Position::FaceUpAttack;
    card.face_up = true;
    card.printed = printed_properties();
    card.current = current_properties();
    return card;
}

ObservedCard redacted_card() {
    ObservedCard card;
    card.locator = {"p0:HAND:7"};
    card.identity_known = false;
    card.controller = 0;
    card.zone = SemanticZone::Hand;
    card.sequence = 7;
    card.position = ygo::observation::Position::FaceDownDefense;
    card.face_down = true;
    return card;
}

PlayerObservation rich_fixture() {
    PlayerObservation observation;
    observation.schema_version = "ygo.m4.3.5\"\\escaped\n\r\t";
    observation.schema_version.push_back('\x01');
    observation.perspective_player = 1;
    observation.decision_index = 42;
    observation.engine_step_index = 1001;
    observation.globals.duel_flags = 0xA5;
    observation.globals.life_points = {8000, 6500};
    observation.globals.player_to_act = 1;
    observation.globals.turn_player = 0;
    observation.globals.turn_count = 3;
    observation.globals.phase = 8;
    observation.globals.chain_length = 2;

    observation.zones = {
        {1, SemanticZone::Hand, 5, 2, 3, true},
        {0, SemanticZone::MonsterZone, 5, 1, 4, false},
        {1, SemanticZone::ExtraDeck, 2, 2, 0, true},
    };

    ObservationLocator escaped_locator;
    escaped_locator.value = "p1:MONSTER_ZONE:2\"\\escaped\n";
    observation.entities.push_back(redacted_card());
    observation.entities.push_back(known_card(escaped_locator));
    ObservedCard overlay;
    overlay.locator = {"p1:OVERLAY:0:0"};
    overlay.identity_known = true;
    overlay.passcode = 87654321;
    overlay.owner = 1;
    overlay.controller = 1;
    overlay.zone = SemanticZone::Overlay;
    overlay.sequence = 0;
    overlay.overlay_sequence = 0;
    overlay.position = ygo::observation::Position::FaceUpDefense;
    overlay.face_up = true;
    observation.entities.push_back(overlay);

    observation.relationships = {
        {RelationshipKind::Target, {"p1:OVERLAY:0:0"}, {"p0:HAND:7"}},
        {RelationshipKind::XyzMaterial, {"p1:OVERLAY:0:0"}, escaped_locator},
        {RelationshipKind::Equip, escaped_locator, {"p1:OVERLAY:0:0"}},
    };

    observation.chain.length = 2;
    ChainLink first_link;
    first_link.index = 0;
    first_link.activating_player = 1;
    first_link.source = escaped_locator;
    first_link.activation_zone = SemanticZone::MonsterZone;
    first_link.effect_description = 0x100000002ULL;
    first_link.targets = {{"p0:HAND:7"}, escaped_locator, {"p1:OVERLAY:0:0"}};
    observation.chain.links.push_back(first_link);
    ChainLink second_link;
    second_link.index = 1;
    second_link.activating_player = 0;
    second_link.targets = {{"p1:OVERLAY:0:0"}, {"p0:HAND:7"}};
    observation.chain.links.push_back(second_link);

    ygo::observation::VisibleGameEvent event;
    event.event_index = 11;
    event.engine_step_index = 1000;
    event.kind = ygo::observation::VisibleEventKind::CardMoved;
    event.player = 1;
    event.entity = escaped_locator;
    event.public_passcode = 12345678;
    event.from_zone = SemanticZone::Hand;
    event.to_zone = SemanticZone::MonsterZone;
    event.count = 1;
    event.amount = -500;
    event.counter_type = 9;
    event.phase = 8;
    event.effect_description = 0x200000003ULL;
    event.targets = {{"p0:HAND:7"}, {"p1:OVERLAY:0:0"}};
    observation.visible_events.push_back(event);

    ygo::observation::VisibleGameEvent counter_event;
    counter_event.event_index = 12;
    counter_event.engine_step_index = 1001;
    counter_event.kind = ygo::observation::VisibleEventKind::CounterChanged;
    counter_event.entity = {"p1:OVERLAY:0:0"};
    counter_event.count = 2;
    counter_event.counter_type = 4;
    observation.visible_events.push_back(counter_event);

    observation.decision_context.decision_id = "decision\"\\id\n";
    observation.decision_context.kind = "select-target\tphase";
    observation.decision_context.engine_step_index = 1001;
    observation.decision_context.player = 1;
    observation.decision_context.engine_message_type = 15;
    observation.decision_context.engine_message_name = "MSG\rNAME";
    observation.decision_context.continuation_id = "continuation\x01";
    observation.decision_context.referenced_entities = {
        {"p0:HAND:7"}, escaped_locator, {"p1:OVERLAY:0:0"}};

    observation.match_context.perspective_player = 1;
    observation.match_context.duel_flags = 0xC0DE;
    observation.match_context.knowledge.own_decklist_known = true;
    observation.match_context.knowledge.opponent_decklist_known = false;
    observation.match_context.own_deck = {true, {9, 2, 7}, {4, 1}};
    observation.match_context.opponent_deck = {false, {8, 3}, {6}};
    observation.observation_hash = "stale-value-is-not-serialized";
    return observation;
}

PlayerObservation terminal_fixture() {
    auto observation = rich_fixture();
    observation.schema_version = "ygo.m4.3.5 terminal";
    observation.perspective_player = 0;
    observation.decision_index = 43;
    observation.engine_step_index = 1002;
    observation.globals.player_to_act.reset();
    observation.globals.turn_player = 1;
    observation.globals.winner = 1;
    observation.globals.win_reason = 4;
    observation.globals.terminal = true;
    observation.visible_events.back().kind = ygo::observation::VisibleEventKind::Win;
    observation.visible_events.back().winner = 1;
    observation.visible_events.back().win_reason = 4;
    return observation;
}

void write_binary_dump(
    const std::filesystem::path& dump_dir,
    const std::string& fixture_name,
    const std::string& artifact_name,
    const std::string& bytes) {
    const auto path = dump_dir / (fixture_name + "." + artifact_name + ".bin");
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    require(stream.is_open(), "could not open canonical fixture dump");
    stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    require(stream.good(), "could not write canonical fixture dump");
}

void write_fixture_dumps(
    const std::filesystem::path& dump_dir,
    const std::string& fixture_name,
    const std::string& without_hash,
    const std::string& canonical) {
    std::error_code error;
    std::filesystem::create_directories(dump_dir, error);
    if (error) {
        throw std::runtime_error("could not create canonical fixture dump directory");
    }
    write_binary_dump(dump_dir, fixture_name, "canonical_without_hash", without_hash);
    write_binary_dump(dump_dir, fixture_name, "canonical", canonical);
}

void check_fixture(
    const std::string& name,
    const PlayerObservation& observation,
    const std::filesystem::path* dump_dir) {
    const auto without_hash = ygo::observation::canonical_serialize_without_hash(observation);
    const auto observed_hash = ygo::observation::observation_hash(observation);
    const auto canonical = ygo::observation::canonical_serialize(observation);
    const auto expected_hash = ygo::trace::sha256_string(without_hash);

    require(!without_hash.empty() && without_hash.back() == '}',
            "canonical without-hash bytes did not end with the object terminator");
    require(without_hash.find("\\\"") != std::string::npos &&
                without_hash.find("\\\\") != std::string::npos &&
                without_hash.find("\\n") != std::string::npos &&
                without_hash.find("\\r") != std::string::npos &&
                without_hash.find("\\t") != std::string::npos &&
                without_hash.find("\\u0001") != std::string::npos,
            "canonical bytes did not preserve all JSON escape forms");
    require(without_hash.find("\"kind\":\"XYZ_MATERIAL\"") != std::string::npos,
            "canonical bytes did not preserve the XYZ material relationship");
    require(observed_hash == expected_hash,
            "observation_hash did not equal SHA-256 of exact no-hash bytes");
    require(canonical.size() > without_hash.size() && canonical.back() == '\n',
            "canonical bytes did not append a hash record");
    require(canonical.find("\"observation_hash\":\"" + expected_hash + "\"") != std::string::npos,
            "canonical bytes did not contain the computed observation hash");

    require(without_hash == ygo::observation::canonical_serialize_without_hash(observation),
            "repeated no-hash serialization was not deterministic");
    require(canonical == ygo::observation::canonical_serialize(observation),
            "repeated canonical serialization was not deterministic");

    if (dump_dir != nullptr) {
        write_fixture_dumps(*dump_dir, name, without_hash, canonical);
    }

    std::cout << "m4_3_5_fixture=" << name
              << " without_hash_bytes=" << without_hash.size()
              << " without_hash_sha256=" << ygo::trace::sha256_string(without_hash)
              << " observation_hash=" << observed_hash
              << " canonical_bytes=" << canonical.size()
              << " canonical_sha256=" << ygo::trace::sha256_string(canonical) << '\n';
}

int run(const std::filesystem::path* dump_dir) {
    check_fixture("rich", rich_fixture(), dump_dir);
    check_fixture("terminal", terminal_fixture(), dump_dir);
    std::cout << "m4_3_5_fixture_test=ok\n";
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        std::filesystem::path dump_dir;
        const std::filesystem::path* dump_dir_ptr = nullptr;
        for (int index = 1; index < argc; ++index) {
            if (std::string(argv[index]) != "--dump-dir" || index + 1 >= argc) {
                throw std::runtime_error(
                    "usage: m4_3_5_serialization_fixture_test [--dump-dir DIR]");
            }
            dump_dir = argv[++index];
            if (dump_dir.empty()) {
                throw std::runtime_error(
                    "usage: m4_3_5_serialization_fixture_test [--dump-dir DIR]");
            }
            dump_dir_ptr = &dump_dir;
        }
        return run(dump_dir_ptr);
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
