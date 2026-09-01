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
#include "ygo/observation/chain_state.hpp"
#include "ygo/observation/observed_card.hpp"
#include "ygo/observation/observed_player_globals.hpp"
#include "ygo/observation/observed_zone.hpp"
#include "ygo/observation/relationship.hpp"
#include "ygo/observation/match_context.hpp"

namespace ygo::model {

inline constexpr std::string_view kLogicalModelInputSchemaId =
    "ocgforge.model_logical_input.v1";

enum class LogicalModelProjectionErrorCode : std::uint8_t {
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

struct LogicalModelProjectionError final {
    LogicalModelProjectionErrorCode code =
        LogicalModelProjectionErrorCode::InternalFailure;
    std::string diagnostic;
};

struct LogicalPublicLocator final {
    std::string value;
    std::uint32_t public_locator_ordinal = 0;
};

struct LogicalCurrentReference final {
    LogicalPublicLocator locator;
    std::optional<std::uint32_t> current_entity_ordinal;
};

struct LogicalHistoricalReference final {
    LogicalPublicLocator locator;
};

struct LogicalPublicCardReference final {
    ygo::environment::PublicCardReferenceKind kind =
        ygo::environment::PublicCardReferenceKind::RedactedSlot;
    LogicalCurrentReference reference;
};

struct LogicalEntity final {
    ygo::observation::ObservedCard card;
    std::uint32_t public_locator_ordinal = 0;
    std::uint32_t current_entity_ordinal = 0;
};

struct LogicalRelationship final {
    ygo::observation::RelationshipKind kind =
        ygo::observation::RelationshipKind::Target;
    LogicalCurrentReference source;
    LogicalCurrentReference target;
};

struct LogicalChainLink final {
    std::uint32_t index = 0;
    std::optional<std::uint8_t> activating_player;
    std::optional<LogicalCurrentReference> source;
    std::optional<ygo::observation::SemanticZone> activation_zone;
    std::optional<std::uint64_t> effect_description;
    std::vector<LogicalCurrentReference> targets;
};

struct LogicalChainState final {
    std::uint32_t length = 0;
    std::vector<LogicalChainLink> links;
};

struct LogicalVisibleEvent final {
    std::uint64_t event_index = 0;
    ygo::observation::VisibleEventKind kind =
        ygo::observation::VisibleEventKind::Unknown;
    std::optional<std::uint8_t> player;
    std::optional<LogicalHistoricalReference> entity;
    std::optional<std::uint32_t> public_passcode;
    std::optional<ygo::observation::SemanticZone> from_zone;
    std::optional<ygo::observation::SemanticZone> to_zone;
    std::optional<std::uint32_t> count;
    std::optional<std::int32_t> amount;
    std::optional<std::uint32_t> counter_type;
    std::optional<std::uint32_t> phase;
    std::optional<std::uint8_t> winner;
    std::optional<std::uint8_t> win_reason;
    std::optional<std::uint64_t> effect_description;
    std::vector<LogicalHistoricalReference> targets;
};

struct LogicalPublicState final {
    ygo::observation::ObservedPlayerGlobals globals;
    std::vector<ygo::observation::ObservedZone> zones;
    std::vector<LogicalEntity> entities;
    std::vector<LogicalRelationship> relationships;
    LogicalChainState chain;
    std::vector<LogicalVisibleEvent> visible_events;
    ygo::observation::MatchContext match_context;
};

struct LogicalCandidate final {
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
};

struct LogicalCandidateRouting final {
    std::string public_action_key;
};

struct LogicalModelInputV1 final {
    std::string schema_id = std::string(kLogicalModelInputSchemaId);
    std::string public_observation_digest;
    std::optional<std::string> public_candidate_domain_digest;
    std::uint8_t perspective_player = 0;
    std::uint64_t decision_index = 0;
    std::optional<std::string> public_observation_context_kind;
    std::optional<std::uint8_t> public_observation_context_player;
    std::vector<LogicalPublicLocator> referenced_public_entities;
    std::vector<LogicalPublicLocator> public_locator_table;
    LogicalPublicState public_safe_state;
    std::vector<LogicalCandidateRouting> candidate_routing;
    std::vector<LogicalCandidate> candidate_features;

    std::size_t candidate_count() const noexcept {
        return candidate_features.size();
    }
};

using LogicalModelInput = LogicalModelInputV1;

struct LogicalModelProjectionResult final {
    std::optional<LogicalModelInputV1> value;
    std::optional<LogicalModelProjectionError> error;

    explicit operator bool() const noexcept {
        return value.has_value() && !error.has_value();
    }
};

std::string_view logical_model_projection_error_code_name(
    LogicalModelProjectionErrorCode code) noexcept;

LogicalModelProjectionResult project_logical_model_input_v1(
    const ygo::environment::PublicEnvironmentObservation& observation,
    const std::vector<ygo::environment::EnvironmentActionCandidate>& candidates) noexcept;

inline LogicalModelProjectionResult project_logical_model_input(
    const ygo::environment::PublicEnvironmentObservation& observation,
    const std::vector<ygo::environment::EnvironmentActionCandidate>& candidates) noexcept {
    return project_logical_model_input_v1(observation, candidates);
}

}  // namespace ygo::model
