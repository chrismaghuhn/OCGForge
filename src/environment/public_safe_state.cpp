#include "ygo/environment/public_safe_state.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#include "ygo/environment/public_environment_observation.hpp"
#include "ygo/observation/player_observation.hpp"

namespace ygo::environment {
namespace {

using ygo::observation::CardProperties;
using ygo::observation::ChainLink;
using ygo::observation::Counter;
using ygo::observation::LinkMarker;
using ygo::observation::ObservationLocator;
using ygo::observation::ObservedCard;
using ygo::observation::ObservedPlayerGlobals;
using ygo::observation::ObservedZone;
using ygo::observation::PlayerObservation;
using ygo::observation::Relationship;
using ygo::observation::RelationshipKind;
using ygo::observation::SemanticZone;
using ygo::observation::VisibleEventKind;

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
        throw std::length_error("public safe state field exceeds u32 length");
    }
    append_u32be(bytes, static_cast<std::uint32_t>(value));
}

void append_string(std::vector<std::uint8_t>& bytes, const std::string_view value) {
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
        throw std::invalid_argument("public safe state contains an invalid locator");
    }
}

void validate_optional_player(const std::optional<std::uint8_t>& value,
                              const char* field_name) {
    if (value.has_value() && *value > 1) {
        throw std::invalid_argument(std::string("public safe state has invalid ") + field_name);
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
    throw std::invalid_argument("public safe state has an unknown semantic zone");
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
    throw std::invalid_argument("public safe state has an unknown card position");
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
    throw std::invalid_argument("public safe state has an unknown link marker");
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
    throw std::invalid_argument("public safe state has an unknown relationship kind");
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
    throw std::invalid_argument("public safe state has an unknown visible event kind");
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
        throw std::invalid_argument("public safe state card cannot be face-up and face-down");
    }
    if (!card.identity_known &&
        (card.passcode.has_value() || card.printed.has_value() || card.current.has_value())) {
        throw std::invalid_argument("public safe state hidden card contains identity data");
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
        throw std::invalid_argument(std::string("public safe state unknown ") + field_name +
                                    " contains card identities");
    }
}

void validate_observation(const PlayerObservation& observation) {
    if (observation.schema_version != kPlayerObservationSchemaId) {
        throw std::invalid_argument("public safe state source schema is not supported");
    }
    if (observation.perspective_player > 1) {
        throw std::invalid_argument("public safe state perspective is invalid");
    }

    for (const auto& zone : observation.zones) {
        if (zone.player > 1) {
            throw std::invalid_argument("public safe state zone player is invalid");
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
            throw std::invalid_argument("public safe state contains duplicate entity locators");
        }
    }

    for (const auto& relationship : observation.relationships) {
        (void)relationship_kind_code(relationship.kind);
        validate_locator(relationship.source);
        validate_locator(relationship.target);
    }

    if (observation.globals.player_to_act.has_value() &&
        *observation.globals.player_to_act > 1) {
        throw std::invalid_argument("public safe state player_to_act is invalid");
    }
    if (observation.globals.turn_player.has_value() && *observation.globals.turn_player > 1) {
        throw std::invalid_argument("public safe state turn_player is invalid");
    }
    validate_optional_player(observation.globals.winner, "winner");
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
            throw std::invalid_argument("public safe state contains duplicate event indices");
        }
    }

    if (observation.match_context.perspective_player > 1) {
        throw std::invalid_argument("public safe state match-context perspective is invalid");
    }
    if (observation.match_context.perspective_player != observation.perspective_player) {
        throw std::invalid_argument("public safe state perspectives disagree");
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

void append_globals(std::vector<std::uint8_t>& bytes, const ObservedPlayerGlobals& globals) {
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

void append_zones(std::vector<std::uint8_t>& bytes, const std::vector<ObservedZone>& input) {
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

void append_entities(std::vector<std::uint8_t>& bytes, const std::vector<ObservedCard>& input) {
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

void append_chain(std::vector<std::uint8_t>& bytes, const ygo::observation::ChainState& chain) {
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
                          const PublicSafeVisibleEvent& event) {
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
                           const std::vector<PublicSafeVisibleEvent>& input) {
    auto events = input;
    std::sort(events.begin(), events.end(),
              [](const PublicSafeVisibleEvent& left, const PublicSafeVisibleEvent& right) {
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

std::vector<std::uint8_t> encode_view(const PublicSafeStateView& view) {
    std::vector<std::uint8_t> bytes;
    bytes.reserve(256 + view.entities().size() * 96 + view.visible_events().size() * 64);
    append_string(bytes, kPublicSafeStateSchemaId);
    append_string(bytes, kPublicSafeStateSchemaId);
    append_globals(bytes, view.globals());
    append_zones(bytes, view.zones());
    append_entities(bytes, view.entities());
    append_relationships(bytes, view.relationships());
    append_chain(bytes, view.chain());
    append_visible_events(bytes, view.visible_events());
    append_match_context(bytes, view.match_context());
    return bytes;
}

bool valid_utf8(const std::string_view value) noexcept {
    for (std::size_t index = 0; index < value.size();) {
        const auto first = static_cast<unsigned char>(value[index]);
        if (first <= 0x7f) {
            ++index;
            continue;
        }
        std::size_t width = 0;
        std::uint32_t code_point = 0;
        if (first >= 0xc2 && first <= 0xdf) {
            width = 2;
            code_point = first & 0x1f;
        } else if (first >= 0xe0 && first <= 0xef) {
            width = 3;
            code_point = first & 0x0f;
        } else if (first >= 0xf0 && first <= 0xf4) {
            width = 4;
            code_point = first & 0x07;
        } else {
            return false;
        }
        if (width > value.size() - index) {
            return false;
        }
        for (std::size_t continuation = 1; continuation < width; ++continuation) {
            const auto byte = static_cast<unsigned char>(value[index + continuation]);
            if ((byte & 0xc0) != 0x80) {
                return false;
            }
            code_point = (code_point << 6) | (byte & 0x3f);
        }
        if ((width == 3 && code_point < 0x800) ||
            (width == 4 && code_point < 0x10000) || code_point > 0x10ffff ||
            (code_point >= 0xd800 && code_point <= 0xdfff)) {
            return false;
        }
        index += width;
    }
    return true;
}

class Cursor final {
public:
    explicit Cursor(const std::vector<std::uint8_t>& bytes) noexcept : bytes_(bytes) {}

    bool u8(std::uint8_t& value) noexcept {
        if (position_ >= bytes_.size()) {
            return false;
        }
        value = bytes_[position_++];
        return true;
    }

    bool u32(std::uint32_t& value) noexcept {
        if (bytes_.size() - position_ < 4) {
            return false;
        }
        value = (static_cast<std::uint32_t>(bytes_[position_]) << 24) |
                (static_cast<std::uint32_t>(bytes_[position_ + 1]) << 16) |
                (static_cast<std::uint32_t>(bytes_[position_ + 2]) << 8) |
                static_cast<std::uint32_t>(bytes_[position_ + 3]);
        position_ += 4;
        return true;
    }

    bool u64(std::uint64_t& value) noexcept {
        if (bytes_.size() - position_ < 8) {
            return false;
        }
        value = 0;
        for (int shift = 56; shift >= 0; shift -= 8) {
            value |= static_cast<std::uint64_t>(bytes_[position_++]) << shift;
        }
        return true;
    }

    bool string(std::string& value) noexcept {
        std::uint32_t length = 0;
        if (!u32(length) || length > bytes_.size() - position_) {
            return false;
        }
        value.assign(reinterpret_cast<const char*>(bytes_.data() + position_), length);
        position_ += length;
        return valid_utf8(value);
    }

    bool optional_u8(std::uint8_t& value, bool& present) noexcept {
        std::uint8_t flag = 0;
        if (!u8(flag) || flag > 1) {
            return false;
        }
        present = flag == 1;
        return !present || u8(value);
    }

    bool optional_u32(std::uint32_t& value, bool& present) noexcept {
        std::uint8_t flag = 0;
        if (!u8(flag) || flag > 1) {
            return false;
        }
        present = flag == 1;
        return !present || u32(value);
    }

    bool optional_u64(std::uint64_t& value, bool& present) noexcept {
        std::uint8_t flag = 0;
        if (!u8(flag) || flag > 1) {
            return false;
        }
        present = flag == 1;
        return !present || u64(value);
    }

    bool optional_i32(std::int32_t& value, bool& present) noexcept {
        std::uint32_t bits = 0;
        if (!optional_u32(bits, present)) {
            return false;
        }
        value = static_cast<std::int32_t>(bits);
        return true;
    }

    bool boolean(bool& value) noexcept {
        std::uint8_t encoded = 0;
        if (!u8(encoded) || encoded > 1) {
            return false;
        }
        value = encoded == 1;
        return true;
    }

    std::size_t remaining() const noexcept { return bytes_.size() - position_; }
    bool end() const noexcept { return position_ == bytes_.size(); }

private:
    const std::vector<std::uint8_t>& bytes_;
    std::size_t position_ = 0;
};

bool decode_zone(const std::uint8_t code, SemanticZone& value) noexcept {
    switch (code) {
    case 0:
        value = SemanticZone::Unknown;
        return true;
    case 1:
        value = SemanticZone::MainDeck;
        return true;
    case 2:
        value = SemanticZone::Hand;
        return true;
    case 3:
        value = SemanticZone::MonsterZone;
        return true;
    case 4:
        value = SemanticZone::SpellTrapZone;
        return true;
    case 5:
        value = SemanticZone::Graveyard;
        return true;
    case 6:
        value = SemanticZone::Banished;
        return true;
    case 7:
        value = SemanticZone::ExtraDeck;
        return true;
    case 8:
        value = SemanticZone::FieldZone;
        return true;
    case 9:
        value = SemanticZone::PendulumRelevant;
        return true;
    case 10:
        value = SemanticZone::Overlay;
        return true;
    default:
        return false;
    }
}

bool decode_position(const std::uint8_t code, ygo::observation::Position& value) noexcept {
    switch (code) {
    case 0:
        value = ygo::observation::Position::Unknown;
        return true;
    case 1:
        value = ygo::observation::Position::FaceUpAttack;
        return true;
    case 2:
        value = ygo::observation::Position::FaceDownAttack;
        return true;
    case 4:
        value = ygo::observation::Position::FaceUpDefense;
        return true;
    case 8:
        value = ygo::observation::Position::FaceDownDefense;
        return true;
    default:
        return false;
    }
}

bool decode_link_marker(const std::uint8_t code, LinkMarker& value) noexcept {
    switch (code) {
    case 0:
        value = LinkMarker::BottomLeft;
        return true;
    case 1:
        value = LinkMarker::Bottom;
        return true;
    case 2:
        value = LinkMarker::BottomRight;
        return true;
    case 3:
        value = LinkMarker::Left;
        return true;
    case 4:
        value = LinkMarker::Right;
        return true;
    case 5:
        value = LinkMarker::TopLeft;
        return true;
    case 6:
        value = LinkMarker::Top;
        return true;
    case 7:
        value = LinkMarker::TopRight;
        return true;
    default:
        return false;
    }
}

bool decode_relationship_kind(const std::uint8_t code, RelationshipKind& value) noexcept {
    switch (code) {
    case 0:
        value = RelationshipKind::XyzMaterial;
        return true;
    case 1:
        value = RelationshipKind::Equip;
        return true;
    case 2:
        value = RelationshipKind::Target;
        return true;
    default:
        return false;
    }
}

bool decode_visible_event_kind(const std::uint8_t code, VisibleEventKind& value) noexcept {
    switch (code) {
    case 0:
        value = VisibleEventKind::Unknown;
        return true;
    case 1:
        value = VisibleEventKind::TurnStarted;
        return true;
    case 2:
        value = VisibleEventKind::PhaseChanged;
        return true;
    case 3:
        value = VisibleEventKind::CardMoved;
        return true;
    case 4:
        value = VisibleEventKind::CardRevealed;
        return true;
    case 5:
        value = VisibleEventKind::Summoned;
        return true;
    case 6:
        value = VisibleEventKind::Set;
        return true;
    case 7:
        value = VisibleEventKind::Draw;
        return true;
    case 8:
        value = VisibleEventKind::Shuffle;
        return true;
    case 9:
        value = VisibleEventKind::RandomizationBoundary;
        return true;
    case 10:
        value = VisibleEventKind::LifePointsChanged;
        return true;
    case 11:
        value = VisibleEventKind::ChainActivated;
        return true;
    case 12:
        value = VisibleEventKind::ChainResolved;
        return true;
    case 13:
        value = VisibleEventKind::ChainEnded;
        return true;
    case 14:
        value = VisibleEventKind::CardDestroyed;
        return true;
    case 15:
        value = VisibleEventKind::CardBanished;
        return true;
    case 16:
        value = VisibleEventKind::CardReturned;
        return true;
    case 17:
        value = VisibleEventKind::PositionChanged;
        return true;
    case 18:
        value = VisibleEventKind::CounterChanged;
        return true;
    case 19:
        value = VisibleEventKind::Equipped;
        return true;
    case 20:
        value = VisibleEventKind::Unequipped;
        return true;
    case 21:
        value = VisibleEventKind::Targeted;
        return true;
    case 22:
        value = VisibleEventKind::Win;
        return true;
    default:
        return false;
    }
}

bool read_optional_player(Cursor& cursor, std::optional<std::uint8_t>& value) noexcept {
    std::uint8_t decoded = 0;
    bool present = false;
    if (!cursor.optional_u8(decoded, present) || (present && decoded > 1)) {
        return false;
    }
    if (present) {
        value = decoded;
    } else {
        value.reset();
    }
    return true;
}

bool read_optional_u32(Cursor& cursor, std::optional<std::uint32_t>& value) noexcept {
    std::uint32_t decoded = 0;
    bool present = false;
    if (!cursor.optional_u32(decoded, present)) {
        return false;
    }
    if (present) {
        value = decoded;
    } else {
        value.reset();
    }
    return true;
}

bool read_optional_u64(Cursor& cursor, std::optional<std::uint64_t>& value) noexcept {
    std::uint64_t decoded = 0;
    bool present = false;
    if (!cursor.optional_u64(decoded, present)) {
        return false;
    }
    if (present) {
        value = decoded;
    } else {
        value.reset();
    }
    return true;
}

bool read_optional_i32(Cursor& cursor, std::optional<std::int32_t>& value) noexcept {
    std::int32_t decoded = 0;
    bool present = false;
    if (!cursor.optional_i32(decoded, present)) {
        return false;
    }
    if (present) {
        value = decoded;
    } else {
        value.reset();
    }
    return true;
}

bool read_optional_locator(Cursor& cursor,
                           std::optional<ObservationLocator>& value) noexcept {
    std::uint8_t flag = 0;
    if (!cursor.u8(flag) || flag > 1) {
        return false;
    }
    if (flag == 0) {
        value.reset();
        return true;
    }
    std::string decoded;
    if (!cursor.string(decoded) || !is_locator(decoded)) {
        return false;
    }
    value = ObservationLocator{std::move(decoded)};
    return true;
}

bool read_optional_zone(Cursor& cursor, std::optional<SemanticZone>& value) noexcept {
    std::uint8_t flag = 0;
    if (!cursor.u8(flag) || flag > 1) {
        return false;
    }
    if (flag == 0) {
        value.reset();
        return true;
    }
    std::uint8_t code = 0;
    SemanticZone decoded = SemanticZone::Unknown;
    if (!cursor.u8(code) || !decode_zone(code, decoded)) {
        return false;
    }
    value = decoded;
    return true;
}

bool read_properties(Cursor& cursor, std::optional<CardProperties>& value) noexcept {
    std::uint8_t flag = 0;
    if (!cursor.u8(flag) || flag > 1) {
        return false;
    }
    if (flag == 0) {
        value.reset();
        return true;
    }

    CardProperties properties;
    if (!read_optional_u32(cursor, properties.type) ||
        !read_optional_u32(cursor, properties.attribute) ||
        !read_optional_u64(cursor, properties.race) ||
        !read_optional_i32(cursor, properties.attack) ||
        !read_optional_i32(cursor, properties.defense) ||
        !read_optional_i32(cursor, properties.base_attack) ||
        !read_optional_i32(cursor, properties.base_defense) ||
        !read_optional_u32(cursor, properties.level) ||
        !read_optional_u32(cursor, properties.rank) ||
        !read_optional_u32(cursor, properties.link_rating)) {
        return false;
    }

    std::uint32_t marker_count = 0;
    if (!cursor.u32(marker_count) || marker_count > cursor.remaining()) {
        return false;
    }
    std::uint8_t previous_marker = 0;
    bool have_marker = false;
    properties.link_markers.reserve(marker_count);
    for (std::uint32_t index = 0; index < marker_count; ++index) {
        std::uint8_t code = 0;
        LinkMarker marker = LinkMarker::BottomLeft;
        if (!cursor.u8(code) || !decode_link_marker(code, marker) ||
            (have_marker && code < previous_marker)) {
            return false;
        }
        previous_marker = code;
        have_marker = true;
        properties.link_markers.push_back(marker);
    }

    if (!read_optional_u32(cursor, properties.left_scale) ||
        !read_optional_u32(cursor, properties.right_scale) ||
        !read_optional_u32(cursor, properties.status_flags)) {
        return false;
    }

    std::uint32_t counter_count = 0;
    if (!cursor.u32(counter_count) || counter_count > cursor.remaining() / 8) {
        return false;
    }
    std::uint32_t previous_type = 0;
    std::uint32_t previous_count = 0;
    bool have_counter = false;
    properties.counters.reserve(counter_count);
    for (std::uint32_t index = 0; index < counter_count; ++index) {
        Counter counter;
        if (!cursor.u32(counter.type) || !cursor.u32(counter.count) ||
            (have_counter && std::tie(counter.type, counter.count) <
                                  std::tie(previous_type, previous_count))) {
            return false;
        }
        previous_type = counter.type;
        previous_count = counter.count;
        have_counter = true;
        properties.counters.push_back(counter);
    }
    value = std::move(properties);
    return true;
}

bool read_sorted_targets(Cursor& cursor, std::vector<ObservationLocator>& targets) noexcept {
    std::uint32_t count = 0;
    if (!cursor.u32(count) || count > cursor.remaining() / 4) {
        return false;
    }
    std::string previous;
    bool have_previous = false;
    targets.clear();
    targets.reserve(count);
    for (std::uint32_t index = 0; index < count; ++index) {
        std::string target;
        if (!cursor.string(target) || !is_locator(target) ||
            (have_previous && target < previous)) {
            return false;
        }
        previous = target;
        have_previous = true;
        targets.push_back({std::move(target)});
    }
    return true;
}

struct ParsedSafeState final {
    ObservedPlayerGlobals globals;
    std::vector<ObservedZone> zones;
    std::vector<ObservedCard> entities;
    std::vector<Relationship> relationships;
    ygo::observation::ChainState chain;
    std::vector<PublicSafeVisibleEvent> visible_events;
    ygo::observation::MatchContext match_context;
};

bool read_safe_state(const std::vector<std::uint8_t>& bytes, ParsedSafeState& output) noexcept {
    Cursor cursor(bytes);
    std::string schema;
    if (!cursor.string(schema) || schema != kPublicSafeStateSchemaId ||
        !cursor.string(schema) || schema != kPublicSafeStateSchemaId) {
        return false;
    }

    if (!cursor.u64(output.globals.duel_flags)) {
        return false;
    }
    std::uint32_t count = 0;
    if (!cursor.u32(count) || count > cursor.remaining() / 4) {
        return false;
    }
    output.globals.life_points.clear();
    output.globals.life_points.reserve(count);
    for (std::uint32_t index = 0; index < count; ++index) {
        std::uint32_t life_points = 0;
        if (!cursor.u32(life_points)) {
            return false;
        }
        output.globals.life_points.push_back(life_points);
    }
    if (!read_optional_player(cursor, output.globals.player_to_act) ||
        !read_optional_player(cursor, output.globals.turn_player) ||
        !read_optional_u32(cursor, output.globals.turn_count) ||
        !read_optional_u32(cursor, output.globals.phase) ||
        !cursor.u32(output.globals.chain_length) ||
        !read_optional_player(cursor, output.globals.winner) ||
        !read_optional_player(cursor, output.globals.win_reason) ||
        !cursor.boolean(output.globals.terminal)) {
        return false;
    }

    if (!cursor.u32(count) || count > cursor.remaining() / 15) {
        return false;
    }
    output.zones.clear();
    output.zones.reserve(count);
    std::tuple<std::uint8_t, std::uint8_t, std::uint32_t, std::uint32_t, std::uint32_t, bool>
        previous_zone{};
    bool have_zone = false;
    for (std::uint32_t index = 0; index < count; ++index) {
        ObservedZone zone;
        std::uint8_t kind_code = 0;
        if (!cursor.u8(zone.player) || !cursor.u8(kind_code) ||
            !cursor.u32(zone.total_count) || !cursor.u32(zone.public_identity_count) ||
            !cursor.u32(zone.hidden_count) || !cursor.boolean(zone.player_observable_order) ||
            zone.player > 1 || !decode_zone(kind_code, zone.kind)) {
            return false;
        }
        const auto current = std::make_tuple(zone.player, kind_code, zone.total_count,
                                             zone.public_identity_count, zone.hidden_count,
                                             zone.player_observable_order);
        if (have_zone && current < previous_zone) {
            return false;
        }
        previous_zone = current;
        have_zone = true;
        output.zones.push_back(zone);
    }

    if (!cursor.u32(count) || count > cursor.remaining() / 16) {
        return false;
    }
    output.entities.clear();
    output.entities.reserve(count);
    std::string previous_locator;
    bool have_locator = false;
    for (std::uint32_t index = 0; index < count; ++index) {
        ObservedCard entity;
        std::string locator;
        std::uint8_t zone_code = 0;
        std::uint8_t position_code_value = 0;
        if (!cursor.string(locator) || !is_locator(locator) ||
            (have_locator && locator <= previous_locator)) {
            return false;
        }
        entity.locator = {std::move(locator)};
        previous_locator = entity.locator.value;
        have_locator = true;
        if (!cursor.boolean(entity.identity_known) ||
            !read_optional_u32(cursor, entity.passcode) ||
            !read_optional_player(cursor, entity.owner) ||
            !read_optional_player(cursor, entity.controller) || !cursor.u8(zone_code) ||
            !decode_zone(zone_code, entity.zone) ||
            !read_optional_u32(cursor, entity.sequence) ||
            !read_optional_u32(cursor, entity.overlay_sequence) ||
            !cursor.u8(position_code_value) ||
            !decode_position(position_code_value, entity.position) ||
            !cursor.boolean(entity.face_up) || !cursor.boolean(entity.face_down) ||
            (entity.face_up && entity.face_down) || !read_properties(cursor, entity.printed) ||
            !read_properties(cursor, entity.current)) {
            return false;
        }
        if (!entity.identity_known &&
            (entity.passcode.has_value() || entity.printed.has_value() ||
             entity.current.has_value())) {
            return false;
        }
        output.entities.push_back(std::move(entity));
    }

    if (!cursor.u32(count) || count > cursor.remaining() / 9) {
        return false;
    }
    output.relationships.clear();
    output.relationships.reserve(count);
    std::tuple<std::uint8_t, std::string, std::string> previous_relationship;
    bool have_relationship = false;
    for (std::uint32_t index = 0; index < count; ++index) {
        Relationship relationship;
        std::uint8_t kind_code = 0;
        if (!cursor.u8(kind_code) || !decode_relationship_kind(kind_code, relationship.kind) ||
            !cursor.string(relationship.source.value) || !is_locator(relationship.source.value) ||
            !cursor.string(relationship.target.value) || !is_locator(relationship.target.value)) {
            return false;
        }
        const auto current = std::make_tuple(kind_code, relationship.source.value,
                                             relationship.target.value);
        if (have_relationship && current < previous_relationship) {
            return false;
        }
        previous_relationship = current;
        have_relationship = true;
        output.relationships.push_back(std::move(relationship));
    }

    if (!cursor.u32(output.chain.length) || !cursor.u32(count) ||
        count > cursor.remaining() / 12) {
        return false;
    }
    output.chain.links.clear();
    output.chain.links.reserve(count);
    for (std::uint32_t index = 0; index < count; ++index) {
        ChainLink link;
        if (!cursor.u32(link.index) || !read_optional_player(cursor, link.activating_player) ||
            !read_optional_locator(cursor, link.source) ||
            !read_optional_zone(cursor, link.activation_zone) ||
            !read_optional_u64(cursor, link.effect_description) ||
            !read_sorted_targets(cursor, link.targets)) {
            return false;
        }
        output.chain.links.push_back(std::move(link));
    }

    if (!cursor.u32(count) || count > cursor.remaining() / 25) {
        return false;
    }
    output.visible_events.clear();
    output.visible_events.reserve(count);
    std::uint64_t previous_event_index = 0;
    bool have_event = false;
    for (std::uint32_t index = 0; index < count; ++index) {
        PublicSafeVisibleEvent event;
        std::uint8_t kind_code = 0;
        if (!cursor.u64(event.event_index) || !cursor.u8(kind_code) ||
            !decode_visible_event_kind(kind_code, event.kind) ||
            (have_event && event.event_index <= previous_event_index) ||
            !read_optional_player(cursor, event.player) ||
            !read_optional_locator(cursor, event.entity) ||
            !read_optional_u32(cursor, event.public_passcode) ||
            !read_optional_zone(cursor, event.from_zone) ||
            !read_optional_zone(cursor, event.to_zone) ||
            !read_optional_u32(cursor, event.count) || !read_optional_i32(cursor, event.amount) ||
            !read_optional_u32(cursor, event.counter_type) ||
            !read_optional_u32(cursor, event.phase) ||
            !read_optional_player(cursor, event.winner) ||
            !read_optional_player(cursor, event.win_reason) ||
            !read_optional_u64(cursor, event.effect_description) ||
            !read_sorted_targets(cursor, event.targets)) {
            return false;
        }
        previous_event_index = event.event_index;
        have_event = true;
        output.visible_events.push_back(std::move(event));
    }

    if (!cursor.u8(output.match_context.perspective_player) ||
        output.match_context.perspective_player > 1 ||
        !cursor.u64(output.match_context.duel_flags) ||
        !cursor.boolean(output.match_context.knowledge.own_decklist_known) ||
        !cursor.boolean(output.match_context.knowledge.opponent_decklist_known)) {
        return false;
    }

    auto read_deck = [&cursor](ygo::observation::StaticDeckContext& deck) noexcept {
        std::uint32_t count = 0;
        if (!cursor.boolean(deck.known) || !cursor.u32(count) ||
            count > cursor.remaining() / 4) {
            return false;
        }
        deck.main_deck.clear();
        deck.main_deck.reserve(count);
        std::uint32_t previous_code = 0;
        bool have_code = false;
        for (std::uint32_t index = 0; index < count; ++index) {
            std::uint32_t code = 0;
            if (!cursor.u32(code) || (have_code && code < previous_code)) {
                return false;
            }
            previous_code = code;
            have_code = true;
            deck.main_deck.push_back(code);
        }
        if (!cursor.u32(count) || count > cursor.remaining() / 4) {
            return false;
        }
        deck.extra_deck.clear();
        deck.extra_deck.reserve(count);
        previous_code = 0;
        have_code = false;
        for (std::uint32_t index = 0; index < count; ++index) {
            std::uint32_t code = 0;
            if (!cursor.u32(code) || (have_code && code < previous_code)) {
                return false;
            }
            previous_code = code;
            have_code = true;
            deck.extra_deck.push_back(code);
        }
        if (!deck.known && (!deck.main_deck.empty() || !deck.extra_deck.empty())) {
            return false;
        }
        return true;
    };

    if (!read_deck(output.match_context.own_deck) ||
        !read_deck(output.match_context.opponent_deck) || !cursor.end()) {
        return false;
    }
    return true;
}

}  // namespace

std::vector<std::uint8_t> canonical_public_safe_state_bytes(
    const PublicSafeStateView& view) {
    return encode_view(view);
}

std::vector<std::uint8_t> canonical_public_safe_state_bytes(
    const ygo::observation::PlayerObservation& observation) {
    validate_observation(observation);

    PublicSafeStateView view;
    view.globals_ = observation.globals;
    view.zones_ = observation.zones;
    view.entities_ = observation.entities;
    view.relationships_ = observation.relationships;
    view.chain_ = observation.chain;
    view.visible_events_.reserve(observation.visible_events.size());
    for (const auto& source : observation.visible_events) {
        PublicSafeVisibleEvent event;
        event.event_index = source.event_index;
        event.kind = source.kind;
        event.player = source.player;
        event.entity = source.entity;
        event.public_passcode = source.public_passcode;
        event.from_zone = source.from_zone;
        event.to_zone = source.to_zone;
        event.count = source.count;
        event.amount = source.amount;
        event.counter_type = source.counter_type;
        event.phase = source.phase;
        event.winner = source.winner;
        event.win_reason = source.win_reason;
        event.effect_description = source.effect_description;
        event.targets = source.targets;
        view.visible_events_.push_back(std::move(event));
    }
    view.match_context_ = observation.match_context;
    return encode_view(view);
}

PublicSafeStateDecodeResult decode_canonical_public_safe_state(
    const std::vector<std::uint8_t>& bytes) noexcept {
    try {
        ParsedSafeState parsed;
        if (!read_safe_state(bytes, parsed)) {
            return {std::nullopt,
                    std::string("public safe state is malformed or noncanonical")};
        }

        PublicSafeStateView view;
        view.globals_ = std::move(parsed.globals);
        view.zones_ = std::move(parsed.zones);
        view.entities_ = std::move(parsed.entities);
        view.relationships_ = std::move(parsed.relationships);
        view.chain_ = std::move(parsed.chain);
        view.visible_events_ = std::move(parsed.visible_events);
        view.match_context_ = std::move(parsed.match_context);
        if (encode_view(view) != bytes) {
            return {std::nullopt,
                    std::string("public safe state is not in canonical encoding")};
        }
        return {std::optional<PublicSafeStateView>(std::move(view)), std::nullopt};
    } catch (const std::exception& error) {
        return {std::nullopt, std::string(error.what())};
    } catch (...) {
        return {std::nullopt, std::string("public safe state decode failed")};
    }
}

}  // namespace ygo::environment
