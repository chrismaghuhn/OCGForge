#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <utility>

#include "ygo/core/core_host.hpp"

#ifndef YGO_M0_CARD_DATA_TSV
#error "YGO_M0_CARD_DATA_TSV must be supplied by CMake"
#endif
#ifndef YGO_M2_CARDSCRIPTS
#error "YGO_M2_CARDSCRIPTS must be supplied by CMake"
#endif

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

ygo::core::CoreHost make_host() {
    ygo::core::CoreHostConfig config;
    config.rules.card_data_tsv = YGO_M0_CARD_DATA_TSV;
    config.rules.card_scripts_root = YGO_M2_CARDSCRIPTS;
    config.seed.words = {0x0123456789abcdefULL, 0xfedcba9876543210ULL,
                         0x13579bdf2468ace0ULL, 0x0eca8642fdb97531ULL};
    return ygo::core::CoreHost(std::move(config));
}

int run() {
    auto host = make_host();
    const auto link = host.static_card_data(146746);
    const auto pendulum = host.static_card_data(41546);
    require(link.has_value(), "pinned Link card was not in the catalog");
    require(pendulum.has_value(), "pinned Pendulum card was not in the catalog");
    require(link->type & 0x04000000U, "catalog Link type was not preserved");
    require(link->level == 2, "catalog Link rating was not decoded from level");
    require(link->link_marker == 34, "catalog Link markers were not decoded from CDB DEF");
    require(link->defense == 0, "catalog Link DEF was fabricated from marker bits");
    require(pendulum->level == 8, "catalog Pendulum level was not decoded");
    require(pendulum->left_scale == 6 && pendulum->right_scale == 6,
            "catalog Pendulum scales were not decoded from packed CDB level");
    std::cout << "card_catalog=ok\n";
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
