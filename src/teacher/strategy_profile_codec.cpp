#include "ygo/teacher/strategy_profile_codec.hpp"

#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace ygo::teacher {
namespace {

template <typename T>
trajectory::DecodeResult<T> failure(std::string message) noexcept {
    trajectory::DecodeResult<T> result;
    result.error = trajectory::DecodeError{std::move(message)};
    return result;
}

template <typename T>
trajectory::DecodeResult<T> success(T value) noexcept {
    trajectory::DecodeResult<T> result;
    result.value = std::move(value);
    return result;
}

bool read_count(trajectory::ByteReader& reader, std::uint32_t& count) noexcept {
    return reader.u32be(count) && count <= reader.remaining();
}

bool valid_predicate_scope(const std::uint8_t value) noexcept { return value <= 3; }
bool valid_predicate_atom_kind(const std::uint8_t value) noexcept { return value <= 4; }
bool valid_recovery_source_kind(const std::uint8_t value) noexcept { return value <= 2; }
bool valid_confidence_class(const std::uint8_t value) noexcept { return value <= 3; }
bool valid_score_dimension(const std::uint8_t value) noexcept { return value <= 8; }
bool valid_preference_subject_kind(const std::uint8_t value) noexcept {
    return value <= 5;
}

bool read_predicate_ref(trajectory::ByteReader& reader, PredicateRef& value) noexcept {
    std::uint8_t scope = 0;
    std::uint32_t count = 0;
    if (!reader.u8(scope) || !valid_predicate_scope(scope) ||
        !reader.string(value.predicate_id) || !read_count(reader, count)) {
        return false;
    }
    value.scope = static_cast<PredicateScope>(scope);
    try {
        value.arguments.reserve(count);
    } catch (...) {
        return false;
    }
    for (std::uint32_t index = 0; index < count; ++index) {
        PredicateAtom argument;
        std::uint8_t kind = 0;
        if (!reader.u8(kind) || !valid_predicate_atom_kind(kind)) {
            return false;
        }
        argument.kind = static_cast<PredicateAtomKind>(kind);
        switch (argument.kind) {
        case PredicateAtomKind::Token:
            if (!reader.string(argument.token)) {
                return false;
            }
            break;
        case PredicateAtomKind::U64:
            if (!reader.u64be(argument.u64)) {
                return false;
            }
            break;
        case PredicateAtomKind::I32:
            if (!reader.i32(argument.i32)) {
                return false;
            }
            break;
        case PredicateAtomKind::Passcode:
            if (!reader.u32be(argument.passcode)) {
                return false;
            }
            break;
        case PredicateAtomKind::Boolean:
            if (!reader.boolean(argument.boolean)) {
                return false;
            }
            break;
        }
        value.arguments.push_back(std::move(argument));
    }
    return true;
}

bool read_predicate_vector(trajectory::ByteReader& reader,
                           std::vector<PredicateRef>& values) noexcept {
    std::uint32_t count = 0;
    if (!read_count(reader, count)) {
        return false;
    }
    try {
        values.reserve(count);
    } catch (...) {
        return false;
    }
    for (std::uint32_t index = 0; index < count; ++index) {
        PredicateRef value;
        if (!read_predicate_ref(reader, value)) {
            return false;
        }
        values.push_back(std::move(value));
    }
    return true;
}

bool read_string_vector(trajectory::ByteReader& reader,
                        std::vector<std::string>& values) noexcept {
    std::uint32_t count = 0;
    if (!read_count(reader, count)) {
        return false;
    }
    try {
        values.reserve(count);
    } catch (...) {
        return false;
    }
    for (std::uint32_t index = 0; index < count; ++index) {
        std::string value;
        if (!reader.string(value)) {
            return false;
        }
        values.push_back(std::move(value));
    }
    return true;
}

bool read_card_roles(trajectory::ByteReader& reader,
                     std::vector<CardRoleEntry>& values) noexcept {
    std::uint32_t count = 0;
    if (!read_count(reader, count)) {
        return false;
    }
    try {
        values.reserve(count);
    } catch (...) {
        return false;
    }
    for (std::uint32_t index = 0; index < count; ++index) {
        CardRoleEntry value;
        if (!reader.u32be(value.passcode) || !read_string_vector(reader, value.role_ids)) {
            return false;
        }
        values.push_back(std::move(value));
    }
    return true;
}

bool read_resources(trajectory::ByteReader& reader,
                    std::vector<ResourceDefinition>& values) noexcept {
    std::uint32_t count = 0;
    if (!read_count(reader, count)) {
        return false;
    }
    try {
        values.reserve(count);
    } catch (...) {
        return false;
    }
    for (std::uint32_t index = 0; index < count; ++index) {
        ResourceDefinition value;
        if (!reader.string(value.resource_id) || !reader.string(value.public_fact_id) ||
            !reader.u32be(value.max_value) || !reader.i32(value.preservation_priority) ||
            !reader.i32(value.conversion_priority)) {
            return false;
        }
        values.push_back(std::move(value));
    }
    return true;
}

bool read_candidate_intents(
    trajectory::ByteReader& reader,
    std::vector<CandidateIntentDefinition>& values) noexcept {
    std::uint32_t count = 0;
    if (!read_count(reader, count)) {
        return false;
    }
    try {
        values.reserve(count);
    } catch (...) {
        return false;
    }
    for (std::uint32_t index = 0; index < count; ++index) {
        CandidateIntentDefinition value;
        if (!reader.string(value.intent_id) ||
            !read_predicate_vector(reader, value.public_predicates)) {
            return false;
        }
        values.push_back(std::move(value));
    }
    return true;
}

bool read_goals(trajectory::ByteReader& reader,
                std::vector<GoalDefinition>& values) noexcept {
    std::uint32_t count = 0;
    if (!read_count(reader, count)) {
        return false;
    }
    try {
        values.reserve(count);
    } catch (...) {
        return false;
    }
    for (std::uint32_t index = 0; index < count; ++index) {
        GoalDefinition value;
        if (!reader.string(value.goal_id) || !reader.i32(value.priority) ||
            !read_predicate_vector(reader, value.preconditions) ||
            !read_predicate_vector(reader, value.completion_predicates) ||
            !read_predicate_vector(reader, value.stop_predicates)) {
            return false;
        }
        values.push_back(std::move(value));
    }
    return true;
}

bool read_resource_requirements(
    trajectory::ByteReader& reader,
    std::vector<ResourceRequirement>& values) noexcept {
    std::uint32_t count = 0;
    if (!read_count(reader, count)) {
        return false;
    }
    try {
        values.reserve(count);
    } catch (...) {
        return false;
    }
    for (std::uint32_t index = 0; index < count; ++index) {
        ResourceRequirement value;
        if (!reader.string(value.resource_id) || !reader.u32be(value.minimum)) {
            return false;
        }
        values.push_back(std::move(value));
    }
    return true;
}

bool read_nodes(trajectory::ByteReader& reader,
                std::vector<LineNode>& values) noexcept {
    std::uint32_t count = 0;
    if (!read_count(reader, count)) {
        return false;
    }
    try {
        values.reserve(count);
    } catch (...) {
        return false;
    }
    for (std::uint32_t index = 0; index < count; ++index) {
        LineNode value;
        if (!reader.string(value.node_id) ||
            !read_string_vector(reader, value.candidate_intent_ids) ||
            !read_predicate_vector(reader, value.completion_predicates) ||
            !read_string_vector(reader, value.preserve_resource_ids) ||
            !read_predicate_vector(reader, value.stop_predicates)) {
            return false;
        }
        values.push_back(std::move(value));
    }
    return true;
}

bool read_dependencies(trajectory::ByteReader& reader,
                        std::vector<NodeDependency>& values) noexcept {
    std::uint32_t count = 0;
    if (!read_count(reader, count)) {
        return false;
    }
    try {
        values.reserve(count);
    } catch (...) {
        return false;
    }
    for (std::uint32_t index = 0; index < count; ++index) {
        NodeDependency value;
        if (!reader.string(value.predecessor_node_id) ||
            !reader.string(value.successor_node_id)) {
            return false;
        }
        values.push_back(std::move(value));
    }
    return true;
}

bool read_lines(trajectory::ByteReader& reader,
                std::vector<LineDefinition>& values) noexcept {
    std::uint32_t count = 0;
    if (!read_count(reader, count)) {
        return false;
    }
    try {
        values.reserve(count);
    } catch (...) {
        return false;
    }
    for (std::uint32_t index = 0; index < count; ++index) {
        LineDefinition value;
        if (!reader.string(value.line_id) || !reader.string(value.goal_id) ||
            !read_predicate_vector(reader, value.applicability_predicates) ||
            !read_resource_requirements(reader, value.required_resources) ||
            !read_string_vector(reader, value.optional_resources) ||
            !read_nodes(reader, value.nodes) ||
            !read_dependencies(reader, value.dependencies) ||
            !read_string_vector(reader, value.recovery_edge_ids)) {
            return false;
        }
        values.push_back(std::move(value));
    }
    return true;
}

bool read_optional_string(trajectory::ByteReader& reader,
                          std::optional<std::string>& value) noexcept {
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

bool read_recovery_edges(
    trajectory::ByteReader& reader,
    std::vector<RecoveryEdge>& values) noexcept {
    std::uint32_t count = 0;
    if (!read_count(reader, count)) {
        return false;
    }
    try {
        values.reserve(count);
    } catch (...) {
        return false;
    }
    for (std::uint32_t index = 0; index < count; ++index) {
        RecoveryEdge value;
        std::uint8_t source_kind = 0;
        std::uint8_t confidence = 0;
        if (!reader.string(value.recovery_edge_id) || !reader.u8(source_kind) ||
            !valid_recovery_source_kind(source_kind) ||
            !reader.string(value.source_id) ||
            !read_string_vector(reader, value.invalidation_reason_ids) ||
            !read_predicate_vector(reader, value.preconditions) ||
            !read_string_vector(reader, value.candidate_intent_ids) ||
            !reader.string(value.target_goal_id)) {
            return false;
        }
        value.source_kind = static_cast<RecoverySourceKind>(source_kind);
        if (!read_optional_string(reader, value.target_line_id) ||
            !read_string_vector(reader, value.preserve_resource_ids) ||
            !reader.u8(confidence) || !valid_confidence_class(confidence)) {
            return false;
        }
        value.confidence_cap = static_cast<ConfidenceClass>(confidence);
        values.push_back(std::move(value));
    }
    return true;
}

bool read_interactions(trajectory::ByteReader& reader,
                       std::vector<InteractionRule>& values) noexcept {
    std::uint32_t count = 0;
    if (!read_count(reader, count)) {
        return false;
    }
    try {
        values.reserve(count);
    } catch (...) {
        return false;
    }
    for (std::uint32_t index = 0; index < count; ++index) {
        InteractionRule value;
        if (!reader.string(value.interaction_id) ||
            !read_predicate_vector(reader, value.trigger_predicates) ||
            !read_string_vector(reader, value.candidate_intent_ids) ||
            !reader.i32(value.timing_priority) ||
            !read_string_vector(reader, value.preserve_resource_ids)) {
            return false;
        }
        values.push_back(std::move(value));
    }
    return true;
}

bool read_preferences(trajectory::ByteReader& reader,
                      std::vector<PreferenceEntry>& values) noexcept {
    std::uint32_t count = 0;
    if (!read_count(reader, count)) {
        return false;
    }
    try {
        values.reserve(count);
    } catch (...) {
        return false;
    }
    for (std::uint32_t index = 0; index < count; ++index) {
        PreferenceEntry value;
        std::uint8_t dimension = 0;
        std::uint8_t subject_kind = 0;
        if (!reader.u8(dimension) || !valid_score_dimension(dimension) ||
            !reader.u8(subject_kind) || !valid_preference_subject_kind(subject_kind) ||
            !reader.string(value.subject_id) || !reader.i32(value.value)) {
            return false;
        }
        value.dimension = static_cast<ScoreDimension>(dimension);
        value.subject_kind = static_cast<PreferenceSubjectKind>(subject_kind);
        values.push_back(std::move(value));
    }
    return true;
}

bool read_profile(trajectory::ByteReader& reader,
                  StrategyProfileV1& value) noexcept {
    std::string domain;
    std::string schema;
    if (!reader.string(domain) || domain != kStrategyProfileSchemaId ||
        !reader.string(schema) || schema != kStrategyProfileSchemaId ||
        !reader.string(value.matchup_id) || !reader.string(value.rules_bundle_id) ||
        !reader.string(value.format_id) || !reader.string(value.duel_mode) ||
        !reader.u64be(value.duel_flags) || !reader.u8(value.own_deck_role) ||
        !reader.string(value.own_deck_id) || !reader.string(value.own_deck_sha256) ||
        !reader.u8(value.opponent_deck_role) ||
        !reader.string(value.opponent_deck_id) ||
        !reader.string(value.opponent_deck_sha256) ||
        !read_card_roles(reader, value.card_roles) ||
        !read_resources(reader, value.resources) ||
        !read_candidate_intents(reader, value.candidate_intents) ||
        !read_goals(reader, value.goals) || !read_lines(reader, value.lines) ||
        !read_recovery_edges(reader, value.recovery_edges) ||
        !read_interactions(reader, value.interactions) ||
        !read_preferences(reader, value.preferences) ||
        !reader.string(value.profile_id)) {
        return false;
    }
    return true;
}

}  // namespace

std::vector<std::uint8_t> canonical_strategy_profile_bytes(
    const StrategyProfileV1& value) {
    std::string diagnostic;
    if (!validate_strategy_profile(value, &diagnostic)) {
        throw std::invalid_argument(diagnostic);
    }
    trajectory::ByteWriter writer;
    writer.raw(canonical_strategy_profile_content_bytes(value));
    writer.string(value.profile_id);
    return std::move(writer).take();
}

trajectory::DecodeResult<StrategyProfileV1> decode_strategy_profile(
    const std::vector<std::uint8_t>& bytes) noexcept {
    try {
        trajectory::ByteReader reader(bytes);
        StrategyProfileV1 value;
        if (!read_profile(reader, value) || !reader.at_end()) {
            return failure<StrategyProfileV1>("malformed strategy profile");
        }
        std::string diagnostic;
        if (!validate_strategy_profile(value, &diagnostic)) {
            return failure<StrategyProfileV1>(diagnostic);
        }
        if (canonical_strategy_profile_bytes(value) != bytes) {
            return failure<StrategyProfileV1>("noncanonical strategy profile");
        }
        return success(std::move(value));
    } catch (const std::exception& error) {
        return failure<StrategyProfileV1>(error.what());
    } catch (...) {
        return failure<StrategyProfileV1>("strategy profile decode threw");
    }
}

std::vector<std::uint8_t> canonical_teacher_policy_binding_bytes(
    const TeacherPolicyBindingV1& value) {
    std::string diagnostic;
    if (!validate_teacher_policy_binding(value, &diagnostic)) {
        throw std::invalid_argument(diagnostic);
    }
    trajectory::ByteWriter writer;
    writer.raw(canonical_teacher_policy_binding_content_bytes(value));
    writer.string(value.teacher_policy_binding_id);
    return std::move(writer).take();
}

trajectory::DecodeResult<TeacherPolicyBindingV1> decode_teacher_policy_binding(
    const std::vector<std::uint8_t>& bytes) noexcept {
    try {
        trajectory::ByteReader reader(bytes);
        TeacherPolicyBindingV1 value;
        std::string domain;
        std::string schema;
        std::uint8_t present = 0;
        if (!reader.string(domain) || domain != kTeacherPolicyBindingSchemaId ||
            !reader.string(schema) || schema != kTeacherPolicyBindingSchemaId ||
            !reader.string(value.teacher_core_artifact_identity) ||
            !reader.string(value.strategy_profile_id) ||
            !reader.string(value.score_contract_identity) ||
            !reader.string(value.fallback_contract_identity) ||
            !reader.string(value.tie_break_contract_identity) ||
            !reader.u8(present) || present > 1) {
            return failure<TeacherPolicyBindingV1>("malformed teacher policy binding");
        }
        if (present == 1) {
            std::string diagnostic;
            if (!reader.string(diagnostic)) {
                return failure<TeacherPolicyBindingV1>(
                    "malformed teacher policy diagnostic identity");
            }
            value.diagnostic_contract_identity = std::move(diagnostic);
        }
        if (!reader.string(value.teacher_policy_binding_id) || !reader.at_end()) {
            return failure<TeacherPolicyBindingV1>("malformed teacher policy binding tail");
        }
        std::string diagnostic;
        if (!validate_teacher_policy_binding(value, &diagnostic)) {
            return failure<TeacherPolicyBindingV1>(diagnostic);
        }
        if (canonical_teacher_policy_binding_bytes(value) != bytes) {
            return failure<TeacherPolicyBindingV1>("noncanonical teacher policy binding");
        }
        return success(std::move(value));
    } catch (const std::exception& error) {
        return failure<TeacherPolicyBindingV1>(error.what());
    } catch (...) {
        return failure<TeacherPolicyBindingV1>("teacher policy binding decode threw");
    }
}

}  // namespace ygo::teacher
