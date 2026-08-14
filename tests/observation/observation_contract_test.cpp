#include <iostream>
#include <stdexcept>
#include <string>

#include "ygo/observation/player_observation.hpp"
#include "ygo/observation/serialization.hpp"

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

ygo::observation::ObservedCard known_card() {
    ygo::observation::ObservedCard card;
    card.locator = {"p0:HAND:0"};
    card.identity_known = true;
    card.passcode = 1001;
    card.controller = 0;
    card.zone = ygo::observation::SemanticZone::Hand;
    card.sequence = 0;
    card.position = ygo::observation::Position::FaceDownDefense;
    card.face_down = true;
    card.printed.emplace();
    card.printed->type = 0x1;
    card.current.emplace();
    card.current->attack = 1200;
    return card;
}

ygo::observation::ObservedCard redacted_card() {
    ygo::observation::ObservedCard card;
    card.locator = {"p1:SPELL_TRAP_ZONE:0"};
    card.identity_known = false;
    card.controller = 1;
    card.zone = ygo::observation::SemanticZone::SpellTrapZone;
    card.sequence = 0;
    card.position = ygo::observation::Position::FaceDownDefense;
    card.face_down = true;
    return card;
}

int run() {
    ygo::observation::PlayerObservation observation;
    observation.schema_version = "ygo.player_observation.v1";
    observation.perspective_player = 0;
    observation.entities.push_back(known_card());
    observation.entities.push_back(redacted_card());
    observation.relationships.push_back({ygo::observation::RelationshipKind::XyzMaterial,
                                         {"p0:OVERLAY:0:0"}, {"p0:MONSTER_ZONE:0"}});
    observation.globals.turn_count.reset();
    observation.chain.length = 1;
    ygo::observation::ChainLink chain_link;
    chain_link.index = 0;
    chain_link.activating_player = 0;
    chain_link.source = ygo::observation::ObservationLocator{"p0:HAND:0"};
    chain_link.activation_zone = ygo::observation::SemanticZone::Hand;
    chain_link.effect_description = 99;
    observation.chain.links.push_back(chain_link);
    observation.entities.front().current->counters.push_back({7, 2});
    ygo::observation::VisibleGameEvent event;
    event.event_index = 7;
    event.kind = ygo::observation::VisibleEventKind::Shuffle;
    observation.visible_events.push_back(event);

    const auto bytes = ygo::observation::canonical_serialize(observation);
    require(bytes.find("\"turn_count\":null") != std::string::npos,
            "absent global was not serialized as null");
    require(bytes.find("XYZ_MATERIAL") != std::string::npos,
            "relationship enum was not canonical");
    require(bytes.find("\"chain\":{\"length\":1") != std::string::npos &&
                bytes.find("\"effect_description\":99") != std::string::npos,
            "structured chain state was not canonical");
    require(ygo::observation::observation_hash(observation).size() == 64,
            "observation hash is not SHA-256 hex");
    require(bytes.find("max_cards") == std::string::npos &&
                bytes.find("max_entities") == std::string::npos,
            "authoritative observation contains a fixed tensor cap");
    require(bytes.find("\"passcode\":null") != std::string::npos,
            "unknown card identity was not explicitly null");
    std::cout << "observation_contract=ok\n";
    return 0;
}

}  // namespace

int main() {
    try {
        return run();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
