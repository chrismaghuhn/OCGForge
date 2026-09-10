#include "ygo/trajectory/identity_resolver.hpp"

#include <array>
#include <utility>

#include "ygo/core/seed_bundle.hpp"
#include "ygo/trace/sha256.hpp"

namespace ygo::trajectory {
namespace {

template <typename T>
DecodeResult<T> error(std::string message) noexcept {
    DecodeResult<T> result;
    result.error = DecodeError{std::move(message)};
    return result;
}

template <typename T>
DecodeResult<T> ok(T value) noexcept {
    DecodeResult<T> result;
    result.value = std::move(value);
    return result;
}

bool read_decks(ByteReader& reader,
                std::vector<environment::CertifiedDeckIdentity>& decks) noexcept {
    std::uint32_t count = 0;
    if (!reader.u32be(count)) {
        return false;
    }
    if (count > reader.remaining() / 8) {
        return false;
    }
    try {
        decks.clear();
        decks.reserve(count);
    } catch (...) {
        return false;
    }
    for (std::uint32_t index = 0; index < count; ++index) {
        environment::CertifiedDeckIdentity deck;
        if (!reader.string(deck.id) || !reader.string(deck.sha256)) {
            return false;
        }
        decks.push_back(std::move(deck));
    }
    return true;
}

bool read_u64_vector(ByteReader& reader, std::array<std::uint64_t, 4>& words) noexcept {
    std::uint32_t count = 0;
    if (!reader.u32be(count) || count != words.size()) {
        return false;
    }
    for (auto& word : words) {
        if (!reader.u64be(word)) {
            return false;
        }
    }
    return true;
}

DecodeResult<environment::CertifiedEnvironmentConfig> decode_environment_identity_input_impl(
    const std::vector<std::uint8_t>& bytes,
    const std::string_view schema_id,
    const std::string_view contract_id) noexcept {
    try {
        ByteReader reader(bytes);
        environment::CertifiedEnvironmentConfig config;
        std::string schema;
        if (!reader.string(schema) || schema != schema_id ||
            !reader.string(schema) || schema != schema_id ||
            !reader.string(config.contract_id) || config.contract_id != contract_id ||
            !reader.string(config.decision_contract_id) ||
            !reader.string(config.observation_contract_id) ||
            !reader.string(config.action_identity_schema_id) ||
            !reader.string(config.public_action_identity_schema_id) ||
            !reader.string(config.candidate_digest_schema_id) ||
            !reader.string(config.public_candidate_digest_schema_id) ||
            !reader.string(config.episode_identity_schema_id) ||
            !reader.string(config.decision_identity_schema_id) ||
            !reader.string(config.public_decision_identity_schema_id) ||
            !reader.string(config.public_observation_contract_id) ||
            !reader.string(config.public_safe_state_schema_id) ||
            !reader.string(config.seed_derivation_id) || !reader.string(config.rules_bundle_id) ||
            !reader.string(config.core_api_version) || !reader.string(config.ocgcore_commit) ||
            !reader.string(config.ocgcore_resolved_checkout_sha256) ||
            !reader.string(config.core_patchset_id) ||
            !reader.string(config.core_patchset_sha256) ||
            !reader.string(config.cardscripts_commit) ||
            !reader.string(config.cardscripts_resolved_checkout_sha256) ||
            !reader.string(config.database_commit) ||
            !reader.string(config.database_resolved_checkout_sha256) ||
            !reader.string(config.database_artifact_sha256) || !reader.string(config.format_id) ||
            !reader.string(config.duel_mode) || !reader.u64be(config.duel_flags) ||
            !read_decks(reader, config.locked_decks) ||
            !reader.string(config.required_script_closure_identity) || !reader.at_end()) {
            return error<environment::CertifiedEnvironmentConfig>(
                "malformed environment identity input");
        }
        config.environment_semantic_id = environment::environment_semantic_id(config);
        if (environment::canonical_environment_identity_bytes(config) != bytes) {
            return error<environment::CertifiedEnvironmentConfig>(
                "noncanonical environment identity input");
        }
        return ok(std::move(config));
    } catch (const std::exception& exception) {
        return error<environment::CertifiedEnvironmentConfig>(exception.what());
    } catch (...) {
        return error<environment::CertifiedEnvironmentConfig>("environment identity decode threw");
    }
}

DecodeResult<environment::EpisodeSpec> decode_episode_identity_input_impl(
    const std::vector<std::uint8_t>& bytes,
    const environment::CertifiedEnvironmentConfig& config,
    const std::string_view contract_id) noexcept {
    try {
        if (config.contract_id != contract_id) {
            return error<environment::EpisodeSpec>("episode identity environment generation mismatch");
        }
        ByteReader reader(bytes);
        environment::EpisodeSpec spec;
        std::string schema;
        std::string environment_id;
        std::array<std::uint64_t, 4> words{};
        std::uint8_t seat = 0;
        std::vector<environment::CertifiedDeckIdentity> spec_decks;
        if (!reader.string(schema) || schema != environment::kEpisodeIdentitySchemaId ||
            !reader.string(schema) || schema != environment::kEpisodeIdentitySchemaId ||
            !reader.string(environment_id) || !reader.u64be(spec.root_seed) ||
            !read_u64_vector(reader, words) || !reader.u8(seat) || seat > 1 ||
            !reader.u8(spec.starting_player) || spec.starting_player > 1 ||
            !read_decks(reader, spec_decks) || !reader.at_end()) {
            return error<environment::EpisodeSpec>("malformed episode identity input");
        }
        if (environment_id != environment::environment_semantic_id(config) ||
            words != core::derive_seed_bundle(spec.root_seed).words) {
            return error<environment::EpisodeSpec>(
                "episode identity inputs disagree with environment");
        }
        spec.contract_id = std::string(contract_id);
        spec.seat_assignment = static_cast<environment::SeatAssignment>(seat);
        std::vector<environment::CertifiedDeckIdentity> expected_decks = config.locked_decks;
        if (spec.seat_assignment == environment::SeatAssignment::Mirror) {
            std::swap(expected_decks[0], expected_decks[1]);
        }
        if (spec_decks.size() != expected_decks.size()) {
            return error<environment::EpisodeSpec>(
                "episode deck identity does not match seat assignment");
        }
        for (std::size_t index = 0; index < spec_decks.size(); ++index) {
            if (spec_decks[index].id != expected_decks[index].id ||
                spec_decks[index].sha256 != expected_decks[index].sha256) {
                return error<environment::EpisodeSpec>(
                    "episode deck identity does not match seat assignment");
            }
        }
        if (environment::canonical_episode_identity_bytes(config, spec) != bytes) {
            return error<environment::EpisodeSpec>("noncanonical episode identity input");
        }
        return ok(std::move(spec));
    } catch (const std::exception& exception) {
        return error<environment::EpisodeSpec>(exception.what());
    } catch (...) {
        return error<environment::EpisodeSpec>("episode identity decode threw");
    }
}

}  // namespace

DecodeResult<environment::CertifiedEnvironmentConfig> decode_environment_identity_input(
    const std::vector<std::uint8_t>& bytes) noexcept {
    return decode_environment_identity_input_impl(
        bytes, environment::kEnvironmentIdentityV2SchemaId,
        environment::kEpisodicEnvironmentV2ContractId);
}

DecodeResult<environment::CertifiedEnvironmentConfig> decode_environment_identity_input_v3(
    const std::vector<std::uint8_t>& bytes) noexcept {
    return decode_environment_identity_input_impl(
        bytes, environment::kEnvironmentIdentityV3SchemaId,
        environment::kEpisodicEnvironmentV3ContractId);
}

DecodeResult<environment::CertifiedEnvironmentConfig> decode_environment_identity_input_v4(
    const std::vector<std::uint8_t>& bytes) noexcept {
    return decode_environment_identity_input_impl(
        bytes, environment::kEnvironmentIdentityV4SchemaId,
        environment::kEpisodicEnvironmentV4ContractId);
}

DecodeResult<environment::EpisodeSpec> decode_episode_identity_input(
    const std::vector<std::uint8_t>& bytes,
    const environment::CertifiedEnvironmentConfig& config) noexcept {
    return decode_episode_identity_input_impl(
        bytes, config, environment::kEpisodicEnvironmentV2ContractId);
}

DecodeResult<environment::EpisodeSpec> decode_episode_identity_input_v3(
    const std::vector<std::uint8_t>& bytes,
    const environment::CertifiedEnvironmentConfig& config) noexcept {
    return decode_episode_identity_input_impl(
        bytes, config, environment::kEpisodicEnvironmentV3ContractId);
}

DecodeResult<environment::EpisodeSpec> decode_episode_identity_input_v4(
    const std::vector<std::uint8_t>& bytes,
    const environment::CertifiedEnvironmentConfig& config) noexcept {
    return decode_episode_identity_input_impl(
        bytes, config, environment::kEpisodicEnvironmentV4ContractId);
}

bool is_current_certified_environment(
    const environment::CertifiedEnvironmentConfig& config) noexcept {
    try {
        if (config.contract_id != environment::kEpisodicEnvironmentV2ContractId) {
            return false;
        }
        const auto canonical = environment::CertifiedEnvironmentConfig::canonical();
        return environment::canonical_environment_identity_bytes(config) ==
               environment::canonical_environment_identity_bytes(canonical);
    } catch (...) {
        return false;
    }
}

bool is_current_certified_environment_v3(
    const environment::CertifiedEnvironmentConfig& config) noexcept {
    try {
        if (config.contract_id != environment::kEpisodicEnvironmentV3ContractId) {
            return false;
        }
        const auto canonical = environment::CertifiedEnvironmentConfig::canonical_v3();
        return environment::canonical_environment_identity_bytes(config) ==
               environment::canonical_environment_identity_bytes(canonical);
    } catch (...) {
        return false;
    }
}

bool is_current_certified_environment_v4(
    const environment::CertifiedEnvironmentConfig& config) noexcept {
    try {
        if (config.contract_id != environment::kEpisodicEnvironmentV4ContractId) {
            return false;
        }
        const auto canonical = environment::CertifiedEnvironmentConfig::canonical_v4();
        return environment::canonical_environment_identity_bytes(config) ==
               environment::canonical_environment_identity_bytes(canonical);
    } catch (...) {
        return false;
    }
}

}  // namespace ygo::trajectory
