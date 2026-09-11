#include "ygo/policy/teacher.hpp"
#include "ygo/policy/teacher_v3.hpp"
#include "ygo/policy/teacher_runner_v4_trajectory.hpp"
#include "ygo/environment/public_safe_state.hpp"
#include "ygo/trajectory/codec_v3.hpp"
#include "ygo/trajectory/trajectory_identity_v3.hpp"
#include "ygo/teacher/salamangreat_profile.hpp"
#include "ygo/teacher/swordsoul_tenyi_profile.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <iterator>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

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

Fixture fixture(const std::uint64_t semantic_action_budget = 1,
                const std::uint64_t engine_process_budget = 512,
                const std::uint64_t root_seed = 2) {
    Fixture result;
    result.episode_spec.contract_id = std::string(kEpisodicEnvironmentV4ContractId);
    result.episode_spec.root_seed = root_seed;
    result.run_control.engine_process_budget = engine_process_budget;
    result.run_control.semantic_action_budget = semantic_action_budget;
    result.run_control.cancellation.reason = "ADMINISTRATIVE_CANCEL";
    result.run_control.cancellation.source = "v4-runner-test";

    const auto swordsoul = make_swordsoul_tenyi_profile();
    const auto salamangreat = make_salamangreat_profile();
    const auto swordsoul_artifact = make_teacher_policy_artifact_v3(swordsoul);
    const auto salamangreat_artifact = make_teacher_policy_artifact_v3(salamangreat);
    result.policy_provenance.policy_artifacts = {
        swordsoul_artifact, salamangreat_artifact};
    std::sort(result.policy_provenance.policy_artifacts.begin(),
              result.policy_provenance.policy_artifacts.end(),
              [](const auto& left, const auto& right) {
                  return left.policy_artifact_id < right.policy_artifact_id;
              });
    result.policy_provenance.participant_assignments = make_teacher_participant_assignments(
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

void test_v4_runner_produces_v3_envelope() {
    auto value = fixture();
    auto created = TeacherRunnerV4TrajectoryRunner::create(
        TeacherRunnerV4TrajectoryConfig{value.environment_config, value.episode_spec,
                                        value.run_control, value.policy_provenance,
                                        std::move(value.runner_config)});
    require(static_cast<bool>(created), "V4 trajectory runner creation failed");
    const auto result = created.value->run();
    require(static_cast<bool>(result) && result.envelope.has_value(),
            "V4 runner did not produce a V3 envelope: " + result.diagnostic);
    require(result.envelope->manifest.episodic_environment_contract_id ==
                kEpisodicEnvironmentV4ContractId,
            "V4 runner emitted the wrong environment generation");
    require(!result.envelope->records.empty(),
            "V4 runner did not record an accepted public transition");
    for (const auto& record : result.envelope->records) {
        require(is_public_action_key_v3(record.selected_public_action_key),
                "V4 runner selected a non-V3 public key");
    }
    const auto bytes = canonical_episode_envelope_bytes_v3(*result.envelope);
    require(!bytes.empty(), "V4 runner returned an empty V3 envelope");
    require(public_gameplay_trajectory_id_v3(*result.envelope).rfind(
                "public_gameplay_trajectory.v3.", 0) == 0 &&
                trajectory_record_id_v3(*result.envelope).rfind(
                    "trajectory_record.v3.", 0) == 0,
            "V4 runner returned non-V3 trajectory identities");
}

void test_v3_runner_rejects_historical_environment() {
    auto value = fixture();
    value.environment_config = CertifiedEnvironmentConfig::canonical_v3();
    value.episode_spec.contract_id = std::string(kEpisodicEnvironmentV3ContractId);
    const auto created = TeacherRunnerV4TrajectoryRunner::create(
        TeacherRunnerV4TrajectoryConfig{value.environment_config, value.episode_spec,
                                        value.run_control, value.policy_provenance,
                                        std::move(value.runner_config)});
    require(!static_cast<bool>(created),
            "V4 runner accepted a historical environment generation");
}

void test_v4_runner_reaches_corrected_unselect_boundary() {
    auto value = fixture(256, 4096, 4);
    auto created = TeacherRunnerV4TrajectoryRunner::create(
        TeacherRunnerV4TrajectoryConfig{value.environment_config, value.episode_spec,
                                        value.run_control, value.policy_provenance,
                                        std::move(value.runner_config)});
    require(static_cast<bool>(created), "V4 prefix runner creation failed");
    auto result = ygo::policy::detail::TeacherRunnerV4TrajectoryTestAccess::run_until_decision(
        *created.value, 236);
    require(static_cast<bool>(result) && result.envelope.has_value(),
            "V4 corrected prefix did not seal a V3 envelope: " + result.diagnostic);

    constexpr std::string_view hiita_locator =
        "p1:EXTRA_DECK:public:48815792:0";
    const auto exposes_hiita = [](const auto& record) {
        const auto decoded = decode_canonical_public_safe_state(
            record.frame.public_observation.canonical_safe_state_bytes());
        if (!decoded) {
            return false;
        }
        const auto entity = std::find_if(
            decoded.value->entities().begin(), decoded.value->entities().end(),
            [](const auto& value) {
                return value.locator.value ==
                           "p1:EXTRA_DECK:public:48815792:0" &&
                       value.identity_known && value.passcode.has_value() &&
                       *value.passcode == 48815792;
            });
        return entity != decoded.value->entities().end();
    };

    const auto& records = result.envelope->records;
    const auto hiita_it = std::find_if(
        records.begin(), records.end(), [&](const auto& record) {
            if (record.frame.request.kind != EnvironmentDecisionKind::IdleCommand ||
                !exposes_hiita(record)) {
                return false;
            }
            const auto candidate = std::find_if(
                record.frame.request.candidates.begin(),
                record.frame.request.candidates.end(),
                [](const auto& value) {
                    return value.action_kind == EnvironmentActionKind::IdleCommand &&
                           value.source_reference.has_value() &&
                           value.source_reference->kind ==
                               PublicCardReferenceKind::VisibleCard &&
                           value.source_reference->observation_locator ==
                               "p1:EXTRA_DECK:public:48815792:0";
                });
            return candidate != record.frame.request.candidates.end() &&
                   candidate->source_reference->observation_locator == hiita_locator &&
                   candidate->public_action_key == record.selected_public_action_key;
        });
    require(hiita_it != records.end(),
            "V4 corrected prefix did not select public Hiita 48815792");
    require(std::next(hiita_it) != records.end(),
            "V4 Hiita selection has no following decision record");

    const auto& hiita_record = *hiita_it;
    const auto& material_record = *std::next(hiita_it);
    require(material_record.frame.decision_index ==
                hiita_record.frame.decision_index + 1 &&
                material_record.frame.request.kind == EnvironmentDecisionKind::UnselectCard,
            "V4 Hiita selection was not immediately followed by UnselectCard");

    const auto selected_material = std::find_if(
        material_record.frame.request.candidates.begin(),
        material_record.frame.request.candidates.end(),
        [&](const auto& candidate) {
            return candidate.public_action_key == material_record.selected_public_action_key;
        });
    require(selected_material != material_record.frame.request.candidates.end(),
            "V4 material record selected an absent public key");
    require(selected_material->action_kind == EnvironmentActionKind::CardSelection &&
                selected_material->card_selection_operation ==
                    PublicCardSelectionOperation::Select,
            "V4 corrected material selection did not select a material candidate");

    std::size_t select_candidates = 0;
    std::size_t cancel_candidates = 0;
    for (const auto& candidate : material_record.frame.request.candidates) {
        if (candidate.action_kind == EnvironmentActionKind::CardSelection) {
            require(candidate.card_selection_operation ==
                        PublicCardSelectionOperation::Select,
                    "V4 material domain contained a non-Select card operation");
            ++select_candidates;
        } else if (candidate.action_kind == EnvironmentActionKind::Cancel) {
            require(candidate.card_selection_operation ==
                        PublicCardSelectionOperation::None,
                    "V4 Cancel candidate carried Select/Unselect metadata");
            ++cancel_candidates;
        } else {
            throw std::runtime_error(
                "V4 material domain contained an unexpected action kind");
        }
    }
    require(select_candidates == 2 && cancel_candidates == 1,
            "V4 corrected material domain was not exactly two Selects plus Cancel");
    require(selected_material->action_kind == EnvironmentActionKind::CardSelection,
            "V4 Teacher selected Cancel instead of a material candidate");

    require(hiita_record.successor.kind == SuccessorKind::NextFrame &&
                hiita_record.successor.next_frame.has_value() &&
                hiita_record.successor.next_frame->kind ==
                    NextFrameTargetKind::NextDecisionRecord &&
                hiita_record.successor.next_frame->next_decision_index ==
                    material_record.frame.decision_index &&
                hiita_record.successor.next_frame->next_public_semantic_decision_id ==
                    material_record.frame.public_semantic_decision_id,
            "V4 Hiita selection did not produce an accepted successor");
    require(material_record.successor.kind == SuccessorKind::NextFrame &&
                material_record.successor.next_frame.has_value() &&
                material_record.successor.next_frame->kind ==
                    NextFrameTargetKind::InterruptionPendingUnactedFrame &&
                material_record.successor.next_frame->next_decision_index ==
                    material_record.frame.decision_index + 1,
            "V4 accepted material selection lacks the pending successor boundary");
}

}  // namespace

int main() {
    try {
        test_v4_runner_produces_v3_envelope();
        test_v3_runner_rejects_historical_environment();
        test_v4_runner_reaches_corrected_unselect_boundary();
        std::cout << "teacher runner V4 trajectory tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
