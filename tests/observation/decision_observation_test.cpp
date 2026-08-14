#include <iostream>
#include <stdexcept>

#include "common.h"
#include "ygo/observation/decision_integration.hpp"
#include "ygo/observation/serialization.hpp"

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

int run() {
    ygo::observation::PlayerObservation observation;
    observation.perspective_player = 0;
    observation.match_context.duel_flags = 0;
    ygo::observation::ObservedCard visible;
    visible.locator = {"p0:MONSTER_ZONE:2"};
    visible.identity_known = true;
    visible.passcode = 1234;
    visible.controller = 0;
    visible.zone = ygo::observation::SemanticZone::MonsterZone;
    visible.sequence = 2;
    observation.entities.push_back(visible);

    ygo::protocol::DecisionRequest request;
    request.kind = ygo::protocol::DecisionRequestKind::BattleCommand;
    request.decision_id = "decision-visible";
    request.engine_step_index = 17;
    request.player = 0;
    request.engine_message_type = MSG_SELECT_BATTLECMD;
    request.engine_message_name = "select_battlecmd";
    ygo::protocol::ActionCandidate candidate;
    candidate.semantic_key = "attack.visible";
    candidate.source_card = 1234;
    candidate.source_controller = 0;
    candidate.source_location = LOCATION_MZONE;
    candidate.source_sequence = 2;
    request.candidates.push_back(candidate);

    require(ygo::observation::candidate_observation_consistent(observation, candidate),
            "visible candidate did not resolve against observation");
    ygo::observation::attach_decision_context(observation, request);
    require(observation.decision_context.decision_id.value_or("") == "decision-visible",
            "decision identity was not attached");
    require(observation.decision_context.kind.value_or("") == "battle_command",
            "decision kind was not attached");
    require(observation.decision_context.referenced_entities.size() == 1 &&
                observation.decision_context.referenced_entities.front().value == "p0:MONSTER_ZONE:2",
            "candidate reference was not projected to the visible locator");

    auto hidden_candidate = candidate;
    hidden_candidate.semantic_key = "attack-hidden";
    hidden_candidate.source_card = 0xdeadbeef;
    require(!ygo::observation::candidate_observation_consistent(observation, hidden_candidate),
            "hidden candidate was treated as observable by passcode manufacture");
    const auto serialized = ygo::observation::canonical_serialize(observation);
    require(serialized.find("engine_message_type") != std::string::npos,
            "decision context serialization omitted engine message metadata");
    std::cout << "decision_observation=ok\n";
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
