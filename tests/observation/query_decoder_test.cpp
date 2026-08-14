#include <cstdint>
#include <cstring>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <vector>

#include "ocgapi_constants.h"
#include "query_decoder.hpp"

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void append_u8(std::vector<std::uint8_t>& bytes, std::uint8_t value) {
    bytes.push_back(value);
}

void append_u16(std::vector<std::uint8_t>& bytes, std::uint16_t value) {
    bytes.push_back(static_cast<std::uint8_t>(value & 0xff));
    bytes.push_back(static_cast<std::uint8_t>((value >> 8) & 0xff));
}

void append_u32(std::vector<std::uint8_t>& bytes, std::uint32_t value) {
    for (unsigned shift = 0; shift < 32; shift += 8) {
        bytes.push_back(static_cast<std::uint8_t>((value >> shift) & 0xff));
    }
}

void append_u64(std::vector<std::uint8_t>& bytes, std::uint64_t value) {
    for (unsigned shift = 0; shift < 64; shift += 8) {
        bytes.push_back(static_cast<std::uint8_t>((value >> shift) & 0xff));
    }
}

void append_location_ref(std::vector<std::uint8_t>& bytes, std::uint8_t controller,
                         std::uint8_t location, std::uint32_t sequence,
                         std::uint32_t position) {
    append_u8(bytes, controller);
    append_u8(bytes, location);
    append_u32(bytes, sequence);
    append_u32(bytes, position);
}

void append_record(std::vector<std::uint8_t>& bytes, std::uint32_t flag,
                   const std::vector<std::uint8_t>& payload) {
    append_u16(bytes, static_cast<std::uint16_t>(sizeof(std::uint32_t) + payload.size()));
    append_u32(bytes, flag);
    bytes.insert(bytes.end(), payload.begin(), payload.end());
}

std::vector<std::uint8_t> make_card_query() {
    std::vector<std::uint8_t> bytes;
    append_record(bytes, QUERY_CODE, {0x78, 0x56, 0x34, 0x12});
    append_record(bytes, QUERY_POSITION, {POS_FACEUP_ATTACK, 0, 0, 0});
    append_record(bytes, QUERY_TYPE, {TYPE_MONSTER, 0, 0, 0});
    append_record(bytes, QUERY_RACE, {0x20, 0, 0, 0, 0, 0, 0, 0});

    std::vector<std::uint8_t> equip;
    append_location_ref(equip, 1, LOCATION_MZONE, 3, POS_FACEUP_DEFENSE);
    append_record(bytes, QUERY_EQUIP_CARD, equip);

    std::vector<std::uint8_t> targets;
    append_u32(targets, 1);
    append_location_ref(targets, 0, LOCATION_SZONE, 5, POS_FACEUP_ATTACK);
    append_record(bytes, QUERY_TARGET_CARD, targets);

    std::vector<std::uint8_t> overlay;
    append_u32(overlay, 1);
    append_u32(overlay, 0x01020304);
    append_record(bytes, QUERY_OVERLAY_CARD, overlay);

    std::vector<std::uint8_t> counters;
    append_u32(counters, 1);
    append_u32(counters, 7U | (3U << 16));
    append_record(bytes, QUERY_COUNTERS, counters);

    append_record(bytes, QUERY_OWNER, {1});
    append_record(bytes, QUERY_IS_PUBLIC, {1});
    append_record(bytes, QUERY_LSCALE, {6, 0, 0, 0});
    append_record(bytes, QUERY_RSCALE, {8, 0, 0, 0});

    std::vector<std::uint8_t> link;
    append_u32(link, 3);
    append_u32(link, LINK_MARKER_TOP | LINK_MARKER_BOTTOM_LEFT);
    append_record(bytes, QUERY_LINK, link);

    append_record(bytes, QUERY_IS_HIDDEN, {0});
    append_record(bytes, QUERY_END, {});
    return bytes;
}

std::vector<std::uint8_t> make_location_query() {
    const auto card = make_card_query();
    std::vector<std::uint8_t> payload;
    append_u16(payload, 0);
    payload.insert(payload.end(), card.begin(), card.end());
    append_u16(payload, 0);
    std::vector<std::uint8_t> result;
    append_u32(result, static_cast<std::uint32_t>(payload.size()));
    result.insert(result.end(), payload.begin(), payload.end());
    return result;
}

std::vector<std::uint8_t> make_field_query() {
    std::vector<std::uint8_t> bytes;
    append_u32(bytes, 0x12345678);
    for (unsigned player = 0; player < 2; ++player) {
        append_u32(bytes, 8000 - player * 1000);
        for (unsigned slot = 0; slot < 7; ++slot) {
            append_u8(bytes, slot == 0 ? 1 : 0);
            if (slot == 0) {
                append_u8(bytes, POS_FACEUP_ATTACK);
                append_u32(bytes, 2);
            }
        }
        for (unsigned slot = 0; slot < 8; ++slot) {
            append_u8(bytes, slot == 5 ? 1 : 0);
            if (slot == 5) {
                append_u8(bytes, POS_FACEUP_DEFENSE);
                append_u32(bytes, 0);
            }
        }
        append_u32(bytes, 40);
        append_u32(bytes, 3);
        append_u32(bytes, 2);
        append_u32(bytes, 1);
        append_u32(bytes, 4);
        append_u32(bytes, player == 0 ? 1 : 0);
    }
    append_u32(bytes, 1);
    append_u32(bytes, 0x01020304);
    append_u8(bytes, 0);
    append_u8(bytes, LOCATION_MZONE);
    append_u32(bytes, 0);
    append_u32(bytes, POS_FACEUP_ATTACK);
    append_u8(bytes, 1);
    append_u8(bytes, LOCATION_SZONE);
    append_u32(bytes, 5);
    append_u64(bytes, 99);
    return bytes;
}

int run() {
    const auto card_bytes = make_card_query();
    const auto card = ygo::observation::detail::decode_card_query(card_bytes);
    require(card.records.size() == 15, "card query record count mismatch");
    require(card.records[2].flag == QUERY_TYPE, "engine-native query flag changed during decode");
    require(card.code.value() == 0x12345678, "QUERY_CODE decode mismatch");
    require(card.targets.size() == 1 && card.targets[0].sequence == 5, "QUERY_TARGET_CARD decode mismatch");
    require(card.equip_card.has_value() && card.equip_card->controller == 1,
            "QUERY_EQUIP_CARD decode mismatch");
    require(card.overlay_codes.size() == 1 && card.overlay_codes[0] == 0x01020304,
            "QUERY_OVERLAY_CARD decode mismatch");
    require(card.counters.size() == 1 && card.counters[0].type == 7 && card.counters[0].count == 3,
            "QUERY_COUNTERS decode mismatch");
    require(card.link_rating.value() == 3 && card.link_markers.value() == (LINK_MARKER_TOP | LINK_MARKER_BOTTOM_LEFT),
            "QUERY_LINK decode mismatch");
    require(card.is_public.value() && !card.is_hidden.value(), "visibility query decode mismatch");

    const auto location = ygo::observation::detail::decode_location_query(make_location_query());
    require(location.entries.size() == 3, "query location entry count mismatch");
    require(!location.entries[0].has_value() && location.entries[1].has_value() && !location.entries[2].has_value(),
            "query location null-slot decode mismatch");

    const auto field = ygo::observation::detail::decode_field_query(make_field_query());
    require(field.duel_options == 0x12345678, "field duel options mismatch");
    require(field.players[0].monster_slots.size() == 7 && field.players[0].monster_slots[0].overlay_count == 2,
            "field monster slot mismatch");
    require(field.players[0].spell_trap_slots[5].position == POS_FACEUP_DEFENSE,
            "field spell/trap slot mismatch");
    require(field.chain.size() == 1 && field.chain[0].description == 99,
            "field chain decode mismatch");

    auto truncated = make_card_query();
    truncated.pop_back();
    bool rejected = false;
    try {
        (void)ygo::observation::detail::decode_card_query(truncated);
    } catch (const ygo::observation::detail::QueryDecodeError&) {
        rejected = true;
    }
    require(rejected, "truncated query was accepted");
    std::cout << "query_decoder=ok\n";
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
