#include "ygo/teacher/strategy_profile.hpp"

#include <algorithm>
#include <cstddef>
#include <functional>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#include "ygo/environment/episodic_environment.hpp"
#include "ygo/teacher/predicate_registry.hpp"
#include "ygo/teacher/public_fact_registry.hpp"
#include "ygo/trace/sha256.hpp"
#include "ygo/trajectory/codec.hpp"

namespace ygo::teacher {
namespace {

constexpr std::int32_t kProfileNumericLimit = 1000000;

void require_condition(const bool condition, const char* message) {
    if (!condition) {
        throw std::invalid_argument(message);
    }
}

void require_count(const std::size_t size, const char* field) {
    if (size > std::numeric_limits<std::uint32_t>::max()) {
        throw std::length_error(std::string("profile ") + field + " exceeds u32 count");
    }
}

bool canonical_token(const std::string_view value) noexcept {
    if (value.empty()) {
        return false;
    }
    for (const auto character : value) {
        const auto byte = static_cast<unsigned char>(character);
        if (!((byte >= 'a' && byte <= 'z') || (byte >= '0' && byte <= '9') ||
              byte == '.' || byte == '_' || byte == '-')) {
            return false;
        }
    }
    if (value.front() == '.' || value.back() == '.' ||
        value.find("..") != std::string_view::npos) {
        return false;
    }
    return true;
}

void require_text(const std::string& value, const char* field) {
    require_condition(!value.empty() && trajectory::is_valid_utf8(value),
                      (std::string("profile ") + field + " is invalid").c_str());
}

void require_token(const std::string& value, const char* field) {
    require_condition(canonical_token(value),
                      (std::string("profile ") + field + " is not a canonical token").c_str());
}

void require_digest(const std::string& value, const char* field) {
    require_condition(trajectory::is_lower_hex_digest(value),
                      (std::string("profile ") + field + " is not a SHA-256 digest").c_str());
}

void require_score_value(const std::int32_t value, const char* field) {
    require_condition(value >= -kProfileNumericLimit && value <= kProfileNumericLimit,
                      (std::string("profile ") + field + " is out of range").c_str());
}

template <typename T, typename KeyFunction>
void require_strictly_sorted_by(const std::vector<T>& values, KeyFunction key,
                                const char* field) {
    for (std::size_t index = 1; index < values.size(); ++index) {
        if (!(key(values[index - 1]) < key(values[index]))) {
            throw std::invalid_argument(std::string("profile ") + field +
                                        " is not strictly sorted");
        }
    }
}

void require_sorted_strings(const std::vector<std::string>& values, const char* field) {
    require_count(values.size(), field);
    require_strictly_sorted_by(
        values, [](const auto& value) -> const std::string& { return value; }, field);
    for (const auto& value : values) {
        require_token(value, field);
    }
}

bool contains_string(const std::vector<std::string>& values, const std::string& value) {
    return std::find(values.begin(), values.end(), value) != values.end();
}

bool valid_predicate_scope(const std::uint8_t value) noexcept { return value <= 3; }
bool valid_predicate_atom_kind(const std::uint8_t value) noexcept { return value <= 4; }
bool valid_recovery_source_kind(const std::uint8_t value) noexcept { return value <= 2; }
bool valid_confidence_class(const std::uint8_t value) noexcept { return value <= 3; }
bool valid_score_dimension(const std::uint8_t value) noexcept { return value <= 8; }
bool valid_preference_subject_kind(const std::uint8_t value) noexcept { return value <= 5; }

void validate_predicate_atom(const PredicateAtom& value) {
    const auto kind = static_cast<std::uint8_t>(value.kind);
    require_condition(valid_predicate_atom_kind(kind), "profile predicate atom kind is unknown");
    switch (value.kind) {
    case PredicateAtomKind::Token:
        require_token(value.token, "predicate atom token");
        break;
    case PredicateAtomKind::U64:
        break;
    case PredicateAtomKind::I32:
        break;
    case PredicateAtomKind::Passcode:
        break;
    case PredicateAtomKind::Boolean:
        break;
    }
}

void validate_predicate_ref(const PredicateRef& value) {
    require_condition(valid_predicate_scope(static_cast<std::uint8_t>(value.scope)),
                      "profile predicate scope is unknown");
    require_token(value.predicate_id, "predicate ID");
    require_count(value.arguments.size(), "predicate arguments");
    for (const auto& argument : value.arguments) {
        validate_predicate_atom(argument);
    }
    std::string diagnostic;
    require_condition(TeacherPredicateRegistryV1::canonical().validate_shape(value, &diagnostic),
                      diagnostic.c_str());
}

void write_predicate_atom(trajectory::ByteWriter& writer, const PredicateAtom& value) {
    validate_predicate_atom(value);
    writer.u8(static_cast<std::uint8_t>(value.kind));
    switch (value.kind) {
    case PredicateAtomKind::Token:
        writer.string(value.token);
        break;
    case PredicateAtomKind::U64:
        writer.u64be(value.u64);
        break;
    case PredicateAtomKind::I32:
        writer.i32(value.i32);
        break;
    case PredicateAtomKind::Passcode:
        writer.u32be(value.passcode);
        break;
    case PredicateAtomKind::Boolean:
        writer.boolean(value.boolean);
        break;
    }
}

void write_predicate_ref(trajectory::ByteWriter& writer, const PredicateRef& value) {
    validate_predicate_ref(value);
    writer.u8(static_cast<std::uint8_t>(value.scope));
    writer.string(value.predicate_id);
    require_count(value.arguments.size(), "predicate arguments");
    writer.u32be(static_cast<std::uint32_t>(value.arguments.size()));
    for (const auto& argument : value.arguments) {
        write_predicate_atom(writer, argument);
    }
}

void require_predicate_vector(const std::vector<PredicateRef>& values, const char* field,
                              const StrategyProfileV1* profile = nullptr) {
    require_count(values.size(), field);
    std::vector<std::vector<std::uint8_t>> encoded;
    encoded.reserve(values.size());
    for (const auto& value : values) {
        encoded.push_back(canonical_predicate_ref_bytes(value));
        if (profile != nullptr) {
            std::string diagnostic;
            require_condition(
                TeacherPredicateRegistryV1::canonical().validate_profile_ref(value, *profile,
                                                                               &diagnostic),
                diagnostic.c_str());
        }
    }
    for (std::size_t index = 1; index < encoded.size(); ++index) {
        if (!(encoded[index - 1] < encoded[index])) {
            throw std::invalid_argument(std::string("profile ") + field +
                                        " is not strictly canonical-byte sorted");
        }
    }
}

void write_predicate_vector(trajectory::ByteWriter& writer,
                            const std::vector<PredicateRef>& values, const char* field) {
    require_predicate_vector(values, field);
    writer.u32be(static_cast<std::uint32_t>(values.size()));
    for (const auto& value : values) {
        write_predicate_ref(writer, value);
    }
}

void validate_resource_requirements(const std::vector<ResourceRequirement>& values) {
    require_count(values.size(), "required resources");
    require_strictly_sorted_by(
        values, [](const auto& value) -> const std::string& { return value.resource_id; },
        "required resources");
    for (const auto& value : values) {
        require_token(value.resource_id, "required resource ID");
    }
}

void write_resource_requirements(trajectory::ByteWriter& writer,
                                 const std::vector<ResourceRequirement>& values) {
    validate_resource_requirements(values);
    writer.u32be(static_cast<std::uint32_t>(values.size()));
    for (const auto& value : values) {
        writer.string(value.resource_id);
        writer.u32be(value.minimum);
    }
}

void validate_node_dependencies(const LineDefinition& line) {
    require_count(line.dependencies.size(), "node dependencies");
    require_strictly_sorted_by(
        line.dependencies,
        [](const auto& value) {
            return std::tie(value.predecessor_node_id, value.successor_node_id);
        },
        "node dependencies");
    std::vector<std::uint8_t> colors(line.nodes.size(), 0);
    const auto node_index = [&line](const std::string& id) -> std::size_t {
        for (std::size_t index = 0; index < line.nodes.size(); ++index) {
            if (line.nodes[index].node_id == id) {
                return index;
            }
        }
        throw std::invalid_argument("profile node dependency references an unknown node");
    };
    std::vector<std::vector<std::size_t>> successors(line.nodes.size());
    for (const auto& dependency : line.dependencies) {
        require_token(dependency.predecessor_node_id, "predecessor node ID");
        require_token(dependency.successor_node_id, "successor node ID");
        const auto predecessor = node_index(dependency.predecessor_node_id);
        const auto successor = node_index(dependency.successor_node_id);
        require_condition(predecessor != successor, "profile node dependency is self-referential");
        successors[predecessor].push_back(successor);
    }
    std::function<void(std::size_t)> visit = [&](const std::size_t index) {
        if (colors[index] == 1) {
            throw std::invalid_argument("profile line dependency graph contains a cycle");
        }
        if (colors[index] == 2) {
            return;
        }
        colors[index] = 1;
        for (const auto successor : successors[index]) {
            visit(successor);
        }
        colors[index] = 2;
    };
    for (std::size_t index = 0; index < colors.size(); ++index) {
        visit(index);
    }
}

void validate_profile_content(const StrategyProfileV1& value) {
    require_token(value.matchup_id, "matchup ID");
    require_digest(value.rules_bundle_id, "rules bundle ID");
    require_text(value.format_id, "format ID");
    require_text(value.duel_mode, "duel mode");
    require_condition(value.own_deck_role <= 1 && value.opponent_deck_role <= 1,
                      "profile deck role is unknown");
    require_condition(value.own_deck_role != value.opponent_deck_role,
                      "profile deck roles must be distinct");
    require_token(value.own_deck_id, "own deck ID");
    require_digest(value.own_deck_sha256, "own deck SHA-256");
    require_token(value.opponent_deck_id, "opponent deck ID");
    require_digest(value.opponent_deck_sha256, "opponent deck SHA-256");

    require_count(value.card_roles.size(), "card roles");
    require_strictly_sorted_by(
        value.card_roles, [](const auto& entry) { return entry.passcode; }, "card roles");
    for (const auto& entry : value.card_roles) {
        require_sorted_strings(entry.role_ids, "card role IDs");
    }

    require_count(value.resources.size(), "resources");
    require_strictly_sorted_by(
        value.resources, [](const auto& entry) -> const std::string& { return entry.resource_id; },
        "resources");
    for (const auto& resource : value.resources) {
        require_token(resource.resource_id, "resource ID");
        require_token(resource.public_fact_id, "public fact ID");
        require_score_value(resource.preservation_priority, "resource preservation priority");
        require_score_value(resource.conversion_priority, "resource conversion priority");

        const auto& definitions = PublicFactRegistry::canonical().definitions();
        const auto fact = std::find_if(
            definitions.begin(), definitions.end(), [&](const auto& definition) {
                return definition.fact_id == resource.public_fact_id;
            });
        require_condition(fact != definitions.end(),
                          "profile resource public fact is not registered");
        require_condition(
            fact->source_classification != PublicFactSourceClassification::Blocked,
            "profile resource public fact is blocked");
        require_condition(fact->value_kind == PublicFactValueKind::U64,
                          "profile resource public fact is not U64");
        require_condition(
            std::find(fact->allowed_scopes.begin(), fact->allowed_scopes.end(),
                      PublicFactValidityScope::CurrentReconciliation) !=
                fact->allowed_scopes.end(),
            "profile resource public fact has no current scope");
        if (fact->u64_maximum.has_value()) {
            require_condition(static_cast<std::uint64_t>(resource.max_value) <=
                                  *fact->u64_maximum,
                              "profile resource maximum exceeds public fact bounds");
        }
    }

    require_count(value.candidate_intents.size(), "candidate intents");
    require_strictly_sorted_by(
        value.candidate_intents,
        [](const auto& entry) -> const std::string& { return entry.intent_id; },
        "candidate intents");
    for (const auto& intent : value.candidate_intents) {
        require_token(intent.intent_id, "candidate intent ID");
        require_predicate_vector(intent.public_predicates, "candidate intent predicates", &value);
    }

    require_count(value.goals.size(), "goals");
    require_strictly_sorted_by(
        value.goals, [](const auto& entry) -> const std::string& { return entry.goal_id; },
        "goals");
    for (const auto& goal : value.goals) {
        require_token(goal.goal_id, "goal ID");
        require_score_value(goal.priority, "goal priority");
        require_predicate_vector(goal.preconditions, "goal preconditions", &value);
        require_predicate_vector(goal.completion_predicates, "goal completion predicates", &value);
        require_predicate_vector(goal.stop_predicates, "goal stop predicates", &value);
    }

    require_count(value.lines.size(), "lines");
    require_strictly_sorted_by(
        value.lines, [](const auto& entry) -> const std::string& { return entry.line_id; },
        "lines");
    for (const auto& line : value.lines) {
        require_token(line.line_id, "line ID");
        require_token(line.goal_id, "line goal ID");
        require_predicate_vector(line.applicability_predicates, "line applicability predicates",
                                 &value);
        validate_resource_requirements(line.required_resources);
        require_sorted_strings(line.optional_resources, "line optional resources");
        require_count(line.nodes.size(), "line nodes");
        require_strictly_sorted_by(
            line.nodes, [](const auto& entry) -> const std::string& { return entry.node_id; },
            "line nodes");
        for (const auto& node : line.nodes) {
            require_token(node.node_id, "line node ID");
            require_sorted_strings(node.candidate_intent_ids, "node candidate intent IDs");
            require_predicate_vector(node.completion_predicates, "node completion predicates",
                                     &value);
            require_sorted_strings(node.preserve_resource_ids, "node preserved resource IDs");
            require_predicate_vector(node.stop_predicates, "node stop predicates", &value);
        }
        validate_node_dependencies(line);
        require_sorted_strings(line.recovery_edge_ids, "line recovery edge IDs");
    }

    require_count(value.recovery_edges.size(), "recovery edges");
    require_strictly_sorted_by(
        value.recovery_edges,
        [](const auto& entry) -> const std::string& { return entry.recovery_edge_id; },
        "recovery edges");
    for (const auto& edge : value.recovery_edges) {
        require_token(edge.recovery_edge_id, "recovery edge ID");
        require_condition(valid_recovery_source_kind(static_cast<std::uint8_t>(edge.source_kind)),
                          "recovery source kind is unknown");
        require_token(edge.source_id, "recovery source ID");
        require_sorted_strings(edge.invalidation_reason_ids, "invalidation reason IDs");
        for (const auto& reason : edge.invalidation_reason_ids) {
            require_condition(is_registered_invalidation_reason(reason),
                              "profile invalidation reason is not registered in v1");
        }
        require_predicate_vector(edge.preconditions, "recovery preconditions", &value);
        require_sorted_strings(edge.candidate_intent_ids, "recovery candidate intent IDs");
        require_token(edge.target_goal_id, "recovery target goal ID");
        if (edge.target_line_id.has_value()) {
            require_token(*edge.target_line_id, "recovery target line ID");
        }
        require_sorted_strings(edge.preserve_resource_ids, "recovery preserved resource IDs");
        require_condition(valid_confidence_class(static_cast<std::uint8_t>(edge.confidence_cap)),
                          "recovery confidence class is unknown");
    }

    require_count(value.interactions.size(), "interactions");
    require_strictly_sorted_by(
        value.interactions,
        [](const auto& entry) -> const std::string& { return entry.interaction_id; },
        "interactions");
    for (const auto& interaction : value.interactions) {
        require_token(interaction.interaction_id, "interaction ID");
        require_predicate_vector(interaction.trigger_predicates, "interaction predicates", &value);
        require_sorted_strings(interaction.candidate_intent_ids,
                               "interaction candidate intent IDs");
        require_score_value(interaction.timing_priority, "interaction timing priority");
        require_sorted_strings(interaction.preserve_resource_ids,
                               "interaction preserved resource IDs");
    }

    require_count(value.preferences.size(), "preferences");
    require_strictly_sorted_by(
        value.preferences,
        [](const auto& entry) {
            return std::make_tuple(static_cast<std::uint8_t>(entry.dimension),
                                   static_cast<std::uint8_t>(entry.subject_kind),
                                   entry.subject_id);
        },
        "preferences");
    for (const auto& preference : value.preferences) {
        require_condition(valid_score_dimension(static_cast<std::uint8_t>(preference.dimension)),
                          "preference score dimension is unknown");
        require_condition(valid_preference_subject_kind(
                              static_cast<std::uint8_t>(preference.subject_kind)),
                          "preference subject kind is unknown");
        require_token(preference.subject_id, "preference subject ID");
        require_score_value(preference.value, "preference value");
    }

    std::vector<std::string> resource_ids;
    resource_ids.reserve(value.resources.size());
    for (const auto& resource : value.resources) {
        resource_ids.push_back(resource.resource_id);
    }
    std::vector<std::string> intent_ids;
    intent_ids.reserve(value.candidate_intents.size());
    for (const auto& intent : value.candidate_intents) {
        intent_ids.push_back(intent.intent_id);
    }
    std::vector<std::string> goal_ids;
    goal_ids.reserve(value.goals.size());
    for (const auto& goal : value.goals) {
        goal_ids.push_back(goal.goal_id);
    }
    std::vector<std::string> line_ids;
    line_ids.reserve(value.lines.size());
    for (const auto& line : value.lines) {
        line_ids.push_back(line.line_id);
    }
    std::vector<std::string> recovery_ids;
    recovery_ids.reserve(value.recovery_edges.size());
    for (const auto& edge : value.recovery_edges) {
        recovery_ids.push_back(edge.recovery_edge_id);
    }

    const auto resource = [&resource_ids, &value](const std::string& id) -> const ResourceDefinition& {
        const auto it = std::find(resource_ids.begin(), resource_ids.end(), id);
        if (it == resource_ids.end()) {
            throw std::invalid_argument("profile references an unknown resource");
        }
        return value.resources[static_cast<std::size_t>(it - resource_ids.begin())];
    };
    const auto require_resource = [&resource](const std::string& id) { (void)resource(id); };
    const auto require_intent = [&intent_ids](const std::string& id) {
        require_condition(contains_string(intent_ids, id),
                          "profile references an unknown candidate intent");
    };
    const auto require_goal = [&goal_ids](const std::string& id) {
        require_condition(contains_string(goal_ids, id), "profile references an unknown goal");
    };
    const auto require_line = [&line_ids](const std::string& id) {
        require_condition(contains_string(line_ids, id), "profile references an unknown line");
    };
    const auto require_recovery = [&recovery_ids](const std::string& id) {
        require_condition(contains_string(recovery_ids, id),
                          "profile references an unknown recovery edge");
    };
    const auto node_occurrences = [&value](const std::string& id) {
        std::size_t count = 0;
        for (const auto& line : value.lines) {
            for (const auto& node : line.nodes) {
                if (node.node_id == id) {
                    ++count;
                }
            }
        }
        return count;
    };

    for (const auto& line : value.lines) {
        require_goal(line.goal_id);
        for (const auto& required : line.required_resources) {
            const auto& definition = resource(required.resource_id);
            require_condition(required.minimum <= definition.max_value,
                              "profile resource requirement exceeds its maximum");
        }
        for (const auto& optional : line.optional_resources) {
            require_resource(optional);
        }
        for (const auto& node : line.nodes) {
            for (const auto& intent : node.candidate_intent_ids) {
                require_intent(intent);
            }
            for (const auto& preserved : node.preserve_resource_ids) {
                require_resource(preserved);
            }
        }
        for (const auto& recovery : line.recovery_edge_ids) {
            require_recovery(recovery);
        }
    }
    for (const auto& edge : value.recovery_edges) {
        switch (edge.source_kind) {
        case RecoverySourceKind::Goal:
            require_goal(edge.source_id);
            break;
        case RecoverySourceKind::Line:
            require_line(edge.source_id);
            break;
        case RecoverySourceKind::Node:
            require_condition(node_occurrences(edge.source_id) == 1,
                              "profile node recovery source is ambiguous or missing");
            break;
        }
        for (const auto& intent : edge.candidate_intent_ids) {
            require_intent(intent);
        }
        require_goal(edge.target_goal_id);
        if (edge.target_line_id.has_value()) {
            require_line(*edge.target_line_id);
            const auto line_it = std::find_if(
                value.lines.begin(), value.lines.end(),
                [&edge](const auto& line) {
                    return line.line_id == *edge.target_line_id;
                });
            require_condition(line_it != value.lines.end() &&
                                  line_it->goal_id == edge.target_goal_id,
                              "profile recovery target line does not belong to target goal");
        }
        for (const auto& preserved : edge.preserve_resource_ids) {
            require_resource(preserved);
        }
    }
    for (const auto& interaction : value.interactions) {
        for (const auto& intent : interaction.candidate_intent_ids) {
            require_intent(intent);
        }
        for (const auto& preserved : interaction.preserve_resource_ids) {
            require_resource(preserved);
        }
    }
    for (const auto& preference : value.preferences) {
        switch (preference.subject_kind) {
        case PreferenceSubjectKind::Global:
            break;
        case PreferenceSubjectKind::Goal:
            require_goal(preference.subject_id);
            break;
        case PreferenceSubjectKind::Line:
            require_line(preference.subject_id);
            break;
        case PreferenceSubjectKind::Intent:
            require_intent(preference.subject_id);
            break;
        case PreferenceSubjectKind::Resource:
            require_resource(preference.subject_id);
            break;
        case PreferenceSubjectKind::Interaction:
            require_condition(
                std::find_if(value.interactions.begin(), value.interactions.end(),
                             [&preference](const auto& interaction) {
                                 return interaction.interaction_id == preference.subject_id;
                             }) != value.interactions.end(),
                "profile references an unknown interaction");
            break;
        }
    }
}

void write_string_vector(trajectory::ByteWriter& writer,
                         const std::vector<std::string>& values, const char* field) {
    require_sorted_strings(values, field);
    writer.u32be(static_cast<std::uint32_t>(values.size()));
    for (const auto& value : values) {
        writer.string(value);
    }
}

void write_card_roles(trajectory::ByteWriter& writer,
                      const std::vector<CardRoleEntry>& values) {
    writer.u32be(static_cast<std::uint32_t>(values.size()));
    for (const auto& value : values) {
        writer.u32be(value.passcode);
        write_string_vector(writer, value.role_ids, "card role IDs");
    }
}

void write_resources(trajectory::ByteWriter& writer,
                     const std::vector<ResourceDefinition>& values) {
    writer.u32be(static_cast<std::uint32_t>(values.size()));
    for (const auto& value : values) {
        writer.string(value.resource_id);
        writer.string(value.public_fact_id);
        writer.u32be(value.max_value);
        writer.i32(value.preservation_priority);
        writer.i32(value.conversion_priority);
    }
}

void write_candidate_intents(trajectory::ByteWriter& writer,
                             const std::vector<CandidateIntentDefinition>& values) {
    writer.u32be(static_cast<std::uint32_t>(values.size()));
    for (const auto& value : values) {
        writer.string(value.intent_id);
        write_predicate_vector(writer, value.public_predicates, "candidate intent predicates");
    }
}

void write_goals(trajectory::ByteWriter& writer, const std::vector<GoalDefinition>& values) {
    writer.u32be(static_cast<std::uint32_t>(values.size()));
    for (const auto& value : values) {
        writer.string(value.goal_id);
        writer.i32(value.priority);
        write_predicate_vector(writer, value.preconditions, "goal preconditions");
        write_predicate_vector(writer, value.completion_predicates,
                               "goal completion predicates");
        write_predicate_vector(writer, value.stop_predicates, "goal stop predicates");
    }
}

void write_nodes(trajectory::ByteWriter& writer, const std::vector<LineNode>& values) {
    writer.u32be(static_cast<std::uint32_t>(values.size()));
    for (const auto& value : values) {
        writer.string(value.node_id);
        write_string_vector(writer, value.candidate_intent_ids, "node candidate intent IDs");
        write_predicate_vector(writer, value.completion_predicates,
                               "node completion predicates");
        write_string_vector(writer, value.preserve_resource_ids,
                           "node preserved resource IDs");
        write_predicate_vector(writer, value.stop_predicates, "node stop predicates");
    }
}

void write_dependencies(trajectory::ByteWriter& writer,
                        const std::vector<NodeDependency>& values) {
    writer.u32be(static_cast<std::uint32_t>(values.size()));
    for (const auto& value : values) {
        writer.string(value.predecessor_node_id);
        writer.string(value.successor_node_id);
    }
}

void write_lines(trajectory::ByteWriter& writer,
                 const std::vector<LineDefinition>& values) {
    writer.u32be(static_cast<std::uint32_t>(values.size()));
    for (const auto& value : values) {
        writer.string(value.line_id);
        writer.string(value.goal_id);
        write_predicate_vector(writer, value.applicability_predicates,
                               "line applicability predicates");
        write_resource_requirements(writer, value.required_resources);
        write_string_vector(writer, value.optional_resources, "line optional resources");
        write_nodes(writer, value.nodes);
        write_dependencies(writer, value.dependencies);
        write_string_vector(writer, value.recovery_edge_ids, "line recovery edge IDs");
    }
}

void write_recovery_edges(trajectory::ByteWriter& writer,
                          const std::vector<RecoveryEdge>& values) {
    writer.u32be(static_cast<std::uint32_t>(values.size()));
    for (const auto& value : values) {
        writer.string(value.recovery_edge_id);
        writer.u8(static_cast<std::uint8_t>(value.source_kind));
        writer.string(value.source_id);
        write_string_vector(writer, value.invalidation_reason_ids, "invalidation reason IDs");
        write_predicate_vector(writer, value.preconditions, "recovery preconditions");
        write_string_vector(writer, value.candidate_intent_ids,
                           "recovery candidate intent IDs");
        writer.string(value.target_goal_id);
        writer.u8(value.target_line_id.has_value() ? 1 : 0);
        if (value.target_line_id.has_value()) {
            writer.string(*value.target_line_id);
        }
        write_string_vector(writer, value.preserve_resource_ids,
                           "recovery preserved resource IDs");
        writer.u8(static_cast<std::uint8_t>(value.confidence_cap));
    }
}

void write_interactions(trajectory::ByteWriter& writer,
                        const std::vector<InteractionRule>& values) {
    writer.u32be(static_cast<std::uint32_t>(values.size()));
    for (const auto& value : values) {
        writer.string(value.interaction_id);
        write_predicate_vector(writer, value.trigger_predicates, "interaction predicates");
        write_string_vector(writer, value.candidate_intent_ids,
                           "interaction candidate intent IDs");
        writer.i32(value.timing_priority);
        write_string_vector(writer, value.preserve_resource_ids,
                           "interaction preserved resource IDs");
    }
}

void write_preferences(trajectory::ByteWriter& writer,
                       const std::vector<PreferenceEntry>& values) {
    writer.u32be(static_cast<std::uint32_t>(values.size()));
    for (const auto& value : values) {
        writer.u8(static_cast<std::uint8_t>(value.dimension));
        writer.u8(static_cast<std::uint8_t>(value.subject_kind));
        writer.string(value.subject_id);
        writer.i32(value.value);
    }
}

void set_diagnostic(std::string* diagnostic, const std::string& message) {
    if (diagnostic != nullptr) {
        *diagnostic = message;
    }
}

}  // namespace

bool is_registered_invalidation_reason(const std::string_view value) noexcept {
    return std::find(kStrategyProfileInvalidationReasonIds.begin(),
                     kStrategyProfileInvalidationReasonIds.end(), value) !=
           kStrategyProfileInvalidationReasonIds.end();
}

std::vector<std::uint8_t> canonical_predicate_ref_bytes(const PredicateRef& value) {
    trajectory::ByteWriter writer;
    write_predicate_ref(writer, value);
    return std::move(writer).take();
}

std::vector<std::uint8_t> canonical_strategy_profile_content_bytes(
    const StrategyProfileV1& value) {
    validate_profile_content(value);
    trajectory::ByteWriter writer;
    writer.string(kStrategyProfileSchemaId);
    writer.string(kStrategyProfileSchemaId);
    writer.string(value.matchup_id);
    writer.string(value.rules_bundle_id);
    writer.string(value.format_id);
    writer.string(value.duel_mode);
    writer.u64be(value.duel_flags);
    writer.u8(value.own_deck_role);
    writer.string(value.own_deck_id);
    writer.string(value.own_deck_sha256);
    writer.u8(value.opponent_deck_role);
    writer.string(value.opponent_deck_id);
    writer.string(value.opponent_deck_sha256);
    write_card_roles(writer, value.card_roles);
    write_resources(writer, value.resources);
    write_candidate_intents(writer, value.candidate_intents);
    write_goals(writer, value.goals);
    write_lines(writer, value.lines);
    write_recovery_edges(writer, value.recovery_edges);
    write_interactions(writer, value.interactions);
    write_preferences(writer, value.preferences);
    return std::move(writer).take();
}

std::string strategy_profile_id(const StrategyProfileV1& value) {
    return std::string(kStrategyProfileIdentityPrefix) +
           trace::sha256_bytes(canonical_strategy_profile_content_bytes(value));
}

bool validate_strategy_profile(const StrategyProfileV1& value,
                               std::string* diagnostic) noexcept {
    try {
        validate_profile_content(value);
        if (!trajectory::is_canonical_identity(value.profile_id,
                                               kStrategyProfileIdentityPrefix)) {
            set_diagnostic(diagnostic, "profile ID is not canonical");
            return false;
        }
        if (value.profile_id != strategy_profile_id(value)) {
            set_diagnostic(diagnostic, "profile ID does not match canonical content");
            return false;
        }
        return true;
    } catch (const std::exception& error) {
        set_diagnostic(diagnostic, error.what());
        return false;
    } catch (...) {
        set_diagnostic(diagnostic, "profile validation threw");
        return false;
    }
}

bool validate_strategy_profile_binding(
    const StrategyProfileV1& value,
    const environment::CertifiedEnvironmentConfig& config,
    std::string* diagnostic) noexcept {
    try {
        if (!validate_strategy_profile(value, diagnostic)) {
            return false;
        }
        if (value.matchup_id != kCertifiedMatchupId) {
            set_diagnostic(diagnostic, "profile matchup does not match the certified matchup");
            return false;
        }
        const auto canonical = environment::CertifiedEnvironmentConfig::canonical();
        if (config.rules_bundle_id != canonical.rules_bundle_id ||
            config.format_id != canonical.format_id || config.duel_mode != canonical.duel_mode ||
            config.duel_flags != canonical.duel_flags || config.locked_decks.size() != 2 ||
            canonical.locked_decks.size() != 2) {
            set_diagnostic(diagnostic, "environment is not the certified profile environment");
            return false;
        }
        for (std::size_t index = 0; index < canonical.locked_decks.size(); ++index) {
            if (config.locked_decks[index].id != canonical.locked_decks[index].id ||
                config.locked_decks[index].sha256 != canonical.locked_decks[index].sha256) {
                set_diagnostic(diagnostic, "environment locked-deck identity is not certified");
                return false;
            }
        }
        const auto own_role = static_cast<std::size_t>(value.own_deck_role);
        const auto opponent_role = static_cast<std::size_t>(value.opponent_deck_role);
        if (value.rules_bundle_id != config.rules_bundle_id ||
            value.format_id != config.format_id || value.duel_mode != config.duel_mode ||
            value.duel_flags != config.duel_flags ||
            value.own_deck_id != config.locked_decks[own_role].id ||
            value.own_deck_sha256 != config.locked_decks[own_role].sha256 ||
            value.opponent_deck_id != config.locked_decks[opponent_role].id ||
            value.opponent_deck_sha256 != config.locked_decks[opponent_role].sha256) {
            set_diagnostic(diagnostic, "profile deck or environment binding does not match");
            return false;
        }
        return true;
    } catch (const std::exception& error) {
        set_diagnostic(diagnostic, error.what());
        return false;
    } catch (...) {
        set_diagnostic(diagnostic, "profile environment binding validation threw");
        return false;
    }
}

}  // namespace ygo::teacher
