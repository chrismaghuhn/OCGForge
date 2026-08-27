#include "ygo/environment/episodic_environment.hpp"

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <limits>
#include <stdexcept>
#include <utility>

#include "ygo/core/rules_bundle.hpp"
#include "ygo/core/seed_bundle.hpp"
#include "ygo/environment/identity_contract.hpp"
#include "ygo/m3/canonical_rules.hpp"
#include "ygo/trace/sha256.hpp"

namespace ygo::environment {
namespace {

void append_u8(std::vector<std::uint8_t>& bytes, const std::uint8_t value) {
    bytes.push_back(value);
}

void append_u32be(std::vector<std::uint8_t>& bytes, const std::uint32_t value) {
    bytes.push_back(static_cast<std::uint8_t>(value >> 24));
    bytes.push_back(static_cast<std::uint8_t>(value >> 16));
    bytes.push_back(static_cast<std::uint8_t>(value >> 8));
    bytes.push_back(static_cast<std::uint8_t>(value));
}

void append_u64be(std::vector<std::uint8_t>& bytes, const std::uint64_t value) {
    for (int shift = 56; shift >= 0; shift -= 8) {
        bytes.push_back(static_cast<std::uint8_t>(value >> shift));
    }
}

void append_length(std::vector<std::uint8_t>& bytes, const std::size_t value) {
    if (value > std::numeric_limits<std::uint32_t>::max()) {
        throw std::length_error("episodic identity field exceeds u32 length");
    }
    append_u32be(bytes, static_cast<std::uint32_t>(value));
}

void append_string(std::vector<std::uint8_t>& bytes, const std::string_view value) {
    append_length(bytes, value.size());
    bytes.insert(bytes.end(), value.begin(), value.end());
}

void append_u64_vector(std::vector<std::uint8_t>& bytes,
                       const std::vector<std::uint64_t>& values) {
    append_length(bytes, values.size());
    for (const auto value : values) {
        append_u64be(bytes, value);
    }
}

void append_deck_vector(std::vector<std::uint8_t>& bytes,
                        const std::vector<CertifiedDeckIdentity>& decks) {
    append_length(bytes, decks.size());
    for (const auto& deck : decks) {
        append_string(bytes, deck.id);
        append_string(bytes, deck.sha256);
    }
}

std::uint8_t seat_assignment_value(const SeatAssignment assignment) {
    switch (assignment) {
    case SeatAssignment::Normal:
        return 0;
    case SeatAssignment::Mirror:
        return 1;
    }
    throw std::invalid_argument("invalid episodic seat assignment");
}

std::vector<CertifiedDeckIdentity> resolved_seat_decks(const CertifiedEnvironmentConfig& config,
                                                       const SeatAssignment assignment) {
    if (config.locked_decks.size() != 2) {
        throw std::invalid_argument("certified environment must contain exactly two locked decks");
    }
    if (assignment == SeatAssignment::Normal) {
        return config.locked_decks;
    }
    if (assignment == SeatAssignment::Mirror) {
        return {config.locked_decks[1], config.locked_decks[0]};
    }
    throw std::invalid_argument("invalid episodic seat assignment");
}

}  // namespace

CertifiedEnvironmentConfig CertifiedEnvironmentConfig::canonical() {
#ifndef YGO_M0_SOURCE_DIR
#error "YGO_M0_SOURCE_DIR must be supplied by CMake"
#endif
#ifndef YGO_M0_CARD_DATA_TSV
#error "YGO_M0_CARD_DATA_TSV must be supplied by CMake"
#endif
#ifndef YGO_M0_CARDSCRIPTS_ROOT
#error "YGO_M0_CARDSCRIPTS_ROOT must be supplied by CMake"
#endif
#ifndef YGO_M0_CORE_API_VERSION
#error "YGO_M0_CORE_API_VERSION must be supplied by the rules lock"
#endif
#ifndef YGO_M0_CORE_COMMIT
#error "YGO_M0_CORE_COMMIT must be supplied by the rules lock"
#endif
#ifndef YGO_M0_CORE_RESOLVED_CHECKOUT_SHA256
#error "YGO_M0_CORE_RESOLVED_CHECKOUT_SHA256 must be supplied by the rules lock"
#endif
#ifndef YGO_M0_CARDSCRIPTS_COMMIT
#error "YGO_M0_CARDSCRIPTS_COMMIT must be supplied by the rules lock"
#endif
#ifndef YGO_M0_CARDSCRIPTS_RESOLVED_CHECKOUT_SHA256
#error "YGO_M0_CARDSCRIPTS_RESOLVED_CHECKOUT_SHA256 must be supplied by the rules lock"
#endif
#ifndef YGO_M0_DATABASE_COMMIT
#error "YGO_M0_DATABASE_COMMIT must be supplied by the rules lock"
#endif
#ifndef YGO_M0_DATABASE_RESOLVED_CHECKOUT_SHA256
#error "YGO_M0_DATABASE_RESOLVED_CHECKOUT_SHA256 must be supplied by the rules lock"
#endif
#ifndef YGO_M0_DATABASE_ARTIFACT_SHA256
#error "YGO_M0_DATABASE_ARTIFACT_SHA256 must be supplied by the rules lock"
#endif

    const auto& rules = m3::canonical_rules();
    CertifiedEnvironmentConfig config;
    config.rules_bundle_id = std::string(rules.rules_bundle_id);
    config.core_api_version = YGO_M0_CORE_API_VERSION;
    config.ocgcore_commit = YGO_M0_CORE_COMMIT;
    config.ocgcore_resolved_checkout_sha256 = YGO_M0_CORE_RESOLVED_CHECKOUT_SHA256;
    config.core_patchset_id = std::string(rules.core_patchset_id);
    config.core_patchset_sha256 = std::string(rules.core_patchset_sha256);
    config.cardscripts_commit = YGO_M0_CARDSCRIPTS_COMMIT;
    config.cardscripts_resolved_checkout_sha256 = YGO_M0_CARDSCRIPTS_RESOLVED_CHECKOUT_SHA256;
    config.database_commit = YGO_M0_DATABASE_COMMIT;
    config.database_resolved_checkout_sha256 = YGO_M0_DATABASE_RESOLVED_CHECKOUT_SHA256;
    config.database_artifact_sha256 = YGO_M0_DATABASE_ARTIFACT_SHA256;
    config.format_id = std::string(rules.format_id);
    config.duel_mode = std::string(rules.duel_mode_name);
    config.duel_flags = rules.duel_flags;
    config.locked_decks = {
        {"ocgforge.swordsoul_tenyi.ml_v1",
         "8ee4b699de19ff256e388d46f35b8696a60ff6ec59f0324f060a2468876711b7"},
        {"ocgforge.salamangreat.ml_v1",
         "6041abe0a59463d0715ae1da9100090ad487de02a02794e8ec0686d4c0513188"},
    };

    const auto source_root = std::filesystem::path(YGO_M0_SOURCE_DIR);
    const auto deck_a = core::load_fixture_deck(
        source_root / "fixtures" / "decks" / "swordsoul_tenyi_ml_v1.ydk");
    const auto deck_b = core::load_fixture_deck(
        source_root / "fixtures" / "decks" / "salamangreat_ml_v1.ydk");
    RequiredScriptClosureInput closure;
    closure.card_scripts_commit = config.cardscripts_commit;
    closure.card_scripts_tree_sha256 = config.cardscripts_resolved_checkout_sha256;
    closure.script_resolution_contract_id = std::string(kScriptResolutionContractId);
    closure.required_global_script_names.assign(kRequiredGlobalScriptNames.begin(),
                                                kRequiredGlobalScriptNames.end());
    closure.required_script_codes = core::canonical_required_script_codes(deck_a, deck_b);
    config.required_script_closure_identity =
        ::ygo::environment::required_script_closure_identity(closure);
    config.environment_semantic_id = ::ygo::environment::environment_semantic_id(config);
    return config;
}

std::vector<std::uint8_t> canonical_environment_identity_bytes(
    const CertifiedEnvironmentConfig& config) {
    std::vector<std::uint8_t> bytes;
    bytes.reserve(1400);
    append_string(bytes, kEnvironmentIdentitySchemaId);
    append_string(bytes, kEnvironmentIdentitySchemaId);
    append_string(bytes, config.contract_id);
    append_string(bytes, config.decision_contract_id);
    append_string(bytes, config.observation_contract_id);
    append_string(bytes, config.action_identity_schema_id);
    append_string(bytes, config.candidate_digest_schema_id);
    append_string(bytes, config.episode_identity_schema_id);
    append_string(bytes, config.decision_identity_schema_id);
    append_string(bytes, config.seed_derivation_id);
    append_string(bytes, config.rules_bundle_id);
    append_string(bytes, config.core_api_version);
    append_string(bytes, config.ocgcore_commit);
    append_string(bytes, config.ocgcore_resolved_checkout_sha256);
    append_string(bytes, config.core_patchset_id);
    append_string(bytes, config.core_patchset_sha256);
    append_string(bytes, config.cardscripts_commit);
    append_string(bytes, config.cardscripts_resolved_checkout_sha256);
    append_string(bytes, config.database_commit);
    append_string(bytes, config.database_resolved_checkout_sha256);
    append_string(bytes, config.database_artifact_sha256);
    append_string(bytes, config.format_id);
    append_string(bytes, config.duel_mode);
    append_u64be(bytes, config.duel_flags);
    append_deck_vector(bytes, config.locked_decks);
    append_string(bytes, config.required_script_closure_identity);
    return bytes;
}

std::string environment_semantic_id(const CertifiedEnvironmentConfig& config) {
    return trace::sha256_bytes(canonical_environment_identity_bytes(config));
}

std::vector<std::uint8_t> canonical_episode_identity_bytes(
    const CertifiedEnvironmentConfig& config, const EpisodeSpec& spec) {
    if (spec.starting_player > 1) {
        throw std::invalid_argument("episodic starting player must be 0 or 1");
    }
    const auto seat_decks = resolved_seat_decks(config, spec.seat_assignment);
    const auto seed = core::derive_seed_bundle(spec.root_seed);
    std::vector<std::uint8_t> bytes;
    bytes.reserve(450);
    append_string(bytes, kEpisodeIdentitySchemaId);
    append_string(bytes, kEpisodeIdentitySchemaId);
    append_string(bytes, environment_semantic_id(config));
    append_u64be(bytes, spec.root_seed);
    append_u64_vector(bytes, {seed.words[0], seed.words[1], seed.words[2], seed.words[3]});
    append_u8(bytes, seat_assignment_value(spec.seat_assignment));
    append_u8(bytes, spec.starting_player);
    append_deck_vector(bytes, seat_decks);
    return bytes;
}

std::string episode_semantic_id(const CertifiedEnvironmentConfig& config, const EpisodeSpec& spec) {
    return trace::sha256_bytes(canonical_episode_identity_bytes(config, spec));
}

std::vector<std::uint8_t> canonical_semantic_decision_identity_bytes(
    const std::string_view episode_id, const std::uint64_t decision_index,
    const std::string_view protocol_decision_id, const std::uint8_t acting_player,
    const std::uint64_t engine_step_index, const std::string_view observation_hash,
    const std::string_view candidate_digest) {
    if (acting_player > 1) {
        throw std::invalid_argument("episodic acting player must be 0 or 1");
    }
    std::vector<std::uint8_t> bytes;
    bytes.reserve(256);
    append_string(bytes, kSemanticDecisionIdentitySchemaId);
    append_string(bytes, kSemanticDecisionIdentitySchemaId);
    append_string(bytes, episode_id);
    append_u64be(bytes, decision_index);
    append_string(bytes, protocol_decision_id);
    append_u8(bytes, acting_player);
    append_u64be(bytes, engine_step_index);
    append_string(bytes, observation_hash);
    append_string(bytes, candidate_digest);
    return bytes;
}

std::string semantic_decision_id(const std::string_view episode_id, const std::uint64_t decision_index,
                                 const std::string_view protocol_decision_id,
                                 const std::uint8_t acting_player,
                                 const std::uint64_t engine_step_index,
                                 const std::string_view observation_hash,
                                 const std::string_view candidate_digest) {
    return trace::sha256_bytes(canonical_semantic_decision_identity_bytes(
        episode_id, decision_index, protocol_decision_id, acting_player, engine_step_index,
        observation_hash, candidate_digest));
}

}  // namespace ygo::environment
