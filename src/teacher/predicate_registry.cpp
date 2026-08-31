#include "ygo/teacher/predicate_registry.hpp"

#include <algorithm>
#include <cstddef>
#include <limits>
#include <string>
#include <iterator>
#include <utility>

#include "ygo/environment/episodic_environment.hpp"
#include "ygo/environment/public_safe_state.hpp"
#include "ygo/trajectory/codec.hpp"

namespace ygo::teacher {
namespace {

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
    return value.front() != '.' && value.back() != '.' &&
           value.find("..") == std::string_view::npos;
}

bool valid_predicate_scope(const PredicateScope value) noexcept {
    return static_cast<std::uint8_t>(value) <=
           static_cast<std::uint8_t>(PredicateScope::ProfileStatic);
}

bool valid_atom_kind(const PredicateAtomKind value) noexcept {
    return static_cast<std::uint8_t>(value) <=
           static_cast<std::uint8_t>(PredicateAtomKind::Boolean);
}

bool canonical_atom(const PredicateAtom& value) noexcept {
    if (!valid_atom_kind(value.kind)) {
        return false;
    }
    switch (value.kind) {
    case PredicateAtomKind::Token:
        return canonical_token(value.token) && value.u64 == 0 && value.i32 == 0 &&
               value.passcode == 0 && !value.boolean;
    case PredicateAtomKind::U64:
        return value.token.empty() && value.i32 == 0 && value.passcode == 0 &&
               !value.boolean;
    case PredicateAtomKind::I32:
        return value.token.empty() && value.u64 == 0 && value.passcode == 0 &&
               !value.boolean;
    case PredicateAtomKind::Passcode:
        return value.token.empty() && value.u64 == 0 && value.i32 == 0 &&
               !value.boolean;
    case PredicateAtomKind::Boolean:
        return value.token.empty() && value.u64 == 0 && value.i32 == 0 &&
               value.passcode == 0;
    }
    return false;
}

PredicateDefinitionV1 definition(std::string id, const PredicateScope scope,
                                 std::vector<PredicateAtomKind> kinds = {}) {
    return PredicateDefinitionV1{std::move(id), scope, std::move(kinds)};
}

const std::vector<PredicateDefinitionV1>& predicate_definitions() {
    // The vector is written in canonical bytewise predicate-id order. It is
    // immutable after first construction and is the sole v1 registry.
    static const std::vector<PredicateDefinitionV1> values = {
        definition("candidate.action_kind_equals", PredicateScope::Candidate,
                   {PredicateAtomKind::Token}),
        definition("candidate.choice_present", PredicateScope::Candidate),
        definition("candidate.choice_value_equals", PredicateScope::Candidate,
                   {PredicateAtomKind::U64}),
        definition("candidate.continuation_present", PredicateScope::Candidate),
        definition("candidate.phase_equals", PredicateScope::Candidate,
                   {PredicateAtomKind::U64}),
        definition("candidate.position_equals", PredicateScope::Candidate,
                   {PredicateAtomKind::U64}),
        definition("candidate.source_index_equals", PredicateScope::Candidate,
                   {PredicateAtomKind::U64}),
        definition("candidate.source_role_contains", PredicateScope::Candidate,
                   {PredicateAtomKind::Token}),
        definition("candidate.source_visibility_equals", PredicateScope::Candidate,
                   {PredicateAtomKind::Token}),
        definition("candidate.submits_engine_response", PredicateScope::Candidate),
        definition("candidate.target_role_contains", PredicateScope::Candidate,
                   {PredicateAtomKind::Token}),
        definition("candidate.target_visibility_equals", PredicateScope::Candidate,
                   {PredicateAtomKind::Token}),
        definition("observation.fact_boolean_equals", PredicateScope::Observation,
                   {PredicateAtomKind::Token, PredicateAtomKind::Boolean}),
        definition("observation.fact_i32_equals", PredicateScope::Observation,
                   {PredicateAtomKind::Token, PredicateAtomKind::I32}),
        definition("observation.fact_token_equals", PredicateScope::Observation,
                   {PredicateAtomKind::Token, PredicateAtomKind::Token}),
        definition("observation.fact_u64_at_least", PredicateScope::Observation,
                   {PredicateAtomKind::Token, PredicateAtomKind::U64}),
        definition("observation.fact_u64_at_most", PredicateScope::Observation,
                   {PredicateAtomKind::Token, PredicateAtomKind::U64}),
        definition("observation.fact_u64_equals", PredicateScope::Observation,
                   {PredicateAtomKind::Token, PredicateAtomKind::U64}),
        definition("profile.card_role_exists", PredicateScope::ProfileStatic,
                   {PredicateAtomKind::Passcode}),
        definition("profile.goal_exists", PredicateScope::ProfileStatic,
                   {PredicateAtomKind::Token}),
        definition("profile.intent_exists", PredicateScope::ProfileStatic,
                   {PredicateAtomKind::Token}),
        definition("profile.line_exists", PredicateScope::ProfileStatic,
                   {PredicateAtomKind::Token}),
        definition("profile.resource_exists", PredicateScope::ProfileStatic,
                   {PredicateAtomKind::Token}),
    };
    return values;
}

const PredicateDefinitionV1* find_definition(const std::string_view predicate_id) noexcept {
    const auto& values = predicate_definitions();
    const auto it = std::lower_bound(
        values.begin(), values.end(), predicate_id,
        [](const PredicateDefinitionV1& left, const std::string_view right) {
            return left.predicate_id < right;
        });
    return it != values.end() && it->predicate_id == predicate_id ? &*it : nullptr;
}

const PublicFactDefinition* find_fact_definition(const std::string_view fact_id) noexcept {
    const auto& values = PublicFactRegistry::canonical().definitions();
    const auto it = std::lower_bound(
        values.begin(), values.end(), fact_id,
        [](const PublicFactDefinition& left, const std::string_view right) {
            return left.fact_id < right;
        });
    return it != values.end() && it->fact_id == fact_id ? &*it : nullptr;
}

bool within_u64_bounds(const PublicFactDefinition& definition,
                       const std::uint64_t value) noexcept {
    return (!definition.u64_minimum.has_value() || value >= *definition.u64_minimum) &&
           (!definition.u64_maximum.has_value() || value <= *definition.u64_maximum);
}

bool within_i32_bounds(const PublicFactDefinition& definition,
                       const std::int32_t value) noexcept {
    return (!definition.i32_minimum.has_value() || value >= *definition.i32_minimum) &&
           (!definition.i32_maximum.has_value() || value <= *definition.i32_maximum);
}

bool token_in_domain(const PublicFactDefinition& definition,
                     const std::string_view value) noexcept {
    return definition.token_domain.empty() ||
           std::binary_search(definition.token_domain.begin(), definition.token_domain.end(),
                              value);
}

bool fact_argument_is_valid(const PredicateRef& value) noexcept {
    if (value.scope != PredicateScope::Observation || value.arguments.size() != 2 ||
        value.arguments[0].kind != PredicateAtomKind::Token) {
        return false;
    }
    const auto* definition = find_fact_definition(value.arguments[0].token);
    if (definition == nullptr ||
        definition->source_classification == PublicFactSourceClassification::Blocked) {
        return false;
    }
    const auto& expected = value.arguments[1];
    if (value.predicate_id == "observation.fact_u64_equals" ||
        value.predicate_id == "observation.fact_u64_at_least" ||
        value.predicate_id == "observation.fact_u64_at_most") {
        return definition->value_kind == PublicFactValueKind::U64 &&
               expected.kind == PredicateAtomKind::U64 &&
               within_u64_bounds(*definition, expected.u64);
    }
    if (value.predicate_id == "observation.fact_i32_equals") {
        return definition->value_kind == PublicFactValueKind::I32 &&
               expected.kind == PredicateAtomKind::I32 &&
               within_i32_bounds(*definition, expected.i32);
    }
    if (value.predicate_id == "observation.fact_boolean_equals") {
        return definition->value_kind == PublicFactValueKind::Boolean &&
               expected.kind == PredicateAtomKind::Boolean;
    }
    if (value.predicate_id == "observation.fact_token_equals") {
        return definition->value_kind == PublicFactValueKind::Token &&
               expected.kind == PredicateAtomKind::Token &&
               token_in_domain(*definition, expected.token);
    }
    return false;
}

bool valid_snapshot(const PublicFactSnapshot& snapshot) noexcept {
    try {
        const auto& registry = PublicFactRegistry::canonical();
        for (std::size_t index = 0; index < snapshot.values.size(); ++index) {
            if (!registry.validate(snapshot.values[index])) {
                return false;
            }
            if (index > 0 &&
                (snapshot.values[index - 1].fact_id == snapshot.values[index].fact_id ||
                 !(canonical_public_fact_value_bytes(snapshot.values[index - 1]) <
                   canonical_public_fact_value_bytes(snapshot.values[index])))) {
                return false;
            }
        }
        return true;
    } catch (...) {
        return false;
    }
}

bool valid_action_kind(const environment::EnvironmentActionKind value) noexcept {
    return static_cast<std::uint8_t>(value) <=
           static_cast<std::uint8_t>(environment::EnvironmentActionKind::Unsupported);
}

bool valid_choice_kind(const environment::PublicChoiceKind value) noexcept {
    const auto code = static_cast<std::uint8_t>(value);
    return code >= static_cast<std::uint8_t>(environment::PublicChoiceKind::YesNo) &&
           code <= static_cast<std::uint8_t>(environment::PublicChoiceKind::AnnouncementNumber);
}

bool valid_choice(const environment::PublicChoice& choice) noexcept {
    switch (choice.kind) {
    case environment::PublicChoiceKind::YesNo:
    case environment::PublicChoiceKind::EffectYesNo:
        return choice.value <= 1 && !choice.response_index.has_value();
    case environment::PublicChoiceKind::EffectChoice:
        return choice.value <= std::numeric_limits<std::uint32_t>::max() &&
               !choice.response_index.has_value();
    case environment::PublicChoiceKind::OptionValue:
    case environment::PublicChoiceKind::AnnouncementNumber:
        return choice.response_index.has_value();
    }
    return false;
}

bool valid_reference_kind(const environment::PublicCardReferenceKind value) noexcept {
    return static_cast<std::uint8_t>(value) <=
           static_cast<std::uint8_t>(environment::PublicCardReferenceKind::RedactedSlot);
}

bool valid_observation_locator(const std::string_view value) noexcept {
    return !value.empty() && std::all_of(value.begin(), value.end(), [](const char character) {
        const auto byte = static_cast<unsigned char>(character);
        return byte >= 0x20 && byte != 0x7f;
    });
}

bool valid_candidate_metadata(const environment::EnvironmentActionCandidate& candidate) noexcept {
    if (!valid_action_kind(candidate.action_kind) ||
        !environment::is_public_action_key(candidate.public_action_key)) {
        return false;
    }
    if (candidate.choice.has_value() &&
        (!valid_choice_kind(candidate.choice->kind) || !valid_choice(*candidate.choice))) {
        return false;
    }
    for (const auto* reference : {&candidate.source_reference, &candidate.target_reference}) {
        if (reference->has_value() &&
            (!valid_reference_kind(reference->value().kind) ||
             !valid_observation_locator(reference->value().observation_locator) ||
             !trajectory::is_valid_utf8(reference->value().observation_locator))) {
            return false;
        }
    }
    if (!candidate.continuation_operation.empty() &&
        !canonical_token(candidate.continuation_operation)) {
        return false;
    }
    return true;
}

bool role_contains(const std::uint32_t passcode, const std::string_view role_id,
                   const StrategyProfileV1& profile) noexcept {
    const auto it = std::lower_bound(
        profile.card_roles.begin(), profile.card_roles.end(), passcode,
        [](const CardRoleEntry& entry, const std::uint32_t value) {
            return entry.passcode < value;
        });
    return it != profile.card_roles.end() && it->passcode == passcode &&
           std::binary_search(it->role_ids.begin(), it->role_ids.end(), role_id);
}

PredicateEvaluationStatus evaluate_role_reference(
    const std::optional<environment::PublicCardReference>& reference,
    const environment::PublicEnvironmentObservation& observation,
    const std::uint8_t owning_participant,
    const std::string_view role_id,
    const StrategyProfileV1& profile) noexcept {
    if (!reference.has_value()) {
        return PredicateEvaluationStatus::False;
    }
    if (reference->kind == environment::PublicCardReferenceKind::RedactedSlot) {
        return PredicateEvaluationStatus::Unsupported;
    }
    if (reference->kind != environment::PublicCardReferenceKind::VisibleCard ||
        owning_participant > 1 || observation.perspective_player != owning_participant) {
        return PredicateEvaluationStatus::Invalid;
    }
    const auto decoded = environment::decode_canonical_public_safe_state(
        observation.canonical_safe_state_bytes());
    if (!decoded || decoded.value->match_context().perspective_player != owning_participant) {
        return PredicateEvaluationStatus::Invalid;
    }
    const auto& entities = decoded.value->entities();
    const auto matches = std::count_if(
        entities.begin(), entities.end(), [&](const auto& entity) {
            return entity.locator.value == reference->observation_locator;
        });
    if (matches != 1) {
        return PredicateEvaluationStatus::Invalid;
    }
    const auto it = std::find_if(entities.begin(), entities.end(), [&](const auto& entity) {
        return entity.locator.value == reference->observation_locator;
    });
    if (it == entities.end() || !it->identity_known || !it->passcode.has_value()) {
        return PredicateEvaluationStatus::Invalid;
    }
    return role_contains(*it->passcode, role_id, profile) ? PredicateEvaluationStatus::True
                                                            : PredicateEvaluationStatus::False;
}

PredicateEvaluationStatus compare_visibility(
    const std::optional<environment::PublicCardReference>& reference,
    const std::string_view expected) noexcept {
    std::string_view actual = "absent";
    if (reference.has_value()) {
        if (reference->kind == environment::PublicCardReferenceKind::VisibleCard) {
            actual = "visible";
        } else if (reference->kind == environment::PublicCardReferenceKind::RedactedSlot) {
            actual = "redacted";
        } else {
            return PredicateEvaluationStatus::Invalid;
        }
    }
    return actual == expected ? PredicateEvaluationStatus::True
                              : PredicateEvaluationStatus::False;
}

const CardRoleEntry* find_card_role(const StrategyProfileV1& profile,
                                    const std::uint32_t passcode) noexcept {
    const auto it = std::lower_bound(
        profile.card_roles.begin(), profile.card_roles.end(), passcode,
        [](const CardRoleEntry& entry, const std::uint32_t value) {
            return entry.passcode < value;
        });
    return it != profile.card_roles.end() && it->passcode == passcode ? &*it : nullptr;
}

}  // namespace

const TeacherPredicateRegistryV1& TeacherPredicateRegistryV1::canonical() noexcept {
    static const TeacherPredicateRegistryV1 registry;
    return registry;
}

const std::vector<PredicateDefinitionV1>&
TeacherPredicateRegistryV1::definitions() const noexcept {
    return predicate_definitions();
}

bool TeacherPredicateRegistryV1::validate_shape(const PredicateRef& value,
                                                std::string* diagnostic) const noexcept {
    try {
        const auto fail = [&](const char* message) {
            if (diagnostic != nullptr) {
                *diagnostic = message;
            }
            return false;
        };
        if (!valid_predicate_scope(value.scope) || !canonical_token(value.predicate_id)) {
            return fail("predicate scope or ID is not canonical");
        }
        const auto* expected = find_definition(value.predicate_id);
        if (expected == nullptr || expected->scope != value.scope) {
            return fail("predicate is not registered in v1");
        }
        if (value.arguments.size() != expected->argument_kinds.size()) {
            return fail("predicate argument arity is invalid");
        }
        for (std::size_t index = 0; index < value.arguments.size(); ++index) {
            if (!canonical_atom(value.arguments[index]) ||
                value.arguments[index].kind != expected->argument_kinds[index]) {
                return fail("predicate argument schema is invalid");
            }
        }

        if (value.scope == PredicateScope::Observation) {
            if (!fact_argument_is_valid(value)) {
                return fail("observation predicate fact argument is invalid");
            }
        } else if (value.scope == PredicateScope::Candidate) {
            if (value.predicate_id == "candidate.action_kind_equals") {
                static constexpr std::string_view values[] = {
                    "announcement", "assign_amount", "battle_command", "cancel",
                    "card_selection", "chain", "finish", "idle_command", "option",
                    "pick", "place", "position", "unsupported", "yes_no",
                };
                if (!std::binary_search(std::begin(values), std::end(values),
                                        std::string_view(value.arguments[0].token))) {
                    return fail("candidate action kind token is invalid");
                }
            } else if (value.predicate_id == "candidate.source_visibility_equals" ||
                       value.predicate_id == "candidate.target_visibility_equals") {
                static constexpr std::string_view values[] = {"absent", "redacted", "visible"};
                if (!std::binary_search(std::begin(values), std::end(values),
                                        std::string_view(value.arguments[0].token))) {
                    return fail("candidate visibility token is invalid");
                }
            } else if (value.predicate_id == "candidate.phase_equals" &&
                       value.arguments[0].u64 > std::numeric_limits<std::uint32_t>::max()) {
                return fail("candidate phase is out of range");
            } else if (value.predicate_id == "candidate.position_equals" &&
                       value.arguments[0].u64 > std::numeric_limits<std::uint8_t>::max()) {
                return fail("candidate position is out of range");
            } else if (value.predicate_id == "candidate.source_index_equals" &&
                       value.arguments[0].u64 > std::numeric_limits<std::uint32_t>::max()) {
                return fail("candidate source index is out of range");
            }
        }
        return true;
    } catch (...) {
        if (diagnostic != nullptr) {
            *diagnostic = "predicate validation threw";
        }
        return false;
    }
}

bool TeacherPredicateRegistryV1::validate_profile_ref(
    const PredicateRef& value, const StrategyProfileV1& profile,
    std::string* diagnostic) const noexcept {
    try {
        if (!validate_shape(value, diagnostic)) {
            return false;
        }
        const auto fail = [&](const char* message) {
            if (diagnostic != nullptr) {
                *diagnostic = message;
            }
            return false;
        };
        if (value.scope == PredicateScope::Observation) {
            return true;
        }
        if (value.scope == PredicateScope::Candidate &&
            (value.predicate_id == "candidate.source_role_contains" ||
             value.predicate_id == "candidate.target_role_contains")) {
            const auto role_id = value.arguments[0].token;
            const auto found = std::any_of(
                profile.card_roles.begin(), profile.card_roles.end(), [&](const auto& entry) {
                    return std::binary_search(entry.role_ids.begin(), entry.role_ids.end(), role_id);
                });
            return found ? true : fail("candidate role is not registered by the profile");
        }
        if (value.scope != PredicateScope::ProfileStatic) {
            return true;
        }
        if (value.predicate_id == "profile.card_role_exists") {
            return find_card_role(profile, value.arguments[0].passcode) != nullptr
                       ? true
                       : fail("profile card role passcode is not registered");
        }
        if (value.predicate_id == "profile.goal_exists") {
            return std::any_of(profile.goals.begin(), profile.goals.end(), [&](const auto& goal) {
                       return goal.goal_id == value.arguments[0].token;
                   })
                       ? true
                       : fail("profile goal is not registered");
        }
        if (value.predicate_id == "profile.intent_exists") {
            return std::any_of(profile.candidate_intents.begin(),
                               profile.candidate_intents.end(), [&](const auto& intent) {
                       return intent.intent_id == value.arguments[0].token;
                   })
                       ? true
                       : fail("profile intent is not registered");
        }
        if (value.predicate_id == "profile.line_exists") {
            return std::any_of(profile.lines.begin(), profile.lines.end(), [&](const auto& line) {
                       return line.line_id == value.arguments[0].token;
                   })
                       ? true
                       : fail("profile line is not registered");
        }
        if (value.predicate_id == "profile.resource_exists") {
            return std::any_of(profile.resources.begin(), profile.resources.end(),
                               [&](const auto& resource) {
                                   return resource.resource_id == value.arguments[0].token;
                               })
                       ? true
                       : fail("profile resource is not registered");
        }
        return fail("profile predicate has an unknown static category");
    } catch (...) {
        if (diagnostic != nullptr) {
            *diagnostic = "profile predicate validation threw";
        }
        return false;
    }
}

PredicateEvaluationStatus combine_predicate_statuses(
    const std::vector<PredicateEvaluationStatus>& statuses) noexcept {
    bool has_false = false;
    bool has_unsupported = false;
    for (const auto status : statuses) {
        switch (status) {
        case PredicateEvaluationStatus::Invalid:
            return PredicateEvaluationStatus::Invalid;
        case PredicateEvaluationStatus::Unsupported:
            has_unsupported = true;
            break;
        case PredicateEvaluationStatus::False:
            has_false = true;
            break;
        case PredicateEvaluationStatus::True:
            break;
        default:
            return PredicateEvaluationStatus::Invalid;
        }
    }
    if (has_unsupported) {
        return PredicateEvaluationStatus::Unsupported;
    }
    return has_false ? PredicateEvaluationStatus::False : PredicateEvaluationStatus::True;
}

PredicateEvaluationStatus evaluate_observation_predicate(
    const PredicateRef& value, const PublicFactSnapshot& public_facts) noexcept {
    try {
        const auto& registry = TeacherPredicateRegistryV1::canonical();
        if (!registry.validate_shape(value) || value.scope != PredicateScope::Observation ||
            !valid_snapshot(public_facts)) {
            return PredicateEvaluationStatus::Invalid;
        }
        const auto current = public_facts.value(value.arguments[0].token);
        if (!current.has_value()) {
            return PredicateEvaluationStatus::Unsupported;
        }
        const auto& expected = value.arguments[1];
        if (value.predicate_id == "observation.fact_u64_equals" ||
            value.predicate_id == "observation.fact_u64_at_least" ||
            value.predicate_id == "observation.fact_u64_at_most") {
            if (current->value_kind != PublicFactValueKind::U64) {
                return PredicateEvaluationStatus::Invalid;
            }
            if (value.predicate_id == "observation.fact_u64_equals") {
                return current->u64_value == expected.u64 ? PredicateEvaluationStatus::True
                                                            : PredicateEvaluationStatus::False;
            }
            if (value.predicate_id == "observation.fact_u64_at_least") {
                return current->u64_value >= expected.u64 ? PredicateEvaluationStatus::True
                                                           : PredicateEvaluationStatus::False;
            }
            return current->u64_value <= expected.u64 ? PredicateEvaluationStatus::True
                                                       : PredicateEvaluationStatus::False;
        }
        if (value.predicate_id == "observation.fact_i32_equals") {
            return current->value_kind == PublicFactValueKind::I32
                               ? (current->i32_value == expected.i32
                                      ? PredicateEvaluationStatus::True
                                      : PredicateEvaluationStatus::False)
                               : PredicateEvaluationStatus::Invalid;
        }
        if (value.predicate_id == "observation.fact_boolean_equals") {
            return current->value_kind == PublicFactValueKind::Boolean
                               ? (current->boolean_value == expected.boolean
                                      ? PredicateEvaluationStatus::True
                                      : PredicateEvaluationStatus::False)
                               : PredicateEvaluationStatus::Invalid;
        }
        if (value.predicate_id == "observation.fact_token_equals") {
            return current->value_kind == PublicFactValueKind::Token
                               ? (current->token_value == expected.token
                                      ? PredicateEvaluationStatus::True
                                      : PredicateEvaluationStatus::False)
                               : PredicateEvaluationStatus::Invalid;
        }
        return PredicateEvaluationStatus::Invalid;
    } catch (...) {
        return PredicateEvaluationStatus::Invalid;
    }
}

PredicateEvaluationStatus evaluate_profile_static_predicate(
    const PredicateRef& value, const StrategyProfileV1& profile) noexcept {
    try {
        const auto& registry = TeacherPredicateRegistryV1::canonical();
        if (!registry.validate_profile_ref(value, profile) ||
            value.scope != PredicateScope::ProfileStatic) {
            return PredicateEvaluationStatus::Invalid;
        }
        if (value.predicate_id == "profile.card_role_exists") {
            return find_card_role(profile, value.arguments[0].passcode) != nullptr
                       ? PredicateEvaluationStatus::True
                       : PredicateEvaluationStatus::False;
        }
        if (value.predicate_id == "profile.goal_exists") {
            return std::any_of(profile.goals.begin(), profile.goals.end(), [&](const auto& goal) {
                       return goal.goal_id == value.arguments[0].token;
                   })
                       ? PredicateEvaluationStatus::True
                       : PredicateEvaluationStatus::False;
        }
        if (value.predicate_id == "profile.intent_exists") {
            return std::any_of(profile.candidate_intents.begin(),
                               profile.candidate_intents.end(), [&](const auto& intent) {
                       return intent.intent_id == value.arguments[0].token;
                   })
                       ? PredicateEvaluationStatus::True
                       : PredicateEvaluationStatus::False;
        }
        if (value.predicate_id == "profile.line_exists") {
            return std::any_of(profile.lines.begin(), profile.lines.end(), [&](const auto& line) {
                       return line.line_id == value.arguments[0].token;
                   })
                       ? PredicateEvaluationStatus::True
                       : PredicateEvaluationStatus::False;
        }
        if (value.predicate_id == "profile.resource_exists") {
            return std::any_of(profile.resources.begin(), profile.resources.end(),
                               [&](const auto& resource) {
                                   return resource.resource_id == value.arguments[0].token;
                               })
                       ? PredicateEvaluationStatus::True
                       : PredicateEvaluationStatus::False;
        }
        return PredicateEvaluationStatus::Invalid;
    } catch (...) {
        return PredicateEvaluationStatus::Invalid;
    }
}

PredicateEvaluationStatus evaluate_candidate_predicate(
    const PredicateRef& value, const environment::EnvironmentActionCandidate& candidate,
    const environment::PublicEnvironmentObservation& observation,
    const std::uint8_t owning_participant, const StrategyProfileV1& profile) noexcept {
    try {
        const auto& registry = TeacherPredicateRegistryV1::canonical();
        if (!registry.validate_profile_ref(value, profile) ||
            value.scope != PredicateScope::Candidate || !valid_candidate_metadata(candidate) ||
            owning_participant > 1 || observation.perspective_player != owning_participant) {
            return PredicateEvaluationStatus::Invalid;
        }
        const auto& id = value.predicate_id;
        if (id == "candidate.action_kind_equals") {
            return environment::environment_action_kind_name(candidate.action_kind) ==
                           value.arguments[0].token
                       ? PredicateEvaluationStatus::True
                       : PredicateEvaluationStatus::False;
        }
        if (id == "candidate.choice_present") {
            return candidate.choice.has_value() ? PredicateEvaluationStatus::True
                                                : PredicateEvaluationStatus::False;
        }
        if (id == "candidate.choice_value_equals") {
            return !candidate.choice.has_value()
                       ? PredicateEvaluationStatus::Unsupported
                       : (candidate.choice->value == value.arguments[0].u64
                              ? PredicateEvaluationStatus::True
                              : PredicateEvaluationStatus::False);
        }
        if (id == "candidate.source_visibility_equals") {
            return compare_visibility(candidate.source_reference, value.arguments[0].token);
        }
        if (id == "candidate.target_visibility_equals") {
            return compare_visibility(candidate.target_reference, value.arguments[0].token);
        }
        if (id == "candidate.source_role_contains") {
            return evaluate_role_reference(candidate.source_reference, observation,
                                           owning_participant, value.arguments[0].token, profile);
        }
        if (id == "candidate.target_role_contains") {
            return evaluate_role_reference(candidate.target_reference, observation,
                                           owning_participant, value.arguments[0].token, profile);
        }
        if (id == "candidate.phase_equals") {
            return !candidate.phase.has_value()
                       ? PredicateEvaluationStatus::Unsupported
                       : (*candidate.phase == value.arguments[0].u64
                              ? PredicateEvaluationStatus::True
                              : PredicateEvaluationStatus::False);
        }
        if (id == "candidate.position_equals") {
            return !candidate.position.has_value()
                       ? PredicateEvaluationStatus::Unsupported
                       : (*candidate.position == value.arguments[0].u64
                              ? PredicateEvaluationStatus::True
                              : PredicateEvaluationStatus::False);
        }
        if (id == "candidate.source_index_equals") {
            return !candidate.source_index.has_value()
                       ? PredicateEvaluationStatus::Unsupported
                       : (*candidate.source_index == value.arguments[0].u64
                              ? PredicateEvaluationStatus::True
                              : PredicateEvaluationStatus::False);
        }
        if (id == "candidate.continuation_present") {
            return candidate.continuation_operation.empty() ? PredicateEvaluationStatus::False
                                                             : PredicateEvaluationStatus::True;
        }
        if (id == "candidate.submits_engine_response") {
            return candidate.submits_engine_response ? PredicateEvaluationStatus::True
                                                     : PredicateEvaluationStatus::False;
        }
        return PredicateEvaluationStatus::Invalid;
    } catch (...) {
        return PredicateEvaluationStatus::Invalid;
    }
}

}  // namespace ygo::teacher
