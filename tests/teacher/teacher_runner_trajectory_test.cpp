#include "ygo/policy/teacher_runner.hpp"

#include "ygo/policy/teacher.hpp"
#include "ygo/teacher/salamangreat_profile.hpp"
#include "ygo/teacher/swordsoul_tenyi_profile.hpp"
#include "ygo/teacher/teacher_explanation_codec.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "ygo/environment/episodic_environment.hpp"
#include "ygo/environment/public_action_identity.hpp"
#include "ygo/observation/player_observation.hpp"
#include "ygo/trace/sha256.hpp"
#include "ygo/trajectory/codec.hpp"
#include "ygo/trajectory/dataset_manifest.hpp"
#include "ygo/trajectory/receipt.hpp"
#include "ygo/trajectory/shard.hpp"

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
                                                 const std::uint64_t decision_index) {
    ygo::observation::PlayerObservation source;
    source.schema_version = "ygo.player_observation.v1";
    source.perspective_player = participant;
    source.decision_index = decision_index;
    source.globals.life_points = {8000, 7000};
    source.globals.player_to_act = participant;
    source.globals.turn_player = 0;
    source.globals.turn_count = 1;
    source.globals.phase = 0x04;
    source.globals.terminal = false;
    source.match_context.perspective_player = participant;
    source.match_context.knowledge.own_decklist_known = true;
    source.match_context.knowledge.opponent_decklist_known = false;
    source.decision_context.kind = "idle_command";
    source.decision_context.player = participant;
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

const ParticipantPolicyAssignment& assignment_for_player(
    const std::vector<ParticipantPolicyAssignment>& assignments,
    const std::uint8_t player) {
    const auto it = std::find_if(assignments.begin(), assignments.end(),
                                 [player](const auto& assignment) {
                                     return assignment.player == player;
                                 });
    require(it != assignments.end(), "Teacher runner assignment is missing a player");
    return *it;
}

TeacherRunnerConfig make_config(const SeatAssignment seat_assignment,
                                const std::uint8_t starting_player) {
    const auto swordsoul = make_swordsoul_tenyi_profile();
    const auto salamangreat = make_salamangreat_profile();
    const auto swordsoul_artifact = make_teacher_policy_artifact(swordsoul);
    const auto salamangreat_artifact = make_teacher_policy_artifact(salamangreat);
    TeacherRunnerConfig config;
    config.environment_config = CertifiedEnvironmentConfig::canonical();
    config.episode_spec.contract_id = std::string(kEpisodicEnvironmentV2ContractId);
    config.episode_spec.root_seed = 2;
    config.episode_spec.seat_assignment = seat_assignment;
    config.episode_spec.starting_player = starting_player;
    config.run_control.engine_process_budget = 512;
    config.run_control.semantic_action_budget = 1;
    config.run_control.cancellation.reason = "ADMINISTRATIVE_CANCEL";
    config.run_control.cancellation.source = "teacher-runner-test";
    const std::array<PolicyRole, 2> roles = {
        PolicyRole::Behavior, PolicyRole::Opponent};
    const auto assignments = make_teacher_participant_assignments(
        swordsoul_artifact, salamangreat_artifact, config.environment_config,
        seat_assignment, starting_player, roles);
    config.policy_provenance.policy_artifacts = {swordsoul_artifact, salamangreat_artifact};
    std::sort(config.policy_provenance.policy_artifacts.begin(),
              config.policy_provenance.policy_artifacts.end(),
              [](const auto& left, const auto& right) {
                  return left.policy_artifact_id < right.policy_artifact_id;
              });
    config.policy_provenance.participant_assignments = assignments;
    for (std::uint8_t player = 0; player < 2; ++player) {
        const auto& assignment = assignment_for_player(assignments, player);
        const auto& profile = assignment.deck_role == DeckRole::FirstLockedDeck
                                  ? swordsoul
                                  : salamangreat;
        const auto& artifact = assignment.deck_role == DeckRole::FirstLockedDeck
                                   ? swordsoul_artifact
                                   : salamangreat_artifact;
        const auto binding = make_teacher_policy_binding(profile);
        auto session = create_teacher_policy_session(
            profile, binding, artifact, assignment);
        require(static_cast<bool>(session), "Teacher runner session setup failed");
        config.sessions[player] = std::move(*session.value);
    }
    return config;
}

std::string canonical_bytes_hex(const std::vector<std::uint8_t>& bytes) {
    std::ostringstream output;
    output << std::hex << std::setfill('0');
    for (const auto value : bytes) {
        output << std::setw(2) << static_cast<unsigned int>(value);
    }
    return output.str();
}

std::string run_probe_process(const std::string& executable) {
    const std::string command = "\"" + executable + "\" --probe";
    FILE* pipe = _popen(command.c_str(), "r");
    require(pipe != nullptr, "could not start the independent Teacher probe process");
    std::string output;
    char buffer[256];
    while (std::fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        output += buffer;
    }
    const auto status = _pclose(pipe);
    require(status == 0, "independent Teacher probe process failed");
    return output;
}

int run_probe() {
    auto config = make_config(SeatAssignment::Normal, 0);
    auto& session = *config.sessions[0];
    const auto candidates = std::vector<EnvironmentActionCandidate>{public_candidate()};
    const auto selection = session.policy.select(
        PolicyInput{public_observation(0, 0), candidates});
    require(static_cast<bool>(selection) && session.policy.pending_ranking_result().has_value(),
            "independent Teacher probe selection failed");
    const auto ranking = session.policy.pending_ranking_result();
    require(ranking->explanation.has_value() && ranking->proposed_state_delta.has_value(),
            "independent Teacher probe lacks derived values");
    std::cout << "selected=" << ranking->selected_public_action_key.value() << '\n';
    std::cout << "fallback=" << static_cast<unsigned int>(*ranking->fallback_level) << '\n';
    std::cout << "evaluations=" << ranking->evaluations.size() << ':'
              << ranking->evaluations.front().public_action_key << ':'
              << static_cast<unsigned int>(ranking->evaluations.front().status) << '\n';
    std::cout << "explanation=" << canonical_bytes_hex(
        canonical_teacher_decision_explanation_bytes(*ranking->explanation)) << '\n';
    std::cout << "delta_profile=" << ranking->proposed_state_delta->strategy_profile_id << '\n';
    std::cout << "delta_action="
              << ranking->proposed_state_delta->proposed_for_public_action_key << '\n';
    return 0;
}

void test_teacher_runner_trusted_trajectory_path() {
    auto config = make_config(SeatAssignment::Normal, 0);
    auto created = TeacherRunner::create(std::move(config));
    require(static_cast<bool>(created), "Teacher runner construction failed");
    auto result = created.value->run();
    require(result.disposition == PolicyRunnerDisposition::CleanAdmitted,
            "Teacher runner did not cleanly admit its bounded trajectory");
    require(result.envelope.has_value() && result.candidate_shard.has_value() &&
                result.admission_verification.has_value() && result.admission_receipt.has_value() &&
                result.dataset_manifest.has_value(),
            "Teacher runner did not return the complete trusted output chain");
    require(result.envelope->records.size() == 1,
            "bounded Teacher run did not record exactly one accepted action");

    const auto& record = result.envelope->records.front();
    require(record.policy_rng_decision_provenance.mode == PolicyRngMode::None &&
                !record.policy_rng_decision_provenance.pre_cursor.has_value() &&
                !record.policy_rng_decision_provenance.post_cursor.has_value() &&
                !record.policy_rng_decision_provenance.pre_state.has_value() &&
                !record.policy_rng_decision_provenance.post_state.has_value() &&
                record.policy_rng_decision_provenance.policy_rng_identity ==
                    kNoPolicyRngContractId,
            "Teacher DecisionRecord did not carry canonical NONE RNG attribution");
    const auto selected_count = std::count_if(
        record.frame.request.candidates.begin(), record.frame.request.candidates.end(),
        [&](const auto& candidate) {
            return candidate.public_action_key == record.selected_public_action_key;
        });
    require(selected_count == 1,
            "Teacher selected a key outside or more than once in the supplied public domain");

    const auto envelope_bytes = canonical_episode_envelope_bytes(*result.envelope);
    const auto& shard = *result.candidate_shard;
    require(shard.entries.size() == 1 && shard.entries.front().envelope_bytes == envelope_bytes &&
                shard.entries.front().episode_envelope_sha256 ==
                    ygo::trace::sha256_bytes(envelope_bytes),
            "Teacher trajectory shard was not bound to the sealed envelope");
    require(static_cast<bool>(decode_episode_envelope(envelope_bytes)) &&
                static_cast<bool>(decode_candidate_trajectory_shard(
                    canonical_candidate_trajectory_shard_bytes(shard))) &&
                static_cast<bool>(decode_admission_receipt(
                    canonical_admission_receipt_bytes(result.admission_receipt->receipt()))),
            "Teacher trusted artifacts did not pass strict decode");
    std::string manifest_error;
    require(dataset::validate_dataset_manifest(
                *result.dataset_manifest,
                std::vector<VerifiedAdmissionReceipt>{*result.admission_receipt},
                &manifest_error),
            "Teacher dataset manifest failed validation: " + manifest_error);
}

void test_participant_handoff_and_rejection_state() {
    auto config = make_config(SeatAssignment::Normal, 0);
    auto& p0 = *config.sessions[0];
    auto& p1 = *config.sessions[1];
    const auto p0_observation = public_observation(0, 10);
    const std::vector<EnvironmentActionCandidate> candidates = {public_candidate()};
    const PolicyInput p0_input{p0_observation, candidates};
    const auto p0_selection = p0.policy.select(p0_input);
    require(static_cast<bool>(p0_selection) && p0_selection.value->rng_cursor == std::nullopt,
            "Player-0 Teacher proposal failed");
    require(p0.policy.pending_ranking_result().has_value() &&
                p0.policy.pending_ranking_result()->evaluations.size() == candidates.size(),
            "Teacher proposal did not retain exact N evaluation records");
    const auto p0_before = p0.policy.state();

    DecisionFrame p1_next;
    p1_next.acting_player = 1;
    p1_next.public_observation = public_observation(1, 11);
    StepAccepted accepted;
    accepted.transition.decision_index = 10;
    accepted.transition.selected_public_action_key = p0_selection.value->public_action_key;
    accepted.next = p1_next;
    require(p0.policy.commit(accepted.transition),
            "Player-0 accepted transition did not commit");
    require(p0.policy.state().last_accepted_decision_index ==
                std::optional<std::uint64_t>{10} &&
                p0.policy.state().last_accepted_public_action_key ==
                    std::optional<std::string>{p0_selection.value->public_action_key},
            "Player-0 accepted markers were not committed");
    require(!p0.policy.pending_ranking_result().has_value(),
            "accepted Teacher proposal was not cleared");
    require(p1.policy.state().last_accepted_decision_index == std::nullopt,
            "Player-1 state was contaminated by Player-0 acceptance");

    const auto p1_selection = p1.policy.select(
        PolicyInput{public_observation(1, 11), candidates});
    require(static_cast<bool>(p1_selection), "Player-1 did not select from its own frame");
    p1.policy.reject_pending_proposal();
    require(p1.policy.state().last_accepted_decision_index == std::nullopt,
            "Player-1 rejected proposal mutated state");

    const auto p0_after_commit = p0.policy.state();
    const auto p0_later_selection = p0.policy.select(
        PolicyInput{public_observation(0, 12), candidates});
    require(static_cast<bool>(p0_later_selection),
            "Player-0 could not reconcile at its later own frame");
    p0.policy.reject_pending_proposal();
    require(p0.policy.state() == p0_after_commit && p0_before != p0_after_commit,
            "Player-0 later proposal changed trusted state before acceptance");

    const auto p0_rejection_selection = p0.policy.select(
        PolicyInput{public_observation(0, 13), candidates});
    require(static_cast<bool>(p0_rejection_selection), "rejection fixture selection failed");
    const auto before_rejection = p0.policy.state();
    p0.policy.reject_pending_proposal();
    require(p0.policy.state() == before_rejection,
            "policy-origin rejection did not preserve Teacher state");
}

void test_policy_origin_rejection_is_quarantined_without_retry() {
    auto config = make_config(SeatAssignment::Normal, 0);
    auto created = TeacherRunner::create(std::move(config));
    require(static_cast<bool>(created), "Teacher rejection runner construction failed");
    const auto selection_calls = std::make_shared<std::size_t>(0);
    const auto result = ygo::policy::detail::TeacherRunnerTestAccess::run_with_test_selector(
        *created.value, 0,
        ygo::policy::detail::TeacherRunnerTestSelectorBehavior::InvalidPublicAction,
        selection_calls);
    require(result.disposition == PolicyRunnerDisposition::Quarantined,
            "policy-origin Teacher rejection did not quarantine the run");
    require(*selection_calls == 1, "Teacher rejection caused a selection retry");
    require(result.envelope.has_value() && result.envelope->records.empty(),
            "rejected Teacher action created a trajectory record");
    require(!result.admission_verification.has_value() &&
                !result.admission_receipt.has_value() &&
                !result.dataset_manifest.has_value(),
            "quarantined Teacher run reached clean admission outputs");
    require(std::holds_alternative<InterruptedClosure>(result.envelope->closure) &&
                std::get<InterruptedClosure>(result.envelope->closure)
                    .pending_unacted_frame.has_value(),
            "quarantined Teacher run lost its pending public frame");
}

void test_policy_failure_has_no_trusted_outputs() {
    auto config = make_config(SeatAssignment::Normal, 0);
    auto created = TeacherRunner::create(std::move(config));
    require(static_cast<bool>(created), "Teacher policy-failure runner construction failed");
    const auto selection_calls = std::make_shared<std::size_t>(0);
    const auto result = ygo::policy::detail::TeacherRunnerTestAccess::run_with_test_selector(
        *created.value, 0,
        ygo::policy::detail::TeacherRunnerTestSelectorBehavior::PolicyFailure,
        selection_calls);
    require(result.disposition == PolicyRunnerDisposition::Failed && *selection_calls == 1 &&
                result.policy_error.has_value() && !result.envelope.has_value() &&
                !result.admission_receipt.has_value(),
            "Teacher policy failure produced a trusted output or retried");
}

void test_independent_process_determinism(const std::string& executable) {
    const auto first = run_probe_process(executable);
    const auto second = run_probe_process(executable);
    require(!first.empty() && first == second,
            "independent Teacher processes produced different public outputs");
}

}  // namespace

int main(int argc, char** argv) {
    try {
        if (argc == 2 && std::string(argv[1]) == "--probe") {
            return run_probe();
        }
        test_teacher_runner_trusted_trajectory_path();
        test_participant_handoff_and_rejection_state();
        test_policy_origin_rejection_is_quarantined_without_retry();
        test_policy_failure_has_no_trusted_outputs();
        test_independent_process_determinism(argv[0]);
        std::cout << "teacher_runner_trajectory_test: PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "teacher_runner_trajectory_test: FAIL: " << error.what() << '\n';
        return 1;
    }
}
