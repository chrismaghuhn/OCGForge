#include <cstdint>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <vector>

#include "ocgapi_constants.h"
#include "public_core_harness.hpp"
#include "query_decoder.hpp"

#ifndef YGO_M35_PLAYER_A
#error "YGO_M35_PLAYER_A must be supplied by CMake"
#endif
#ifndef YGO_M35_PLAYER_B
#error "YGO_M35_PLAYER_B must be supplied by CMake"
#endif
#ifndef YGO_M35_XYZ_SETUP
#error "YGO_M35_XYZ_SETUP must be supplied by CMake"
#endif
#ifndef YGO_M35_XYZ_EMPTY_SETUP
#error "YGO_M35_XYZ_EMPTY_SETUP must be supplied by CMake"
#endif

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

}  // namespace

int main() {
    try {
        m35::core_test::PublicCoreDuel duel;
        duel.load_deck(0, ygo::core::load_fixture_deck(YGO_M35_PLAYER_A));
        duel.load_deck(1, ygo::core::load_fixture_deck(YGO_M35_PLAYER_B));
        duel.load_script(YGO_M35_XYZ_SETUP);
        duel.start();
        duel.process_once();

        OCG_QueryInfo parent_info{};
        parent_info.flags = QUERY_CODE | QUERY_OVERLAY_CARD | QUERY_END;
        parent_info.con = 0;
        parent_info.loc = LOCATION_MZONE;
        parent_info.seq = 0;
        const auto parent = ygo::observation::detail::decode_card_query(duel.query(parent_info));
        require(parent.code.value_or(0) == 359563, "M3.5 Xyz parent was not queryable");
        require(parent.overlay_codes.size() == 2, "M3.5 Xyz fixture did not create two materials");

        const std::vector<std::uint32_t> expected_materials{32864, 549481};
        for (std::uint32_t index = 0; index < expected_materials.size(); ++index) {
            OCG_QueryInfo material_info{};
            material_info.flags = QUERY_CODE | QUERY_OWNER | QUERY_POSITION | QUERY_IS_PUBLIC |
                                  QUERY_IS_HIDDEN | QUERY_END;
            material_info.con = 0;
            material_info.loc = LOCATION_MZONE | LOCATION_OVERLAY;
            material_info.seq = 0;
            material_info.overlay_seq = index;
            const auto material_bytes = duel.query(material_info);
            require(!material_bytes.empty(), "M3.5 individual overlay query returned an empty result");
            const auto material = ygo::observation::detail::decode_card_query(material_bytes);
            require(material.code.value_or(0) == expected_materials[index],
                    "M3.5 individual overlay query returned the wrong material");
        }

        OCG_QueryInfo out_of_range{};
        out_of_range.flags = QUERY_CODE | QUERY_END;
        out_of_range.con = 0;
        out_of_range.loc = LOCATION_MZONE | LOCATION_OVERLAY;
        out_of_range.seq = 0;
        out_of_range.overlay_seq = 2;
        require(duel.query(out_of_range).empty(), "M3.5 out-of-range overlay query did not fail closed");

        m35::core_test::PublicCoreDuel empty_duel;
        empty_duel.load_deck(0, ygo::core::load_fixture_deck(YGO_M35_PLAYER_A));
        empty_duel.load_deck(1, ygo::core::load_fixture_deck(YGO_M35_PLAYER_B));
        empty_duel.load_script(YGO_M35_XYZ_EMPTY_SETUP);
        empty_duel.start();
        empty_duel.process_once();
        const auto empty_parent = ygo::observation::detail::decode_card_query(empty_duel.query(parent_info));
        require(empty_parent.overlay_codes.empty(), "M3.5 zero-material Xyz parent was not empty");
        require(empty_duel.query(out_of_range).empty(),
                "M3.5 zero-material overlay query did not fail closed");

        std::cout << "m35_xyz_material_query=pass\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
