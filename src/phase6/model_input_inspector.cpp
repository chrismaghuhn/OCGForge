#include "ygo/phase6/model_input_inspector.hpp"

#include <cstddef>
#include <cstdint>
#include <exception>
#include <new>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

#include "ygo/environment/public_action_identity.hpp"
#include "ygo/environment/episodic_environment.hpp"
#include "ygo/model/encoded_model_input.hpp"
#include "ygo/model/logical_model_input.hpp"
#include "ygo/observation/observed_zone.hpp"
#include "ygo/observation/relationship.hpp"
#include "ygo/observation/visible_event.hpp"
#include "ygo/trace/sha256.hpp"

namespace ygo::phase6 {
namespace {

ModelInputInspectionResult failure(
    const ModelInputInspectionErrorCode code,
    const char* diagnostic) noexcept {
    ModelInputInspectionResult result;
    result.error = ModelInputInspectionError{code, diagnostic};
    return result;
}

template <typename T>
std::string optional_value(const std::optional<T>& value) {
    return value.has_value() ? std::to_string(*value) : "none";
}

std::string choice_text(const std::optional<environment::PublicChoice>& choice) {
    if (!choice.has_value()) return "none";
    std::ostringstream output;
    output << "kind=" << static_cast<unsigned int>(choice->kind)
           << ",value=" << choice->value
           << ",response_index=" << optional_value(choice->response_index);
    return output.str();
}

std::string reference_text(
    const std::optional<model::LogicalPublicCardReference>& reference) {
    if (!reference.has_value()) return "none";
    std::ostringstream output;
    output << "kind=" << static_cast<unsigned int>(reference->kind)
           << ",locator=" << reference->reference.locator.value
           << ",public_locator_ordinal="
           << reference->reference.locator.public_locator_ordinal
           << ",current_entity_ordinal="
           << optional_value(reference->reference.current_entity_ordinal);
    return output.str();
}

std::string current_reference_text(const model::LogicalCurrentReference& reference) {
    std::ostringstream output;
    output << "locator=" << reference.locator.value
           << ",public_locator_ordinal=" << reference.locator.public_locator_ordinal
           << ",current_entity_ordinal="
           << optional_value(reference.current_entity_ordinal);
    return output.str();
}

std::string optional_zone_value(
    const std::optional<observation::SemanticZone>& value) {
    return value.has_value() ? observation::semantic_zone_name(*value) : "none";
}

std::string public_identity_text(const model::LogicalEntity& entity) {
    if (!entity.card.identity_known || !entity.card.passcode.has_value()) {
        return "redacted";
    }
    return "visible:" + std::to_string(*entity.card.passcode);
}

void append_public_state(std::ostringstream& output,
                         const model::LogicalPublicState& state) {
    output << "public_safe_state.globals.life_points=";
    for (std::size_t index = 0; index < state.globals.life_points.size(); ++index) {
        if (index != 0) output << ',';
        output << state.globals.life_points[index];
    }
    output << '\n';
    output << "public_safe_state.globals.player_to_act="
           << optional_value(state.globals.player_to_act)
           << ",turn_player=" << optional_value(state.globals.turn_player)
           << ",turn_count=" << optional_value(state.globals.turn_count)
           << ",phase=" << optional_value(state.globals.phase)
           << ",chain_length=" << state.globals.chain_length
           << ",terminal=" << (state.globals.terminal ? 1 : 0) << '\n';

    output << "zones.count=" << state.zones.size() << '\n';
    for (std::size_t index = 0; index < state.zones.size(); ++index) {
        const auto& zone = state.zones[index];
        output << "zone[" << index << "].player="
               << static_cast<unsigned int>(zone.player)
               << ",kind=" << observation::semantic_zone_name(zone.kind)
               << ",total_count=" << zone.total_count
               << ",public_identity_count=" << zone.public_identity_count
               << ",redacted_count=" << zone.hidden_count
               << ",observable_order=" << (zone.player_observable_order ? 1 : 0)
               << '\n';
    }

    output << "entities.count=" << state.entities.size() << '\n';
    for (std::size_t index = 0; index < state.entities.size(); ++index) {
        const auto& entity = state.entities[index];
        output << "entity[" << index << "].public_locator_ordinal="
               << entity.public_locator_ordinal
               << ",current_entity_ordinal=" << entity.current_entity_ordinal
               << ",identity=" << public_identity_text(entity)
               << ",owner=" << optional_value(entity.card.owner)
               << ",controller=" << optional_value(entity.card.controller)
               << ",zone=" << observation::semantic_zone_name(entity.card.zone)
               << ",sequence=" << optional_value(entity.card.sequence)
               << ",position=" << observation::position_name(entity.card.position)
               << ",face_up=" << (entity.card.face_up ? 1 : 0)
               << ",face_down=" << (entity.card.face_down ? 1 : 0) << '\n';
    }

    output << "relationships.count=" << state.relationships.size() << '\n';
    for (std::size_t index = 0; index < state.relationships.size(); ++index) {
        const auto& relationship = state.relationships[index];
        output << "relationship[" << index << "].kind="
               << observation::relationship_kind_name(relationship.kind)
               << ",source=" << current_reference_text(relationship.source)
               << ",target=" << current_reference_text(relationship.target)
               << '\n';
    }

    output << "chain.length=" << state.chain.length
           << ",chain_links.count=" << state.chain.links.size() << '\n';
    for (std::size_t index = 0; index < state.chain.links.size(); ++index) {
        const auto& link = state.chain.links[index];
        output << "chain_link[" << index << "].index=" << link.index
               << ",activating_player=" << optional_value(link.activating_player)
               << ",activation_zone=" << optional_zone_value(link.activation_zone)
               << ",effect_description=" << optional_value(link.effect_description)
               << ",target_count=" << link.targets.size() << '\n';
    }

    output << "visible_events.count=" << state.visible_events.size() << '\n';
    for (std::size_t index = 0; index < state.visible_events.size(); ++index) {
        const auto& event = state.visible_events[index];
        output << "visible_event[" << index << "].event_index=" << event.event_index
               << ",kind=" << observation::visible_event_kind_name(event.kind)
               << ",player=" << optional_value(event.player)
               << ",public_locator_ordinal="
               << (event.entity.has_value()
                       ? std::to_string(event.entity->locator.public_locator_ordinal)
                       : "none")
               << ",public_passcode=" << optional_value(event.public_passcode)
               << ",from_zone="
               << (event.from_zone.has_value()
                       ? observation::semantic_zone_name(*event.from_zone)
                       : "none")
               << ",to_zone="
               << (event.to_zone.has_value()
                       ? observation::semantic_zone_name(*event.to_zone)
                       : "none")
               << ",target_count=" << event.targets.size()
               << '\n';
    }

    output << "match_context.perspective_player="
           << static_cast<unsigned int>(state.match_context.perspective_player)
           << ",duel_flags=" << state.match_context.duel_flags
           << ",own_decklist_known="
           << (state.match_context.knowledge.own_decklist_known ? 1 : 0)
           << ",opponent_decklist_known="
           << (state.match_context.knowledge.opponent_decklist_known ? 1 : 0)
           << '\n';
}

void append_candidates(std::ostringstream& output,
                       const model::LogicalModelInputV1& logical,
                       const std::optional<std::uint32_t> selected) {
    output << "candidate_count=" << logical.candidate_features.size() << '\n';
    for (std::size_t index = 0; index < logical.candidate_features.size(); ++index) {
        const auto& candidate = logical.candidate_features[index];
        output << "candidate[" << index << "].action_kind="
               << environment::environment_action_kind_name(candidate.action_kind)
               << ",public_action_key="
               << logical.candidate_routing[index].public_action_key
               << ",choice=" << choice_text(candidate.choice)
               << ",source_reference=" << reference_text(candidate.source_reference)
               << ",target_reference=" << reference_text(candidate.target_reference)
               << ",phase=" << optional_value(candidate.phase)
               << ",position=" << optional_value(candidate.position)
               << ",source_index=" << optional_value(candidate.source_index)
               << ",amount=" << optional_value(candidate.amount)
               << ",continuation_operation="
               << (candidate.continuation_operation.empty()
                       ? "none"
                       : candidate.continuation_operation)
               << ",submits_engine_response="
               << (candidate.submits_engine_response ? 1 : 0) << '\n';
    }
    if (!selected.has_value()) {
        output << "selected_label=none\n";
    } else {
        output << "selected_label=" << *selected
               << ",selected_public_action_key="
               << logical.candidate_routing[*selected].public_action_key << '\n';
    }
}

void validate_inputs(const model::LogicalModelInputV1& logical,
                     const model::EncodedModelInputV1& encoded,
                     const std::optional<std::uint32_t> selected) {
    if (logical.schema_id != model::kLogicalModelInputSchemaId) {
        throw ModelInputInspectionErrorCode::InvalidLogicalModelInput;
    }
    if (encoded.schema_id != model::kEncodedModelInputSchemaId) {
        throw ModelInputInspectionErrorCode::InvalidEncodedModelInput;
    }
    (void)model::canonical_logical_model_input_bytes(logical);
    (void)model::canonical_encoded_model_input_bytes(encoded);
    if (logical.candidate_count() == 0 ||
        logical.candidate_routing.size() != logical.candidate_count() ||
        encoded.candidate_count() != logical.candidate_count() ||
        encoded.routing_keys.size() != logical.candidate_count()) {
        throw ModelInputInspectionErrorCode::ModelInputMismatch;
    }
    std::set<std::string> keys;
    for (std::size_t index = 0; index < logical.candidate_count(); ++index) {
        const auto& key = logical.candidate_routing[index].public_action_key;
        if (!environment::is_public_action_key(key) || !keys.insert(key).second ||
            encoded.routing_keys[index] != key) {
            throw ModelInputInspectionErrorCode::ModelInputMismatch;
        }
    }
    if (selected.has_value() && *selected >= logical.candidate_count()) {
        throw ModelInputInspectionErrorCode::InvalidSelectedOrdinal;
    }
    if (model::model_input_identity(logical, encoded).empty()) {
        throw ModelInputInspectionErrorCode::ModelInputMismatch;
    }
}

}  // namespace

ModelInputInspectionResult inspect_public_model_input_v1(
    const model::LogicalModelInputV1& logical,
    const model::EncodedModelInputV1& encoded,
    const std::optional<std::uint32_t> selected_candidate_ordinal) noexcept {
    try {
        validate_inputs(logical, encoded, selected_candidate_ordinal);
        const auto logical_bytes = model::canonical_logical_model_input_bytes(logical);
        const auto encoded_bytes = model::canonical_encoded_model_input_bytes(encoded);
        std::ostringstream output;
        output << "model_input_inspection.v1\n"
               << "perspective_player="
               << static_cast<unsigned int>(logical.perspective_player)
               << ",decision_index=" << logical.decision_index << '\n'
               << "public_observation_context_kind="
               << (logical.public_observation_context_kind.has_value()
                       ? *logical.public_observation_context_kind
                       : "none")
               << ",public_observation_context_player="
               << optional_value(logical.public_observation_context_player) << '\n'
               << "logical_model_input_digest="
               << trace::sha256_bytes(logical_bytes) << '\n'
               << "encoded_model_input_digest="
               << trace::sha256_bytes(encoded_bytes) << '\n'
               << "model_input_identity="
               << model::model_input_identity(logical, encoded) << '\n';
        output << "public_locator_table.count=" << logical.public_locator_table.size() << '\n';
        for (std::size_t index = 0; index < logical.public_locator_table.size(); ++index) {
            output << "public_locator[" << index << "].value="
                   << logical.public_locator_table[index].value
                   << ",ordinal=" << logical.public_locator_table[index].public_locator_ordinal
                   << '\n';
        }
        output << "public_observation_context_references.count="
               << logical.referenced_public_entities.size() << '\n';
        for (std::size_t index = 0; index < logical.referenced_public_entities.size(); ++index) {
            output << "public_observation_context_reference[" << index << "].value="
                   << logical.referenced_public_entities[index].value
                   << ",ordinal="
                   << logical.referenced_public_entities[index].public_locator_ordinal
                   << '\n';
        }
        append_public_state(output, logical.public_safe_state);
        append_candidates(output, logical, selected_candidate_ordinal);
        return {std::optional<std::string>(output.str()), std::nullopt};
    } catch (const ModelInputInspectionErrorCode code) {
        switch (code) {
        case ModelInputInspectionErrorCode::InvalidLogicalModelInput:
            return failure(code, "logical model input is invalid");
        case ModelInputInspectionErrorCode::InvalidEncodedModelInput:
            return failure(code, "encoded model input is invalid");
        case ModelInputInspectionErrorCode::ModelInputMismatch:
            return failure(code, "logical and encoded model inputs differ");
        case ModelInputInspectionErrorCode::InvalidSelectedOrdinal:
            return failure(code, "selected candidate ordinal is out of range");
        case ModelInputInspectionErrorCode::InternalFailure:
            return failure(code, "model-input inspection failed");
        }
    } catch (const std::bad_alloc&) {
        return failure(ModelInputInspectionErrorCode::InternalFailure,
                       "model-input inspection failed");
    } catch (...) {
        return failure(ModelInputInspectionErrorCode::InternalFailure,
                       "model-input inspection failed");
    }
    return failure(ModelInputInspectionErrorCode::InternalFailure,
                   "model-input inspection failed");
}

}  // namespace ygo::phase6
