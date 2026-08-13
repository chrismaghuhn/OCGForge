#include <cstdint>
#include <exception>
#include <iostream>

#include "ygo/core/core_error.hpp"
#include "ygo/core/core_host.hpp"
#include "ygo/protocol/message_decoder.hpp"
#include "ygo/protocol/protocol_error.hpp"

#ifndef YGO_M0_PLAYER_A
#error "YGO_M0_PLAYER_A must be supplied by CMake"
#endif

int main() {
    try {
        ygo::core::CoreHostConfig config;
        config.rules.card_scripts_root = YGO_M0_CARDSCRIPTS;
        config.rules.card_data_tsv = YGO_M0_CARD_DATA_TSV;
        config.rules.bundle_id = "6fbbd212ae4be2df36170dcbfcdf5c46aaaa0e3091cf815c2d0261fd01640ea4";
        config.seed.words = {0x0123456789abcdefULL, 0xfedcba9876543210ULL, 0x13579bdf2468ace0ULL,
                             0x0eca8642fdb97531ULL};
        ygo::core::CoreHost host(config);
        if (host.api_major() != 11 || host.api_minor() != 0) {
            std::cerr << "unexpected OCG API version\n";
            return 1;
        }
        host.load_deck(0, ygo::core::load_fixture_deck(YGO_M0_PLAYER_A));
        host.load_deck(1, ygo::core::load_fixture_deck(YGO_M0_PLAYER_B));
        host.start_duel();

        for (int step = 0; step < 256; ++step) {
            const auto result = host.process();
            const auto decoded = ygo::protocol::decode_messages(result.message);
            if (decoded.terminal) {
                std::cerr << "fixture reached terminal state before first decision\n";
                return 1;
            }
            if (decoded.interactive && !decoded.decisions.empty()) {
                ygo::protocol::validate_candidate_set(decoded.decisions.front());
                if (decoded.decisions.front().candidates.empty()) {
                    std::cerr << "first decision has no candidates\n";
                    return 1;
                }
                const auto& candidate = decoded.decisions.front().candidates.front();
                host.submit_response(candidate.exact_response_bytes);
                (void)host.process();
                return 0;
            }
        }
        std::cerr << "fixture did not reach a typed decision\n";
        return 1;
    } catch (const ygo::protocol::ProtocolError& error) {
        std::cerr << "protocol error: " << error.what() << " type="
                  << static_cast<unsigned>(error.message_type()) << " player="
                  << static_cast<unsigned>(error.player()) << '\n';
        return 1;
    } catch (const ygo::core::CoreError& error) {
        std::cerr << "core error: " << error.what() << '\n';
        return 1;
    } catch (const std::exception& error) {
        std::cerr << "unexpected error: " << error.what() << '\n';
        return 1;
    }
}
