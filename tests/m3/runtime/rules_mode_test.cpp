#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "ocgapi_constants.h"
#include "ygo/core/core_host.hpp"
#include "ygo/m3/canonical_rules.hpp"

#ifndef YGO_M0_PLAYER_A
#error "YGO_M0_PLAYER_A must be supplied by CMake"
#endif
#ifndef YGO_M0_PLAYER_B
#error "YGO_M0_PLAYER_B must be supplied by CMake"
#endif
#ifndef YGO_M0_CARDSCRIPTS
#error "YGO_M0_CARDSCRIPTS must be supplied by CMake"
#endif

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

bool has_new_turn_for(const std::vector<std::uint8_t>& message, std::uint8_t player) {
    std::size_t offset = 0;
    while (offset + 4 <= message.size()) {
        const auto length = static_cast<std::uint32_t>(message[offset]) |
                            (static_cast<std::uint32_t>(message[offset + 1]) << 8) |
                            (static_cast<std::uint32_t>(message[offset + 2]) << 16) |
                            (static_cast<std::uint32_t>(message[offset + 3]) << 24);
        offset += 4;
        if (length == 0 || offset + length > message.size()) {
            return false;
        }
        if (message[offset] == MSG_NEW_TURN && length == 2 && message[offset + 1] == player) {
            return true;
        }
        offset += length;
    }
    return false;
}

}  // namespace

int main() {
    try {
        const auto& rules = ygo::m3::canonical_rules();
        require(rules.format_id == "TCG_ADVANCED_2026_05_18", "unexpected canonical format");
        require(rules.duel_mode_name == "DUEL_MODE_MR5", "unexpected canonical duel mode");
        require(rules.duel_flags == static_cast<std::uint64_t>(DUEL_MODE_MR5),
                "canonical flags do not equal the pinned core DUEL_MODE_MR5");
        require(rules.duel_flags == 0x2E800, "canonical MR5 numeric flags mismatch");

        ygo::core::CoreHostConfig config;
        config.rules.card_scripts_root = YGO_M0_CARDSCRIPTS;
        config.rules.card_data_tsv = YGO_M0_CARD_DATA_TSV;
        config.rules.bundle_id = std::string(rules.rules_bundle_id);
        config.rules.core_patchset_id = std::string(rules.core_patchset_id);
        config.rules.core_patchset_sha256 = std::string(rules.core_patchset_sha256);
        config.duel_flags = rules.duel_flags;
        config.starting_player = 1;
        config.seed.words = {1, 2, 3, 4};
        ygo::core::CoreHost host(config);
        host.load_deck(0, ygo::core::load_fixture_deck(YGO_M0_PLAYER_A));
        host.load_deck(1, ygo::core::load_fixture_deck(YGO_M0_PLAYER_B));
        host.start_duel();
        bool saw_player_one = false;
        for (int attempt = 0; attempt < 8 && !saw_player_one; ++attempt) {
            saw_player_one = has_new_turn_for(host.process().message, 1);
        }
        require(saw_player_one, "CoreHost did not start the duel with the requested player");
        require(host.config().duel_flags == rules.duel_flags,
                "CoreHost did not retain the canonical duel flags");
        require(host.config().rules.bundle_id == rules.rules_bundle_id,
                "CoreHost did not retain the canonical bundle identity");
        require(host.config().rules.core_patchset_id == rules.core_patchset_id &&
                    host.config().rules.core_patchset_sha256 == rules.core_patchset_sha256,
                "CoreHost did not retain the canonical core patchset identity");
        std::cout << "rules_mode=canonical_mr5\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
