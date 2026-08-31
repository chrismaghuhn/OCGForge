#include "ygo/teacher/public_fact_registry.hpp"

#include <algorithm>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <utility>

#include "ygo/observation/visible_event.hpp"
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

bool valid_kind(const PublicFactValueKind value) noexcept {
    return static_cast<std::uint8_t>(value) <= 3;
}

bool valid_scope(const PublicFactValidityScope value) noexcept {
    return static_cast<std::uint8_t>(value) <= 1;
}

PublicFactDefinition make_definition(
    std::string fact_id,
    const PublicFactValueKind value_kind,
    const PublicFactSourceClassification source_classification,
    std::string source_rule,
    std::optional<std::uint64_t> u64_minimum = std::nullopt,
    std::optional<std::uint64_t> u64_maximum = std::nullopt,
    std::optional<std::int32_t> i32_minimum = std::nullopt,
    std::optional<std::int32_t> i32_maximum = std::nullopt,
    std::vector<std::string> token_domain = {}) {
    PublicFactDefinition result;
    result.fact_id = std::move(fact_id);
    result.value_kind = value_kind;
    result.allowed_scopes = {PublicFactValidityScope::CurrentReconciliation};
    result.source_classification = source_classification;
    result.u64_minimum = u64_minimum;
    result.u64_maximum = u64_maximum;
    result.i32_minimum = i32_minimum;
    result.i32_maximum = i32_maximum;
    result.token_domain = std::move(token_domain);
    result.source_rule = std::move(source_rule);
    return result;
}

const std::vector<std::string>& decision_context_kind_domain() {
    static const std::vector<std::string> domain = {
        "announcement", "battle_command", "card_selection", "chain",
        "counter",     "idle_command",   "option",        "ordering",
        "place",       "position",        "sum",           "tribute",
        "unselect_card", "unsupported",   "yes_no",
    };
    return domain;
}

const std::vector<PublicFactDefinition>& fact_definitions() {
    static const std::vector<PublicFactDefinition> definitions = {
        make_definition("blocked.continuation.assigned_amounts", PublicFactValueKind::U64,
                        PublicFactSourceClassification::Blocked,
                        "blocked:continuation.assigned_amounts"),
        make_definition("blocked.continuation.available_mask", PublicFactValueKind::U64,
                        PublicFactSourceClassification::Blocked,
                        "blocked:continuation.available_mask"),
        make_definition("blocked.continuation.can_cancel", PublicFactValueKind::Boolean,
                        PublicFactSourceClassification::Blocked,
                        "blocked:continuation.can_cancel"),
        make_definition("blocked.continuation.can_finish", PublicFactValueKind::Boolean,
                        PublicFactSourceClassification::Blocked,
                        "blocked:continuation.can_finish"),
        make_definition("blocked.continuation.max_count", PublicFactValueKind::U64,
                        PublicFactSourceClassification::Blocked,
                        "blocked:continuation.max_count"),
        make_definition("blocked.continuation.min_count", PublicFactValueKind::U64,
                        PublicFactSourceClassification::Blocked,
                        "blocked:continuation.min_count"),
        make_definition("blocked.continuation.remaining_indices", PublicFactValueKind::U64,
                        PublicFactSourceClassification::Blocked,
                        "blocked:continuation.remaining_indices"),
        make_definition("blocked.continuation.required_amount", PublicFactValueKind::U64,
                        PublicFactSourceClassification::Blocked,
                        "blocked:continuation.required_amount"),
        make_definition("blocked.continuation.selected_indices", PublicFactValueKind::U64,
                        PublicFactSourceClassification::Blocked,
                        "blocked:continuation.selected_indices"),
        make_definition("blocked.continuation.selected_mask", PublicFactValueKind::U64,
                        PublicFactSourceClassification::Blocked,
                        "blocked:continuation.selected_mask"),
        make_definition("blocked.continuation.target_sum", PublicFactValueKind::U64,
                        PublicFactSourceClassification::Blocked,
                        "blocked:continuation.target_sum"),
        make_definition("blocked.private.effect_use_state", PublicFactValueKind::Token,
                        PublicFactSourceClassification::Blocked,
                        "blocked:private.effect_use_state"),
        make_definition("blocked.private.exact_face_down_identity", PublicFactValueKind::Token,
                        PublicFactSourceClassification::Blocked,
                        "blocked:private.exact_face_down_identity"),
        make_definition("blocked.private.hidden_deck_order", PublicFactValueKind::Token,
                        PublicFactSourceClassification::Blocked,
                        "blocked:private.hidden_deck_order"),
        make_definition("blocked.private.hidden_extra_deck_identity", PublicFactValueKind::Token,
                        PublicFactSourceClassification::Blocked,
                        "blocked:private.hidden_extra_deck_identity"),
        make_definition("blocked.private.locator_cache", PublicFactValueKind::Token,
                        PublicFactSourceClassification::Blocked,
                        "blocked:private.locator_cache"),
        make_definition("blocked.private.opponent_hand_identity", PublicFactValueKind::Token,
                        PublicFactSourceClassification::Blocked,
                        "blocked:private.opponent_hand_identity"),
        make_definition("blocked.private.physical_identity_after_shuffle", PublicFactValueKind::Token,
                        PublicFactSourceClassification::Blocked,
                        "blocked:private.physical_identity_after_shuffle"),
        make_definition("blocked.private.raw_engine_query_state", PublicFactValueKind::Token,
                        PublicFactSourceClassification::Blocked,
                        "blocked:private.raw_engine_query_state"),
        make_definition("public.chain.length", PublicFactValueKind::U64,
                        PublicFactSourceClassification::Direct,
                        "public_safe_state.globals.chain_length", 0,
                        std::numeric_limits<std::uint64_t>::max()),
        make_definition("public.current_actor_is_perspective", PublicFactValueKind::Boolean,
                        PublicFactSourceClassification::SafeDerivation,
                        "public_safe_state.globals.player_to_act_vs_perspective"),
        make_definition("public.decision_context.kind", PublicFactValueKind::Token,
                        PublicFactSourceClassification::Direct,
                        "observation.decision_context.kind", std::nullopt, std::nullopt,
                        std::nullopt, std::nullopt, decision_context_kind_domain()),
        make_definition("public.last_event.amount", PublicFactValueKind::I32,
                        PublicFactSourceClassification::SafeDerivation,
                        "public_safe_state.visible_events.last.amount",
                        std::nullopt, std::nullopt,
                        std::numeric_limits<std::int32_t>::min(),
                        std::numeric_limits<std::int32_t>::max()),
        make_definition("public.life_points.opponent", PublicFactValueKind::U64,
                        PublicFactSourceClassification::Direct,
                        "public_safe_state.globals.life_points.opponent", 0,
                        std::numeric_limits<std::uint64_t>::max()),
        make_definition("public.life_points.self", PublicFactValueKind::U64,
                        PublicFactSourceClassification::Direct,
                        "public_safe_state.globals.life_points.self", 0,
                        std::numeric_limits<std::uint64_t>::max()),
        make_definition("public.perspective_player", PublicFactValueKind::U64,
                        PublicFactSourceClassification::Direct,
                        "observation.perspective_player", 0, 1),
        make_definition("public.terminal", PublicFactValueKind::Boolean,
                        PublicFactSourceClassification::Direct,
                        "public_safe_state.globals.terminal"),
        make_definition("public.turn.count", PublicFactValueKind::U64,
                        PublicFactSourceClassification::Direct,
                        "public_safe_state.globals.turn_count", 0,
                        std::numeric_limits<std::uint64_t>::max()),
        make_definition("public.turn.phase", PublicFactValueKind::U64,
                        PublicFactSourceClassification::Direct,
                        "public_safe_state.globals.phase", 0,
                        std::numeric_limits<std::uint64_t>::max()),
        make_definition("public.visible.entity_count", PublicFactValueKind::U64,
                        PublicFactSourceClassification::SafeDerivation,
                        "public_safe_state.entities.size", 0,
                        std::numeric_limits<std::uint64_t>::max()),
        make_definition("public.visible.event_count", PublicFactValueKind::U64,
                        PublicFactSourceClassification::SafeDerivation,
                        "public_safe_state.visible_events.size", 0,
                        std::numeric_limits<std::uint64_t>::max()),
        make_definition("public.visible.face_down_present", PublicFactValueKind::Boolean,
                        PublicFactSourceClassification::SafeDerivation,
                        "public_safe_state.entities.face_down_any"),
    };
    return definitions;
}

std::optional<std::size_t> definition_index(const std::string_view fact_id) noexcept {
    const auto& definitions = fact_definitions();
    for (std::size_t index = 0; index < definitions.size(); ++index) {
        if (definitions[index].fact_id == fact_id) {
            return index;
        }
    }
    return std::nullopt;
}

bool scope_allowed(const PublicFactDefinition& definition,
                   const PublicFactValidityScope scope) noexcept {
    return std::find(definition.allowed_scopes.begin(), definition.allowed_scopes.end(), scope) !=
           definition.allowed_scopes.end();
}

bool token_allowed(const PublicFactDefinition& definition,
                   const std::string_view token) noexcept {
    if (definition.token_domain.empty()) {
        return true;
    }
    return std::find(definition.token_domain.begin(), definition.token_domain.end(), token) !=
           definition.token_domain.end();
}

bool append_fact(std::vector<PublicFactValue>& values,
                 const PublicFactRegistry& registry,
                 PublicFactValue value) {
    if (!registry.validate(value)) {
        return false;
    }
    values.push_back(std::move(value));
    return true;
}

PublicFactValue make_boolean(std::string fact_id, const bool value) {
    PublicFactValue result;
    result.fact_id = std::move(fact_id);
    result.value_kind = PublicFactValueKind::Boolean;
    result.boolean_value = value;
    return result;
}

PublicFactValue make_u64(std::string fact_id, const std::uint64_t value) {
    PublicFactValue result;
    result.fact_id = std::move(fact_id);
    result.value_kind = PublicFactValueKind::U64;
    result.u64_value = value;
    return result;
}

PublicFactValue make_i32(std::string fact_id, const std::int32_t value) {
    PublicFactValue result;
    result.fact_id = std::move(fact_id);
    result.value_kind = PublicFactValueKind::I32;
    result.i32_value = value;
    return result;
}

PublicFactValue make_token(std::string fact_id, std::string value) {
    PublicFactValue result;
    result.fact_id = std::move(fact_id);
    result.value_kind = PublicFactValueKind::Token;
    result.token_value = std::move(value);
    return result;
}

}  // namespace

std::string_view public_fact_value_kind_name(const PublicFactValueKind value) noexcept {
    switch (value) {
    case PublicFactValueKind::Boolean:
        return "BOOLEAN";
    case PublicFactValueKind::U64:
        return "U64";
    case PublicFactValueKind::I32:
        return "I32";
    case PublicFactValueKind::Token:
        return "TOKEN";
    }
    return "UNKNOWN";
}

std::string_view public_fact_scope_name(const PublicFactValidityScope value) noexcept {
    switch (value) {
    case PublicFactValidityScope::CurrentReconciliation:
        return "CURRENT_RECONCILIATION";
    case PublicFactValidityScope::AcceptedPublicHistory:
        return "ACCEPTED_PUBLIC_HISTORY";
    }
    return "UNKNOWN";
}

std::string_view public_fact_source_name(
    const PublicFactSourceClassification value) noexcept {
    switch (value) {
    case PublicFactSourceClassification::Direct:
        return "DIRECT";
    case PublicFactSourceClassification::SafeDerivation:
        return "SAFE_DERIVATION";
    case PublicFactSourceClassification::Blocked:
        return "BLOCKED";
    }
    return "UNKNOWN";
}

bool validate_public_fact_value(const PublicFactValue& value) noexcept {
    if (!canonical_token(value.fact_id) || !valid_kind(value.value_kind) ||
        !valid_scope(value.validity_scope)) {
        return false;
    }
    switch (value.value_kind) {
    case PublicFactValueKind::Boolean:
        return value.u64_value == 0 && value.i32_value == 0 && value.token_value.empty();
    case PublicFactValueKind::U64:
        return !value.boolean_value && value.i32_value == 0 && value.token_value.empty();
    case PublicFactValueKind::I32:
        return !value.boolean_value && value.u64_value == 0 && value.token_value.empty();
    case PublicFactValueKind::Token:
        return !value.boolean_value && value.u64_value == 0 && value.i32_value == 0 &&
               canonical_token(value.token_value);
    }
    return false;
}

std::vector<std::uint8_t> canonical_public_fact_value_bytes(
    const PublicFactValue& value) {
    if (!validate_public_fact_value(value)) {
        throw std::invalid_argument("public fact value is not canonical");
    }
    trajectory::ByteWriter writer;
    writer.string(value.fact_id);
    writer.u8(static_cast<std::uint8_t>(value.value_kind));
    switch (value.value_kind) {
    case PublicFactValueKind::Boolean:
        writer.boolean(value.boolean_value);
        break;
    case PublicFactValueKind::U64:
        writer.u64be(value.u64_value);
        break;
    case PublicFactValueKind::I32:
        writer.i32(value.i32_value);
        break;
    case PublicFactValueKind::Token:
        writer.string(value.token_value);
        break;
    }
    writer.u8(static_cast<std::uint8_t>(value.validity_scope));
    return std::move(writer).take();
}

const PublicFactRegistry& PublicFactRegistry::canonical() noexcept {
    static const PublicFactRegistry registry;
    return registry;
}

const std::vector<PublicFactDefinition>& PublicFactRegistry::definitions() const noexcept {
    return fact_definitions();
}

bool PublicFactRegistry::is_registered(const std::string_view fact_id) const noexcept {
    return definition_index(fact_id).has_value();
}

std::optional<PublicFactSourceClassification> PublicFactRegistry::source_classification(
    const std::string_view fact_id) const noexcept {
    const auto index = definition_index(fact_id);
    if (!index.has_value()) {
        return std::nullopt;
    }
    return fact_definitions()[*index].source_classification;
}

bool PublicFactRegistry::validate(const PublicFactValue& value) const noexcept {
    if (!validate_public_fact_value(value)) {
        return false;
    }
    const auto index = definition_index(value.fact_id);
    if (!index.has_value()) {
        return false;
    }
    const auto& definition = fact_definitions()[*index];
    if (definition.source_classification == PublicFactSourceClassification::Blocked ||
        definition.value_kind != value.value_kind ||
        !scope_allowed(definition, value.validity_scope)) {
        return false;
    }
    if (value.value_kind == PublicFactValueKind::U64 &&
        ((!definition.u64_minimum.has_value() || value.u64_value >= *definition.u64_minimum) &&
         (!definition.u64_maximum.has_value() || value.u64_value <= *definition.u64_maximum))) {
        return token_allowed(definition, value.token_value);
    }
    if (value.value_kind == PublicFactValueKind::I32 &&
        ((!definition.i32_minimum.has_value() || value.i32_value >= *definition.i32_minimum) &&
         (!definition.i32_maximum.has_value() || value.i32_value <= *definition.i32_maximum))) {
        return token_allowed(definition, value.token_value);
    }
    if (value.value_kind == PublicFactValueKind::Token) {
        return token_allowed(definition, value.token_value);
    }
    return value.value_kind == PublicFactValueKind::Boolean;
}

bool PublicFactSnapshot::contains(const std::string_view fact_id) const noexcept {
    return std::any_of(values.begin(), values.end(), [&](const auto& value) {
        return value.fact_id == fact_id;
    });
}

std::optional<PublicFactValue> PublicFactSnapshot::value(
    const std::string_view fact_id) const {
    for (const auto& value : values) {
        if (value.fact_id == fact_id) {
            return value;
        }
    }
    return std::nullopt;
}

PublicFactExtractionResult extract_public_fact_snapshot(
    const environment::PublicEnvironmentObservation& observation) noexcept {
    PublicFactExtractionResult result;
    try {
        if (observation.perspective_player > 1) {
            return result;
        }
        const auto decoded = environment::decode_canonical_public_safe_state(
            observation.canonical_safe_state_bytes());
        if (!decoded ||
            decoded.value->match_context().perspective_player != observation.perspective_player) {
            return result;
        }

        const auto& registry = PublicFactRegistry::canonical();
        auto& values = result.snapshot.values;
        if (!append_fact(values, registry,
                         make_u64("public.perspective_player",
                                  observation.perspective_player))) {
            return result;
        }
        if (observation.decision_context.kind.has_value() &&
            !append_fact(values, registry,
                         make_token("public.decision_context.kind",
                                    *observation.decision_context.kind))) {
            return result;
        }

        const auto& view = *decoded.value;
        const auto& globals = view.globals();
        if (globals.phase.has_value() &&
            !append_fact(values, registry,
                         make_u64("public.turn.phase", *globals.phase))) {
            return result;
        }
        if (globals.turn_count.has_value() &&
            !append_fact(values, registry,
                         make_u64("public.turn.count", *globals.turn_count))) {
            return result;
        }
        if (!append_fact(values, registry,
                         make_u64("public.chain.length", globals.chain_length)) ||
            !append_fact(values, registry,
                         make_boolean("public.terminal", globals.terminal))) {
            return result;
        }
        const auto perspective = observation.perspective_player;
        const auto opponent = static_cast<std::uint8_t>(1 - perspective);
        if (perspective < globals.life_points.size() &&
            !append_fact(values, registry,
                         make_u64("public.life_points.self", globals.life_points[perspective]))) {
            return result;
        }
        if (opponent < globals.life_points.size() &&
            !append_fact(values, registry,
                         make_u64("public.life_points.opponent", globals.life_points[opponent]))) {
            return result;
        }
        if (globals.player_to_act.has_value() &&
            !append_fact(values, registry,
                         make_boolean("public.current_actor_is_perspective",
                                      *globals.player_to_act == perspective))) {
            return result;
        }
        if (!append_fact(values, registry,
                         make_u64("public.visible.entity_count", view.entities().size())) ||
            !append_fact(values, registry,
                         make_u64("public.visible.event_count", view.visible_events().size()))) {
            return result;
        }
        const bool face_down_present = std::any_of(
            view.entities().begin(), view.entities().end(), [](const auto& entity) {
                return entity.face_down;
            });
        if (!append_fact(values, registry,
                         make_boolean("public.visible.face_down_present", face_down_present))) {
            return result;
        }
        if (!view.visible_events().empty()) {
            const auto event = std::max_element(
                view.visible_events().begin(), view.visible_events().end(),
                [](const auto& left, const auto& right) {
                    return left.event_index < right.event_index;
                });
            if (event->amount.has_value() &&
                !append_fact(values, registry,
                             make_i32("public.last_event.amount", *event->amount))) {
                return result;
            }
        }

        std::sort(values.begin(), values.end(), [](const auto& left, const auto& right) {
            return canonical_public_fact_value_bytes(left) <
                   canonical_public_fact_value_bytes(right);
        });
        for (std::size_t index = 1; index < values.size(); ++index) {
            if (canonical_public_fact_value_bytes(values[index - 1]) ==
                canonical_public_fact_value_bytes(values[index])) {
                return result;
            }
        }
        result.valid = true;
        return result;
    } catch (...) {
        return result;
    }
}

}  // namespace ygo::teacher
