#include "ygo/environment/episodic_environment.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "ygo/observation/serialization.hpp"
#include "ygo/policy/teacher_runner.hpp"
#include "ygo/teacher/salamangreat_profile.hpp"
#include "ygo/teacher/swordsoul_tenyi_profile.hpp"
namespace {

using ygo::environment::EpisodeDiagnosticSnapshot;

void require(const bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

struct Witness final {
    std::string episode_id;
    std::string decision_id;
    std::vector<std::string> candidate_keys;
    std::string observation_hash;
    std::vector<std::uint8_t> observation_bytes;
    std::string candidate_domain_digest;
    std::string selected_key;
};

Witness run_witness(const bool diagnostics,
                    std::vector<EpisodeDiagnosticSnapshot>& events) {
    auto factory = ygo::environment::EpisodicEnvironment::create(
        ygo::environment::CertifiedEnvironmentConfig::canonical());
    require(std::holds_alternative<std::unique_ptr<ygo::environment::EpisodicEnvironment>>(factory),
            "canonical environment factory rejected its certified config");
    auto environment = std::move(std::get<std::unique_ptr<ygo::environment::EpisodicEnvironment>>(factory));
    if (diagnostics) {
        environment->set_diagnostic_observer(
            [&events](const EpisodeDiagnosticSnapshot& snapshot) { events.push_back(snapshot); });
    }

    ygo::environment::EpisodeSpec spec;
    spec.root_seed = 2;
    ygo::environment::RunControl control;
    control.engine_process_budget = 64;
    control.semantic_action_budget = 64;
    const auto reset = environment->reset(spec, control);
    require(std::holds_alternative<ygo::environment::ResetAccepted>(reset),
            "canonical environment reset was not accepted");
    const auto& accepted = std::get<ygo::environment::ResetAccepted>(reset);
    if (!std::holds_alternative<ygo::environment::DecisionFrame>(accepted.next)) {
        if (const auto* failure = std::get_if<ygo::environment::EpisodeFailure>(&accepted.next)) {
            throw std::runtime_error(
                "bounded witness failure=" +
                std::string(ygo::environment::failure_code_name(failure->failure_code)) +
                ":" + std::string(ygo::environment::failure_stage_name(failure->failure_stage)));
        }
        if (std::holds_alternative<ygo::environment::EpisodeTerminal>(accepted.next)) {
            throw std::runtime_error("bounded witness reached terminal");
        }
        throw std::runtime_error("bounded witness was interrupted");
    }
    const auto& frame = std::get<ygo::environment::DecisionFrame>(accepted.next);
    Witness witness;
    witness.episode_id = frame.episode_semantic_id;
    witness.decision_id = frame.public_semantic_decision_id;
    witness.observation_hash = frame.public_observation_digest;
    witness.observation_bytes =
        ygo::environment::canonical_public_environment_observation_bytes(
            frame.public_observation);
    witness.candidate_domain_digest = frame.public_candidate_domain_digest;
    for (const auto& candidate : frame.request.candidates) {
        witness.candidate_keys.push_back(candidate.public_action_key);
    }
    ygo::environment::ActionSelection selection;
    selection.contract_id = frame.contract_id;
    selection.episode_semantic_id = frame.episode_semantic_id;
    selection.public_semantic_decision_id = frame.public_semantic_decision_id;
    selection.submission_token = frame.submission_token;
    selection.public_action_key = frame.request.candidates.front().public_action_key;
    witness.selected_key = selection.public_action_key;
    const auto step = environment->step(selection);
    require(std::holds_alternative<ygo::environment::StepAccepted>(step),
            "bounded witness step was rejected");
    return witness;
}

void test_diagnostic_toggle_is_semantically_neutral() {
    std::vector<EpisodeDiagnosticSnapshot> off_events;
    std::vector<EpisodeDiagnosticSnapshot> on_events;
    const auto off = run_witness(false, off_events);
    const auto on = run_witness(true, on_events);
    require(off.episode_id == on.episode_id && off.decision_id == on.decision_id &&
                off.candidate_keys == on.candidate_keys &&
                off.observation_hash == on.observation_hash &&
                off.observation_bytes == on.observation_bytes &&
                off.candidate_domain_digest == on.candidate_domain_digest &&
                off.selected_key == on.selected_key,
            "diagnostic toggle changed bounded semantic witness");
    require(!on_events.empty(), "diagnostic observer received no progress events");
    require(std::any_of(on_events.begin(), on_events.end(), [](const auto& event) {
                return event.current_phase == "CORE_ADVANCE";
            }),
            "diagnostic observer missed CORE_ADVANCE");
    require(std::any_of(on_events.begin(), on_events.end(), [](const auto& event) {
                return event.current_phase == "CORE_PROCESS_BEGIN";
            }),
            "diagnostic observer missed CORE_PROCESS_BEGIN");
    require(std::all_of(on_events.begin(), on_events.end(), [](const auto& event) {
                return event.engine_process_count <= event.engine_process_budget &&
                       event.semantic_action_count <= event.semantic_action_budget;
            }),
            "diagnostic observer emitted a budget-inconsistent snapshot");
    for (const auto phase : {"PUBLIC_SAFE_STATE", "CANDIDATE_BUILD", "OBSERVATION_BUILD"}) {
        require(std::any_of(on_events.begin(), on_events.end(), [phase](const auto& event) {
                    return event.current_phase == phase;
                }),
                std::string("diagnostic observer missed ") + phase);
    }
    for (const auto phase : {"PUBLIC_SAFE_STATE_BEGIN", "CANDIDATE_BUILD_BEGIN",
                             "OBSERVATION_BUILD_BEGIN", "PROTOCOL_DECODE_BEGIN"}) {
        require(std::any_of(on_events.begin(), on_events.end(), [phase](const auto& event) {
                    return event.current_phase == phase;
                }),
                std::string("diagnostic observer missed ") + phase);
    }
    require(std::none_of(off_events.begin(), off_events.end(), [](const auto&) { return true; }),
            "diagnostics unexpectedly emitted with observer disabled");
}

const ygo::trajectory::ParticipantPolicyAssignment& assignment_for_player(
    const std::vector<ygo::trajectory::ParticipantPolicyAssignment>& assignments,
    const std::uint8_t player) {
    const auto it = std::find_if(assignments.begin(), assignments.end(),
                                 [player](const auto& assignment) {
                                     return assignment.player == player;
                                 });
    require(it != assignments.end(), "teacher diagnostic assignment is missing");
    return *it;
}

ygo::policy::TeacherRunnerConfig make_teacher_config() {
    const auto swordsoul = ygo::teacher::make_swordsoul_tenyi_profile();
    const auto salamangreat = ygo::teacher::make_salamangreat_profile();
    const auto swordsoul_artifact = ygo::policy::make_teacher_policy_artifact(swordsoul);
    const auto salamangreat_artifact = ygo::policy::make_teacher_policy_artifact(salamangreat);

    ygo::policy::TeacherRunnerConfig config;
    config.environment_config = ygo::environment::CertifiedEnvironmentConfig::canonical();
    config.episode_spec.contract_id = std::string(ygo::environment::kEpisodicEnvironmentV2ContractId);
    config.episode_spec.root_seed = 2;
    config.episode_spec.seat_assignment = ygo::environment::SeatAssignment::Normal;
    config.episode_spec.starting_player = 0;
    config.run_control.engine_process_budget = 64;
    config.run_control.semantic_action_budget = 1;
    config.run_control.cancellation.reason = "ADMINISTRATIVE_CANCEL";
    config.run_control.cancellation.source = "task7-live-progress-test";
    config.policy_provenance.policy_artifacts = {swordsoul_artifact, salamangreat_artifact};
    std::sort(config.policy_provenance.policy_artifacts.begin(),
              config.policy_provenance.policy_artifacts.end(),
              [](const auto& left, const auto& right) {
                  return left.policy_artifact_id < right.policy_artifact_id;
              });
    config.policy_provenance.participant_assignments =
        ygo::policy::make_teacher_participant_assignments(
            swordsoul_artifact, salamangreat_artifact, config.environment_config,
            config.episode_spec.seat_assignment, config.episode_spec.starting_player,
            {ygo::trajectory::PolicyRole::Behavior, ygo::trajectory::PolicyRole::Opponent});
    for (std::uint8_t player = 0; player < 2; ++player) {
        const auto& assignment = assignment_for_player(
            config.policy_provenance.participant_assignments, player);
        const auto& profile = assignment.deck_role == ygo::trajectory::DeckRole::FirstLockedDeck
                                  ? swordsoul
                                  : salamangreat;
        const auto& artifact = assignment.deck_role == ygo::trajectory::DeckRole::FirstLockedDeck
                                   ? swordsoul_artifact
                                   : salamangreat_artifact;
        const auto binding = ygo::policy::make_teacher_policy_binding(profile);
        auto session = ygo::policy::create_teacher_policy_session(
            profile, binding, artifact, assignment);
        require(static_cast<bool>(session), "teacher diagnostic session setup failed");
        config.sessions[player] = std::move(*session.value);
    }
    return config;
}

void test_teacher_and_recorder_progress_phases_are_visible() {
    std::vector<EpisodeDiagnosticSnapshot> events;
    auto config = make_teacher_config();
    config.diagnostic_observer = [&events](const EpisodeDiagnosticSnapshot& snapshot) {
        events.push_back(snapshot);
    };
    auto created = ygo::policy::TeacherRunner::create(std::move(config));
    require(static_cast<bool>(created), "teacher diagnostic runner creation failed");
    (void)created.value->run();
    for (const auto phase : {"TEACHER_SELECT_BEGIN", "TEACHER_SELECT",
                             "ENVIRONMENT_STEP_TOTAL_BEGIN", "ENVIRONMENT_STEP_TOTAL",
                             "RECORDER_BEGIN", "RECORDER"}) {
        require(std::any_of(events.begin(), events.end(), [phase](const auto& event) {
                    return event.current_phase == phase;
                }),
                std::string("teacher diagnostic observer missed ") + phase);
    }
    require(std::any_of(events.begin(), events.end(), [](const auto& event) {
                return event.timing.teacher_select_us > 0 &&
                       event.timing.environment_step_total_us > 0 &&
                       event.timing.recorder_us > 0;
            }),
            "teacher diagnostic timing did not accumulate");
}

}  // namespace

int main() {
    try {
        test_diagnostic_toggle_is_semantically_neutral();
        test_teacher_and_recorder_progress_phases_are_visible();
        std::cout << "phase6_task7_live_progress_diagnostic_tests=passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
