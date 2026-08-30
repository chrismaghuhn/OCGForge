#include <cstdlib>
#include <cstdint>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "ygo/environment/public_environment_observation.hpp"
#include "ygo/environment/public_safe_state.hpp"
#include "ygo/observation/player_observation.hpp"

namespace {

void require(const bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

ygo::observation::ObservedCard known_card() {
    ygo::observation::ObservedCard card;
    card.locator = {"p0:MONSTER_ZONE:0"};
    card.identity_known = true;
    card.passcode = 12345678;
    card.owner = 0;
    card.controller = 0;
    card.zone = ygo::observation::SemanticZone::MonsterZone;
    card.sequence = 0;
    card.position = ygo::observation::Position::FaceUpAttack;
    card.face_up = true;
    card.printed.emplace();
    card.printed->type = 0x401;
    card.printed->attribute = 0x20;
    card.printed->race = 0x1;
    card.printed->attack = 2500;
    card.printed->defense = 2000;
    card.printed->level = 8;
    card.printed->link_markers = {ygo::observation::LinkMarker::Bottom,
                                  ygo::observation::LinkMarker::Top};
    card.printed->counters = {{7, 2}, {3, 1}};
    card.current.emplace();
    card.current->attack = 2700;
    card.current->defense = 2100;
    card.current->status_flags = 0x8;
    return card;
}

ygo::observation::ObservedCard redacted_card() {
    ygo::observation::ObservedCard card;
    card.locator = {"p1:SPELL_TRAP_ZONE:2"};
    card.identity_known = false;
    card.owner = 1;
    card.controller = 1;
    card.zone = ygo::observation::SemanticZone::SpellTrapZone;
    card.sequence = 2;
    card.position = ygo::observation::Position::FaceDownDefense;
    card.face_down = true;
    return card;
}

ygo::observation::PlayerObservation source_observation() {
    ygo::observation::PlayerObservation source;
    source.schema_version = "ygo.player_observation.v1";
    source.perspective_player = 0;
    source.decision_index = 42;
    source.engine_step_index = 9001;

    source.globals.duel_flags = 0x1122334455667788ULL;
    source.globals.life_points = {8000, 7000};
    source.globals.player_to_act = 1;
    source.globals.turn_player = 0;
    source.globals.turn_count = 3;
    source.globals.phase = 2;
    source.globals.chain_length = 1;
    source.globals.winner.reset();
    source.globals.win_reason.reset();
    source.globals.terminal = false;

    source.zones = {
        {1, ygo::observation::SemanticZone::SpellTrapZone, 5, 0, 5, false},
        {0, ygo::observation::SemanticZone::MonsterZone, 5, 1, 4, true},
    };
    source.entities = {redacted_card(), known_card()};
    source.relationships.push_back({ygo::observation::RelationshipKind::Target,
                                    {"p0:MONSTER_ZONE:0"},
                                    {"p1:SPELL_TRAP_ZONE:2"}});

    source.chain.length = 1;
    ygo::observation::ChainLink link;
    link.index = 0;
    link.activating_player = 0;
    link.source = ygo::observation::ObservationLocator{"p0:MONSTER_ZONE:0"};
    link.activation_zone = ygo::observation::SemanticZone::MonsterZone;
    link.effect_description = 0x1020304050607080ULL;
    link.targets = {{"p1:SPELL_TRAP_ZONE:2"}};
    source.chain.links.push_back(link);

    ygo::observation::VisibleGameEvent event;
    event.event_index = 7;
    event.engine_step_index = 123456789;
    event.kind = ygo::observation::VisibleEventKind::ChainActivated;
    event.player = 0;
    event.entity = ygo::observation::ObservationLocator{"p0:MONSTER_ZONE:0"};
    event.public_passcode = 12345678;
    event.from_zone = ygo::observation::SemanticZone::Hand;
    event.to_zone = ygo::observation::SemanticZone::MonsterZone;
    event.count = 1;
    event.amount = -500;
    event.counter_type = 7;
    event.phase = 2;
    event.effect_description = 0x1020304050607080ULL;
    event.targets = {{"p1:SPELL_TRAP_ZONE:2"}};
    source.visible_events.push_back(event);

    source.match_context.perspective_player = 0;
    source.match_context.duel_flags = source.globals.duel_flags;
    source.match_context.knowledge.own_decklist_known = true;
    source.match_context.knowledge.opponent_decklist_known = false;
    source.match_context.own_deck.known = true;
    source.match_context.own_deck.main_deck = {2, 1, 3};
    source.match_context.own_deck.extra_deck = {9, 8};
    source.match_context.opponent_deck.known = false;
    return source;
}

ygo::observation::PlayerObservation source_observation_with_two_events() {
    auto source = source_observation();
    auto second = source.visible_events.front();
    second.event_index = 8;
    second.engine_step_index = 987654321;
    second.kind = ygo::observation::VisibleEventKind::CardMoved;
    source.visible_events.push_back(second);
    return source;
}

ygo::observation::PlayerObservation source_observation_with_known_opponent_deck() {
    auto source = source_observation();
    source.match_context.opponent_deck.known = true;
    source.match_context.opponent_deck.main_deck = {42};
    return source;
}

struct WireRange final {
    std::size_t begin = 0;
    std::size_t end = 0;
};

struct SafeStateWireLocations final {
    std::vector<WireRange> entity_records;
    std::vector<WireRange> entity_locators;
    std::vector<std::size_t> entity_identity_flags;
    std::vector<std::size_t> entity_zone_codes;
    std::vector<std::size_t> entity_face_up_flags;
    std::vector<std::size_t> entity_face_down_flags;
    std::vector<WireRange> event_records;
    std::vector<std::size_t> event_index_values;
    std::vector<std::size_t> event_kind_codes;
    std::size_t player_to_act_presence = std::numeric_limits<std::size_t>::max();
    std::vector<std::size_t> deck_known_flags;
};

class SafeStateWireCursor final {
public:
    explicit SafeStateWireCursor(const std::vector<std::uint8_t>& bytes) : bytes_(bytes) {}

    bool u8(std::uint8_t& value, std::size_t* location = nullptr) noexcept {
        if (position_ >= bytes_.size()) {
            return false;
        }
        if (location != nullptr) {
            *location = position_;
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

    bool u64(std::uint64_t& value, std::size_t* location = nullptr) noexcept {
        if (bytes_.size() - position_ < 8) {
            return false;
        }
        if (location != nullptr) {
            *location = position_;
        }
        value = 0;
        for (int shift = 56; shift >= 0; shift -= 8) {
            value |= static_cast<std::uint64_t>(bytes_[position_++]) << shift;
        }
        return true;
    }

    bool string(std::string& value, WireRange* payload = nullptr) noexcept {
        std::uint32_t length = 0;
        if (!u32(length) || length > bytes_.size() - position_) {
            return false;
        }
        const auto begin = position_;
        value.assign(reinterpret_cast<const char*>(bytes_.data() + position_), length);
        position_ += length;
        if (payload != nullptr) {
            *payload = {begin, position_};
        }
        return true;
    }

    bool boolean(bool& value, std::size_t* location = nullptr) noexcept {
        std::uint8_t encoded = 0;
        if (!u8(encoded, location) || encoded > 1) {
            return false;
        }
        value = encoded == 1;
        return true;
    }

    bool optional_u8(std::size_t* flag_location = nullptr) noexcept {
        std::uint8_t flag = 0;
        if (!u8(flag, flag_location) || flag > 1) {
            return false;
        }
        if (flag == 0) {
            return true;
        }
        return u8(flag);
    }

    bool optional_u32() noexcept {
        std::uint8_t flag = 0;
        if (!u8(flag) || flag > 1) {
            return false;
        }
        if (flag == 0) {
            return true;
        }
        std::uint32_t value = 0;
        return u32(value);
    }

    bool optional_u64() noexcept {
        std::uint8_t flag = 0;
        if (!u8(flag) || flag > 1) {
            return false;
        }
        if (flag == 0) {
            return true;
        }
        std::uint64_t value = 0;
        return u64(value);
    }

    bool optional_i32() noexcept {
        return optional_u32();
    }

    bool optional_locator() noexcept {
        std::uint8_t flag = 0;
        if (!u8(flag) || flag > 1) {
            return false;
        }
        if (flag == 0) {
            return true;
        }
        std::string value;
        return string(value);
    }

    bool optional_zone() noexcept {
        std::uint8_t flag = 0;
        if (!u8(flag) || flag > 1) {
            return false;
        }
        if (flag == 0) {
            return true;
        }
        return u8(flag);
    }

    bool properties() noexcept {
        bool present = false;
        if (!boolean(present)) {
            return false;
        }
        if (!present) {
            return true;
        }
        if (!optional_u32() || !optional_u32() || !optional_u64() || !optional_i32() ||
            !optional_i32() || !optional_i32() || !optional_i32() || !optional_u32() ||
            !optional_u32() || !optional_u32()) {
            return false;
        }
        std::uint32_t count = 0;
        if (!u32(count) || count > bytes_.size() - position_) {
            return false;
        }
        position_ += count;
        if (!optional_u32() || !optional_u32() || !optional_u32() || !u32(count) ||
            count > (bytes_.size() - position_) / 8) {
            return false;
        }
        position_ += static_cast<std::size_t>(count) * 8;
        return true;
    }

    bool targets() noexcept {
        std::uint32_t count = 0;
        if (!u32(count) || count > bytes_.size() - position_) {
            return false;
        }
        for (std::uint32_t index = 0; index < count; ++index) {
            std::string value;
            if (!string(value)) {
                return false;
            }
        }
        return true;
    }

    std::size_t position() const noexcept { return position_; }
    bool end() const noexcept { return position_ == bytes_.size(); }

private:
    const std::vector<std::uint8_t>& bytes_;
    std::size_t position_ = 0;
};

bool locate_safe_state(const std::vector<std::uint8_t>& bytes,
                       SafeStateWireLocations& locations) {
    locations = SafeStateWireLocations{};
    SafeStateWireCursor cursor(bytes);
    std::string schema;
    std::uint64_t u64_value = 0;
    std::uint32_t count = 0;
    std::uint8_t u8_value = 0;
    bool bool_value = false;
    if (!cursor.string(schema) || !cursor.string(schema) || !cursor.u64(u64_value) ||
        !cursor.u32(count)) {
        return false;
    }
    const auto life_count = count;
    for (std::uint32_t index = 0; index < life_count; ++index) {
        std::uint32_t life_points = 0;
        if (!cursor.u32(life_points)) {
            return false;
        }
    }
    if (!cursor.optional_u8(&locations.player_to_act_presence) || !cursor.optional_u8() ||
        !cursor.optional_u32() || !cursor.optional_u32() || !cursor.u32(count) ||
        !cursor.optional_u8() || !cursor.optional_u8() || !cursor.boolean(bool_value)) {
        return false;
    }

    if (!cursor.u32(count)) {
        return false;
    }
    const auto zone_count = count;
    for (std::uint32_t index = 0; index < zone_count; ++index) {
        std::uint32_t value = 0;
        if (!cursor.u8(u8_value) || !cursor.u8(u8_value) || !cursor.u32(value) ||
            !cursor.u32(value) || !cursor.u32(value) || !cursor.boolean(bool_value)) {
            return false;
        }
    }

    if (!cursor.u32(count)) {
        return false;
    }
    for (std::uint32_t index = 0; index < count; ++index) {
        WireRange record;
        record.begin = cursor.position();
        WireRange locator;
        if (!cursor.string(schema, &locator)) {
            return false;
        }
        locations.entity_records.push_back(record);
        locations.entity_locators.push_back(locator);
        std::size_t flag = 0;
        if (!cursor.boolean(bool_value, &flag)) {
            return false;
        }
        locations.entity_identity_flags.push_back(flag);
        if (!cursor.optional_u32() || !cursor.optional_u8() || !cursor.optional_u8() ||
            !cursor.u8(u8_value)) {
            return false;
        }
        locations.entity_zone_codes.push_back(cursor.position() - 1);
        if (!cursor.optional_u32() || !cursor.optional_u32() || !cursor.u8(u8_value) ||
            !cursor.boolean(bool_value, &flag)) {
            return false;
        }
        locations.entity_face_up_flags.push_back(flag);
        if (!cursor.boolean(bool_value, &flag)) {
            return false;
        }
        locations.entity_face_down_flags.push_back(flag);
        if (!cursor.properties() || !cursor.properties()) {
            return false;
        }
        locations.entity_records.back().end = cursor.position();
    }

    if (!cursor.u32(count)) {
        return false;
    }
    for (std::uint32_t index = 0; index < count; ++index) {
        if (!cursor.u8(u8_value) || !cursor.string(schema) || !cursor.string(schema)) {
            return false;
        }
    }

    if (!cursor.u32(count)) {
        return false;
    }
    if (!cursor.u32(count)) {
        return false;
    }
    for (std::uint32_t index = 0; index < count; ++index) {
        std::uint32_t value = 0;
        if (!cursor.u32(value) || !cursor.optional_u8() || !cursor.optional_locator() ||
            !cursor.optional_zone() || !cursor.optional_u64() || !cursor.targets()) {
            return false;
        }
    }

    if (!cursor.u32(count)) {
        return false;
    }
    for (std::uint32_t index = 0; index < count; ++index) {
        WireRange record;
        record.begin = cursor.position();
        std::size_t value_location = 0;
        if (!cursor.u64(u64_value, &value_location) || !cursor.u8(u8_value)) {
            return false;
        }
        locations.event_records.push_back(record);
        locations.event_index_values.push_back(value_location);
        locations.event_kind_codes.push_back(cursor.position() - 1);
        if (!cursor.optional_u8() || !cursor.optional_locator() || !cursor.optional_u32() ||
            !cursor.optional_zone() || !cursor.optional_zone() || !cursor.optional_u32() ||
            !cursor.optional_i32() || !cursor.optional_u32() || !cursor.optional_u32() ||
            !cursor.optional_u8() || !cursor.optional_u8() || !cursor.optional_u64() ||
            !cursor.targets()) {
            return false;
        }
        locations.event_records.back().end = cursor.position();
    }

    if (!cursor.u8(u8_value) || !cursor.u64(u64_value) || !cursor.boolean(bool_value) ||
        !cursor.boolean(bool_value)) {
        return false;
    }
    for (int deck = 0; deck < 2; ++deck) {
        std::size_t known_location = 0;
        if (!cursor.boolean(bool_value, &known_location) || !cursor.u32(count)) {
            return false;
        }
        locations.deck_known_flags.push_back(known_location);
        const auto main_count = count;
        for (std::uint32_t index = 0; index < main_count; ++index) {
            std::uint32_t value = 0;
            if (!cursor.u32(value)) {
                return false;
            }
        }
        if (!cursor.u32(count)) {
            return false;
        }
        const auto extra_count = count;
        for (std::uint32_t index = 0; index < extra_count; ++index) {
            std::uint32_t value = 0;
            if (!cursor.u32(value)) {
                return false;
            }
        }
    }
    return cursor.end();
}

void write_u64be(std::vector<std::uint8_t>& bytes,
                 const std::size_t location,
                 const std::uint64_t value) {
    for (int shift = 56; shift >= 0; shift -= 8) {
        bytes[location + static_cast<std::size_t>((56 - shift) / 8)] =
            static_cast<std::uint8_t>(value >> shift);
    }
}

std::vector<std::uint8_t> swapped_ranges(const std::vector<std::uint8_t>& bytes,
                                         const WireRange first,
                                         const WireRange second) {
    require(first.begin < first.end && first.end <= second.begin && second.begin < second.end,
            "test fixture ranges were not ordered");
    std::vector<std::uint8_t> result;
    result.reserve(bytes.size());
    result.insert(result.end(), bytes.begin(), bytes.begin() + first.begin);
    result.insert(result.end(), bytes.begin() + second.begin, bytes.begin() + second.end);
    result.insert(result.end(), bytes.begin() + first.end, bytes.begin() + second.begin);
    result.insert(result.end(), bytes.begin() + first.begin, bytes.begin() + first.end);
    result.insert(result.end(), bytes.begin() + second.end, bytes.end());
    return result;
}

void replace_payload(std::vector<std::uint8_t>& bytes,
                     const WireRange payload,
                     const std::string& replacement) {
    std::vector<std::uint8_t> encoded;
    const auto size = static_cast<std::uint32_t>(replacement.size());
    encoded.push_back(static_cast<std::uint8_t>(size >> 24));
    encoded.push_back(static_cast<std::uint8_t>(size >> 16));
    encoded.push_back(static_cast<std::uint8_t>(size >> 8));
    encoded.push_back(static_cast<std::uint8_t>(size));
    encoded.insert(encoded.end(), replacement.begin(), replacement.end());
    bytes.erase(bytes.begin() + static_cast<std::ptrdiff_t>(payload.begin - 4),
                bytes.begin() + static_cast<std::ptrdiff_t>(payload.end));
    bytes.insert(bytes.begin() + static_cast<std::ptrdiff_t>(payload.begin - 4), encoded.begin(),
                 encoded.end());
}

void require_rejected(const std::vector<std::uint8_t>& bytes, const char* message) {
    const auto decoded = ygo::environment::decode_canonical_public_safe_state(bytes);
    require(!decoded && !decoded.value.has_value() && decoded.diagnostic.has_value(), message);
}

void test_strict_negative_inputs() {
    const auto canonical = ygo::environment::canonical_public_safe_state_bytes(source_observation());
    SafeStateWireLocations locations;
    require(locate_safe_state(canonical, locations), "test fixture safe-state layout did not parse");
    require(locations.entity_records.size() == 2 && locations.event_records.size() == 1,
            "test fixture did not expose the expected entity/event records");

    auto truncated = canonical;
    truncated.pop_back();
    require_rejected(truncated, "truncated safe-state bytes were accepted");

    auto trailing = canonical;
    trailing.push_back(0);
    require_rejected(trailing, "trailing safe-state bytes were accepted");

    auto invalid_presence = canonical;
    invalid_presence[locations.player_to_act_presence] = 2;
    require_rejected(invalid_presence, "invalid optional presence byte was accepted");

    auto invalid_enum = canonical;
    invalid_enum[locations.entity_zone_codes.front()] = 0xff;
    require_rejected(invalid_enum, "invalid enum code was accepted");

    const auto unsorted_entities = swapped_ranges(
        canonical, locations.entity_records[0], locations.entity_records[1]);
    require_rejected(unsorted_entities, "noncanonical entity ordering was accepted");

    auto duplicate_entity = canonical;
    const std::string first_locator(
        canonical.begin() + static_cast<std::ptrdiff_t>(locations.entity_locators[0].begin),
        canonical.begin() + static_cast<std::ptrdiff_t>(locations.entity_locators[0].end));
    replace_payload(duplicate_entity, locations.entity_locators[1], first_locator);
    require_rejected(duplicate_entity, "duplicate entity locator was accepted");

    auto hidden_identity = canonical;
    hidden_identity[locations.entity_identity_flags.front()] = 0;
    require_rejected(hidden_identity, "hidden entity identity data was accepted");

    auto both_face_states = canonical;
    both_face_states[locations.entity_face_down_flags.front()] = 1;
    require_rejected(both_face_states, "face-up and face-down entity was accepted");

    const auto two_event_bytes = ygo::environment::canonical_public_safe_state_bytes(
        source_observation_with_two_events());
    SafeStateWireLocations two_event_locations;
    require(locate_safe_state(two_event_bytes, two_event_locations),
            "two-event fixture safe-state layout did not parse");
    require(two_event_locations.event_records.size() == 2,
            "two-event fixture did not expose both records");

    const auto unsorted_events = swapped_ranges(
        two_event_bytes, two_event_locations.event_records[0], two_event_locations.event_records[1]);
    require_rejected(unsorted_events, "noncanonical event ordering was accepted");

    auto duplicate_events = two_event_bytes;
    write_u64be(duplicate_events, two_event_locations.event_index_values[1], 7);
    require_rejected(duplicate_events, "duplicate event index was accepted");

    const auto known_opponent_bytes = ygo::environment::canonical_public_safe_state_bytes(
        source_observation_with_known_opponent_deck());
    SafeStateWireLocations known_opponent_locations;
    require(locate_safe_state(known_opponent_bytes, known_opponent_locations),
            "known-opponent fixture safe-state layout did not parse");
    require(known_opponent_locations.deck_known_flags.size() == 2,
            "test fixture did not expose both static-deck flags");
    auto unknown_with_identity = known_opponent_bytes;
    unknown_with_identity[known_opponent_locations.deck_known_flags[1]] = 0;
    require_rejected(unknown_with_identity,
                     "unknown static deck containing identities was accepted");
}

void test_public_header_has_no_private_event_step_or_observation_include() {
    const auto header_path = std::filesystem::path(YGO_SOURCE_DIR) /
                             "include/ygo/environment/public_safe_state.hpp";
    std::ifstream header(header_path, std::ios::binary);
    require(header.good(), "public safe-state header could not be read");
    const std::string source((std::istreambuf_iterator<char>(header)),
                             std::istreambuf_iterator<char>());
    require(source.find("engine_step_index") == std::string::npos,
            "public safe-state header mentions private engine-step metadata");
    require(source.find("player_observation.hpp") == std::string::npos,
            "public safe-state header includes private PlayerObservation");
}

void test_typed_public_safe_state_round_trip() {
    const auto source = source_observation();
    const auto bytes = ygo::environment::canonical_public_safe_state_bytes(source);
    const auto decoded = ygo::environment::decode_canonical_public_safe_state(bytes);
    require(static_cast<bool>(decoded), "valid safe-state bytes did not decode");
    require(decoded.value.has_value(), "successful decode did not contain a typed view");

    const auto& view = *decoded.value;
    require(view.globals().duel_flags == source.globals.duel_flags,
            "typed view changed public globals");
    require(view.globals().life_points == source.globals.life_points,
            "typed view changed public life points");
    require(view.globals().player_to_act == source.globals.player_to_act,
            "typed view changed acting player");
    require(view.zones().size() == source.zones.size(), "typed view changed zones");
    require(view.entities().size() == source.entities.size(), "typed view changed entities");
    require(view.entities()[0].locator.value == "p0:MONSTER_ZONE:0",
            "typed view did not preserve canonical entity ordering");
    require(view.entities()[1].identity_known == false,
            "typed view did not preserve redaction");
    require(view.relationships().size() == source.relationships.size(),
            "typed view changed relationships");
    require(view.chain().length == source.chain.length, "typed view changed chain length");
    require(view.chain().links.size() == source.chain.links.size(),
            "typed view changed chain links");
    require(view.visible_events().size() == source.visible_events.size(),
            "typed view changed visible events");
    require(view.visible_events()[0].event_index == source.visible_events[0].event_index,
            "typed view changed public event index");
    require(view.visible_events()[0].effect_description ==
                source.visible_events[0].effect_description,
            "typed view changed public event effect description");
    require(view.match_context().perspective_player == source.match_context.perspective_player,
            "typed view changed match perspective");
    require(view.match_context().own_deck.main_deck ==
                std::vector<std::uint32_t>({1, 2, 3}),
            "typed view did not preserve canonical deck ordering");
    require(ygo::environment::canonical_public_safe_state_bytes(view) == bytes,
            "typed safe-state round-trip changed canonical bytes");

    auto changed_internal_metadata = source;
    changed_internal_metadata.engine_step_index = 9002;
    changed_internal_metadata.visible_events[0].engine_step_index = 987654321;
    require(ygo::environment::canonical_public_safe_state_bytes(changed_internal_metadata) == bytes,
            "internal engine-step metadata changed public safe-state bytes");
}

int run() {
    test_public_header_has_no_private_event_step_or_observation_include();
    test_typed_public_safe_state_round_trip();
    test_strict_negative_inputs();
    return EXIT_SUCCESS;
}

}  // namespace

int main() {
    try {
        return run();
    } catch (const std::exception& error) {
        return (std::cerr << error.what() << '\n'), EXIT_FAILURE;
    }
}
