#include <cstdint>
#include <exception>
#include <iostream>
#include <stdexcept>

#include "ocgapi_constants.h"
#include "ocgapi_types.h"
#include "ygo/core/core_host.hpp"

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

int run() {
    ygo::core::CoreHostConfig config;
    config.rules.card_scripts_root = YGO_M0_CARDSCRIPTS;
    config.rules.card_data_tsv = YGO_M0_CARD_DATA_TSV;
    config.rules.bundle_id = "6fbbd212ae4be2df36170dcbfcdf5c46aaaa0e3091cf815c2d0261fd01640ea4";
    config.seed.words = {0x0123456789abcdefULL, 0xfedcba9876543210ULL, 0x13579bdf2468ace0ULL,
                         0x0eca8642fdb97531ULL};

    ygo::core::CoreHost host(config);
    host.load_deck(0, ygo::core::load_fixture_deck(YGO_M0_PLAYER_A));
    host.load_deck(1, ygo::core::load_fixture_deck(YGO_M0_PLAYER_B));
    host.start_duel();
    (void)host.process();

    OCG_QueryInfo info{};
    info.flags = QUERY_CODE | QUERY_END;
    info.con = 0;
    info.loc = LOCATION_HAND;
    info.seq = 0;
    (void)host.query(info);
    (void)host.query_location(info);
    (void)host.query_field();
    (void)host.query_count(0, LOCATION_DECK);

    require(host.response_submission_count() == 0,
            "response_submission_count changed without submit_response");
    const auto metrics = host.metrics();
    require(metrics.duel_process_calls == 1, "duel_process_calls did not count the process call");
    require(metrics.duel_process_calls == host.process_call_count(),
            "duel_process_calls diverged from process_call_count");
    require(metrics.duel_query_calls == 1, "duel_query_calls did not count the query call");
    require(metrics.duel_query_location_calls == 1,
            "duel_query_location_calls did not count the query_location call");
    require(metrics.duel_query_field_calls == 1, "duel_query_field_calls did not count the query_field call");
    require(metrics.duel_query_count_calls == 1, "duel_query_count_calls did not count the query_count call");
    require(metrics.script_reader_requests > 0, "M0 fixture did not exercise the script-reader callback");
    require(metrics.script_loads >= 3, "global M0 scripts were not counted as successful loads");
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
