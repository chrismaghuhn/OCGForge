#include "ygo/model/logical_model_input.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ygo::model {
namespace {

class ProjectionFailure final {
public:
    explicit ProjectionFailure(const LogicalModelProjectionErrorCode code) : code_(code) {}

    LogicalModelProjectionErrorCode code() const noexcept { return code_; }

private:
    LogicalModelProjectionErrorCode code_;
};

[[noreturn]] void fail(const LogicalModelProjectionErrorCode code) {
    throw ProjectionFailure(code);
}

const char* diagnostic_for(const LogicalModelProjectionErrorCode code) noexcept {
    switch (code) {
    case LogicalModelProjectionErrorCode::InvalidPublicObservation:
        return "public observation is invalid";
    case LogicalModelProjectionErrorCode::PublicSafeStateDecodeFailure:
        return "public safe state decode failed";
    case LogicalModelProjectionErrorCode::EmptyCandidateDomain:
        return "public candidate domain is empty";
    case LogicalModelProjectionErrorCode::InvalidPublicActionKey:
        return "public action key is invalid";
    case LogicalModelProjectionErrorCode::DuplicatePublicActionKey:
        return "public candidate domain contains a duplicate key";
    case LogicalModelProjectionErrorCode::InvalidPublicCandidateDescriptor:
        return "public candidate descriptor is invalid";
    case LogicalModelProjectionErrorCode::InvalidPublicReference:
        return "public reference is invalid";
    case LogicalModelProjectionErrorCode::CandidateDomainDigestFailure:
        return "public candidate-domain digest could not be derived";
    case LogicalModelProjectionErrorCode::LocatorTableFailure:
        return "public locator table could not be constructed";
    case LogicalModelProjectionErrorCode::InternalFailure:
        return "logical model projection failed";
    }
    return "logical model projection failed";
}

bool valid_utf8(const std::string_view value) noexcept {
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
            (width == 4 && code_point < 0x10000) ||
            code_point > 0x10ffff ||
            (code_point >= 0xd800 && code_point <= 0xdfff)) {
            return false;
        }
        index += width;
    }
    return true;
}

bool valid_public_locator(const std::string_view value) noexcept {
    if (!valid_utf8(value) || value.empty()) {
        return false;
    }
    return std::all_of(value.begin(), value.end(), [](const char character) {
        const auto byte = static_cast<unsigned char>(character);
        return byte >= 0x20 && byte != 0x7f;
    });
}

bool byte_less(
    const std::string_view left,
    const std::string_view right) noexcept {
    return std::lexicographical_compare(
        left.begin(), left.end(), right.begin(), right.end(),
        [](const char left_byte, const char right_byte) {
            return static_cast<unsigned char>(left_byte) <
                   static_cast<unsigned char>(right_byte);
        });
}

struct LocatorTable final {
    std::vector<std::string> values;

    std::uint32_t ordinal(const std::string_view value) const {
        const auto found = std::lower_bound(
            values.begin(), values.end(), value,
            [](const std::string& left, const std::string_view right) {
                return byte_less(left, right);
            });
        if (found == values.end() || *found != value) {
            fail(LogicalModelProjectionErrorCode::LocatorTableFailure);
        }
        const auto distance = static_cast<std::size_t>(found - values.begin());
        if (distance > std::numeric_limits<std::uint32_t>::max()) {
            fail(LogicalModelProjectionErrorCode::LocatorTableFailure);
        }
        return static_cast<std::uint32_t>(distance);
    }
};

void add_locator_token(std::vector<std::string>& values, const std::string_view value) {
    if (!valid_public_locator(value)) {
        fail(LogicalModelProjectionErrorCode::InvalidPublicReference);
    }
    values.emplace_back(value);
}

void add_optional_locator_token(
    std::vector<std::string>& values,
    const std::optional<ygo::observation::ObservationLocator>& value) {
    if (value.has_value()) {
        add_locator_token(values, value->value);
    }
}

LocatorTable make_locator_table(
    const ygo::environment::PublicEnvironmentObservation& observation,
    const ygo::environment::PublicSafeStateView& safe_state,
    const std::vector<ygo::environment::EnvironmentActionCandidate>& candidates) {
    std::vector<std::string> values;
    values.reserve(observation.decision_context.referenced_entities.size() +
                   safe_state.entities().size() + safe_state.relationships().size() * 2 +
                   safe_state.chain().links.size() * 2 +
                   safe_state.visible_events().size() * 2 + candidates.size() * 2);

    for (const auto& reference : observation.decision_context.referenced_entities) {
        add_locator_token(values, reference.value);
    }
    for (const auto& entity : safe_state.entities()) {
        add_locator_token(values, entity.locator.value);
    }
    for (const auto& relationship : safe_state.relationships()) {
        add_locator_token(values, relationship.source.value);
        add_locator_token(values, relationship.target.value);
    }
    for (const auto& link : safe_state.chain().links) {
        add_optional_locator_token(values, link.source);
        for (const auto& target : link.targets) {
            add_locator_token(values, target.value);
        }
    }
    for (const auto& event : safe_state.visible_events()) {
        add_optional_locator_token(values, event.entity);
        for (const auto& target : event.targets) {
            add_locator_token(values, target.value);
        }
    }
    for (const auto& candidate : candidates) {
        if (candidate.source_reference.has_value()) {
            add_locator_token(values, candidate.source_reference->observation_locator);
        }
        if (candidate.target_reference.has_value()) {
            add_locator_token(values, candidate.target_reference->observation_locator);
        }
    }

    std::sort(values.begin(), values.end(), byte_less);
    values.erase(std::unique(values.begin(), values.end()), values.end());
    if (values.size() > std::numeric_limits<std::uint32_t>::max()) {
        fail(LogicalModelProjectionErrorCode::LocatorTableFailure);
    }
    return LocatorTable{std::move(values)};
}

std::optional<std::uint32_t> current_entity_ordinal(
    const ygo::environment::PublicSafeStateView& safe_state,
    const std::string_view value) {
    std::optional<std::uint32_t> result;
    const auto& entities = safe_state.entities();
    for (std::size_t index = 0; index < entities.size(); ++index) {
        if (entities[index].locator.value != value) {
            continue;
        }
        if (result.has_value()) {
            fail(LogicalModelProjectionErrorCode::LocatorTableFailure);
        }
        if (index > std::numeric_limits<std::uint32_t>::max()) {
            fail(LogicalModelProjectionErrorCode::LocatorTableFailure);
        }
        result = static_cast<std::uint32_t>(index);
    }
    return result;
}

LogicalPublicLocator make_public_locator(
    const LocatorTable& table,
    const std::string_view value) {
    if (!valid_public_locator(value)) {
        fail(LogicalModelProjectionErrorCode::InvalidPublicReference);
    }
    return LogicalPublicLocator{std::string(value), table.ordinal(value)};
}

LogicalCurrentReference make_current_reference(
    const LocatorTable& table,
    const ygo::environment::PublicSafeStateView& safe_state,
    const std::string_view value) {
    LogicalCurrentReference result;
    result.locator = make_public_locator(table, value);
    result.current_entity_ordinal = current_entity_ordinal(safe_state, value);
    return result;
}

LogicalHistoricalReference make_historical_reference(
    const LocatorTable& table,
    const std::string_view value) {
    return LogicalHistoricalReference{make_public_locator(table, value)};
}

bool valid_public_card_reference_kind(
    const ygo::environment::PublicCardReferenceKind kind) noexcept {
    return kind == ygo::environment::PublicCardReferenceKind::VisibleCard ||
           kind == ygo::environment::PublicCardReferenceKind::RedactedSlot;
}

bool valid_public_choice(const ygo::environment::PublicChoice& choice) noexcept {
    switch (choice.kind) {
    case ygo::environment::PublicChoiceKind::YesNo:
    case ygo::environment::PublicChoiceKind::EffectYesNo:
        return choice.value <= 1 && !choice.response_index.has_value();
    case ygo::environment::PublicChoiceKind::EffectChoice:
        return !choice.response_index.has_value();
    case ygo::environment::PublicChoiceKind::OptionValue:
    case ygo::environment::PublicChoiceKind::AnnouncementNumber:
        return choice.response_index.has_value();
    }
    return false;
}

bool valid_action_kind(
    const ygo::environment::EnvironmentActionKind kind) noexcept {
    switch (kind) {
    case ygo::environment::EnvironmentActionKind::IdleCommand:
    case ygo::environment::EnvironmentActionKind::BattleCommand:
    case ygo::environment::EnvironmentActionKind::Chain:
    case ygo::environment::EnvironmentActionKind::Option:
    case ygo::environment::EnvironmentActionKind::CardSelection:
    case ygo::environment::EnvironmentActionKind::Announcement:
    case ygo::environment::EnvironmentActionKind::Place:
    case ygo::environment::EnvironmentActionKind::Position:
    case ygo::environment::EnvironmentActionKind::YesNo:
    case ygo::environment::EnvironmentActionKind::Pick:
    case ygo::environment::EnvironmentActionKind::Finish:
    case ygo::environment::EnvironmentActionKind::Cancel:
    case ygo::environment::EnvironmentActionKind::AssignAmount:
        return true;
    case ygo::environment::EnvironmentActionKind::Unsupported:
        return false;
    }
    return false;
}

std::string_view action_kind_name(
    const ygo::environment::EnvironmentActionKind kind) noexcept {
    switch (kind) {
    case ygo::environment::EnvironmentActionKind::IdleCommand:
        return "idle_command";
    case ygo::environment::EnvironmentActionKind::BattleCommand:
        return "battle_command";
    case ygo::environment::EnvironmentActionKind::Chain:
        return "chain";
    case ygo::environment::EnvironmentActionKind::Option:
        return "option";
    case ygo::environment::EnvironmentActionKind::CardSelection:
        return "card_selection";
    case ygo::environment::EnvironmentActionKind::Announcement:
        return "announcement";
    case ygo::environment::EnvironmentActionKind::Place:
        return "place";
    case ygo::environment::EnvironmentActionKind::Position:
        return "position";
    case ygo::environment::EnvironmentActionKind::YesNo:
        return "yes_no";
    case ygo::environment::EnvironmentActionKind::Pick:
        return "pick";
    case ygo::environment::EnvironmentActionKind::Finish:
        return "finish";
    case ygo::environment::EnvironmentActionKind::Cancel:
        return "cancel";
    case ygo::environment::EnvironmentActionKind::AssignAmount:
        return "assign_amount";
    case ygo::environment::EnvironmentActionKind::Unsupported:
        return {};
    }
    return {};
}

bool valid_continuation_operation(const std::string_view value) noexcept {
    return value.empty() || value == "pick" || value == "amount" ||
           value == "finish" || value == "cancel" || value == "bypass";
}

void validate_candidate(
    const ygo::environment::EnvironmentActionCandidate& candidate) {
    if (!ygo::environment::is_public_action_key(candidate.public_action_key)) {
        fail(LogicalModelProjectionErrorCode::InvalidPublicActionKey);
    }
    if (!valid_action_kind(candidate.action_kind)) {
        fail(LogicalModelProjectionErrorCode::InvalidPublicCandidateDescriptor);
    }
    if (candidate.choice.has_value() && !valid_public_choice(*candidate.choice)) {
        fail(LogicalModelProjectionErrorCode::InvalidPublicCandidateDescriptor);
    }
    for (const auto& reference :
         {candidate.source_reference, candidate.target_reference}) {
        if (!reference.has_value()) {
            continue;
        }
        if (!valid_public_card_reference_kind(reference->kind) ||
            !valid_public_locator(reference->observation_locator)) {
            fail(LogicalModelProjectionErrorCode::InvalidPublicReference);
        }
    }
    if (!valid_continuation_operation(candidate.continuation_operation)) {
        fail(LogicalModelProjectionErrorCode::InvalidPublicCandidateDescriptor);
    }

    ygo::environment::PublicActionKeyInput key;
    key.action_kind = std::string(action_kind_name(candidate.action_kind));
    key.choice = candidate.choice;
    key.source_reference = candidate.source_reference;
    key.target_reference = candidate.target_reference;
    key.phase = candidate.phase;
    key.position = candidate.position;
    key.source_index = candidate.source_index;
    key.amount = candidate.amount;
    key.continuation_operation = candidate.continuation_operation;
    try {
        if (ygo::environment::public_action_key(key) != candidate.public_action_key) {
            fail(LogicalModelProjectionErrorCode::InvalidPublicCandidateDescriptor);
        }
    } catch (const ProjectionFailure&) {
        throw;
    } catch (...) {
        fail(LogicalModelProjectionErrorCode::InvalidPublicCandidateDescriptor);
    }
}

LogicalPublicCardReference make_card_reference(
    const ygo::environment::PublicCardReference& source,
    const LocatorTable& table,
    const ygo::environment::PublicSafeStateView& safe_state) {
    if (!valid_public_card_reference_kind(source.kind) ||
        !valid_public_locator(source.observation_locator)) {
        fail(LogicalModelProjectionErrorCode::InvalidPublicReference);
    }
    LogicalPublicCardReference result;
    result.kind = source.kind;
    result.reference = make_current_reference(
        table, safe_state, source.observation_locator);
    return result;
}

LogicalModelProjectionErrorCode validate_observation_and_decode(
    const ygo::environment::PublicEnvironmentObservation& observation,
    std::optional<ygo::environment::PublicSafeStateView>& safe_state) {
    const auto decoded = ygo::environment::decode_canonical_public_safe_state(
        observation.canonical_safe_state_bytes());
    if (!decoded || !decoded.value.has_value()) {
        return LogicalModelProjectionErrorCode::PublicSafeStateDecodeFailure;
    }
    try {
        if (decoded.value->match_context().perspective_player !=
            observation.perspective_player) {
            return LogicalModelProjectionErrorCode::InvalidPublicObservation;
        }
        if (ygo::environment::canonical_public_safe_state_bytes(*decoded.value) !=
            observation.canonical_safe_state_bytes()) {
            return LogicalModelProjectionErrorCode::PublicSafeStateDecodeFailure;
        }
        safe_state = std::move(*decoded.value);
    } catch (...) {
        return LogicalModelProjectionErrorCode::PublicSafeStateDecodeFailure;
    }

    try {
        (void)ygo::environment::canonical_public_environment_observation_bytes(observation);
    } catch (...) {
        return LogicalModelProjectionErrorCode::InvalidPublicObservation;
    }
    return LogicalModelProjectionErrorCode::InternalFailure;
}

void copy_public_state(
    const ygo::environment::PublicSafeStateView& safe_state,
    const LocatorTable& table,
    LogicalPublicState& output) {
    output.globals = safe_state.globals();
    output.zones = safe_state.zones();
    output.match_context = safe_state.match_context();

    output.entities.reserve(safe_state.entities().size());
    for (std::size_t index = 0; index < safe_state.entities().size(); ++index) {
        const auto& source = safe_state.entities()[index];
        LogicalEntity entity;
        entity.card = source;
        entity.public_locator_ordinal = table.ordinal(source.locator.value);
        if (index > std::numeric_limits<std::uint32_t>::max()) {
            fail(LogicalModelProjectionErrorCode::LocatorTableFailure);
        }
        entity.current_entity_ordinal = static_cast<std::uint32_t>(index);
        output.entities.push_back(std::move(entity));
    }

    output.relationships.reserve(safe_state.relationships().size());
    for (const auto& source : safe_state.relationships()) {
        LogicalRelationship relationship;
        relationship.kind = source.kind;
        relationship.source =
            make_current_reference(table, safe_state, source.source.value);
        relationship.target =
            make_current_reference(table, safe_state, source.target.value);
        output.relationships.push_back(std::move(relationship));
    }

    output.chain.length = safe_state.chain().length;
    output.chain.links.reserve(safe_state.chain().links.size());
    for (const auto& source : safe_state.chain().links) {
        LogicalChainLink link;
        link.index = source.index;
        link.activating_player = source.activating_player;
        if (source.source.has_value()) {
            link.source = make_current_reference(
                table, safe_state, source.source->value);
        }
        link.activation_zone = source.activation_zone;
        link.effect_description = source.effect_description;
        link.targets.reserve(source.targets.size());
        for (const auto& target : source.targets) {
            link.targets.push_back(
                make_current_reference(table, safe_state, target.value));
        }
        output.chain.links.push_back(std::move(link));
    }

    output.visible_events.reserve(safe_state.visible_events().size());
    for (const auto& source : safe_state.visible_events()) {
        LogicalVisibleEvent event;
        event.event_index = source.event_index;
        event.kind = source.kind;
        event.player = source.player;
        if (source.entity.has_value()) {
            event.entity = make_historical_reference(
                table, source.entity->value);
        }
        event.public_passcode = source.public_passcode;
        event.from_zone = source.from_zone;
        event.to_zone = source.to_zone;
        event.count = source.count;
        event.amount = source.amount;
        event.counter_type = source.counter_type;
        event.phase = source.phase;
        event.winner = source.winner;
        event.win_reason = source.win_reason;
        event.effect_description = source.effect_description;
        event.targets.reserve(source.targets.size());
        for (const auto& target : source.targets) {
            event.targets.push_back(
                make_historical_reference(table, target.value));
        }
        output.visible_events.push_back(std::move(event));
    }
}

void copy_context(
    const ygo::environment::PublicEnvironmentObservation& observation,
    const LocatorTable& table,
    LogicalModelInput& output) {
    output.public_observation_context_kind = observation.decision_context.kind;
    output.public_observation_context_player = observation.decision_context.player;
    output.referenced_public_entities.reserve(
        observation.decision_context.referenced_entities.size());

    std::vector<std::string> sorted_references;
    sorted_references.reserve(observation.decision_context.referenced_entities.size());
    for (const auto& source : observation.decision_context.referenced_entities) {
        sorted_references.emplace_back(source.value);
    }
    std::sort(sorted_references.begin(), sorted_references.end(), byte_less);
    if (std::adjacent_find(sorted_references.begin(), sorted_references.end()) !=
        sorted_references.end()) {
        fail(LogicalModelProjectionErrorCode::InvalidPublicReference);
    }
    for (const auto& reference : sorted_references) {
        output.referenced_public_entities.push_back(
            make_public_locator(table, reference));
    }
}

void copy_candidates(
    const std::vector<ygo::environment::EnvironmentActionCandidate>& candidates,
    const LocatorTable& table,
    const ygo::environment::PublicSafeStateView& safe_state,
    LogicalModelInput& output) {
    output.candidate_features.reserve(candidates.size());
    output.candidate_routing.reserve(candidates.size());

    std::vector<std::string> keys;
    keys.reserve(candidates.size());
    for (const auto& source : candidates) {
        validate_candidate(source);
        if (std::find(keys.begin(), keys.end(), source.public_action_key) !=
            keys.end()) {
            fail(LogicalModelProjectionErrorCode::DuplicatePublicActionKey);
        }
        keys.push_back(source.public_action_key);

        LogicalCandidate candidate;
        candidate.action_kind = source.action_kind;
        candidate.choice = source.choice;
        if (source.source_reference.has_value()) {
            candidate.source_reference = make_card_reference(
                *source.source_reference, table, safe_state);
        }
        if (source.target_reference.has_value()) {
            candidate.target_reference = make_card_reference(
                *source.target_reference, table, safe_state);
        }
        candidate.phase = source.phase;
        candidate.position = source.position;
        candidate.source_index = source.source_index;
        candidate.amount = source.amount;
        candidate.continuation_operation = source.continuation_operation;
        candidate.submits_engine_response = source.submits_engine_response;
        output.candidate_features.push_back(std::move(candidate));
        output.candidate_routing.push_back({source.public_action_key});
    }

    if (output.public_observation_context_kind.has_value()) {
        try {
            output.public_candidate_domain_digest =
                ygo::environment::public_candidate_domain_digest(
                    *output.public_observation_context_kind, keys);
        } catch (...) {
            fail(LogicalModelProjectionErrorCode::CandidateDomainDigestFailure);
        }
    }
}

}  // namespace

std::string_view logical_model_projection_error_code_name(
    const LogicalModelProjectionErrorCode code) noexcept {
    switch (code) {
    case LogicalModelProjectionErrorCode::InvalidPublicObservation:
        return "invalid_public_observation";
    case LogicalModelProjectionErrorCode::PublicSafeStateDecodeFailure:
        return "public_safe_state_decode_failure";
    case LogicalModelProjectionErrorCode::EmptyCandidateDomain:
        return "empty_candidate_domain";
    case LogicalModelProjectionErrorCode::InvalidPublicActionKey:
        return "invalid_public_action_key";
    case LogicalModelProjectionErrorCode::DuplicatePublicActionKey:
        return "duplicate_public_action_key";
    case LogicalModelProjectionErrorCode::InvalidPublicCandidateDescriptor:
        return "invalid_public_candidate_descriptor";
    case LogicalModelProjectionErrorCode::InvalidPublicReference:
        return "invalid_public_reference";
    case LogicalModelProjectionErrorCode::CandidateDomainDigestFailure:
        return "candidate_domain_digest_failure";
    case LogicalModelProjectionErrorCode::LocatorTableFailure:
        return "locator_table_failure";
    case LogicalModelProjectionErrorCode::InternalFailure:
        return "internal_failure";
    }
    return "internal_failure";
}

LogicalModelProjectionResult project_logical_model_input_v1(
    const ygo::environment::PublicEnvironmentObservation& observation,
    const std::vector<ygo::environment::EnvironmentActionCandidate>& candidates) noexcept {
    try {
        if (candidates.empty()) {
            fail(LogicalModelProjectionErrorCode::EmptyCandidateDomain);
        }

        std::optional<ygo::environment::PublicSafeStateView> safe_state;
        const auto observation_result =
            validate_observation_and_decode(observation, safe_state);
        if (observation_result != LogicalModelProjectionErrorCode::InternalFailure) {
            fail(observation_result);
        }

        for (const auto& candidate : candidates) {
            validate_candidate(candidate);
        }

        const auto table = make_locator_table(observation, *safe_state, candidates);
        LogicalModelInput output;
        output.schema_id = std::string(kLogicalModelInputSchemaId);
        try {
            output.public_observation_digest =
                ygo::environment::public_observation_digest(observation);
        } catch (...) {
            fail(LogicalModelProjectionErrorCode::InvalidPublicObservation);
        }
        output.perspective_player = observation.perspective_player;
        output.decision_index = observation.decision_index;
        output.public_locator_table.reserve(table.values.size());
        for (std::size_t index = 0; index < table.values.size(); ++index) {
            if (index > std::numeric_limits<std::uint32_t>::max()) {
                fail(LogicalModelProjectionErrorCode::LocatorTableFailure);
            }
            output.public_locator_table.push_back(
                LogicalPublicLocator{table.values[index], static_cast<std::uint32_t>(index)});
        }

        copy_context(observation, table, output);
        copy_public_state(*safe_state, table, output.public_safe_state);
        copy_candidates(candidates, table, *safe_state, output);
        return {std::optional<LogicalModelInput>(std::move(output)), std::nullopt};
    } catch (const ProjectionFailure& failure) {
        const auto code = failure.code();
        return {std::nullopt, LogicalModelProjectionError{code, diagnostic_for(code)}};
    } catch (const std::bad_alloc&) {
        const auto code = LogicalModelProjectionErrorCode::InternalFailure;
        return {std::nullopt, LogicalModelProjectionError{code, diagnostic_for(code)}};
    } catch (...) {
        const auto code = LogicalModelProjectionErrorCode::InternalFailure;
        return {std::nullopt, LogicalModelProjectionError{code, diagnostic_for(code)}};
    }
}

}  // namespace ygo::model
