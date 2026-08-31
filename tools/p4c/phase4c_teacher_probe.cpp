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
#include <string_view>
#include <utility>
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
constexpr std::string_view kSwordsoulBindingId =
    "ocgforge.teacher_policy_binding.v1.4f78a100a75f98b8c5a7845198984a8ea34db8b6a75b6fde396c19d2b3ca6d0c";
constexpr std::string_view kSalamangreatBindingId =
    "ocgforge.teacher_policy_binding.v1.ecbf2ae56dab29e93f319399a08930a3700466cd3d9ab553ef964fc109846c56";
constexpr std::string_view kSwordsoulArtifactId =
    "policy_artifact.v1.52f56b550a2a674430439d3db104a0b2281b69df79891573e4d71967e3d4310d";
constexpr std::string_view kSalamangreatArtifactId =
    "policy_artifact.v1.a68642ee28f0dd53ebe4908994664f178b3d5cea6fb7c06421990729cd9c4527";
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

void verify_record_provenance(
    const DecisionRecord& record,
    const EpisodeEnvelope& envelope,
    const SeatAssignment seat_assignment,
    const std::uint8_t starting_player) {
    const ParticipantPolicyAssignment* assignment = nullptr;
    for (const auto& candidate :
         envelope.manifest.policy_provenance.participant_assignments) {
        if (candidate.participant_policy_assignment_id !=
            record.acting_policy_assignment_id) {
            continue;
        }
        require(assignment == nullptr,
                "record assignment ID resolved to multiple participant assignments");
        assignment = &candidate;
    }
    require(assignment != nullptr,
            "record assignment ID did not resolve to the sealed assignment manifest");
    require(assignment->player == record.frame.acting_player,
            "record assignment player does not match the acting frame player");

    const auto& attribution = record.policy_rng_decision_provenance;
    require(attribution.acting_policy_assignment_id ==
                record.acting_policy_assignment_id &&
                attribution.decision_index == record.frame.decision_index,
            "record policy attribution does not match its frame");

    const auto expected_deck_role =
        seat_assignment == SeatAssignment::Mirror
            ? (assignment->player == 0 ? DeckRole::SecondLockedDeck
                                       : DeckRole::FirstLockedDeck)
            : (assignment->player == 0 ? DeckRole::FirstLockedDeck
                                       : DeckRole::SecondLockedDeck);
    const auto expected_artifact =
        expected_deck_role == DeckRole::FirstLockedDeck ? kSwordsoulArtifactId
                                                         : kSalamangreatArtifactId;
    require(assignment->deck_role == expected_deck_role &&
                assignment->policy_artifact_id == expected_artifact,
            "record assignment deck role or PolicyArtifact attribution changed");
    require(assignment->seat_role ==
                (assignment->player == starting_player
                     ? SeatRole::StartingPlayer
                     : SeatRole::NonStartingPlayer),
            "record assignment seat role does not match the matrix row");

    require(attribution.mode == PolicyRngMode::None &&
                attribution.policy_rng_identity == kNoPolicyRngContractId &&
                attribution.policy_rng_contract_identity ==
                    kNoPolicyRngContractId &&
                attribution.policy_rng_stream_id == kNoPolicyRngContractId &&
                attribution.policy_rng_initialization_identity ==
                    kNoPolicyRngContractId &&
                !attribution.pre_cursor.has_value() &&
                !attribution.post_cursor.has_value() &&
                !attribution.pre_state.has_value() &&
                !attribution.post_state.has_value(),
            "record does not carry canonical NONE policy RNG provenance");
}

TeacherRunnerConfig make_config(const SeatAssignment seat_assignment,
                                const std::uint8_t starting_player) {
    const auto swordsoul = make_swordsoul_tenyi_profile();
    const auto salamangreat = make_salamangreat_profile();
    require(swordsoul.profile_id == kSwordsoulProfileId &&
                salamangreat.profile_id == kSalamangreatProfileId,
            "Teacher probe used an unexpected profile identity");
    require(swordsoul.rules_bundle_id == kRulesBundleId &&
                salamangreat.rules_bundle_id == kRulesBundleId,
            "Teacher probe used an unexpected rules identity");

    const auto swordsoul_artifact = make_teacher_policy_artifact(swordsoul);
    const auto salamangreat_artifact = make_teacher_policy_artifact(salamangreat);
    require(swordsoul_artifact.policy_artifact_id == kSwordsoulArtifactId &&
                salamangreat_artifact.policy_artifact_id == kSalamangreatArtifactId,
            "Teacher probe used an unexpected PolicyArtifact identity");

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
        require(binding.teacher_policy_binding_id ==
                    (assignment.deck_role == DeckRole::FirstLockedDeck
                         ? kSwordsoulBindingId
                         : kSalamangreatBindingId),
                "Teacher probe used an unexpected binding identity");
        auto session = create_teacher_policy_session(
            profile, binding, artifact, assignment);
        require(static_cast<bool>(session),
                "Teacher probe session creation failed");
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
                result.restricted_evidence.has_value() &&
                result.admission_verification.has_value() &&
                result.admission_receipt.has_value() &&
                result.dataset_manifest.has_value(),
            "Teacher probe did not receive the complete trusted result chain");
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
    std::string row;
    std::uint8_t starting_player = 0;
    std::size_t record_count = 0;
    std::size_t battle_decision_record_count = 0;
    std::size_t battle_command_candidate_count = 0;
    std::size_t sidecar_invalid_count = 0;
    std::size_t proven_lethal_count = 0;
    std::size_t lower_bound_present_count = 0;
    TrustedArtifactBytes artifacts;
};

RowSummary run_teacher_row(const SeatAssignment seat_assignment,
                           const std::uint8_t starting_player) {
    auto config = make_config(seat_assignment, starting_player);
    auto created = TeacherRunner::create(std::move(config));
    require(static_cast<bool>(created),
            "TeacherRunner creation failed for evaluation row");
    const auto result = created.value->run();
    require(result.disposition == PolicyRunnerDisposition::CleanAdmitted,
            "Teacher evaluation row was not CleanAdmitted");

    RowSummary summary;
    summary.row = seat_assignment == SeatAssignment::Normal ? "normal" : "mirror";
    summary.starting_player = starting_player;
    const auto artifacts_before = capture_artifacts(result);
    summary.artifacts = artifacts_before;
    summary.record_count = result.envelope->records.size();
    require(summary.record_count > 0,
            "Teacher evaluation row contains no DecisionRecord");

    for (const auto& record : result.envelope->records) {
        verify_record_provenance(record, *result.envelope, seat_assignment,
                                 starting_player);
        const auto& candidates = record.frame.request.candidates;
        const auto snapshot = extract_public_battle_snapshot(
            record.frame.public_observation, candidates);
        require(snapshot.valid &&
                    snapshot.snapshot.candidate_facts.size() ==
                        candidates.size(),
                "Teacher sidecar snapshot is not valid N-to-N evidence");
        const auto lethal = evaluate_provable_lethal(snapshot.snapshot);
        require(lethal.valid && lethal.candidates.size() == candidates.size(),
                "Teacher sidecar lethal result is not valid N-to-N evidence");

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
                    "Teacher sidecar changed candidate order or public key");
            require(snapshot.snapshot.candidate_facts[index].status !=
                        PublicBattleCandidateStatus::Invalid,
                    "trusted Teacher record contains an INVALID snapshot fact");

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
                        "current BattleCommand sidecar was not UNSUPPORTED");
            }
        }
    }

    require(summary.sidecar_invalid_count == 0 &&
                summary.proven_lethal_count == 0 &&
                summary.lower_bound_present_count == 0,
            "Teacher row emitted forbidden lethal sidecar evidence");
    require(artifacts_before == capture_artifacts(result),
            "post-hoc sidecar changed a trusted artifact or identity");
    return summary;
}

std::string row_name(const SeatAssignment seat_assignment) {
    return seat_assignment == SeatAssignment::Normal ? "normal" : "mirror";
}

int run_row(const SeatAssignment seat_assignment,
            const std::uint8_t starting_player) {
    const auto before = run_teacher_row(seat_assignment, starting_player);

    const auto& artifacts = before.artifacts;
    std::cout << "ROW=" << row_name(seat_assignment) << '\n';
    std::cout << "STARTING_PLAYER=" << static_cast<unsigned int>(starting_player)
              << '\n';
    std::cout << "ROOT_SEED=2\n";
    std::cout << "SEMANTIC_ACTION_BUDGET=32\n";
    std::cout << "ENGINE_PROCESS_BUDGET=4096\n";
    std::cout << "MATCHUP_ID=ocgforge.matchup.swordsoul_salamangreat.v1\n";
    std::cout << "RULES_BUNDLE_ID=" << kRulesBundleId << '\n';
    std::cout << "SWORDSOUL_PROFILE_ID=" << kSwordsoulProfileId << '\n';
    std::cout << "SALAMANGREAT_PROFILE_ID=" << kSalamangreatProfileId << '\n';
    std::cout << "SWORDSOUL_BINDING_ID=" << kSwordsoulBindingId << '\n';
    std::cout << "SALAMANGREAT_BINDING_ID=" << kSalamangreatBindingId << '\n';
    std::cout << "SWORDSOUL_POLICY_ARTIFACT_ID=" << kSwordsoulArtifactId << '\n';
    std::cout << "SALAMANGREAT_POLICY_ARTIFACT_ID=" << kSalamangreatArtifactId
              << '\n';
    std::cout << "DISPOSITION=CLEAN_ADMITTED\n";
    std::cout << "RECORD_COUNT=" << before.record_count << '\n';
    std::cout << "BATTLE_DECISION_RECORD_COUNT="
              << before.battle_decision_record_count << '\n';
    std::cout << "BATTLE_COMMAND_CANDIDATE_COUNT="
              << before.battle_command_candidate_count << '\n';
    std::cout << "SIDECAR_INVALID_COUNT=" << before.sidecar_invalid_count << '\n';
    std::cout << "PROVEN_LETHAL_COUNT=" << before.proven_lethal_count << '\n';
    std::cout << "LOWER_BOUND_PRESENT_COUNT="
              << before.lower_bound_present_count << '\n';
    std::cout << "PUBLIC_GAMEPLAY_TRAJECTORY_ID="
              << artifacts.public_gameplay_trajectory_id << '\n';
    std::cout << "TRAJECTORY_RECORD_ID=" << artifacts.trajectory_record_id << '\n';
    std::cout << "DATASET_SEMANTIC_ID=" << artifacts.dataset_semantic_id << '\n';
    std::cout << "SIDECAR_INFLUENCES_GAMEPLAY=NO\n";
    std::cout << "POSITIVE_LETHAL_CAPABILITY="
                 "BLOCKED_BY_ACCEPTED_CURRENT_ACTION_CONTRACT\n";
    std::cout << "ROW_STATUS=PASS\n";
    return 0;
}

}  // namespace

int main(const int argc, char** argv) {
    try {
        if (argc != 4 || std::string_view(argv[1]) != "--row") {
            throw std::runtime_error(
                "usage: phase4c_teacher_probe --row normal|mirror 0|1");
        }
        const std::string_view seat(argv[2]);
        const std::string_view start(argv[3]);
        const auto seat_assignment =
            seat == "normal"
                ? SeatAssignment::Normal
                : seat == "mirror" ? SeatAssignment::Mirror
                                    : static_cast<SeatAssignment>(255);
        if (start != "0" && start != "1") {
            throw std::runtime_error("starting player must be 0 or 1");
        }
        if (static_cast<std::uint8_t>(seat_assignment) > 1) {
            throw std::runtime_error("row must be normal or mirror");
        }
        return run_row(seat_assignment,
                       static_cast<std::uint8_t>(start[0] - '0'));
    } catch (const std::exception& error) {
        std::cerr << "phase4c_teacher_probe: " << error.what() << '\n';
        return 1;
    }
}
