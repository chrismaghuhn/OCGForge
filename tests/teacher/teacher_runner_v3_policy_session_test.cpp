#include "ygo/environment/episodic_environment.hpp"
#include "ygo/environment/public_action_identity.hpp"
#include "ygo/observation/player_observation.hpp"
#include "ygo/policy/teacher.hpp"
#include "ygo/policy/teacher_runner_v3.hpp"
#include "ygo/policy/teacher_v2.hpp"
#include "ygo/teacher/salamangreat_profile.hpp"
#include "ygo/teacher/swordsoul_tenyi_profile.hpp"
#include "ygo/trajectory/codec.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace ygo::environment;
using namespace ygo::policy;
using namespace ygo::teacher;
using namespace ygo::trajectory;
namespace trajectory = ygo::trajectory;

void require(const bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

PublicEnvironmentObservation observation(const std::uint64_t decision_index,
                                         const std::string& kind,
                                         const std::optional<std::uint32_t>& passcode =
                                             std::nullopt,
                                         const std::optional<std::uint64_t>& private_marker =
                                             std::nullopt) {
    ygo::observation::PlayerObservation source;
    source.schema_version = "ygo.player_observation.v1";
    source.perspective_player = 0;
    source.decision_index = decision_index;
    source.globals.life_points = {8000, 7000};
    source.globals.player_to_act = 0;
    source.globals.turn_player = 0;
    source.globals.turn_count = 16;
    source.globals.phase = 4;
    source.globals.chain_length = 0;
    source.globals.terminal = false;
    source.match_context.perspective_player = 0;
    source.match_context.knowledge.own_decklist_known = true;
    source.match_context.knowledge.opponent_decklist_known = false;
    source.decision_context.kind = kind;
    source.decision_context.player = 0;
    if (private_marker.has_value()) {
        const auto marker = std::to_string(*private_marker);
        source.decision_context.decision_id = "private.decision." + marker;
        source.decision_context.engine_step_index = *private_marker;
        source.decision_context.engine_message_type =
            static_cast<std::uint8_t>(*private_marker % 255);
        source.decision_context.engine_message_name = "private.message." + marker;
        source.decision_context.continuation_id = "private.continuation." + marker;
        source.observation_hash = "private.observation." + marker;
    }
    if (passcode.has_value()) {
        ygo::observation::ObservedCard entity;
        entity.locator = {"p0:EXTRA_DECK:0"};
        entity.identity_known = true;
        entity.passcode = *passcode;
        entity.owner = 0;
        entity.controller = 0;
        entity.zone = ygo::observation::SemanticZone::ExtraDeck;
        entity.sequence = 0;
        entity.face_down = false;
        entity.face_up = true;
        source.entities.push_back(std::move(entity));
    }
    return project_public_observation(source);
}

EnvironmentActionCandidate idle_card_v2(const std::uint32_t command) {
    EnvironmentActionCandidate result;
    result.action_kind = EnvironmentActionKind::IdleCommand;
    result.choice = PublicChoice{PublicChoiceKind::EffectChoice, 0, std::nullopt};
    result.source_reference =
        PublicCardReference{PublicCardReferenceKind::VisibleCard, "p0:EXTRA_DECK:0"};
    result.phase = command;
    PublicActionKeyInput key;
    key.action_kind = "idle_command";
    key.choice = result.choice;
    key.source_reference = result.source_reference;
    key.phase = result.phase;
    result.public_action_key = public_action_key_v2(key);
    return result;
}

EnvironmentActionCandidate idle_card_v1(const std::uint32_t command) {
    auto result = idle_card_v2(command);
    PublicActionKeyInput key;
    key.action_kind = "idle_command";
    key.choice = result.choice;
    key.source_reference = result.source_reference;
    key.phase = result.phase;
    result.public_action_key = public_action_key(key);
    return result;
}

EnvironmentActionCandidate material_v2(
    const std::string& locator,
    const PublicCardSelectionOperation operation) {
    EnvironmentActionCandidate result;
    result.action_kind = EnvironmentActionKind::CardSelection;
    result.source_reference =
        PublicCardReference{PublicCardReferenceKind::VisibleCard, locator};
    result.card_selection_operation = operation;
    PublicActionKeyInput key;
    key.action_kind = "card_selection";
    key.source_reference = result.source_reference;
    key.card_selection_operation = operation;
    result.public_action_key = public_action_key_v2(key);
    return result;
}

EnvironmentActionCandidate cancel_v2() {
    EnvironmentActionCandidate result;
    result.action_kind = EnvironmentActionKind::Cancel;
    PublicActionKeyInput key;
    key.action_kind = "cancel";
    result.public_action_key = public_action_key_v2(key);
    return result;
}

DecisionFrame frame(const std::uint64_t decision_index,
                    const EnvironmentDecisionKind kind,
                    const std::string& observation_kind,
                    std::vector<EnvironmentActionCandidate> candidates,
                    const std::optional<std::uint32_t>& visible_passcode = std::nullopt) {
    DecisionFrame result;
    result.contract_id = std::string(kEpisodicEnvironmentV3ContractId);
    result.episode_semantic_id =
        "0000000000000000000000000000000000000000000000000000000000000000";
    result.decision_index = decision_index;
    result.submission_token = SubmissionToken{1, decision_index + 1};
    result.acting_player = 0;
    result.public_observation = observation(decision_index, observation_kind, visible_passcode);
    result.public_observation_digest = public_observation_digest(result.public_observation);
    result.request.kind = kind;
    result.request.player = 0;
    result.request.candidates = std::move(candidates);
    std::vector<std::string> keys;
    for (const auto& candidate : result.request.candidates) {
        keys.push_back(candidate.public_action_key);
    }
    result.public_candidate_domain_digest = public_candidate_domain_digest_v2(
        environment_decision_kind_name(kind), keys);
    PublicSemanticDecisionIdentityInput identity;
    identity.episode_semantic_id = result.episode_semantic_id;
    identity.decision_index = result.decision_index;
    identity.acting_player = result.acting_player;
    identity.request_kind = std::string(environment_decision_kind_name(kind));
    identity.public_observation_digest = result.public_observation_digest;
    identity.public_candidate_domain_digest = result.public_candidate_domain_digest;
    result.public_semantic_decision_id = public_semantic_decision_id_v2(identity);
    return result;
}

struct Fixture final {
    StrategyProfileV1 swordsoul = make_swordsoul_tenyi_profile();
    StrategyProfileV1 salamangreat = make_salamangreat_profile();
    trajectory::PolicyArtifact swordsoul_artifact = make_teacher_policy_artifact_v2(swordsoul);
    trajectory::PolicyArtifact salamangreat_artifact =
        make_teacher_policy_artifact_v2(salamangreat);
    CertifiedEnvironmentConfig config = CertifiedEnvironmentConfig::canonical();
    std::vector<ParticipantPolicyAssignment> assignments;
};

Fixture fixture() {
    Fixture result;
    const std::array<PolicyRole, 2> roles = {PolicyRole::Behavior, PolicyRole::Opponent};
    result.assignments = make_teacher_participant_assignments(
        result.swordsoul_artifact, result.salamangreat_artifact, result.config,
        SeatAssignment::Mirror, 0, roles);
    return result;
}

const ParticipantPolicyAssignment& assignment_for(
    const std::vector<ParticipantPolicyAssignment>& assignments,
    const std::uint8_t player) {
    const auto found = std::find_if(
        assignments.begin(), assignments.end(),
        [player](const auto& value) { return value.player == player; });
    require(found != assignments.end(), "fixture assignment missing player");
    return *found;
}

TeacherPolicySessionV2 make_session(const Fixture& value, const std::uint8_t player) {
    const auto& assignment = assignment_for(value.assignments, player);
    const auto& profile = assignment.deck_role == DeckRole::FirstLockedDeck
                              ? value.swordsoul
                              : value.salamangreat;
    const auto& artifact = assignment.deck_role == DeckRole::FirstLockedDeck
                               ? value.swordsoul_artifact
                               : value.salamangreat_artifact;
    const auto binding = make_teacher_policy_binding_v2(profile);
    auto created = create_teacher_policy_session_v2(
        profile, binding, artifact, assignment);
    require(static_cast<bool>(created),
            created.error.has_value() ? created.error->message :
                                        "V2 session creation failed");
    return std::move(*created.value);
}

TeacherRunnerV3Config make_config(const Fixture& value) {
    TeacherRunnerV3Config config;
    config.sessions[0] = make_session(value, 0);
    config.sessions[1] = make_session(value, 1);
    return config;
}

TeacherRunnerV3 make_runner(const Fixture& value) {
    auto config = make_config(value);
    auto created = TeacherRunnerV3::create(std::move(config));
    require(static_cast<bool>(created), "V3 runner creation failed");
    return std::move(*created.value);
}

void commit_selected(TeacherRunnerV3& runner,
                     const DecisionFrame& frame,
                     const std::string& selected_key) {
    StepAccepted accepted;
    accepted.transition.episode_semantic_id = frame.episode_semantic_id;
    accepted.transition.public_semantic_decision_id = frame.public_semantic_decision_id;
    accepted.transition.decision_index = frame.decision_index;
    accepted.transition.selected_public_action_key = selected_key;
    require(runner.commit(accepted), "V3 runner commit rejected pending V2 proposal");
}

void test_v2_session_provenance_and_rng() {
    const auto value = fixture();
    auto session = make_session(value, 0);
    require(session.policy.policy_binding().teacher_policy_binding_id ==
                "ocgforge.teacher_policy_binding.v1.0ec1d4ce29956c72e7e8537d24ba04834dbed4f820ff222d0209f9d7880bc7c0" &&
                session.artifact.policy_artifact_id ==
                    "policy_artifact.v1.17b2395a97e820c645f59037e7203f17bab808f90c1b70936c1000591efdb40e",
            "V2 Salamangreat provenance identity changed");
    require(session.policy.policy_binding().teacher_core_artifact_identity ==
                kTeacherProducerImplementationIdentityV2 &&
                session.policy.policy_binding().diagnostic_contract_identity == std::nullopt &&
                session.artifact.action_adapter_identity == kPublicActionKeyAdapterIdentityV2 &&
                session.artifact.policy_rng_contract_identity == kNoPolicyRngContractId,
            "V2 session provenance or RNG identity is not exact");
    require(session.execution_binding().policy_rng_identity == kNoPolicyRngContractId,
            "V2 session published a policy RNG identity");
    auto swordsoul_session = make_session(value, 1);
    require(swordsoul_session.policy.policy_binding().teacher_policy_binding_id ==
                "ocgforge.teacher_policy_binding.v1.4da70292d08b5608552d9f9246050c2ea5b9c3b52c962ea26a8f9816c5447a5f" &&
                swordsoul_session.artifact.policy_artifact_id ==
                    "policy_artifact.v1.efbd7962734c993d9374acc4c527f722a2413e7279b851d10340a83defccfc01",
            "V2 Swordsoul provenance identity changed");
}

template <typename Mutator>
void require_runner_rejects_mutated_session(Mutator mutator,
                                             const std::string& label) {
    const auto value = fixture();
    auto config = make_config(value);
    mutator(*config.sessions[0]);
    const auto created = TeacherRunnerV3::create(std::move(config));
    require(!created, "V3 runner accepted " + label);
}

void test_runner_revalidates_mutable_session_provenance() {
    require_runner_rejects_mutated_session(
        [](auto& session) {
            session.artifact.producer_implementation_identity = "tampered.producer";
        },
        "a tampered producer");
    require_runner_rejects_mutated_session(
        [](auto& session) {
            session.artifact.inference_adapter_identity = "tampered.inference";
        },
        "a tampered inference adapter");
    require_runner_rejects_mutated_session(
        [](auto& session) {
            session.artifact.sampling_contract_identity = "tampered.sampling";
        },
        "a tampered sampling contract");
    require_runner_rejects_mutated_session(
        [](auto& session) {
            session.assignment.resolved_locked_deck_id = "tampered.deck";
        },
        "a tampered locked-deck ID");
    require_runner_rejects_mutated_session(
        [](auto& session) {
            session.assignment.resolved_locked_deck_sha256 = "tampered.sha256";
        },
        "a tampered locked-deck SHA256");
    require_runner_rejects_mutated_session(
        [](auto& session) {
            session.assignment.deck_role = DeckRole::FirstLockedDeck;
        },
        "a tampered deck role");

    const auto value = fixture();
    auto duplicate_config = make_config(value);
    auto duplicate_assignment = assignment_for(value.assignments, 0);
    duplicate_assignment.player = 1;
    duplicate_assignment.seat_role = SeatRole::NonStartingPlayer;
    duplicate_assignment.policy_role = PolicyRole::Opponent;
    duplicate_assignment.participant_policy_assignment_id =
        compute_participant_policy_assignment_id(duplicate_assignment);
    const auto duplicate_binding = make_teacher_policy_binding_v2(value.salamangreat);
    auto duplicate_session = create_teacher_policy_session_v2(
        value.salamangreat, duplicate_binding, value.salamangreat_artifact,
        duplicate_assignment);
    require(static_cast<bool>(duplicate_session), "duplicate-deck fixture session was invalid");
    duplicate_config.sessions[1] = std::move(*duplicate_session.value);
    require(!TeacherRunnerV3::create(std::move(duplicate_config)),
            "V3 runner accepted two sessions for one locked deck role");
}

void test_v2_domain_rejection() {
    const auto value = fixture();
    auto session = make_session(value, 0);
    const auto v2_frame = frame(10, EnvironmentDecisionKind::IdleCommand, "idle_command",
                                {idle_card_v2(1)}, 48815792);
    const auto v1_candidate = idle_card_v1(1);
    require(!session.policy.select(
                 PolicyInput{v2_frame.public_observation, {v1_candidate}}),
            "V2 session accepted a historical V1 action domain");
    require(!session.policy.select(
                 PolicyInput{v2_frame.public_observation,
                             {idle_card_v2(1), v1_candidate}}),
            "V2 session accepted a mixed action domain");
    auto malformed = idle_card_v2(1);
    malformed.public_action_key = "public_action.v2.malformed";
    require(!session.policy.select(
                 PolicyInput{v2_frame.public_observation, {malformed}}),
            "V2 session accepted a malformed V2 action key");
}

void test_hiita_session_lifecycle(const bool relabel_first_material) {
    const auto value = fixture();
    auto runner = make_runner(value);

    const auto idle = frame(10, EnvironmentDecisionKind::IdleCommand, "idle_command",
                            {idle_card_v2(1)}, 48815792);
    const auto initial = runner.select_action(idle);
    require(static_cast<bool>(initial) && runner.has_pending_proposal() &&
                initial.value->contract_id == idle.contract_id &&
                initial.value->episode_semantic_id == idle.episode_semantic_id &&
                initial.value->public_semantic_decision_id ==
                    idle.public_semantic_decision_id &&
                initial.value->submission_token == idle.submission_token,
            "V3 runner did not select the initial V2 Hiita action");
    const auto initial_key = initial.value->public_action_key;
    commit_selected(runner, idle, initial_key);

    const std::string first_locator = relabel_first_material
                                          ? "p0:MONSTER_ZONE:1"
                                          : "p0:MONSTER_ZONE:0";
    const std::string second_locator = relabel_first_material
                                           ? "p0:MONSTER_ZONE:0"
                                           : "p0:MONSTER_ZONE:1";
    const auto material_a = material_v2(first_locator, PublicCardSelectionOperation::Select);
    const auto material_b = material_v2(second_locator, PublicCardSelectionOperation::Select);
    const auto cancel = cancel_v2();
    const auto initial_material = frame(
        11, EnvironmentDecisionKind::UnselectCard, "unselect_card",
        {material_a, material_b, cancel});
    const auto first_material_selection = runner.select_action(initial_material);
    require(static_cast<bool>(first_material_selection) &&
                first_material_selection.value->contract_id == initial_material.contract_id &&
                first_material_selection.value->submission_token ==
                    initial_material.submission_token,
            "V3 runner did not select the first V2 material");
    const auto first_key = first_material_selection.value->public_action_key;
    const auto pending_first = runner.session(0)->policy.pending_ranking_result();
    require(pending_first.has_value(), "V2 first-material ranking was not retained");
    require(!runner.select_action(initial_material),
            "V3 runner allowed a second selection before commit");
    commit_selected(runner, initial_material, first_key);

    const bool first_is_a = first_key == material_a.public_action_key;
    const auto unselected = material_v2(
        first_is_a ? first_locator : second_locator,
        PublicCardSelectionOperation::Unselect);
    const auto remaining = material_v2(
        first_is_a ? second_locator : first_locator,
        PublicCardSelectionOperation::Select);
    const auto second_material = frame(
        12, EnvironmentDecisionKind::UnselectCard, "unselect_card",
        {unselected, remaining, cancel});
    const auto second_selection = runner.select_action(second_material);
    require(static_cast<bool>(second_selection) &&
                second_selection.value->contract_id == second_material.contract_id &&
                second_selection.value->public_action_key == remaining.public_action_key,
            "V3 runner did not select the remaining V2 material");
    const auto pending_second = runner.session(0)->policy.pending_ranking_result();
    require(pending_second.has_value(), "V2 second-material ranking was not retained");
    commit_selected(runner, second_material, second_selection.value->public_action_key);

    const auto committed_state = runner.session(0)->policy.state();
    require(committed_state.last_accepted_decision_index ==
                std::optional<std::uint64_t>{12} &&
                !runner.has_pending_proposal(),
            "V3 runner did not commit and clear the V2 proposal");
}

void test_rejection_preserves_state_and_cross_participant_rejects() {
    const auto value = fixture();
    auto runner = make_runner(value);
    require(runner.session(2) == nullptr,
            "V3 runner out-of-range session accessor was not fail-closed");
    const auto idle = frame(10, EnvironmentDecisionKind::IdleCommand, "idle_command",
                            {idle_card_v2(1)}, 48815792);
    auto wrong_perspective = idle;
    wrong_perspective.public_observation.perspective_player = 1;
    require(!runner.select_action(wrong_perspective),
            "V3 runner accepted a cross-participant observation");
    const auto selection = runner.select_action(idle);
    require(static_cast<bool>(selection), "V3 rejection fixture could not select");
    const auto before = runner.session(0)->policy.state();
    require(runner.reject_pending_proposal(), "V3 runner did not reject pending proposal");
    require(runner.session(0)->policy.state() == before &&
                !runner.has_pending_proposal(),
            "V3 rejection mutated committed V2 state");
}

void test_public_equivalence() {
    const auto value = fixture();
    auto left = make_runner(value);
    auto right = make_runner(value);
    const auto left_frame = frame(10, EnvironmentDecisionKind::IdleCommand, "idle_command",
                                  {idle_card_v2(1)}, 48815792);
    auto right_frame = left_frame;
    right_frame.public_observation = observation(10, "idle_command", 48815792, 42);
    right_frame.public_observation_digest = public_observation_digest(
        right_frame.public_observation);
    require(left_frame.public_observation_digest == right_frame.public_observation_digest,
            "private metadata changed the public observation digest");
    const auto left_selection = left.select_action(left_frame);
    const auto right_selection = right.select_action(right_frame);
    require(static_cast<bool>(left_selection) && static_cast<bool>(right_selection) &&
                left_selection.value->public_action_key ==
                    right_selection.value->public_action_key,
            "public-equivalent worlds produced different V2 selections");
    const auto left_ranking = left.session(0)->policy.pending_ranking_result();
    const auto right_ranking = right.session(0)->policy.pending_ranking_result();
    require(left_ranking.has_value() && right_ranking.has_value() &&
                left_ranking->selected_public_action_key ==
                    right_ranking->selected_public_action_key &&
                left_ranking->selected_score_vector == right_ranking->selected_score_vector &&
                left_ranking->fallback_level == right_ranking->fallback_level &&
                left_ranking->evaluations.size() == right_ranking->evaluations.size() &&
                left_ranking->proposed_state_delta == right_ranking->proposed_state_delta,
            "public-equivalent worlds produced different V2 ranking state");
}

}  // namespace

int main() {
    try {
        test_v2_session_provenance_and_rng();
        test_runner_revalidates_mutable_session_provenance();
        test_v2_domain_rejection();
        test_hiita_session_lifecycle(false);
        test_hiita_session_lifecycle(true);
        test_rejection_preserves_state_and_cross_participant_rejects();
        test_public_equivalence();
        std::cout << "teacher_runner_v3_policy_session_test: PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "teacher_runner_v3_policy_session_test: " << error.what() << '\n';
        return 1;
    }
}
