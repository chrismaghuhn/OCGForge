#include "ygo/observation/serialization.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <sstream>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include "ygo/trace/sha256.hpp"

namespace ygo::observation {
namespace {

using SerializationClock = std::chrono::steady_clock;

constexpr std::uint8_t kCopyTopLevelZones = 0;
constexpr std::uint8_t kCopyTopLevelEntities = 1;
constexpr std::uint8_t kCopyTopLevelRelationships = 2;
constexpr std::uint8_t kCopyTopLevelVisibleEvents = 3;
constexpr std::uint8_t kCopyChainTargets = 4;
constexpr std::uint8_t kCopyEventTargets = 5;
constexpr std::uint8_t kCopyDecisionReferences = 6;
constexpr std::uint8_t kCopyLinkMarkers = 7;
constexpr std::uint8_t kCopyCounters = 8;
constexpr std::uint8_t kCopyOwnDeck = 9;
constexpr std::uint8_t kCopyOpponentDeck = 10;

constexpr std::uint8_t kSortZones = 0;
constexpr std::uint8_t kSortEntities = 1;
constexpr std::uint8_t kSortRelationships = 2;
constexpr std::uint8_t kSortVisibleEvents = 3;
constexpr std::uint8_t kSortChainTargets = 4;
constexpr std::uint8_t kSortEventTargets = 5;
constexpr std::uint8_t kSortDecisionReferences = 6;
constexpr std::uint8_t kSortLinkMarkers = 7;
constexpr std::uint8_t kSortCounters = 8;
constexpr std::uint8_t kSortDecks = 9;

constexpr std::uint8_t kNestedPrintedProperties = 0;
constexpr std::uint8_t kNestedCurrentProperties = 1;
constexpr std::uint8_t kNestedCounters = 2;
constexpr std::uint8_t kNestedLinkMarkers = 3;
constexpr std::uint8_t kNestedOwnDeck = 4;
constexpr std::uint8_t kNestedOpponentDeck = 5;
constexpr std::uint8_t kNestedOtherMatchContext = 6;

constexpr std::uint8_t kPrimitiveNumeric = 0;
constexpr std::uint8_t kPrimitiveBoolean = 1;
constexpr std::uint8_t kPrimitiveNull = 2;

std::size_t output_offset(std::ostringstream& output) {
    const auto position = output.tellp();
    return position < 0 ? 0 : static_cast<std::size_t>(position);
}

template <typename T>
std::uint64_t approximate_fixed_vector_bytes(const std::vector<T>& values) {
    return static_cast<std::uint64_t>(sizeof(T)) * static_cast<std::uint64_t>(values.size());
}

#ifdef YGO_M4_SERIALIZATION_SHAPE_AUDIT
std::uint64_t approximate_locator_bytes(const ObservationLocator& locator) {
    return static_cast<std::uint64_t>(sizeof(ObservationLocator) + locator.value.size());
}

std::uint64_t approximate_card_bytes(const ObservedCard& card) {
    std::uint64_t bytes = static_cast<std::uint64_t>(sizeof(ObservedCard)) + card.locator.value.size();
    const auto add_properties = [&bytes](const std::optional<CardProperties>& properties) {
        if (!properties.has_value()) {
            return;
        }
        bytes += sizeof(CardProperties);
        bytes += static_cast<std::uint64_t>(properties->link_markers.size()) * sizeof(LinkMarker);
        bytes += static_cast<std::uint64_t>(properties->counters.size()) * sizeof(Counter);
    };
    add_properties(card.printed);
    add_properties(card.current);
    return bytes;
}

std::uint64_t approximate_event_bytes(const VisibleGameEvent& event) {
    std::uint64_t bytes = sizeof(VisibleGameEvent);
    if (event.entity.has_value()) {
        bytes += approximate_locator_bytes(*event.entity);
    }
    for (const auto& target : event.targets) {
        bytes += approximate_locator_bytes(target);
    }
    return bytes;
}

std::uint64_t approximate_relationship_bytes(const Relationship& relationship) {
    return static_cast<std::uint64_t>(sizeof(Relationship)) + relationship.source.value.size() +
           relationship.target.value.size();
}
#endif

std::uint64_t approximate_locator_vector_bytes(const std::vector<ObservationLocator>& values) {
    std::uint64_t bytes = approximate_fixed_vector_bytes(values);
    for (const auto& value : values) {
        bytes += value.value.size();
    }
    return bytes;
}

#ifdef YGO_M4_SERIALIZATION_SHAPE_AUDIT
class SerializationShapeProbe final {
public:
    explicit SerializationShapeProbe(PerformanceAuditCollector* audit = nullptr)
        : audit_(audit), start_(SerializationClock::now()) {
        if (audit_ != nullptr) {
            record_.lifecycle_id = audit_->active_lifecycle_id();
            if (record_.lifecycle_id == 0) {
                audit_->mark_serialization_shape_lifecycle_context_missing();
            }
        }
    }

    template <typename Action>
    auto copy(const std::uint8_t kind,
              const std::uint64_t elements,
              const std::uint64_t approximate_bytes,
              Action&& action) -> decltype(action()) {
        if (audit_ == nullptr) {
            return action();
        }
        const auto start = SerializationClock::now();
        if constexpr (std::is_void_v<decltype(action())>) {
            action();
            const auto elapsed = elapsed_us(start, SerializationClock::now());
            record_copy(kind, elements, approximate_bytes, elapsed);
            return;
        } else {
            auto result = action();
            const auto elapsed = elapsed_us(start, SerializationClock::now());
            record_copy(kind, elements, approximate_bytes, elapsed);
            return result;
        }
    }

    template <typename Action>
    void sort(const std::uint8_t kind, const std::uint64_t elements, Action&& action) {
        if (audit_ == nullptr) {
            action();
            return;
        }
        const auto start = SerializationClock::now();
        action();
        const auto elapsed = elapsed_us(start, SerializationClock::now());
        ++record_.sort_calls;
        record_.sort_elements += elements;
        record_.sorting_us += elapsed;
        audit_->record_serialization_shape_sort(kind, elements, elapsed);
    }

    template <typename Action>
    std::string escape(const std::string& input, Action&& action) {
        if (audit_ == nullptr) {
            return action();
        }
        const auto start = SerializationClock::now();
        auto output = action();
        const auto elapsed = elapsed_us(start, SerializationClock::now());
        ++record_.json_escape_calls;
        record_.json_escape_input_bytes += static_cast<std::uint64_t>(input.size());
        record_.json_escape_output_bytes += static_cast<std::uint64_t>(output.size());
        record_.escaping_us += elapsed;
        audit_->record_serialization_shape_escape(input.size(), output.size(), elapsed);
        return output;
    }

    void set_observation_metadata(const std::uint8_t perspective_player,
                                  const std::uint64_t decision_index,
                                  const std::uint64_t engine_step_index,
                                  const std::uint64_t chain_length) noexcept {
        record_.perspective_player = perspective_player;
        record_.decision_index = decision_index;
        record_.engine_step_index = engine_step_index;
        record_.chain_length = chain_length;
    }

    void record_section(const std::uint8_t section,
                        const std::size_t start,
                        const std::size_t end) noexcept {
        if (section < record_.section_bytes.size() && end >= start) {
            record_.section_bytes[section] += static_cast<std::uint64_t>(end - start);
        }
    }

    void record_entity(const std::uint64_t bytes) noexcept {
        ++record_.entity_instances;
        record_.entity_bytes += bytes;
    }

    void record_property(const std::uint8_t kind,
                         const bool present,
                         const std::uint64_t bytes) noexcept {
        if (kind == kNestedPrintedProperties) {
            record_.printed_property_objects += present ? 1 : 0;
            record_.printed_property_bytes += bytes;
        } else {
            record_.current_property_objects += present ? 1 : 0;
            record_.current_property_bytes += bytes;
        }
    }

    void record_nested_bytes(const std::uint8_t kind,
                             const std::uint64_t elements,
                             const std::uint64_t bytes) noexcept {
        switch (kind) {
        case kNestedCounters:
            record_.counter_instances += elements;
            record_.counter_bytes += bytes;
            break;
        case kNestedLinkMarkers:
            record_.link_marker_instances += elements;
            record_.link_marker_bytes += bytes;
            break;
        case kNestedOwnDeck:
            record_.own_deck_bytes += bytes;
            break;
        case kNestedOpponentDeck:
            record_.opponent_deck_bytes += bytes;
            break;
        case kNestedOtherMatchContext:
            record_.other_match_context_bytes += bytes;
            break;
        default:
            break;
        }
    }

    void record_visible_event(const VisibleGameEvent& event,
                              const std::uint64_t bytes) noexcept {
        ++record_.visible_event_instances;
        record_.visible_event_bytes += bytes;
        if (audit_ != nullptr) {
            audit_->record_serialization_shape_visible_event(
                static_cast<std::uint8_t>(record_.perspective_player),
                event.event_index,
                event.engine_step_index,
                static_cast<std::uint8_t>(event.kind));
        }
    }

    void record_primitive(const std::uint8_t kind) noexcept {
        if (kind == kPrimitiveNumeric) {
            ++record_.numeric_values;
        } else if (kind == kPrimitiveBoolean) {
            ++record_.boolean_values;
        } else {
            ++record_.null_values;
        }
        if (audit_ != nullptr) {
            audit_->record_serialization_shape_primitive(kind);
        }
    }

    void record_final_extraction(const std::uint64_t elapsed_us_value) noexcept {
        record_.final_extraction_us += elapsed_us_value;
    }

    void record_legacy_copy_event() noexcept {
        if (audit_ != nullptr) {
            audit_->record_copy_event();
        }
    }

    void finish(const std::uint64_t serialized_bytes) noexcept {
        if (audit_ == nullptr) {
            return;
        }
        record_.canonical_bytes = serialized_bytes;
        record_.total_us = elapsed_us(start_, SerializationClock::now());
        const auto measured = record_.preparation_copy_us + record_.sorting_us +
                              record_.escaping_us + record_.final_extraction_us;
        if (record_.total_us >= measured) {
            record_.rendering_us = record_.total_us - measured;
        } else {
            record_.rendering_us = 0;
            record_.rendering_residual_clamped = true;
        }
        audit_->record_serialization_shape_record(record_);
    }

private:
    static std::uint64_t elapsed_us(const SerializationClock::time_point start,
                                    const SerializationClock::time_point end) noexcept {
        return static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(end - start).count());
    }

    void record_copy(const std::uint8_t kind,
                     const std::uint64_t elements,
                     const std::uint64_t approximate_bytes,
                     const std::uint64_t elapsed_us_value) noexcept {
        ++record_.copy_calls;
        record_.copy_elements += elements;
        record_.copy_approximate_bytes += approximate_bytes;
        record_.preparation_copy_us += elapsed_us_value;
        audit_->record_serialization_shape_copy(kind, elements, approximate_bytes, elapsed_us_value);
    }

    PerformanceAuditCollector* audit_ = nullptr;
    SerializationClock::time_point start_{};
    PerformanceAuditSerializationShapeRecord record_{};
};
#else
class SerializationShapeProbe final {
public:
#ifdef YGO_M4_PERFORMANCE_AUDIT
    explicit SerializationShapeProbe(PerformanceAuditCollector* audit) noexcept : audit_(audit) {}
#else
    SerializationShapeProbe() = default;
#endif

    template <typename Action>
    auto copy(std::uint8_t, std::uint64_t, std::uint64_t, Action&& action) -> decltype(action()) {
        return action();
    }

    template <typename Action>
    void sort(std::uint8_t, std::uint64_t, Action&& action) {
        action();
    }

    template <typename Action>
    std::string escape(const std::string&, Action&& action) {
        return action();
    }

    void set_observation_metadata(std::uint8_t, std::uint64_t, std::uint64_t, std::uint64_t) noexcept {}
    void record_section(std::uint8_t, std::size_t, std::size_t) noexcept {}
    void record_entity(std::uint64_t) noexcept {}
    void record_property(std::uint8_t, bool, std::uint64_t) noexcept {}
    void record_nested_bytes(std::uint8_t, std::uint64_t, std::uint64_t) noexcept {}
    void record_visible_event(const VisibleGameEvent&, std::uint64_t) noexcept {}
    void record_primitive(std::uint8_t) noexcept {}
    void record_final_extraction(std::uint64_t) noexcept {}
    void record_legacy_copy_event() noexcept {
#ifdef YGO_M4_PERFORMANCE_AUDIT
        if (audit_ != nullptr) {
            audit_->record_copy_event();
        }
#endif
    }
    void finish(std::uint64_t) noexcept {}

#ifdef YGO_M4_PERFORMANCE_AUDIT
private:
    PerformanceAuditCollector* audit_ = nullptr;
#endif
};
#endif

std::string json_escape_impl(const std::string& value) {
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

std::string json_escape(const std::string& value, SerializationShapeProbe& shape) {
    return shape.escape(value, [&value]() { return json_escape_impl(value); });
}

std::string json_escape(const std::string& value) {
    return json_escape_impl(value);
}

template <typename T>
void write_optional_number(std::ostringstream& out,
                           const std::optional<T>& value,
                           SerializationShapeProbe& shape) {
    if (value.has_value()) {
        shape.record_primitive(kPrimitiveNumeric);
        out << *value;
    } else {
        shape.record_primitive(kPrimitiveNull);
        out << "null";
    }
}

void write_optional_string(std::ostringstream& out,
                           const std::optional<std::string>& value,
                           SerializationShapeProbe& shape) {
    if (value.has_value()) {
        out << json_escape(*value, shape);
    } else {
        shape.record_primitive(kPrimitiveNull);
        out << "null";
    }
}

void write_optional_locator(std::ostringstream& out,
                            const std::optional<ObservationLocator>& value,
                            SerializationShapeProbe& shape) {
    if (value.has_value()) {
        out << json_escape(value->value, shape);
    } else {
        shape.record_primitive(kPrimitiveNull);
        out << "null";
    }
}

void write_optional_zone(std::ostringstream& out,
                         const std::optional<SemanticZone>& value,
                         SerializationShapeProbe& shape) {
    if (value.has_value()) {
        out << json_escape(semantic_zone_name(*value), shape);
    } else {
        shape.record_primitive(kPrimitiveNull);
        out << "null";
    }
}

void write_optional_u8(std::ostringstream& out,
                       const std::optional<std::uint8_t>& value,
                       SerializationShapeProbe& shape) {
    if (value.has_value()) {
        shape.record_primitive(kPrimitiveNumeric);
        out << static_cast<unsigned>(*value);
    } else {
        shape.record_primitive(kPrimitiveNull);
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

void write_counters(std::ostringstream& out,
                    const std::vector<Counter>& input,
                    SerializationShapeProbe& shape) {
    auto values = shape.copy(kCopyCounters, input.size(), approximate_fixed_vector_bytes(input),
                             [&input]() { return input; });
    shape.sort(kSortCounters, values.size(), [&values]() {
        std::sort(values.begin(), values.end(), [](const Counter& left, const Counter& right) {
            return std::tie(left.type, left.count) < std::tie(right.type, right.count);
        });
    });
    const auto start = output_offset(out);
    write_array(out, values, [&shape](std::ostringstream& stream, const Counter& counter) {
        shape.record_primitive(kPrimitiveNumeric);
        shape.record_primitive(kPrimitiveNumeric);
        stream << "{\"type\":" << counter.type << ",\"count\":" << counter.count << '}';
    });
    shape.record_nested_bytes(kNestedCounters, values.size(), output_offset(out) - start);
}

void write_properties(std::ostringstream& out,
                      const std::optional<CardProperties>& properties,
                      const std::uint8_t property_kind,
                      SerializationShapeProbe& shape) {
    const auto start = output_offset(out);
    if (!properties.has_value()) {
        shape.record_property(property_kind, false, 4);
        out << "null";
        return;
    }
    const auto& value = *properties;
    auto markers = shape.copy(kCopyLinkMarkers, value.link_markers.size(),
                              approximate_fixed_vector_bytes(value.link_markers), [&value]() {
                                  return value.link_markers;
                              });
    shape.sort(kSortLinkMarkers, markers.size(), [&markers]() {
        std::sort(markers.begin(), markers.end(), [](LinkMarker left, LinkMarker right) {
            return static_cast<unsigned>(left) < static_cast<unsigned>(right);
        });
    });
    out << "{\"type\":";
    write_optional_number(out, value.type, shape);
    out << ",\"attribute\":";
    write_optional_number(out, value.attribute, shape);
    out << ",\"race\":";
    write_optional_number(out, value.race, shape);
    out << ",\"attack\":";
    write_optional_number(out, value.attack, shape);
    out << ",\"defense\":";
    write_optional_number(out, value.defense, shape);
    out << ",\"base_attack\":";
    write_optional_number(out, value.base_attack, shape);
    out << ",\"base_defense\":";
    write_optional_number(out, value.base_defense, shape);
    out << ",\"level\":";
    write_optional_number(out, value.level, shape);
    out << ",\"rank\":";
    write_optional_number(out, value.rank, shape);
    out << ",\"link_rating\":";
    write_optional_number(out, value.link_rating, shape);
    out << ",\"link_markers\":";
    const auto marker_start = output_offset(out);
    write_array(out, markers, [&shape](std::ostringstream& stream, LinkMarker marker) {
        stream << json_escape(link_marker_name(marker), shape);
    });
    shape.record_nested_bytes(kNestedLinkMarkers, markers.size(), output_offset(out) - marker_start);
    out << ",\"left_scale\":";
    write_optional_number(out, value.left_scale, shape);
    out << ",\"right_scale\":";
    write_optional_number(out, value.right_scale, shape);
    out << ",\"status_flags\":";
    write_optional_number(out, value.status_flags, shape);
    out << ",\"counters\":";
    write_counters(out, value.counters, shape);
    out << '}';
    shape.record_property(property_kind, true, output_offset(out) - start);
}

void write_card(std::ostringstream& out,
                const ObservedCard& card,
                SerializationShapeProbe& shape) {
    out << "{\"locator\":" << json_escape(card.locator.value, shape);
    shape.record_primitive(kPrimitiveBoolean);
    out << ",\"identity_known\":" << (card.identity_known ? "true" : "false")
        << ",\"passcode\":";
    write_optional_number(out, card.passcode, shape);
    out << ",\"owner\":";
    write_optional_u8(out, card.owner, shape);
    out << ",\"controller\":";
    write_optional_u8(out, card.controller, shape);
    out << ",\"zone\":" << json_escape(semantic_zone_name(card.zone), shape) << ",\"sequence\":";
    write_optional_number(out, card.sequence, shape);
    out << ",\"overlay_sequence\":";
    write_optional_number(out, card.overlay_sequence, shape);
    out << ",\"position\":" << json_escape(position_name(card.position), shape);
    shape.record_primitive(kPrimitiveBoolean);
    shape.record_primitive(kPrimitiveBoolean);
    out
        << ",\"face_up\":" << (card.face_up ? "true" : "false")
        << ",\"face_down\":" << (card.face_down ? "true" : "false")
        << ",\"printed\":";
    write_properties(out, card.printed, kNestedPrintedProperties, shape);
    out << ",\"current\":";
    write_properties(out, card.current, kNestedCurrentProperties, shape);
    out << '}';
}

void write_event(std::ostringstream& out,
                 const VisibleGameEvent& event,
                 SerializationShapeProbe& shape) {
    auto targets = shape.copy(kCopyEventTargets, event.targets.size(),
                              approximate_locator_vector_bytes(event.targets), [&event]() {
                                  return event.targets;
                              });
    shape.sort(kSortEventTargets, targets.size(), [&targets]() {
        std::sort(targets.begin(), targets.end());
    });
    shape.record_primitive(kPrimitiveNumeric);
    shape.record_primitive(kPrimitiveNumeric);
    out << "{\"event_index\":" << event.event_index << ",\"engine_step_index\":"
        << event.engine_step_index << ",\"kind\":" << json_escape(visible_event_kind_name(event.kind), shape)
        << ",\"player\":";
    write_optional_u8(out, event.player, shape);
    out << ",\"entity\":";
    write_optional_locator(out, event.entity, shape);
    out << ",\"public_passcode\":";
    write_optional_number(out, event.public_passcode, shape);
    out << ",\"from_zone\":";
    write_optional_zone(out, event.from_zone, shape);
    out << ",\"to_zone\":";
    write_optional_zone(out, event.to_zone, shape);
    out << ",\"count\":";
    write_optional_number(out, event.count, shape);
    out << ",\"amount\":";
    write_optional_number(out, event.amount, shape);
    out << ",\"counter_type\":";
    write_optional_number(out, event.counter_type, shape);
    out << ",\"phase\":";
    write_optional_number(out, event.phase, shape);
    out << ",\"winner\":";
    write_optional_u8(out, event.winner, shape);
    out << ",\"win_reason\":";
    write_optional_u8(out, event.win_reason, shape);
    out << ",\"effect_description\":";
    write_optional_number(out, event.effect_description, shape);
    out << ",\"targets\":";
    write_array(out, targets, [&shape](std::ostringstream& stream, const ObservationLocator& locator) {
        stream << json_escape(locator.value, shape);
    });
    out << '}';
}

void write_match_context(std::ostringstream& out,
                         const MatchContext& context,
                         SerializationShapeProbe& shape) {
    const auto write_deck = [&out, &shape](const StaticDeckContext& deck,
                                           const std::uint8_t copy_kind,
                                           const std::uint8_t nested_kind) {
        const auto deck_start = output_offset(out);
        auto main = shape.copy(copy_kind, deck.main_deck.size(), approximate_fixed_vector_bytes(deck.main_deck),
                               [&deck]() { return deck.main_deck; });
        auto extra = shape.copy(copy_kind, deck.extra_deck.size(), approximate_fixed_vector_bytes(deck.extra_deck),
                                [&deck]() { return deck.extra_deck; });
        shape.sort(kSortDecks, main.size(), [&main]() { std::sort(main.begin(), main.end()); });
        shape.sort(kSortDecks, extra.size(), [&extra]() { std::sort(extra.begin(), extra.end()); });
        shape.record_primitive(kPrimitiveBoolean);
        out << "{\"known\":" << (deck.known ? "true" : "false") << ",\"main_deck\":";
        write_array(out, main, [&shape](std::ostringstream& stream, std::uint32_t code) {
            shape.record_primitive(kPrimitiveNumeric);
            stream << code;
        });
        out << ",\"extra_deck\":";
        write_array(out, extra, [&shape](std::ostringstream& stream, std::uint32_t code) {
            shape.record_primitive(kPrimitiveNumeric);
            stream << code;
        });
        out << '}';
        shape.record_nested_bytes(nested_kind, 0, output_offset(out) - deck_start);
    };
    const auto context_start = output_offset(out);
    shape.record_primitive(kPrimitiveNumeric);
    shape.record_primitive(kPrimitiveNumeric);
    shape.record_primitive(kPrimitiveBoolean);
    shape.record_primitive(kPrimitiveBoolean);
    out << "{\"perspective_player\":" << static_cast<unsigned>(context.perspective_player)
        << ",\"duel_flags\":" << context.duel_flags << ",\"knowledge\":{\"own_decklist_known\":"
        << (context.knowledge.own_decklist_known ? "true" : "false")
        << ",\"opponent_decklist_known\":"
        << (context.knowledge.opponent_decklist_known ? "true" : "false") << "},\"own_deck\":";
    const auto own_start = output_offset(out);
    write_deck(context.own_deck, kCopyOwnDeck, kNestedOwnDeck);
    const auto own_end = output_offset(out);
    out << ",\"opponent_deck\":";
    const auto opponent_start = output_offset(out);
    write_deck(context.opponent_deck, kCopyOpponentDeck, kNestedOpponentDeck);
    const auto opponent_end = output_offset(out);
    out << '}';
    const auto total = output_offset(out) - context_start;
    const auto deck_bytes = (own_end - own_start) + (opponent_end - opponent_start);
    shape.record_nested_bytes(kNestedOtherMatchContext, 0, total >= deck_bytes ? total - deck_bytes : 0);
}

std::string serialize_without_hash(
    const PlayerObservation& observation
#ifdef YGO_M4_PERFORMANCE_AUDIT
    , PerformanceAuditCollector* audit
#endif
) {
#ifdef YGO_M4_PERFORMANCE_AUDIT
    SerializationShapeProbe shape(audit);
#else
    SerializationShapeProbe shape;
#endif
    shape.set_observation_metadata(observation.perspective_player,
                                   observation.decision_index,
                                   observation.engine_step_index,
                                   observation.chain.length);
    const auto zones_copy_bytes = approximate_fixed_vector_bytes(observation.zones);
    const auto entities_copy_bytes = [&observation]() {
        std::uint64_t bytes = approximate_fixed_vector_bytes(observation.entities);
#ifdef YGO_M4_SERIALIZATION_SHAPE_AUDIT
        bytes = 0;
        for (const auto& entity : observation.entities) {
            bytes += approximate_card_bytes(entity);
        }
#endif
        return bytes;
    }();
    const auto relationships_copy_bytes = [&observation]() {
        std::uint64_t bytes = approximate_fixed_vector_bytes(observation.relationships);
#ifdef YGO_M4_SERIALIZATION_SHAPE_AUDIT
        bytes = 0;
        for (const auto& relationship : observation.relationships) {
            bytes += approximate_relationship_bytes(relationship);
        }
#endif
        return bytes;
    }();
    const auto visible_events_copy_bytes = [&observation]() {
        std::uint64_t bytes = approximate_fixed_vector_bytes(observation.visible_events);
#ifdef YGO_M4_SERIALIZATION_SHAPE_AUDIT
        bytes = 0;
        for (const auto& event : observation.visible_events) {
            bytes += approximate_event_bytes(event);
        }
#endif
        return bytes;
    }();
    auto zones = shape.copy(kCopyTopLevelZones, observation.zones.size(),
                            zones_copy_bytes,
                            [&observation]() { return observation.zones; });
    shape.record_legacy_copy_event();
    shape.sort(kSortZones, zones.size(), [&zones]() {
        std::sort(zones.begin(), zones.end(), [](const ObservedZone& left, const ObservedZone& right) {
            return std::tie(left.player, left.kind, left.total_count, left.public_identity_count,
                            left.hidden_count) <
                   std::tie(right.player, right.kind, right.total_count, right.public_identity_count,
                            right.hidden_count);
        });
    });
    auto entities = shape.copy(kCopyTopLevelEntities, observation.entities.size(),
                               entities_copy_bytes,
                               [&observation]() { return observation.entities; });
    shape.record_legacy_copy_event();
    shape.sort(kSortEntities, entities.size(), [&entities]() {
        std::sort(entities.begin(), entities.end(), [](const ObservedCard& left, const ObservedCard& right) {
            return left.locator.value < right.locator.value;
        });
    });
    auto relationships = shape.copy(kCopyTopLevelRelationships, observation.relationships.size(),
                                    relationships_copy_bytes,
                                    [&observation]() { return observation.relationships; });
    shape.record_legacy_copy_event();
    shape.sort(kSortRelationships, relationships.size(), [&relationships]() {
        std::sort(relationships.begin(), relationships.end(),
                  [](const Relationship& left, const Relationship& right) {
                      return std::tie(left.kind, left.source.value, left.target.value) <
                             std::tie(right.kind, right.source.value, right.target.value);
                  });
    });
    auto events = shape.copy(kCopyTopLevelVisibleEvents, observation.visible_events.size(),
                             visible_events_copy_bytes,
                             [&observation]() { return observation.visible_events; });
    shape.record_legacy_copy_event();
    shape.sort(kSortVisibleEvents, events.size(), [&events]() {
        std::sort(events.begin(), events.end(), [](const VisibleGameEvent& left, const VisibleGameEvent& right) {
            return std::tie(left.event_index, left.engine_step_index) <
                   std::tie(right.event_index, right.engine_step_index);
        });
    });

    std::ostringstream out;
    out << "{\"schema_version\":" << json_escape(observation.schema_version, shape)
        << ",\"perspective_player\":" << static_cast<unsigned>(observation.perspective_player)
        << ",\"decision_index\":" << observation.decision_index
        << ",\"engine_step_index\":" << observation.engine_step_index;
    shape.record_primitive(kPrimitiveNumeric);
    shape.record_primitive(kPrimitiveNumeric);
    shape.record_primitive(kPrimitiveNumeric);
    shape.record_section(0, 0, output_offset(out));

    auto section_start = output_offset(out);
    out << ",\"globals\":{";
    shape.record_primitive(kPrimitiveNumeric);
    out << "\"duel_flags\":" << observation.globals.duel_flags << ",\"life_points\":";
    write_array(out, observation.globals.life_points,
                [&shape](std::ostringstream& stream, std::uint32_t value) {
                    shape.record_primitive(kPrimitiveNumeric);
                    stream << value;
                });
    out << ",\"player_to_act\":";
    write_optional_u8(out, observation.globals.player_to_act, shape);
    out << ",\"turn_player\":";
    write_optional_u8(out, observation.globals.turn_player, shape);
    out << ",\"turn_count\":";
    write_optional_number(out, observation.globals.turn_count, shape);
    out << ",\"phase\":";
    write_optional_number(out, observation.globals.phase, shape);
    shape.record_primitive(kPrimitiveNumeric);
    out << ",\"chain_length\":" << observation.globals.chain_length << ",\"winner\":";
    write_optional_u8(out, observation.globals.winner, shape);
    out << ",\"win_reason\":";
    write_optional_u8(out, observation.globals.win_reason, shape);
    shape.record_primitive(kPrimitiveBoolean);
    out << ",\"terminal\":" << (observation.globals.terminal ? "true" : "false") << '}';
    shape.record_section(1, section_start, output_offset(out));

    section_start = output_offset(out);
    out << ",\"zones\":";
    write_array(out, zones, [&shape](std::ostringstream& stream, const ObservedZone& zone) {
        for (int index = 0; index < 5; ++index) {
            shape.record_primitive(kPrimitiveNumeric);
        }
        shape.record_primitive(kPrimitiveBoolean);
        stream << "{\"player\":" << static_cast<unsigned>(zone.player) << ",\"kind\":"
               << json_escape(semantic_zone_name(zone.kind), shape) << ",\"total_count\":" << zone.total_count
               << ",\"public_identity_count\":" << zone.public_identity_count
               << ",\"hidden_count\":" << zone.hidden_count
               << ",\"player_observable_order\":"
               << (zone.player_observable_order ? "true" : "false") << '}';
    });
    shape.record_section(2, section_start, output_offset(out));

    section_start = output_offset(out);
    out << ",\"entities\":[";
    for (std::size_t index = 0; index < entities.size(); ++index) {
        if (index != 0) {
            out << ',';
        }
        const auto entity_start = output_offset(out);
        write_card(out, entities[index], shape);
        shape.record_entity(output_offset(out) - entity_start);
    }
    out << ']';
    shape.record_section(3, section_start, output_offset(out));

    section_start = output_offset(out);
    out << ",\"relationships\":";
    write_array(out, relationships, [&shape](std::ostringstream& stream, const Relationship& relationship) {
        stream << "{\"kind\":" << json_escape(relationship_kind_name(relationship.kind), shape)
               << ",\"source\":" << json_escape(relationship.source.value, shape)
               << ",\"target\":" << json_escape(relationship.target.value, shape) << '}';
    });
    shape.record_section(4, section_start, output_offset(out));

    section_start = output_offset(out);
    shape.record_primitive(kPrimitiveNumeric);
    out << ",\"chain\":{\"length\":" << observation.chain.length << ",\"links\":";
    write_array(out, observation.chain.links, [&shape](std::ostringstream& stream, const ChainLink& link) {
        auto targets = shape.copy(kCopyChainTargets, link.targets.size(),
                                  approximate_locator_vector_bytes(link.targets), [&link]() {
                                      return link.targets;
                                  });
        shape.record_legacy_copy_event();
        shape.sort(kSortChainTargets, targets.size(), [&targets]() {
            std::sort(targets.begin(), targets.end());
        });
        shape.record_primitive(kPrimitiveNumeric);
        stream << "{\"index\":" << link.index << ",\"activating_player\":";
        write_optional_u8(stream, link.activating_player, shape);
        stream << ",\"source\":";
        write_optional_locator(stream, link.source, shape);
        stream << ",\"activation_zone\":";
        write_optional_zone(stream, link.activation_zone, shape);
        stream << ",\"effect_description\":";
        write_optional_number(stream, link.effect_description, shape);
        stream << ",\"targets\":";
        write_array(stream, targets, [&shape](std::ostringstream& target_stream,
                                             const ObservationLocator& locator) {
            target_stream << json_escape(locator.value, shape);
        });
        stream << '}';
    });
    out << '}';
    shape.record_section(5, section_start, output_offset(out));

    section_start = output_offset(out);
    out << ",\"visible_events\":[";
    for (std::size_t index = 0; index < events.size(); ++index) {
        if (index != 0) {
            out << ',';
        }
        const auto event_start = output_offset(out);
        write_event(out, events[index], shape);
        shape.record_visible_event(events[index], output_offset(out) - event_start);
        shape.record_legacy_copy_event();
    }
    out << ']';
    shape.record_section(6, section_start, output_offset(out));

    section_start = output_offset(out);
    out << ",\"decision_context\":{";
    out << "\"decision_id\":";
    write_optional_string(out, observation.decision_context.decision_id, shape);
    out << ",\"kind\":";
    write_optional_string(out, observation.decision_context.kind, shape);
    out << ",\"engine_step_index\":";
    write_optional_number(out, observation.decision_context.engine_step_index, shape);
    out << ",\"player\":";
    write_optional_u8(out, observation.decision_context.player, shape);
    out << ",\"engine_message_type\":";
    write_optional_u8(out, observation.decision_context.engine_message_type, shape);
    out << ",\"engine_message_name\":";
    write_optional_string(out, observation.decision_context.engine_message_name, shape);
    out << ",\"continuation_id\":";
    write_optional_string(out, observation.decision_context.continuation_id, shape);
    out << ",\"referenced_entities\":";
    auto references = shape.copy(kCopyDecisionReferences,
                                observation.decision_context.referenced_entities.size(),
                                approximate_locator_vector_bytes(observation.decision_context.referenced_entities),
                                [&observation]() { return observation.decision_context.referenced_entities; });
    shape.record_legacy_copy_event();
    shape.sort(kSortDecisionReferences, references.size(), [&references]() {
        std::sort(references.begin(), references.end());
    });
    write_array(out, references, [&shape](std::ostringstream& stream, const ObservationLocator& locator) {
        stream << json_escape(locator.value, shape);
    });
    out << '}';
    shape.record_section(7, section_start, output_offset(out));

    section_start = output_offset(out);
    out << ",\"match_context\":";
    write_match_context(out, observation.match_context, shape);
    out << '}';
    shape.record_section(8, section_start, output_offset(out));
#ifdef YGO_M4_PERFORMANCE_AUDIT
    const auto extraction_start = SerializationClock::now();
    auto serialized = out.str();
    shape.record_final_extraction(static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(SerializationClock::now() - extraction_start).count()));
    shape.finish(serialized.size());
    if (audit != nullptr) {
        audit->record_serialize_without_hash(serialized.size());
    }
    return serialized;
#else
    const auto extraction_start = SerializationClock::now();
    auto serialized = out.str();
    shape.record_final_extraction(static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(SerializationClock::now() - extraction_start).count()));
    shape.finish(serialized.size());
    return serialized;
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
