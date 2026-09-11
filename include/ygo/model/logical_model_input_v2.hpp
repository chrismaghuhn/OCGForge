#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "ygo/environment/public_action_identity.hpp"
#include "ygo/environment/public_decision.hpp"
#include "ygo/environment/public_environment_observation.hpp"
#include "ygo/environment/public_safe_state.hpp"
#include "ygo/model/logical_model_input.hpp"

namespace ygo::model {

inline constexpr std::string_view kLogicalModelInputV2SchemaId =
    "ocgforge.model_logical_input.v2";

enum class LogicalModelProjectionErrorCodeV2 : std::uint8_t {
    InvalidPublicObservation,
    PublicSafeStateDecodeFailure,
    EmptyCandidateDomain,
    InvalidPublicActionKey,
    DuplicatePublicActionKey,
    InvalidPublicCandidateDescriptor,
    InvalidPublicReference,
    CandidateDomainDigestFailure,
    LocatorTableFailure,
    InternalFailure,
};

struct LogicalModelProjectionErrorV2 final {
    LogicalModelProjectionErrorCodeV2 code =
        LogicalModelProjectionErrorCodeV2::InternalFailure;
    std::string diagnostic;
};

struct LogicalCandidateV2 final {
    ygo::environment::EnvironmentActionKind action_kind =
        ygo::environment::EnvironmentActionKind::Unsupported;
    std::optional<ygo::environment::PublicChoice> choice;
    std::optional<LogicalPublicCardReference> source_reference;
    std::optional<LogicalPublicCardReference> target_reference;
    std::optional<std::uint32_t> phase;
    std::optional<std::uint8_t> position;
    std::optional<std::uint32_t> source_index;
    std::optional<std::int32_t> amount;
    std::string continuation_operation;
    bool submits_engine_response = true;
    ygo::environment::PublicCardSelectionOperation card_selection_operation =
        ygo::environment::PublicCardSelectionOperation::None;
};

struct LogicalCandidateRoutingV2 final {
    std::string public_action_key;
};

struct LogicalModelInputV2 final {
    std::string schema_id = std::string(kLogicalModelInputV2SchemaId);
    std::string public_observation_digest;
    std::optional<std::string> public_candidate_domain_digest;
    std::uint8_t perspective_player = 0;
    std::uint64_t decision_index = 0;
    std::optional<std::string> public_observation_context_kind;
    std::optional<std::uint8_t> public_observation_context_player;
    std::vector<LogicalPublicLocator> referenced_public_entities;
    std::vector<LogicalPublicLocator> public_locator_table;
    LogicalPublicState public_safe_state;
    std::vector<LogicalCandidateRoutingV2> candidate_routing;
    std::vector<LogicalCandidateV2> candidate_features;

    std::size_t candidate_count() const noexcept {
        return candidate_features.size();
    }
};

struct LogicalModelProjectionResultV2 final {
    std::optional<LogicalModelInputV2> value;
    std::optional<LogicalModelProjectionErrorV2> error;

    explicit operator bool() const noexcept {
        return value.has_value() && !error.has_value();
    }
};

std::string_view logical_model_projection_error_code_name(
    LogicalModelProjectionErrorCodeV2 code) noexcept;

LogicalModelProjectionResultV2 project_logical_model_input_v2(
    const ygo::environment::PublicEnvironmentObservation& observation,
    const std::vector<ygo::environment::EnvironmentActionCandidate>& candidates) noexcept;

}  // namespace ygo::model
