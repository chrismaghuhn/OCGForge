#include "ygo/observation/serialization.hpp"

#include <algorithm>
#include <cstdint>
#include <sstream>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "ygo/trace/sha256.hpp"

namespace ygo::observation {
namespace {

std::string json_escape(const std::string& value) {
    std::ostringstream out;
    out << '"';
    for (const unsigned char character : value) {
        switch (character) {
        case '"':
            out << "\\\"";
            break;
        case '\\':
            out << "\\\\";
            break;
        case '\n':
            out << "\\n";
            break;
        case '\r':
            out << "\\r";
            break;
        case '\t':
            out << "\\t";
            break;
        default:
            if (character < 0x20) {
                const char* hex = "0123456789abcdef";
                out << "\\u00" << hex[character >> 4] << hex[character & 0xf];
            } else {
                out << static_cast<char>(character);
            }
            break;
        }
    }
    out << '"';
    return out.str();
}

template <typename T>
void write_optional_number(std::ostringstream& out, const std::optional<T>& value) {
    if (value.has_value()) {
        out << *value;
    } else {
        out << "null";
    }
}

void write_optional_string(std::ostringstream& out, const std::optional<std::string>& value) {
    if (value.has_value()) {
        out << json_escape(*value);
    } else {
        out << "null";
    }
}

void write_optional_locator(std::ostringstream& out, const std::optional<ObservationLocator>& value) {
    if (value.has_value()) {
        out << json_escape(value->value);
    } else {
        out << "null";
    }
}

void write_optional_zone(std::ostringstream& out, const std::optional<SemanticZone>& value) {
    if (value.has_value()) {
        out << json_escape(semantic_zone_name(*value));
    } else {
        out << "null";
    }
}

void write_optional_u8(std::ostringstream& out, const std::optional<std::uint8_t>& value) {
    if (value.has_value()) {
        out << static_cast<unsigned>(*value);
    } else {
        out << "null";
    }
}

template <typename T, typename Writer>
void write_array(std::ostringstream& out, const std::vector<T>& values, Writer writer) {
    out << '[';
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index != 0) {
            out << ',';
        }
        writer(out, values[index]);
    }
    out << ']';
}

void write_counters(std::ostringstream& out, const std::vector<Counter>& input) {
    auto values = input;
    std::sort(values.begin(), values.end(), [](const Counter& left, const Counter& right) {
        return std::tie(left.type, left.count) < std::tie(right.type, right.count);
    });
    write_array(out, values, [](std::ostringstream& stream, const Counter& counter) {
        stream << "{\"type\":" << counter.type << ",\"count\":" << counter.count << '}';
    });
}

void write_properties(std::ostringstream& out, const std::optional<CardProperties>& properties) {
    if (!properties.has_value()) {
        out << "null";
        return;
    }
    const auto& value = *properties;
    auto markers = value.link_markers;
    std::sort(markers.begin(), markers.end(), [](LinkMarker left, LinkMarker right) {
        return static_cast<unsigned>(left) < static_cast<unsigned>(right);
    });
    out << "{\"type\":";
    write_optional_number(out, value.type);
    out << ",\"attribute\":";
    write_optional_number(out, value.attribute);
    out << ",\"race\":";
    write_optional_number(out, value.race);
    out << ",\"attack\":";
    write_optional_number(out, value.attack);
    out << ",\"defense\":";
    write_optional_number(out, value.defense);
    out << ",\"base_attack\":";
    write_optional_number(out, value.base_attack);
    out << ",\"base_defense\":";
    write_optional_number(out, value.base_defense);
    out << ",\"level\":";
    write_optional_number(out, value.level);
    out << ",\"rank\":";
    write_optional_number(out, value.rank);
    out << ",\"link_rating\":";
    write_optional_number(out, value.link_rating);
    out << ",\"link_markers\":";
    write_array(out, markers, [](std::ostringstream& stream, LinkMarker marker) {
        stream << json_escape(link_marker_name(marker));
    });
    out << ",\"left_scale\":";
    write_optional_number(out, value.left_scale);
    out << ",\"right_scale\":";
    write_optional_number(out, value.right_scale);
    out << ",\"status_flags\":";
    write_optional_number(out, value.status_flags);
    out << ",\"counters\":";
    write_counters(out, value.counters);
    out << '}';
}

void write_card(std::ostringstream& out, const ObservedCard& card) {
    out << "{\"locator\":" << json_escape(card.locator.value)
        << ",\"identity_known\":" << (card.identity_known ? "true" : "false")
        << ",\"passcode\":";
    write_optional_number(out, card.passcode);
    out << ",\"owner\":";
    write_optional_u8(out, card.owner);
    out << ",\"controller\":";
    write_optional_u8(out, card.controller);
    out << ",\"zone\":" << json_escape(semantic_zone_name(card.zone)) << ",\"sequence\":";
    write_optional_number(out, card.sequence);
    out << ",\"overlay_sequence\":";
    write_optional_number(out, card.overlay_sequence);
    out << ",\"position\":" << json_escape(position_name(card.position))
        << ",\"face_up\":" << (card.face_up ? "true" : "false")
        << ",\"face_down\":" << (card.face_down ? "true" : "false")
        << ",\"printed\":";
    write_properties(out, card.printed);
    out << ",\"current\":";
    write_properties(out, card.current);
    out << '}';
}

void write_event(std::ostringstream& out, const VisibleGameEvent& event) {
    auto targets = event.targets;
    std::sort(targets.begin(), targets.end());
    out << "{\"event_index\":" << event.event_index << ",\"engine_step_index\":"
        << event.engine_step_index << ",\"kind\":" << json_escape(visible_event_kind_name(event.kind))
        << ",\"player\":";
    write_optional_u8(out, event.player);
    out << ",\"entity\":";
    write_optional_locator(out, event.entity);
    out << ",\"public_passcode\":";
    write_optional_number(out, event.public_passcode);
    out << ",\"from_zone\":";
    write_optional_zone(out, event.from_zone);
    out << ",\"to_zone\":";
    write_optional_zone(out, event.to_zone);
    out << ",\"count\":";
    write_optional_number(out, event.count);
    out << ",\"amount\":";
    write_optional_number(out, event.amount);
    out << ",\"counter_type\":";
    write_optional_number(out, event.counter_type);
    out << ",\"phase\":";
    write_optional_number(out, event.phase);
    out << ",\"winner\":";
    write_optional_u8(out, event.winner);
    out << ",\"win_reason\":";
    write_optional_u8(out, event.win_reason);
    out << ",\"effect_description\":";
    write_optional_number(out, event.effect_description);
    out << ",\"targets\":";
    write_array(out, targets, [](std::ostringstream& stream, const ObservationLocator& locator) {
        stream << json_escape(locator.value);
    });
    out << '}';
}

void write_match_context(std::ostringstream& out, const MatchContext& context) {
    const auto write_deck = [&out](const StaticDeckContext& deck) {
        auto main = deck.main_deck;
        auto extra = deck.extra_deck;
        std::sort(main.begin(), main.end());
        std::sort(extra.begin(), extra.end());
        out << "{\"known\":" << (deck.known ? "true" : "false") << ",\"main_deck\":";
        write_array(out, main, [](std::ostringstream& stream, std::uint32_t code) { stream << code; });
        out << ",\"extra_deck\":";
        write_array(out, extra, [](std::ostringstream& stream, std::uint32_t code) { stream << code; });
        out << '}';
    };
    out << "{\"perspective_player\":" << static_cast<unsigned>(context.perspective_player)
        << ",\"duel_flags\":" << context.duel_flags << ",\"knowledge\":{\"own_decklist_known\":"
        << (context.knowledge.own_decklist_known ? "true" : "false")
        << ",\"opponent_decklist_known\":"
        << (context.knowledge.opponent_decklist_known ? "true" : "false") << "},\"own_deck\":";
    write_deck(context.own_deck);
    out << ",\"opponent_deck\":";
    write_deck(context.opponent_deck);
    out << '}';
}

std::string serialize_without_hash(
    const PlayerObservation& observation
#ifdef YGO_M4_PERFORMANCE_AUDIT
    , PerformanceAuditCollector* audit
#endif
) {
    auto zones = observation.zones;
#ifdef YGO_M4_PERFORMANCE_AUDIT
    if (audit != nullptr) {
        audit->record_copy_event();
    }
#endif
    std::sort(zones.begin(), zones.end(), [](const ObservedZone& left, const ObservedZone& right) {
        return std::tie(left.player, left.kind, left.total_count, left.public_identity_count,
                        left.hidden_count) <
               std::tie(right.player, right.kind, right.total_count, right.public_identity_count,
                        right.hidden_count);
    });
    auto entities = observation.entities;
#ifdef YGO_M4_PERFORMANCE_AUDIT
    if (audit != nullptr) {
        audit->record_copy_event();
    }
#endif
    std::sort(entities.begin(), entities.end(), [](const ObservedCard& left, const ObservedCard& right) {
        return left.locator.value < right.locator.value;
    });
    auto relationships = observation.relationships;
#ifdef YGO_M4_PERFORMANCE_AUDIT
    if (audit != nullptr) {
        audit->record_copy_event();
    }
#endif
    std::sort(relationships.begin(), relationships.end(), [](const Relationship& left, const Relationship& right) {
        return std::tie(left.kind, left.source.value, left.target.value) <
               std::tie(right.kind, right.source.value, right.target.value);
    });
    auto events = observation.visible_events;
#ifdef YGO_M4_PERFORMANCE_AUDIT
    if (audit != nullptr) {
        audit->record_copy_event();
    }
#endif
    std::sort(events.begin(), events.end(), [](const VisibleGameEvent& left, const VisibleGameEvent& right) {
        return std::tie(left.event_index, left.engine_step_index) <
               std::tie(right.event_index, right.engine_step_index);
    });

    std::ostringstream out;
    out << "{\"schema_version\":" << json_escape(observation.schema_version)
        << ",\"perspective_player\":" << static_cast<unsigned>(observation.perspective_player)
        << ",\"decision_index\":" << observation.decision_index
        << ",\"engine_step_index\":" << observation.engine_step_index << ",\"globals\":{";
    out << "\"duel_flags\":" << observation.globals.duel_flags << ",\"life_points\":";
    write_array(out, observation.globals.life_points,
                [](std::ostringstream& stream, std::uint32_t value) { stream << value; });
    out << ",\"player_to_act\":";
    write_optional_u8(out, observation.globals.player_to_act);
    out << ",\"turn_player\":";
    write_optional_u8(out, observation.globals.turn_player);
    out << ",\"turn_count\":";
    write_optional_number(out, observation.globals.turn_count);
    out << ",\"phase\":";
    write_optional_number(out, observation.globals.phase);
    out << ",\"chain_length\":" << observation.globals.chain_length << ",\"winner\":";
    write_optional_u8(out, observation.globals.winner);
    out << ",\"win_reason\":";
    write_optional_u8(out, observation.globals.win_reason);
    out << ",\"terminal\":" << (observation.globals.terminal ? "true" : "false") << "},\"zones\":";
    write_array(out, zones, [](std::ostringstream& stream, const ObservedZone& zone) {
        stream << "{\"player\":" << static_cast<unsigned>(zone.player) << ",\"kind\":"
               << json_escape(semantic_zone_name(zone.kind)) << ",\"total_count\":" << zone.total_count
               << ",\"public_identity_count\":" << zone.public_identity_count
               << ",\"hidden_count\":" << zone.hidden_count
               << ",\"player_observable_order\":"
               << (zone.player_observable_order ? "true" : "false") << '}';
    });
    out << ",\"entities\":";
    write_array(out, entities, [](std::ostringstream& stream, const ObservedCard& card) {
        write_card(stream, card);
    });
    out << ",\"relationships\":";
    write_array(out, relationships, [](std::ostringstream& stream, const Relationship& relationship) {
        stream << "{\"kind\":" << json_escape(relationship_kind_name(relationship.kind))
               << ",\"source\":" << json_escape(relationship.source.value)
               << ",\"target\":" << json_escape(relationship.target.value) << '}';
    });
    out << ",\"chain\":{\"length\":" << observation.chain.length << ",\"links\":";
    write_array(out, observation.chain.links,
#ifdef YGO_M4_PERFORMANCE_AUDIT
                [audit](std::ostringstream& stream, const ChainLink& link) {
#else
                [](std::ostringstream& stream, const ChainLink& link) {
#endif
        auto targets = link.targets;
#ifdef YGO_M4_PERFORMANCE_AUDIT
        if (audit != nullptr) {
            audit->record_copy_event();
        }
#endif
        std::sort(targets.begin(), targets.end());
        stream << "{\"index\":" << link.index << ",\"activating_player\":";
        write_optional_u8(stream, link.activating_player);
        stream << ",\"source\":";
        write_optional_locator(stream, link.source);
        stream << ",\"activation_zone\":";
        write_optional_zone(stream, link.activation_zone);
        stream << ",\"effect_description\":";
        write_optional_number(stream, link.effect_description);
        stream << ",\"targets\":";
        write_array(stream, targets, [](std::ostringstream& target_stream, const ObservationLocator& locator) {
            target_stream << json_escape(locator.value);
        });
        stream << '}';
    });
    out << "},\"visible_events\":";
    write_array(out, events,
#ifdef YGO_M4_PERFORMANCE_AUDIT
                [audit](std::ostringstream& stream, const VisibleGameEvent& event) {
                    if (audit != nullptr) {
                        audit->record_copy_event();
                    }
#else
                [](std::ostringstream& stream, const VisibleGameEvent& event) {
#endif
        write_event(stream, event);
    });
    out << ",\"decision_context\":{";
    out << "\"decision_id\":";
    write_optional_string(out, observation.decision_context.decision_id);
    out << ",\"kind\":";
    write_optional_string(out, observation.decision_context.kind);
    out << ",\"engine_step_index\":";
    write_optional_number(out, observation.decision_context.engine_step_index);
    out << ",\"player\":";
    write_optional_u8(out, observation.decision_context.player);
    out << ",\"engine_message_type\":";
    write_optional_u8(out, observation.decision_context.engine_message_type);
    out << ",\"engine_message_name\":";
    write_optional_string(out, observation.decision_context.engine_message_name);
    out << ",\"continuation_id\":";
    write_optional_string(out, observation.decision_context.continuation_id);
    out << ",\"referenced_entities\":";
    auto references = observation.decision_context.referenced_entities;
#ifdef YGO_M4_PERFORMANCE_AUDIT
    if (audit != nullptr) {
        audit->record_copy_event();
    }
#endif
    std::sort(references.begin(), references.end());
    write_array(out, references, [](std::ostringstream& stream, const ObservationLocator& locator) {
        stream << json_escape(locator.value);
    });
    out << "},\"match_context\":";
    write_match_context(out, observation.match_context);
    out << '}';
#ifdef YGO_M4_PERFORMANCE_AUDIT
    auto serialized = out.str();
    if (audit != nullptr) {
        audit->record_serialize_without_hash(serialized.size());
    }
    return serialized;
#else
    return out.str();
#endif
}

}  // namespace

std::string semantic_zone_name(SemanticZone zone) {
    switch (zone) {
    case SemanticZone::MainDeck:
        return "MAIN_DECK";
    case SemanticZone::Hand:
        return "HAND";
    case SemanticZone::MonsterZone:
        return "MONSTER_ZONE";
    case SemanticZone::SpellTrapZone:
        return "SPELL_TRAP_ZONE";
    case SemanticZone::Graveyard:
        return "GRAVEYARD";
    case SemanticZone::Banished:
        return "BANISHED";
    case SemanticZone::ExtraDeck:
        return "EXTRA_DECK";
    case SemanticZone::FieldZone:
        return "FIELD_ZONE";
    case SemanticZone::PendulumRelevant:
        return "PENDULUM_RELEVANT_STATE";
    case SemanticZone::Overlay:
        return "OVERLAY";
    case SemanticZone::Unknown:
        return "UNKNOWN";
    }
    return "UNKNOWN";
}

std::string position_name(Position position) {
    switch (position) {
    case Position::FaceUpAttack:
        return "FACE_UP_ATTACK";
    case Position::FaceDownAttack:
        return "FACE_DOWN_ATTACK";
    case Position::FaceUpDefense:
        return "FACE_UP_DEFENSE";
    case Position::FaceDownDefense:
        return "FACE_DOWN_DEFENSE";
    case Position::Unknown:
        return "UNKNOWN";
    }
    return "UNKNOWN";
}

std::string link_marker_name(LinkMarker marker) {
    switch (marker) {
    case LinkMarker::BottomLeft:
        return "BOTTOM_LEFT";
    case LinkMarker::Bottom:
        return "BOTTOM";
    case LinkMarker::BottomRight:
        return "BOTTOM_RIGHT";
    case LinkMarker::Left:
        return "LEFT";
    case LinkMarker::Right:
        return "RIGHT";
    case LinkMarker::TopLeft:
        return "TOP_LEFT";
    case LinkMarker::Top:
        return "TOP";
    case LinkMarker::TopRight:
        return "TOP_RIGHT";
    }
    return "UNKNOWN";
}

std::string relationship_kind_name(RelationshipKind kind) {
    switch (kind) {
    case RelationshipKind::XyzMaterial:
        return "XYZ_MATERIAL";
    case RelationshipKind::Equip:
        return "EQUIP";
    case RelationshipKind::Target:
        return "TARGET";
    }
    return "TARGET";
}

std::string visible_event_kind_name(VisibleEventKind kind) {
    switch (kind) {
    case VisibleEventKind::TurnStarted:
        return "TURN_STARTED";
    case VisibleEventKind::PhaseChanged:
        return "PHASE_CHANGED";
    case VisibleEventKind::CardMoved:
        return "CARD_MOVED";
    case VisibleEventKind::CardRevealed:
        return "CARD_REVEALED";
    case VisibleEventKind::Summoned:
        return "SUMMONED";
    case VisibleEventKind::Set:
        return "SET";
    case VisibleEventKind::Draw:
        return "DRAW";
    case VisibleEventKind::Shuffle:
        return "SHUFFLE";
    case VisibleEventKind::RandomizationBoundary:
        return "RANDOMIZATION_BOUNDARY";
    case VisibleEventKind::LifePointsChanged:
        return "LIFE_POINTS_CHANGED";
    case VisibleEventKind::ChainActivated:
        return "CHAIN_ACTIVATED";
    case VisibleEventKind::ChainResolved:
        return "CHAIN_RESOLVED";
    case VisibleEventKind::ChainEnded:
        return "CHAIN_ENDED";
    case VisibleEventKind::CardDestroyed:
        return "CARD_DESTROYED";
    case VisibleEventKind::CardBanished:
        return "CARD_BANISHED";
    case VisibleEventKind::CardReturned:
        return "CARD_RETURNED";
    case VisibleEventKind::PositionChanged:
        return "POSITION_CHANGED";
    case VisibleEventKind::CounterChanged:
        return "COUNTER_CHANGED";
    case VisibleEventKind::Equipped:
        return "EQUIPPED";
    case VisibleEventKind::Unequipped:
        return "UNEQUIPPED";
    case VisibleEventKind::Targeted:
        return "TARGETED";
    case VisibleEventKind::Win:
        return "WIN";
    case VisibleEventKind::Unknown:
        return "UNKNOWN";
    }
    return "UNKNOWN";
}

std::string canonical_serialize_without_hash(const PlayerObservation& observation) {
    return serialize_without_hash(observation
#ifdef YGO_M4_PERFORMANCE_AUDIT
                                  , nullptr
#endif
    );
}

std::string observation_hash(const PlayerObservation& observation) {
    return ygo::trace::sha256_string(canonical_serialize_without_hash(observation));
}

std::string canonical_serialize(const PlayerObservation& observation) {
    const auto without_hash = canonical_serialize_without_hash(observation);
    const auto hash = ygo::trace::sha256_string(without_hash);
    return without_hash.substr(0, without_hash.size() - 1) + ",\"observation_hash\":" + json_escape(hash) + "}\n";
}

#ifdef YGO_M4_PERFORMANCE_AUDIT
std::string canonical_serialize_without_hash(const PlayerObservation& observation,
                                             PerformanceAuditCollector* audit) {
    if (audit == nullptr) {
        return canonical_serialize_without_hash(observation);
    }
    PerformanceAuditCollector::Scope scope(audit, PerformanceAuditBucket::CanonicalSerialization);
    return serialize_without_hash(observation, audit);
}

std::string canonical_serialize(const PlayerObservation& observation,
                                PerformanceAuditCollector* audit) {
    if (audit == nullptr) {
        return canonical_serialize(observation);
    }
    std::string without_hash;
    {
        PerformanceAuditCollector::Scope scope(audit, PerformanceAuditBucket::CanonicalSerialization);
        without_hash = serialize_without_hash(observation, audit);
    }
    std::string hash;
    {
        PerformanceAuditCollector::Scope scope(audit, PerformanceAuditBucket::Hash);
        audit->record_sha256_call();
        hash = ygo::trace::sha256_string(without_hash);
    }
    const auto serialized =
        without_hash.substr(0, without_hash.size() - 1) + ",\"observation_hash\":" + json_escape(hash) + "}\n";
    audit->record_canonical_serialize(serialized.size());
    return serialized;
}

std::string observation_hash(const PlayerObservation& observation,
                             PerformanceAuditCollector* audit) {
    if (audit == nullptr) {
        return observation_hash(observation);
    }
    std::string serialized;
    {
        PerformanceAuditCollector::Scope scope(audit, PerformanceAuditBucket::CanonicalSerialization);
        serialized = serialize_without_hash(observation, audit);
    }
    PerformanceAuditCollector::Scope scope(audit, PerformanceAuditBucket::Hash);
    audit->record_sha256_call();
    return ygo::trace::sha256_string(serialized);
}
#endif

}  // namespace ygo::observation
