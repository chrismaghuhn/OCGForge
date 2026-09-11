#include "episodic_environment_test_access.hpp"
#include "ocgapi_constants.h"
#include "ygo/environment/episodic_environment.hpp"
#include "ygo/environment/public_action_identity.hpp"
#include "ygo/environment/public_safe_state.hpp"
#include "ygo/observation/decision_integration.hpp"
#include "ygo/observation/player_observation.hpp"
#include "ygo/observation/serialization.hpp"
#include "ygo/policy/teacher.hpp"
#include "ygo/policy/teacher_runner_v4_trajectory.hpp"
#include "ygo/policy/teacher_v3.hpp"
#include "ygo/teacher/salamangreat_profile.hpp"
#include "ygo/teacher/swordsoul_tenyi_profile.hpp"
#include "ygo/protocol/action_candidate.hpp"
#include "ygo/protocol/message_decoder.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <iterator>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using namespace ygo::environment;
using namespace ygo::policy;
using namespace ygo::trajectory;

constexpr std::uint64_t kRootSeed = 4;
constexpr std::uint64_t kDecisionLimit = 236;
constexpr std::uint32_t kHiitaPasscode = 48815792;
constexpr std::string_view kHiitaLocator =
    "p1:EXTRA_DECK:public:48815792:0";

void require(const bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

struct Fixture final {
    CertifiedEnvironmentConfig environment_config =
        CertifiedEnvironmentConfig::canonical_v4();
    EpisodeSpec episode_spec;
    RunControl run_control;
    PolicyProvenanceEnvelope policy_provenance;
    TeacherRunnerV4Config runner_config;
};

const ParticipantPolicyAssignment& assignment_for(
    const std::vector<ParticipantPolicyAssignment>& assignments,
    const std::uint8_t player) {
    const auto found = std::find_if(
        assignments.begin(), assignments.end(),
        [player](const auto& value) { return value.player == player; });
    require(found != assignments.end(), "V4 fixture lacks a participant assignment");
    return *found;
}

Fixture fixture() {
    Fixture result;
    result.episode_spec.contract_id = std::string(kEpisodicEnvironmentV4ContractId);
    result.episode_spec.root_seed = kRootSeed;
    result.episode_spec.seat_assignment = SeatAssignment::Normal;
    result.episode_spec.starting_player = 0;
    result.run_control.engine_process_budget = 4096;
    result.run_control.semantic_action_budget = 4096;
    result.run_control.cancellation.reason = "ADMINISTRATIVE_CANCEL";
    result.run_control.cancellation.source = "task8-hiita-reproducer";

    const auto swordsoul = ygo::teacher::make_swordsoul_tenyi_profile();
    const auto salamangreat = ygo::teacher::make_salamangreat_profile();
    const auto swordsoul_artifact = make_teacher_policy_artifact_v3(swordsoul);
    const auto salamangreat_artifact = make_teacher_policy_artifact_v3(salamangreat);
    result.policy_provenance.policy_artifacts = {
        swordsoul_artifact, salamangreat_artifact};
    std::sort(result.policy_provenance.policy_artifacts.begin(),
              result.policy_provenance.policy_artifacts.end(),
              [](const auto& left, const auto& right) {
                  return left.policy_artifact_id < right.policy_artifact_id;
              });
    result.policy_provenance.participant_assignments =
        make_teacher_participant_assignments(
            swordsoul_artifact, salamangreat_artifact, result.environment_config,
            result.episode_spec.seat_assignment, result.episode_spec.starting_player,
            {PolicyRole::Behavior, PolicyRole::Opponent});

    for (std::uint8_t player = 0; player < 2; ++player) {
        const auto& assignment = assignment_for(
            result.policy_provenance.participant_assignments, player);
        const auto& profile = assignment.deck_role == DeckRole::FirstLockedDeck
                                  ? swordsoul
                                  : salamangreat;
        const auto& artifact = assignment.deck_role == DeckRole::FirstLockedDeck
                                   ? swordsoul_artifact
                                   : salamangreat_artifact;
        const auto binding = make_teacher_policy_binding_v3(profile);
        auto session = create_teacher_policy_session_v3(
            profile, binding, artifact, assignment);
        require(static_cast<bool>(session), "V3 Teacher session creation failed");
        result.runner_config.sessions[player] = std::move(*session.value);
    }
    return result;
}

bool exposes_hiita(const ygo::trajectory::DecisionRecordV3& record) {
    const auto safe = decode_canonical_public_safe_state(
        record.frame.public_observation.canonical_safe_state_bytes());
    if (!safe) return false;
    const auto entity = std::find_if(
        safe.value->entities().begin(), safe.value->entities().end(),
        [](const auto& value) {
            return value.locator.value == kHiitaLocator && value.identity_known &&
                   value.passcode.has_value() && *value.passcode == kHiitaPasscode;
        });
    return entity != safe.value->entities().end();
}

void append_u32(std::vector<std::uint8_t>& bytes, const std::uint32_t value) {
    for (unsigned shift = 0; shift < 32; shift += 8) {
        bytes.push_back(static_cast<std::uint8_t>((value >> shift) & 0xffu));
    }
}

void append_card(std::vector<std::uint8_t>& payload,
                 const std::uint32_t code,
                 const std::uint8_t controller,
                 const std::uint8_t location,
                 const std::uint32_t sequence) {
    append_u32(payload, code);
    payload.push_back(controller);
    payload.push_back(location);
    append_u32(payload, sequence);
    append_u32(payload, 0);
}

std::vector<std::uint8_t> framed(const std::vector<std::uint8_t>& payload) {
    std::vector<std::uint8_t> result;
    append_u32(result, static_cast<std::uint32_t>(payload.size()));
    result.insert(result.end(), payload.begin(), payload.end());
    return result;
}

std::vector<std::uint8_t> select_unselect_payload() {
    std::vector<std::uint8_t> payload = {MSG_SELECT_UNSELECT_CARD, 0, 1, 0};
    append_u32(payload, 0);
    append_u32(payload, 2);
    append_u32(payload, 1);
    append_card(payload, 801, 0, LOCATION_HAND, 0);
    append_u32(payload, 1);
    append_card(payload, 802, 1, LOCATION_HAND, 1);
    return payload;
}

ygo::observation::PlayerObservation observation_for(
    const ygo::protocol::DecisionRequest& request) {
    ygo::observation::PlayerObservation observation;
    observation.schema_version = "ygo.player_observation.v1";
    observation.perspective_player = 0;
    observation.engine_step_index = request.engine_step_index;
    observation.globals.life_points = {8000, 8000};
    observation.match_context.perspective_player = 0;
    observation.match_context.duel_flags = 190464;
    for (const auto [code, sequence] :
         {std::pair<std::uint32_t, std::uint32_t>{801, 0},
          std::pair<std::uint32_t, std::uint32_t>{802, 1}}) {
        ygo::observation::ObservedCard card;
        card.locator = {"p0:HAND:" + std::to_string(sequence)};
        card.identity_known = true;
        card.passcode = code;
        card.owner = 0;
        card.controller = 0;
        card.zone = ygo::observation::SemanticZone::Hand;
        card.sequence = sequence;
        card.face_up = true;
        observation.entities.push_back(std::move(card));
    }
    ygo::observation::attach_decision_context(observation, request);
    observation.observation_hash = ygo::observation::observation_hash(observation);
    return observation;
}

bool equal_choice(const std::optional<PublicChoice>& left,
                  const std::optional<PublicChoice>& right) {
    return left.has_value() == right.has_value() &&
           (!left.has_value() || (left->kind == right->kind &&
                                  left->value == right->value &&
                                  left->response_index == right->response_index));
}

bool equal_reference(const std::optional<PublicCardReference>& left,
                     const std::optional<PublicCardReference>& right) {
    return left.has_value() == right.has_value() &&
           (!left.has_value() || (left->kind == right->kind &&
                                  left->observation_locator == right->observation_locator));
}

void require_public_candidate_equal_except_generation(
    const EnvironmentActionCandidate& historical,
    const EnvironmentActionCandidate& corrected) {
    require(historical.action_kind == corrected.action_kind &&
                equal_choice(historical.choice, corrected.choice) &&
                equal_reference(historical.source_reference, corrected.source_reference) &&
                equal_reference(historical.target_reference, corrected.target_reference) &&
                historical.phase == corrected.phase &&
                historical.position == corrected.position &&
                historical.source_index == corrected.source_index &&
                historical.amount == corrected.amount &&
                historical.continuation_operation == corrected.continuation_operation &&
                historical.submits_engine_response == corrected.submits_engine_response,
            "V3/V4 public candidate changed outside generation semantics");
}

void require_public_projection_equivalence() {
    const auto bytes = framed(select_unselect_payload());
    const auto historical_request = ygo::protocol::decode_messages(bytes).decisions.front();
    const auto corrected_request = ygo::protocol::decode_messages_v4(bytes).decisions.front();
    auto historical_factory = EpisodicEnvironment::create(
        CertifiedEnvironmentConfig::canonical_v3());
    auto corrected_factory = EpisodicEnvironment::create(
        CertifiedEnvironmentConfig::canonical_v4());
    require(std::holds_alternative<std::unique_ptr<EpisodicEnvironment>>(historical_factory) &&
                std::holds_alternative<std::unique_ptr<EpisodicEnvironment>>(corrected_factory),
            "public-equivalence environment fixtures could not be created");
    auto historical_environment = std::move(
        std::get<std::unique_ptr<EpisodicEnvironment>>(historical_factory));
    auto corrected_environment = std::move(
        std::get<std::unique_ptr<EpisodicEnvironment>>(corrected_factory));
    const auto historical_frame =
        ygo::environment::detail::EpisodicEnvironmentTestAccess::project_frame_for_test(
            *historical_environment, historical_request, observation_for(historical_request),
            std::string(64, 'a'), 0);
    const auto corrected_frame =
        ygo::environment::detail::EpisodicEnvironmentTestAccess::project_frame_for_test(
            *corrected_environment, corrected_request, observation_for(corrected_request),
            std::string(64, 'a'), 0);
    require(historical_frame.public_observation.canonical_safe_state_bytes() ==
                corrected_frame.public_observation.canonical_safe_state_bytes() &&
                historical_frame.public_observation_digest ==
                    corrected_frame.public_observation_digest &&
                historical_frame.request.candidates.size() ==
                    corrected_frame.request.candidates.size(),
            "historical/corrected public observation or domain shape changed");
    for (std::size_t index = 0; index < historical_frame.request.candidates.size(); ++index) {
        require_public_candidate_equal_except_generation(
            historical_frame.request.candidates[index],
            corrected_frame.request.candidates[index]);
    }
}

void require_decoder_response_equivalence() {
    const auto bytes = framed(select_unselect_payload());
    const auto historical = ygo::protocol::decode_messages(bytes, 17);
    const auto corrected = ygo::protocol::decode_messages_v4(bytes, 17);
    require(historical.decisions.size() == 1 && corrected.decisions.size() == 1,
            "historical/corrected decoder did not emit one decision");
    const auto& old_request = historical.decisions.front();
    const auto& new_request = corrected.decisions.front();
    require(old_request.candidates.size() == new_request.candidates.size() &&
                old_request.candidates.size() == 3,
            "historical/corrected decoder changed candidate membership");
    for (std::size_t index = 0; index < old_request.candidates.size(); ++index) {
        const auto& old_candidate = old_request.candidates[index];
        const auto& new_candidate = new_request.candidates[index];
        require(old_candidate.action_kind == new_candidate.action_kind &&
                    old_candidate.semantic_key == new_candidate.semantic_key &&
                    old_candidate.source_index == new_candidate.source_index &&
                    old_candidate.exact_response_bytes == new_candidate.exact_response_bytes,
                "historical/corrected candidate changed outside operation semantics");
    }
    require(old_request.candidates[0].card_selection_operation ==
                ygo::protocol::CardSelectionOperation::Unselect &&
                old_request.candidates[1].card_selection_operation ==
                    ygo::protocol::CardSelectionOperation::Select &&
                new_request.candidates[0].card_selection_operation ==
                    ygo::protocol::CardSelectionOperation::Select &&
                new_request.candidates[1].card_selection_operation ==
                    ygo::protocol::CardSelectionOperation::Unselect &&
                new_request.candidates[2].card_selection_operation ==
                    ygo::protocol::CardSelectionOperation::None,
            "historical/corrected operation mapping is not the frozen direction");
}

void test_v4_environment_exposes_corrected_hiita_boundary() {
    auto value = fixture();
    auto created = TeacherRunnerV4TrajectoryRunner::create(
        TeacherRunnerV4TrajectoryConfig{value.environment_config, value.episode_spec,
                                        value.run_control, value.policy_provenance,
                                        std::move(value.runner_config)});
    require(static_cast<bool>(created), "V4 trajectory runner creation failed");
    const auto result = ygo::policy::detail::TeacherRunnerV4TrajectoryTestAccess::run_until_decision(
        *created.value, kDecisionLimit);
    require(static_cast<bool>(result) && result.envelope.has_value(),
            "V4 bounded prefix did not seal a V3 envelope: " + result.diagnostic);

    const auto& records = result.envelope->records;
    const auto hiita_it = std::find_if(
        records.begin(), records.end(), [](const auto& record) {
            if (!exposes_hiita(record) ||
                record.frame.request.kind != EnvironmentDecisionKind::IdleCommand) {
                return false;
            }
            const auto candidate = std::find_if(
                record.frame.request.candidates.begin(),
                record.frame.request.candidates.end(),
                [](const auto& value) {
                    return value.action_kind == EnvironmentActionKind::IdleCommand &&
                           value.source_reference.has_value() &&
                           value.source_reference->kind == PublicCardReferenceKind::VisibleCard &&
                           value.source_reference->observation_locator == kHiitaLocator;
                });
            return candidate != record.frame.request.candidates.end() &&
                   candidate->public_action_key == record.selected_public_action_key;
        });
    require(hiita_it != records.end(),
            "V4 bounded prefix did not select public Hiita from IdleCommand");
    const auto material_it = std::next(hiita_it);
    require(material_it != records.end() &&
                material_it->frame.decision_index == hiita_it->frame.decision_index + 1 &&
                material_it->frame.request.kind == EnvironmentDecisionKind::UnselectCard,
            "Hiita selection was not immediately followed by UnselectCard");

    const auto& hiita = *hiita_it;
    const auto& material = *material_it;
    require(public_observation_digest(hiita.frame.public_observation) ==
                hiita.frame.public_observation_digest &&
                public_observation_digest(material.frame.public_observation) ==
                    material.frame.public_observation_digest,
            "V4 public observation digest was not self-consistent");
    std::vector<std::string> keys;
    std::size_t select_count = 0;
    std::size_t cancel_count = 0;
    std::size_t unselect_count = 0;
    for (const auto& candidate : material.frame.request.candidates) {
        require(is_public_action_key_v3(candidate.public_action_key),
                "first material domain contained a non-V3 public action key");
        keys.push_back(candidate.public_action_key);
        if (candidate.action_kind == EnvironmentActionKind::CardSelection) {
            require(candidate.card_selection_operation ==
                        PublicCardSelectionOperation::Select,
                    "first material CardSelection was not Select");
            ++select_count;
        } else if (candidate.action_kind == EnvironmentActionKind::Cancel) {
            require(candidate.card_selection_operation ==
                        PublicCardSelectionOperation::None,
                    "first material Cancel was not None");
            ++cancel_count;
        } else {
            throw std::runtime_error(
                "first material domain contained an unexpected action kind");
        }
        if (candidate.card_selection_operation ==
            PublicCardSelectionOperation::Unselect) {
            ++unselect_count;
        }
    }
    require(select_count == 2 && cancel_count == 1 && unselect_count == 0 &&
                material.frame.request.candidates.size() == 3,
            "first material domain was not exactly two Selects plus Cancel");
    require(material.frame.public_candidate_domain_digest ==
                public_candidate_domain_digest_v3("unselect_card", keys),
            "first material domain digest was not recomputed from ordered V3 keys");
    PublicSemanticDecisionIdentityInputV3 decision;
    decision.episode_semantic_id = material.frame.episode_semantic_id;
    decision.decision_index = material.frame.decision_index;
    decision.acting_player = material.frame.acting_player;
    decision.request_kind = "unselect_card";
    decision.public_observation_digest = material.frame.public_observation_digest;
    decision.public_candidate_domain_digest = material.frame.public_candidate_domain_digest;
    decision.public_action_keys = keys;
    require(material.frame.public_semantic_decision_id ==
                public_semantic_decision_id_v3(decision),
            "first material public decision identity was not recomputed exactly");

    const auto selected = std::find_if(
        material.frame.request.candidates.begin(),
        material.frame.request.candidates.end(),
        [&](const auto& candidate) {
            return candidate.public_action_key == material.selected_public_action_key;
        });
    require(selected != material.frame.request.candidates.end() &&
                selected->action_kind == EnvironmentActionKind::CardSelection &&
                selected->card_selection_operation == PublicCardSelectionOperation::Select,
            "V4 environment selected Cancel instead of a Select material");
    require(hiita.successor.kind == SuccessorKind::NextFrame &&
                hiita.successor.next_frame.has_value() &&
                hiita.successor.next_frame->next_decision_index == material.frame.decision_index &&
                material.successor.kind == SuccessorKind::NextFrame &&
                material.successor.next_frame.has_value(),
            "corrected Hiita path did not publish a forward successor");

    std::cout << "HIITA_DECISION_INDEX=" << hiita.frame.decision_index << '\n'
              << "HIITA_PUBLIC_ACTION_KEY=" << hiita.selected_public_action_key << '\n'
              << "FIRST_MATERIAL_DECISION_INDEX=" << material.frame.decision_index << '\n'
              << "FIRST_MATERIAL_DOMAIN_DIGEST=" <<
                     material.frame.public_candidate_domain_digest << '\n'
              << "FIRST_MATERIAL_CARD_SELECTION_COUNT=" << select_count << '\n'
              << "FIRST_MATERIAL_CANCEL_COUNT=" << cancel_count << '\n'
              << "FIRST_MATERIAL_UNSELECT_COUNT=" << unselect_count << '\n';
}

}  // namespace

int main() {
    try {
        require_decoder_response_equivalence();
        require_public_projection_equivalence();
        test_v4_environment_exposes_corrected_hiita_boundary();
        std::cout << "episodic_environment_v4_hiita_reproducer_test: PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "episodic_environment_v4_hiita_reproducer_test: "
                  << error.what() << '\n';
        return 1;
    }
}
