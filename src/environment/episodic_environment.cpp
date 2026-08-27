#include "ygo/environment/episodic_environment.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <stdexcept>
#include <utility>
#include <variant>

#include "common.h"
#include "ygo/core/core_error.hpp"
#include "ygo/core/rules_bundle.hpp"
#include "ygo/environment/episode_driver.hpp"
#include "ygo/environment/episodic_environment_test_access.hpp"
#include "ygo/core/seed_bundle.hpp"
#include "ygo/environment/identity_contract.hpp"
#include "ygo/m3/canonical_rules.hpp"
#include "ygo/observation/decision_integration.hpp"
#include "ygo/observation/serialization.hpp"
#include "ygo/protocol/message_decoder.hpp"
#include "ygo/protocol/protocol_error.hpp"
#include "ygo/trace/engine_trace.hpp"
#include "ygo/trace/sha256.hpp"
#include "../observation/zone_projection.hpp"

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

struct BoundaryError final : std::runtime_error {
    BoundaryError(const FailureCode code, const FailureStage stage, std::string message,
                  const bool mutation_may_have_occurred = false)
        : std::runtime_error(std::move(message)), failure_code(code), failure_stage(stage),
          mutation_may_have_occurred(mutation_may_have_occurred) {}

    FailureCode failure_code;
    FailureStage failure_stage;
    bool mutation_may_have_occurred;
};

std::string sha256_file(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        throw std::runtime_error("cannot open certified database artifact");
    }
    std::vector<std::uint8_t> bytes;
    std::array<char, 8192> buffer{};
    while (stream) {
        stream.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const auto count = stream.gcount();
        if (count > 0) {
            bytes.insert(bytes.end(), reinterpret_cast<const std::uint8_t*>(buffer.data()),
                         reinterpret_cast<const std::uint8_t*>(buffer.data()) + count);
        }
    }
    return trace::sha256_bytes(bytes);
}

std::vector<std::filesystem::path> certified_deck_paths() {
#ifndef YGO_M0_SOURCE_DIR
#error "YGO_M0_SOURCE_DIR must be supplied by CMake"
#endif
    const auto source_root = std::filesystem::path(YGO_M0_SOURCE_DIR);
    return {source_root / "fixtures" / "decks" / "swordsoul_tenyi_ml_v1.ydk",
            source_root / "fixtures" / "decks" / "salamangreat_ml_v1.ydk"};
}

std::vector<core::FixtureDeck> load_certified_decks() {
    const auto paths = certified_deck_paths();
    return {core::load_fixture_deck(paths[0]), core::load_fixture_deck(paths[1])};
}

bool equal_deck_identity(const CertifiedDeckIdentity& left, const CertifiedDeckIdentity& right) {
    return left.id == right.id && left.sha256 == right.sha256;
}

bool equal_config(const CertifiedEnvironmentConfig& left, const CertifiedEnvironmentConfig& right) {
    if (left.contract_id != right.contract_id || left.environment_semantic_id != right.environment_semantic_id ||
        left.decision_contract_id != right.decision_contract_id ||
        left.observation_contract_id != right.observation_contract_id ||
        left.action_identity_schema_id != right.action_identity_schema_id ||
        left.public_action_identity_schema_id != right.public_action_identity_schema_id ||
        left.candidate_digest_schema_id != right.candidate_digest_schema_id ||
        left.public_candidate_digest_schema_id != right.public_candidate_digest_schema_id ||
        left.episode_identity_schema_id != right.episode_identity_schema_id ||
        left.decision_identity_schema_id != right.decision_identity_schema_id ||
        left.public_decision_identity_schema_id != right.public_decision_identity_schema_id ||
        left.public_observation_contract_id != right.public_observation_contract_id ||
        left.public_safe_state_schema_id != right.public_safe_state_schema_id ||
        left.seed_derivation_id != right.seed_derivation_id || left.rules_bundle_id != right.rules_bundle_id ||
        left.core_api_version != right.core_api_version || left.ocgcore_commit != right.ocgcore_commit ||
        left.ocgcore_resolved_checkout_sha256 != right.ocgcore_resolved_checkout_sha256 ||
        left.core_patchset_id != right.core_patchset_id || left.core_patchset_sha256 != right.core_patchset_sha256 ||
        left.cardscripts_commit != right.cardscripts_commit ||
        left.cardscripts_resolved_checkout_sha256 != right.cardscripts_resolved_checkout_sha256 ||
        left.database_commit != right.database_commit ||
        left.database_resolved_checkout_sha256 != right.database_resolved_checkout_sha256 ||
        left.database_artifact_sha256 != right.database_artifact_sha256 || left.format_id != right.format_id ||
        left.duel_mode != right.duel_mode || left.duel_flags != right.duel_flags ||
        left.required_script_closure_identity != right.required_script_closure_identity ||
        left.locked_decks.size() != right.locked_decks.size()) {
        return false;
    }
    for (std::size_t index = 0; index < left.locked_decks.size(); ++index) {
        if (!equal_deck_identity(left.locked_decks[index], right.locked_decks[index])) {
            return false;
        }
    }
    return true;
}

bool certified_resources_match(const CertifiedEnvironmentConfig& config) {
#ifndef YGO_M0_CARD_DATA_TSV
#error "YGO_M0_CARD_DATA_TSV must be supplied by CMake"
#endif
#ifndef YGO_M0_CARDSCRIPTS_ROOT
#error "YGO_M0_CARDSCRIPTS_ROOT must be supplied by CMake"
#endif
#ifndef YGO_M0_BABELCDB_ROOT
#error "YGO_M0_BABELCDB_ROOT must be supplied by CMake"
#endif
    try {
        const auto scripts_root = std::filesystem::path(YGO_M0_CARDSCRIPTS_ROOT);
        const auto card_data = std::filesystem::path(YGO_M0_CARD_DATA_TSV);
        const auto database = std::filesystem::path(YGO_M0_BABELCDB_ROOT) / "cards.cdb";
        if (!std::filesystem::is_directory(scripts_root) || std::filesystem::is_symlink(scripts_root) ||
            !std::filesystem::is_regular_file(card_data) || std::filesystem::is_symlink(card_data) ||
            !std::filesystem::is_regular_file(database) || std::filesystem::is_symlink(database) ||
            sha256_file(database) != config.database_artifact_sha256) {
            return false;
        }
        const auto decks = load_certified_decks();
        return config.locked_decks.size() == 2 && decks.size() == 2 &&
               decks[0].sha256 == config.locked_decks[0].sha256 &&
               decks[1].sha256 == config.locked_decks[1].sha256;
    } catch (const std::exception&) {
        return false;
    }
}

EpisodeDriverConfig make_driver_config(const CertifiedEnvironmentConfig& config,
                                       const EpisodeSpec& spec, const RunControl& control) {
    const auto decks = load_certified_decks();
    if (config.locked_decks.size() != 2 || decks.size() != 2 ||
        decks[0].sha256 != config.locked_decks[0].sha256 ||
        decks[1].sha256 != config.locked_decks[1].sha256) {
        throw BoundaryError(FailureCode::ResourceIdentityMismatch, FailureStage::Construction,
                            "certified deck identity does not match the environment configuration");
    }
    const auto seat_decks = spec.seat_assignment == SeatAssignment::Normal
                                ? decks
                                : std::vector<core::FixtureDeck>{decks[1], decks[0]};
    EpisodeDriverConfig driver;
    driver.rules.card_scripts_root = YGO_M0_CARDSCRIPTS_ROOT;
    driver.rules.card_data_tsv = YGO_M0_CARD_DATA_TSV;
    driver.rules.bundle_id = config.rules_bundle_id;
    driver.rules.core_api_version = config.core_api_version;
    driver.rules.core_commit = config.ocgcore_commit;
    driver.rules.cardscripts_commit = config.cardscripts_commit;
    driver.rules.database_commit = config.database_commit;
    driver.rules.core_patchset_id = config.core_patchset_id;
    driver.rules.core_patchset_sha256 = config.core_patchset_sha256;
    driver.player_zero_deck = seat_decks[0];
    driver.player_one_deck = seat_decks[1];
    driver.seed = spec.root_seed;
    driver.duel_flags = config.duel_flags;
    driver.starting_player = spec.starting_player;
    driver.engine_process_budget = control.engine_process_budget;
    driver.semantic_action_budget = control.semantic_action_budget;
    driver.build_full_observation = true;
    driver.required_script_codes = core::canonical_required_script_codes(seat_decks[0], seat_decks[1]);
    driver.fixture_setup_script.clear();
    driver.instrumentation = false;
    return driver;
}

FailureCode protocol_failure_code(const protocol::ProtocolErrorCode code, const std::string& message) {
    switch (code) {
    case protocol::ProtocolErrorCode::MalformedMessage:
        return FailureCode::MalformedProtocol;
    case protocol::ProtocolErrorCode::UnsupportedDecision:
        return FailureCode::UnsupportedProtocol;
    case protocol::ProtocolErrorCode::IncompleteCandidates:
        return message.find("duplicate") != std::string::npos ? FailureCode::DuplicateCandidates
                                                                : FailureCode::IncompleteCandidates;
    case protocol::ProtocolErrorCode::InvalidSemanticKey:
        return FailureCode::InternalDomainDivergence;
    }
    return FailureCode::InvalidAuthoritativeState;
}

FailureCode driver_failure_code(const DriverFailure& failure) {
    if (failure.failure_code == "retry") {
        return FailureCode::RetryFailure;
    }
    if (failure.failure_code == "core_error") {
        return FailureCode::CoreError;
    }
    if (failure.failure_code == "unsupported_protocol" || failure.failure_code == "forced_unsupported") {
        return FailureCode::UnsupportedProtocol;
    }
    if (failure.failure_code == "malformed_protocol") {
        return FailureCode::MalformedProtocol;
    }
    if (failure.failure_code == "candidate_truncated") {
        return FailureCode::IncompleteCandidates;
    }
    if (failure.failure_code == "response_inconsistency") {
        return FailureCode::ResponseInconsistency;
    }
    if (failure.failure_code == "invalid_authoritative_state") {
        return FailureCode::InvalidAuthoritativeState;
    }
    return FailureCode::InternalDomainDivergence;
}

FailureStage driver_failure_stage(const DriverFailure& failure) {
    if (failure.failure_stage == "action") {
        return FailureStage::Action;
    }
    if (failure.failure_stage == "teardown") {
        return FailureStage::Teardown;
    }
    if (failure.failure_stage == "projection") {
        return FailureStage::Projection;
    }
    return FailureStage::Advance;
}

bool starts_with(const std::string& value, const std::string& prefix) {
    return value.size() >= prefix.size() && value.compare(0, prefix.size(), prefix) == 0;
}

bool safe_source_label(const std::string& value) {
    if (value.size() > 128) {
        return false;
    }
    for (const unsigned char character : value) {
        if (character < 0x20 || character == 0x7f) {
            return false;
        }
    }
    return true;
}

}  // namespace

std::string_view environment_decision_kind_name(const EnvironmentDecisionKind kind) noexcept {
    switch (kind) {
    case EnvironmentDecisionKind::IdleCommand:
        return "idle_command";
    case EnvironmentDecisionKind::BattleCommand:
        return "battle_command";
    case EnvironmentDecisionKind::Chain:
        return "chain";
    case EnvironmentDecisionKind::Option:
        return "option";
    case EnvironmentDecisionKind::CardSelection:
        return "card_selection";
    case EnvironmentDecisionKind::Tribute:
        return "tribute";
    case EnvironmentDecisionKind::Sum:
        return "sum";
    case EnvironmentDecisionKind::Place:
        return "place";
    case EnvironmentDecisionKind::Counter:
        return "counter";
    case EnvironmentDecisionKind::Ordering:
        return "ordering";
    case EnvironmentDecisionKind::Announcement:
        return "announcement";
    case EnvironmentDecisionKind::UnselectCard:
        return "unselect_card";
    case EnvironmentDecisionKind::Position:
        return "position";
    case EnvironmentDecisionKind::YesNo:
        return "yes_no";
    case EnvironmentDecisionKind::Unsupported:
        return "unsupported";
    }
    return "unsupported";
}

std::string_view environment_action_kind_name(const EnvironmentActionKind kind) noexcept {
    switch (kind) {
    case EnvironmentActionKind::IdleCommand:
        return "idle_command";
    case EnvironmentActionKind::BattleCommand:
        return "battle_command";
    case EnvironmentActionKind::Chain:
        return "chain";
    case EnvironmentActionKind::Option:
        return "option";
    case EnvironmentActionKind::CardSelection:
        return "card_selection";
    case EnvironmentActionKind::Announcement:
        return "announcement";
    case EnvironmentActionKind::Place:
        return "place";
    case EnvironmentActionKind::Position:
        return "position";
    case EnvironmentActionKind::YesNo:
        return "yes_no";
    case EnvironmentActionKind::Pick:
        return "pick";
    case EnvironmentActionKind::Finish:
        return "finish";
    case EnvironmentActionKind::Cancel:
        return "cancel";
    case EnvironmentActionKind::AssignAmount:
        return "assign_amount";
    case EnvironmentActionKind::Unsupported:
        return "unsupported";
    }
    return "unsupported";
}

std::string_view interruption_reason_name(const InterruptionReason reason) noexcept {
    switch (reason) {
    case InterruptionReason::EngineProcessBudget:
        return "ENGINE_PROCESS_BUDGET";
    case InterruptionReason::SemanticActionBudget:
        return "SEMANTIC_ACTION_BUDGET";
    case InterruptionReason::AdministrativeCancel:
        return "ADMINISTRATIVE_CANCEL";
    }
    return "UNKNOWN";
}

std::string_view failure_code_name(const FailureCode code) noexcept {
    switch (code) {
    case FailureCode::RetryFailure:
        return "RETRY_FAILURE";
    case FailureCode::CoreError:
        return "CORE_ERROR";
    case FailureCode::UnsupportedProtocol:
        return "UNSUPPORTED_PROTOCOL";
    case FailureCode::MalformedProtocol:
        return "MALFORMED_PROTOCOL";
    case FailureCode::IncompleteCandidates:
        return "INCOMPLETE_CANDIDATES";
    case FailureCode::DuplicateCandidates:
        return "DUPLICATE_CANDIDATES";
    case FailureCode::ResponseInconsistency:
        return "RESPONSE_INCONSISTENCY";
    case FailureCode::CandidateObservationInconsistency:
        return "CANDIDATE_OBSERVATION_INCONSISTENCY";
    case FailureCode::PrivacyInvariant:
        return "PRIVACY_INVARIANT";
    case FailureCode::PublicFrameInvariant:
        return "PUBLIC_FRAME_INVARIANT";
    case FailureCode::InvalidAuthoritativeState:
        return "INVALID_AUTHORITATIVE_STATE";
    case FailureCode::ResponseSubmissionFailure:
        return "RESPONSE_SUBMISSION_FAILURE";
    case FailureCode::ObservationFailure:
        return "OBSERVATION_FAILURE";
    case FailureCode::InternalDomainDivergence:
        return "INTERNAL_DOMAIN_DIVERGENCE";
    case FailureCode::TokenNamespaceExhausted:
        return "TOKEN_NAMESPACE_EXHAUSTED";
    case FailureCode::ResourceIdentityMismatch:
        return "RESOURCE_IDENTITY_MISMATCH";
    }
    return "INVALID_AUTHORITATIVE_STATE";
}

std::string_view failure_stage_name(const FailureStage stage) noexcept {
    switch (stage) {
    case FailureStage::Validation:
        return "VALIDATION";
    case FailureStage::Construction:
        return "CONSTRUCTION";
    case FailureStage::Advance:
        return "ADVANCE";
    case FailureStage::Projection:
        return "PROJECTION";
    case FailureStage::Action:
        return "ACTION";
    case FailureStage::Interruption:
        return "INTERRUPTION";
    case FailureStage::Teardown:
        return "TEARDOWN";
    }
    return "VALIDATION";
}

std::string_view lifecycle_name(const Lifecycle lifecycle) noexcept {
    switch (lifecycle) {
    case Lifecycle::Empty:
        return "EMPTY";
    case Lifecycle::AwaitingAction:
        return "AWAITING_ACTION";
    case Lifecycle::GameTerminal:
        return "GAME_TERMINAL";
    case Lifecycle::Interrupted:
        return "INTERRUPTED";
    case Lifecycle::Failed:
        return "FAILED";
    }
    return "FAILED";
}

std::string_view rejection_code_name(const RejectionCode code) noexcept {
    switch (code) {
    case RejectionCode::IncompatibleContract:
        return "INCOMPATIBLE_CONTRACT";
    case RejectionCode::InvalidLifecycle:
        return "INVALID_LIFECYCLE";
    case RejectionCode::WrongEpisode:
        return "WRONG_EPISODE";
    case RejectionCode::StaleSubmissionToken:
        return "STALE_SUBMISSION_TOKEN";
    case RejectionCode::WrongPublicSemanticDecision:
        return "WRONG_PUBLIC_SEMANTIC_DECISION";
    case RejectionCode::UnknownPublicActionKey:
        return "UNKNOWN_PUBLIC_ACTION_KEY";
    case RejectionCode::PublicActionDomainDivergence:
        return "PUBLIC_ACTION_DOMAIN_DIVERGENCE";
    case RejectionCode::UnsupportedInterruptionReason:
        return "UNSUPPORTED_INTERRUPTION_REASON";
    }
    return "INVALID_LIFECYCLE";
}

std::string_view reset_rejection_code_name(const ResetRejectionCode code) noexcept {
    switch (code) {
    case ResetRejectionCode::ResetWhileAwaitingAction:
        return "RESET_WHILE_AWAITING_ACTION";
    case ResetRejectionCode::InvalidContract:
        return "INVALID_CONTRACT";
    case ResetRejectionCode::InvalidEnvironmentId:
        return "INVALID_ENVIRONMENT_ID";
    case ResetRejectionCode::InvalidEpisodeSpec:
        return "INVALID_EPISODE_SPEC";
    case ResetRejectionCode::InvalidStartingPlayer:
        return "INVALID_STARTING_PLAYER";
    case ResetRejectionCode::InvalidRunControl:
        return "INVALID_RUN_CONTROL";
    case ResetRejectionCode::ResourceIdentityMismatch:
        return "RESOURCE_IDENTITY_MISMATCH";
    case ResetRejectionCode::TokenNamespaceExhausted:
        return "TOKEN_NAMESPACE_EXHAUSTED";
    case ResetRejectionCode::UnsupportedResetConfiguration:
        return "UNSUPPORTED_RESET_CONFIGURATION";
    }
    return "INVALID_ENVIRONMENT_ID";
}

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
    append_string(bytes, config.public_action_identity_schema_id);
    append_string(bytes, config.candidate_digest_schema_id);
    append_string(bytes, config.public_candidate_digest_schema_id);
    append_string(bytes, config.episode_identity_schema_id);
    append_string(bytes, config.decision_identity_schema_id);
    append_string(bytes, config.public_decision_identity_schema_id);
    append_string(bytes, config.public_observation_contract_id);
    append_string(bytes, config.public_safe_state_schema_id);
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

namespace {

EnvironmentDecisionKind public_decision_kind(const protocol::DecisionRequestKind kind) {
    switch (kind) {
    case protocol::DecisionRequestKind::IdleCommand:
        return EnvironmentDecisionKind::IdleCommand;
    case protocol::DecisionRequestKind::BattleCommand:
        return EnvironmentDecisionKind::BattleCommand;
    case protocol::DecisionRequestKind::Chain:
        return EnvironmentDecisionKind::Chain;
    case protocol::DecisionRequestKind::Option:
        return EnvironmentDecisionKind::Option;
    case protocol::DecisionRequestKind::CardSelection:
        return EnvironmentDecisionKind::CardSelection;
    case protocol::DecisionRequestKind::Tribute:
        return EnvironmentDecisionKind::Tribute;
    case protocol::DecisionRequestKind::Sum:
        return EnvironmentDecisionKind::Sum;
    case protocol::DecisionRequestKind::Place:
        return EnvironmentDecisionKind::Place;
    case protocol::DecisionRequestKind::Counter:
        return EnvironmentDecisionKind::Counter;
    case protocol::DecisionRequestKind::Ordering:
        return EnvironmentDecisionKind::Ordering;
    case protocol::DecisionRequestKind::Announcement:
        return EnvironmentDecisionKind::Announcement;
    case protocol::DecisionRequestKind::UnselectCard:
        return EnvironmentDecisionKind::UnselectCard;
    case protocol::DecisionRequestKind::Position:
        return EnvironmentDecisionKind::Position;
    case protocol::DecisionRequestKind::YesNo:
        return EnvironmentDecisionKind::YesNo;
    case protocol::DecisionRequestKind::Unsupported:
        return EnvironmentDecisionKind::Unsupported;
    }
    return EnvironmentDecisionKind::Unsupported;
}

EnvironmentActionKind public_action_kind(const protocol::ActionKind kind) {
    switch (kind) {
    case protocol::ActionKind::IdleCommand:
        return EnvironmentActionKind::IdleCommand;
    case protocol::ActionKind::BattleCommand:
        return EnvironmentActionKind::BattleCommand;
    case protocol::ActionKind::Chain:
        return EnvironmentActionKind::Chain;
    case protocol::ActionKind::Option:
        return EnvironmentActionKind::Option;
    case protocol::ActionKind::CardSelection:
        return EnvironmentActionKind::CardSelection;
    case protocol::ActionKind::Announcement:
        return EnvironmentActionKind::Announcement;
    case protocol::ActionKind::Place:
        return EnvironmentActionKind::Place;
    case protocol::ActionKind::Position:
        return EnvironmentActionKind::Position;
    case protocol::ActionKind::YesNo:
        return EnvironmentActionKind::YesNo;
    case protocol::ActionKind::Pick:
        return EnvironmentActionKind::Pick;
    case protocol::ActionKind::Finish:
        return EnvironmentActionKind::Finish;
    case protocol::ActionKind::Cancel:
        return EnvironmentActionKind::Cancel;
    case protocol::ActionKind::AssignAmount:
        return EnvironmentActionKind::AssignAmount;
    }
    return EnvironmentActionKind::Unsupported;
}

bool safe_key_shape(const protocol::DecisionRequest& request,
                    const protocol::ActionCandidate& candidate) {
    if (request.continuation.has_value()) {
        const auto& continuation = *request.continuation;
        if (continuation.continuation_id.empty() || candidate.continuation_id != continuation.continuation_id ||
            !starts_with(candidate.semantic_key, continuation.continuation_id + ".")) {
            return false;
        }
        switch (candidate.action_kind) {
        case protocol::ActionKind::Pick:
            return starts_with(candidate.semantic_key, continuation.continuation_id + ".pick.");
        case protocol::ActionKind::AssignAmount:
            return starts_with(candidate.semantic_key, continuation.continuation_id + ".amount.");
        case protocol::ActionKind::Finish:
            return candidate.semantic_key == continuation.continuation_id + ".finish";
        case protocol::ActionKind::Cancel:
            return candidate.semantic_key == continuation.continuation_id + ".cancel" ||
                   candidate.semantic_key == continuation.continuation_id + ".bypass";
        default:
            return false;
        }
    }
    if (!candidate.continuation_id.empty()) {
        return false;
    }
    const auto has_prefix = [&candidate](const char* prefix) {
        return starts_with(candidate.semantic_key, prefix);
    };
    switch (candidate.action_kind) {
    case protocol::ActionKind::IdleCommand:
        return has_prefix("idle.");
    case protocol::ActionKind::BattleCommand:
        return has_prefix("battle.");
    case protocol::ActionKind::Chain:
        return has_prefix("chain.");
    case protocol::ActionKind::Option:
        return has_prefix("option.");
    case protocol::ActionKind::CardSelection:
        return has_prefix("card.") || has_prefix("unselect.");
    case protocol::ActionKind::Announcement:
        return has_prefix("announce_number.") || has_prefix("announce_mask.");
    case protocol::ActionKind::Place:
        return has_prefix("place.");
    case protocol::ActionKind::Position:
        return has_prefix("position.");
    case protocol::ActionKind::YesNo:
        return candidate.semantic_key == "yes_no.no" || candidate.semantic_key == "yes_no.yes";
    case protocol::ActionKind::Pick:
    case protocol::ActionKind::AssignAmount:
        return false;
    case protocol::ActionKind::Finish:
        return candidate.semantic_key == "unselect.finish";
    case protocol::ActionKind::Cancel:
        return candidate.semantic_key == "card.cancel" || candidate.semantic_key == "unselect.cancel";
    }
    return false;
}

const observation::ObservedCard* resolve_observation_card(
    const observation::PlayerObservation& current, const std::uint32_t code,
    const std::uint8_t controller, const std::uint32_t location,
    const std::uint32_t sequence) {
    if (code == 0 || controller > 1) {
        return nullptr;
    }

    const auto matches = [&](const observation::ObservedCard& entity) {
        if (entity.controller.value_or(2) != controller) {
            return false;
        }
        if (location == 0) {
            return entity.identity_known && entity.passcode.value_or(0) == code;
        }
        const auto projected_zone = observation::detail::project_zone(
            location, controller, sequence,
            static_cast<std::uint32_t>(current.match_context.duel_flags)).zone;
        if (entity.zone != projected_zone) {
            return false;
        }
        if (entity.sequence.has_value() && *entity.sequence != sequence) {
            return false;
        }
        return !entity.identity_known || entity.passcode.value_or(0) == code;
    };

    const observation::ObservedCard* match = nullptr;
    for (const auto& entity : current.entities) {
        if (!matches(entity)) {
            continue;
        }
        if (match != nullptr) {
            return nullptr;
        }
        match = &entity;
    }
    return match;
}

std::optional<PublicCardReference> project_card_reference(
    const observation::PlayerObservation& current, const std::uint32_t code,
    const std::uint8_t controller, const std::uint32_t location,
    const std::uint32_t sequence) {
    const auto* entity = resolve_observation_card(current, code, controller, location, sequence);
    if (entity == nullptr) {
        return std::nullopt;
    }
    return PublicCardReference{
        entity->identity_known ? PublicCardReferenceKind::VisibleCard
                               : PublicCardReferenceKind::RedactedSlot,
        entity->locator.value,
    };
}

std::optional<PublicChoice> project_choice(
    const protocol::DecisionRequest& request, const protocol::ActionCandidate& candidate) {
    if (candidate.action_kind == protocol::ActionKind::YesNo) {
        if (!candidate.choice_value.has_value() || *candidate.choice_value > 1) {
            throw BoundaryError(FailureCode::PublicFrameInvariant, FailureStage::Projection,
                                "yes/no candidate is missing its typed choice");
        }
        const auto kind = request.engine_message_name == "MSG_SELECT_EFFECTYN"
                              ? PublicChoiceKind::EffectYesNo
                              : PublicChoiceKind::YesNo;
        return PublicChoice{kind, *candidate.choice_value, std::nullopt};
    }
    if (candidate.action_kind == protocol::ActionKind::Option) {
        if (!candidate.choice_value.has_value() || !candidate.choice_index.has_value()) {
            throw BoundaryError(FailureCode::PublicFrameInvariant, FailureStage::Projection,
                                "option candidate is missing its value or response selector");
        }
        return PublicChoice{PublicChoiceKind::OptionValue, *candidate.choice_value,
                            candidate.choice_index};
    }
    if (candidate.action_kind == protocol::ActionKind::Announcement &&
        request.engine_message_name == "MSG_ANNOUNCE_NUMBER") {
        if (!candidate.choice_value.has_value() || !candidate.choice_index.has_value()) {
            throw BoundaryError(FailureCode::PublicFrameInvariant, FailureStage::Projection,
                                "announcement candidate is missing its value or response selector");
        }
        return PublicChoice{PublicChoiceKind::AnnouncementNumber, *candidate.choice_value,
                            candidate.choice_index};
    }
    if ((candidate.action_kind == protocol::ActionKind::IdleCommand ||
         candidate.action_kind == protocol::ActionKind::BattleCommand ||
         candidate.action_kind == protocol::ActionKind::Chain) &&
        candidate.choice_index.has_value()) {
        return PublicChoice{PublicChoiceKind::EffectChoice, *candidate.choice_index, std::nullopt};
    }
    return std::nullopt;
}

bool ends_with(const std::string& value, const std::string_view suffix) {
    return value.size() >= suffix.size() &&
           value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::string project_continuation_operation(const protocol::ActionCandidate& candidate) {
    switch (candidate.action_kind) {
    case protocol::ActionKind::Pick:
        return "pick";
    case protocol::ActionKind::AssignAmount:
        return "amount";
    case protocol::ActionKind::Finish:
        return "finish";
    case protocol::ActionKind::Cancel:
        return ends_with(candidate.semantic_key, ".bypass") ? "bypass" : "cancel";
    default:
        return {};
    }
}

struct ProjectedCandidate final {
    EnvironmentActionCandidate public_candidate;
    detail::PublicActionBinding binding;
};

ProjectedCandidate project_candidate(const protocol::DecisionRequest& request,
                                     const protocol::ActionCandidate& candidate,
                                     const observation::PlayerObservation& current) {
    if (!safe_key_shape(request, candidate)) {
        throw BoundaryError(FailureCode::PublicFrameInvariant, FailureStage::Projection,
                            "internal semantic action key is not coupled to its request family");
    }

    ProjectedCandidate result;
    result.public_candidate.action_kind = public_action_kind(candidate.action_kind);
    if (result.public_candidate.action_kind == EnvironmentActionKind::Unsupported) {
        throw BoundaryError(FailureCode::UnsupportedProtocol, FailureStage::Projection,
                            "internal action kind has no public representation");
    }

    if (candidate.source_card != 0) {
        result.public_candidate.source_reference = project_card_reference(
            current, candidate.source_card, candidate.source_controller,
            candidate.source_location, candidate.source_sequence);
    }
    if (candidate.target_card != 0) {
        result.public_candidate.target_reference = project_card_reference(
            current, candidate.target_card, candidate.target_controller,
            candidate.target_location, candidate.target_sequence);
    }

    const auto choice = project_choice(request, candidate);
    result.public_candidate.choice = choice;
    const auto continuation_operation = request.continuation.has_value()
                                             ? project_continuation_operation(candidate)
                                             : std::string{};
    if (request.continuation.has_value() && continuation_operation.empty()) {
        throw BoundaryError(FailureCode::PublicFrameInvariant, FailureStage::Projection,
                            "continuation candidate has no safe operation descriptor");
    }

    PublicActionKeyInput key;
    key.action_kind = std::string(environment_action_kind_name(result.public_candidate.action_kind));
    key.choice = choice;
    key.source_reference = result.public_candidate.source_reference;
    key.target_reference = result.public_candidate.target_reference;
    key.continuation_operation = continuation_operation;

    switch (candidate.action_kind) {
    case protocol::ActionKind::IdleCommand:
    case protocol::ActionKind::BattleCommand:
    case protocol::ActionKind::Chain:
        key.phase = candidate.phase;
        result.public_candidate.phase = candidate.phase;
        break;
    case protocol::ActionKind::Position:
        key.position = candidate.position;
        result.public_candidate.position = candidate.position;
        break;
    case protocol::ActionKind::CardSelection:
    case protocol::ActionKind::Place:
    case protocol::ActionKind::Pick:
    case protocol::ActionKind::AssignAmount:
        key.source_index = candidate.source_index;
        result.public_candidate.source_index = candidate.source_index;
        break;
    case protocol::ActionKind::Announcement:
        if (request.engine_message_name != "MSG_ANNOUNCE_NUMBER") {
            key.source_index = candidate.source_index;
            result.public_candidate.source_index = candidate.source_index;
        }
        break;
    default:
        break;
    }
    if (candidate.action_kind == protocol::ActionKind::AssignAmount) {
        if (candidate.amount < 0) {
            throw BoundaryError(FailureCode::PublicFrameInvariant, FailureStage::Projection,
                                "amount candidate has a negative public amount");
        }
        key.amount = candidate.amount;
        result.public_candidate.amount = candidate.amount;
    }
    result.public_candidate.continuation_operation = continuation_operation;
    result.public_candidate.submits_engine_response = candidate.submits_engine_response;
    try {
        result.public_candidate.public_action_key = public_action_key(key);
    } catch (const std::exception&) {
        throw BoundaryError(FailureCode::PublicFrameInvariant, FailureStage::Projection,
                            "candidate cannot be encoded as a canonical public action key");
    }
    result.binding = {result.public_candidate.public_action_key, candidate.semantic_key};
    return result;
}

EnvironmentContinuationView project_continuation(
    const protocol::DecisionRequest& request, const observation::PlayerObservation& current) {
    if (!request.continuation.has_value()) {
        throw BoundaryError(FailureCode::PublicFrameInvariant, FailureStage::Projection,
                            "continuation projection requested for an atomic request");
    }
    const auto& continuation = *request.continuation;
    if (continuation.continuation_id.empty() ||
        !current.decision_context.continuation_id.has_value() ||
        *current.decision_context.continuation_id != continuation.continuation_id) {
        throw BoundaryError(FailureCode::PrivacyInvariant, FailureStage::Projection,
                            "continuation identity is not coupled to the public observation");
    }
    EnvironmentContinuationView result;
    result.continuation_kind = protocol::continuation_kind_name(continuation.continuation_kind);
    result.continuation_step = continuation.continuation_step;
    result.selected_indices = continuation.selected_indices;
    result.remaining_indices = continuation.remaining_indices;
    result.assigned_amounts = continuation.assigned_amounts;
    result.min_count = continuation.min_count;
    result.max_count = continuation.max_count;
    result.target_sum = continuation.target_sum;
    result.required_amount = continuation.required_amount;
    result.available_mask = continuation.available_mask;
    result.selected_mask = continuation.selected_mask;
    result.continuation_steps = continuation.continuation_steps;
    result.exact_sum = continuation.exact_sum;
    result.greater_sum = continuation.greater_sum;
    result.can_finish = continuation.can_finish;
    result.can_cancel = continuation.can_cancel;
    return result;
}

struct ProjectedFrame final {
    DecisionFrame frame;
    std::vector<detail::PublicActionBinding> bindings;
};

}  // namespace

struct EpisodicEnvironment::Impl final {
    using NextBoundary = std::variant<DecisionFrame, EpisodeTerminal, EpisodeInterrupted, EpisodeFailure>;

    explicit Impl(CertifiedEnvironmentConfig value) : config(std::move(value)) {}

    CertifiedEnvironmentConfig config;
    Lifecycle lifecycle_state = Lifecycle::Empty;
    std::unique_ptr<EpisodeDriver> driver;
    EpisodeSpec current_spec;
    RunControl current_control;
    std::string current_episode_id;
    std::uint64_t public_decision_index = 0;
    std::uint64_t episode_incarnation_counter = 0;
    std::uint64_t frame_generation_counter = 0;
    std::optional<DecisionFrame> current_frame;
    std::optional<EpisodeTerminal> terminal;
    std::optional<EpisodeInterrupted> interruption;
    std::optional<EpisodeFailure> failure;
    std::array<std::optional<PublicEnvironmentObservation>, 2> terminal_views;
    std::vector<detail::PublicActionBinding> current_bindings;
    bool force_next_reset_failure_for_test = false;

    StepRejected rejected(const ActionSelection& selection, const RejectionCode code) const {
        StepRejected result;
        result.contract_id = std::string(kEpisodicEnvironmentContractId);
        result.rejection_code = code;
        result.submitted_episode_semantic_id = selection.episode_semantic_id;
        result.submitted_public_semantic_decision_id = selection.public_semantic_decision_id;
        result.submitted_submission_token = selection.submission_token;
        result.submitted_public_action_key = selection.public_action_key;
        result.current_episode_semantic_id = current_episode_id;
        if (current_frame.has_value()) {
            result.current_public_semantic_decision_id =
                current_frame->public_semantic_decision_id;
            result.current_public_candidate_domain_digest =
                current_frame->public_candidate_domain_digest;
        }
        return result;
    }

    EpisodeFailure make_failure(const FailureCode code, const FailureStage stage,
                                const bool mutation_may_have_occurred,
                                const bool include_diagnostic_reference = true) const {
        EpisodeFailure result;
        result.contract_id = std::string(kEpisodicEnvironmentContractId);
        if (!current_episode_id.empty()) {
            result.episode_semantic_id = current_episode_id;
        }
        result.failure_code = code;
        result.failure_stage = stage;
        result.mutation_may_have_occurred = mutation_may_have_occurred;
        if (driver != nullptr) {
            result.semantic_action_count = driver->metrics().semantic_action_count;
            if (current_frame.has_value()) {
                result.last_public_semantic_decision_id =
                    current_frame->public_semantic_decision_id;
                result.last_valid_audit_prefix_hash = trace::canonical_trace_hash_v2(driver->trace());
            } else if (!driver->trace().steps.empty()) {
                result.last_valid_audit_prefix_hash = trace::canonical_trace_hash_v2(driver->trace());
            }
        }
        if (include_diagnostic_reference) {
            result.restricted_diagnostic_reference =
                std::string("episodic.") + std::string(failure_code_name(code));
        }
        return result;
    }

    EpisodeFailure make_driver_failure(const DriverFailure& driver_failure,
                                       const bool accepted_action) const {
        return make_failure(driver_failure_code(driver_failure), driver_failure_stage(driver_failure),
                            accepted_action || driver_failure.mutation_may_have_occurred);
    }

    std::uint64_t closure_engine_step_index() const {
        if (driver == nullptr) {
            return 0;
        }
        if (!driver->trace().steps.empty()) {
            return driver->trace().steps.back().engine_step_index;
        }
        return driver->metrics().process_call_count == 0 ? 0 : driver->metrics().process_call_count - 1;
    }

    std::optional<std::uint64_t> closure_last_decision_index() const {
        if (current_frame.has_value()) {
            return current_frame->decision_index;
        }
        return std::nullopt;
    }

    EpisodeTerminal make_terminal(const DriverGameTerminal& driver_terminal) const {
        if (!driver_terminal.player_zero_observation.has_value() ||
            !driver_terminal.player_one_observation.has_value()) {
            throw BoundaryError(FailureCode::ObservationFailure, FailureStage::Teardown,
                                "terminal boundary did not provide both perspective observations");
        }
        const auto verify = [](const observation::PlayerObservation& value, const std::uint8_t player) {
            if (value.schema_version != kObservationContractId || value.perspective_player != player ||
                value.observation_hash != observation::observation_hash(value)) {
                throw BoundaryError(FailureCode::ObservationFailure, FailureStage::Teardown,
                                    "terminal perspective observation failed validation");
            }
        };
        verify(*driver_terminal.player_zero_observation, 0);
        verify(*driver_terminal.player_one_observation, 1);
        if (driver == nullptr) {
            throw BoundaryError(FailureCode::InvalidAuthoritativeState, FailureStage::Teardown,
                                "terminal boundary has no live driver evidence");
        }
        EpisodeTerminal result;
        result.contract_id = std::string(kEpisodicEnvironmentContractId);
        result.episode_semantic_id = current_episode_id;
        result.winner = driver_terminal.winner;
        result.win_reason = driver_terminal.win_reason;
        result.semantic_action_count = driver->metrics().semantic_action_count;
        result.last_decision_index = closure_last_decision_index();
        result.final_engine_step_index = closure_engine_step_index();
        result.semantic_gameplay_hash = trace::semantic_gameplay_hash(driver->trace());
        result.final_audit_prefix_hash = trace::canonical_trace_hash_v2(driver->trace());
        return result;
    }

    EpisodeInterrupted make_interrupted(const InterruptionReason reason) const {
        EpisodeInterrupted result;
        result.contract_id = std::string(kEpisodicEnvironmentContractId);
        result.episode_semantic_id = current_episode_id;
        result.reason = reason;
        if (driver != nullptr) {
            result.semantic_action_count = driver->metrics().semantic_action_count;
            result.final_engine_step_index = closure_engine_step_index();
            result.last_valid_audit_prefix_hash = trace::canonical_trace_hash_v2(driver->trace());
        }
        if (current_frame.has_value()) {
            result.last_public_semantic_decision_id = current_frame->public_semantic_decision_id;
            result.last_decision_index = current_frame->decision_index;
        }
        result.run_control_evidence.engine_process_budget = current_control.engine_process_budget;
        result.run_control_evidence.semantic_action_budget = current_control.semantic_action_budget;
        if (driver != nullptr) {
            result.run_control_evidence.engine_process_count = driver->metrics().process_call_count;
            result.run_control_evidence.semantic_action_count = driver->metrics().semantic_action_count;
        }
        return result;
    }

    PublicEnvironmentObservation project_public_observation_for_index(
        const observation::PlayerObservation& current, const std::uint64_t decision_index) const {
        if (current.schema_version != kObservationContractId ||
            current.observation_hash != observation::observation_hash(current)) {
            throw BoundaryError(FailureCode::ObservationFailure, FailureStage::Projection,
                                "driver observation failed internal schema/hash validation");
        }
        auto public_source = current;
        public_source.decision_index = decision_index;
        public_source.observation_hash = observation::observation_hash(public_source);
        try {
            auto result = project_public_observation(public_source);
            if (result.decision_index != decision_index ||
                result.perspective_player != current.perspective_player) {
                throw BoundaryError(FailureCode::PublicFrameInvariant, FailureStage::Projection,
                                    "public observation index or perspective is not owned by the facade");
            }
            return result;
        } catch (const BoundaryError&) {
            throw;
        } catch (const std::exception&) {
            throw BoundaryError(FailureCode::ObservationFailure, FailureStage::Projection,
                                "perspective observation could not be projected safely");
        }
    }

    ProjectedFrame project_frame(const DriverDecisionBoundary& boundary,
                                 const std::uint64_t decision_index) {
        if (boundary.request == nullptr || boundary.observation == nullptr) {
            throw BoundaryError(FailureCode::PublicFrameInvariant, FailureStage::Projection,
                                "driver published a boundary without owned projection inputs");
        }
        const auto& request = *boundary.request;
        const auto& current = *boundary.observation;
        try {
            protocol::validate_candidate_set(request);
        } catch (const protocol::ProtocolError& error) {
            throw BoundaryError(protocol_failure_code(error.code(), error.what()), FailureStage::Projection,
                                "driver candidate domain failed public validation");
        }
        const auto public_kind = public_decision_kind(request.kind);
        if (public_kind == EnvironmentDecisionKind::Unsupported || request.decision_id.empty() ||
            request.engine_message_name.empty() || request.player > 1) {
            throw BoundaryError(FailureCode::UnsupportedProtocol, FailureStage::Projection,
                                "driver request is outside the certified public decision domain");
        }
        if (current.schema_version != kObservationContractId || current.perspective_player != request.player ||
            current.engine_step_index != request.engine_step_index ||
            current.observation_hash != observation::observation_hash(current)) {
            throw BoundaryError(FailureCode::PublicFrameInvariant, FailureStage::Projection,
                                "decision and perspective observation are not coupled");
        }
        const auto& context = current.decision_context;
        if (!context.decision_id.has_value() || *context.decision_id != request.decision_id ||
            !context.kind.has_value() || *context.kind != protocol::decision_kind_name(request.kind) ||
            !context.engine_step_index.has_value() || *context.engine_step_index != request.engine_step_index ||
            !context.player.has_value() || *context.player != request.player ||
            !context.engine_message_type.has_value() || *context.engine_message_type != request.engine_message_type ||
            !context.engine_message_name.has_value() || *context.engine_message_name != request.engine_message_name) {
            throw BoundaryError(FailureCode::PublicFrameInvariant, FailureStage::Projection,
                                "observation decision context does not match the driver request");
        }

        auto public_observation = project_public_observation_for_index(current, decision_index);
        const auto public_observation_hash =
            ::ygo::environment::public_observation_digest(public_observation);

        EnvironmentDecisionRequest public_request;
        public_request.kind = public_kind;
        public_request.player = request.player;
        if (request.continuation.has_value()) {
            public_request.continuation = project_continuation(request, current);
        } else if (context.continuation_id.has_value()) {
            throw BoundaryError(FailureCode::PublicFrameInvariant, FailureStage::Projection,
                                "atomic request unexpectedly carries a continuation observation reference");
        }

        std::vector<std::string> keys;
        keys.reserve(request.candidates.size());
        public_request.candidates.reserve(request.candidates.size());
        std::vector<detail::PublicActionBinding> bindings;
        bindings.reserve(request.candidates.size());
        for (const auto& candidate : request.candidates) {
            auto projected = project_candidate(request, candidate, current);
            keys.push_back(projected.public_candidate.public_action_key);
            bindings.push_back(std::move(projected.binding));
            public_request.candidates.push_back(std::move(projected.public_candidate));
        }
        if (public_request.candidates.size() != request.candidates.size() || keys.size() != request.candidates.size()) {
            throw BoundaryError(FailureCode::PublicFrameInvariant, FailureStage::Projection,
                                "public candidate projection changed domain cardinality");
        }

        std::string public_candidate_digest;
        try {
            public_candidate_digest = public_candidate_domain_digest(
                environment_decision_kind_name(public_kind), keys);
        } catch (const std::exception&) {
            throw BoundaryError(FailureCode::PublicFrameInvariant, FailureStage::Projection,
                                "public candidate domain is not canonical or unique");
        }

        PublicSemanticDecisionIdentityInput public_identity;
        public_identity.episode_semantic_id = current_episode_id;
        public_identity.decision_index = decision_index;
        public_identity.acting_player = request.player;
        public_identity.request_kind = std::string(environment_decision_kind_name(public_kind));
        public_identity.public_observation_digest = public_observation_hash;
        public_identity.public_candidate_domain_digest = public_candidate_digest;

        ProjectedFrame result;
        result.frame.contract_id = std::string(kEpisodicEnvironmentContractId);
        result.frame.episode_semantic_id = current_episode_id;
        result.frame.decision_index = decision_index;
        result.frame.engine_step_index = request.engine_step_index;
        result.frame.acting_player = request.player;
        result.frame.public_observation = std::move(public_observation);
        result.frame.request = std::move(public_request);
        result.frame.public_observation_digest = public_observation_hash;
        result.frame.public_candidate_domain_digest = public_candidate_digest;
        try {
            result.frame.public_semantic_decision_id = public_semantic_decision_id(public_identity);
        } catch (const std::exception&) {
            throw BoundaryError(FailureCode::PublicFrameInvariant, FailureStage::Projection,
                                "public semantic decision identity is not canonical");
        }
        if (result.frame.acting_player != result.frame.request.player ||
            result.frame.acting_player != result.frame.public_observation.perspective_player ||
            result.frame.decision_index != result.frame.public_observation.decision_index ||
            result.frame.request.candidates.size() != keys.size()) {
            throw BoundaryError(FailureCode::PublicFrameInvariant, FailureStage::Projection,
                                "public frame coupling invariant failed");
        }
        if (episode_incarnation_counter == 0 || frame_generation_counter == std::numeric_limits<std::uint64_t>::max()) {
            throw BoundaryError(FailureCode::TokenNamespaceExhausted, FailureStage::Projection,
                                "submission-token frame namespace is exhausted");
        }
        ++frame_generation_counter;
        result.frame.submission_token = {episode_incarnation_counter, frame_generation_counter};
        if (!result.frame.submission_token.valid()) {
            throw BoundaryError(FailureCode::TokenNamespaceExhausted, FailureStage::Projection,
                                "submission-token publication produced an invalid token");
        }
        result.bindings = std::move(bindings);
        return result;
    }

    NextBoundary close_failure(const FailureCode code, const FailureStage stage,
                               const bool mutation_may_have_occurred) {
        const auto value = make_failure(code, stage, mutation_may_have_occurred);
        driver.reset();
        current_frame.reset();
        current_bindings.clear();
        terminal.reset();
        interruption.reset();
        failure = value;
        terminal_views[0].reset();
        terminal_views[1].reset();
        lifecycle_state = Lifecycle::Failed;
        return value;
    }

    NextBoundary close_driver_failure(const DriverFailure& driver_failure,
                                      const bool accepted_action) {
        const auto value = make_driver_failure(driver_failure, accepted_action);
        driver.reset();
        current_frame.reset();
        current_bindings.clear();
        terminal.reset();
        interruption.reset();
        failure = value;
        terminal_views[0].reset();
        terminal_views[1].reset();
        lifecycle_state = Lifecycle::Failed;
        return value;
    }

    NextBoundary consume_boundary(const DriverBoundary& boundary, const bool accepted_action) {
        if (const auto* decision = std::get_if<DriverDecisionBoundary>(&boundary)) {
            if (driver == nullptr) {
                throw BoundaryError(FailureCode::InvalidAuthoritativeState, FailureStage::Advance,
                                    "actionable boundary has no driver owner", accepted_action);
            }
            std::uint64_t next_index = 0;
            if (accepted_action) {
                if (!current_frame.has_value() || current_frame->decision_index == std::numeric_limits<std::uint64_t>::max()) {
                    throw BoundaryError(FailureCode::InvalidAuthoritativeState, FailureStage::Projection,
                                        "public decision index cannot advance", true);
                }
                next_index = current_frame->decision_index + 1;
            }
            auto projected = project_frame(*decision, next_index);
            public_decision_index = next_index;
            current_bindings = std::move(projected.bindings);
            current_frame = std::move(projected.frame);
            lifecycle_state = Lifecycle::AwaitingAction;
            terminal.reset();
            interruption.reset();
            failure.reset();
            return *current_frame;
        }
        if (const auto* reached_terminal = std::get_if<DriverGameTerminal>(&boundary)) {
            auto value = make_terminal(*reached_terminal);
            terminal_views[0] = project_public_observation_for_index(
                *reached_terminal->player_zero_observation, public_decision_index);
            terminal_views[1] = project_public_observation_for_index(
                *reached_terminal->player_one_observation, public_decision_index);
            driver.reset();
            current_frame.reset();
            current_bindings.clear();
            terminal = value;
            interruption.reset();
            failure.reset();
            lifecycle_state = Lifecycle::GameTerminal;
            return value;
        }
        if (std::holds_alternative<DriverProcessBudgetExceeded>(boundary)) {
            auto value = make_interrupted(InterruptionReason::EngineProcessBudget);
            driver.reset();
            current_frame.reset();
            current_bindings.clear();
            terminal.reset();
            interruption = value;
            failure.reset();
            terminal_views[0].reset();
            terminal_views[1].reset();
            lifecycle_state = Lifecycle::Interrupted;
            return value;
        }
        if (std::holds_alternative<DriverSemanticActionBudgetExceeded>(boundary)) {
            auto value = make_interrupted(InterruptionReason::SemanticActionBudget);
            driver.reset();
            current_frame.reset();
            current_bindings.clear();
            terminal.reset();
            interruption = value;
            failure.reset();
            terminal_views[0].reset();
            terminal_views[1].reset();
            lifecycle_state = Lifecycle::Interrupted;
            return value;
        }
        if (std::holds_alternative<DriverAdministrativeInterrupt>(boundary)) {
            auto value = make_interrupted(InterruptionReason::AdministrativeCancel);
            driver.reset();
            current_frame.reset();
            current_bindings.clear();
            terminal.reset();
            interruption = value;
            failure.reset();
            terminal_views[0].reset();
            terminal_views[1].reset();
            lifecycle_state = Lifecycle::Interrupted;
            return value;
        }
        return close_driver_failure(std::get<DriverFailure>(boundary), accepted_action);
    }

    ResetResult reset(const EpisodeSpec& spec, const RunControl& control) {
        if (lifecycle_state == Lifecycle::AwaitingAction) {
            return ResetRejected{ResetRejectionCode::ResetWhileAwaitingAction, lifecycle_state};
        }
        if (spec.contract_id != kEpisodicEnvironmentContractId ||
            control.cancellation.reason != "ADMINISTRATIVE_CANCEL") {
            return ResetRejected{spec.contract_id != kEpisodicEnvironmentContractId
                                     ? ResetRejectionCode::InvalidContract
                                     : ResetRejectionCode::UnsupportedResetConfiguration,
                                 lifecycle_state};
        }
        if (spec.starting_player > 1) {
            return ResetRejected{ResetRejectionCode::InvalidStartingPlayer, lifecycle_state};
        }
        if (spec.seat_assignment != SeatAssignment::Normal && spec.seat_assignment != SeatAssignment::Mirror) {
            return ResetRejected{ResetRejectionCode::InvalidEpisodeSpec, lifecycle_state};
        }
        if (control.engine_process_budget == 0 || control.semantic_action_budget == 0 ||
            !safe_source_label(control.cancellation.source)) {
            return ResetRejected{ResetRejectionCode::InvalidRunControl, lifecycle_state};
        }
        if (episode_incarnation_counter == std::numeric_limits<std::uint64_t>::max()) {
            return ResetRejected{ResetRejectionCode::TokenNamespaceExhausted, lifecycle_state};
        }
        if (environment_semantic_id(config) != config.environment_semantic_id ||
            !certified_resources_match(config)) {
            return ResetRejected{ResetRejectionCode::ResourceIdentityMismatch, lifecycle_state};
        }

        EpisodeDriverConfig driver_config;
        std::string next_episode_id;
        try {
            next_episode_id = episode_semantic_id(config, spec);
            driver_config = make_driver_config(config, spec, control);
        } catch (const BoundaryError&) {
            return ResetRejected{ResetRejectionCode::ResourceIdentityMismatch, lifecycle_state};
        } catch (const std::invalid_argument&) {
            return ResetRejected{ResetRejectionCode::InvalidEpisodeSpec, lifecycle_state};
        } catch (const std::exception&) {
            return ResetRejected{ResetRejectionCode::ResourceIdentityMismatch, lifecycle_state};
        }

        const bool force_reset_failure = force_next_reset_failure_for_test;
        force_next_reset_failure_for_test = false;
        driver_config.force_unsupported_for_test = force_reset_failure;

        ++episode_incarnation_counter;
        current_spec = spec;
        current_control = control;
        current_episode_id = std::move(next_episode_id);
        public_decision_index = 0;
        current_frame.reset();
        terminal.reset();
        interruption.reset();
        failure.reset();
        terminal_views[0].reset();
        terminal_views[1].reset();
        driver.reset();
        try {
            driver = std::make_unique<EpisodeDriver>(std::move(driver_config));
            const auto boundary = driver->advance_until_boundary();
            auto next = consume_boundary(boundary, false);
            return ResetAccepted{std::move(next)};
        } catch (const BoundaryError& error) {
            auto next = close_failure(error.failure_code, error.failure_stage, error.mutation_may_have_occurred);
            return ResetAccepted{std::move(next)};
        } catch (const std::exception&) {
            auto next = close_failure(FailureCode::InternalDomainDivergence, FailureStage::Construction, true);
            return ResetAccepted{std::move(next)};
        }
    }

    StepResult step(const ActionSelection& selection) {
        if (selection.contract_id != kEpisodicEnvironmentContractId) {
            return rejected(selection, RejectionCode::IncompatibleContract);
        }
        if (lifecycle_state != Lifecycle::AwaitingAction || !current_frame.has_value() || driver == nullptr) {
            return rejected(selection, RejectionCode::InvalidLifecycle);
        }
        if (selection.episode_semantic_id != current_frame->episode_semantic_id) {
            return rejected(selection, RejectionCode::WrongEpisode);
        }
        if (selection.submission_token != current_frame->submission_token) {
            return rejected(selection, RejectionCode::StaleSubmissionToken);
        }
        if (selection.public_semantic_decision_id != current_frame->public_semantic_decision_id) {
            return rejected(selection, RejectionCode::WrongPublicSemanticDecision);
        }
        const auto selected_it = std::find_if(
            current_frame->request.candidates.begin(), current_frame->request.candidates.end(),
            [&selection](const EnvironmentActionCandidate& candidate) {
                return candidate.public_action_key == selection.public_action_key;
            });
        if (selected_it == current_frame->request.candidates.end()) {
            return rejected(selection, RejectionCode::UnknownPublicActionKey);
        }
        if (current_bindings.size() != current_frame->request.candidates.size()) {
            return rejected(selection, RejectionCode::PublicActionDomainDivergence);
        }
        const auto internal_key = detail::resolve_public_action_key(
            current_bindings, selection.public_action_key);
        if (!internal_key.has_value()) {
            return rejected(selection, RejectionCode::PublicActionDomainDivergence);
        }

        const auto accepted_episode_id = current_frame->episode_semantic_id;
        const auto accepted_decision_id = current_frame->public_semantic_decision_id;
        const auto accepted_decision_index = current_frame->decision_index;
        AcceptedActionTransition transition;
        transition.episode_semantic_id = accepted_episode_id;
        transition.public_semantic_decision_id = accepted_decision_id;
        transition.decision_index = accepted_decision_index;
        transition.selected_public_action_key = selection.public_action_key;

        DriverApplyResult apply_result;
        try {
            apply_result = driver->apply_semantic_key(*internal_key);
        } catch (const protocol::ProtocolError&) {
            auto next = close_failure(FailureCode::InternalDomainDivergence, FailureStage::Action, false);
            return StepAccepted{std::move(transition), std::get<EpisodeFailure>(std::move(next))};
        } catch (const std::exception&) {
            auto next = close_failure(FailureCode::InternalDomainDivergence, FailureStage::Action, true);
            return StepAccepted{std::move(transition), std::get<EpisodeFailure>(std::move(next))};
        }

        if (!apply_result.accepted.has_value() ||
            apply_result.accepted->selected_semantic_key != *internal_key ||
            (!apply_result.accepted->core_response_submitted &&
             apply_result.accepted->final_response_sha256.has_value())) {
            auto next = close_failure(FailureCode::InternalDomainDivergence, FailureStage::Action, true);
            return StepAccepted{std::move(transition), std::get<EpisodeFailure>(std::move(next))};
        }
        transition.core_response_submitted = apply_result.accepted->core_response_submitted;
        transition.final_response_sha256 = apply_result.accepted->final_response_sha256;
        try {
            auto next = consume_boundary(apply_result.next, true);
            return StepAccepted{std::move(transition), std::move(next)};
        } catch (const BoundaryError& error) {
            auto next = close_failure(error.failure_code, error.failure_stage, true);
            return StepAccepted{std::move(transition), std::get<EpisodeFailure>(std::move(next))};
        } catch (const std::exception&) {
            auto next = close_failure(FailureCode::InternalDomainDivergence, FailureStage::Action, true);
            return StepAccepted{std::move(transition), std::get<EpisodeFailure>(std::move(next))};
        }
    }

    InterruptResult interrupt(const InterruptRequest& request) {
        if (request.contract_id != kEpisodicEnvironmentContractId) {
            return InterruptRejected{RejectionCode::IncompatibleContract, lifecycle_state};
        }
        if (lifecycle_state != Lifecycle::AwaitingAction || !current_frame.has_value() || driver == nullptr) {
            return InterruptRejected{RejectionCode::InvalidLifecycle, lifecycle_state};
        }
        if (request.reason != InterruptionReason::AdministrativeCancel) {
            return InterruptRejected{RejectionCode::UnsupportedInterruptionReason, lifecycle_state};
        }
        try {
            const auto boundary = driver->administrative_interrupt();
            auto next = consume_boundary(boundary, false);
            return InterruptAccepted{std::get<EpisodeInterrupted>(std::move(next))};
        } catch (const BoundaryError& error) {
            auto next = close_failure(error.failure_code, FailureStage::Interruption, true);
            return std::get<EpisodeFailure>(std::move(next));
        } catch (const std::exception&) {
            auto next = close_failure(FailureCode::InternalDomainDivergence, FailureStage::Interruption, true);
            return std::get<EpisodeFailure>(std::move(next));
        }
    }
};

void detail::EpisodicEnvironmentTestAccess::force_next_reset_failure(
    EpisodicEnvironment& environment) {
    environment.impl_->force_next_reset_failure_for_test = true;
}

EnvironmentFactoryResult EpisodicEnvironment::create(CertifiedEnvironmentConfig config) {
    try {
        const auto canonical = CertifiedEnvironmentConfig::canonical();
        if (!equal_config(config, canonical)) {
            return EnvironmentFactoryRejected{ResetRejectionCode::InvalidEnvironmentId};
        }
        if (!certified_resources_match(config)) {
            return EnvironmentFactoryRejected{ResetRejectionCode::ResourceIdentityMismatch};
        }
        return std::unique_ptr<EpisodicEnvironment>(new EpisodicEnvironment(std::move(config)));
    } catch (const std::exception&) {
        return EnvironmentFactoryRejected{ResetRejectionCode::ResourceIdentityMismatch};
    }
}

EpisodicEnvironment::EpisodicEnvironment(CertifiedEnvironmentConfig config)
    : impl_(std::make_unique<Impl>(std::move(config))) {}

EpisodicEnvironment::~EpisodicEnvironment() = default;

ResetResult EpisodicEnvironment::reset(const EpisodeSpec& spec, const RunControl& control) {
    return impl_->reset(spec, control);
}

StepResult EpisodicEnvironment::step(const ActionSelection& selection) {
    return impl_->step(selection);
}

InterruptResult EpisodicEnvironment::interrupt(const InterruptRequest& request) {
    return impl_->interrupt(request);
}

std::optional<PublicEnvironmentObservation> EpisodicEnvironment::perspective_terminal_view(
    const std::uint8_t player) const {
    if (impl_->lifecycle_state != Lifecycle::GameTerminal || player > 1 ||
        !impl_->terminal_views[player].has_value()) {
        return std::nullopt;
    }
    return *impl_->terminal_views[player];
}

Lifecycle EpisodicEnvironment::lifecycle() const noexcept { return impl_->lifecycle_state; }

const CertifiedEnvironmentConfig& EpisodicEnvironment::config() const noexcept { return impl_->config; }

}  // namespace ygo::environment
