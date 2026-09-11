#include "ygo/trajectory/codec_v3.hpp"

#include <algorithm>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include "ygo/environment/public_action_identity.hpp"
#include "ygo/trajectory/identity_resolver.hpp"
#include "ygo/trace/sha256.hpp"

namespace ygo::trajectory {

// These shared readers/writers retain their historical field semantics; only
// the V3 trajectory-owned markers and public-key validation are successor data.
void write_optional_string(ByteWriter&, const std::optional<std::string>&);
bool read_optional_string(ByteReader&, std::optional<std::string>&) noexcept;
void write_optional_u64(ByteWriter&, const std::optional<std::uint64_t>&);
bool read_optional_u64(ByteReader&, std::optional<std::uint64_t>&) noexcept;
void write_optional_public_choice(
    ByteWriter&, const std::optional<environment::PublicChoice>&);
bool read_optional_public_choice(
    ByteReader&, std::optional<environment::PublicChoice>&) noexcept;
void write_optional_reference(
    ByteWriter&, const std::optional<environment::PublicCardReference>&);
bool read_optional_reference(
    ByteReader&, std::optional<environment::PublicCardReference>&) noexcept;
void write_optional_u32(ByteWriter&, const std::optional<std::uint32_t>&);
bool read_optional_u32(ByteReader&, std::optional<std::uint32_t>&) noexcept;
void write_optional_u8(ByteWriter&, const std::optional<std::uint8_t>&);
bool read_optional_u8(ByteReader&, std::optional<std::uint8_t>&) noexcept;
void write_optional_i32(ByteWriter&, const std::optional<std::int32_t>&);
bool read_optional_i32(ByteReader&, std::optional<std::int32_t>&) noexcept;
void write_u32_vector(ByteWriter&, const std::vector<std::uint32_t>&);
bool read_u32_vector(ByteReader&, std::vector<std::uint32_t>&) noexcept;
void write_u16_vector(ByteWriter&, const std::vector<std::uint16_t>&);
bool read_u16_vector(ByteReader&, std::vector<std::uint16_t>&) noexcept;
bool read_policy_provenance_direct(ByteReader&, PolicyProvenanceEnvelope&) noexcept;
bool read_disposition_direct(ByteReader&, CollectionDisposition&) noexcept;
bool read_public_observation_direct(
    ByteReader&, environment::PublicEnvironmentObservationInput&) noexcept;
bool read_successor(ByteReader&, Successor&) noexcept;
void write_successor(ByteWriter&, const Successor&);
bool read_policy_rng_decision_direct(ByteReader&, PolicyRngDecisionProvenance&) noexcept;
std::optional<environment::EnvironmentActionKind> action_kind_from_token(
    std::string_view) noexcept;
std::optional<environment::EnvironmentDecisionKind> decision_kind_from_token(
    std::string_view) noexcept;

namespace {

template <typename T>
DecodeResult<T> failure(std::string message) {
    DecodeResult<T> result;
    result.error = DecodeError{std::move(message)};
    return result;
}

template <typename T>
DecodeResult<T> success(T value) {
    DecodeResult<T> result;
    result.value = std::move(value);
    return result;
}

void require_length(const std::size_t size) {
    if (size > std::numeric_limits<std::uint32_t>::max()) {
        throw std::length_error("trajectory codec field exceeds u32 length");
    }
}

void require_digest(const std::string& value, const char* field) {
    if (!is_lower_hex_digest(value)) {
        throw std::invalid_argument(std::string("trajectory ") + field +
                                    " is not a SHA-256 digest");
    }
}

void require_identity(const std::string& value, const std::string_view prefix,
                      const char* field) {
    if (!is_canonical_identity(value, prefix)) {
        throw std::invalid_argument(std::string("trajectory ") + field +
                                    " has invalid identity");
    }
}

void require_contract(const std::string_view value, const std::string_view expected,
                      const char* field) {
    if (value != expected) {
        throw std::invalid_argument(std::string("trajectory ") + field +
                                    " has an unknown contract");
    }
}

bool valid_environment_decision_kind(const std::uint8_t value) noexcept {
    return value <= static_cast<std::uint8_t>(
                         environment::EnvironmentDecisionKind::Unsupported);
}

bool valid_environment_action_kind(const std::uint8_t value) noexcept {
    return value <= static_cast<std::uint8_t>(
                         environment::EnvironmentActionKind::Unsupported);
}

bool valid_transition_class(const std::uint8_t value) noexcept { return value <= 2; }
bool valid_next_frame_target_kind(const std::uint8_t value) noexcept { return value <= 1; }
bool valid_successor_kind(const std::uint8_t value) noexcept { return value <= 3; }
bool valid_failure_code(const std::uint8_t value) noexcept {
    return value <= static_cast<std::uint8_t>(
                         environment::FailureCode::ResourceIdentityMismatch);
}
bool valid_failure_stage(const std::uint8_t value) noexcept {
    return value <= static_cast<std::uint8_t>(environment::FailureStage::Teardown);
}
bool valid_interruption_reason(const std::uint8_t value) noexcept {
    return value <= static_cast<std::uint8_t>(
                         environment::InterruptionReason::AdministrativeCancel);
}
bool valid_closure_kind(const std::uint8_t value) noexcept { return value <= 2; }

void validate_rng_decision(const PolicyRngDecisionProvenance& value) {
    (void)canonical_policy_rng_decision_provenance_bytes(value);
}

void validate_successor(const Successor& value) {
    if (!valid_successor_kind(static_cast<std::uint8_t>(value.kind))) {
        throw std::invalid_argument("successor kind is unknown");
    }
    if (value.kind == SuccessorKind::NextFrame) {
        if (!value.next_frame.has_value()) {
            throw std::invalid_argument("next-frame successor has no target");
        }
        if (!valid_next_frame_target_kind(
                static_cast<std::uint8_t>(value.next_frame->kind))) {
            throw std::invalid_argument("next-frame target kind is unknown");
        }
        require_digest(value.next_frame->next_public_semantic_decision_id,
                       "successor decision");
    } else if (value.next_frame.has_value()) {
        throw std::invalid_argument("closure successor has an unexpected target");
    }
}

bool strictly_increasing(const std::vector<std::uint32_t>& values) noexcept {
    for (std::size_t index = 1; index < values.size(); ++index) {
        if (values[index - 1] >= values[index]) {
            return false;
        }
    }
    return true;
}

std::uint32_t bit_count(const std::uint64_t value) noexcept {
    std::uint32_t count = 0;
    std::uint64_t remaining = value;
    while (remaining != 0) {
        remaining &= remaining - 1;
        ++count;
    }
    return count;
}

bool uses_public_cardinality(std::string_view continuation_kind) noexcept {
    return continuation_kind == "unordered" || continuation_kind == "tribute" ||
           continuation_kind == "sum" || continuation_kind == "zone" ||
           continuation_kind == "announce_mask";
}

bool uses_monotonic_selection(std::string_view continuation_kind) noexcept {
    return continuation_kind == "unordered" || continuation_kind == "tribute" ||
           continuation_kind == "sum" || continuation_kind == "zone" ||
           continuation_kind == "announce_mask";
}

std::optional<bool> public_can_finish(
    const environment::EnvironmentContinuationView& value) {
    if (value.continuation_kind == "unordered" ||
        value.continuation_kind == "zone") {
        const auto count = value.selected_indices.size();
        return count >= value.min_count && count <= value.max_count;
    }
    if (value.continuation_kind == "ordering") {
        return value.remaining_indices.empty();
    }
    if (value.continuation_kind == "counter") {
        std::uint64_t assigned_total = 0;
        for (const auto amount : value.assigned_amounts) {
            assigned_total += amount;
        }
        return value.remaining_indices.empty() &&
               assigned_total == value.required_amount;
    }
    if (value.continuation_kind == "announce_mask") {
        return bit_count(value.selected_mask) == value.min_count;
    }
    return std::nullopt;
}

void validate_continuation(
    const environment::EnvironmentContinuationView& value) {
    static constexpr std::string_view valid[] = {
        "unordered", "tribute", "sum", "zone", "counter", "ordering", "announce_mask"};
    if (std::find(std::begin(valid), std::end(valid),
                  value.continuation_kind) == std::end(valid)) {
        throw std::invalid_argument("trajectory continuation kind is unknown");
    }
    const bool unbounded_greater_sum =
        value.continuation_kind == "sum" && value.greater_sum &&
        value.max_count == 0;
    if (value.exact_sum == value.greater_sum ||
        (value.greater_sum && value.continuation_kind != "sum")) {
        throw std::invalid_argument("trajectory continuation has an invalid sum mode");
    }
    if (value.min_count > value.max_count && !unbounded_greater_sum) {
        throw std::invalid_argument("trajectory continuation cardinality is inverted");
    }
    if (value.continuation_steps != value.continuation_step) {
        throw std::invalid_argument("trajectory continuation step metrics disagree");
    }
    if (!strictly_increasing(value.remaining_indices) ||
        (uses_monotonic_selection(value.continuation_kind) &&
         !strictly_increasing(value.selected_indices))) {
        throw std::invalid_argument("trajectory continuation indices are not strictly ordered");
    }
    if (value.continuation_kind == "counter" && !value.selected_indices.empty()) {
        throw std::invalid_argument("counter continuation has selected indices");
    }
    const auto selected_count =
        value.continuation_kind == "announce_mask"
            ? static_cast<std::size_t>(bit_count(value.selected_mask))
            : value.selected_indices.size();
    if (uses_public_cardinality(value.continuation_kind) &&
        !unbounded_greater_sum && selected_count > value.max_count) {
        throw std::invalid_argument("trajectory continuation exceeds public max_count");
    }
    for (std::size_t left = 0; left < value.selected_indices.size(); ++left) {
        for (std::size_t right = 0; right < left; ++right) {
            if (value.selected_indices[left] == value.selected_indices[right]) {
                throw std::invalid_argument("trajectory continuation has duplicate selected indices");
            }
        }
        for (const auto remaining : value.remaining_indices) {
            if (value.selected_indices[left] == remaining) {
                throw std::invalid_argument(
                    "trajectory continuation selected and remaining indices overlap");
            }
        }
    }
    for (std::size_t left = 0; left < value.remaining_indices.size(); ++left) {
        for (std::size_t right = 0; right < left; ++right) {
            if (value.remaining_indices[left] == value.remaining_indices[right]) {
                throw std::invalid_argument("trajectory continuation has duplicate remaining indices");
            }
        }
    }
    if (value.continuation_kind != "counter" &&
        !value.assigned_amounts.empty()) {
        throw std::invalid_argument(
            "trajectory continuation has assigned amounts outside counter allocation");
    }
    if (value.continuation_kind == "announce_mask" &&
        (value.selected_mask & ~value.available_mask) != 0) {
        throw std::invalid_argument(
            "trajectory announcement continuation selects a bit outside its public mask");
    }
    if (value.continuation_kind == "announce_mask") {
        std::uint64_t selected_index_mask = 0;
        for (const auto index : value.selected_indices) {
            if (index >= 64) {
                throw std::invalid_argument(
                    "trajectory announcement index exceeds the public mask width");
            }
            selected_index_mask |= std::uint64_t{1} << index;
        }
        if (selected_index_mask != value.selected_mask) {
            throw std::invalid_argument(
                "trajectory announcement selected indices disagree with the selected mask");
        }
        std::uint64_t remaining_index_mask = 0;
        for (const auto index : value.remaining_indices) {
            if (index >= 64) {
                throw std::invalid_argument(
                    "trajectory announcement index exceeds the public mask width");
            }
            remaining_index_mask |= std::uint64_t{1} << index;
        }
        if (remaining_index_mask !=
            (value.available_mask & ~value.selected_mask)) {
            throw std::invalid_argument(
                "trajectory announcement remaining indices disagree with the public mask domain");
        }
    }
    const auto can_finish = public_can_finish(value);
    if (can_finish.has_value() && value.can_finish != *can_finish) {
        throw std::invalid_argument(
            "trajectory continuation can_finish disagrees with public state");
    }
}

}  // namespace
namespace {

constexpr std::string_view kCandidateSchemaV3 = kTrustedTrajectoryV3ContractId;
constexpr std::string_view kFrameSchemaV3 = kTrustedTrajectoryV3ContractId;
constexpr std::string_view kClosureSchemaV3 = kTrustedTrajectoryV3ContractId;
constexpr std::string_view kManifestSchemaV3 = kTrustedTrajectoryV3ContractId;
constexpr std::string_view kEnvelopeSchemaV3 = kTrustedTrajectoryV3ContractId;

bool valid_v3_card_selection_operation(const std::uint8_t value) noexcept {
    return value <= static_cast<std::uint8_t>(
                        environment::PublicCardSelectionOperation::Unselect);
}

void validate_v3_candidate(const environment::EnvironmentActionCandidate& value) {
    if (!valid_environment_action_kind(static_cast<std::uint8_t>(value.action_kind)) ||
        value.action_kind == environment::EnvironmentActionKind::Unsupported ||
        value.public_action_key.empty() ||
        !valid_v3_card_selection_operation(
            static_cast<std::uint8_t>(value.card_selection_operation))) {
        throw std::invalid_argument("V3 trajectory candidate is unsupported or malformed");
    }
    if (!value.continuation_operation.empty() && value.continuation_operation != "pick" &&
        value.continuation_operation != "amount" && value.continuation_operation != "finish" &&
        value.continuation_operation != "cancel" && value.continuation_operation != "bypass") {
        throw std::invalid_argument("V3 trajectory candidate continuation operation is unknown");
    }
    if (value.action_kind != environment::EnvironmentActionKind::CardSelection &&
        value.card_selection_operation != environment::PublicCardSelectionOperation::None) {
        throw std::invalid_argument("non-card V3 candidate carries selection operation");
    }
    if ((value.action_kind == environment::EnvironmentActionKind::Finish ||
         value.action_kind == environment::EnvironmentActionKind::Cancel) &&
        value.card_selection_operation != environment::PublicCardSelectionOperation::None) {
        throw std::invalid_argument("finish or cancel V3 candidate carries selection operation");
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
    input.card_selection_operation = value.card_selection_operation;
    if (environment::public_action_key_v3(input) != value.public_action_key ||
        !environment::is_public_action_key_v3(value.public_action_key)) {
        throw std::invalid_argument("V3 trajectory public action key is not canonical");
    }
}

void validate_v3_successor(const Successor& value) {
    validate_successor(value);
}

void write_public_candidate_v3(
    ByteWriter& writer, const environment::EnvironmentActionCandidate& value) {
    validate_v3_candidate(value);
    writer.string(kCandidateSchemaV3);
    writer.string(environment::environment_action_kind_name(value.action_kind));
    writer.u8(static_cast<std::uint8_t>(value.card_selection_operation));
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
}

bool read_public_candidate_v3(
    ByteReader& reader, environment::EnvironmentActionCandidate& value) noexcept {
    std::string schema;
    std::string action_token;
    std::uint8_t operation = 0;
    if (!reader.string(schema) || schema != kCandidateSchemaV3 ||
        !reader.string(action_token) || !reader.u8(operation) ||
        !valid_v3_card_selection_operation(operation)) {
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
        !reader.string(value.continuation_operation) ||
        !reader.boolean(value.submits_engine_response)) {
        return false;
    }
    value.action_kind = *kind;
    value.card_selection_operation =
        static_cast<environment::PublicCardSelectionOperation>(operation);
    try {
        validate_v3_candidate(value);
    } catch (...) {
        return false;
    }
    return true;
}

void write_continuation_v3(
    ByteWriter& writer, const environment::EnvironmentContinuationView& value) {
    validate_continuation(value);
    writer.string(kCandidateSchemaV3);
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
}

bool read_continuation_v3(
    ByteReader& reader, environment::EnvironmentContinuationView& value) noexcept {
    std::string schema;
    if (!reader.string(schema) || schema != kCandidateSchemaV3 ||
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

void validate_v3_request(const environment::EnvironmentDecisionRequest& value) {
    if (value.player > 1 ||
        !valid_environment_decision_kind(static_cast<std::uint8_t>(value.kind)) ||
        value.kind == environment::EnvironmentDecisionKind::Unsupported ||
        value.candidates.empty()) {
        throw std::invalid_argument("V3 trajectory request is invalid or incomplete");
    }
    std::vector<std::string> keys;
    keys.reserve(value.candidates.size());
    for (const auto& candidate : value.candidates) {
        validate_v3_candidate(candidate);
        if (value.kind == environment::EnvironmentDecisionKind::UnselectCard &&
            candidate.action_kind == environment::EnvironmentActionKind::CardSelection &&
            candidate.card_selection_operation ==
                environment::PublicCardSelectionOperation::None) {
            throw std::invalid_argument("UnselectCard candidate lacks operation");
        }
        if (value.kind != environment::EnvironmentDecisionKind::UnselectCard &&
            candidate.action_kind == environment::EnvironmentActionKind::CardSelection &&
            candidate.card_selection_operation !=
                environment::PublicCardSelectionOperation::None) {
            throw std::invalid_argument("non-UnselectCard candidate carries operation");
        }
        keys.push_back(candidate.public_action_key);
    }
    for (std::size_t left = 0; left < keys.size(); ++left) {
        for (std::size_t right = left + 1; right < keys.size(); ++right) {
            if (keys[left] == keys[right]) {
                throw std::invalid_argument("V3 request has duplicate public action keys");
            }
        }
    }
    if (!value.continuation.has_value()) {
        for (const auto& candidate : value.candidates) {
            if (!candidate.continuation_operation.empty() ||
                !candidate.submits_engine_response) {
                throw std::invalid_argument("V3 atomic request has invalid candidate");
            }
        }
        return;
    }

    const auto& continuation = *value.continuation;
    validate_continuation(continuation);
    std::size_t finish_count = 0;
    std::size_t cancel_count = 0;
    std::size_t bypass_count = 0;
    std::vector<std::uint32_t> ordering_picks;
    const auto is_remaining_index = [&continuation](const std::uint32_t source_index) {
        return std::find(continuation.remaining_indices.begin(),
                         continuation.remaining_indices.end(), source_index) !=
               continuation.remaining_indices.end();
    };
    for (const auto& candidate : value.candidates) {
        if (candidate.continuation_operation.empty()) {
            throw std::invalid_argument("V3 continuation candidate has no operation");
        }
        if (candidate.continuation_operation == "pick") {
            if (candidate.action_kind != environment::EnvironmentActionKind::Pick ||
                candidate.submits_engine_response || continuation.continuation_kind == "counter" ||
                !candidate.source_index.has_value() ||
                !is_remaining_index(*candidate.source_index)) {
                throw std::invalid_argument("V3 pick candidate is structurally invalid");
            }
            if (continuation.continuation_kind == "ordering") {
                ordering_picks.push_back(*candidate.source_index);
            }
        } else if (candidate.continuation_operation == "amount") {
            if (candidate.action_kind != environment::EnvironmentActionKind::AssignAmount ||
                candidate.submits_engine_response || continuation.continuation_kind != "counter" ||
                !candidate.source_index.has_value() || !candidate.amount.has_value() ||
                *candidate.amount < 0 || !is_remaining_index(*candidate.source_index) ||
                continuation.remaining_indices.empty() ||
                *candidate.source_index != continuation.remaining_indices.front()) {
                throw std::invalid_argument("V3 amount candidate is structurally invalid");
            }
        } else if (candidate.continuation_operation == "finish") {
            ++finish_count;
            if (candidate.action_kind != environment::EnvironmentActionKind::Finish ||
                !candidate.submits_engine_response || !continuation.can_finish) {
                throw std::invalid_argument("V3 finish candidate is structurally invalid");
            }
        } else if (candidate.continuation_operation == "cancel") {
            ++cancel_count;
            if (candidate.action_kind != environment::EnvironmentActionKind::Cancel ||
                !candidate.submits_engine_response || !continuation.can_cancel) {
                throw std::invalid_argument("V3 cancel candidate is structurally invalid");
            }
        } else if (candidate.continuation_operation == "bypass") {
            ++bypass_count;
            if (candidate.action_kind != environment::EnvironmentActionKind::Cancel ||
                !candidate.submits_engine_response ||
                continuation.continuation_kind != "ordering") {
                throw std::invalid_argument("V3 bypass candidate is structurally invalid");
            }
        } else {
            throw std::invalid_argument("V3 continuation candidate has unknown operation");
        }
    }
    if (continuation.continuation_kind == "ordering" &&
        (bypass_count != 1 || ordering_picks != continuation.remaining_indices)) {
        throw std::invalid_argument("V3 ordering domain is incomplete");
    }
    if (finish_count > 1 || cancel_count > 1 || bypass_count > 1 ||
        (finish_count != 0) != continuation.can_finish ||
        (cancel_count != 0) != continuation.can_cancel) {
        throw std::invalid_argument("V3 continuation terminal candidates disagree with state");
    }
}

void write_request_v3(
    ByteWriter& writer, const environment::EnvironmentDecisionRequest& value) {
    validate_v3_request(value);
    writer.string(kCandidateSchemaV3);
    writer.string(environment::environment_decision_kind_name(value.kind));
    writer.u8(value.player);
    require_length(value.candidates.size());
    writer.u32be(static_cast<std::uint32_t>(value.candidates.size()));
    for (const auto& candidate : value.candidates) {
        write_public_candidate_v3(writer, candidate);
    }
    writer.u8(value.continuation.has_value() ? 1 : 0);
    if (value.continuation.has_value()) {
        write_continuation_v3(writer, *value.continuation);
    }
}

bool read_request_v3_direct(
    ByteReader& reader, environment::EnvironmentDecisionRequest& value) noexcept {
    std::string schema;
    std::string kind_token;
    std::uint32_t count = 0;
    if (!reader.string(schema) || schema != kCandidateSchemaV3 ||
        !reader.string(kind_token) || !reader.u8(value.player) || value.player > 1 ||
        !reader.u32be(count)) {
        return false;
    }
    const auto kind = decision_kind_from_token(kind_token);
    if (!kind.has_value() || count > reader.remaining()) {
        return false;
    }
    value.kind = *kind;
    value.candidates.clear();
    try {
        value.candidates.reserve(count);
    } catch (...) {
        return false;
    }
    for (std::uint32_t index = 0; index < count; ++index) {
        environment::EnvironmentActionCandidate candidate;
        if (!read_public_candidate_v3(reader, candidate)) {
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
        if (!read_continuation_v3(reader, continuation)) {
            return false;
        }
        value.continuation = std::move(continuation);
    } else {
        value.continuation.reset();
    }
    try {
        validate_v3_request(value);
    } catch (...) {
        return false;
    }
    return true;
}

void validate_v3_frame(const PublicFrameSnapshotV3& value) {
    require_contract(value.episodic_environment_contract_id,
                     environment::kEpisodicEnvironmentV4ContractId,
                     "V3 contract");
    require_digest(value.episode_semantic_id, "episode");
    validate_v3_request(value.request);
    if (value.acting_player > 1 || value.request.player != value.acting_player ||
        value.public_observation.perspective_player != value.acting_player ||
        value.public_observation.decision_index != value.decision_index) {
        throw std::invalid_argument("V3 frame perspective or index mismatch");
    }
    if (environment::public_observation_digest(value.public_observation) !=
        value.public_observation_digest) {
        throw std::invalid_argument("V3 frame observation digest mismatch");
    }
    std::vector<std::string> keys;
    keys.reserve(value.request.candidates.size());
    for (const auto& candidate : value.request.candidates) {
        keys.push_back(candidate.public_action_key);
    }
    if (environment::public_candidate_domain_digest_v3(
            std::string(environment::environment_decision_kind_name(value.request.kind)), keys) !=
        value.public_candidate_domain_digest) {
        throw std::invalid_argument("V3 frame candidate digest mismatch");
    }
    environment::PublicSemanticDecisionIdentityInputV3 identity;
    identity.episode_semantic_id = value.episode_semantic_id;
    identity.decision_index = value.decision_index;
    identity.acting_player = value.acting_player;
    identity.request_kind =
        std::string(environment::environment_decision_kind_name(value.request.kind));
    identity.public_observation_digest = value.public_observation_digest;
    identity.public_candidate_domain_digest = value.public_candidate_domain_digest;
    identity.public_action_keys = keys;
    if (environment::public_semantic_decision_id_v3(identity) !=
        value.public_semantic_decision_id) {
        throw std::invalid_argument("V3 frame decision identity mismatch");
    }
}

bool read_frame_v3_direct(ByteReader& reader, PublicFrameSnapshotV3& value) noexcept {
    std::string schema;
    if (!reader.string(schema) || schema != kFrameSchemaV3 ||
        !reader.string(value.episodic_environment_contract_id) ||
        value.episodic_environment_contract_id != environment::kEpisodicEnvironmentV4ContractId ||
        !reader.string(value.episode_semantic_id) ||
        !reader.string(value.public_semantic_decision_id) ||
        !reader.u64be(value.decision_index) || !reader.u8(value.acting_player) ||
        value.acting_player > 1 ||
        !read_public_observation_direct(reader, value.public_observation) ||
        !reader.string(value.public_observation_digest) ||
        !read_request_v3_direct(reader, value.request) ||
        !reader.string(value.public_candidate_domain_digest)) {
        return false;
    }
    try {
        validate_v3_frame(value);
    } catch (...) {
        return false;
    }
    return true;
}

void validate_v3_public_record(const DecisionRecordV3& value) {
    validate_v3_frame(value.frame);
    validate_v3_successor(value.successor);
    if (!environment::is_public_action_key_v3(value.selected_public_action_key)) {
        throw std::invalid_argument("V3 selected action key is invalid");
    }
    std::size_t matches = 0;
    const environment::EnvironmentActionCandidate* selected = nullptr;
    for (const auto& candidate : value.frame.request.candidates) {
        if (candidate.public_action_key == value.selected_public_action_key) {
            ++matches;
            selected = &candidate;
        }
    }
    if (matches != 1 || selected == nullptr) {
        throw std::invalid_argument("V3 selection is not exactly one candidate");
    }
    const auto transition = static_cast<std::uint8_t>(value.transition_class);
    if (!valid_transition_class(transition)) {
        throw std::invalid_argument("V3 transition class is unknown");
    }
    if (value.transition_class == TransitionClass::AtomicEngineResponse) {
        if (value.frame.request.continuation.has_value() || !selected->submits_engine_response) {
            throw std::invalid_argument("V3 atomic transition is inconsistent");
        }
    } else if (value.transition_class == TransitionClass::IntermediateContinuation) {
        if (!value.frame.request.continuation.has_value() || selected->submits_engine_response) {
            throw std::invalid_argument("V3 intermediate transition is inconsistent");
        }
    } else if (value.transition_class == TransitionClass::FinalContinuationResponse) {
        if (!value.frame.request.continuation.has_value() || !selected->submits_engine_response) {
            throw std::invalid_argument("V3 final transition is inconsistent");
        }
    }
    if (value.successor.kind == SuccessorKind::NextFrame &&
        (!value.successor.next_frame.has_value() ||
         value.frame.decision_index == std::numeric_limits<std::uint64_t>::max() ||
         value.successor.next_frame->next_decision_index != value.frame.decision_index + 1)) {
        throw std::invalid_argument("V3 successor index is inconsistent");
    }
}

bool read_public_record_v3_direct(ByteReader& reader, DecisionRecordV3& value) noexcept {
    std::string schema;
    std::uint8_t transition = 0;
    if (!reader.string(schema) || schema != kTrustedTrajectoryV3ContractId ||
        !read_frame_v3_direct(reader, value.frame) ||
        !reader.string(value.selected_public_action_key) ||
        !reader.u8(transition) || !valid_transition_class(transition) ||
        !read_successor(reader, value.successor)) {
        return false;
    }
    value.transition_class = static_cast<TransitionClass>(transition);
    try {
        validate_v3_public_record(value);
    } catch (...) {
        return false;
    }
    return true;
}

}  // namespace

std::vector<std::uint8_t> canonical_public_environment_action_candidate_bytes_v3(
    const environment::EnvironmentActionCandidate& value) {
    ByteWriter writer;
    write_public_candidate_v3(writer, value);
    return std::move(writer).take();
}

DecodeResult<environment::EnvironmentActionCandidate>
decode_public_environment_action_candidate_v3(
    const std::vector<std::uint8_t>& bytes) noexcept {
    try {
        ByteReader reader(bytes);
        environment::EnvironmentActionCandidate value;
        if (!read_public_candidate_v3(reader, value) || !reader.at_end() ||
            canonical_public_environment_action_candidate_bytes_v3(value) != bytes) {
            return failure<environment::EnvironmentActionCandidate>(
                "malformed or noncanonical V3 candidate");
        }
        return success(std::move(value));
    } catch (const std::exception& error) {
        return failure<environment::EnvironmentActionCandidate>(error.what());
    } catch (...) {
        return failure<environment::EnvironmentActionCandidate>("V3 candidate decode threw");
    }
}

std::vector<std::uint8_t> canonical_public_environment_decision_request_bytes_v3(
    const environment::EnvironmentDecisionRequest& value) {
    ByteWriter writer;
    write_request_v3(writer, value);
    return std::move(writer).take();
}

DecodeResult<environment::EnvironmentDecisionRequest>
decode_public_environment_decision_request_v3(
    const std::vector<std::uint8_t>& bytes) noexcept {
    try {
        ByteReader reader(bytes);
        environment::EnvironmentDecisionRequest value;
        if (!read_request_v3_direct(reader, value) || !reader.at_end() ||
            canonical_public_environment_decision_request_bytes_v3(value) != bytes) {
            return failure<environment::EnvironmentDecisionRequest>(
                "malformed or noncanonical V3 request");
        }
        return success(std::move(value));
    } catch (const std::exception& error) {
        return failure<environment::EnvironmentDecisionRequest>(error.what());
    } catch (...) {
        return failure<environment::EnvironmentDecisionRequest>("V3 request decode threw");
    }
}

std::vector<std::uint8_t> canonical_public_frame_snapshot_bytes_v3(
    const PublicFrameSnapshotV3& value) {
    validate_v3_frame(value);
    ByteWriter writer;
    writer.string(kFrameSchemaV3);
    writer.string(value.episodic_environment_contract_id);
    writer.string(value.episode_semantic_id);
    writer.string(value.public_semantic_decision_id);
    writer.u64be(value.decision_index);
    writer.u8(value.acting_player);
    writer.raw(environment::canonical_public_environment_observation_bytes(
        value.public_observation));
    writer.string(value.public_observation_digest);
    writer.raw(canonical_public_environment_decision_request_bytes_v3(value.request));
    writer.string(value.public_candidate_domain_digest);
    return std::move(writer).take();
}

DecodeResult<PublicFrameSnapshotV3> decode_public_frame_snapshot_v3(
    const std::vector<std::uint8_t>& bytes) noexcept {
    try {
        ByteReader reader(bytes);
        PublicFrameSnapshotV3 value;
        if (!read_frame_v3_direct(reader, value) || !reader.at_end() ||
            canonical_public_frame_snapshot_bytes_v3(value) != bytes) {
            return failure<PublicFrameSnapshotV3>("malformed or noncanonical V3 frame");
        }
        return success(std::move(value));
    } catch (const std::exception& error) {
        return failure<PublicFrameSnapshotV3>(error.what());
    } catch (...) {
        return failure<PublicFrameSnapshotV3>("V3 frame decode threw");
    }
}

std::vector<std::uint8_t> canonical_public_decision_record_bytes_v3(
    const DecisionRecordV3& value) {
    validate_v3_public_record(value);
    ByteWriter writer;
    writer.string(kTrustedTrajectoryV3ContractId);
    writer.raw(canonical_public_frame_snapshot_bytes_v3(value.frame));
    writer.string(value.selected_public_action_key);
    writer.u8(static_cast<std::uint8_t>(value.transition_class));
    write_successor(writer, value.successor);
    return std::move(writer).take();
}

DecodeResult<DecisionRecordV3> decode_public_decision_record_v3(
    const std::vector<std::uint8_t>& bytes) noexcept {
    try {
        ByteReader reader(bytes);
        DecisionRecordV3 value;
        if (!read_public_record_v3_direct(reader, value) || !reader.at_end() ||
            canonical_public_decision_record_bytes_v3(value) != bytes) {
            return failure<DecisionRecordV3>("malformed or noncanonical V3 public record");
        }
        return success(std::move(value));
    } catch (const std::exception& error) {
        return failure<DecisionRecordV3>(error.what());
    } catch (...) {
        return failure<DecisionRecordV3>("V3 public record decode threw");
    }
}

namespace {

void validate_v3_decision_record(const DecisionRecordV3& value) {
    validate_v3_public_record(value);
    require_identity(value.acting_policy_assignment_id,
                     "participant_policy_assignment.v1.", "V3 record assignment");
    validate_rng_decision(value.policy_rng_decision_provenance);
    if (value.policy_rng_decision_provenance.decision_index != value.frame.decision_index ||
        value.policy_rng_decision_provenance.acting_policy_assignment_id !=
            value.acting_policy_assignment_id) {
        throw std::invalid_argument("V3 record attribution does not match frame");
    }
}

std::vector<std::uint8_t> canonical_policy_decision_attribution_bytes_v3_impl(
    const DecisionRecordV3& value) {
    validate_v3_decision_record(value);
    ByteWriter writer;
    writer.string(kPolicyProvenanceContractId);
    writer.string(value.acting_policy_assignment_id);
    writer.raw(canonical_policy_rng_decision_provenance_bytes(
        value.policy_rng_decision_provenance));
    return std::move(writer).take();
}

bool read_collection_record_v3_direct(ByteReader& reader, DecisionRecordV3& value) noexcept {
    std::string schema;
    std::string attribution_schema;
    if (!reader.string(schema) || schema != kTrustedTrajectoryV3ContractId ||
        !read_public_record_v3_direct(reader, value) ||
        !reader.string(attribution_schema) ||
        attribution_schema != kPolicyProvenanceContractId ||
        !reader.string(value.acting_policy_assignment_id) ||
        !read_policy_rng_decision_direct(reader, value.policy_rng_decision_provenance)) {
        return false;
    }
    try {
        validate_v3_decision_record(value);
    } catch (...) {
        return false;
    }
    return true;
}

void validate_v3_terminal_closure(const TerminalClosureV3& value) {
    if (value.terminal_view_player_0.perspective_player != 0 ||
        value.terminal_view_player_1.perspective_player != 1 ||
        value.winner > 2 || value.win_reason == 255 ||
        (value.semantic_action_count == 0
             ? value.last_decision_index.has_value()
             : !value.last_decision_index.has_value() ||
                   *value.last_decision_index == std::numeric_limits<std::uint64_t>::max() ||
                   *value.last_decision_index + 1 != value.semantic_action_count)) {
        throw std::invalid_argument("V3 terminal closure is inconsistent");
    }
    if (environment::public_observation_digest(value.terminal_view_player_0) !=
            value.terminal_view_player_0_digest ||
        environment::public_observation_digest(value.terminal_view_player_1) !=
            value.terminal_view_player_1_digest) {
        throw std::invalid_argument("V3 terminal observation digest mismatch");
    }
}

void validate_v3_interrupted_closure(const InterruptedClosureV3& value) {
    if (value.pending_unacted_frame.has_value()) {
        if (value.pending_unacted_frame->decision_index != value.record_count) {
            throw std::invalid_argument("V3 pending frame index is inconsistent");
        }
        validate_v3_frame(*value.pending_unacted_frame);
    }
}

void validate_v3_failed_closure(const FailedClosureV3& value) {
    if (!valid_failure_code(static_cast<std::uint8_t>(value.failure_code)) ||
        !valid_failure_stage(static_cast<std::uint8_t>(value.failure_stage))) {
        throw std::invalid_argument("V3 failed closure has an unknown failure value");
    }
}

void validate_v3_closure(const EpisodeClosureV3& value) {
    std::visit(
        [](const auto& closure) {
            using T = std::decay_t<decltype(closure)>;
            if constexpr (std::is_same_v<T, TerminalClosureV3>) {
                validate_v3_terminal_closure(closure);
            } else if constexpr (std::is_same_v<T, InterruptedClosureV3>) {
                validate_v3_interrupted_closure(closure);
            } else {
                validate_v3_failed_closure(closure);
            }
        },
        value);
}

void write_closure_v3(ByteWriter& writer, const EpisodeClosureV3& value) {
    validate_v3_closure(value);
    writer.string(kClosureSchemaV3);
    if (const auto* terminal = std::get_if<TerminalClosureV3>(&value)) {
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
    } else if (const auto* interrupted = std::get_if<InterruptedClosureV3>(&value)) {
        writer.u8(1);
        writer.u64be(interrupted->record_count);
        writer.u8(interrupted->pending_unacted_frame.has_value() ? 1 : 0);
        if (interrupted->pending_unacted_frame.has_value()) {
            writer.raw(canonical_public_frame_snapshot_bytes_v3(
                *interrupted->pending_unacted_frame));
        }
    } else {
        const auto& failed = std::get<FailedClosureV3>(value);
        writer.u8(2);
        writer.u8(static_cast<std::uint8_t>(failed.failure_code));
        writer.u8(static_cast<std::uint8_t>(failed.failure_stage));
        writer.boolean(failed.mutation_may_have_occurred);
        writer.u64be(failed.record_count);
    }
}

bool read_closure_v3_direct(ByteReader& reader, EpisodeClosureV3& value) noexcept {
    std::string schema;
    std::uint8_t kind = 0;
    if (!reader.string(schema) || schema != kClosureSchemaV3 || !reader.u8(kind) ||
        !valid_closure_kind(kind)) {
        return false;
    }
    if (kind == 0) {
        TerminalClosureV3 terminal;
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
        InterruptedClosureV3 interrupted;
        std::uint8_t present = 0;
        if (!reader.u64be(interrupted.record_count) || !reader.u8(present) || present > 1) {
            return false;
        }
        if (present == 1) {
            PublicFrameSnapshotV3 frame;
            if (!read_frame_v3_direct(reader, frame)) {
                return false;
            }
            interrupted.pending_unacted_frame = std::move(frame);
        }
        value = std::move(interrupted);
    } else {
        FailedClosureV3 failed;
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
        validate_v3_closure(value);
    } catch (...) {
        return false;
    }
    return true;
}

void validate_v3_manifest(const EpisodeManifestV3& value) {
    require_contract(value.trusted_trajectory_contract_id, kTrustedTrajectoryV3ContractId,
                     "V3 trusted trajectory contract");
    require_contract(value.episodic_environment_contract_id,
                     environment::kEpisodicEnvironmentV4ContractId,
                     "V3 environment contract");
    require_contract(value.episode_identity_schema_id, environment::kEpisodeIdentitySchemaId,
                     "episode identity schema");
    require_digest(value.environment_semantic_id, "V3 environment semantic ID");
    require_digest(value.episode_semantic_id, "V3 episode semantic ID");
    if (value.environment_identity_input.empty() || value.episode_identity_input.empty() ||
        trace::sha256_bytes(value.environment_identity_input) != value.environment_semantic_id ||
        trace::sha256_bytes(value.episode_identity_input) != value.episode_semantic_id) {
        throw std::invalid_argument("V3 manifest identity input mismatch");
    }
    const auto environment_result =
        decode_environment_identity_input_v4(value.environment_identity_input);
    if (!environment_result ||
        environment_result.value->environment_semantic_id != value.environment_semantic_id ||
        !is_current_certified_environment_v4(*environment_result.value)) {
        throw std::invalid_argument("V3 manifest environment identity is not current V4");
    }
    const auto episode_result = decode_episode_identity_input_v4(
        value.episode_identity_input, *environment_result.value);
    if (!episode_result ||
        environment::episode_semantic_id(*environment_result.value, *episode_result.value) !=
            value.episode_semantic_id) {
        throw std::invalid_argument("V3 manifest episode identity is not canonical V3");
    }
    (void)canonical_policy_provenance_envelope_bytes(value.policy_provenance);
    (void)canonical_collection_disposition_bytes(value.collection_disposition);
}

bool read_manifest_v3_direct(ByteReader& reader, EpisodeManifestV3& value) noexcept {
    std::string schema;
    if (!reader.string(schema) || schema != kManifestSchemaV3 ||
        value.trusted_trajectory_contract_id != kTrustedTrajectoryV3ContractId ||
        !reader.string(value.episodic_environment_contract_id) ||
        value.episodic_environment_contract_id != environment::kEpisodicEnvironmentV4ContractId ||
        !reader.string(value.environment_semantic_id) ||
        !reader.bytes(value.environment_identity_input) ||
        !reader.string(value.episode_identity_schema_id) ||
        !reader.string(value.episode_semantic_id) ||
        !reader.bytes(value.episode_identity_input) ||
        !read_policy_provenance_direct(reader, value.policy_provenance) ||
        !read_disposition_direct(reader, value.collection_disposition)) {
        return false;
    }
    try {
        validate_v3_manifest(value);
    } catch (...) {
        return false;
    }
    return true;
}

void validate_v3_envelope_sequence(const EpisodeEnvelopeV3& value) {
    validate_v3_manifest(value.manifest);
    require_length(value.records.size());
    for (std::size_t index = 0; index < value.records.size(); ++index) {
        const auto& record = value.records[index];
        if (record.frame.episode_semantic_id != value.manifest.episode_semantic_id ||
            record.frame.decision_index != index) {
            throw std::invalid_argument("V3 envelope record identity or index mismatch");
        }
        validate_v3_decision_record(record);
        if (index > 0) {
            const auto& previous = value.records[index - 1];
            if (previous.successor.kind != SuccessorKind::NextFrame ||
                !previous.successor.next_frame.has_value() ||
                previous.successor.next_frame->kind != NextFrameTargetKind::NextDecisionRecord ||
                previous.successor.next_frame->next_decision_index != index ||
                previous.successor.next_frame->next_public_semantic_decision_id !=
                    record.frame.public_semantic_decision_id) {
                throw std::invalid_argument("V3 envelope successor sequence is inconsistent");
            }
        }
    }
    const auto count = static_cast<std::uint64_t>(value.records.size());
    if (const auto* terminal = std::get_if<TerminalClosureV3>(&value.closure)) {
        if (terminal->semantic_action_count != count ||
            (count == 0 ? terminal->last_decision_index.has_value()
                        : !terminal->last_decision_index.has_value() ||
                              *terminal->last_decision_index == std::numeric_limits<std::uint64_t>::max() ||
                              *terminal->last_decision_index + 1 != count)) {
            throw std::invalid_argument("V3 terminal envelope count is inconsistent");
        }
        if (!value.records.empty() &&
            value.records.back().successor.kind != SuccessorKind::Terminal) {
            throw std::invalid_argument("V3 terminal envelope has no terminal successor");
        }
    } else if (const auto* interrupted = std::get_if<InterruptedClosureV3>(&value.closure)) {
        if (interrupted->record_count != count) {
            throw std::invalid_argument("V3 interrupted envelope count is inconsistent");
        }
        if (interrupted->pending_unacted_frame.has_value()) {
            const auto& pending = *interrupted->pending_unacted_frame;
            if (pending.episode_semantic_id != value.manifest.episode_semantic_id ||
                pending.decision_index != count ||
                (!value.records.empty() &&
                 (value.records.back().successor.kind != SuccessorKind::NextFrame ||
                  !value.records.back().successor.next_frame.has_value() ||
                  value.records.back().successor.next_frame->kind !=
                      NextFrameTargetKind::InterruptionPendingUnactedFrame ||
                  value.records.back().successor.next_frame->next_public_semantic_decision_id !=
                      pending.public_semantic_decision_id))) {
                throw std::invalid_argument("V3 pending interruption is inconsistent");
            }
        } else if (!value.records.empty() &&
                   value.records.back().successor.kind != SuccessorKind::Interrupted) {
            throw std::invalid_argument("V3 interrupted envelope has no interruption successor");
        }
    } else {
        const auto& failed = std::get<FailedClosureV3>(value.closure);
        if (failed.record_count != count) {
            throw std::invalid_argument("V3 failed envelope count is inconsistent");
        }
        if (!value.records.empty() &&
            value.records.back().successor.kind != SuccessorKind::Failed) {
            throw std::invalid_argument("V3 failed envelope has no failed successor");
        }
    }
}

}  // namespace

std::vector<std::uint8_t> canonical_policy_decision_attribution_bytes_v3(
    const DecisionRecordV3& value) {
    return canonical_policy_decision_attribution_bytes_v3_impl(value);
}

std::vector<std::uint8_t> canonical_collection_decision_record_bytes_v3(
    const DecisionRecordV3& value) {
    validate_v3_decision_record(value);
    ByteWriter writer;
    writer.string(kTrustedTrajectoryV3ContractId);
    writer.raw(canonical_public_decision_record_bytes_v3(value));
    writer.raw(canonical_policy_decision_attribution_bytes_v3_impl(value));
    return std::move(writer).take();
}

DecodeResult<DecisionRecordV3> decode_collection_decision_record_v3(
    const std::vector<std::uint8_t>& bytes) noexcept {
    try {
        ByteReader reader(bytes);
        DecisionRecordV3 value;
        if (!read_collection_record_v3_direct(reader, value) || !reader.at_end() ||
            canonical_collection_decision_record_bytes_v3(value) != bytes) {
            return failure<DecisionRecordV3>("malformed or noncanonical V3 collection record");
        }
        return success(std::move(value));
    } catch (const std::exception& error) {
        return failure<DecisionRecordV3>(error.what());
    } catch (...) {
        return failure<DecisionRecordV3>("V3 collection record decode threw");
    }
}

std::vector<std::uint8_t> canonical_episode_closure_bytes_v3(
    const EpisodeClosureV3& value) {
    ByteWriter writer;
    write_closure_v3(writer, value);
    return std::move(writer).take();
}

DecodeResult<EpisodeClosureV3> decode_episode_closure_v3(
    const std::vector<std::uint8_t>& bytes) noexcept {
    try {
        ByteReader reader(bytes);
        EpisodeClosureV3 value;
        if (!read_closure_v3_direct(reader, value) || !reader.at_end() ||
            canonical_episode_closure_bytes_v3(value) != bytes) {
            return failure<EpisodeClosureV3>("malformed or noncanonical V3 closure");
        }
        return success(std::move(value));
    } catch (const std::exception& error) {
        return failure<EpisodeClosureV3>(error.what());
    } catch (...) {
        return failure<EpisodeClosureV3>("V3 closure decode threw");
    }
}

std::vector<std::uint8_t> canonical_episode_manifest_bytes_v3(
    const EpisodeManifestV3& value) {
    validate_v3_manifest(value);
    ByteWriter writer;
    writer.string(kManifestSchemaV3);
    writer.string(value.episodic_environment_contract_id);
    writer.string(value.environment_semantic_id);
    writer.bytes(value.environment_identity_input);
    writer.string(value.episode_identity_schema_id);
    writer.string(value.episode_semantic_id);
    writer.bytes(value.episode_identity_input);
    writer.raw(canonical_policy_provenance_envelope_bytes(value.policy_provenance));
    writer.raw(canonical_collection_disposition_bytes(value.collection_disposition));
    return std::move(writer).take();
}

DecodeResult<EpisodeManifestV3> decode_episode_manifest_v3(
    const std::vector<std::uint8_t>& bytes) noexcept {
    try {
        ByteReader reader(bytes);
        EpisodeManifestV3 value;
        if (!read_manifest_v3_direct(reader, value) || !reader.at_end() ||
            canonical_episode_manifest_bytes_v3(value) != bytes) {
            return failure<EpisodeManifestV3>("malformed or noncanonical V3 manifest");
        }
        return success(std::move(value));
    } catch (const std::exception& error) {
        return failure<EpisodeManifestV3>(error.what());
    } catch (...) {
        return failure<EpisodeManifestV3>("V3 manifest decode threw");
    }
}

std::vector<std::uint8_t> canonical_episode_envelope_bytes_v3(
    const EpisodeEnvelopeV3& value) {
    validate_v3_envelope_sequence(value);
    ByteWriter writer;
    writer.string(kEnvelopeSchemaV3);
    writer.raw(canonical_episode_manifest_bytes_v3(value.manifest));
    require_length(value.records.size());
    writer.u32be(static_cast<std::uint32_t>(value.records.size()));
    for (const auto& record : value.records) {
        writer.raw(canonical_collection_decision_record_bytes_v3(record));
    }
    writer.raw(canonical_episode_closure_bytes_v3(value.closure));
    return std::move(writer).take();
}

DecodeResult<EpisodeEnvelopeV3> decode_episode_envelope_v3(
    const std::vector<std::uint8_t>& bytes) noexcept {
    try {
        ByteReader reader(bytes);
        EpisodeEnvelopeV3 value;
        std::string schema;
        std::uint32_t count = 0;
        if (!reader.string(schema) || schema != kEnvelopeSchemaV3 ||
            !read_manifest_v3_direct(reader, value.manifest) || !reader.u32be(count) ||
            count > reader.remaining()) {
            return failure<EpisodeEnvelopeV3>("malformed V3 envelope header");
        }
        value.records.reserve(count);
        for (std::uint32_t index = 0; index < count; ++index) {
            DecisionRecordV3 record;
            if (!read_collection_record_v3_direct(reader, record)) {
                return failure<EpisodeEnvelopeV3>("malformed V3 collection record");
            }
            value.records.push_back(std::move(record));
        }
        if (!read_closure_v3_direct(reader, value.closure) || !reader.at_end()) {
            return failure<EpisodeEnvelopeV3>("malformed V3 closure or trailing bytes");
        }
        validate_v3_envelope_sequence(value);
        if (canonical_episode_envelope_bytes_v3(value) != bytes) {
            return failure<EpisodeEnvelopeV3>("noncanonical V3 envelope");
        }
        return success(std::move(value));
    } catch (const std::exception& error) {
        return failure<EpisodeEnvelopeV3>(error.what());
    } catch (...) {
        return failure<EpisodeEnvelopeV3>("V3 envelope decode threw");
    }
}

}  // namespace ygo::trajectory
