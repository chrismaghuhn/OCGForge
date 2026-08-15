#include "ygo/simulation/simulation_contract.hpp"

int main() {
    ygo::simulation::CanonicalSimulationConfig config;
    if (!ygo::simulation::is_canonical_identity(config)) {
        return 1;
    }

    auto invalid = config;
    invalid.format = "TCG_ADVANCED_INVALID";
    if (ygo::simulation::is_canonical_identity(invalid)) {
        return 2;
    }

    invalid = config;
    invalid.duel_mode = "DUEL_MODE_INVALID";
    if (ygo::simulation::is_canonical_identity(invalid)) {
        return 3;
    }

    invalid = config;
    invalid.duel_flags ^= 1U;
    if (ygo::simulation::is_canonical_identity(invalid)) {
        return 4;
    }

    invalid = config;
    invalid.rules_bundle_id = "rules-invalid";
    if (ygo::simulation::is_canonical_identity(invalid)) {
        return 5;
    }

    invalid = config;
    invalid.patchset_sha256 = "patchset-invalid";
    if (ygo::simulation::is_canonical_identity(invalid)) {
        return 6;
    }

    invalid = config;
    invalid.locked_deck_hashes[0] = ygo::simulation::kCanonicalDeckBSha256;
    invalid.locked_deck_hashes[1] = ygo::simulation::kCanonicalDeckASha256;
    if (ygo::simulation::is_canonical_identity(invalid)) {
        return 7;
    }

    return 0;
}
