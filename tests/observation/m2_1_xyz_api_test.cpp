#include <algorithm>
#include <cstdint>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <vector>

#include "common.h"
#include "ocgapi_constants.h"
#include "ocgapi_types.h"
#include "query_decoder.hpp"
#include "ygo/core/core_host.hpp"
#include "ygo/observation/observation_builder.hpp"
#include "ygo/observation/serialization.hpp"

#ifndef YGO_M2_PLAYER_A
#error "YGO_M2_PLAYER_A must be supplied by CMake"
#endif
#ifndef YGO_M2_PLAYER_B
#error "YGO_M2_PLAYER_B must be supplied by CMake"
#endif
#ifndef YGO_M2_CARDSCRIPTS
#error "YGO_M2_CARDSCRIPTS must be supplied by CMake"
#endif
#ifndef YGO_M2_SETUP
#error "YGO_M2_SETUP must be supplied by CMake"
#endif
#ifndef YGO_M2_SETUP_XYZ_A
#error "YGO_M2_SETUP_XYZ_A must be supplied by CMake"
#endif
#ifndef YGO_M2_SETUP_XYZ_B
#error "YGO_M2_SETUP_XYZ_B must be supplied by CMake"
#endif

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

ygo::observation::PlayerObservation build_observation(const char* setup_path) {
    ygo::core::CoreHostConfig core_config;
    core_config.rules.card_data_tsv = YGO_M0_CARD_DATA_TSV;
    core_config.rules.card_scripts_root = YGO_M2_CARDSCRIPTS;
    core_config.duel_flags = DUEL_PZONE | DUEL_SEPARATE_PZONE;
    core_config.starting_draw_count = 0;
    core_config.draw_count_per_turn = 0;
    core_config.seed.words = {0x0123456789abcdefULL, 0xfedcba9876543210ULL,
                              0x13579bdf2468ace0ULL, 0x0eca8642fdb97531ULL};
    ygo::core::CoreHost host(core_config);
    host.load_deck(0, ygo::core::load_fixture_deck(YGO_M2_PLAYER_A));
    host.load_deck(1, ygo::core::load_fixture_deck(YGO_M2_PLAYER_B));
    host.load_fixture_script(setup_path);
    host.start_duel();
    (void)host.process();
    return ygo::observation::build_player_observation(host, 0, {});
}

int run() {
    ygo::core::CoreHostConfig core_config;
    core_config.rules.card_data_tsv = YGO_M0_CARD_DATA_TSV;
    core_config.rules.card_scripts_root = YGO_M2_CARDSCRIPTS;
    core_config.duel_flags = DUEL_PZONE | DUEL_SEPARATE_PZONE;
    core_config.starting_draw_count = 0;
    core_config.draw_count_per_turn = 0;
    core_config.seed.words = {0x0123456789abcdefULL, 0xfedcba9876543210ULL,
                              0x13579bdf2468ace0ULL, 0x0eca8642fdb97531ULL};
    ygo::core::CoreHost host(core_config);
    host.load_deck(0, ygo::core::load_fixture_deck(YGO_M2_PLAYER_A));
    host.load_deck(1, ygo::core::load_fixture_deck(YGO_M2_PLAYER_B));
    host.load_fixture_script(YGO_M2_SETUP);
    host.start_duel();
    (void)host.process();

    OCG_QueryInfo parent_info{};
    parent_info.flags = QUERY_CODE | QUERY_OVERLAY_CARD | QUERY_END;
    parent_info.con = 0;
    parent_info.loc = LOCATION_MZONE;
    parent_info.seq = 0;
    const auto parent = ygo::observation::detail::decode_card_query(host.query(parent_info));
    require(parent.code.value_or(0) == 359563, "pinned Xyz parent was not publicly queryable");
    require(parent.overlay_codes.size() >= 2, "pinned public query did not expose both aggregate materials");
    const auto field = ygo::observation::detail::decode_field_query(host.query_field());
    require(field.players[0].monster_slots[0].overlay_count == parent.overlay_codes.size(),
            "OCG_DuelQueryField aggregate overlay count disagreed with the parent query");

    OCG_QueryInfo material_info{};
    material_info.flags = QUERY_CODE | QUERY_POSITION | QUERY_OWNER | QUERY_IS_PUBLIC | QUERY_IS_HIDDEN | QUERY_END;
    material_info.con = 0;
    material_info.loc = LOCATION_MZONE | LOCATION_OVERLAY;
    material_info.seq = 0;
    std::vector<std::size_t> individual_lengths;
    std::vector<std::size_t> location_lengths;
    bool all_individual_queries_empty = true;
    bool all_location_queries_null = true;
    for (std::uint32_t overlay_sequence = 0; overlay_sequence < parent.overlay_codes.size(); ++overlay_sequence) {
        material_info.overlay_seq = overlay_sequence;
        const auto individual_bytes = host.query(material_info);
        const auto location_bytes = host.query_location(material_info);
        const auto location = ygo::observation::detail::decode_location_query(location_bytes);
        individual_lengths.push_back(individual_bytes.size());
        location_lengths.push_back(location_bytes.size());
        const bool location_is_null = location.entries.size() == 1 && !location.entries.front().has_value();
        all_individual_queries_empty = all_individual_queries_empty && individual_bytes.empty();
        all_location_queries_null = all_location_queries_null && location_is_null;
    }

    std::cout << "parent_overlay_count=" << parent.overlay_codes.size() << '\n'
              << "field_overlay_count=" << field.players[0].monster_slots[0].overlay_count << '\n'
              << "overlay_query_count=" << host.query_count(0, LOCATION_OVERLAY) << '\n';
    for (std::size_t index = 0; index < individual_lengths.size(); ++index) {
        std::cout << "overlay_seq=" << index << " individual_query_bytes=" << individual_lengths[index]
                  << " location_query_bytes=" << location_lengths[index] << '\n';
    }

    require(all_individual_queries_empty,
            "pinned public OCG_DuelQuery unexpectedly returned an individual Xyz material record");
    require(all_location_queries_null,
            "pinned public OCG_DuelQueryLocation unexpectedly returned an individual Xyz material record");

    const auto world_a = build_observation(YGO_M2_SETUP_XYZ_A);
    const auto world_b = build_observation(YGO_M2_SETUP_XYZ_B);
    require(world_a.observation_hash == world_b.observation_hash,
            "paired hidden Xyz material worlds changed the perspective-safe observation hash");
    require(ygo::observation::canonical_serialize(world_a) == ygo::observation::canonical_serialize(world_b),
            "paired hidden Xyz material worlds changed canonical observation bytes");
    for (const auto& observation : {world_a, world_b}) {
        std::size_t redacted_material_count = 0;
        for (const auto& entity : observation.entities) {
            if (entity.zone != ygo::observation::SemanticZone::Overlay) {
                continue;
            }
            ++redacted_material_count;
            require(!entity.identity_known && !entity.passcode.has_value() && !entity.current.has_value() &&
                        !entity.printed.has_value(),
                    "Xyz material paired-world fixture exposed identity-derived metadata");
        }
        require(redacted_material_count == 2, "paired-world fixture did not retain both material slots safely");
    }
    std::cout << "paired_observation_hash=" << world_a.observation_hash << '\n';
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
