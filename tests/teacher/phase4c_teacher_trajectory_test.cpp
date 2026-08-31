#include "ygo/policy/teacher.hpp"
#include "ygo/policy/teacher_runner.hpp"
#include "ygo/teacher/provable_lethal.hpp"
#include "ygo/teacher/public_battle_snapshot.hpp"
#include "ygo/teacher/salamangreat_profile.hpp"
#include "ygo/teacher/swordsoul_tenyi_profile.hpp"
#include "ygo/trajectory/codec.hpp"
#include "ygo/trajectory/dataset_manifest.hpp"
#include "ygo/trajectory/receipt.hpp"
#include "ygo/trajectory/shard.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using namespace ygo::environment;
using namespace ygo::policy;
using namespace ygo::teacher;
using namespace ygo::trajectory;

constexpr std::string_view kSwordsoulProfileId =
    "ocgforge.strategy_profile.v1.7a96ab091b52b8988a6873beb3b7d58575d5ea6f0e0aa7bf5059a1c87a748f74";
constexpr std::string_view kSalamangreatProfileId =
    "ocgforge.strategy_profile.v1.3499e34962230eda64e9ef52af53433272cda5ca45ffae61258e0809dbfefa55";
constexpr std::string_view kRulesBundleId =
    "3adfe6b4cfe2c2805e50b389fc0eb4e70a3b0b6107436614d328fddc865e585f";

void require(const bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

const ParticipantPolicyAssignment& assignment_for_player(
    const std::vector<ParticipantPolicyAssignment>& assignments,
    const std::uint8_t player) {
    for (const auto& assignment : assignments) {
        if (assignment.player == player) {
            return assignment;
        }
    }
    throw std::runtime_error("Teacher assignment is missing a player");
}

TeacherRunnerConfig make_config(const SeatAssignment seat_assignment,
                                const std::uint8_t starting_player) {
    const auto swordsoul = make_swordsoul_tenyi_profile();
    const auto salamangreat = make_salamangreat_profile();
    require(swordsoul.profile_id == kSwordsoulProfileId &&
                salamangreat.profile_id == kSalamangreatProfileId &&
                swordsoul.rules_bundle_id == kRulesBundleId &&
                salamangreat.rules_bundle_id == kRulesBundleId,
            "Teacher runner config did not use the accepted profile identities");

    const auto swordsoul_artifact = make_teacher_policy_artifact(swordsoul);
    const auto salamangreat_artifact = make_teacher_policy_artifact(salamangreat);
    TeacherRunnerConfig config;
    config.environment_config = CertifiedEnvironmentConfig::canonical();
    config.episode_spec.contract_id =
        std::string(kEpisodicEnvironmentV2ContractId);
    config.episode_spec.root_seed = 2;
    config.episode_spec.seat_assignment = seat_assignment;
    config.episode_spec.starting_player = starting_player;
    config.run_control.engine_process_budget = 4096;
    config.run_control.semantic_action_budget = 32;
    config.run_control.cancellation.reason = "ADMINISTRATIVE_CANCEL";
    config.run_control.cancellation.source = "phase4c-evaluation";

    const std::array<PolicyRole, 2> roles = {
        PolicyRole::Behavior, PolicyRole::Opponent};
    const auto assignments = make_teacher_participant_assignments(
        swordsoul_artifact, salamangreat_artifact, config.environment_config,
        seat_assignment, starting_player, roles);
    config.policy_provenance.policy_artifacts = {
        swordsoul_artifact, salamangreat_artifact};
    std::sort(config.policy_provenance.policy_artifacts.begin(),
              config.policy_provenance.policy_artifacts.end(),
              [](const auto& left, const auto& right) {
                  return left.policy_artifact_id < right.policy_artifact_id;
              });
    config.policy_provenance.participant_assignments = assignments;

    for (std::uint8_t player = 0; player < 2; ++player) {
        const auto& assignment = assignment_for_player(assignments, player);
        const auto& profile =
            assignment.deck_role == DeckRole::FirstLockedDeck ? swordsoul
                                                              : salamangreat;
        const auto& artifact =
            assignment.deck_role == DeckRole::FirstLockedDeck
                ? swordsoul_artifact
                : salamangreat_artifact;
        const auto binding = make_teacher_policy_binding(profile);
        auto session = create_teacher_policy_session(
            profile, binding, artifact, assignment);
        require(static_cast<bool>(session),
                "Teacher session creation failed for matrix row");
        config.sessions[player] = std::move(*session.value);
    }
    return config;
}

struct TrustedArtifactBytes final {
    std::vector<std::uint8_t> envelope;
    std::string trajectory_record_id;
    std::string public_gameplay_trajectory_id;
    std::vector<std::uint8_t> candidate_shard;
    std::string candidate_shard_artifact;
    std::vector<std::uint8_t> admission_receipt;
    std::string admission_receipt_id;
    std::vector<std::uint8_t> dataset_manifest;
    std::string dataset_semantic_id;

    bool operator==(const TrustedArtifactBytes& other) const noexcept {
        return envelope == other.envelope &&
               trajectory_record_id == other.trajectory_record_id &&
               public_gameplay_trajectory_id ==
                   other.public_gameplay_trajectory_id &&
               candidate_shard == other.candidate_shard &&
               candidate_shard_artifact == other.candidate_shard_artifact &&
               admission_receipt == other.admission_receipt &&
               admission_receipt_id == other.admission_receipt_id &&
               dataset_manifest == other.dataset_manifest &&
               dataset_semantic_id == other.dataset_semantic_id;
    }
};

TrustedArtifactBytes capture_artifacts(const PolicyRunnerResult& result) {
    require(result.envelope.has_value() && result.candidate_shard.has_value() &&
                result.admission_receipt.has_value() &&
                result.dataset_manifest.has_value(),
            "Teacher runner did not return the complete trusted artifact chain");
    TrustedArtifactBytes output;
    output.envelope = canonical_episode_envelope_bytes(*result.envelope);
    output.trajectory_record_id = trajectory_record_id(*result.envelope);
    output.public_gameplay_trajectory_id =
        public_gameplay_trajectory_id(*result.envelope);
    output.candidate_shard =
        canonical_candidate_trajectory_shard_bytes(*result.candidate_shard);
    output.candidate_shard_artifact =
        candidate_shard_artifact_sha256(*result.candidate_shard);
    output.admission_receipt =
        canonical_admission_receipt_bytes(result.admission_receipt->receipt());
    output.admission_receipt_id =
        admission_receipt_id(result.admission_receipt->receipt());
    output.dataset_manifest =
        dataset::canonical_dataset_manifest_bytes(*result.dataset_manifest);
    output.dataset_semantic_id = result.dataset_manifest->dataset_semantic_id;
    return output;
}

struct RowSummary final {
    std::size_t record_count = 0;
    std::size_t battle_decision_record_count = 0;
    std::size_t battle_command_candidate_count = 0;
    std::size_t sidecar_invalid_count = 0;
    std::size_t proven_lethal_count = 0;
    std::size_t lower_bound_present_count = 0;
};

RowSummary evaluate_sidecar(const PolicyRunnerResult& result) {
    RowSummary summary;
    require(result.disposition == PolicyRunnerDisposition::CleanAdmitted,
            "Teacher row did not reach CleanAdmitted");
    require(result.envelope.has_value(),
            "Teacher row did not return a sealed episode envelope");
    summary.record_count = result.envelope->records.size();
    require(summary.record_count > 0,
            "Teacher row produced no trusted DecisionRecord");

    for (const auto& record : result.envelope->records) {
        const auto& candidates = record.frame.request.candidates;
        const auto snapshot = extract_public_battle_snapshot(
            record.frame.public_observation, candidates);
        require(snapshot.valid &&
                    snapshot.snapshot.candidate_facts.size() ==
                        candidates.size(),
                "post-hoc snapshot sidecar did not preserve the record domain");
        const auto lethal = evaluate_provable_lethal(snapshot.snapshot);
        require(lethal.valid && lethal.candidates.size() == candidates.size(),
                "post-hoc lethal sidecar did not preserve the record domain");

        if (record.frame.request.kind == EnvironmentDecisionKind::BattleCommand) {
            ++summary.battle_decision_record_count;
        }
        const auto selected_count = std::count_if(
            candidates.begin(), candidates.end(), [&](const auto& candidate) {
                return candidate.public_action_key ==
                       record.selected_public_action_key;
            });
        require(selected_count == 1,
                "Teacher selected a key outside or more than once in its domain");

        for (std::size_t index = 0; index < candidates.size(); ++index) {
            require(snapshot.snapshot.candidate_facts[index]
                        .public_action_key == candidates[index].public_action_key &&
                        lethal.candidates[index].public_action_key ==
                            candidates[index].public_action_key,
                    "sidecar changed candidate order or public key");
            require(snapshot.snapshot.candidate_facts[index].status !=
                        PublicBattleCandidateStatus::Invalid,
                    "trusted record produced an INVALID snapshot sidecar fact");

            const auto& lethal_candidate = lethal.candidates[index];
            if (lethal_candidate.status == ProvableLethalStatus::Invalid) {
                ++summary.sidecar_invalid_count;
            }
            if (lethal_candidate.status == ProvableLethalStatus::ProvenLethal) {
                ++summary.proven_lethal_count;
            }
            if (lethal_candidate.guaranteed_opponent_lp_loss_lower_bound
                    .has_value()) {
                ++summary.lower_bound_present_count;
            }
            (void)canonical_provable_lethal_candidate_bytes(lethal_candidate);

            if (candidates[index].action_kind ==
                EnvironmentActionKind::BattleCommand) {
                ++summary.battle_command_candidate_count;
                require(snapshot.snapshot.candidate_facts[index]
                            .battle_candidate_class ==
                            PublicBattleCandidateClass::
                                BattleCommandUnclassified &&
                            lethal_candidate.status ==
                                ProvableLethalStatus::Unsupported &&
                            !lethal_candidate
                                 .guaranteed_opponent_lp_loss_lower_bound
                                 .has_value(),
                        "current BattleCommand sidecar was not fail-closed");
            }
        }
    }
    require(summary.sidecar_invalid_count == 0 &&
                summary.proven_lethal_count == 0 &&
                summary.lower_bound_present_count == 0,
            "trusted sidecar emitted invalid/proven/bounded lethal evidence");
    return summary;
}

void test_fixed_matrix_and_posthoc_sidecar() {
    RowSummary aggregate;
    for (const auto seat_assignment : {SeatAssignment::Normal,
                                       SeatAssignment::Mirror}) {
        for (const auto starting_player : {std::uint8_t{0},
                                           std::uint8_t{1}}) {
            auto config = make_config(seat_assignment, starting_player);
            auto created = TeacherRunner::create(std::move(config));
            require(static_cast<bool>(created),
                    "TeacherRunner creation failed for fixed matrix row");
            const auto result = created.value->run();
            require(result.disposition == PolicyRunnerDisposition::CleanAdmitted,
                    "fixed matrix row was not cleanly admitted");
            const auto before = capture_artifacts(result);
            const auto row = evaluate_sidecar(result);
            const auto after = capture_artifacts(result);
            require(before == after,
                    "post-hoc sidecar changed a trusted artifact or identity");

            aggregate.record_count += row.record_count;
            aggregate.battle_decision_record_count +=
                row.battle_decision_record_count;
            aggregate.battle_command_candidate_count +=
                row.battle_command_candidate_count;
            aggregate.sidecar_invalid_count += row.sidecar_invalid_count;
            aggregate.proven_lethal_count += row.proven_lethal_count;
            aggregate.lower_bound_present_count +=
                row.lower_bound_present_count;
        }
    }

    require(aggregate.battle_decision_record_count > 0 &&
                aggregate.battle_command_candidate_count > 0,
            "fixed matrix produced zero BattleCommand coverage");
    require(aggregate.sidecar_invalid_count == 0 &&
                aggregate.proven_lethal_count == 0 &&
                aggregate.lower_bound_present_count == 0,
            "fixed matrix emitted forbidden positive lethal evidence");
}

}  // namespace

int main() {
    try {
        test_fixed_matrix_and_posthoc_sidecar();
        std::cout << "phase4c_teacher_trajectory_test: PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "phase4c_teacher_trajectory_test: FAIL: "
                  << error.what() << '\n';
        return 1;
    }
}
