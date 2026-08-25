#include "ygo/simulation/simulation_contract.hpp"

namespace {

ygo::simulation::CanonicalSimulationConfig valid_config() {
    ygo::simulation::CanonicalSimulationConfig config;
    config.rules.bundle_id = ygo::simulation::kCanonicalRulesBundleId;
    config.rules.core_commit = "9a0c558c2d686542f7914a6d529fd7aa57746aed";
    config.rules.cardscripts_commit =
        "f337c87018ca723c1aded5143e616bb649555273";
    config.rules.database_commit = "89ad6837b0766a52984d8c715a7d5d4f8447946b";
    config.rules.core_patchset_id = ygo::simulation::kCanonicalPatchsetId;
    config.rules.core_patchset_sha256 = ygo::simulation::kCanonicalPatchsetSha256;
    config.deck_a.sha256 = ygo::simulation::kCanonicalDeckASha256;
    config.deck_b.sha256 = ygo::simulation::kCanonicalDeckBSha256;
    return config;
}

}  // namespace

int main() {
    const auto config = valid_config();
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
    invalid.patchset_id = "patchset-invalid";
    if (ygo::simulation::is_canonical_identity(invalid)) {
        return 6;
    }

    invalid = config;
    invalid.patchset_sha256 = "patchset-invalid";
    if (ygo::simulation::is_canonical_identity(invalid)) {
        return 7;
    }

    invalid = config;
    invalid.locked_deck_hashes[0] = "deck-a-invalid";
    if (ygo::simulation::is_canonical_identity(invalid)) {
        return 8;
    }

    invalid = config;
    invalid.rules.bundle_id = "rules-payload-invalid";
    if (ygo::simulation::is_canonical_identity(invalid)) {
        return 9;
    }

    invalid = config;
    invalid.rules.core_commit = "core-commit-invalid";
    if (ygo::simulation::is_canonical_identity(invalid)) {
        return 10;
    }

    invalid = config;
    invalid.rules.cardscripts_commit = "cardscripts-commit-invalid";
    if (ygo::simulation::is_canonical_identity(invalid)) {
        return 11;
    }

    invalid = config;
    invalid.rules.database_commit = "database-commit-invalid";
    if (ygo::simulation::is_canonical_identity(invalid)) {
        return 12;
    }

    invalid = config;
    invalid.rules.core_patchset_id = "patchset-payload-invalid";
    if (ygo::simulation::is_canonical_identity(invalid)) {
        return 13;
    }

    invalid = config;
    invalid.rules.core_patchset_sha256 = "patchset-sha-payload-invalid";
    if (ygo::simulation::is_canonical_identity(invalid)) {
        return 14;
    }

    invalid = config;
    invalid.deck_a.sha256 = "deck-a-payload-invalid";
    if (ygo::simulation::is_canonical_identity(invalid)) {
        return 15;
    }

    invalid = config;
    invalid.deck_b.sha256 = "deck-b-payload-invalid";
    if (ygo::simulation::is_canonical_identity(invalid)) {
        return 16;
    }

    return 0;
}
