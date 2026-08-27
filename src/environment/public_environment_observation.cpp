#include "ygo/environment/public_environment_observation.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "ygo/trace/sha256.hpp"

namespace ygo::environment {
namespace {

using ygo::observation::CardProperties;
using ygo::observation::ChainLink;
using ygo::observation::LinkMarker;
using ygo::observation::ObservationLocator;
using ygo::observation::ObservedCard;
using ygo::observation::ObservedZone;
using ygo::observation::PlayerObservation;
using ygo::observation::Relationship;
using ygo::observation::RelationshipKind;
using ygo::observation::SemanticZone;
using ygo::observation::VisibleEventKind;
using ygo::observation::VisibleGameEvent;

constexpr std::string_view kPlayerObservationSchemaId = "ygo.player_observation.v1";

void append_u8(std::vector<std::uint8_t>& bytes, const std::uint8_t value) {
    bytes.push_back(value);
}

void append_bool(std::vector<std::uint8_t>& bytes, const bool value) {
    append_u8(bytes, value ? 1 : 0);
}

void append_u32be(std::vector<std::uint8_t>& bytes, const std::uint32_t value) {
    bytes.push_back(static_cast<std::uint8_t>(value >> 24));
    bytes.push_back(static_cast<std::uint8_t>(value >> 16));
    bytes.push_back(static_cast<std::uint8_t>(value >> 8));
    bytes.push_back(static_cast<std::uint8_t>(value));
}

void append_u64be(std::vector<std::uint8_t>& bytes, const std::uint64_t value) {
    for (int shift = 56; shift >= 0; shift -= 8) {
        bytes.push_back(static_cast<std::uint8_t>(value >> shift));
    }
}

void append_count(std::vector<std::uint8_t>& bytes, const std::size_t value) {
    if (value > std::numeric_limits<std::uint32_t>::max()) {
        throw std::length_error("public observation field exceeds u32 length");
    }
    append_u32be(bytes, static_cast<std::uint32_t>(value));
}

void append_string(std::vector<std::uint8_t>& bytes, const std::string_view value) {
    append_count(bytes, value.size());
    bytes.insert(bytes.end(), value.begin(), value.end());
}

void append_bytes(std::vector<std::uint8_t>& bytes,
                  const std::vector<std::uint8_t>& value) {
    append_count(bytes, value.size());
    bytes.insert(bytes.end(), value.begin(), value.end());
}

void append_optional_u8_value(std::vector<std::uint8_t>& bytes,
                              const std::optional<std::uint8_t>& value) {
    append_bool(bytes, value.has_value());
    if (value.has_value()) {
        append_u8(bytes, *value);
    }
}

void append_optional_u32_value(std::vector<std::uint8_t>& bytes,
                               const std::optional<std::uint32_t>& value) {
    append_bool(bytes, value.has_value());
    if (value.has_value()) {
        append_u32be(bytes, *value);
    }
}

void append_optional_u64_value(std::vector<std::uint8_t>& bytes,
                               const std::optional<std::uint64_t>& value) {
    append_bool(bytes, value.has_value());
    if (value.has_value()) {
        append_u64be(bytes, *value);
    }
}

void append_optional_i32_value(std::vector<std::uint8_t>& bytes,
                               const std::optional<std::int32_t>& value) {
    append_bool(bytes, value.has_value());
    if (value.has_value()) {
        append_u32be(bytes, static_cast<std::uint32_t>(*value));
    }
}

bool is_locator(const std::string_view value) noexcept {
    return !value.empty() &&
           std::all_of(value.begin(), value.end(), [](const unsigned char character) {
               return character >= 0x20 && character != 0x7f;
           });
}

void validate_locator(const ObservationLocator& locator) {
    if (!is_locator(locator.value)) {
        throw std::invalid_argument("public observation contains an invalid locator");
    }
}

void validate_optional_player(const std::optional<std::uint8_t>& value,
                              const char* field_name) {
    if (value.has_value() && *value > 1) {
        throw std::invalid_argument(std::string("public observation has invalid ") + field_name);
    }
}

std::uint8_t semantic_zone_code(const SemanticZone value) {
    switch (value) {
    case SemanticZone::Unknown:
        return 0;
    case SemanticZone::MainDeck:
        return 1;
    case SemanticZone::Hand:
        return 2;
    case SemanticZone::MonsterZone:
        return 3;
    case SemanticZone::SpellTrapZone:
        return 4;
    case SemanticZone::Graveyard:
        return 5;
    case SemanticZone::Banished:
        return 6;
    case SemanticZone::ExtraDeck:
        return 7;
    case SemanticZone::FieldZone:
        return 8;
    case SemanticZone::PendulumRelevant:
        return 9;
    case SemanticZone::Overlay:
        return 10;
    }
    throw std::invalid_argument("public observation has an unknown semantic zone");
}

std::uint8_t position_code(const ygo::observation::Position value) {
    switch (value) {
    case ygo::observation::Position::Unknown:
        return 0;
    case ygo::observation::Position::FaceUpAttack:
        return 1;
    case ygo::observation::Position::FaceDownAttack:
        return 2;
    case ygo::observation::Position::FaceUpDefense:
        return 4;
    case ygo::observation::Position::FaceDownDefense:
        return 8;
    }
    throw std::invalid_argument("public observation has an unknown card position");
}

std::uint8_t link_marker_code(const LinkMarker value) {
    switch (value) {
    case LinkMarker::BottomLeft:
        return 0;
    case LinkMarker::Bottom:
        return 1;
    case LinkMarker::BottomRight:
        return 2;
    case LinkMarker::Left:
        return 3;
    case LinkMarker::Right:
        return 4;
    case LinkMarker::TopLeft:
        return 5;
    case LinkMarker::Top:
        return 6;
    case LinkMarker::TopRight:
        return 7;
    }
    throw std::invalid_argument("public observation has an unknown link marker");
}

std::uint8_t relationship_kind_code(const RelationshipKind value) {
    switch (value) {
    case RelationshipKind::XyzMaterial:
        return 0;
    case RelationshipKind::Equip:
        return 1;
    case RelationshipKind::Target:
        return 2;
    }
    throw std::invalid_argument("public observation has an unknown relationship kind");
}

std::uint8_t visible_event_kind_code(const VisibleEventKind value) {
    switch (value) {
    case VisibleEventKind::Unknown:
        return 0;
    case VisibleEventKind::TurnStarted:
        return 1;
    case VisibleEventKind::PhaseChanged:
        return 2;
    case VisibleEventKind::CardMoved:
        return 3;
    case VisibleEventKind::CardRevealed:
        return 4;
    case VisibleEventKind::Summoned:
        return 5;
    case VisibleEventKind::Set:
        return 6;
    case VisibleEventKind::Draw:
        return 7;
    case VisibleEventKind::Shuffle:
        return 8;
    case VisibleEventKind::RandomizationBoundary:
        return 9;
    case VisibleEventKind::LifePointsChanged:
        return 10;
    case VisibleEventKind::ChainActivated:
        return 11;
    case VisibleEventKind::ChainResolved:
        return 12;
    case VisibleEventKind::ChainEnded:
        return 13;
    case VisibleEventKind::CardDestroyed:
        return 14;
    case VisibleEventKind::CardBanished:
        return 15;
    case VisibleEventKind::CardReturned:
        return 16;
    case VisibleEventKind::PositionChanged:
        return 17;
    case VisibleEventKind::CounterChanged:
        return 18;
    case VisibleEventKind::Equipped:
        return 19;
    case VisibleEventKind::Unequipped:
        return 20;
    case VisibleEventKind::Targeted:
        return 21;
    case VisibleEventKind::Win:
        return 22;
    }
    throw std::invalid_argument("public observation has an unknown visible event kind");
}

void validate_properties(const CardProperties& properties) {
    for (const auto marker : properties.link_markers) {
        (void)link_marker_code(marker);
    }
}

void validate_card(const ObservedCard& card) {
    validate_locator(card.locator);
    validate_optional_player(card.owner, "card owner");
    validate_optional_player(card.controller, "card controller");
    (void)semantic_zone_code(card.zone);
    (void)position_code(card.position);
    if (card.face_up && card.face_down) {
        throw std::invalid_argument("public observation card cannot be face-up and face-down");
    }
    if (!card.identity_known &&
        (card.passcode.has_value() || card.printed.has_value() || card.current.has_value())) {
        throw std::invalid_argument("public observation hidden card contains identity data");
    }
    if (card.printed.has_value()) {
        validate_properties(*card.printed);
    }
    if (card.current.has_value()) {
        validate_properties(*card.current);
    }
}

void validate_deck(const ygo::observation::StaticDeckContext& deck,
                   const char* field_name) {
    if (!deck.known && (!deck.main_deck.empty() || !deck.extra_deck.empty())) {
        throw std::invalid_argument(std::string("public observation unknown ") + field_name +
                                    " contains card identities");
    }
}

void validate_observation(const PlayerObservation& observation) {
    if (observation.schema_version != kPlayerObservationSchemaId) {
        throw std::invalid_argument("public observation source schema is not supported");
    }
    if (observation.perspective_player > 1) {
        throw std::invalid_argument("public observation perspective is invalid");
    }

    for (const auto& zone : observation.zones) {
        if (zone.player > 1) {
            throw std::invalid_argument("public observation zone player is invalid");
        }
        (void)semantic_zone_code(zone.kind);
    }

    std::vector<ObservedCard> sorted_entities = observation.entities;
    for (const auto& entity : sorted_entities) {
        validate_card(entity);
    }
    std::sort(sorted_entities.begin(), sorted_entities.end(),
              [](const ObservedCard& left, const ObservedCard& right) {
                  return left.locator.value < right.locator.value;
              });
    for (std::size_t index = 1; index < sorted_entities.size(); ++index) {
        if (sorted_entities[index - 1].locator == sorted_entities[index].locator) {
            throw std::invalid_argument("public observation contains duplicate entity locators");
        }
    }

    for (const auto& relationship : observation.relationships) {
        (void)relationship_kind_code(relationship.kind);
        validate_locator(relationship.source);
        validate_locator(relationship.target);
    }

    if (observation.globals.player_to_act.has_value() &&
        *observation.globals.player_to_act > 1) {
        throw std::invalid_argument("public observation player_to_act is invalid");
    }
    if (observation.globals.turn_player.has_value() && *observation.globals.turn_player > 1) {
        throw std::invalid_argument("public observation turn_player is invalid");
    }
    if (observation.globals.winner.has_value() && *observation.globals.winner > 1) {
        throw std::invalid_argument("public observation winner is invalid");
    }
    for (const auto& link : observation.chain.links) {
        validate_optional_player(link.activating_player, "chain activating player");
        if (link.source.has_value()) {
            validate_locator(*link.source);
        }
        if (link.activation_zone.has_value()) {
            (void)semantic_zone_code(*link.activation_zone);
        }
        for (const auto& target : link.targets) {
            validate_locator(target);
        }
    }

    std::vector<std::uint64_t> event_indices;
    event_indices.reserve(observation.visible_events.size());
    for (const auto& event : observation.visible_events) {
        (void)visible_event_kind_code(event.kind);
        validate_optional_player(event.player, "visible event player");
        validate_optional_player(event.winner, "visible event winner");
        if (event.entity.has_value()) {
            validate_locator(*event.entity);
        }
        if (event.from_zone.has_value()) {
            (void)semantic_zone_code(*event.from_zone);
        }
        if (event.to_zone.has_value()) {
            (void)semantic_zone_code(*event.to_zone);
        }
        for (const auto& target : event.targets) {
            validate_locator(target);
        }
        event_indices.push_back(event.event_index);
    }
    std::sort(event_indices.begin(), event_indices.end());
    for (std::size_t index = 1; index < event_indices.size(); ++index) {
        if (event_indices[index - 1] == event_indices[index]) {
            throw std::invalid_argument("public observation contains duplicate event indices");
        }
    }

    if (observation.match_context.perspective_player > 1) {
        throw std::invalid_argument("public observation match-context perspective is invalid");
    }
    if (observation.match_context.perspective_player != observation.perspective_player) {
        throw std::invalid_argument("public observation perspectives disagree");
    }
    validate_deck(observation.match_context.own_deck, "own deck");
    validate_deck(observation.match_context.opponent_deck, "opponent deck");
}

void append_optional_locator(std::vector<std::uint8_t>& bytes,
                            const std::optional<ObservationLocator>& value) {
    append_bool(bytes, value.has_value());
    if (value.has_value()) {
        append_string(bytes, value->value);
    }
}

void append_optional_zone(std::vector<std::uint8_t>& bytes,
                          const std::optional<SemanticZone>& value) {
    append_bool(bytes, value.has_value());
    if (value.has_value()) {
        append_u8(bytes, semantic_zone_code(*value));
    }
}

void append_properties(std::vector<std::uint8_t>& bytes,
                       const std::optional<CardProperties>& value) {
    append_bool(bytes, value.has_value());
    if (!value.has_value()) {
        return;
    }
    const auto& properties = *value;
    append_optional_u32_value(bytes, properties.type);
    append_optional_u32_value(bytes, properties.attribute);
    append_optional_u64_value(bytes, properties.race);
    append_optional_i32_value(bytes, properties.attack);
    append_optional_i32_value(bytes, properties.defense);
    append_optional_i32_value(bytes, properties.base_attack);
    append_optional_i32_value(bytes, properties.base_defense);
    append_optional_u32_value(bytes, properties.level);
    append_optional_u32_value(bytes, properties.rank);
    append_optional_u32_value(bytes, properties.link_rating);

    auto markers = properties.link_markers;
    std::sort(markers.begin(), markers.end(), [](const LinkMarker left, const LinkMarker right) {
        return link_marker_code(left) < link_marker_code(right);
    });
    append_count(bytes, markers.size());
    for (const auto marker : markers) {
        append_u8(bytes, link_marker_code(marker));
    }

    append_optional_u32_value(bytes, properties.left_scale);
    append_optional_u32_value(bytes, properties.right_scale);
    append_optional_u32_value(bytes, properties.status_flags);

    auto counters = properties.counters;
    std::sort(counters.begin(), counters.end(), [](const auto& left, const auto& right) {
        return std::tie(left.type, left.count) < std::tie(right.type, right.count);
    });
    append_count(bytes, counters.size());
    for (const auto& counter : counters) {
        append_u32be(bytes, counter.type);
        append_u32be(bytes, counter.count);
    }
}

void append_globals(std::vector<std::uint8_t>& bytes,
                    const ygo::observation::ObservedPlayerGlobals& globals) {
    append_u64be(bytes, globals.duel_flags);
    append_count(bytes, globals.life_points.size());
    for (const auto life_points : globals.life_points) {
        append_u32be(bytes, life_points);
    }
    append_optional_u8_value(bytes, globals.player_to_act);
    append_optional_u8_value(bytes, globals.turn_player);
    append_optional_u32_value(bytes, globals.turn_count);
    append_optional_u32_value(bytes, globals.phase);
    append_u32be(bytes, globals.chain_length);
    append_optional_u8_value(bytes, globals.winner);
    append_optional_u8_value(bytes, globals.win_reason);
    append_bool(bytes, globals.terminal);
}

void append_zones(std::vector<std::uint8_t>& bytes,
                  const std::vector<ObservedZone>& input) {
    auto zones = input;
    std::sort(zones.begin(), zones.end(), [](const ObservedZone& left, const ObservedZone& right) {
        return std::tie(left.player, left.kind, left.total_count, left.public_identity_count,
                        left.hidden_count, left.player_observable_order) <
               std::tie(right.player, right.kind, right.total_count, right.public_identity_count,
                        right.hidden_count, right.player_observable_order);
    });
    append_count(bytes, zones.size());
    for (const auto& zone : zones) {
        append_u8(bytes, zone.player);
        append_u8(bytes, semantic_zone_code(zone.kind));
        append_u32be(bytes, zone.total_count);
        append_u32be(bytes, zone.public_identity_count);
        append_u32be(bytes, zone.hidden_count);
        append_bool(bytes, zone.player_observable_order);
    }
}

void append_entities(std::vector<std::uint8_t>& bytes,
                     const std::vector<ObservedCard>& input) {
    auto entities = input;
    std::sort(entities.begin(), entities.end(), [](const ObservedCard& left, const ObservedCard& right) {
        return left.locator.value < right.locator.value;
    });
    append_count(bytes, entities.size());
    for (const auto& entity : entities) {
        append_string(bytes, entity.locator.value);
        append_bool(bytes, entity.identity_known);
        append_optional_u32_value(bytes, entity.passcode);
        append_optional_u8_value(bytes, entity.owner);
        append_optional_u8_value(bytes, entity.controller);
        append_u8(bytes, semantic_zone_code(entity.zone));
        append_optional_u32_value(bytes, entity.sequence);
        append_optional_u32_value(bytes, entity.overlay_sequence);
        append_u8(bytes, position_code(entity.position));
        append_bool(bytes, entity.face_up);
        append_bool(bytes, entity.face_down);
        append_properties(bytes, entity.printed);
        append_properties(bytes, entity.current);
    }
}

void append_relationships(std::vector<std::uint8_t>& bytes,
                          const std::vector<Relationship>& input) {
    auto relationships = input;
    std::sort(relationships.begin(), relationships.end(),
              [](const Relationship& left, const Relationship& right) {
                  return std::tie(left.kind, left.source.value, left.target.value) <
                         std::tie(right.kind, right.source.value, right.target.value);
              });
    append_count(bytes, relationships.size());
    for (const auto& relationship : relationships) {
        append_u8(bytes, relationship_kind_code(relationship.kind));
        append_string(bytes, relationship.source.value);
        append_string(bytes, relationship.target.value);
    }
}

void append_targets(std::vector<std::uint8_t>& bytes,
                    const std::vector<ObservationLocator>& input) {
    auto targets = input;
    std::sort(targets.begin(), targets.end());
    append_count(bytes, targets.size());
    for (const auto& target : targets) {
        append_string(bytes, target.value);
    }
}

void append_chain(std::vector<std::uint8_t>& bytes,
                  const ygo::observation::ChainState& chain) {
    append_u32be(bytes, chain.length);
    append_count(bytes, chain.links.size());
    for (const ChainLink& link : chain.links) {
        append_u32be(bytes, link.index);
        append_optional_u8_value(bytes, link.activating_player);
        append_optional_locator(bytes, link.source);
        append_optional_zone(bytes, link.activation_zone);
        append_optional_u64_value(bytes, link.effect_description);
        append_targets(bytes, link.targets);
    }
}

void append_visible_event(std::vector<std::uint8_t>& bytes,
                          const VisibleGameEvent& event) {
    // event.engine_step_index is internal process metadata and is deliberately
    // omitted. event_index is the public history position.
    append_u64be(bytes, event.event_index);
    append_u8(bytes, visible_event_kind_code(event.kind));
    append_optional_u8_value(bytes, event.player);
    append_optional_locator(bytes, event.entity);
    append_optional_u32_value(bytes, event.public_passcode);
    append_optional_zone(bytes, event.from_zone);
    append_optional_zone(bytes, event.to_zone);
    append_optional_u32_value(bytes, event.count);
    append_optional_i32_value(bytes, event.amount);
    append_optional_u32_value(bytes, event.counter_type);
    append_optional_u32_value(bytes, event.phase);
    append_optional_u8_value(bytes, event.winner);
    append_optional_u8_value(bytes, event.win_reason);
    append_optional_u64_value(bytes, event.effect_description);
    append_targets(bytes, event.targets);
}

void append_visible_events(std::vector<std::uint8_t>& bytes,
                           const std::vector<VisibleGameEvent>& input) {
    auto events = input;
    std::sort(events.begin(), events.end(),
              [](const VisibleGameEvent& left, const VisibleGameEvent& right) {
                  return left.event_index < right.event_index;
              });
    append_count(bytes, events.size());
    for (const auto& event : events) {
        append_visible_event(bytes, event);
    }
}

void append_deck(std::vector<std::uint8_t>& bytes,
                 const ygo::observation::StaticDeckContext& deck) {
    append_bool(bytes, deck.known);
    auto main_deck = deck.main_deck;
    auto extra_deck = deck.extra_deck;
    std::sort(main_deck.begin(), main_deck.end());
    std::sort(extra_deck.begin(), extra_deck.end());
    append_count(bytes, main_deck.size());
    for (const auto code : main_deck) {
        append_u32be(bytes, code);
    }
    append_count(bytes, extra_deck.size());
    for (const auto code : extra_deck) {
        append_u32be(bytes, code);
    }
}

void append_match_context(std::vector<std::uint8_t>& bytes,
                          const ygo::observation::MatchContext& context) {
    append_u8(bytes, context.perspective_player);
    append_u64be(bytes, context.duel_flags);
    append_bool(bytes, context.knowledge.own_decklist_known);
    append_bool(bytes, context.knowledge.opponent_decklist_known);
    append_deck(bytes, context.own_deck);
    append_deck(bytes, context.opponent_deck);
}

std::vector<std::uint8_t> serialize_safe_state(const PlayerObservation& observation) {
    validate_observation(observation);

    std::vector<std::uint8_t> bytes;
    bytes.reserve(256 + observation.entities.size() * 96 +
                  observation.visible_events.size() * 64);
    append_string(bytes, kPublicSafeStateSchemaId);
    append_string(bytes, kPublicSafeStateSchemaId);
    append_globals(bytes, observation.globals);
    append_zones(bytes, observation.zones);
    append_entities(bytes, observation.entities);
    append_relationships(bytes, observation.relationships);
    append_chain(bytes, observation.chain);
    append_visible_events(bytes, observation.visible_events);
    append_match_context(bytes, observation.match_context);
    return bytes;
}

bool is_lower_token(const std::string_view value) noexcept {
    if (value.empty()) {
        return false;
    }
    return std::all_of(value.begin(), value.end(), [](const unsigned char character) {
        return (character >= 'a' && character <= 'z') ||
               (character >= '0' && character <= '9') || character == '_';
    });
}

void validate_input(const PublicEnvironmentObservationInput& input) {
    if (input.perspective_player > 1) {
        throw std::invalid_argument("public observation perspective is invalid");
    }
    if (input.canonical_safe_state_bytes().empty()) {
        throw std::invalid_argument("public observation safe-state bytes are empty");
    }
    if (input.decision_context.kind.has_value() &&
        !is_lower_token(*input.decision_context.kind)) {
        throw std::invalid_argument("public observation request kind is not canonical");
    }
    if (input.decision_context.player.has_value() && *input.decision_context.player > 1) {
        throw std::invalid_argument("public observation acting player is invalid");
    }
    for (std::size_t index = 0; index < input.decision_context.referenced_entities.size(); ++index) {
        const auto& locator = input.decision_context.referenced_entities[index].value;
        if (!is_locator(locator)) {
            throw std::invalid_argument("public observation contains an invalid locator");
        }
        for (std::size_t previous = 0; previous < index; ++previous) {
            if (locator == input.decision_context.referenced_entities[previous].value) {
                throw std::invalid_argument("public observation contains a duplicate locator");
            }
        }
    }
}

}  // namespace

std::vector<std::uint8_t> canonical_public_safe_state_bytes(
    const ygo::observation::PlayerObservation& observation) {
    return serialize_safe_state(observation);
}

PublicEnvironmentObservationInput project_public_observation(
    const ygo::observation::PlayerObservation& observation) {
    PublicEnvironmentObservationInput result;
    result.perspective_player = observation.perspective_player;
    result.decision_index = observation.decision_index;
    result.canonical_safe_state_bytes_ = canonical_public_safe_state_bytes(observation);

    // This is the complete public decision-context projection. In particular,
    // decision_id, engine_step_index, engine message internals, continuation_id,
    // and the v1 observation_hash are intentionally not copied.
    result.decision_context.kind = observation.decision_context.kind;
    result.decision_context.player = observation.decision_context.player;
    result.decision_context.referenced_entities = observation.decision_context.referenced_entities;
    validate_input(result);
    return result;
}

std::vector<std::uint8_t> canonical_public_environment_observation_bytes(
    const PublicEnvironmentObservationInput& input) {
    validate_input(input);

    auto references = input.decision_context.referenced_entities;
    std::sort(references.begin(), references.end(), [](const auto& left, const auto& right) {
        return left.value < right.value;
    });

    std::vector<std::uint8_t> bytes;
    bytes.reserve(128 + input.canonical_safe_state_bytes().size() + references.size() * 32);
    append_string(bytes, kPublicEnvironmentObservationSchemaId);
    append_string(bytes, kPublicEnvironmentObservationSchemaId);
    append_u8(bytes, input.perspective_player);
    append_u64be(bytes, input.decision_index);
    append_bytes(bytes, input.canonical_safe_state_bytes());
    append_u8(bytes, input.decision_context.kind.has_value() ? 1 : 0);
    if (input.decision_context.kind.has_value()) {
        append_string(bytes, *input.decision_context.kind);
    }
    append_u8(bytes, input.decision_context.player.has_value() ? 1 : 0);
    if (input.decision_context.player.has_value()) {
        append_u8(bytes, *input.decision_context.player);
    }
    append_count(bytes, references.size());
    for (const auto& locator : references) {
        append_string(bytes, locator.value);
    }
    return bytes;
}

std::string public_observation_digest(const PublicEnvironmentObservationInput& input) {
    return trace::sha256_bytes(canonical_public_environment_observation_bytes(input));
}

}  // namespace ygo::environment
