#include "public_core_harness.hpp"

namespace m35::core_test {

int PublicCoreDuel::set_starting_player(std::uint8_t player) {
    return OCG_DuelSetStartingPlayer(handle(), player);
}

}  // namespace m35::core_test
