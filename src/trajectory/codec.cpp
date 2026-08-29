#include "ygo/trajectory/codec.hpp"

#include <algorithm>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>

#include "ygo/environment/public_action_identity.hpp"
#include "ygo/environment/public_environment_observation.hpp"
#include "ygo/trajectory/identity_resolver.hpp"
#include "ygo/trace/sha256.hpp"

namespace ygo::trajectory {

std::string policy_artifact_id_for(const PolicyArtifact& value);
std::string assignment_id_for(const ParticipantPolicyAssignment& value);
std::string rng_initialization_id_for(const PolicyRngInitializationIdentity& value);
std::string rng_stream_id_for(const PolicyRngStreamIdentity& value);

namespace {

constexpr std::string_view kCandidateSchema = kTrustedTrajectoryContractId;
constexpr std::string_view kFrameSchema = kTrustedTrajectoryContractId;
constexpr std::string_view kClosureSchema = kTrustedTrajectoryContractId;
constexpr std::string_view kManifestSchema = kTrustedTrajectoryContractId;
constexpr std::string_view kEnvelopeSchema = kTrustedTrajectoryContractId;

template <typename T>
DecodeResult<T> failure(std::string message) noexcept {
    DecodeResult<T> result;
    result.error = DecodeError{std::move(message)};
    return result;
}

template <typename T>
DecodeResult<T> success(T value) noexcept {
    DecodeResult<T> result;
    result.value = std::move(value);
    return result;
}

void require_length(const std::size_t size) {
    if (size > std::numeric_limits<std::uint32_t>::max()) {
        throw std::length_error("trajectory codec field exceeds u32 length");
    }
}

void require_nonempty(const std::string& value, const char* field) {
    if (value.empty() || !is_valid_utf8(value)) {
        throw std::invalid_argument(std::string("trajectory ") + field + " is invalid");
    }
}

void require_digest(const std::string& value, const char* field) {
    if (!is_lower_hex_digest(value)) {
        throw std::invalid_argument(std::string("trajectory ") + field + " is not a SHA-256 digest");
    }
}

void require_identity(const std::string& value, const std::string_view prefix, const char* field) {
    if (!is_canonical_identity(value, prefix)) {
        throw std::invalid_argument(std::string("trajectory ") + field + " has invalid identity");
    }
}

template <typename T>
void require_strictly_sorted(const std::vector<T>& values, const char* field) {
    for (std::size_t index = 1; index < values.size(); ++index) {
        if (!(values[index - 1] < values[index])) {
            throw std::invalid_argument(std::string("trajectory ") + field + " is not strictly sorted");
        }
    }
}

template <typename T>
void require_sorted(const std::vector<T>& values, const char* field) {
    for (std::size_t index = 1; index < values.size(); ++index) {
        if (values[index] < values[index - 1]) {
            throw std::invalid_argument(std::string("trajectory ") + field + " is not sorted");
        }
    }
}

void require_contract(std::string_view value, std::string_view expected, const char* field) {
    if (value != expected) {
        throw std::invalid_argument(std::string("trajectory ") + field + " has an unknown contract");
    }
}

std::uint8_t enum_value(const PolicyKind value) noexcept {
    return static_cast<std::uint8_t>(value);
}

bool valid_policy_kind(const std::uint8_t value) noexcept { return value <= 4; }
bool valid_policy_role(const std::uint8_t value) noexcept { return value <= 4; }
bool valid_seat_role(const std::uint8_t value) noexcept { return value <= 1; }
bool valid_deck_role(const std::uint8_t value) noexcept { return value <= 1; }
bool valid_rng_mode(const std::uint8_t value) noexcept { return value <= 2; }
bool valid_environment_decision_kind(const std::uint8_t value) noexcept {
    return value <= static_cast<std::uint8_t>(environment::EnvironmentDecisionKind::Unsupported);
}
bool valid_environment_action_kind(const std::uint8_t value) noexcept {
    return value <= static_cast<std::uint8_t>(environment::EnvironmentActionKind::Unsupported);
}
bool valid_public_choice_kind(const std::uint8_t value) noexcept {
    return value >= static_cast<std::uint8_t>(environment::PublicChoiceKind::YesNo) &&
           value <= static_cast<std::uint8_t>(environment::PublicChoiceKind::AnnouncementNumber);
}
bool valid_public_card_reference_kind(const std::uint8_t value) noexcept { return value <= 1; }
bool valid_collection_disposition_kind(const std::uint8_t value) noexcept { return value <= 1; }
bool valid_rejection_code(const std::uint8_t value) noexcept {
    return value <= static_cast<std::uint8_t>(environment::RejectionCode::UnsupportedInterruptionReason);
}
bool valid_transition_class(const std::uint8_t value) noexcept { return value <= 2; }
bool valid_next_frame_target_kind(const std::uint8_t value) noexcept { return value <= 1; }
bool valid_successor_kind(const std::uint8_t value) noexcept { return value <= 3; }
bool valid_failure_code(const std::uint8_t value) noexcept {
    return value <= static_cast<std::uint8_t>(environment::FailureCode::ResourceIdentityMismatch);
}
bool valid_failure_stage(const std::uint8_t value) noexcept {
    return value <= static_cast<std::uint8_t>(environment::FailureStage::Teardown);
}
bool valid_interruption_reason(const std::uint8_t value) noexcept {
    return value <= static_cast<std::uint8_t>(environment::InterruptionReason::AdministrativeCancel);
}
bool valid_closure_kind(const std::uint8_t value) noexcept { return value <= 2; }

void validate_policy_artifact(const PolicyArtifact& value) {
    if (!valid_policy_kind(enum_value(value.policy_kind))) {
        throw std::invalid_argument("trajectory policy kind is unknown");
    }
    require_nonempty(value.producer_implementation_identity, "producer identity");
    require_nonempty(value.inference_adapter_identity, "inference adapter identity");
    require_nonempty(value.observation_adapter_identity, "observation adapter identity");
    require_nonempty(value.action_adapter_identity, "action adapter identity");
    require_nonempty(value.sampling_contract_identity, "sampling contract identity");
    require_nonempty(value.policy_rng_contract_identity, "policy RNG contract identity");
    if (value.policy_kind == PolicyKind::DeterministicHeuristic &&
        value.policy_rng_contract_identity != kNoPolicyRngContractId) {
        throw std::invalid_argument("deterministic policy must use the no-RNG contract");
    }
    if (value.policy_kind == PolicyKind::RandomLegal &&
        value.policy_rng_contract_identity == kNoPolicyRngContractId) {
        throw std::invalid_argument("random legal policy requires a RNG contract");
    }
    if (value.policy_kind == PolicyKind::DeterministicHeuristic &&
        (value.model_checkpoint_identity.has_value() || value.search_contract_identity.has_value() ||
         value.demonstration_source_identity.has_value())) {
        throw std::invalid_argument("deterministic policy has incompatible provenance");
    }
    if (value.policy_kind == PolicyKind::RandomLegal &&
        (value.model_checkpoint_identity.has_value() || value.search_contract_identity.has_value() ||
         value.demonstration_source_identity.has_value())) {
        throw std::invalid_argument("random legal policy has incompatible provenance");
    }
    for (const auto* optional_identity :
         {&value.model_checkpoint_identity, &value.search_contract_identity,
          &value.demonstration_source_identity, &value.artifact_metadata_identity}) {
        if (optional_identity->has_value()) {
            require_nonempty(**optional_identity, "optional provenance identity");
        }
    }
    if ((value.policy_kind == PolicyKind::NeuralCheckpoint &&
         !value.model_checkpoint_identity.has_value()) ||
        (value.policy_kind == PolicyKind::SearchAssisted &&
         !value.search_contract_identity.has_value()) ||
        (value.policy_kind == PolicyKind::ImportedDemonstration &&
         !value.demonstration_source_identity.has_value())) {
        throw std::invalid_argument("policy kind lacks its mandatory provenance identity");
    }
    const auto expected = policy_artifact_id_for(value);
    require_identity(value.policy_artifact_id, "policy_artifact.v1.", "policy artifact");
    if (value.policy_artifact_id != expected) {
        throw std::invalid_argument("policy artifact identity mismatch");
    }
}

void validate_league_context(const std::optional<LeagueContext>& value) {
    if (!value.has_value()) {
        return;
    }
    require_nonempty(value->league_member_id, "league member identity");
    require_nonempty(value->league_role, "league role");
    if (!std::all_of(value->league_role.begin(), value->league_role.end(), [](const unsigned char c) {
            return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
        })) {
        throw std::invalid_argument("league role is not a lower-case token");
    }
}

void validate_assignment(const ParticipantPolicyAssignment& value) {
    if (value.player > 1 || !valid_seat_role(static_cast<std::uint8_t>(value.seat_role)) ||
        !valid_deck_role(static_cast<std::uint8_t>(value.deck_role)) ||
        !valid_policy_role(static_cast<std::uint8_t>(value.policy_role))) {
        throw std::invalid_argument("trajectory assignment enum or player is unknown");
    }
    require_nonempty(value.resolved_locked_deck_id, "resolved deck identity");
    require_digest(value.resolved_locked_deck_sha256, "resolved deck");
    require_identity(value.policy_artifact_id, "policy_artifact.v1.", "assignment policy artifact");
    validate_league_context(value.league_context);
    const auto expected = assignment_id_for(value);
    require_identity(value.participant_policy_assignment_id,
                     "participant_policy_assignment.v1.", "participant assignment");
    if (value.participant_policy_assignment_id != expected) {
        throw std::invalid_argument("participant assignment identity mismatch");
    }
}

void validate_initialization(const PolicyRngInitializationIdentity& value) {
    require_nonempty(value.policy_rng_contract_identity, "policy RNG contract identity");
    require_nonempty(value.policy_rng_stream_id, "policy RNG stream ID");
    if (value.policy_rng_contract_identity == kNoPolicyRngContractId) {
        if (value.policy_rng_stream_id != kNoPolicyRngContractId ||
            !value.initialization_material.empty() ||
            value.policy_rng_initialization_identity != kNoPolicyRngContractId) {
            throw std::invalid_argument("no-RNG initialization is not canonical");
        }
        return;
    }
    require_identity(value.policy_rng_initialization_identity,
                     "policy_rng_initialization.v1.", "policy RNG initialization");
    const auto expected = rng_initialization_id_for(value);
    if (value.policy_rng_initialization_identity != expected) {
        throw std::invalid_argument("policy RNG initialization identity mismatch");
    }
}

void validate_stream(const PolicyRngStreamIdentity& value) {
    require_identity(value.policy_artifact_id, "policy_artifact.v1.", "stream artifact");
    require_identity(value.participant_policy_assignment_id,
                     "participant_policy_assignment.v1.", "stream assignment");
    require_nonempty(value.policy_rng_contract_identity, "stream RNG contract");
    require_nonempty(value.policy_rng_stream_id, "stream ID");
    require_nonempty(value.policy_rng_initialization_identity, "stream initialization");
    const auto expected = rng_stream_id_for(value);
    require_identity(value.policy_rng_identity, "policy_rng.v1.", "policy RNG stream");
    if (value.policy_rng_identity != expected) {
        throw std::invalid_argument("policy RNG stream identity mismatch");
    }
}

void validate_rng_decision(const PolicyRngDecisionProvenance& value) {
    require_identity(value.acting_policy_assignment_id,
                     "participant_policy_assignment.v1.", "decision assignment");
    if (!valid_rng_mode(static_cast<std::uint8_t>(value.mode))) {
        throw std::invalid_argument("decision RNG mode is unknown");
    }
    if (value.mode == PolicyRngMode::None) {
        if (value.policy_rng_contract_identity != kNoPolicyRngContractId ||
            value.policy_rng_stream_id != kNoPolicyRngContractId ||
            value.policy_rng_initialization_identity != kNoPolicyRngContractId ||
            value.policy_rng_identity != kNoPolicyRngContractId || value.pre_cursor.has_value() ||
            value.post_cursor.has_value() || value.pre_state.has_value() || value.post_state.has_value()) {
            throw std::invalid_argument("NONE RNG provenance is not canonical");
        }
        return;
    }
    require_identity(value.policy_rng_identity, "policy_rng.v1.", "decision RNG identity");
    require_nonempty(value.policy_rng_contract_identity, "decision RNG contract");
    require_nonempty(value.policy_rng_stream_id, "decision RNG stream");
    require_nonempty(value.policy_rng_initialization_identity, "decision RNG initialization");
    if (value.mode == PolicyRngMode::Cursor) {
        if (!value.pre_cursor.has_value() || !value.post_cursor.has_value() ||
            value.pre_state.has_value() || value.post_state.has_value() ||
            value.policy_rng_contract_identity == kNoPolicyRngContractId) {
            throw std::invalid_argument("CURSOR RNG provenance is not canonical");
        }
    } else if (!value.pre_state.has_value() || !value.post_state.has_value() ||
               value.pre_cursor.has_value() || value.post_cursor.has_value() ||
               value.policy_rng_contract_identity == kNoPolicyRngContractId) {
        throw std::invalid_argument("STATE RNG provenance is not canonical");
    }
}

void validate_public_candidate(const environment::EnvironmentActionCandidate& value) {
    if (!valid_environment_action_kind(static_cast<std::uint8_t>(value.action_kind)) ||
        value.action_kind == environment::EnvironmentActionKind::Unsupported ||
        value.public_action_key.empty()) {
        throw std::invalid_argument("trajectory candidate is unsupported or empty");
    }
    if (!value.continuation_operation.empty() && value.continuation_operation != "pick" &&
        value.continuation_operation != "amount" && value.continuation_operation != "finish" &&
        value.continuation_operation != "cancel" && value.continuation_operation != "bypass") {
        throw std::invalid_argument("trajectory candidate continuation operation is unknown");
    }
    environment::PublicActionKeyInput input;
    input.action_kind = std::string(environment::environment_action_kind_name(value.action_kind));
    input.choice = value.choice;
    input.source_reference = value.source_reference;
    input.target_reference = value.target_reference;
    input.phase = value.phase;
    input.position = value.position;
    input.source_index = value.source_index;
    input.amount = value.amount;
    input.continuation_operation = value.continuation_operation;
    if (environment::public_action_key(input) != value.public_action_key) {
        throw std::invalid_argument("trajectory public action key does not match descriptor");
    }
    if (!environment::is_public_action_key(value.public_action_key)) {
        throw std::invalid_argument("trajectory public action key has invalid identity");
    }
}

void validate_continuation(const environment::EnvironmentContinuationView& value) {
    static constexpr std::string_view valid[] = {
        "unordered", "tribute", "sum", "zone", "counter", "ordering", "announce_mask"};
    if (std::find(std::begin(valid), std::end(valid), value.continuation_kind) == std::end(valid)) {
        throw std::invalid_argument("trajectory continuation kind is unknown");
    }
}

void validate_request(const environment::EnvironmentDecisionRequest& value) {
    if (value.player > 1 ||
        !valid_environment_decision_kind(static_cast<std::uint8_t>(value.kind)) ||
        value.kind == environment::EnvironmentDecisionKind::Unsupported ||
        value.candidates.empty()) {
        throw std::invalid_argument("trajectory request is invalid or incomplete");
    }
    for (const auto& candidate : value.candidates) {
        validate_public_candidate(candidate);
    }
    std::vector<std::string> keys;
    keys.reserve(value.candidates.size());
    for (const auto& candidate : value.candidates) {
        keys.push_back(candidate.public_action_key);
    }
    for (std::size_t left = 0; left < keys.size(); ++left) {
        for (std::size_t right = left + 1; right < keys.size(); ++right) {
            if (keys[left] == keys[right]) {
                throw std::invalid_argument("trajectory request has duplicate public action keys");
            }
        }
    }
    if (!value.continuation.has_value()) {
        for (const auto& candidate : value.candidates) {
            if (!candidate.continuation_operation.empty()) {
                throw std::invalid_argument(
                    "atomic request contains a continuation operation");
            }
            if (!candidate.submits_engine_response) {
                throw std::invalid_argument(
                    "atomic request contains a non-submitting candidate");
            }
        }
        return;
    }

    const auto& continuation = *value.continuation;
    validate_continuation(continuation);
    const auto is_remaining_index = [&continuation](const std::uint32_t source_index) {
        return std::find(continuation.remaining_indices.begin(),
                         continuation.remaining_indices.end(), source_index) !=
               continuation.remaining_indices.end();
    };
    for (const auto& candidate : value.candidates) {
        if (candidate.continuation_operation.empty()) {
            throw std::invalid_argument(
                "continuation request contains a candidate without an operation");
        }
        if (candidate.continuation_operation == "pick") {
            if (candidate.action_kind != environment::EnvironmentActionKind::Pick ||
                candidate.submits_engine_response ||
                continuation.continuation_kind == "counter" ||
                !candidate.source_index.has_value() ||
                !is_remaining_index(*candidate.source_index)) {
                throw std::invalid_argument("pick continuation candidate is structurally invalid");
            }
        } else if (candidate.continuation_operation == "amount") {
            if (candidate.action_kind != environment::EnvironmentActionKind::AssignAmount ||
                candidate.submits_engine_response || continuation.continuation_kind != "counter" ||
                !candidate.source_index.has_value() || !candidate.amount.has_value() ||
                *candidate.amount < 0 || !is_remaining_index(*candidate.source_index)) {
                throw std::invalid_argument(
                    "amount continuation candidate is structurally invalid");
            }
        } else if (candidate.continuation_operation == "finish") {
            if (candidate.action_kind != environment::EnvironmentActionKind::Finish ||
                !candidate.submits_engine_response || !continuation.can_finish) {
                throw std::invalid_argument("finish continuation candidate is structurally invalid");
            }
        } else if (candidate.continuation_operation == "cancel") {
            if (candidate.action_kind != environment::EnvironmentActionKind::Cancel ||
                !candidate.submits_engine_response || !continuation.can_cancel) {
                throw std::invalid_argument("cancel continuation candidate is structurally invalid");
            }
        } else if (candidate.continuation_operation == "bypass") {
            if (candidate.action_kind != environment::EnvironmentActionKind::Cancel ||
                !candidate.submits_engine_response || continuation.continuation_kind != "ordering") {
                throw std::invalid_argument("bypass continuation candidate is structurally invalid");
            }
        }
    }
}

void validate_public_frame(const PublicFrameSnapshot& value) {
    require_contract(value.v2_contract_id, environment::kEpisodicEnvironmentV2ContractId,
                     "V2 contract");
    require_digest(value.episode_semantic_id, "episode");
    validate_request(value.request);
    if (value.acting_player > 1 || value.request.player != value.acting_player ||
        value.public_observation.perspective_player != value.acting_player ||
        value.public_observation.decision_index != value.decision_index) {
        throw std::invalid_argument("trajectory frame perspective or index mismatch");
    }
    if (environment::public_observation_digest(value.public_observation) !=
        value.public_observation_digest) {
        throw std::invalid_argument("trajectory frame observation digest mismatch");
    }
    std::vector<std::string> keys;
    keys.reserve(value.request.candidates.size());
    for (const auto& candidate : value.request.candidates) {
        validate_public_candidate(candidate);
        keys.push_back(candidate.public_action_key);
    }
    for (std::size_t left = 0; left < keys.size(); ++left) {
        for (std::size_t right = left + 1; right < keys.size(); ++right) {
            if (keys[left] == keys[right]) {
                throw std::invalid_argument("trajectory frame has duplicate public action keys");
            }
        }
    }
    if (environment::public_candidate_domain_digest(
            std::string(environment::environment_decision_kind_name(value.request.kind)), keys) !=
        value.public_candidate_domain_digest) {
        throw std::invalid_argument("trajectory frame candidate digest mismatch");
    }
    environment::PublicSemanticDecisionIdentityInput identity;
    identity.episode_semantic_id = value.episode_semantic_id;
    identity.decision_index = value.decision_index;
    identity.acting_player = value.acting_player;
    identity.request_kind = std::string(environment::environment_decision_kind_name(value.request.kind));
    identity.public_observation_digest = value.public_observation_digest;
    identity.public_candidate_domain_digest = value.public_candidate_domain_digest;
    if (environment::public_semantic_decision_id(identity) != value.public_semantic_decision_id) {
        throw std::invalid_argument("trajectory frame decision identity mismatch");
    }
}

void validate_successor(const Successor& value) {
    if (!valid_successor_kind(static_cast<std::uint8_t>(value.kind))) {
        throw std::invalid_argument("successor kind is unknown");
    }
    if (value.kind == SuccessorKind::NextFrame) {
        if (!value.next_frame.has_value()) {
            throw std::invalid_argument("next-frame successor has no target");
        }
        if (!valid_next_frame_target_kind(static_cast<std::uint8_t>(value.next_frame->kind))) {
            throw std::invalid_argument("next-frame target kind is unknown");
        }
        require_digest(value.next_frame->next_public_semantic_decision_id, "successor decision");
    } else if (value.next_frame.has_value()) {
        throw std::invalid_argument("closure successor has an unexpected target");
    }
}

}  // namespace

void ByteWriter::u8(const std::uint8_t value) { data_.push_back(value); }

void ByteWriter::u16be(const std::uint16_t value) {
    data_.push_back(static_cast<std::uint8_t>(value >> 8));
    data_.push_back(static_cast<std::uint8_t>(value));
}

void ByteWriter::u32be(const std::uint32_t value) {
    data_.push_back(static_cast<std::uint8_t>(value >> 24));
    data_.push_back(static_cast<std::uint8_t>(value >> 16));
    data_.push_back(static_cast<std::uint8_t>(value >> 8));
    data_.push_back(static_cast<std::uint8_t>(value));
}

void ByteWriter::u64be(const std::uint64_t value) {
    for (int shift = 56; shift >= 0; shift -= 8) {
        data_.push_back(static_cast<std::uint8_t>(value >> shift));
    }
}

void ByteWriter::i32(const std::int32_t value) { u32be(static_cast<std::uint32_t>(value)); }

void ByteWriter::boolean(const bool value) { u8(value ? 1 : 0); }

void ByteWriter::string(const std::string_view value) {
    require_length(value.size());
    if (!is_valid_utf8(value)) {
        throw std::invalid_argument("trajectory string is not UTF-8");
    }
    u32be(static_cast<std::uint32_t>(value.size()));
    data_.insert(data_.end(), value.begin(), value.end());
}

void ByteWriter::bytes(const std::vector<std::uint8_t>& value) {
    require_length(value.size());
    u32be(static_cast<std::uint32_t>(value.size()));
    raw(value);
}

void ByteWriter::raw(const std::vector<std::uint8_t>& value) {
    data_.insert(data_.end(), value.begin(), value.end());
}

bool ByteReader::u8(std::uint8_t& value) noexcept {
    if (offset_ >= data_.size()) {
        return false;
    }
    value = data_[offset_++];
    return true;
}

bool ByteReader::u16be(std::uint16_t& value) noexcept {
    if (data_.size() - offset_ < 2) {
        return false;
    }
    value = static_cast<std::uint16_t>((static_cast<std::uint16_t>(data_[offset_]) << 8) |
                                       data_[offset_ + 1]);
    offset_ += 2;
    return true;
}

bool ByteReader::u32be(std::uint32_t& value) noexcept {
    if (data_.size() - offset_ < 4) {
        return false;
    }
    value = (static_cast<std::uint32_t>(data_[offset_]) << 24) |
            (static_cast<std::uint32_t>(data_[offset_ + 1]) << 16) |
            (static_cast<std::uint32_t>(data_[offset_ + 2]) << 8) |
            static_cast<std::uint32_t>(data_[offset_ + 3]);
    offset_ += 4;
    return true;
}

bool ByteReader::u64be(std::uint64_t& value) noexcept {
    if (data_.size() - offset_ < 8) {
        return false;
    }
    value = 0;
    for (int shift = 56; shift >= 0; shift -= 8) {
        value |= static_cast<std::uint64_t>(data_[offset_++]) << shift;
    }
    return true;
}

bool ByteReader::i32(std::int32_t& value) noexcept {
    std::uint32_t bits = 0;
    if (!u32be(bits)) {
        return false;
    }
    value = static_cast<std::int32_t>(bits);
    return true;
}

bool ByteReader::boolean(bool& value) noexcept {
    std::uint8_t encoded = 0;
    if (!u8(encoded) || encoded > 1) {
        return false;
    }
    value = encoded == 1;
    return true;
}

bool ByteReader::string(std::string& value) noexcept {
    std::uint32_t length = 0;
    if (!u32be(length) || length > data_.size() - offset_) {
        return false;
    }
    try {
        value.assign(reinterpret_cast<const char*>(data_.data() + offset_), length);
    } catch (...) {
        return false;
    }
    offset_ += length;
    return is_valid_utf8(value);
}

bool ByteReader::bytes(std::vector<std::uint8_t>& value) noexcept {
    std::uint32_t length = 0;
    if (!u32be(length) || length > data_.size() - offset_) {
        return false;
    }
    try {
        value.assign(data_.begin() + static_cast<std::ptrdiff_t>(offset_),
                     data_.begin() + static_cast<std::ptrdiff_t>(offset_ + length));
    } catch (...) {
        return false;
    }
    offset_ += length;
    return true;
}

bool ByteReader::raw(const std::size_t length, std::vector<std::uint8_t>& value) noexcept {
    if (length > data_.size() - offset_) {
        return false;
    }
    try {
        value.assign(data_.begin() + static_cast<std::ptrdiff_t>(offset_),
                     data_.begin() + static_cast<std::ptrdiff_t>(offset_ + length));
    } catch (...) {
        return false;
    }
    offset_ += length;
    return true;
}

bool is_valid_utf8(const std::string_view value) noexcept {
    for (std::size_t index = 0; index < value.size();) {
        const auto first = static_cast<unsigned char>(value[index]);
        if (first <= 0x7f) {
            ++index;
            continue;
        }
        std::size_t width = 0;
        std::uint32_t code_point = 0;
        if (first >= 0xc2 && first <= 0xdf) {
            width = 2;
            code_point = first & 0x1f;
        } else if (first >= 0xe0 && first <= 0xef) {
            width = 3;
            code_point = first & 0x0f;
        } else if (first >= 0xf0 && first <= 0xf4) {
            width = 4;
            code_point = first & 0x07;
        } else {
            return false;
        }
        if (width > value.size() - index) {
            return false;
        }
        for (std::size_t continuation = 1; continuation < width; ++continuation) {
            const auto byte = static_cast<unsigned char>(value[index + continuation]);
            if ((byte & 0xc0) != 0x80) {
                return false;
            }
            code_point = (code_point << 6) | (byte & 0x3f);
        }
        if ((width == 3 && code_point < 0x800) ||
            (width == 4 && code_point < 0x10000) || code_point > 0x10ffff ||
            (code_point >= 0xd800 && code_point <= 0xdfff)) {
            return false;
        }
        index += width;
    }
    return true;
}

bool is_lower_hex_digest(const std::string_view value) noexcept {
    return value.size() == 64 &&
           std::all_of(value.begin(), value.end(), [](const unsigned char character) {
               return (character >= '0' && character <= '9') ||
                      (character >= 'a' && character <= 'f');
           });
}

bool is_canonical_identity(const std::string_view value, const std::string_view prefix) noexcept {
    if (value.size() <= prefix.size() || value.substr(0, prefix.size()) != prefix) {
        return false;
    }
    return is_lower_hex_digest(value.substr(prefix.size()));
}

void write_optional_string(ByteWriter& writer, const std::optional<std::string>& value) {
    writer.u8(value.has_value() ? 1 : 0);
    if (value.has_value()) {
        writer.string(*value);
    }
}

bool read_optional_string(ByteReader& reader, std::optional<std::string>& value) noexcept {
    std::uint8_t present = 0;
    if (!reader.u8(present) || present > 1) {
        return false;
    }
    if (present == 0) {
        value.reset();
        return true;
    }
    std::string decoded;
    if (!reader.string(decoded)) {
        return false;
    }
    value = std::move(decoded);
    return true;
}

void write_optional_u64(ByteWriter& writer, const std::optional<std::uint64_t>& value) {
    writer.u8(value.has_value() ? 1 : 0);
    if (value.has_value()) {
        writer.u64be(*value);
    }
}

bool read_optional_u64(ByteReader& reader, std::optional<std::uint64_t>& value) noexcept {
    std::uint8_t present = 0;
    if (!reader.u8(present) || present > 1) {
        return false;
    }
    if (present == 0) {
        value.reset();
        return true;
    }
    std::uint64_t decoded = 0;
    if (!reader.u64be(decoded)) {
        return false;
    }
    value = decoded;
    return true;
}

void write_optional_bytes(ByteWriter& writer,
                          const std::optional<std::vector<std::uint8_t>>& value) {
    writer.u8(value.has_value() ? 1 : 0);
    if (value.has_value()) {
        writer.bytes(*value);
    }
}

bool read_optional_bytes(ByteReader& reader,
                         std::optional<std::vector<std::uint8_t>>& value) noexcept {
    std::uint8_t present = 0;
    if (!reader.u8(present) || present > 1) {
        return false;
    }
    if (present == 0) {
        value.reset();
        return true;
    }
    std::vector<std::uint8_t> decoded;
    if (!reader.bytes(decoded)) {
        return false;
    }
    value = std::move(decoded);
    return true;
}

void write_optional_u32(ByteWriter& writer, const std::optional<std::uint32_t>& value);

void write_optional_public_choice(ByteWriter& writer,
                                  const std::optional<environment::PublicChoice>& value) {
    writer.u8(value.has_value() ? 1 : 0);
    if (!value.has_value()) {
        return;
    }
    writer.u8(static_cast<std::uint8_t>(value->kind));
    writer.u64be(value->value);
    write_optional_u32(writer, value->response_index);
}

void write_optional_u32(ByteWriter& writer, const std::optional<std::uint32_t>& value) {
    writer.u8(value.has_value() ? 1 : 0);
    if (value.has_value()) {
        writer.u32be(*value);
    }
}

bool read_optional_u32(ByteReader& reader, std::optional<std::uint32_t>& value) noexcept {
    std::uint8_t present = 0;
    if (!reader.u8(present) || present > 1) {
        return false;
    }
    if (present == 0) {
        value.reset();
        return true;
    }
    std::uint32_t decoded = 0;
    if (!reader.u32be(decoded)) {
        return false;
    }
    value = decoded;
    return true;
}

bool read_optional_public_choice(ByteReader& reader,
                                 std::optional<environment::PublicChoice>& value) noexcept {
    std::uint8_t present = 0;
    if (!reader.u8(present) || present > 1) {
        return false;
    }
    if (present == 0) {
        value.reset();
        return true;
    }
    std::uint8_t kind = 0;
    std::uint64_t choice_value = 0;
    if (!reader.u8(kind) || !reader.u64be(choice_value)) {
        return false;
    }
    std::optional<std::uint32_t> response_index;
    if (!read_optional_u32(reader, response_index)) {
        return false;
    }
    if (!valid_public_choice_kind(kind)) {
        return false;
    }
    const auto choice_kind = static_cast<environment::PublicChoiceKind>(kind);
    if ((choice_kind == environment::PublicChoiceKind::YesNo ||
         choice_kind == environment::PublicChoiceKind::EffectYesNo) &&
        (choice_value > 1 || response_index.has_value())) {
        return false;
    }
    if (choice_kind == environment::PublicChoiceKind::EffectChoice &&
        response_index.has_value()) {
        return false;
    }
    if ((choice_kind == environment::PublicChoiceKind::OptionValue ||
         choice_kind == environment::PublicChoiceKind::AnnouncementNumber) &&
        !response_index.has_value()) {
        return false;
    }
    value = environment::PublicChoice{choice_kind, choice_value, response_index};
    return true;
}

void write_optional_reference(ByteWriter& writer,
                             const std::optional<environment::PublicCardReference>& value) {
    writer.u8(value.has_value() ? 1 : 0);
    if (!value.has_value()) {
        return;
    }
    writer.u8(static_cast<std::uint8_t>(value->kind));
    writer.string(value->observation_locator);
}

bool read_optional_reference(ByteReader& reader,
                             std::optional<environment::PublicCardReference>& value) noexcept {
    std::uint8_t present = 0;
    if (!reader.u8(present) || present > 1) {
        return false;
    }
    if (present == 0) {
        value.reset();
        return true;
    }
    std::uint8_t kind = 0;
    std::string locator;
    if (!reader.u8(kind) || !valid_public_card_reference_kind(kind) ||
        !reader.string(locator) || locator.empty()) {
        return false;
    }
    value = environment::PublicCardReference{
        static_cast<environment::PublicCardReferenceKind>(kind), std::move(locator)};
    return true;
}

void write_optional_u8(ByteWriter& writer, const std::optional<std::uint8_t>& value) {
    writer.u8(value.has_value() ? 1 : 0);
    if (value.has_value()) {
        writer.u8(*value);
    }
}

bool read_optional_u8(ByteReader& reader, std::optional<std::uint8_t>& value) noexcept {
    std::uint8_t present = 0;
    if (!reader.u8(present) || present > 1) {
        return false;
    }
    if (present == 0) {
        value.reset();
        return true;
    }
    std::uint8_t decoded = 0;
    if (!reader.u8(decoded)) {
        return false;
    }
    value = decoded;
    return true;
}

void write_optional_i32(ByteWriter& writer, const std::optional<std::int32_t>& value) {
    writer.u8(value.has_value() ? 1 : 0);
    if (value.has_value()) {
        writer.i32(*value);
    }
}

bool read_optional_i32(ByteReader& reader, std::optional<std::int32_t>& value) noexcept {
    std::uint8_t present = 0;
    if (!reader.u8(present) || present > 1) {
        return false;
    }
    if (present == 0) {
        value.reset();
        return true;
    }
    std::int32_t decoded = 0;
    if (!reader.i32(decoded)) {
        return false;
    }
    value = decoded;
    return true;
}

std::vector<std::uint8_t> policy_artifact_identity_input(const PolicyArtifact& value) {
    ByteWriter writer;
    writer.string(kPolicyArtifactIdentityDomain);
    writer.string(kPolicyArtifactIdentityDomain);
    writer.string(kPolicyProvenanceContractId);
    writer.u8(static_cast<std::uint8_t>(value.policy_kind));
    writer.string(value.producer_implementation_identity);
    writer.string(value.inference_adapter_identity);
    writer.string(value.observation_adapter_identity);
    writer.string(value.action_adapter_identity);
    writer.string(value.sampling_contract_identity);
    writer.string(value.policy_rng_contract_identity);
    write_optional_string(writer, value.model_checkpoint_identity);
    write_optional_string(writer, value.search_contract_identity);
    write_optional_string(writer, value.demonstration_source_identity);
    write_optional_string(writer, value.artifact_metadata_identity);
    return std::move(writer).take();
}

std::string policy_artifact_id_for(const PolicyArtifact& value) {
    return "policy_artifact.v1." + trace::sha256_bytes(policy_artifact_identity_input(value));
}

std::vector<std::uint8_t> assignment_identity_input(const ParticipantPolicyAssignment& value) {
    ByteWriter writer;
    writer.string(kParticipantAssignmentIdentityDomain);
    writer.string(kParticipantAssignmentIdentityDomain);
    writer.string(kPolicyProvenanceContractId);
    writer.u8(value.player);
    writer.u8(static_cast<std::uint8_t>(value.seat_role));
    writer.u8(static_cast<std::uint8_t>(value.deck_role));
    writer.string(value.resolved_locked_deck_id);
    writer.string(value.resolved_locked_deck_sha256);
    writer.u8(static_cast<std::uint8_t>(value.policy_role));
    writer.string(value.policy_artifact_id);
    writer.u32be(value.assignment_epoch);
    writer.u64be(value.effective_from_decision_index);
    writer.u8(value.league_context.has_value() ? 1 : 0);
    if (value.league_context.has_value()) {
        writer.u64be(value.league_context->league_generation);
        writer.string(value.league_context->league_member_id);
        writer.string(value.league_context->league_role);
    }
    return std::move(writer).take();
}

std::string assignment_id_for(const ParticipantPolicyAssignment& value) {
    return "participant_policy_assignment.v1." + trace::sha256_bytes(assignment_identity_input(value));
}

std::vector<std::uint8_t> rng_initialization_identity_input(
    const PolicyRngInitializationIdentity& value) {
    ByteWriter writer;
    writer.string(kPolicyRngInitializationIdentityDomain);
    writer.string(kPolicyRngInitializationIdentityDomain);
    writer.string(value.policy_rng_contract_identity);
    writer.string(value.policy_rng_stream_id);
    writer.bytes(value.initialization_material);
    return std::move(writer).take();
}

std::string rng_initialization_id_for(const PolicyRngInitializationIdentity& value) {
    return "policy_rng_initialization.v1." +
           trace::sha256_bytes(rng_initialization_identity_input(value));
}

std::vector<std::uint8_t> rng_stream_identity_input(const PolicyRngStreamIdentity& value) {
    ByteWriter writer;
    writer.string(kPolicyRngStreamIdentityDomain);
    writer.string(kPolicyRngStreamIdentityDomain);
    writer.string(value.policy_artifact_id);
    writer.string(value.participant_policy_assignment_id);
    writer.string(value.policy_rng_contract_identity);
    writer.string(value.policy_rng_stream_id);
    writer.string(value.policy_rng_initialization_identity);
    return std::move(writer).take();
}

std::string rng_stream_id_for(const PolicyRngStreamIdentity& value) {
    return "policy_rng.v1." + trace::sha256_bytes(rng_stream_identity_input(value));
}

std::string compute_policy_artifact_id(const PolicyArtifact& value) {
    return policy_artifact_id_for(value);
}

std::string compute_participant_policy_assignment_id(
    const ParticipantPolicyAssignment& value) {
    return assignment_id_for(value);
}

std::string compute_policy_rng_initialization_id(
    const PolicyRngInitializationIdentity& value) {
    if (value.policy_rng_contract_identity == kNoPolicyRngContractId) {
        return kNoPolicyRngContractId;
    }
    return rng_initialization_id_for(value);
}

std::string compute_policy_rng_stream_id(const PolicyRngStreamIdentity& value) {
    return rng_stream_id_for(value);
}

std::vector<std::uint8_t> canonical_policy_artifact_bytes(const PolicyArtifact& value) {
    validate_policy_artifact(value);
    ByteWriter writer;
    const auto input = policy_artifact_identity_input(value);
    writer.raw(input);
    writer.string(value.policy_artifact_id);
    return std::move(writer).take();
}

DecodeResult<PolicyArtifact> decode_policy_artifact(
    const std::vector<std::uint8_t>& bytes) noexcept {
    try {
        ByteReader reader(bytes);
        PolicyArtifact value;
        std::string domain;
        std::string schema;
        std::string contract;
        std::uint8_t policy_kind = 0;
        if (!reader.string(domain) || domain != kPolicyArtifactIdentityDomain ||
            !reader.string(schema) || schema != kPolicyArtifactIdentityDomain ||
            !reader.string(contract) || contract != kPolicyProvenanceContractId ||
            !reader.u8(policy_kind) || !valid_policy_kind(policy_kind) ||
            !reader.string(value.producer_implementation_identity) ||
            !reader.string(value.inference_adapter_identity) ||
            !reader.string(value.observation_adapter_identity) ||
            !reader.string(value.action_adapter_identity) ||
            !reader.string(value.sampling_contract_identity) ||
            !reader.string(value.policy_rng_contract_identity) ||
            !read_optional_string(reader, value.model_checkpoint_identity) ||
            !read_optional_string(reader, value.search_contract_identity) ||
            !read_optional_string(reader, value.demonstration_source_identity) ||
            !read_optional_string(reader, value.artifact_metadata_identity) ||
            !reader.string(value.policy_artifact_id) || !reader.at_end()) {
            return failure<PolicyArtifact>("malformed policy artifact");
        }
        value.policy_kind = static_cast<PolicyKind>(policy_kind);
        try {
            validate_policy_artifact(value);
        } catch (const std::exception& error) {
            return failure<PolicyArtifact>(error.what());
        }
        if (canonical_policy_artifact_bytes(value) != bytes) {
            return failure<PolicyArtifact>("noncanonical policy artifact");
        }
        return success(std::move(value));
    } catch (...) {
        return failure<PolicyArtifact>("policy artifact decode threw");
    }
}

std::vector<std::uint8_t> canonical_participant_policy_assignment_bytes(
    const ParticipantPolicyAssignment& value) {
    validate_assignment(value);
    ByteWriter writer;
    writer.raw(assignment_identity_input(value));
    writer.string(value.participant_policy_assignment_id);
    return std::move(writer).take();
}

DecodeResult<ParticipantPolicyAssignment> decode_participant_policy_assignment(
    const std::vector<std::uint8_t>& bytes) noexcept {
    try {
        ByteReader reader(bytes);
        ParticipantPolicyAssignment value;
        std::string domain;
        std::string schema;
        std::string contract;
        std::uint8_t seat_role = 0;
        std::uint8_t deck_role = 0;
        std::uint8_t policy_role = 0;
        std::uint8_t present = 0;
        if (!reader.string(domain) || domain != kParticipantAssignmentIdentityDomain ||
            !reader.string(schema) || schema != kParticipantAssignmentIdentityDomain ||
            !reader.string(contract) || contract != kPolicyProvenanceContractId ||
            !reader.u8(value.player) || !reader.u8(seat_role) || !valid_seat_role(seat_role) ||
            !reader.u8(deck_role) || !valid_deck_role(deck_role) ||
            !reader.string(value.resolved_locked_deck_id) ||
            !reader.string(value.resolved_locked_deck_sha256) || !reader.u8(policy_role) ||
            !valid_policy_role(policy_role) || !reader.string(value.policy_artifact_id) ||
            !reader.u32be(value.assignment_epoch) ||
            !reader.u64be(value.effective_from_decision_index) || !reader.u8(present) ||
            present > 1) {
            return failure<ParticipantPolicyAssignment>("malformed participant assignment");
        }
        value.seat_role = static_cast<SeatRole>(seat_role);
        value.deck_role = static_cast<DeckRole>(deck_role);
        value.policy_role = static_cast<PolicyRole>(policy_role);
        if (present == 1) {
            LeagueContext context;
            if (!reader.u64be(context.league_generation) ||
                !reader.string(context.league_member_id) || !reader.string(context.league_role)) {
                return failure<ParticipantPolicyAssignment>("malformed league context");
            }
            value.league_context = std::move(context);
        }
        if (!reader.string(value.participant_policy_assignment_id) || !reader.at_end()) {
            return failure<ParticipantPolicyAssignment>("malformed participant assignment tail");
        }
        validate_assignment(value);
        if (canonical_participant_policy_assignment_bytes(value) != bytes) {
            return failure<ParticipantPolicyAssignment>("noncanonical participant assignment");
        }
        return success(std::move(value));
    } catch (const std::exception& error) {
        return failure<ParticipantPolicyAssignment>(error.what());
    } catch (...) {
        return failure<ParticipantPolicyAssignment>("participant assignment decode threw");
    }
}

std::vector<std::uint8_t> canonical_policy_rng_initialization_identity_bytes(
    const PolicyRngInitializationIdentity& value) {
    validate_initialization(value);
    ByteWriter writer;
    writer.string(kPolicyRngInitializationIdentityDomain);
    writer.string(kPolicyRngInitializationIdentityDomain);
    writer.string(value.policy_rng_contract_identity);
    writer.string(value.policy_rng_stream_id);
    writer.bytes(value.initialization_material);
    writer.string(value.policy_rng_initialization_identity);
    return std::move(writer).take();
}

DecodeResult<PolicyRngInitializationIdentity> decode_policy_rng_initialization_identity(
    const std::vector<std::uint8_t>& bytes) noexcept {
    try {
        ByteReader reader(bytes);
        PolicyRngInitializationIdentity value;
        std::string domain;
        std::string schema;
        if (!reader.string(domain) || domain != kPolicyRngInitializationIdentityDomain ||
            !reader.string(schema) || schema != kPolicyRngInitializationIdentityDomain ||
            !reader.string(value.policy_rng_contract_identity) ||
            !reader.string(value.policy_rng_stream_id) ||
            !reader.bytes(value.initialization_material) ||
            !reader.string(value.policy_rng_initialization_identity) || !reader.at_end()) {
            return failure<PolicyRngInitializationIdentity>("malformed RNG initialization");
        }
        validate_initialization(value);
        if (canonical_policy_rng_initialization_identity_bytes(value) != bytes) {
            return failure<PolicyRngInitializationIdentity>("noncanonical RNG initialization");
        }
        return success(std::move(value));
    } catch (const std::exception& error) {
        return failure<PolicyRngInitializationIdentity>(error.what());
    } catch (...) {
        return failure<PolicyRngInitializationIdentity>("RNG initialization decode threw");
    }
}

std::vector<std::uint8_t> canonical_policy_rng_stream_identity_bytes(
    const PolicyRngStreamIdentity& value) {
    validate_stream(value);
    ByteWriter writer;
    writer.raw(rng_stream_identity_input(value));
    writer.string(value.policy_rng_identity);
    return std::move(writer).take();
}

DecodeResult<PolicyRngStreamIdentity> decode_policy_rng_stream_identity(
    const std::vector<std::uint8_t>& bytes) noexcept {
    try {
        ByteReader reader(bytes);
        PolicyRngStreamIdentity value;
        std::string domain;
        std::string schema;
        if (!reader.string(domain) || domain != kPolicyRngStreamIdentityDomain ||
            !reader.string(schema) || schema != kPolicyRngStreamIdentityDomain ||
            !reader.string(value.policy_artifact_id) ||
            !reader.string(value.participant_policy_assignment_id) ||
            !reader.string(value.policy_rng_contract_identity) ||
            !reader.string(value.policy_rng_stream_id) ||
            !reader.string(value.policy_rng_initialization_identity) ||
            !reader.string(value.policy_rng_identity) || !reader.at_end()) {
            return failure<PolicyRngStreamIdentity>("malformed RNG stream identity");
        }
        validate_stream(value);
        if (canonical_policy_rng_stream_identity_bytes(value) != bytes) {
            return failure<PolicyRngStreamIdentity>("noncanonical RNG stream identity");
        }
        return success(std::move(value));
    } catch (const std::exception& error) {
        return failure<PolicyRngStreamIdentity>(error.what());
    } catch (...) {
        return failure<PolicyRngStreamIdentity>("RNG stream decode threw");
    }
}

std::vector<std::uint8_t> canonical_policy_rng_decision_provenance_bytes(
    const PolicyRngDecisionProvenance& value) {
    validate_rng_decision(value);
    ByteWriter writer;
    writer.string(kPolicyRngDecisionProvenanceDomain);
    writer.string(kPolicyRngDecisionProvenanceDomain);
    writer.u64be(value.decision_index);
    writer.string(value.acting_policy_assignment_id);
    writer.string(value.policy_rng_identity);
    writer.string(value.policy_rng_contract_identity);
    writer.string(value.policy_rng_stream_id);
    writer.string(value.policy_rng_initialization_identity);
    writer.u8(static_cast<std::uint8_t>(value.mode));
    switch (value.mode) {
    case PolicyRngMode::None:
        break;
    case PolicyRngMode::Cursor:
        writer.u64be(*value.pre_cursor);
        writer.u64be(*value.post_cursor);
        break;
    case PolicyRngMode::State:
        writer.bytes(*value.pre_state);
        writer.bytes(*value.post_state);
        break;
    }
    return std::move(writer).take();
}

DecodeResult<PolicyRngDecisionProvenance> decode_policy_rng_decision_provenance(
    const std::vector<std::uint8_t>& bytes) noexcept {
    try {
        ByteReader reader(bytes);
        PolicyRngDecisionProvenance value;
        std::string domain;
        std::string schema;
        std::uint8_t mode = 0;
        if (!reader.string(domain) || domain != kPolicyRngDecisionProvenanceDomain ||
            !reader.string(schema) || schema != kPolicyRngDecisionProvenanceDomain ||
            !reader.u64be(value.decision_index) ||
            !reader.string(value.acting_policy_assignment_id) ||
            !reader.string(value.policy_rng_identity) ||
            !reader.string(value.policy_rng_contract_identity) ||
            !reader.string(value.policy_rng_stream_id) ||
            !reader.string(value.policy_rng_initialization_identity) || !reader.u8(mode) ||
            !valid_rng_mode(mode)) {
            return failure<PolicyRngDecisionProvenance>("malformed RNG decision provenance");
        }
        value.mode = static_cast<PolicyRngMode>(mode);
        if (value.mode == PolicyRngMode::Cursor) {
            std::uint64_t pre = 0;
            std::uint64_t post = 0;
            if (!reader.u64be(pre) || !reader.u64be(post)) {
                return failure<PolicyRngDecisionProvenance>("malformed cursor provenance");
            }
            value.pre_cursor = pre;
            value.post_cursor = post;
        } else if (value.mode == PolicyRngMode::State) {
            std::vector<std::uint8_t> pre;
            std::vector<std::uint8_t> post;
            if (!reader.bytes(pre) || !reader.bytes(post)) {
                return failure<PolicyRngDecisionProvenance>("malformed state provenance");
            }
            value.pre_state = std::move(pre);
            value.post_state = std::move(post);
        }
        if (!reader.at_end()) {
            return failure<PolicyRngDecisionProvenance>("RNG decision provenance has trailing bytes");
        }
        validate_rng_decision(value);
        if (canonical_policy_rng_decision_provenance_bytes(value) != bytes) {
            return failure<PolicyRngDecisionProvenance>("noncanonical RNG decision provenance");
        }
        return success(std::move(value));
    } catch (const std::exception& error) {
        return failure<PolicyRngDecisionProvenance>(error.what());
    } catch (...) {
        return failure<PolicyRngDecisionProvenance>("RNG decision provenance decode threw");
    }
}

std::vector<std::uint8_t> canonical_collection_disposition_bytes(
    const CollectionDisposition& value) {
    if (value.kind == CollectionDispositionKind::Clean) {
        if (!value.policy_rejections.empty()) {
            throw std::invalid_argument("clean disposition contains rejection evidence");
        }
    } else if (value.kind == CollectionDispositionKind::QuarantinedAfterPolicyRejection) {
        if (value.policy_rejections.empty()) {
            throw std::invalid_argument("quarantine disposition has no rejection");
        }
        for (const auto rejection : value.policy_rejections) {
            if (!valid_rejection_code(static_cast<std::uint8_t>(rejection))) {
                throw std::invalid_argument("quarantine has unknown rejection code");
            }
        }
    } else {
        throw std::invalid_argument("collection disposition is unknown");
    }
    ByteWriter writer;
    writer.u8(static_cast<std::uint8_t>(value.kind));
    if (value.kind == CollectionDispositionKind::QuarantinedAfterPolicyRejection) {
        require_length(value.policy_rejections.size());
        writer.u32be(static_cast<std::uint32_t>(value.policy_rejections.size()));
        for (const auto rejection : value.policy_rejections) {
            writer.string(environment::rejection_code_name(rejection));
        }
    }
    return std::move(writer).take();
}

DecodeResult<CollectionDisposition> decode_collection_disposition(
    const std::vector<std::uint8_t>& bytes) noexcept {
    try {
        ByteReader reader(bytes);
        CollectionDisposition value;
        std::uint8_t kind = 0;
        if (!reader.u8(kind) || !valid_collection_disposition_kind(kind)) {
            return failure<CollectionDisposition>("unknown collection disposition");
        }
        value.kind = static_cast<CollectionDispositionKind>(kind);
        if (value.kind == CollectionDispositionKind::QuarantinedAfterPolicyRejection) {
            std::uint32_t count = 0;
            if (!reader.u32be(count)) {
                return failure<CollectionDisposition>("missing quarantine count");
            }
            if (count > reader.remaining() / 4) {
                return failure<CollectionDisposition>(
                    "quarantine count exceeds the remaining input");
            }
            value.policy_rejections.reserve(count);
            for (std::uint32_t index = 0; index < count; ++index) {
                std::string token;
                if (!reader.string(token)) {
                    return failure<CollectionDisposition>("malformed quarantine token");
                }
                bool found = false;
                for (std::uint8_t code = 0; valid_rejection_code(code); ++code) {
                    const auto rejection = static_cast<environment::RejectionCode>(code);
                    if (token == environment::rejection_code_name(rejection)) {
                        value.policy_rejections.push_back(rejection);
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    return failure<CollectionDisposition>("unknown quarantine token");
                }
            }
        }
        if (!reader.at_end()) {
            return failure<CollectionDisposition>("collection disposition has trailing bytes");
        }
        const auto canonical = canonical_collection_disposition_bytes(value);
        if (canonical != bytes) {
            return failure<CollectionDisposition>("noncanonical collection disposition");
        }
        return success(std::move(value));
    } catch (const std::exception& error) {
        return failure<CollectionDisposition>(error.what());
    } catch (...) {
        return failure<CollectionDisposition>("collection disposition decode threw");
    }
}

bool read_policy_artifact_direct(ByteReader& reader, PolicyArtifact& value) noexcept {
    std::string domain;
    std::string schema;
    std::string contract;
    std::uint8_t policy_kind = 0;
    if (!reader.string(domain) || domain != kPolicyArtifactIdentityDomain ||
        !reader.string(schema) || schema != kPolicyArtifactIdentityDomain ||
        !reader.string(contract) || contract != kPolicyProvenanceContractId ||
        !reader.u8(policy_kind) || !valid_policy_kind(policy_kind) ||
        !reader.string(value.producer_implementation_identity) ||
        !reader.string(value.inference_adapter_identity) ||
        !reader.string(value.observation_adapter_identity) ||
        !reader.string(value.action_adapter_identity) ||
        !reader.string(value.sampling_contract_identity) ||
        !reader.string(value.policy_rng_contract_identity) ||
        !read_optional_string(reader, value.model_checkpoint_identity) ||
        !read_optional_string(reader, value.search_contract_identity) ||
        !read_optional_string(reader, value.demonstration_source_identity) ||
        !read_optional_string(reader, value.artifact_metadata_identity) ||
        !reader.string(value.policy_artifact_id)) {
        return false;
    }
    value.policy_kind = static_cast<PolicyKind>(policy_kind);
    try {
        validate_policy_artifact(value);
    } catch (...) {
        return false;
    }
    return true;
}

bool read_assignment_direct(ByteReader& reader,
                            ParticipantPolicyAssignment& value) noexcept {
    std::string domain;
    std::string schema;
    std::string contract;
    std::uint8_t seat_role = 0;
    std::uint8_t deck_role = 0;
    std::uint8_t policy_role = 0;
    std::uint8_t present = 0;
    if (!reader.string(domain) || domain != kParticipantAssignmentIdentityDomain ||
        !reader.string(schema) || schema != kParticipantAssignmentIdentityDomain ||
        !reader.string(contract) || contract != kPolicyProvenanceContractId ||
        !reader.u8(value.player) || !reader.u8(seat_role) || !valid_seat_role(seat_role) ||
        !reader.u8(deck_role) || !valid_deck_role(deck_role) ||
        !reader.string(value.resolved_locked_deck_id) ||
        !reader.string(value.resolved_locked_deck_sha256) || !reader.u8(policy_role) ||
        !valid_policy_role(policy_role) || !reader.string(value.policy_artifact_id) ||
        !reader.u32be(value.assignment_epoch) ||
        !reader.u64be(value.effective_from_decision_index) || !reader.u8(present) ||
        present > 1) {
        return false;
    }
    value.seat_role = static_cast<SeatRole>(seat_role);
    value.deck_role = static_cast<DeckRole>(deck_role);
    value.policy_role = static_cast<PolicyRole>(policy_role);
    if (present == 1) {
        LeagueContext context;
        if (!reader.u64be(context.league_generation) || !reader.string(context.league_member_id) ||
            !reader.string(context.league_role)) {
            return false;
        }
        value.league_context = std::move(context);
    } else {
        value.league_context.reset();
    }
    if (!reader.string(value.participant_policy_assignment_id)) {
        return false;
    }
    try {
        validate_assignment(value);
    } catch (...) {
        return false;
    }
    return true;
}

bool read_policy_provenance_direct(ByteReader& reader,
                                   PolicyProvenanceEnvelope& value) noexcept {
    std::string domain;
    std::string schema;
    std::uint32_t count = 0;
    if (!reader.string(domain) || domain != kPolicyProvenanceContractId ||
        !reader.string(schema) || schema != kPolicyProvenanceContractId ||
        !reader.u32be(count)) {
        return false;
    }
    if (count > reader.remaining()) {
        return false;
    }
    try {
        value.policy_artifacts.clear();
        value.policy_artifacts.reserve(count);
        value.participant_assignments.clear();
    } catch (...) {
        return false;
    }
    for (std::uint32_t index = 0; index < count; ++index) {
        PolicyArtifact artifact;
        if (!read_policy_artifact_direct(reader, artifact)) {
            return false;
        }
        value.policy_artifacts.push_back(std::move(artifact));
    }
    if (!reader.u32be(count)) {
        return false;
    }
    if (count > reader.remaining()) {
        return false;
    }
    try {
        value.participant_assignments.reserve(count);
    } catch (...) {
        return false;
    }
    for (std::uint32_t index = 0; index < count; ++index) {
        ParticipantPolicyAssignment assignment;
        if (!read_assignment_direct(reader, assignment)) {
            return false;
        }
        value.participant_assignments.push_back(std::move(assignment));
    }
    try {
        (void)canonical_policy_provenance_envelope_bytes(value);
    } catch (...) {
        return false;
    }
    return true;
}

bool read_disposition_direct(ByteReader& reader, CollectionDisposition& value) noexcept {
    std::uint8_t kind = 0;
    if (!reader.u8(kind) || !valid_collection_disposition_kind(kind)) {
        return false;
    }
    value.kind = static_cast<CollectionDispositionKind>(kind);
    value.policy_rejections.clear();
    if (value.kind == CollectionDispositionKind::Clean) {
        return true;
    }
    std::uint32_t count = 0;
    if (!reader.u32be(count)) {
        return false;
    }
    if (count > reader.remaining() / 4) {
        return false;
    }
    try {
        value.policy_rejections.reserve(count);
    } catch (...) {
        return false;
    }
    for (std::uint32_t index = 0; index < count; ++index) {
        std::string token;
        if (!reader.string(token)) {
            return false;
        }
        bool found = false;
        for (std::uint8_t code = 0; valid_rejection_code(code); ++code) {
            const auto rejection = static_cast<environment::RejectionCode>(code);
            if (token == environment::rejection_code_name(rejection)) {
                value.policy_rejections.push_back(rejection);
                found = true;
                break;
            }
        }
        if (!found) {
            return false;
        }
    }
    try {
        (void)canonical_collection_disposition_bytes(value);
    } catch (...) {
        return false;
    }
    return true;
}

std::vector<std::uint8_t> canonical_policy_provenance_envelope_bytes(
    const PolicyProvenanceEnvelope& value) {
    std::vector<std::string> artifact_ids;
    artifact_ids.reserve(value.policy_artifacts.size());
    for (const auto& artifact : value.policy_artifacts) {
        validate_policy_artifact(artifact);
        artifact_ids.push_back(artifact.policy_artifact_id);
    }
    require_strictly_sorted(artifact_ids, "policy artifacts");
    std::vector<std::string> assignment_ids;
    assignment_ids.reserve(value.participant_assignments.size());
    for (const auto& assignment : value.participant_assignments) {
        validate_assignment(assignment);
        assignment_ids.push_back(assignment.participant_policy_assignment_id);
    }
    require_strictly_sorted(assignment_ids, "participant assignments");

    ByteWriter writer;
    writer.string(kPolicyProvenanceContractId);
    writer.string(kPolicyProvenanceContractId);
    require_length(value.policy_artifacts.size());
    writer.u32be(static_cast<std::uint32_t>(value.policy_artifacts.size()));
    for (const auto& artifact : value.policy_artifacts) {
        writer.raw(canonical_policy_artifact_bytes(artifact));
    }
    require_length(value.participant_assignments.size());
    writer.u32be(static_cast<std::uint32_t>(value.participant_assignments.size()));
    for (const auto& assignment : value.participant_assignments) {
        writer.raw(canonical_participant_policy_assignment_bytes(assignment));
    }
    return std::move(writer).take();
}

DecodeResult<PolicyProvenanceEnvelope> decode_policy_provenance_envelope(
    const std::vector<std::uint8_t>& bytes) noexcept {
    try {
        ByteReader reader(bytes);
        PolicyProvenanceEnvelope value;
        // Use the direct parser so nested entries are consumed in linear time;
        // each entry is self-delimiting by its fixed field order and length
        // prefixed strings/optionals.
        if (!read_policy_provenance_direct(reader, value)) {
            return failure<PolicyProvenanceEnvelope>("malformed provenance envelope entries");
        }
        if (!reader.at_end()) {
            return failure<PolicyProvenanceEnvelope>("provenance envelope has trailing bytes");
        }
        const auto canonical = canonical_policy_provenance_envelope_bytes(value);
        if (canonical != bytes) {
            return failure<PolicyProvenanceEnvelope>("noncanonical provenance envelope");
        }
        return success(std::move(value));
    } catch (const std::exception& error) {
        return failure<PolicyProvenanceEnvelope>(error.what());
    } catch (...) {
        return failure<PolicyProvenanceEnvelope>("provenance envelope decode threw");
    }
}

std::optional<environment::EnvironmentActionKind> action_kind_from_token(
    const std::string_view token) noexcept {
    static constexpr std::pair<std::string_view, environment::EnvironmentActionKind> values[] = {
        {"idle_command", environment::EnvironmentActionKind::IdleCommand},
        {"battle_command", environment::EnvironmentActionKind::BattleCommand},
        {"chain", environment::EnvironmentActionKind::Chain},
        {"option", environment::EnvironmentActionKind::Option},
        {"card_selection", environment::EnvironmentActionKind::CardSelection},
        {"announcement", environment::EnvironmentActionKind::Announcement},
        {"place", environment::EnvironmentActionKind::Place},
        {"position", environment::EnvironmentActionKind::Position},
        {"yes_no", environment::EnvironmentActionKind::YesNo},
        {"pick", environment::EnvironmentActionKind::Pick},
        {"finish", environment::EnvironmentActionKind::Finish},
        {"cancel", environment::EnvironmentActionKind::Cancel},
        {"assign_amount", environment::EnvironmentActionKind::AssignAmount},
    };
    for (const auto& entry : values) {
        if (entry.first == token) {
            return entry.second;
        }
    }
    return std::nullopt;
}

std::optional<environment::EnvironmentDecisionKind> decision_kind_from_token(
    const std::string_view token) noexcept {
    static constexpr std::pair<std::string_view, environment::EnvironmentDecisionKind> values[] = {
        {"idle_command", environment::EnvironmentDecisionKind::IdleCommand},
        {"battle_command", environment::EnvironmentDecisionKind::BattleCommand},
        {"chain", environment::EnvironmentDecisionKind::Chain},
        {"option", environment::EnvironmentDecisionKind::Option},
        {"card_selection", environment::EnvironmentDecisionKind::CardSelection},
        {"tribute", environment::EnvironmentDecisionKind::Tribute},
        {"sum", environment::EnvironmentDecisionKind::Sum},
        {"place", environment::EnvironmentDecisionKind::Place},
        {"counter", environment::EnvironmentDecisionKind::Counter},
        {"ordering", environment::EnvironmentDecisionKind::Ordering},
        {"announcement", environment::EnvironmentDecisionKind::Announcement},
        {"unselect_card", environment::EnvironmentDecisionKind::UnselectCard},
        {"position", environment::EnvironmentDecisionKind::Position},
        {"yes_no", environment::EnvironmentDecisionKind::YesNo},
    };
    for (const auto& entry : values) {
        if (entry.first == token) {
            return entry.second;
        }
    }
    return std::nullopt;
}

void write_u32_vector(ByteWriter& writer, const std::vector<std::uint32_t>& values) {
    require_length(values.size());
    writer.u32be(static_cast<std::uint32_t>(values.size()));
    for (const auto value : values) {
        writer.u32be(value);
    }
}

bool read_u32_vector(ByteReader& reader, std::vector<std::uint32_t>& values) noexcept {
    std::uint32_t count = 0;
    if (!reader.u32be(count)) {
        return false;
    }
    if (count > reader.remaining() / 4) {
        return false;
    }
    values.clear();
    try {
        values.reserve(count);
    } catch (...) {
        return false;
    }
    for (std::uint32_t index = 0; index < count; ++index) {
        std::uint32_t value = 0;
        if (!reader.u32be(value)) {
            return false;
        }
        values.push_back(value);
    }
    return true;
}

void write_u16_vector(ByteWriter& writer, const std::vector<std::uint16_t>& values) {
    require_length(values.size());
    writer.u32be(static_cast<std::uint32_t>(values.size()));
    for (const auto value : values) {
        writer.u16be(value);
    }
}

bool read_u16_vector(ByteReader& reader, std::vector<std::uint16_t>& values) noexcept {
    std::uint32_t count = 0;
    if (!reader.u32be(count)) {
        return false;
    }
    if (count > reader.remaining() / 2) {
        return false;
    }
    values.clear();
    try {
        values.reserve(count);
    } catch (...) {
        return false;
    }
    for (std::uint32_t index = 0; index < count; ++index) {
        std::uint16_t value = 0;
        if (!reader.u16be(value)) {
            return false;
        }
        values.push_back(value);
    }
    return true;
}

bool read_public_candidate(ByteReader& reader,
                           environment::EnvironmentActionCandidate& value) noexcept {
    std::string schema;
    std::string action_token;
    std::uint8_t position = 0;
    if (!reader.string(schema) || schema != kCandidateSchema || !reader.string(action_token)) {
        return false;
    }
    const auto kind = action_kind_from_token(action_token);
    if (!kind.has_value() || !reader.string(value.public_action_key) ||
        !read_optional_public_choice(reader, value.choice) ||
        !read_optional_reference(reader, value.source_reference) ||
        !read_optional_reference(reader, value.target_reference) ||
        !read_optional_u32(reader, value.phase) ||
        !read_optional_u8(reader, value.position) ||
        !read_optional_u32(reader, value.source_index) ||
        !read_optional_i32(reader, value.amount) ||
        !reader.string(value.continuation_operation) || !reader.boolean(value.submits_engine_response)) {
        return false;
    }
    value.action_kind = *kind;
    try {
        validate_public_candidate(value);
    } catch (...) {
        return false;
    }
    (void)position;
    return true;
}

std::vector<std::uint8_t> canonical_public_environment_action_candidate_bytes(
    const environment::EnvironmentActionCandidate& value) {
    validate_public_candidate(value);
    ByteWriter writer;
    writer.string(kCandidateSchema);
    writer.string(environment::environment_action_kind_name(value.action_kind));
    writer.string(value.public_action_key);
    write_optional_public_choice(writer, value.choice);
    write_optional_reference(writer, value.source_reference);
    write_optional_reference(writer, value.target_reference);
    write_optional_u32(writer, value.phase);
    write_optional_u8(writer, value.position);
    write_optional_u32(writer, value.source_index);
    write_optional_i32(writer, value.amount);
    writer.string(value.continuation_operation);
    writer.boolean(value.submits_engine_response);
    return std::move(writer).take();
}

DecodeResult<environment::EnvironmentActionCandidate>
decode_public_environment_action_candidate(const std::vector<std::uint8_t>& bytes) noexcept {
    try {
        ByteReader reader(bytes);
        environment::EnvironmentActionCandidate value;
        if (!read_public_candidate(reader, value) || !reader.at_end()) {
            return failure<environment::EnvironmentActionCandidate>("malformed public candidate");
        }
        if (canonical_public_environment_action_candidate_bytes(value) != bytes) {
            return failure<environment::EnvironmentActionCandidate>("noncanonical public candidate");
        }
        return success(std::move(value));
    } catch (const std::exception& error) {
        return failure<environment::EnvironmentActionCandidate>(error.what());
    } catch (...) {
        return failure<environment::EnvironmentActionCandidate>("public candidate decode threw");
    }
}

bool read_continuation(ByteReader& reader,
                       environment::EnvironmentContinuationView& value) noexcept {
    std::string schema;
    if (!reader.string(schema) || schema != kCandidateSchema ||
        !reader.string(value.continuation_kind) || !reader.u32be(value.continuation_step) ||
        !read_u32_vector(reader, value.selected_indices) ||
        !read_u32_vector(reader, value.remaining_indices) ||
        !read_u16_vector(reader, value.assigned_amounts) ||
        !reader.u32be(value.min_count) || !reader.u32be(value.max_count) ||
        !reader.u32be(value.target_sum) || !reader.u32be(value.required_amount) ||
        !reader.u64be(value.available_mask) || !reader.u64be(value.selected_mask) ||
        !reader.u32be(value.continuation_steps) || !reader.boolean(value.exact_sum) ||
        !reader.boolean(value.greater_sum) || !reader.boolean(value.can_finish) ||
        !reader.boolean(value.can_cancel)) {
        return false;
    }
    try {
        validate_continuation(value);
    } catch (...) {
        return false;
    }
    return true;
}

std::vector<std::uint8_t> canonical_public_environment_continuation_bytes(
    const environment::EnvironmentContinuationView& value) {
    validate_continuation(value);
    ByteWriter writer;
    writer.string(kCandidateSchema);
    writer.string(value.continuation_kind);
    writer.u32be(value.continuation_step);
    write_u32_vector(writer, value.selected_indices);
    write_u32_vector(writer, value.remaining_indices);
    write_u16_vector(writer, value.assigned_amounts);
    writer.u32be(value.min_count);
    writer.u32be(value.max_count);
    writer.u32be(value.target_sum);
    writer.u32be(value.required_amount);
    writer.u64be(value.available_mask);
    writer.u64be(value.selected_mask);
    writer.u32be(value.continuation_steps);
    writer.boolean(value.exact_sum);
    writer.boolean(value.greater_sum);
    writer.boolean(value.can_finish);
    writer.boolean(value.can_cancel);
    return std::move(writer).take();
}

DecodeResult<environment::EnvironmentContinuationView> decode_public_environment_continuation(
    const std::vector<std::uint8_t>& bytes) noexcept {
    try {
        ByteReader reader(bytes);
        environment::EnvironmentContinuationView value;
        if (!read_continuation(reader, value) || !reader.at_end()) {
            return failure<environment::EnvironmentContinuationView>("malformed continuation");
        }
        if (canonical_public_environment_continuation_bytes(value) != bytes) {
            return failure<environment::EnvironmentContinuationView>("noncanonical continuation");
        }
        return success(std::move(value));
    } catch (const std::exception& error) {
        return failure<environment::EnvironmentContinuationView>(error.what());
    } catch (...) {
        return failure<environment::EnvironmentContinuationView>("continuation decode threw");
    }
}

std::vector<std::uint8_t> canonical_public_environment_decision_request_bytes(
    const environment::EnvironmentDecisionRequest& value) {
    validate_request(value);
    ByteWriter writer;
    writer.string(kCandidateSchema);
    writer.string(environment::environment_decision_kind_name(value.kind));
    writer.u8(value.player);
    require_length(value.candidates.size());
    writer.u32be(static_cast<std::uint32_t>(value.candidates.size()));
    for (const auto& candidate : value.candidates) {
        writer.raw(canonical_public_environment_action_candidate_bytes(candidate));
    }
    writer.u8(value.continuation.has_value() ? 1 : 0);
    if (value.continuation.has_value()) {
        writer.raw(canonical_public_environment_continuation_bytes(*value.continuation));
    }
    return std::move(writer).take();
}

DecodeResult<environment::EnvironmentDecisionRequest> decode_public_environment_decision_request(
    const std::vector<std::uint8_t>& bytes) noexcept {
    try {
        ByteReader reader(bytes);
        environment::EnvironmentDecisionRequest value;
        std::string schema;
        std::string kind_token;
        std::uint32_t count = 0;
        if (!reader.string(schema) || schema != kCandidateSchema ||
            !reader.string(kind_token) || !reader.u8(value.player) || value.player > 1 ||
            !reader.u32be(count)) {
            return failure<environment::EnvironmentDecisionRequest>("malformed decision request header");
        }
        if (count > reader.remaining()) {
            return failure<environment::EnvironmentDecisionRequest>(
                "decision candidate count exceeds the remaining input");
        }
        const auto kind = decision_kind_from_token(kind_token);
        if (!kind.has_value()) {
            return failure<environment::EnvironmentDecisionRequest>("unknown decision kind");
        }
        value.kind = *kind;
        value.candidates.reserve(count);
        for (std::uint32_t index = 0; index < count; ++index) {
            environment::EnvironmentActionCandidate candidate;
            if (!read_public_candidate(reader, candidate)) {
                return failure<environment::EnvironmentDecisionRequest>("malformed candidate entry");
            }
            value.candidates.push_back(std::move(candidate));
        }
        std::uint8_t present = 0;
        if (!reader.u8(present) || present > 1) {
            return failure<environment::EnvironmentDecisionRequest>("invalid continuation presence");
        }
        if (present == 1) {
            environment::EnvironmentContinuationView continuation;
            if (!read_continuation(reader, continuation)) {
                return failure<environment::EnvironmentDecisionRequest>("malformed continuation entry");
            }
            value.continuation = std::move(continuation);
        }
        if (!reader.at_end()) {
            return failure<environment::EnvironmentDecisionRequest>("decision request has trailing bytes");
        }
        validate_request(value);
        if (canonical_public_environment_decision_request_bytes(value) != bytes) {
            return failure<environment::EnvironmentDecisionRequest>("noncanonical decision request");
        }
        return success(std::move(value));
    } catch (const std::exception& error) {
        return failure<environment::EnvironmentDecisionRequest>(error.what());
    } catch (...) {
        return failure<environment::EnvironmentDecisionRequest>("decision request decode threw");
    }
}

bool read_public_request_direct(ByteReader& reader,
                                environment::EnvironmentDecisionRequest& value) noexcept {
    std::string schema;
    std::string kind_token;
    std::uint32_t count = 0;
    if (!reader.string(schema) || schema != kCandidateSchema ||
        !reader.string(kind_token) || !reader.u8(value.player) || value.player > 1 ||
        !reader.u32be(count)) {
        return false;
    }
    if (count > reader.remaining()) {
        return false;
    }
    const auto kind = decision_kind_from_token(kind_token);
    if (!kind.has_value()) {
        return false;
    }
    value.kind = *kind;
    try {
        value.candidates.clear();
        value.candidates.reserve(count);
    } catch (...) {
        return false;
    }
    for (std::uint32_t index = 0; index < count; ++index) {
        environment::EnvironmentActionCandidate candidate;
        if (!read_public_candidate(reader, candidate)) {
            return false;
        }
        value.candidates.push_back(std::move(candidate));
    }
    std::uint8_t present = 0;
    if (!reader.u8(present) || present > 1) {
        return false;
    }
    if (present == 1) {
        environment::EnvironmentContinuationView continuation;
        if (!read_continuation(reader, continuation)) {
            return false;
        }
        value.continuation = std::move(continuation);
    } else {
        value.continuation.reset();
    }
    try {
        validate_request(value);
    } catch (...) {
        return false;
    }
    return true;
}

bool read_public_observation_direct(ByteReader& reader,
                                    environment::PublicEnvironmentObservationInput& value) noexcept {
    const auto start = reader.position();
    std::string schema;
    std::uint8_t perspective = 0;
    std::uint64_t decision_index = 0;
    std::vector<std::uint8_t> ignored_safe_state;
    if (!reader.string(schema) || schema != environment::kPublicEnvironmentObservationSchemaId ||
        !reader.string(schema) || schema != environment::kPublicEnvironmentObservationSchemaId ||
        !reader.u8(perspective) || perspective > 1 || !reader.u64be(decision_index) ||
        !reader.bytes(ignored_safe_state)) {
        return false;
    }
    std::uint8_t present = 0;
    if (!reader.u8(present) || present > 1) {
        return false;
    }
    if (present == 1) {
        std::string kind;
        if (!reader.string(kind)) {
            return false;
        }
    }
    if (!reader.u8(present) || present > 1) {
        return false;
    }
    if (present == 1) {
        std::uint8_t player = 0;
        if (!reader.u8(player) || player > 1) {
            return false;
        }
    }
    std::uint32_t count = 0;
    if (!reader.u32be(count)) {
        return false;
    }
    for (std::uint32_t index = 0; index < count; ++index) {
        std::string reference;
        if (!reader.string(reference)) {
            return false;
        }
    }
    const auto end = reader.position();
    std::vector<std::uint8_t> encoded(
        reader.data().begin() + static_cast<std::ptrdiff_t>(start),
        reader.data().begin() + static_cast<std::ptrdiff_t>(end));
    if (!environment::decode_canonical_public_environment_observation(encoded, value)) {
        return false;
    }
    return reader.set_position(end);
}

bool read_public_frame(ByteReader& reader, PublicFrameSnapshot& value) noexcept {
    std::string schema;
    if (!reader.string(schema) || schema != kFrameSchema ||
        !reader.string(value.v2_contract_id) ||
        value.v2_contract_id != environment::kEpisodicEnvironmentV2ContractId ||
        !reader.string(value.episode_semantic_id) ||
        !reader.string(value.public_semantic_decision_id) ||
        !reader.u64be(value.decision_index) || !reader.u8(value.acting_player) ||
        value.acting_player > 1) {
        return false;
    }
    if (!read_public_observation_direct(reader, value.public_observation) ||
        !reader.string(value.public_observation_digest) ||
        !read_public_request_direct(reader, value.request) ||
        !reader.string(value.public_candidate_domain_digest)) {
        return false;
    }
    try {
        validate_public_frame(value);
    } catch (...) {
        return false;
    }
    return true;
}

std::vector<std::uint8_t> canonical_public_frame_snapshot_bytes(
    const PublicFrameSnapshot& value) {
    validate_public_frame(value);
    ByteWriter writer;
    writer.string(kFrameSchema);
    writer.string(value.v2_contract_id);
    writer.string(value.episode_semantic_id);
    writer.string(value.public_semantic_decision_id);
    writer.u64be(value.decision_index);
    writer.u8(value.acting_player);
    writer.raw(environment::canonical_public_environment_observation_bytes(value.public_observation));
    writer.string(value.public_observation_digest);
    writer.raw(canonical_public_environment_decision_request_bytes(value.request));
    writer.string(value.public_candidate_domain_digest);
    return std::move(writer).take();
}

DecodeResult<PublicFrameSnapshot> decode_public_frame_snapshot(
    const std::vector<std::uint8_t>& bytes) noexcept {
    try {
        ByteReader reader(bytes);
        PublicFrameSnapshot value;
        if (!read_public_frame(reader, value) || !reader.at_end()) {
            return failure<PublicFrameSnapshot>("malformed public frame snapshot");
        }
        if (canonical_public_frame_snapshot_bytes(value) != bytes) {
            return failure<PublicFrameSnapshot>("noncanonical public frame snapshot");
        }
        return success(std::move(value));
    } catch (const std::exception& error) {
        return failure<PublicFrameSnapshot>(error.what());
    } catch (...) {
        return failure<PublicFrameSnapshot>("public frame decode threw");
    }
}

bool read_successor(ByteReader& reader, Successor& value) noexcept {
    std::uint8_t kind = 0;
    if (!reader.u8(kind) || !valid_successor_kind(kind)) {
        return false;
    }
    value.kind = static_cast<SuccessorKind>(kind);
    if (value.kind != SuccessorKind::NextFrame) {
        value.next_frame.reset();
        return true;
    }
    NextFrameTarget target;
    std::uint8_t target_kind = 0;
    if (!reader.u8(target_kind) || !valid_next_frame_target_kind(target_kind) ||
        !reader.u64be(target.next_decision_index) ||
        !reader.string(target.next_public_semantic_decision_id)) {
        return false;
    }
    target.kind = static_cast<NextFrameTargetKind>(target_kind);
    value.next_frame = std::move(target);
    try {
        validate_successor(value);
    } catch (...) {
        return false;
    }
    return true;
}

void write_successor(ByteWriter& writer, const Successor& value) {
    validate_successor(value);
    writer.u8(static_cast<std::uint8_t>(value.kind));
    if (value.kind == SuccessorKind::NextFrame) {
        writer.u8(static_cast<std::uint8_t>(value.next_frame->kind));
        writer.u64be(value.next_frame->next_decision_index);
        writer.string(value.next_frame->next_public_semantic_decision_id);
    }
}

void validate_public_record(const DecisionRecord& value) {
    validate_public_frame(value.frame);
    validate_successor(value.successor);
    std::size_t matches = 0;
    const environment::EnvironmentActionCandidate* selected = nullptr;
    for (const auto& candidate : value.frame.request.candidates) {
        if (candidate.public_action_key == value.selected_public_action_key) {
            ++matches;
            selected = &candidate;
        }
    }
    if (matches != 1 || selected == nullptr) {
        throw std::invalid_argument("trajectory record selection is not exactly one candidate");
    }
    if (value.transition_class == TransitionClass::AtomicEngineResponse) {
        if (value.frame.request.continuation.has_value() || !selected->submits_engine_response) {
            throw std::invalid_argument("atomic transition classification is inconsistent");
        }
    } else if (value.transition_class == TransitionClass::IntermediateContinuation) {
        if (!value.frame.request.continuation.has_value() || selected->submits_engine_response) {
            throw std::invalid_argument("intermediate transition classification is inconsistent");
        }
    } else if (value.transition_class == TransitionClass::FinalContinuationResponse) {
        if (!value.frame.request.continuation.has_value() || !selected->submits_engine_response) {
            throw std::invalid_argument("final continuation classification is inconsistent");
        }
    } else {
        throw std::invalid_argument("trajectory transition class is unknown");
    }
    if (value.successor.kind == SuccessorKind::NextFrame) {
        if (!value.successor.next_frame.has_value() ||
            value.frame.decision_index == std::numeric_limits<std::uint64_t>::max() ||
            value.successor.next_frame->next_decision_index != value.frame.decision_index + 1) {
            throw std::invalid_argument("record successor index is inconsistent");
        }
    }
}

void validate_decision_record(const DecisionRecord& value) {
    validate_public_record(value);
    require_identity(value.acting_policy_assignment_id,
                     "participant_policy_assignment.v1.", "record assignment");
    validate_rng_decision(value.policy_rng_decision_provenance);
    if (value.policy_rng_decision_provenance.decision_index != value.frame.decision_index ||
        value.policy_rng_decision_provenance.acting_policy_assignment_id !=
            value.acting_policy_assignment_id) {
        throw std::invalid_argument("record attribution does not match frame");
    }
}

std::vector<std::uint8_t> canonical_public_decision_record_bytes(const DecisionRecord& value) {
    validate_public_record(value);
    ByteWriter writer;
    writer.string(kTrustedTrajectoryContractId);
    writer.raw(canonical_public_frame_snapshot_bytes(value.frame));
    writer.string(value.selected_public_action_key);
    writer.u8(static_cast<std::uint8_t>(value.transition_class));
    write_successor(writer, value.successor);
    return std::move(writer).take();
}

std::vector<std::uint8_t> canonical_policy_decision_attribution_bytes(
    const DecisionRecord& value) {
    validate_decision_record(value);
    ByteWriter writer;
    writer.string(kPolicyProvenanceContractId);
    writer.string(value.acting_policy_assignment_id);
    writer.raw(canonical_policy_rng_decision_provenance_bytes(
        value.policy_rng_decision_provenance));
    return std::move(writer).take();
}

bool read_decision_record(ByteReader& reader, DecisionRecord& value) noexcept {
    std::string schema;
    std::uint8_t transition = 0;
    if (!reader.string(schema) || schema != kTrustedTrajectoryContractId ||
        !read_public_frame(reader, value.frame) || !reader.string(value.selected_public_action_key) ||
        !reader.u8(transition) || !valid_transition_class(transition) ||
        !read_successor(reader, value.successor)) {
        return false;
    }
    value.transition_class = static_cast<TransitionClass>(transition);
    try {
        validate_public_record(value);
    } catch (...) {
        return false;
    }
    return true;
}

DecodeResult<DecisionRecord> decode_public_decision_record(
    const std::vector<std::uint8_t>& bytes) noexcept {
    try {
        ByteReader reader(bytes);
        DecisionRecord value;
        if (!read_decision_record(reader, value) || !reader.at_end()) {
            return failure<DecisionRecord>("malformed decision record");
        }
        if (canonical_public_decision_record_bytes(value) != bytes) {
            return failure<DecisionRecord>("noncanonical decision record");
        }
        return success(std::move(value));
    } catch (const std::exception& error) {
        return failure<DecisionRecord>(error.what());
    } catch (...) {
        return failure<DecisionRecord>("decision record decode threw");
    }
}

void validate_terminal_closure(const TerminalClosure& value) {
    if (value.winner > 2 || value.win_reason == 255 ||
        value.terminal_view_player_0.perspective_player != 0 ||
        value.terminal_view_player_1.perspective_player != 1 ||
        environment::public_observation_digest(value.terminal_view_player_0) !=
            value.terminal_view_player_0_digest ||
        environment::public_observation_digest(value.terminal_view_player_1) !=
            value.terminal_view_player_1_digest) {
        throw std::invalid_argument("terminal closure is inconsistent");
    }
    require_digest(value.terminal_view_player_0_digest, "terminal view player 0");
    require_digest(value.terminal_view_player_1_digest, "terminal view player 1");
    if (value.semantic_action_count == 0) {
        if (value.last_decision_index.has_value()) {
            throw std::invalid_argument("zero-action terminal has a last decision index");
        }
    } else if (!value.last_decision_index.has_value() ||
               *value.last_decision_index == std::numeric_limits<std::uint64_t>::max() ||
               *value.last_decision_index + 1 != value.semantic_action_count) {
        throw std::invalid_argument("terminal last decision index is inconsistent");
    }
}

void validate_interrupted_closure(const InterruptedClosure& value) {
    if (value.pending_unacted_frame.has_value()) {
        if (value.pending_unacted_frame->decision_index != value.record_count) {
            throw std::invalid_argument("pending frame index is inconsistent");
        }
        validate_public_frame(*value.pending_unacted_frame);
    }
}

void validate_failed_closure(const FailedClosure& value) {
    if (!valid_failure_code(static_cast<std::uint8_t>(value.failure_code)) ||
        !valid_failure_stage(static_cast<std::uint8_t>(value.failure_stage))) {
        throw std::invalid_argument("failed closure has an unknown failure code");
    }
}

void validate_closure(const EpisodeClosure& value) {
    std::visit(
        [](const auto& closure) {
            using T = std::decay_t<decltype(closure)>;
            if constexpr (std::is_same_v<T, TerminalClosure>) {
                validate_terminal_closure(closure);
            } else if constexpr (std::is_same_v<T, InterruptedClosure>) {
                validate_interrupted_closure(closure);
            } else {
                validate_failed_closure(closure);
            }
        },
        value);
}

std::vector<std::uint8_t> canonical_episode_closure_bytes(const EpisodeClosure& value) {
    validate_closure(value);
    ByteWriter writer;
    writer.string(kClosureSchema);
    if (const auto* terminal = std::get_if<TerminalClosure>(&value)) {
        writer.u8(0);
        writer.u8(terminal->winner);
        writer.u8(terminal->win_reason);
        writer.u64be(terminal->semantic_action_count);
        write_optional_u64(writer, terminal->last_decision_index);
        writer.raw(environment::canonical_public_environment_observation_bytes(
            terminal->terminal_view_player_0));
        writer.string(terminal->terminal_view_player_0_digest);
        writer.raw(environment::canonical_public_environment_observation_bytes(
            terminal->terminal_view_player_1));
        writer.string(terminal->terminal_view_player_1_digest);
    } else if (const auto* interrupted = std::get_if<InterruptedClosure>(&value)) {
        writer.u8(1);
        writer.u64be(interrupted->record_count);
        writer.u8(interrupted->pending_unacted_frame.has_value() ? 1 : 0);
        if (interrupted->pending_unacted_frame.has_value()) {
            writer.raw(canonical_public_frame_snapshot_bytes(*interrupted->pending_unacted_frame));
        }
    } else {
        const auto& failed = std::get<FailedClosure>(value);
        writer.u8(2);
        writer.u8(static_cast<std::uint8_t>(failed.failure_code));
        writer.u8(static_cast<std::uint8_t>(failed.failure_stage));
        writer.boolean(failed.mutation_may_have_occurred);
        writer.u64be(failed.record_count);
    }
    return std::move(writer).take();
}

std::vector<std::uint8_t> canonical_public_episode_closure_bytes(const EpisodeClosure& value) {
    if (std::holds_alternative<FailedClosure>(value)) {
        throw std::invalid_argument("failed closure has no public gameplay identity");
    }
    return canonical_episode_closure_bytes(value);
}

DecodeResult<EpisodeClosure> decode_episode_closure(
    const std::vector<std::uint8_t>& bytes) noexcept {
    try {
        ByteReader reader(bytes);
        std::string schema;
        std::uint8_t kind = 0;
        if (!reader.string(schema) || schema != kClosureSchema || !reader.u8(kind) ||
            !valid_closure_kind(kind)) {
            return failure<EpisodeClosure>("malformed closure header");
        }
        EpisodeClosure value;
        if (kind == 0) {
            TerminalClosure terminal;
            if (!reader.u8(terminal.winner) || terminal.winner > 2 ||
                !reader.u8(terminal.win_reason) || !reader.u64be(terminal.semantic_action_count) ||
                !read_optional_u64(reader, terminal.last_decision_index) ||
                !read_public_observation_direct(reader, terminal.terminal_view_player_0) ||
                !reader.string(terminal.terminal_view_player_0_digest) ||
                !read_public_observation_direct(reader, terminal.terminal_view_player_1) ||
                !reader.string(terminal.terminal_view_player_1_digest)) {
                return failure<EpisodeClosure>("malformed terminal closure");
            }
            value = std::move(terminal);
        } else if (kind == 1) {
            InterruptedClosure interrupted;
            std::uint8_t present = 0;
            if (!reader.u64be(interrupted.record_count) || !reader.u8(present) || present > 1) {
                return failure<EpisodeClosure>("malformed interrupted closure");
            }
            if (present == 1) {
                PublicFrameSnapshot frame;
                if (!read_public_frame(reader, frame)) {
                    return failure<EpisodeClosure>("malformed pending frame");
                }
                interrupted.pending_unacted_frame = std::move(frame);
            }
            value = std::move(interrupted);
        } else {
            FailedClosure failed;
            std::uint8_t failure_code = 0;
            std::uint8_t failure_stage = 0;
            if (!reader.u8(failure_code) || !valid_failure_code(failure_code) ||
                !reader.u8(failure_stage) || !valid_failure_stage(failure_stage) ||
                !reader.boolean(failed.mutation_may_have_occurred) ||
                !reader.u64be(failed.record_count)) {
                return failure<EpisodeClosure>("malformed failed closure");
            }
            failed.failure_code = static_cast<environment::FailureCode>(failure_code);
            failed.failure_stage = static_cast<environment::FailureStage>(failure_stage);
            value = std::move(failed);
        }
        if (!reader.at_end()) {
            return failure<EpisodeClosure>("closure has trailing bytes");
        }
        validate_closure(value);
        if (canonical_episode_closure_bytes(value) != bytes) {
            return failure<EpisodeClosure>("noncanonical closure");
        }
        return success(std::move(value));
    } catch (const std::exception& error) {
        return failure<EpisodeClosure>(error.what());
    } catch (...) {
        return failure<EpisodeClosure>("closure decode threw");
    }
}

void validate_manifest(const EpisodeManifest& value) {
    require_contract(value.trusted_trajectory_contract_id, kTrustedTrajectoryContractId,
                     "trusted trajectory contract");
    require_contract(value.v2_contract_id, environment::kEpisodicEnvironmentV2ContractId,
                     "manifest V2 contract");
    require_digest(value.environment_semantic_id, "environment semantic ID");
    require_digest(value.episode_semantic_id, "episode semantic ID");
    if (trace::sha256_bytes(value.environment_identity_input) != value.environment_semantic_id ||
        trace::sha256_bytes(value.episode_identity_input) != value.episode_semantic_id) {
        throw std::invalid_argument("manifest identity input digest mismatch");
    }
    require_contract(value.episode_identity_schema_id, environment::kEpisodeIdentitySchemaId,
                     "episode identity schema");
    if (value.environment_identity_input.empty() || value.episode_identity_input.empty()) {
        throw std::invalid_argument("manifest identity inputs are empty");
    }
    const auto decoded_environment = decode_environment_identity_input(value.environment_identity_input);
    if (!decoded_environment || decoded_environment.value->environment_semantic_id !=
                                   value.environment_semantic_id ||
        !is_current_certified_environment(*decoded_environment.value)) {
        throw std::invalid_argument("manifest environment identity cannot be resolved");
    }
    const auto decoded_episode = decode_episode_identity_input(
        value.episode_identity_input, *decoded_environment.value);
    if (!decoded_episode || trace::sha256_bytes(value.episode_identity_input) !=
                                  value.episode_semantic_id) {
        throw std::invalid_argument("manifest episode identity cannot be resolved");
    }
    (void)canonical_policy_provenance_envelope_bytes(value.policy_provenance);
    (void)canonical_collection_disposition_bytes(value.collection_disposition);
}

std::vector<std::uint8_t> canonical_episode_manifest_bytes(const EpisodeManifest& value) {
    validate_manifest(value);
    ByteWriter writer;
    writer.string(kManifestSchema);
    writer.string(value.v2_contract_id);
    writer.string(value.environment_semantic_id);
    writer.bytes(value.environment_identity_input);
    writer.string(value.episode_identity_schema_id);
    writer.string(value.episode_semantic_id);
    writer.bytes(value.episode_identity_input);
    writer.raw(canonical_policy_provenance_envelope_bytes(value.policy_provenance));
    writer.raw(canonical_collection_disposition_bytes(value.collection_disposition));
    return std::move(writer).take();
}

bool read_manifest(ByteReader& reader, EpisodeManifest& value) noexcept {
    std::string schema;
    if (!reader.string(schema) || schema != kManifestSchema ||
        !reader.string(value.v2_contract_id) ||
        value.v2_contract_id != environment::kEpisodicEnvironmentV2ContractId ||
        !reader.string(value.environment_semantic_id) ||
        !reader.bytes(value.environment_identity_input) ||
        !reader.string(value.episode_identity_schema_id) ||
        !reader.string(value.episode_semantic_id) ||
        !reader.bytes(value.episode_identity_input)) {
        return false;
    }
    if (!read_policy_provenance_direct(reader, value.policy_provenance)) {
        return false;
    }
    if (!read_disposition_direct(reader, value.collection_disposition)) {
        return false;
    }
    try {
        validate_manifest(value);
    } catch (...) {
        return false;
    }
    return true;
}

DecodeResult<EpisodeManifest> decode_episode_manifest(
    const std::vector<std::uint8_t>& bytes) noexcept {
    try {
        ByteReader reader(bytes);
        EpisodeManifest value;
        if (!read_manifest(reader, value) || !reader.at_end()) {
            return failure<EpisodeManifest>("malformed episode manifest");
        }
        if (canonical_episode_manifest_bytes(value) != bytes) {
            return failure<EpisodeManifest>("noncanonical episode manifest");
        }
        return success(std::move(value));
    } catch (const std::exception& error) {
        return failure<EpisodeManifest>(error.what());
    } catch (...) {
        return failure<EpisodeManifest>("episode manifest decode threw");
    }
}

std::vector<std::uint8_t> canonical_collection_decision_record_bytes(
    const DecisionRecord& value) {
    validate_decision_record(value);
    ByteWriter writer;
    writer.string(kTrustedTrajectoryContractId);
    writer.raw(canonical_public_decision_record_bytes(value));
    writer.raw(canonical_policy_decision_attribution_bytes(value));
    return std::move(writer).take();
}

bool read_policy_rng_decision_direct(ByteReader& reader,
                                     PolicyRngDecisionProvenance& value) noexcept {
    std::string domain;
    std::string schema;
    std::uint8_t mode = 0;
    if (!reader.string(domain) || domain != kPolicyRngDecisionProvenanceDomain ||
        !reader.string(schema) || schema != kPolicyRngDecisionProvenanceDomain ||
        !reader.u64be(value.decision_index) || !reader.string(value.acting_policy_assignment_id) ||
        !reader.string(value.policy_rng_identity) ||
        !reader.string(value.policy_rng_contract_identity) ||
        !reader.string(value.policy_rng_stream_id) ||
        !reader.string(value.policy_rng_initialization_identity) || !reader.u8(mode) ||
        !valid_rng_mode(mode)) {
        return false;
    }
    value.mode = static_cast<PolicyRngMode>(mode);
    value.pre_cursor.reset();
    value.post_cursor.reset();
    value.pre_state.reset();
    value.post_state.reset();
    if (value.mode == PolicyRngMode::Cursor) {
        std::uint64_t pre = 0;
        std::uint64_t post = 0;
        if (!reader.u64be(pre) || !reader.u64be(post)) {
            return false;
        }
        value.pre_cursor = pre;
        value.post_cursor = post;
    } else if (value.mode == PolicyRngMode::State) {
        std::vector<std::uint8_t> pre;
        std::vector<std::uint8_t> post;
        if (!reader.bytes(pre) || !reader.bytes(post)) {
            return false;
        }
        value.pre_state = std::move(pre);
        value.post_state = std::move(post);
    }
    try {
        validate_rng_decision(value);
    } catch (...) {
        return false;
    }
    return true;
}

bool read_collection_record(ByteReader& reader, DecisionRecord& value) noexcept {
    std::string schema;
    if (!reader.string(schema) || schema != kTrustedTrajectoryContractId ||
        !read_decision_record(reader, value)) {
        return false;
    }
    std::string attribution_schema;
    if (!reader.string(attribution_schema) || attribution_schema != kPolicyProvenanceContractId ||
        !reader.string(value.acting_policy_assignment_id)) {
        return false;
    }
    if (!read_policy_rng_decision_direct(reader, value.policy_rng_decision_provenance)) {
        return false;
    }
    try {
        validate_decision_record(value);
    } catch (...) {
        return false;
    }
    return true;
}

bool read_closure_from_reader(ByteReader& reader, EpisodeClosure& value) noexcept {
    std::string schema;
    std::uint8_t kind = 0;
    if (!reader.string(schema) || schema != kClosureSchema || !reader.u8(kind) ||
        !valid_closure_kind(kind)) {
        return false;
    }
    if (kind == 0) {
        TerminalClosure terminal;
        if (!reader.u8(terminal.winner) || terminal.winner > 2 ||
            !reader.u8(terminal.win_reason) || !reader.u64be(terminal.semantic_action_count) ||
            !read_optional_u64(reader, terminal.last_decision_index) ||
            !read_public_observation_direct(reader, terminal.terminal_view_player_0) ||
            !reader.string(terminal.terminal_view_player_0_digest) ||
            !read_public_observation_direct(reader, terminal.terminal_view_player_1) ||
            !reader.string(terminal.terminal_view_player_1_digest)) {
            return false;
        }
        value = std::move(terminal);
    } else if (kind == 1) {
        InterruptedClosure interrupted;
        std::uint8_t present = 0;
        if (!reader.u64be(interrupted.record_count) || !reader.u8(present) || present > 1) {
            return false;
        }
        if (present == 1) {
            PublicFrameSnapshot frame;
            if (!read_public_frame(reader, frame)) {
                return false;
            }
            interrupted.pending_unacted_frame = std::move(frame);
        }
        value = std::move(interrupted);
    } else {
        FailedClosure failed;
        std::uint8_t failure_code = 0;
        std::uint8_t failure_stage = 0;
        if (!reader.u8(failure_code) || !valid_failure_code(failure_code) ||
            !reader.u8(failure_stage) || !valid_failure_stage(failure_stage) ||
            !reader.boolean(failed.mutation_may_have_occurred) ||
            !reader.u64be(failed.record_count)) {
            return false;
        }
        failed.failure_code = static_cast<environment::FailureCode>(failure_code);
        failed.failure_stage = static_cast<environment::FailureStage>(failure_stage);
        value = std::move(failed);
    }
    try {
        validate_closure(value);
    } catch (...) {
        return false;
    }
    return true;
}

bool read_closure_direct(ByteReader& reader, EpisodeClosure& value) noexcept {
    return read_closure_from_reader(reader, value);
}

void validate_envelope_sequence(const EpisodeEnvelope& value) {
    validate_manifest(value.manifest);
    if (value.records.size() > std::numeric_limits<std::uint32_t>::max()) {
        throw std::length_error("trajectory record count exceeds u32");
    }
    for (std::size_t index = 0; index < value.records.size(); ++index) {
        const auto& record = value.records[index];
        if (record.frame.episode_semantic_id != value.manifest.episode_semantic_id) {
            throw std::invalid_argument(
                "trajectory record frame episode identity differs from its manifest");
        }
        validate_decision_record(record);
        if (record.frame.decision_index != index) {
            throw std::invalid_argument("trajectory record decision indices are not contiguous");
        }
        if (index > 0) {
            const auto& previous = value.records[index - 1];
            if (previous.successor.kind != SuccessorKind::NextFrame ||
                !previous.successor.next_frame.has_value() ||
                previous.successor.next_frame->kind != NextFrameTargetKind::NextDecisionRecord ||
                previous.successor.next_frame->next_decision_index != index ||
                previous.successor.next_frame->next_public_semantic_decision_id !=
                    record.frame.public_semantic_decision_id) {
                throw std::invalid_argument("trajectory record successor sequence is inconsistent");
            }
        }
    }
    const auto count = static_cast<std::uint64_t>(value.records.size());
    if (const auto* terminal = std::get_if<TerminalClosure>(&value.closure)) {
        if (terminal->semantic_action_count != count ||
            (count == 0 ? terminal->last_decision_index.has_value()
                        : !terminal->last_decision_index.has_value() ||
                              *terminal->last_decision_index ==
                                  std::numeric_limits<std::uint64_t>::max() ||
                              *terminal->last_decision_index + 1 != count)) {
            throw std::invalid_argument("terminal closure count is inconsistent");
        }
        if (!value.records.empty() && value.records.back().successor.kind != SuccessorKind::Terminal) {
            throw std::invalid_argument("terminal envelope has no terminal successor");
        }
    } else if (const auto* interrupted = std::get_if<InterruptedClosure>(&value.closure)) {
        if (interrupted->record_count != count) {
            throw std::invalid_argument("interrupted closure count is inconsistent");
        }
        if (interrupted->pending_unacted_frame.has_value()) {
            if (interrupted->pending_unacted_frame->episode_semantic_id !=
                    value.manifest.episode_semantic_id ||
                interrupted->pending_unacted_frame->decision_index != count ||
                (!value.records.empty() &&
                (value.records.back().successor.kind != SuccessorKind::NextFrame ||
                !value.records.back().successor.next_frame.has_value() ||
                value.records.back().successor.next_frame->kind !=
                    NextFrameTargetKind::InterruptionPendingUnactedFrame ||
                value.records.back().successor.next_frame->next_public_semantic_decision_id !=
                    interrupted->pending_unacted_frame->public_semantic_decision_id))) {
                throw std::invalid_argument("pending interruption successor is inconsistent");
            }
        } else if (!value.records.empty() &&
                   value.records.back().successor.kind != SuccessorKind::Interrupted) {
            throw std::invalid_argument("interrupted envelope has no interruption successor");
        }
    } else {
        const auto& failed = std::get<FailedClosure>(value.closure);
        if (failed.record_count != count) {
            throw std::invalid_argument("failed closure count is inconsistent");
        }
        if (!value.records.empty() && value.records.back().successor.kind != SuccessorKind::Failed) {
            throw std::invalid_argument("failed envelope has no failed successor");
        }
    }
}

std::vector<std::uint8_t> canonical_episode_envelope_bytes(const EpisodeEnvelope& value) {
    validate_envelope_sequence(value);
    ByteWriter writer;
    writer.string(kEnvelopeSchema);
    writer.raw(canonical_episode_manifest_bytes(value.manifest));
    require_length(value.records.size());
    writer.u32be(static_cast<std::uint32_t>(value.records.size()));
    for (const auto& record : value.records) {
        writer.raw(canonical_collection_decision_record_bytes(record));
    }
    writer.raw(canonical_episode_closure_bytes(value.closure));
    return std::move(writer).take();
}

DecodeResult<EpisodeEnvelope> decode_episode_envelope(
    const std::vector<std::uint8_t>& bytes) noexcept {
    try {
        ByteReader reader(bytes);
        EpisodeEnvelope value;
        std::string schema;
        std::uint32_t count = 0;
        if (!reader.string(schema) || schema != kEnvelopeSchema ||
            !read_manifest(reader, value.manifest) ||
            !reader.u32be(count)) {
            return failure<EpisodeEnvelope>("malformed episode envelope header");
        }
        if (count > reader.remaining()) {
            return failure<EpisodeEnvelope>(
                "episode record count exceeds the remaining input");
        }
        value.records.reserve(count);
        for (std::uint32_t index = 0; index < count; ++index) {
            DecisionRecord record;
            if (!read_collection_record(reader, record)) {
                return failure<EpisodeEnvelope>("malformed collection record");
            }
            value.records.push_back(std::move(record));
        }
        if (!read_closure_direct(reader, value.closure) || !reader.at_end()) {
            return failure<EpisodeEnvelope>("malformed episode closure or trailing bytes");
        }
        validate_envelope_sequence(value);
        if (canonical_episode_envelope_bytes(value) != bytes) {
            return failure<EpisodeEnvelope>("noncanonical episode envelope");
        }
        return success(std::move(value));
    } catch (const std::exception& error) {
        return failure<EpisodeEnvelope>(error.what());
    } catch (...) {
        return failure<EpisodeEnvelope>("episode envelope decode threw");
    }
}

void validate_restricted_evidence(const RestrictedReplayEvidence& value) {
    require_contract(value.v2_contract_id, environment::kEpisodicEnvironmentV2ContractId,
                     "restricted evidence V2 contract");
    require_digest(value.episode_semantic_id, "restricted evidence episode");
    if (!valid_interruption_reason(static_cast<std::uint8_t>(value.interruption_reason)) ||
        value.engine_process_budget == 0 || value.semantic_action_budget == 0 ||
        value.observed_engine_process_count > value.engine_process_budget ||
        value.observed_semantic_action_count > value.semantic_action_budget) {
        throw std::invalid_argument("restricted replay evidence is inconsistent");
    }
}

std::vector<std::uint8_t> canonical_restricted_replay_evidence_bytes(
    const RestrictedReplayEvidence& value) {
    validate_restricted_evidence(value);
    ByteWriter writer;
    writer.string(kRestrictedReplayEvidenceSchemaId);
    writer.string(value.v2_contract_id);
    writer.string(value.episode_semantic_id);
    writer.u8(1);
    writer.u8(static_cast<std::uint8_t>(value.interruption_reason));
    writer.u64be(value.engine_process_budget);
    writer.u64be(value.semantic_action_budget);
    writer.u64be(value.observed_engine_process_count);
    writer.u64be(value.observed_semantic_action_count);
    writer.u64be(value.final_engine_step_index);
    return std::move(writer).take();
}

DecodeResult<RestrictedReplayEvidence> decode_restricted_replay_evidence(
    const std::vector<std::uint8_t>& bytes) noexcept {
    try {
        ByteReader reader(bytes);
        RestrictedReplayEvidence value;
        std::string schema;
        std::uint8_t closure_kind = 0;
        std::uint8_t reason = 0;
        if (!reader.string(schema) || schema != kRestrictedReplayEvidenceSchemaId ||
            !reader.string(value.v2_contract_id) ||
            value.v2_contract_id != environment::kEpisodicEnvironmentV2ContractId ||
            !reader.string(value.episode_semantic_id) || !reader.u8(closure_kind) ||
            closure_kind != 1 || !reader.u8(reason) || !valid_interruption_reason(reason) ||
            !reader.u64be(value.engine_process_budget) ||
            !reader.u64be(value.semantic_action_budget) ||
            !reader.u64be(value.observed_engine_process_count) ||
            !reader.u64be(value.observed_semantic_action_count) ||
            !reader.u64be(value.final_engine_step_index) || !reader.at_end()) {
            return failure<RestrictedReplayEvidence>("malformed restricted evidence");
        }
        value.interruption_reason = static_cast<environment::InterruptionReason>(reason);
        validate_restricted_evidence(value);
        if (canonical_restricted_replay_evidence_bytes(value) != bytes) {
            return failure<RestrictedReplayEvidence>("noncanonical restricted evidence");
        }
        return success(std::move(value));
    } catch (const std::exception& error) {
        return failure<RestrictedReplayEvidence>(error.what());
    } catch (...) {
        return failure<RestrictedReplayEvidence>("restricted evidence decode threw");
    }
}

std::string public_gameplay_trajectory_id(const EpisodeEnvelope& value) {
    validate_envelope_sequence(value);
    if (std::holds_alternative<FailedClosure>(value.closure)) {
        throw std::invalid_argument("failed envelope has no public gameplay identity");
    }
    ByteWriter writer;
    writer.string(kPublicGameplayIdentityDomain);
    writer.string(kPublicGameplayIdentityDomain);
    writer.string(kTrustedTrajectoryContractId);
    writer.string(value.manifest.v2_contract_id);
    writer.string(value.manifest.environment_semantic_id);
    writer.string(value.manifest.episode_identity_schema_id);
    writer.string(value.manifest.episode_semantic_id);
    require_length(value.records.size());
    writer.u32be(static_cast<std::uint32_t>(value.records.size()));
    for (const auto& record : value.records) {
        writer.raw(canonical_public_decision_record_bytes(record));
    }
    writer.raw(canonical_public_episode_closure_bytes(value.closure));
    return "public_gameplay_trajectory.v1." + trace::sha256_bytes(writer.data());
}

std::string trajectory_record_id(const EpisodeEnvelope& value) {
    validate_envelope_sequence(value);
    if (!std::holds_alternative<TerminalClosure>(value.closure) &&
        !std::holds_alternative<InterruptedClosure>(value.closure)) {
        throw std::invalid_argument("failed envelope has no trajectory record identity");
    }
    if (value.manifest.collection_disposition.kind != CollectionDispositionKind::Clean) {
        throw std::invalid_argument("quarantined envelope has no trajectory record identity");
    }
    ByteWriter writer;
    writer.string(kTrajectoryRecordIdentityDomain);
    writer.string(kTrajectoryRecordIdentityDomain);
    writer.string(kTrustedTrajectoryContractId);
    writer.string(public_gameplay_trajectory_id(value));
    writer.raw(canonical_policy_provenance_envelope_bytes(value.manifest.policy_provenance));
    require_length(value.records.size());
    writer.u32be(static_cast<std::uint32_t>(value.records.size()));
    for (const auto& record : value.records) {
        writer.raw(canonical_policy_decision_attribution_bytes(record));
    }
    writer.raw(canonical_collection_disposition_bytes(value.manifest.collection_disposition));
    return "trajectory_record.v1." + trace::sha256_bytes(writer.data());
}

}  // namespace ygo::trajectory
