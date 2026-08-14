#include <cstdint>
#include <exception>
#include <iostream>
#include <stdexcept>

#include "common.h"
#include "zone_projection.hpp"

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void require_zone(std::uint32_t location, std::uint8_t controller, std::uint32_t sequence,
                  std::uint64_t flags, ygo::observation::SemanticZone expected) {
    require(ygo::observation::detail::project_zone(location, controller, sequence, flags).zone == expected,
            "semantic zone projection mismatch");
}

int run() {
    using ygo::observation::SemanticZone;
    require_zone(LOCATION_DECK, 0, 7, 0, SemanticZone::MainDeck);
    require_zone(LOCATION_HAND, 1, 2, 0, SemanticZone::Hand);
    require_zone(LOCATION_MZONE, 1, 5, 0, SemanticZone::MonsterZone);
    require_zone(LOCATION_SZONE, 1, 5, 0, SemanticZone::FieldZone);
    require_zone(LOCATION_SZONE, 1, 2, 0, SemanticZone::SpellTrapZone);
    require_zone(LOCATION_GRAVE, 1, 0, 0, SemanticZone::Graveyard);
    require_zone(LOCATION_REMOVED, 1, 0, 0, SemanticZone::Banished);
    require_zone(LOCATION_EXTRA, 1, 0, 0, SemanticZone::ExtraDeck);
    require_zone(LOCATION_OVERLAY, 1, 0, 0, SemanticZone::Overlay);

    require_zone(LOCATION_PZONE, 0, 0, DUEL_PZONE, SemanticZone::PendulumRelevant);
    require_zone(LOCATION_FZONE, 0, 0, 0, SemanticZone::FieldZone);
    require_zone(LOCATION_STZONE, 0, 0, 0, SemanticZone::SpellTrapZone);
    require_zone(LOCATION_MMZONE, 0, 0, 0, SemanticZone::MonsterZone);
    require_zone(LOCATION_EMZONE, 0, 0, 0, SemanticZone::MonsterZone);

    require_zone(LOCATION_SZONE, 0, 0, DUEL_PZONE, SemanticZone::PendulumRelevant);
    require_zone(LOCATION_SZONE, 0, 4, DUEL_PZONE, SemanticZone::PendulumRelevant);
    require_zone(LOCATION_SZONE, 0, 6, DUEL_PZONE | DUEL_SEPARATE_PZONE, SemanticZone::PendulumRelevant);
    require_zone(LOCATION_SZONE, 0, 1, DUEL_PZONE | DUEL_3_COLUMNS_FIELD, SemanticZone::PendulumRelevant);
    require_zone(LOCATION_SZONE, 0, 1, DUEL_PZONE | DUEL_3_COLUMNS_FIELD, SemanticZone::PendulumRelevant);
    require_zone(LOCATION_SZONE, 0, 1, DUEL_3_COLUMNS_FIELD, SemanticZone::SpellTrapZone);
    require_zone(LOCATION_MZONE | LOCATION_SZONE, 0, 0, 0, SemanticZone::Unknown);
    std::cout << "zone_projection=ok\n";
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
