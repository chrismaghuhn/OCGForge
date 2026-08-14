#include <cstdint>
#include <iostream>
#include <vector>

#include "ygo/protocol/response_builder.hpp"

int main() {
    const auto cards = ygo::protocol::encode_card_index_response({1, 256});
    const std::vector<std::uint8_t> expected_cards = {0, 0, 0, 0, 2, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0};
    if (cards != expected_cards) {
        std::cerr << "card response encoding mismatch\n";
        return 1;
    }

    const auto zones = ygo::protocol::encode_zone_response({{0, 4, 2}, {1, 8, 7}});
    if (zones != std::vector<std::uint8_t>({0, 4, 2, 1, 8, 7})) {
        std::cerr << "zone response encoding mismatch\n";
        return 1;
    }

    const auto counters = ygo::protocol::encode_counter_response({0, 300});
    if (counters != std::vector<std::uint8_t>({0, 0, 44, 1})) {
        std::cerr << "counter response encoding mismatch\n";
        return 1;
    }

    const auto ordering = ygo::protocol::encode_order_response({2, 0, 1});
    if (ordering != std::vector<std::uint8_t>({2, 0, 1})) {
        std::cerr << "ordering response encoding mismatch\n";
        return 1;
    }
    return 0;
}
