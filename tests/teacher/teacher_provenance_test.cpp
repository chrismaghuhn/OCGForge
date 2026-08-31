#include "ygo/policy/teacher.hpp"

#include "ygo/policy/production_provenance.hpp"
#include "ygo/teacher/salamangreat_profile.hpp"
#include "ygo/teacher/swordsoul_tenyi_profile.hpp"
#include "ygo/teacher/teacher_core.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "ygo/environment/episodic_environment.hpp"
#include "ygo/environment/public_action_identity.hpp"
#include "ygo/observation/player_observation.hpp"
#include "ygo/trajectory/codec.hpp"
#include "ygo/trajectory/policy_provenance.hpp"

namespace {

using namespace ygo::environment;
using namespace ygo::policy;
using namespace ygo::teacher;
using namespace ygo::trajectory;

void require(const bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

PublicEnvironmentObservation public_observation(const std::uint8_t participant,
                                                 const std::uint64_t decision_index,
                                                 const std::uint32_t self_life_points = 8000,
                                                 const std::uint32_t opponent_life_points = 7000,
                                                 const std::uint32_t chain_length = 0,
                                                 const std::string& decision_kind = "idle_command",
                                                 const std::optional<std::uint32_t>& visible_passcode =
                                                     std::nullopt) {
    ygo::observation::PlayerObservation source;
    source.schema_version = "ygo.player_observation.v1";
    source.perspective_player = participant;
    source.decision_index = decision_index;
    source.globals.life_points = {8000, 7000};
    source.globals.life_points[participant] = self_life_points;
    source.globals.life_points[1 - participant] = opponent_life_points;
    source.globals.player_to_act = participant;
    source.globals.turn_player = 0;
    source.globals.turn_count = 1;
    source.globals.phase = 0x04;
    source.globals.chain_length = chain_length;
    source.globals.terminal = false;
    source.match_context.perspective_player = participant;
    source.match_context.knowledge.own_decklist_known = true;
    source.match_context.knowledge.opponent_decklist_known = false;
    source.decision_context.kind = decision_kind;
    source.decision_context.player = participant;
    if (visible_passcode.has_value()) {
        ygo::observation::ObservedCard entity;
        entity.locator = {"p" + std::to_string(participant) + ":MONSTER_ZONE:0"};
        entity.identity_known = true;
        entity.passcode = *visible_passcode;
        entity.owner = participant;
        entity.controller = participant;
        entity.zone = ygo::observation::SemanticZone::MonsterZone;
        entity.sequence = 0;
        entity.face_up = true;
        entity.face_down = false;
        source.entities.push_back(std::move(entity));
    }
    return project_public_observation(source);
}

EnvironmentActionCandidate public_candidate() {
    EnvironmentActionCandidate candidate;
    candidate.action_kind = EnvironmentActionKind::YesNo;
    candidate.choice = PublicChoice{PublicChoiceKind::YesNo, 0, std::nullopt};
    PublicActionKeyInput key;
    key.action_kind = "yes_no";
    key.choice = candidate.choice;
    candidate.public_action_key = public_action_key(key);
    return candidate;
}

EnvironmentActionCandidate public_chain_candidate() {
    EnvironmentActionCandidate candidate;
    candidate.action_kind = EnvironmentActionKind::Chain;
    candidate.source_reference =
        PublicCardReference{PublicCardReferenceKind::VisibleCard, "p0:MONSTER_ZONE:0"};
    PublicActionKeyInput key;
    key.action_kind = "chain";
    key.source_reference = candidate.source_reference;
    candidate.public_action_key = public_action_key(key);
    return candidate;
}

PublicFactValue u64_fact(const std::string& fact_id, const std::uint64_t value) {
    PublicFactValue fact;
    fact.fact_id = fact_id;
    fact.value_kind = PublicFactValueKind::U64;
    fact.u64_value = value;
    return fact;
}

PublicFactValue boolean_fact(const std::string& fact_id, const bool value) {
    PublicFactValue fact;
    fact.fact_id = fact_id;
    fact.value_kind = PublicFactValueKind::Boolean;
    fact.boolean_value = value;
    return fact;
}

EpisodeLocalStrategyStateV1 reset_state(const StrategyProfileV1& profile) {
    const auto state = reset_strategy_state(profile);
    require(state.has_value(), "valid Teacher profile did not reset strategy state");
    return *state;
}

PredicateRef observation_u64_equals(const std::string& fact_id,
                                    const std::uint64_t value) {
    PredicateAtom fact_atom;
    fact_atom.kind = PredicateAtomKind::Token;
    fact_atom.token = fact_id;
    PredicateAtom value_atom;
    value_atom.kind = PredicateAtomKind::U64;
    value_atom.u64 = value;
    PredicateRef predicate;
    predicate.scope = PredicateScope::Observation;
    predicate.predicate_id = "observation.fact_u64_equals";
    predicate.arguments = {fact_atom, value_atom};
    return predicate;
}

const ParticipantPolicyAssignment& assignment_for_player(
    const std::vector<ParticipantPolicyAssignment>& assignments,
    const std::uint8_t player) {
    const auto it = std::find_if(assignments.begin(), assignments.end(),
                                 [player](const auto& assignment) {
                                     return assignment.player == player;
                                 });
    require(it != assignments.end(), "teacher assignment is missing a player");
    return *it;
}

const PolicyArtifact& artifact_for_deck_role(const PolicyArtifact& swordsoul,
                                             const PolicyArtifact& salamangreat,
                                             const DeckRole role) {
    return role == DeckRole::FirstLockedDeck ? swordsoul : salamangreat;
}

PolicyProvenanceEnvelope provenance_for(
    const PolicyArtifact& swordsoul, const PolicyArtifact& salamangreat,
    const std::vector<ParticipantPolicyAssignment>& assignments) {
    PolicyProvenanceEnvelope result;
    result.policy_artifacts = {swordsoul, salamangreat};
    std::sort(result.policy_artifacts.begin(), result.policy_artifacts.end(),
              [](const auto& left, const auto& right) {
                  return left.policy_artifact_id < right.policy_artifact_id;
              });
    result.participant_assignments = assignments;
    return result;
}

void test_production_registrations_and_artifacts() {
    const auto swordsoul = make_swordsoul_tenyi_profile();
    const auto salamangreat = make_salamangreat_profile();
    const auto swordsoul_binding = make_teacher_policy_binding(swordsoul);
    const auto salamangreat_binding = make_teacher_policy_binding(salamangreat);
    const auto swordsoul_artifact = make_teacher_policy_artifact(swordsoul);
    const auto salamangreat_artifact = make_teacher_policy_artifact(salamangreat);
    const auto resolver = make_production_policy_provenance_resolver();

    require(resolver.can_resolve(ProvenanceKind::ProducerImplementation,
                                 kTeacherProducerImplementationIdentity),
            "Teacher producer registration is missing");
    const auto* sampling = resolver.sampling_contract_capabilities(
        kDeterministicLexicographicArgmaxSamplingContractIdentity);
    require(sampling != nullptr && sampling->complete && sampling->deterministic,
            "Teacher deterministic sampling registration is incomplete");
    require(resolver.can_resolve(ProvenanceKind::ArtifactMetadataArtifact,
                                 swordsoul_binding.teacher_policy_binding_id) &&
                resolver.can_resolve(ProvenanceKind::ArtifactMetadataArtifact,
                                     salamangreat_binding.teacher_policy_binding_id),
            "current Teacher binding metadata identities are not registered");

    for (const auto& artifact : {swordsoul_artifact, salamangreat_artifact}) {
        require(artifact.policy_kind == PolicyKind::DeterministicHeuristic &&
                    artifact.producer_implementation_identity ==
                        kTeacherProducerImplementationIdentity &&
                    artifact.inference_adapter_identity ==
                        kDirectExecutionInferenceAdapterIdentity &&
                    artifact.observation_adapter_identity ==
                        kPublicObservationAdapterIdentity &&
                    artifact.action_adapter_identity ==
                        kPublicActionKeyAdapterIdentity &&
                    artifact.sampling_contract_identity ==
                        kDeterministicLexicographicArgmaxSamplingContractIdentity &&
                    artifact.policy_rng_contract_identity == kNoPolicyRngContractId &&
                    artifact.model_checkpoint_identity == std::nullopt &&
                    artifact.search_contract_identity == std::nullopt &&
                    artifact.demonstration_source_identity == std::nullopt &&
                    artifact.artifact_metadata_identity.has_value(),
                "Teacher artifact has incompatible provenance fields");
        require(static_cast<bool>(decode_policy_artifact(
                       canonical_policy_artifact_bytes(artifact))),
                "Teacher artifact failed its canonical codec");
    }
    require(swordsoul_artifact.artifact_metadata_identity ==
                std::optional<std::string>{swordsoul_binding.teacher_policy_binding_id} &&
                salamangreat_artifact.artifact_metadata_identity ==
                    std::optional<std::string>{salamangreat_binding.teacher_policy_binding_id},
            "Teacher artifact metadata is not its exact binding ID");

    auto changed_profile = swordsoul;
    changed_profile.preferences.back().value++;
    changed_profile.profile_id = strategy_profile_id(changed_profile);
    const auto changed_artifact = make_teacher_policy_artifact(changed_profile);
    require(changed_artifact.policy_artifact_id != swordsoul_artifact.policy_artifact_id,
            "changing profile content did not change Teacher artifact identity");

    const auto config = CertifiedEnvironmentConfig::canonical();
    std::string binding_error;
    require(validate_strategy_profile_binding(swordsoul, config, &binding_error) &&
                validate_teacher_policy_binding(swordsoul_binding, swordsoul, &binding_error) &&
                validate_strategy_profile_binding(salamangreat, config, &binding_error) &&
                validate_teacher_policy_binding(salamangreat_binding, salamangreat,
                                                &binding_error),
            "current Teacher profile/binding pair failed environment validation: " +
                binding_error);
    const std::array<PolicyRole, 2> policy_roles = {
        PolicyRole::Behavior, PolicyRole::Opponent};
    for (const auto seat_assignment : {SeatAssignment::Normal, SeatAssignment::Mirror}) {
        for (const auto starting_player : {std::uint8_t{0}, std::uint8_t{1}}) {
            const auto assignments = make_teacher_participant_assignments(
                swordsoul_artifact, salamangreat_artifact, config, seat_assignment,
                starting_player, policy_roles);
            const auto provenance = provenance_for(
                swordsoul_artifact, salamangreat_artifact, assignments);
            EpisodeSpec spec;
            spec.seat_assignment = seat_assignment;
            spec.starting_player = starting_player;
            std::string error;
            require(resolver.validate(provenance, config, spec, &error),
                    "Teacher provenance failed validation: " + error);
            for (std::uint8_t player = 0; player < 2; ++player) {
                const auto& assignment = assignment_for_player(assignments, player);
                const auto expected_deck_role = seat_assignment == SeatAssignment::Mirror
                                                    ? (player == 0 ? DeckRole::SecondLockedDeck
                                                                   : DeckRole::FirstLockedDeck)
                                                    : (player == 0 ? DeckRole::FirstLockedDeck
                                                                   : DeckRole::SecondLockedDeck);
                require(assignment.deck_role == expected_deck_role &&
                            assignment.seat_role ==
                                (player == starting_player ? SeatRole::StartingPlayer
                                                            : SeatRole::NonStartingPlayer),
                        "Teacher assignment did not preserve seat/deck mapping");
                const auto& profile = assignment.deck_role == DeckRole::FirstLockedDeck
                                          ? swordsoul
                                          : salamangreat;
                const auto& artifact = artifact_for_deck_role(
                    swordsoul_artifact, salamangreat_artifact, assignment.deck_role);
                const auto binding = make_teacher_policy_binding(profile);
                auto session = create_teacher_policy_session(
                    profile, binding, artifact, assignment);
                require(static_cast<bool>(session),
                        "Teacher policy session failed construction");
                const auto before = session.value->policy.state();
                const std::vector<EnvironmentActionCandidate> candidates = {public_candidate()};
                const PolicyInput input{public_observation(player, 0), candidates};
                const auto selection = session.value->policy.select(input);
                require(static_cast<bool>(selection) && selection.value->rng_cursor == std::nullopt,
                        "Teacher selection did not use NONE RNG");
                const auto execution = session.value->execution_binding();
                require(execution.policy_artifact_id == artifact.policy_artifact_id &&
                            execution.participant_policy_assignment_id ==
                                assignment.participant_policy_assignment_id &&
                            execution.policy_rng_contract_identity == kNoPolicyRngContractId &&
                            execution.policy_rng_stream_id == kNoPolicyRngContractId &&
                            execution.policy_rng_initialization_identity ==
                                kNoPolicyRngContractId &&
                            execution.policy_rng_identity == kNoPolicyRngContractId,
                        "Teacher execution binding did not use canonical NONE identities");
                require(session.value->policy.state() == before &&
                            session.value->policy.has_pending_proposal(),
                        "Teacher selection mutated trusted state or lost its proposal");
                session.value->policy.reject_pending_proposal();
                require(session.value->policy.state() == before &&
                            !session.value->policy.has_pending_proposal(),
                        "Teacher rejection did not clear only the pending proposal");
            }
        }
    }

    const auto valid_assignments = make_teacher_participant_assignments(
        swordsoul_artifact, salamangreat_artifact, config, SeatAssignment::Normal, 0,
        policy_roles);
    auto mismatched_assignment = assignment_for_player(valid_assignments, 0);
    mismatched_assignment.resolved_locked_deck_id = config.locked_decks[1].id;
    mismatched_assignment.resolved_locked_deck_sha256 = config.locked_decks[1].sha256;
    mismatched_assignment.participant_policy_assignment_id =
        compute_participant_policy_assignment_id(mismatched_assignment);
    require(!create_teacher_policy_session(
                  swordsoul, swordsoul_binding, swordsoul_artifact, mismatched_assignment),
            "Teacher session accepted an assignment with the wrong resolved deck");
}

void test_none_rng_codec_and_stale_binding() {
    PolicyRngDecisionProvenance attribution;
    attribution.decision_index = 3;
    attribution.acting_policy_assignment_id =
        "participant_policy_assignment.v1." + std::string(64, 'a');
    attribution.mode = PolicyRngMode::None;
    attribution.policy_rng_identity = kNoPolicyRngContractId;
    attribution.policy_rng_contract_identity = kNoPolicyRngContractId;
    attribution.policy_rng_stream_id = kNoPolicyRngContractId;
    attribution.policy_rng_initialization_identity = kNoPolicyRngContractId;
    const auto bytes = canonical_policy_rng_decision_provenance_bytes(attribution);
    const auto decoded = decode_policy_rng_decision_provenance(bytes);
    require(static_cast<bool>(decoded) && decoded.value->mode == PolicyRngMode::None &&
                !decoded.value->pre_cursor.has_value() && !decoded.value->post_cursor.has_value() &&
                !decoded.value->pre_state.has_value() && !decoded.value->post_state.has_value(),
            "NONE RNG attribution did not pass its canonical codec");

    auto binding = make_teacher_policy_binding(make_swordsoul_tenyi_profile());
    binding.teacher_policy_binding_id =
        "ocgforge.teacher_policy_binding.v1." + std::string(64, 'b');
    require(!validate_teacher_policy_binding(binding),
            "stale/forged Teacher binding ID passed validation");
    const auto resolver = make_production_policy_provenance_resolver();
    require(!resolver.can_resolve(ProvenanceKind::ArtifactMetadataArtifact,
                                  binding.teacher_policy_binding_id),
            "production resolver accepted an unregistered Teacher binding ID");
}

void test_teacher_core_stage_and_state_contracts() {
    const auto profile = make_swordsoul_tenyi_profile();
    const auto reset = reset_strategy_state(profile);
    require(reset.has_value(), "Swordsoul profile did not reset for TeacherCore tests");
    const auto observation = public_observation(0, 0);
    const std::vector<EnvironmentActionCandidate> nonmatching = {public_candidate()};
    TeacherCore core;

    const auto zero_match = core.propose(PolicyInput{observation, nonmatching}, profile, *reset);
    require(zero_match.status == TeacherRankingStatus::Selected &&
                zero_match.fallback_level == std::optional<TeacherFallbackLevel>{
                    TeacherFallbackLevel::F4},
            "a domain with no active-line match was incorrectly published as F0");

    auto staged_profile = profile;
    const auto recovery_edge = std::find_if(
        staged_profile.recovery_edges.begin(), staged_profile.recovery_edges.end(),
        [](const auto& edge) { return edge.recovery_edge_id == "recovery.interaction.main1"; });
    require(recovery_edge != staged_profile.recovery_edges.end(),
            "Swordsoul recovery edge fixture is missing");
    recovery_edge->candidate_intent_ids = {"intent.interaction.chain"};
    staged_profile.profile_id = strategy_profile_id(staged_profile);
    auto recovery_state = reset_state(staged_profile);
    recovery_state.active_goal_id = "goal.interaction.preservation";
    recovery_state.active_line_id = "line.interaction.preserve";
    recovery_state.public_resource_facts = {u64_fact("public.chain.length", 1)};
    const auto recovery_observation = public_observation(
        0, 0, 8000, 7000, 0, "idle_command", std::optional<std::uint32_t>{10045474});
    const auto recovery_candidate = public_chain_candidate();
    const std::vector<EnvironmentActionCandidate> recovery_candidates = {recovery_candidate};
    const auto recovery_result = core.propose(
        PolicyInput{recovery_observation, recovery_candidates}, staged_profile, recovery_state);
    require(recovery_result.status == TeacherRankingStatus::Selected &&
                recovery_result.fallback_level == std::optional<TeacherFallbackLevel>{
                    TeacherFallbackLevel::F1},
            "a recovery-only match was incorrectly published as F0");

    auto image_state = *reset;
    image_state.active_goal_id = "goal.main1.swordsoul";
    image_state.active_line_id = "line.main1.swordsoul";
    image_state.completed_line_node_ids = {"node.main1.mo_ye"};
    image_state.public_resource_facts = {u64_fact("public.chain.length", 0)};
    image_state.public_restriction_facts = {boolean_fact("public.terminal", false)};
    image_state.public_threat_facts = {u64_fact("public.life_points.self", 8000)};
    const auto image_result = core.propose(
        PolicyInput{observation, nonmatching}, profile, image_state);
    require(image_result.proposed_state_delta.has_value() &&
                image_result.proposed_state_delta->public_resource_facts ==
                    image_state.public_resource_facts &&
                image_result.proposed_state_delta->public_restriction_facts ==
                    image_state.public_restriction_facts &&
                image_result.proposed_state_delta->public_threat_facts ==
                    image_state.public_threat_facts &&
                image_result.proposed_state_delta->completed_line_node_ids ==
                    image_state.completed_line_node_ids,
            "TeacherCore did not preserve the complete reconciled fact image");

    auto completion_profile = profile;
    auto completion_line = std::find_if(
        completion_profile.lines.begin(), completion_profile.lines.end(),
        [](const auto& line) { return line.line_id == "line.main1.swordsoul"; });
    require(completion_line != completion_profile.lines.end(),
            "Swordsoul completion line fixture is missing");
    auto completion_node = std::find_if(
        completion_line->nodes.begin(), completion_line->nodes.end(),
        [](const auto& node) { return node.node_id == "node.main1.mo_ye"; });
    require(completion_node != completion_line->nodes.end(),
            "Swordsoul completion node fixture is missing");
    completion_node->completion_predicates = {observation_u64_equals(
        "public.life_points.self", 8000)};
    completion_profile.profile_id = strategy_profile_id(completion_profile);
    auto completion_state = reset_state(completion_profile);
    completion_state.active_goal_id = "goal.main1.swordsoul";
    completion_state.active_line_id = "line.main1.swordsoul";
    completion_state.last_accepted_decision_index = 10;
    completion_state.last_accepted_public_action_key = public_candidate().public_action_key;
    const auto completion_result = core.propose(
        PolicyInput{public_observation(0, 12), nonmatching}, completion_profile,
        completion_state);
    require(completion_result.proposed_state_delta.has_value() &&
                std::binary_search(
                    completion_result.proposed_state_delta->completed_line_node_ids.begin(),
                    completion_result.proposed_state_delta->completed_line_node_ids.end(),
                    "node.main1.mo_ye"),
            "same-participant public completion was not carried into the proposed state image");
}

void test_unpublished_profile_is_rejected_before_session_creation() {
    const auto config = CertifiedEnvironmentConfig::canonical();
    const auto published = make_swordsoul_tenyi_profile();
    auto unpublished = published;
    unpublished.preferences.back().value++;
    unpublished.profile_id = strategy_profile_id(unpublished);
    const auto binding = make_teacher_policy_binding(unpublished);
    const auto artifact = make_teacher_policy_artifact(unpublished);
    const auto published_artifact = make_teacher_policy_artifact(published);
    const auto other_artifact = make_teacher_policy_artifact(make_salamangreat_profile());
    const auto assignments = make_teacher_participant_assignments(
        published_artifact, other_artifact, config, SeatAssignment::Normal, 0,
        {PolicyRole::Behavior, PolicyRole::Opponent});
    auto assignment = assignment_for_player(assignments, 0);
    assignment.policy_artifact_id = artifact.policy_artifact_id;
    assignment.participant_policy_assignment_id =
        compute_participant_policy_assignment_id(assignment);
    require(!create_teacher_policy_session(unpublished, binding, artifact, assignment),
            "an internally valid but unpublished profile entered a production session");

    auto wrong_format = published;
    wrong_format.format_id = "TCG_ADVANCED_2026_05_17";
    wrong_format.profile_id = strategy_profile_id(wrong_format);
    const auto wrong_format_binding = make_teacher_policy_binding(wrong_format);
    const auto wrong_format_artifact = make_teacher_policy_artifact(wrong_format);
    auto wrong_format_assignment = assignment_for_player(assignments, 0);
    wrong_format_assignment.policy_artifact_id = wrong_format_artifact.policy_artifact_id;
    wrong_format_assignment.participant_policy_assignment_id =
        compute_participant_policy_assignment_id(wrong_format_assignment);
    require(!create_teacher_policy_session(wrong_format, wrong_format_binding,
                                           wrong_format_artifact, wrong_format_assignment),
            "a profile with a non-certified format entered a production session");

    auto wrong_rules = published;
    wrong_rules.rules_bundle_id = std::string(64, '0');
    wrong_rules.profile_id = strategy_profile_id(wrong_rules);
    const auto wrong_rules_binding = make_teacher_policy_binding(wrong_rules);
    const auto wrong_rules_artifact = make_teacher_policy_artifact(wrong_rules);
    auto wrong_rules_assignment = assignment_for_player(assignments, 0);
    wrong_rules_assignment.policy_artifact_id = wrong_rules_artifact.policy_artifact_id;
    wrong_rules_assignment.participant_policy_assignment_id =
        compute_participant_policy_assignment_id(wrong_rules_assignment);
    require(!create_teacher_policy_session(wrong_rules, wrong_rules_binding,
                                           wrong_rules_artifact, wrong_rules_assignment),
            "a profile with a non-certified rules bundle entered a production session");
}

}  // namespace

int main() {
    try {
        test_production_registrations_and_artifacts();
        test_none_rng_codec_and_stale_binding();
        test_teacher_core_stage_and_state_contracts();
        test_unpublished_profile_is_rejected_before_session_creation();
        const auto swordsoul = make_swordsoul_tenyi_profile();
        const auto salamangreat = make_salamangreat_profile();
        std::cout << "swordsoul_profile_id=" << swordsoul.profile_id << '\n';
        std::cout << "salamangreat_profile_id=" << salamangreat.profile_id << '\n';
        std::cout << "swordsoul_binding_id="
                  << make_teacher_policy_binding(swordsoul).teacher_policy_binding_id << '\n';
        std::cout << "salamangreat_binding_id="
                  << make_teacher_policy_binding(salamangreat).teacher_policy_binding_id << '\n';
        std::cout << "swordsoul_policy_artifact_id="
                  << make_teacher_policy_artifact(swordsoul).policy_artifact_id << '\n';
        std::cout << "salamangreat_policy_artifact_id="
                  << make_teacher_policy_artifact(salamangreat).policy_artifact_id << '\n';
        std::cout << "teacher_provenance_test: PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "teacher_provenance_test: FAIL: " << error.what() << '\n';
        return 1;
    }
}
