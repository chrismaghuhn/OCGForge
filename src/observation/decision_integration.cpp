#include "ygo/observation/decision_integration.hpp"

#include <algorithm>
#include <cstdint>
#include <utility>

#include "common.h"
#include "ygo/observation/serialization.hpp"
#include "ygo/protocol/message_decoder.hpp"
#include "zone_projection.hpp"

namespace ygo::observation {
namespace {

std::optional<ObservationLocator> resolve_card(const PlayerObservation& observation,
                                               std::uint32_t code,
                                               std::uint8_t controller,
                                               std::uint32_t location,
                                               std::uint32_t sequence) {
    const auto zone = detail::project_zone(location, controller, sequence,
                                           static_cast<std::uint32_t>(observation.match_context.duel_flags)).zone;
    for (const auto& entity : observation.entities) {
        if (entity.controller.value_or(2) != controller || entity.zone != zone) {
            continue;
        }
        if (entity.sequence.has_value() && *entity.sequence != sequence) {
            continue;
        }
        if (code != 0 && (!entity.identity_known || entity.passcode.value_or(0) != code)) {
            continue;
        }
        return entity.locator;
    }
    return std::nullopt;
}

bool card_reference_consistent(const PlayerObservation& observation,
                               std::uint32_t code,
                               std::uint8_t controller,
                               std::uint32_t location,
                               std::uint32_t sequence) {
    return resolve_card(observation, code, controller, location, sequence).has_value();
}

void append_reference(PlayerObservation& observation,
                      std::uint32_t code,
                      std::uint8_t controller,
                      std::uint32_t location,
                      std::uint32_t sequence) {
    if (const auto locator = resolve_card(observation, code, controller, location, sequence);
        locator.has_value()) {
        if (std::find(observation.decision_context.referenced_entities.begin(),
                      observation.decision_context.referenced_entities.end(), *locator) ==
            observation.decision_context.referenced_entities.end()) {
            observation.decision_context.referenced_entities.push_back(*locator);
        }
    }
}

}  // namespace

void attach_decision_context(PlayerObservation& observation,
                             const ygo::protocol::DecisionRequest& request) {
    DecisionContext context;
    context.decision_id = request.decision_id;
    context.kind = ygo::protocol::decision_kind_name(request.kind);
    context.engine_step_index = request.engine_step_index;
    context.player = request.player;
    context.engine_message_type = request.engine_message_type;
    context.engine_message_name = request.engine_message_name;
    if (request.continuation.has_value()) {
        context.continuation_id = request.continuation->continuation_id;
    }
    observation.decision_context = std::move(context);
    observation.globals.player_to_act = request.player;
    for (const auto& candidate : request.candidates) {
        if (candidate.source_card != 0) {
            append_reference(observation, candidate.source_card, candidate.source_controller,
                             candidate.source_location, candidate.source_sequence);
        }
        if (candidate.target_card != 0) {
            append_reference(observation, candidate.target_card, candidate.target_controller,
                             candidate.target_location, candidate.target_sequence);
        }
    }
    std::sort(observation.decision_context.referenced_entities.begin(),
              observation.decision_context.referenced_entities.end());
    observation.observation_hash = observation_hash(observation);
}

bool candidate_observation_consistent(const PlayerObservation& observation,
                                      const ygo::protocol::ActionCandidate& candidate) {
    if (candidate.source_card != 0 &&
        !card_reference_consistent(observation, candidate.source_card, candidate.source_controller,
                                   candidate.source_location, candidate.source_sequence)) {
        return false;
    }
    if (candidate.target_card != 0 &&
        !card_reference_consistent(observation, candidate.target_card, candidate.target_controller,
                                   candidate.target_location, candidate.target_sequence)) {
        return false;
    }
    return true;
}

}  // namespace ygo::observation
