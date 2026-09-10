#include "ygo/policy/teacher.hpp"
#include "ygo/policy/teacher_v3.hpp"
#include "ygo/policy/teacher_runner_v4_trajectory.hpp"
#include "ygo/trajectory/codec_v3.hpp"
#include "ygo/trajectory/trajectory_identity_v3.hpp"
#include "ygo/teacher/salamangreat_profile.hpp"
#include "ygo/teacher/swordsoul_tenyi_profile.hpp"

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
                const std::uint64_t engine_process_budget = 512) {
    Fixture result;
    result.episode_spec.contract_id = std::string(kEpisodicEnvironmentV4ContractId);
    result.episode_spec.root_seed = 2;
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
    auto value = fixture(256, 4096);
    auto created = TeacherRunnerV4TrajectoryRunner::create(
        TeacherRunnerV4TrajectoryConfig{value.environment_config, value.episode_spec,
                                        value.run_control, value.policy_provenance,
                                        std::move(value.runner_config)});
    require(static_cast<bool>(created), "V4 prefix runner creation failed");
    auto result = ygo::policy::detail::TeacherRunnerV4TrajectoryTestAccess::run_until_decision(
        *created.value, 236);
    require(static_cast<bool>(result) && result.envelope.has_value(),
            "V4 corrected prefix did not seal a V3 envelope: " + result.diagnostic);

    bool saw_unselect = false;
    bool saw_selected_select = false;
    for (const auto& record : result.envelope->records) {
        if (record.frame.request.kind != EnvironmentDecisionKind::UnselectCard) {
            continue;
        }
        saw_unselect = true;
        const auto selected = std::find_if(
            record.frame.request.candidates.begin(),
            record.frame.request.candidates.end(),
            [&](const auto& candidate) {
                return candidate.public_action_key == record.selected_public_action_key;
            });
        require(selected != record.frame.request.candidates.end(),
                "V4 unselect record selected an absent public key");
        if (selected->action_kind == EnvironmentActionKind::CardSelection &&
            selected->card_selection_operation == PublicCardSelectionOperation::Select) {
            saw_selected_select = true;
        }
    }
    require(saw_unselect && saw_selected_select,
            "V4 corrected prefix did not accept a public Select continuation");
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
