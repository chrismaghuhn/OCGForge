#include "ygo/environment/episodic_environment.hpp"
#include "ygo/environment/public_safe_state.hpp"
#include "ygo/phase6/task7_dataset_authority_provisioning.hpp"
#include "ygo/policy/teacher.hpp"
#include "ygo/teacher/salamangreat_profile.hpp"
#include "ygo/teacher/swordsoul_tenyi_profile.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <deque>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace {

using ygo::environment::ActionSelection;
using ygo::environment::CertifiedEnvironmentConfig;
using ygo::environment::DecisionFrame;
using ygo::environment::EnvironmentActionCandidate;
using ygo::environment::EnvironmentActionKind;
using ygo::environment::EnvironmentDecisionKind;
using ygo::environment::EpisodeDiagnosticSnapshot;
using ygo::environment::EpisodeSpec;
using ygo::environment::EpisodeTerminal;
using ygo::environment::EpisodeInterrupted;
using ygo::environment::EpisodeFailure;
using ygo::environment::EpisodicEnvironment;
using ygo::environment::ResetAccepted;
using ygo::environment::RunControl;
using ygo::environment::SeatAssignment;
using ygo::environment::StepAccepted;
using ygo::environment::StepRejected;
using ygo::phase6::task7::Task7CollectionJobV1;
using ygo::policy::PolicyInput;
using ygo::policy::TeacherPolicySession;
using ygo::teacher::CandidateEvaluation;
using ygo::teacher::CandidateEvaluationStatus;
using ygo::teacher::ConfidenceClass;
using ygo::teacher::ScoreVector;
using ygo::teacher::TeacherFallbackLevel;
using ygo::teacher::TeacherRankingResult;
using ygo::teacher::TeacherRankingStatus;

constexpr std::string_view kTask = "P6_TASK7_HIITA_CYCLE_ISOLATION";
constexpr std::string_view kSemanticMain =
    "f929de0b4d4157327dba003067d2e21e42f7ad75";
constexpr std::string_view kDiagnosticBase =
    "827f73db843636e289e5687698bb77996b4692ef";
constexpr std::string_view kCollectorSemanticSourceCommit =
    "d0cf9f8e9168aef304474930a28722bc7e1d1e4a";
constexpr std::uint64_t kEngineProcessBudget = 5000;
constexpr std::uint64_t kSemanticActionBudget = 2000;
constexpr std::size_t kMaxContinuationDepth = 4;
constexpr std::size_t kMaxCounterfactualPaths = 16;
constexpr std::uint32_t kHiitaPasscode = 48815792;

using Boundary = std::variant<DecisionFrame, EpisodeTerminal, EpisodeInterrupted,
                              EpisodeFailure>;

enum class FailureKind : std::uint8_t {
    Baseline,
    Replay,
    Counterfactual,
    Internal,
};

struct ProbeFailure final : std::runtime_error {
    ProbeFailure(const FailureKind kind, std::string message)
        : std::runtime_error(std::move(message)), kind(kind) {}

    FailureKind kind;
};

[[noreturn]] void fail(const FailureKind kind, std::string message) {
    throw ProbeFailure(kind, std::move(message));
}

void require_probe(const bool condition, const FailureKind kind,
                   const std::string& message) {
    if (!condition) {
        fail(kind, message);
    }
}

std::string optional_u8(const std::optional<std::uint8_t>& value) {
    return value.has_value() ? std::to_string(*value) : "ABSENT";
}

std::string optional_u32(const std::optional<std::uint32_t>& value) {
    return value.has_value() ? std::to_string(*value) : "ABSENT";
}

std::string optional_i32(const std::optional<std::int32_t>& value) {
    return value.has_value() ? std::to_string(*value) : "ABSENT";
}

std::string optional_string(const std::optional<std::string>& value) {
    return value.has_value() ? *value : "ABSENT";
}

std::string join_ids(const std::vector<std::string>& values) {
    std::ostringstream output;
    output << values.size() << ':';
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index != 0) {
            output << ',';
        }
        output << values[index];
    }
    return output.str();
}

std::string score_text(const std::optional<ScoreVector>& score) {
    if (!score.has_value()) {
        return "ABSENT";
    }
    std::ostringstream output;
    for (std::size_t index = 0; index < score->values.size(); ++index) {
        if (index != 0) {
            output << ',';
        }
        output << score->values[index];
    }
    return output.str();
}

std::string decision_status_name(const TeacherRankingStatus status) {
    switch (status) {
    case TeacherRankingStatus::Selected:
        return "selected";
    case TeacherRankingStatus::InvalidInput:
        return "invalid_input";
    case TeacherRankingStatus::Blocked:
        return "blocked";
    case TeacherRankingStatus::Unsupported:
        return "unsupported";
    }
    return "unknown";
}

std::string evaluation_status_name(const CandidateEvaluationStatus status) {
    switch (status) {
    case CandidateEvaluationStatus::Supported:
        return "supported";
    case CandidateEvaluationStatus::NotApplicable:
        return "not_applicable";
    case CandidateEvaluationStatus::Unsupported:
        return "unsupported";
    case CandidateEvaluationStatus::Invalid:
        return "invalid";
    }
    return "unknown";
}

std::string fallback_name(const std::optional<TeacherFallbackLevel>& level) {
    if (!level.has_value()) {
        return "ABSENT";
    }
    switch (*level) {
    case TeacherFallbackLevel::F0:
        return "F0";
    case TeacherFallbackLevel::F1:
        return "F1";
    case TeacherFallbackLevel::F2:
        return "F2";
    case TeacherFallbackLevel::F3:
        return "F3";
    case TeacherFallbackLevel::F4:
        return "F4";
    }
    return "OTHER";
}

std::string confidence_name(const ConfidenceClass confidence) {
    switch (confidence) {
    case ConfidenceClass::High:
        return "high";
    case ConfidenceClass::Medium:
        return "medium";
    case ConfidenceClass::Low:
        return "low";
    case ConfidenceClass::Fallback:
        return "fallback";
    }
    return "unknown";
}

std::string choice_kind_name(const ygo::environment::PublicChoiceKind kind) {
    switch (kind) {
    case ygo::environment::PublicChoiceKind::YesNo:
        return "yes_no";
    case ygo::environment::PublicChoiceKind::EffectYesNo:
        return "effect_yes_no";
    case ygo::environment::PublicChoiceKind::EffectChoice:
        return "effect_choice";
    case ygo::environment::PublicChoiceKind::OptionValue:
        return "option_value";
    case ygo::environment::PublicChoiceKind::AnnouncementNumber:
        return "announcement_number";
    }
    return "unknown";
}

std::string card_reference_kind_name(
    const ygo::environment::PublicCardReferenceKind kind) {
    switch (kind) {
    case ygo::environment::PublicCardReferenceKind::VisibleCard:
        return "visible_card";
    case ygo::environment::PublicCardReferenceKind::RedactedSlot:
        return "redacted_slot";
    }
    return "unknown";
}

std::string boundary_kind(const Boundary& boundary) {
    if (std::holds_alternative<DecisionFrame>(boundary)) {
        return "DECISION_FRAME";
    }
    if (std::holds_alternative<EpisodeTerminal>(boundary)) {
        return "EPISODE_TERMINAL";
    }
    if (std::holds_alternative<EpisodeInterrupted>(boundary)) {
        return "EPISODE_INTERRUPTED";
    }
    return "EPISODE_FAILURE";
}

std::string path_text(const std::vector<std::string>& path) {
    if (path.empty()) {
        return "EMPTY";
    }
    std::ostringstream output;
    for (std::size_t index = 0; index < path.size(); ++index) {
        if (index != 0) {
            output << " -> ";
        }
        output << path[index];
    }
    return output.str();
}

struct FrameProgress final {
    std::optional<std::uint8_t> turn_player;
    std::optional<std::uint32_t> turn_count;
    std::optional<std::uint32_t> phase;
};

struct ProgressTracker final {
    std::optional<EpisodeDiagnosticSnapshot> last;
};

struct ReplayStep final {
    std::uint64_t decision_index = 0;
    std::uint8_t acting_player = 0;
    std::string request_kind;
    std::string selected_public_action_key;
    std::string public_observation_digest;
    std::string public_candidate_domain_digest;
};

struct ReplayState final {
    std::unique_ptr<EpisodicEnvironment> environment;
    std::shared_ptr<ProgressTracker> progress;
    Boundary next;
};

struct CounterfactualNode final {
    std::vector<std::string> path;
    DecisionFrame frame;
    FrameProgress progress;
};

struct CounterfactualOutcome final {
    std::vector<std::string> path;
    std::string next_boundary_kind;
};

struct ProbeReport final {
    bool target_found = false;
    bool unselect_found = false;
    std::optional<DecisionFrame> target_idle_frame;
    std::optional<FrameProgress> target_idle_progress;
    std::optional<TeacherRankingResult> target_idle_ranking;
    std::optional<DecisionFrame> unselect_frame;
    std::optional<FrameProgress> unselect_progress;
    std::optional<TeacherRankingResult> unselect_ranking;
    std::vector<ReplayStep> prefix;
    bool prefix_replay_exact = false;
    std::size_t unselect_non_cancel_count = 0;
    bool unselect_cancel_present = false;
    bool unselect_finish_present = false;
    std::vector<CounterfactualNode> counterfactual_nodes;
    std::vector<CounterfactualOutcome> counterfactual_outcomes;
    std::size_t counterfactual_path_count = 0;
    bool counterfactual_search_bound_exhausted = false;
    bool finish_reachable = false;
    std::vector<std::string> finish_path;
    bool finish_accepted = false;
    std::string next_boundary_kind = "ABSENT";
    std::string next_decision_family = "ABSENT";
    std::optional<std::uint32_t> next_turn_count;
    std::optional<std::uint32_t> next_phase;
    bool returned_immediately_to_same_hiita_idle_boundary = false;
    std::string error;
    std::optional<FailureKind> failure_kind;
};

std::optional<FrameProgress> progress_for(const ReplayState& state) {
    if (state.environment != nullptr) {
        if (const auto snapshot = state.environment->diagnostic_snapshot("PROBE_FRAME");
            snapshot.has_value()) {
            return FrameProgress{snapshot->turn_player, snapshot->turn_count,
                                 snapshot->phase};
        }
    }
    if (state.progress != nullptr && state.progress->last.has_value()) {
        const auto& snapshot = *state.progress->last;
        return FrameProgress{snapshot.turn_player, snapshot.turn_count,
                             snapshot.phase};
    }
    return std::nullopt;
}

EpisodeSpec episode_spec_for(const Task7CollectionJobV1& job,
                             const FailureKind failure_kind) {
    EpisodeSpec spec;
    spec.contract_id = std::string(ygo::environment::kEpisodicEnvironmentV2ContractId);
    spec.root_seed = job.root_seed;
    if (job.placement == "NORMAL") {
        spec.seat_assignment = SeatAssignment::Normal;
    } else if (job.placement == "MIRROR") {
        spec.seat_assignment = SeatAssignment::Mirror;
    } else {
        fail(failure_kind, "Task7 placement is not canonical");
    }
    spec.starting_player = job.starting_player;
    return spec;
}

ReplayState make_replay_state(const Task7CollectionJobV1& job,
                              const FailureKind failure_kind) {
    auto factory = EpisodicEnvironment::create(CertifiedEnvironmentConfig::canonical());
    require_probe(
        std::holds_alternative<std::unique_ptr<EpisodicEnvironment>>(factory),
        failure_kind, "canonical environment factory rejected the diagnostic probe");
    auto environment = std::move(
        std::get<std::unique_ptr<EpisodicEnvironment>>(factory));
    auto progress = std::make_shared<ProgressTracker>();
    environment->set_diagnostic_observer(
        [progress](const EpisodeDiagnosticSnapshot& snapshot) {
            progress->last = snapshot;
        });

    const auto spec = episode_spec_for(job, failure_kind);
    RunControl control;
    control.engine_process_budget = kEngineProcessBudget;
    control.semantic_action_budget = kSemanticActionBudget;
    control.cancellation.reason = job.cancellation_reason;
    control.cancellation.source = job.cancellation_source;
    const auto reset = environment->reset(spec, control);
    const auto* accepted = std::get_if<ResetAccepted>(&reset);
    if (accepted == nullptr) {
        fail(failure_kind, "bounded diagnostic reset was rejected");
    }
    return ReplayState{std::move(environment), std::move(progress), accepted->next};
}

const DecisionFrame* frame_of(const Boundary& boundary) {
    return std::get_if<DecisionFrame>(&boundary);
}

const EnvironmentActionCandidate* candidate_for_key(
    const DecisionFrame& frame, const std::string& public_action_key) {
    const auto it = std::find_if(
        frame.request.candidates.begin(), frame.request.candidates.end(),
        [&public_action_key](const auto& candidate) {
            return candidate.public_action_key == public_action_key;
        });
    return it == frame.request.candidates.end() ? nullptr : &*it;
}

StepAccepted apply_public_key(ReplayState& state, const DecisionFrame& frame,
                              const std::string& public_action_key,
                              const FailureKind failure_kind) {
    require_probe(candidate_for_key(frame, public_action_key) != nullptr,
                  failure_kind,
                  "stored public action key is absent from the regenerated domain");
    const ActionSelection selection{frame.contract_id, frame.episode_semantic_id,
                                    frame.public_semantic_decision_id,
                                    frame.submission_token, public_action_key};
    const auto result = state.environment->step(selection);
    if (const auto* rejected = std::get_if<StepRejected>(&result);
        rejected != nullptr) {
        fail(failure_kind,
             "public action step was rejected: " +
                 std::string(ygo::environment::rejection_code_name(
                     rejected->rejection_code)));
    }
    const auto* accepted = std::get_if<StepAccepted>(&result);
    require_probe(accepted != nullptr, failure_kind,
                  "public action step returned an unknown result");
    state.next = accepted->next;
    return *accepted;
}

ReplayStep replay_step_for(const DecisionFrame& frame,
                           const std::string& selected_public_action_key) {
    ReplayStep result;
    result.decision_index = frame.decision_index;
    result.acting_player = frame.acting_player;
    result.request_kind = std::string(
        ygo::environment::environment_decision_kind_name(frame.request.kind));
    result.selected_public_action_key = selected_public_action_key;
    result.public_observation_digest = frame.public_observation_digest;
    result.public_candidate_domain_digest = frame.public_candidate_domain_digest;
    return result;
}

void verify_replay_frame(const DecisionFrame& actual, const ReplayStep& expected,
                         const std::size_t prefix_index) {
    const auto context = "prefix replay decision " + std::to_string(prefix_index) +
                         ": ";
    require_probe(
        actual.decision_index == expected.decision_index,
        FailureKind::Replay, context + "decision index diverged");
    require_probe(
        actual.acting_player == expected.acting_player,
        FailureKind::Replay, context + "acting player diverged");
    require_probe(
        ygo::environment::environment_decision_kind_name(actual.request.kind) ==
            expected.request_kind,
        FailureKind::Replay, context + "request kind diverged");
    require_probe(
        actual.public_observation_digest == expected.public_observation_digest,
        FailureKind::Replay, context + "public observation digest diverged");
    require_probe(
        actual.public_candidate_domain_digest ==
            expected.public_candidate_domain_digest,
        FailureKind::Replay, context + "public candidate domain digest diverged");
    require_probe(
        candidate_for_key(actual, expected.selected_public_action_key) != nullptr,
        FailureKind::Replay, context + "selected public action key is absent");
}

ReplayState replay_prefix(const Task7CollectionJobV1& job,
                          const std::vector<ReplayStep>& prefix) {
    auto state = make_replay_state(job, FailureKind::Replay);
    for (std::size_t index = 0; index < prefix.size(); ++index) {
        const auto* frame = frame_of(state.next);
        require_probe(frame != nullptr, FailureKind::Replay,
                      "prefix replay ended before its stored decision prefix");
        verify_replay_frame(*frame, prefix[index], index);
        (void)apply_public_key(state, *frame,
                               prefix[index].selected_public_action_key,
                               FailureKind::Replay);
    }
    return state;
}

ReplayState replay_prefix_and_path(const Task7CollectionJobV1& job,
                                   const ProbeReport& report,
                                   const std::vector<std::string>& path) {
    auto state = replay_prefix(job, report.prefix);
    const auto* target = frame_of(state.next);
    require_probe(target != nullptr, FailureKind::Replay,
                  "prefix replay did not arrive at a target decision frame");
    require_probe(
        report.unselect_frame.has_value() &&
            target->decision_index == report.unselect_frame->decision_index &&
            target->engine_step_index == report.unselect_frame->engine_step_index &&
            target->acting_player == report.unselect_frame->acting_player &&
            target->request.kind == report.unselect_frame->request.kind &&
            target->public_observation_digest ==
                report.unselect_frame->public_observation_digest &&
            target->public_candidate_domain_digest ==
                report.unselect_frame->public_candidate_domain_digest &&
            target->request.candidates.size() ==
                report.unselect_frame->request.candidates.size(),
        FailureKind::Replay,
        "prefix replay did not reproduce the exact target UNSELECT_CARD frame");
    for (const auto& public_action_key : path) {
        const auto* frame = frame_of(state.next);
        require_probe(frame != nullptr, FailureKind::Counterfactual,
                      "counterfactual path ended before its next action");
        (void)apply_public_key(state, *frame, public_action_key,
                               FailureKind::Counterfactual);
    }
    return state;
}

const EnvironmentActionCandidate* hiita_candidate(const DecisionFrame& frame) {
    const auto it = std::find_if(
        frame.request.candidates.begin(), frame.request.candidates.end(),
        [](const auto& candidate) {
            if (candidate.action_kind != EnvironmentActionKind::IdleCommand ||
                !candidate.phase.has_value() || *candidate.phase != 1 ||
                !candidate.choice.has_value() ||
                candidate.choice->kind !=
                    ygo::environment::PublicChoiceKind::EffectChoice ||
                candidate.choice->value != 6 ||
                !candidate.source_reference.has_value() ||
                candidate.source_reference->kind !=
                    ygo::environment::PublicCardReferenceKind::VisibleCard) {
                return false;
            }
            return candidate.source_reference->observation_locator ==
                   "p1:EXTRA_DECK:public:" + std::to_string(kHiitaPasscode) + ":0";
        });
    return it == frame.request.candidates.end() ? nullptr : &*it;
}

bool is_hiita_idle_frame(const DecisionFrame& frame) {
    return frame.request.kind == EnvironmentDecisionKind::IdleCommand &&
           frame.acting_player == 1 && hiita_candidate(frame) != nullptr;
}

bool is_same_hiita_idle_boundary(const DecisionFrame& actual,
                                 const DecisionFrame& target_idle) {
    return is_hiita_idle_frame(actual) &&
           actual.public_candidate_domain_digest ==
               target_idle.public_candidate_domain_digest;
}

struct TeacherSessions final {
    std::array<std::optional<TeacherPolicySession>, 2> sessions;
};

const ygo::trajectory::ParticipantPolicyAssignment& assignment_for_player(
    const std::vector<ygo::trajectory::ParticipantPolicyAssignment>& assignments,
    const std::uint8_t player) {
    const auto it = std::find_if(
        assignments.begin(), assignments.end(),
        [player](const auto& assignment) { return assignment.player == player; });
    if (it == assignments.end()) {
        fail(FailureKind::Baseline, "accepted Teacher assignment is missing");
    }
    return *it;
}

TeacherSessions make_teacher_sessions(const Task7CollectionJobV1& job) {
    const auto swordsoul = ygo::teacher::make_swordsoul_tenyi_profile();
    const auto salamangreat = ygo::teacher::make_salamangreat_profile();
    const auto swordsoul_artifact =
        ygo::policy::make_teacher_policy_artifact(swordsoul);
    const auto salamangreat_artifact =
        ygo::policy::make_teacher_policy_artifact(salamangreat);
    require_probe(
        swordsoul_artifact.policy_artifact_id == job.seat_0_teacher_artifact &&
            salamangreat_artifact.policy_artifact_id == job.seat_1_teacher_artifact,
        FailureKind::Baseline,
        "Task7 job does not bind the accepted Teacher artifact pair");

    const auto config = CertifiedEnvironmentConfig::canonical();
    const std::array<ygo::trajectory::PolicyRole, 2> roles = {
        ygo::trajectory::PolicyRole::Behavior,
        ygo::trajectory::PolicyRole::Opponent};
    const auto assignments = ygo::policy::make_teacher_participant_assignments(
        swordsoul_artifact, salamangreat_artifact, config,
        job.placement == "NORMAL" ? SeatAssignment::Normal : SeatAssignment::Mirror,
        job.starting_player, roles);

    TeacherSessions result;
    for (std::uint8_t player = 0; player < 2; ++player) {
        const auto& assignment = assignment_for_player(assignments, player);
        const bool first_locked =
            assignment.deck_role == ygo::trajectory::DeckRole::FirstLockedDeck;
        const auto& profile = first_locked ? swordsoul : salamangreat;
        const auto& artifact = first_locked ? swordsoul_artifact : salamangreat_artifact;
        const auto binding = ygo::policy::make_teacher_policy_binding(profile);
        require_probe(
            binding.teacher_policy_binding_id ==
                (first_locked ? job.seat_0_teacher_binding
                              : job.seat_1_teacher_binding),
            FailureKind::Baseline,
            "Task7 job does not bind the accepted Teacher policy binding pair");
        auto session = ygo::policy::create_teacher_policy_session(
            profile, binding, artifact, assignment);
        require_probe(session && session.value.has_value(), FailureKind::Baseline,
                      "accepted Teacher session construction failed");
        result.sessions[player] = std::move(*session.value);
    }
    return result;
}

TeacherRankingResult select_teacher(TeacherPolicySession& session,
                                    const DecisionFrame& frame) {
    const PolicyInput input{frame.public_observation, frame.request.candidates};
    const auto selection = session.policy.select(input);
    require_probe(selection && selection.value.has_value(), FailureKind::Baseline,
                  "normal deterministic Teacher returned no public selection");
    const auto ranking = session.policy.pending_ranking_result();
    require_probe(ranking.has_value(), FailureKind::Baseline,
                  "Teacher pending ranking was not published after select");
    require_probe(
        ranking->selected_public_action_key.has_value() &&
            *ranking->selected_public_action_key ==
                selection.value->public_action_key,
        FailureKind::Baseline,
        "Teacher ranking and selected public action key disagree");
    return *ranking;
}

void commit_teacher(TeacherPolicySession& session,
                     const StepAccepted& accepted) {
    require_probe(session.policy.commit(accepted.transition), FailureKind::Baseline,
                  "Teacher accepted transition did not commit its pending ranking");
}

void validate_public_references(const DecisionFrame& frame,
                                FailureKind failure_kind);

void require_target_unselect(const DecisionFrame& frame,
                             const std::optional<FrameProgress>& progress) {
    std::ostringstream mismatch;
    bool valid = true;
    if (frame.request.kind != EnvironmentDecisionKind::UnselectCard) {
        valid = false;
        mismatch << "request_kind="
                 << ygo::environment::environment_decision_kind_name(
                        frame.request.kind)
                 << ' ';
    }
    if (frame.acting_player != 1) {
        valid = false;
        mismatch << "acting_player=" << static_cast<unsigned>(frame.acting_player)
                 << ' ';
    }
    if (frame.request.candidates.size() != 3) {
        valid = false;
        mismatch << "candidate_count=" << frame.request.candidates.size() << ' ';
    }
    if (!progress.has_value() || !progress->turn_count.has_value() ||
        *progress->turn_count != 16) {
        valid = false;
        mismatch << "turn_count="
                 << (progress.has_value() ? optional_u32(progress->turn_count)
                                           : "ABSENT")
                 << ' ';
    }
    if (!progress.has_value() || !progress->phase.has_value() ||
        *progress->phase != 4) {
        valid = false;
        mismatch << "phase="
                 << (progress.has_value() ? optional_u32(progress->phase)
                                           : "ABSENT")
                 << ' ';
    }
    if (!valid) {
        fail(FailureKind::Baseline,
             "Hiita IDLE action did not reach target UNSELECT_CARD boundary: " +
                 mismatch.str());
    }
}

void collect_baseline(const Task7CollectionJobV1& job, ProbeReport& report) {
    auto sessions = make_teacher_sessions(job);
    auto state = make_replay_state(job, FailureKind::Baseline);
    std::size_t guard = 0;
    for (;;) {
        require_probe(guard++ < kSemanticActionBudget, FailureKind::Baseline,
                      "bounded baseline did not reach the first Hiita boundary");
        const auto* frame = frame_of(state.next);
        require_probe(frame != nullptr, FailureKind::Baseline,
                      "bounded baseline ended before the first Hiita boundary");
        const auto progress = progress_for(state);
        auto& session = *sessions.sessions[frame->acting_player];
        const auto ranking = select_teacher(session, *frame);
        const auto selected_key = *ranking.selected_public_action_key;

        const auto* hiita = hiita_candidate(*frame);
        if (hiita != nullptr && selected_key == hiita->public_action_key) {
            require_probe(is_hiita_idle_frame(*frame), FailureKind::Baseline,
                          "selected Hiita candidate was not on an IDLE_COMMAND frame");
            report.target_found = true;
            report.target_idle_frame = *frame;
            report.target_idle_progress = progress;
            report.target_idle_ranking = ranking;
            validate_public_references(*frame, FailureKind::Baseline);
            report.prefix.push_back(replay_step_for(*frame, selected_key));
            const auto accepted = apply_public_key(
                state, *frame, selected_key, FailureKind::Baseline);
            commit_teacher(session, accepted);
            const auto* unselect = frame_of(state.next);
            require_probe(unselect != nullptr, FailureKind::Baseline,
                          "Hiita IDLE action did not produce a decision boundary");
            const auto unselect_progress = progress_for(state);
            require_target_unselect(*unselect, unselect_progress);
            report.unselect_found = true;
            report.unselect_frame = *unselect;
            report.unselect_progress = unselect_progress;
            validate_public_references(*unselect, FailureKind::Baseline);
            auto& unselect_session = *sessions.sessions[unselect->acting_player];
            report.unselect_ranking = select_teacher(unselect_session, *unselect);
            for (const auto& candidate : unselect->request.candidates) {
                if (candidate.action_kind == EnvironmentActionKind::Cancel) {
                    report.unselect_cancel_present = true;
                } else {
                    ++report.unselect_non_cancel_count;
                }
                if (candidate.action_kind == EnvironmentActionKind::Finish) {
                    report.unselect_finish_present = true;
                }
            }
            return;
        }

        report.prefix.push_back(replay_step_for(*frame, selected_key));
        const auto accepted = apply_public_key(
            state, *frame, selected_key, FailureKind::Baseline);
        commit_teacher(session, accepted);
    }
}

std::string visible_passcode(
    const ygo::environment::PublicEnvironmentObservation& observation,
    const std::optional<ygo::environment::PublicCardReference>& reference,
    const FailureKind failure_kind) {
    if (!reference.has_value() ||
        reference->kind != ygo::environment::PublicCardReferenceKind::VisibleCard) {
        return "ABSENT";
    }
    const auto decoded = ygo::environment::decode_canonical_public_safe_state(
        observation.canonical_safe_state_bytes());
    require_probe(decoded && decoded.value.has_value(), failure_kind,
                  "VisibleCard reference could not decode the public observation");
    const ygo::observation::ObservedCard* match = nullptr;
    for (const auto& entity : decoded.value->entities()) {
        if (entity.locator.value != reference->observation_locator) {
            continue;
        }
        require_probe(match == nullptr, failure_kind,
                      "VisibleCard locator matched multiple public observation entities");
        match = &entity;
    }
    require_probe(match != nullptr, failure_kind,
                  "VisibleCard locator did not resolve to a public observation entity");
    require_probe(match->identity_known, failure_kind,
                  "VisibleCard locator resolved to an identity-unknown entity");
    require_probe(match->passcode.has_value(), failure_kind,
                  "VisibleCard locator resolved without a public passcode");
    return std::to_string(*match->passcode);
}

void validate_public_references(const DecisionFrame& frame,
                                const FailureKind failure_kind) {
    for (const auto& candidate : frame.request.candidates) {
        (void)visible_passcode(frame.public_observation,
                               candidate.source_reference, failure_kind);
        (void)visible_passcode(frame.public_observation,
                               candidate.target_reference, failure_kind);
    }
}

void emit_reference(std::ostream& output, const std::string& prefix,
                    const ygo::environment::PublicEnvironmentObservation& observation,
                    const std::optional<ygo::environment::PublicCardReference>& reference,
                    const bool source) {
    const auto side = source ? "SOURCE" : "TARGET";
    if (!reference.has_value()) {
        output << prefix << '_' << side << "_REFERENCE_KIND=ABSENT\n"
               << prefix << '_' << side << "_OBSERVATION_LOCATOR=ABSENT\n";
        if (source) {
            output << prefix << "_SOURCE_LOCATOR=ABSENT\n"
                   << prefix << "_VISIBLE_PASSCODE=ABSENT\n";
        }
        return;
    }
    output << prefix << '_' << side << "_REFERENCE_KIND="
           << card_reference_kind_name(reference->kind) << '\n'
           << prefix << '_' << side << "_OBSERVATION_LOCATOR="
           << reference->observation_locator << '\n';
    if (source) {
        output << prefix << "_SOURCE_LOCATOR="
               << reference->observation_locator << '\n'
               << prefix << "_VISIBLE_PASSCODE="
               << visible_passcode(observation, reference, FailureKind::Internal) << '\n';
    }
}

void emit_candidate(std::ostream& output, const std::string& prefix,
                    const ygo::environment::PublicEnvironmentObservation& observation,
                    const EnvironmentActionCandidate& candidate) {
    output << prefix << "_ACTION_KIND="
           << ygo::environment::environment_action_kind_name(candidate.action_kind)
           << '\n'
           << prefix << "_PUBLIC_ACTION_KEY=" << candidate.public_action_key << '\n';
    if (candidate.choice.has_value()) {
        output << prefix << "_CHOICE_KIND="
               << choice_kind_name(candidate.choice->kind) << '\n'
               << prefix << "_CHOICE_VALUE=" << candidate.choice->value << '\n'
               << prefix << "_CHOICE_RESPONSE_INDEX="
               << optional_u32(candidate.choice->response_index) << '\n';
    } else {
        output << prefix << "_CHOICE_KIND=ABSENT\n"
               << prefix << "_CHOICE_VALUE=ABSENT\n"
               << prefix << "_CHOICE_RESPONSE_INDEX=ABSENT\n";
    }
    emit_reference(output, prefix, observation, candidate.source_reference, true);
    emit_reference(output, prefix, observation, candidate.target_reference, false);
    output << prefix << "_PHASE=" << optional_u32(candidate.phase) << '\n'
           << prefix << "_POSITION=" << optional_u8(candidate.position) << '\n'
           << prefix << "_SOURCE_INDEX=" << optional_u32(candidate.source_index) << '\n'
           << prefix << "_AMOUNT=" << optional_i32(candidate.amount) << '\n'
           << prefix << "_CONTINUATION_OPERATION="
           << (candidate.continuation_operation.empty()
                   ? "ABSENT"
                   : candidate.continuation_operation)
           << '\n'
           << prefix << "_SUBMITS_ENGINE_RESPONSE="
           << (candidate.submits_engine_response ? "YES" : "NO") << '\n';
}

void emit_candidates(std::ostream& output, const std::string& prefix,
                     const DecisionFrame& frame) {
    for (std::size_t index = 0; index < frame.request.candidates.size(); ++index) {
        output << prefix << '_' << index << "_ORDINAL=" << index << '\n';
        emit_candidate(output, prefix + '_' + std::to_string(index),
                       frame.public_observation,
                       frame.request.candidates[index]);
    }
}

const CandidateEvaluation* evaluation_for(
    const TeacherRankingResult& ranking, const std::string& public_action_key) {
    const auto it = std::find_if(
        ranking.evaluations.begin(), ranking.evaluations.end(),
        [&public_action_key](const auto& evaluation) {
            return evaluation.public_action_key == public_action_key;
        });
    return it == ranking.evaluations.end() ? nullptr : &*it;
}

void emit_evaluation(std::ostream& output, const std::string& prefix,
                     const CandidateEvaluation& evaluation) {
    output << prefix << "_STATUS=" << evaluation_status_name(evaluation.status) << '\n'
           << prefix << "_SCORE=" << score_text(evaluation.score) << '\n'
           << prefix << "_MATCHED_INTENTS="
           << join_ids(evaluation.matched_intent_ids) << '\n'
           << prefix << "_MATCHED_GOALS=" << join_ids(evaluation.matched_goal_ids)
           << '\n'
           << prefix << "_MATCHED_LINES=" << join_ids(evaluation.matched_line_ids)
           << '\n'
           << prefix << "_REASON_IDS=" << join_ids(evaluation.reason_ids) << '\n';
}

void emit_ranking(std::ostream& output, const std::string& prefix,
                  const TeacherRankingResult& ranking) {
    output << prefix << "_STATUS=" << decision_status_name(ranking.status) << '\n'
           << prefix << "_FALLBACK_LEVEL=" << fallback_name(ranking.fallback_level)
           << '\n'
           << prefix << "_SELECTED_PUBLIC_ACTION_KEY="
           << optional_string(ranking.selected_public_action_key) << '\n'
           << prefix << "_SELECTED_SCORE_VECTOR="
           << score_text(ranking.selected_score_vector) << '\n';
    if (ranking.explanation.has_value()) {
        output << prefix << "_EXPLANATION_CONFIDENCE_CLASS="
               << confidence_name(ranking.explanation->confidence_class) << '\n'
               << prefix << "_EXPLANATION_FALLBACK_LEVEL="
               << fallback_name(ranking.explanation->fallback_level) << '\n';
    } else {
        output << prefix << "_EXPLANATION_CONFIDENCE_CLASS=ABSENT\n"
               << prefix << "_EXPLANATION_FALLBACK_LEVEL=ABSENT\n";
    }
    for (std::size_t index = 0; index < ranking.evaluations.size(); ++index) {
        emit_evaluation(output,
                        prefix + "_EVAL_" + std::to_string(index),
                        ranking.evaluations[index]);
    }
}

void emit_progress(std::ostream& output, const std::string& prefix,
                   const std::optional<FrameProgress>& progress) {
    if (!progress.has_value()) {
        output << prefix << "_TURN_PLAYER=ABSENT\n"
               << prefix << "_TURN_COUNT=ABSENT\n"
               << prefix << "_PHASE=ABSENT\n";
        return;
    }
    output << prefix << "_TURN_PLAYER=" << optional_u8(progress->turn_player) << '\n'
           << prefix << "_TURN_COUNT=" << optional_u32(progress->turn_count) << '\n'
           << prefix << "_PHASE=" << optional_u32(progress->phase) << '\n';
}

void record_next_boundary(ProbeReport& report, const Boundary& next,
                          const std::shared_ptr<ProgressTracker>& progress) {
    report.next_boundary_kind = boundary_kind(next);
    report.next_decision_family = "ABSENT";
    report.next_turn_count.reset();
    report.next_phase.reset();
    report.returned_immediately_to_same_hiita_idle_boundary = false;
    if (const auto* frame = frame_of(next); frame != nullptr) {
        report.next_decision_family = std::string(
            ygo::environment::environment_decision_kind_name(frame->request.kind));
        if (progress != nullptr && progress->last.has_value()) {
            report.next_turn_count = progress->last->turn_count;
            report.next_phase = progress->last->phase;
        }
        if (report.target_idle_frame.has_value()) {
            report.returned_immediately_to_same_hiita_idle_boundary =
                is_same_hiita_idle_boundary(*frame, *report.target_idle_frame);
        }
    }
}

std::optional<std::string> finish_key_for(const DecisionFrame& frame) {
    const auto it = std::find_if(
        frame.request.candidates.begin(), frame.request.candidates.end(),
        [](const auto& candidate) {
            return candidate.action_kind == EnvironmentActionKind::Finish;
        });
    if (it == frame.request.candidates.end()) {
        return std::nullopt;
    }
    return it->public_action_key;
}

bool is_material_continuation_boundary(const DecisionFrame& frame) {
    return frame.request.kind == EnvironmentDecisionKind::UnselectCard ||
           frame.request.continuation.has_value();
}

void replay_finish(ProbeReport& report, const Task7CollectionJobV1& job,
                   const std::vector<std::string>& path,
                   const std::string& finish_key) {
    auto state = replay_prefix_and_path(job, report, path);
    const auto* frame = frame_of(state.next);
    require_probe(frame != nullptr, FailureKind::Counterfactual,
                  "finish replay did not reach the Finish decision frame");
    require_probe(candidate_for_key(*frame, finish_key) != nullptr,
                  FailureKind::Counterfactual,
                  "Finish key disappeared during the fresh legal replay");
    const ActionSelection selection{frame->contract_id, frame->episode_semantic_id,
                                    frame->public_semantic_decision_id,
                                    frame->submission_token, finish_key};
    const auto result = state.environment->step(selection);
    if (const auto* rejected = std::get_if<StepRejected>(&result);
        rejected != nullptr) {
        report.finish_accepted = false;
        report.next_boundary_kind = "STEP_REJECTED";
        report.next_decision_family = "ABSENT";
        return;
    }
    const auto* accepted = std::get_if<StepAccepted>(&result);
    require_probe(accepted != nullptr, FailureKind::Counterfactual,
                  "Finish replay returned an unknown step result");
    report.finish_accepted = true;
    state.next = accepted->next;
    record_next_boundary(report, state.next, state.progress);
}

std::optional<DecisionFrame> evaluate_counterfactual_path(
    ProbeReport& report, const Task7CollectionJobV1& job,
    const std::vector<std::string>& path) {
    require_probe(report.counterfactual_path_count < kMaxCounterfactualPaths,
                  FailureKind::Counterfactual,
                  "counterfactual path budget was exceeded");
    ++report.counterfactual_path_count;
    auto state = replay_prefix_and_path(job, report, path);
    report.counterfactual_outcomes.push_back(
        CounterfactualOutcome{path, boundary_kind(state.next)});
    const auto* frame = frame_of(state.next);
    if (frame == nullptr) {
        return std::nullopt;
    }
    const auto progress = progress_for(state).value_or(FrameProgress{});
    validate_public_references(*frame, FailureKind::Counterfactual);
    report.counterfactual_nodes.push_back(
        CounterfactualNode{path, *frame, progress});
    if (!report.finish_reachable) {
        if (const auto finish_key = finish_key_for(*frame); finish_key.has_value()) {
            report.finish_reachable = true;
            report.finish_path = path;
            report.finish_path.push_back(*finish_key);
            replay_finish(report, job, path, *finish_key);
        }
    }
    return *frame;
}

void explore_counterfactuals(const Task7CollectionJobV1& job,
                             ProbeReport& report) {
    require_probe(report.target_idle_frame.has_value() &&
                       report.unselect_frame.has_value() &&
                       report.unselect_ranking.has_value(),
                   FailureKind::Internal,
                   "counterfactual exploration lacks the captured public boundary");
    std::vector<std::string> roots;
    for (const auto& candidate : report.unselect_frame->request.candidates) {
        if (candidate.action_kind != EnvironmentActionKind::Cancel) {
            roots.push_back(candidate.public_action_key);
        }
    }
    std::deque<std::vector<std::string>> pending;
    for (const auto& root : roots) {
        pending.push_back({root});
    }

    while (!pending.empty()) {
        auto path = std::move(pending.front());
        pending.pop_front();
        // Both initial non-Cancel choices are always replayed. Once one of
        // them proves a Finish route, deeper siblings are unnecessary.
        if (report.finish_reachable && path.size() > 1) {
            break;
        }
        if (report.counterfactual_path_count >= kMaxCounterfactualPaths) {
            report.counterfactual_search_bound_exhausted = true;
            break;
        }
        const auto frame = evaluate_counterfactual_path(report, job, path);
        if (report.finish_reachable || !frame.has_value()) {
            continue;
        }
        if (path.size() >= kMaxContinuationDepth) {
            report.counterfactual_search_bound_exhausted = true;
            continue;
        }
        if (!is_material_continuation_boundary(*frame)) {
            continue;
        }
        for (const auto& candidate : frame->request.candidates) {
            if (candidate.action_kind == EnvironmentActionKind::Cancel) {
                continue;
            }
            if (report.counterfactual_path_count + pending.size() >=
                kMaxCounterfactualPaths) {
                report.counterfactual_search_bound_exhausted = true;
                break;
            }
            auto child = path;
            child.push_back(candidate.public_action_key);
            pending.push_back(std::move(child));
        }
    }
    if (!report.finish_reachable &&
        report.counterfactual_path_count >= kMaxCounterfactualPaths &&
        !pending.empty()) {
        report.counterfactual_search_bound_exhausted = true;
    }
}

std::string root_cause_class(const ProbeReport& report) {
    if (report.failure_kind.has_value() &&
        *report.failure_kind == FailureKind::Replay) {
        return "PUBLIC_REPLAY_DIVERGENCE";
    }
    if (!report.target_found || !report.unselect_found) {
        return "BASELINE_TARGET_NOT_FOUND";
    }
    if (!report.prefix_replay_exact) {
        return "PUBLIC_REPLAY_DIVERGENCE";
    }
    if (report.counterfactual_search_bound_exhausted &&
        !report.finish_reachable) {
        return "COUNTERFACTUAL_SEARCH_INCONCLUSIVE";
    }
    if (!report.finish_reachable) {
        return "NO_COMPLETING_PUBLIC_CONTINUATION_PATH_FOUND";
    }
    const bool teacher_selected_cancel =
        report.unselect_ranking.has_value() &&
        report.unselect_ranking->selected_public_action_key.has_value() &&
        report.unselect_frame.has_value() &&
        candidate_for_key(*report.unselect_frame,
                          *report.unselect_ranking->selected_public_action_key) !=
            nullptr &&
        candidate_for_key(*report.unselect_frame,
                          *report.unselect_ranking->selected_public_action_key)
                ->action_kind == EnvironmentActionKind::Cancel;
    if (teacher_selected_cancel && report.finish_accepted &&
        !report.returned_immediately_to_same_hiita_idle_boundary) {
        if (report.unselect_ranking->fallback_level ==
            std::optional<TeacherFallbackLevel>{TeacherFallbackLevel::F4}) {
            return "TEACHER_CONTINUATION_COMMITMENT_LOSS_CONFIRMED";
        }
        return "TEACHER_SELECTION_BUG_CONFIRMED_F4_HYPOTHESIS_FALSE";
    }
    return "COUNTERFACTUAL_SEARCH_INCONCLUSIVE";
}

void render_report(const ProbeReport& report) {
    std::cout << "TASK=" << kTask << '\n'
              << "SEMANTIC_MAIN=" << kSemanticMain << '\n'
              << "DIAGNOSTIC_BASE=" << kDiagnosticBase << '\n'
              << "COLLECTOR_SEMANTIC_SOURCE_COMMIT="
              << kCollectorSemanticSourceCommit << '\n'
              << "SEED=4\n"
              << "PLACEMENT=NORMAL\n"
              << "STARTING_PLAYER=0\n"
              << "ENGINE_PROCESS_BUDGET=" << kEngineProcessBudget << '\n'
              << "SEMANTIC_ACTION_BUDGET=" << kSemanticActionBudget << '\n';

    std::cout << "TARGET_FOUND=" << (report.target_found ? "YES" : "NO") << '\n';
    if (report.target_idle_frame.has_value()) {
        const auto& frame = *report.target_idle_frame;
        std::cout << "TARGET_IDLE_DECISION_INDEX=" << frame.decision_index << '\n'
                  << "TARGET_IDLE_ENGINE_STEP_INDEX=" << frame.engine_step_index << '\n'
                  << "TARGET_IDLE_TURN_COUNT="
                  << (report.target_idle_progress.has_value()
                          ? optional_u32(report.target_idle_progress->turn_count)
                          : "ABSENT")
                  << '\n'
                  << "TARGET_IDLE_PHASE="
                  << (report.target_idle_progress.has_value()
                          ? optional_u32(report.target_idle_progress->phase)
                          : "ABSENT")
                  << '\n'
                  << "TARGET_IDLE_PUBLIC_OBSERVATION_DIGEST="
                  << frame.public_observation_digest << '\n'
                  << "TARGET_IDLE_PUBLIC_CANDIDATE_DOMAIN_DIGEST="
                  << frame.public_candidate_domain_digest << '\n'
                  << "TARGET_IDLE_SELECTED_PUBLIC_ACTION_KEY="
                  << (report.target_idle_ranking.has_value()
                          ? optional_string(
                                report.target_idle_ranking->selected_public_action_key)
                          : "ABSENT")
                  << '\n';
        if (report.target_idle_ranking.has_value()) {
            emit_ranking(std::cout, "TARGET_IDLE_TEACHER",
                         *report.target_idle_ranking);
            if (report.target_idle_ranking->selected_public_action_key.has_value()) {
                if (const auto* evaluation = evaluation_for(
                        *report.target_idle_ranking,
                        *report.target_idle_ranking->selected_public_action_key);
                    evaluation != nullptr) {
                    std::cout << "TARGET_IDLE_TEACHER_SELECTED_STATUS="
                              << evaluation_status_name(evaluation->status) << '\n'
                              << "TARGET_IDLE_MATCHED_INTENTS="
                              << join_ids(evaluation->matched_intent_ids) << '\n'
                              << "TARGET_IDLE_MATCHED_GOALS="
                              << join_ids(evaluation->matched_goal_ids) << '\n'
                              << "TARGET_IDLE_MATCHED_LINES="
                              << join_ids(evaluation->matched_line_ids) << '\n'
                              << "TARGET_IDLE_REASON_IDS="
                              << join_ids(evaluation->reason_ids) << '\n';
                }
            }
        }
        emit_candidates(std::cout, "TARGET_IDLE_CANDIDATE", frame);
    } else {
        for (const auto* field : {"TARGET_IDLE_DECISION_INDEX",
                                  "TARGET_IDLE_ENGINE_STEP_INDEX",
                                  "TARGET_IDLE_TURN_COUNT", "TARGET_IDLE_PHASE",
                                  "TARGET_IDLE_PUBLIC_OBSERVATION_DIGEST",
                                  "TARGET_IDLE_PUBLIC_CANDIDATE_DOMAIN_DIGEST",
                                  "TARGET_IDLE_SELECTED_PUBLIC_ACTION_KEY",
                                  "TARGET_IDLE_TEACHER_FALLBACK_LEVEL",
                                  "TARGET_IDLE_TEACHER_SELECTED_STATUS",
                                  "TARGET_IDLE_MATCHED_INTENTS",
                                  "TARGET_IDLE_MATCHED_GOALS",
                                  "TARGET_IDLE_MATCHED_LINES"}) {
            std::cout << field << "=ABSENT\n";
        }
    }

    std::cout << "UNSELECT_FOUND=" << (report.unselect_found ? "YES" : "NO") << '\n';
    if (report.unselect_frame.has_value()) {
        const auto& frame = *report.unselect_frame;
        std::cout << "UNSELECT_DECISION_INDEX=" << frame.decision_index << '\n'
                  << "UNSELECT_ENGINE_STEP_INDEX=" << frame.engine_step_index << '\n'
                  << "UNSELECT_TURN_COUNT="
                  << (report.unselect_progress.has_value()
                          ? optional_u32(report.unselect_progress->turn_count)
                          : "ABSENT")
                  << '\n'
                  << "UNSELECT_PHASE="
                  << (report.unselect_progress.has_value()
                          ? optional_u32(report.unselect_progress->phase)
                          : "ABSENT")
                  << '\n'
                  << "UNSELECT_PUBLIC_OBSERVATION_DIGEST="
                  << frame.public_observation_digest << '\n'
                  << "UNSELECT_PUBLIC_CANDIDATE_DOMAIN_DIGEST="
                  << frame.public_candidate_domain_digest << '\n'
                  << "UNSELECT_CANDIDATE_COUNT="
                  << frame.request.candidates.size() << '\n'
                  << "UNSELECT_CONTINUATION_PRESENT="
                  << (frame.request.continuation.has_value() ? "YES" : "NO") << '\n'
                  << "UNSELECT_NON_CANCEL_COUNT="
                  << report.unselect_non_cancel_count << '\n'
                  << "UNSELECT_CANCEL_PRESENT="
                  << (report.unselect_cancel_present ? "YES" : "NO") << '\n'
                  << "UNSELECT_FINISH_PRESENT="
                  << (report.unselect_finish_present ? "YES" : "NO") << '\n';
        emit_candidates(std::cout, "UNSELECT_CANDIDATE", frame);
        if (report.unselect_ranking.has_value()) {
            emit_ranking(std::cout, "UNSELECT_TEACHER",
                         *report.unselect_ranking);
            const bool selected_cancel =
                report.unselect_ranking->selected_public_action_key.has_value() &&
                candidate_for_key(
                    frame, *report.unselect_ranking->selected_public_action_key) !=
                    nullptr &&
                candidate_for_key(
                    frame, *report.unselect_ranking->selected_public_action_key)
                        ->action_kind == EnvironmentActionKind::Cancel;
            std::cout << "UNSELECT_TEACHER_SELECTED_CANCEL="
                      << (selected_cancel ? "YES" : "NO") << '\n';
        } else {
            std::cout << "UNSELECT_TEACHER_STATUS=ABSENT\n"
                      << "UNSELECT_TEACHER_FALLBACK_LEVEL=ABSENT\n"
                      << "UNSELECT_TEACHER_SELECTED_PUBLIC_ACTION_KEY=ABSENT\n"
                      << "UNSELECT_TEACHER_SELECTED_CANCEL=NO\n";
        }
    } else {
        for (const auto* field : {"UNSELECT_DECISION_INDEX",
                                  "UNSELECT_ENGINE_STEP_INDEX", "UNSELECT_TURN_COUNT",
                                  "UNSELECT_PHASE",
                                  "UNSELECT_PUBLIC_OBSERVATION_DIGEST",
                                  "UNSELECT_PUBLIC_CANDIDATE_DOMAIN_DIGEST",
                                  "UNSELECT_CANDIDATE_COUNT",
                                  "UNSELECT_CONTINUATION_PRESENT",
                                  "UNSELECT_NON_CANCEL_COUNT"}) {
            std::cout << field << "=ABSENT\n";
        }
        std::cout << "UNSELECT_CANCEL_PRESENT=NO\n"
                  << "UNSELECT_FINISH_PRESENT=NO\n"
                  << "UNSELECT_TEACHER_STATUS=ABSENT\n"
                  << "UNSELECT_TEACHER_FALLBACK_LEVEL=ABSENT\n"
                  << "UNSELECT_TEACHER_SELECTED_PUBLIC_ACTION_KEY=ABSENT\n"
                  << "UNSELECT_TEACHER_SELECTED_CANCEL=NO\n";
    }

    std::cout << "PREFIX_DECISION_COUNT=" << report.prefix.size() << '\n'
              << "PREFIX_REPLAY_EXACT="
              << (report.prefix_replay_exact ? "YES" : "NO") << '\n'
              << "COUNTERFACTUAL_OUTCOME_COUNT="
              << report.counterfactual_outcomes.size() << '\n';
    for (std::size_t index = 0; index < report.counterfactual_outcomes.size();
         ++index) {
        const auto& outcome = report.counterfactual_outcomes[index];
        const auto prefix = "COUNTERFACTUAL_OUTCOME_" + std::to_string(index);
        std::cout << prefix << "_PATH=" << path_text(outcome.path) << '\n'
                  << prefix << "_NEXT_BOUNDARY_KIND="
                  << outcome.next_boundary_kind << '\n';
    }
    std::cout << "COUNTERFACTUAL_NODE_COUNT="
              << report.counterfactual_nodes.size() << '\n';
    for (std::size_t index = 0; index < report.counterfactual_nodes.size(); ++index) {
        const auto& node = report.counterfactual_nodes[index];
        const auto prefix = "COUNTERFACTUAL_NODE_" + std::to_string(index);
        std::cout << prefix << "_PATH=" << path_text(node.path) << '\n'
                  << prefix << "_DECISION_INDEX=" << node.frame.decision_index << '\n'
                  << prefix << "_ENGINE_STEP_INDEX=" << node.frame.engine_step_index << '\n'
                  << prefix << "_REQUEST_KIND="
                  << ygo::environment::environment_decision_kind_name(
                         node.frame.request.kind)
                  << '\n'
                  << prefix << "_ACTING_PLAYER="
                  << static_cast<unsigned>(node.frame.acting_player) << '\n'
                  << prefix << "_PUBLIC_OBSERVATION_DIGEST="
                  << node.frame.public_observation_digest << '\n'
                  << prefix << "_PUBLIC_CANDIDATE_DOMAIN_DIGEST="
                  << node.frame.public_candidate_domain_digest << '\n'
                  << prefix << "_CANDIDATE_COUNT="
                  << node.frame.request.candidates.size() << '\n'
                  << prefix << "_CONTINUATION_PRESENT="
                  << (node.frame.request.continuation.has_value() ? "YES" : "NO")
                  << '\n';
        emit_progress(std::cout, prefix, node.progress);
        emit_candidates(std::cout, prefix + "_CANDIDATE", node.frame);
    }
    std::cout << "COUNTERFACTUAL_PATH_COUNT="
              << report.counterfactual_path_count << '\n'
              << "COUNTERFACTUAL_SEARCH_BOUND_EXHAUSTED="
              << (report.counterfactual_search_bound_exhausted ? "YES" : "NO")
              << '\n'
              << "FINISH_REACHABLE="
              << (report.finish_reachable ? "YES" : "NO") << '\n'
              << "FINISH_PATH="
              << (report.finish_reachable ? path_text(report.finish_path) : "ABSENT")
              << '\n'
              << "FINISH_ACCEPTED="
              << (report.finish_accepted ? "YES" : "NO") << '\n'
              << "NEXT_BOUNDARY_KIND=" << report.next_boundary_kind << '\n'
              << "NEXT_DECISION_FAMILY=" << report.next_decision_family << '\n'
              << "NEXT_TURN_COUNT=" << optional_u32(report.next_turn_count) << '\n'
              << "NEXT_PHASE=" << optional_u32(report.next_phase) << '\n'
              << "RETURNED_IMMEDIATELY_TO_SAME_HIITA_IDLE_BOUNDARY="
              << (report.returned_immediately_to_same_hiita_idle_boundary ? "YES"
                                                                            : "NO")
              << '\n'
              << "ROOT_CAUSE_CLASS=" << root_cause_class(report) << '\n';
    if (!report.error.empty()) {
        std::cout << "ERROR=" << report.error << '\n';
    }
}

bool successful_report(const ProbeReport& report) {
    const auto classification = root_cause_class(report);
    const bool supported_classification =
        classification == "TEACHER_CONTINUATION_COMMITMENT_LOSS_CONFIRMED" ||
        classification == "TEACHER_SELECTION_BUG_CONFIRMED_F4_HYPOTHESIS_FALSE" ||
        classification == "NO_COMPLETING_PUBLIC_CONTINUATION_PATH_FOUND" ||
        classification == "COUNTERFACTUAL_SEARCH_INCONCLUSIVE";
    return report.error.empty() && report.target_found && report.unselect_found &&
           report.prefix_replay_exact && supported_classification;
}

void validate_task7_job(const Task7CollectionJobV1& job) {
    const auto config = CertifiedEnvironmentConfig::canonical();
    require_probe(
        job.collector_semantic_source_commit == kCollectorSemanticSourceCommit &&
            job.matchup == "ocgforge.matchup.swordsoul_salamangreat.v1" &&
            job.rules_bundle == config.rules_bundle_id &&
            job.format == config.format_id && job.duel_mode == config.duel_mode &&
            job.duel_flags == config.duel_flags && job.root_seed == 4 &&
            job.placement == "NORMAL" && job.starting_player == 0 &&
            config.locked_decks.size() == 2 &&
            job.seat_0_deck_role == config.locked_decks[0].id &&
            job.seat_0_deck_sha256 == config.locked_decks[0].sha256 &&
            job.seat_1_deck_role == config.locked_decks[1].id &&
            job.seat_1_deck_sha256 == config.locked_decks[1].sha256 &&
            job.engine_process_budget ==
                ygo::phase6::task7::kTask7CollectionEngineProcessBudget &&
            job.semantic_action_budget ==
                ygo::phase6::task7::kTask7CollectionSemanticActionBudget &&
            job.cancellation_reason == "ADMINISTRATIVE_CANCEL",
        FailureKind::Internal,
        "frozen Task7 Job0 does not match the canonical collection inputs");
}

}  // namespace

int main() {
    ProbeReport report;
    try {
        const auto schedule = ygo::phase6::task7::make_task7_collection_schedule(
            std::string(kCollectorSemanticSourceCommit));
        require_probe(schedule.jobs.size() == 16, FailureKind::Internal,
                      "frozen Task7 schedule does not contain 16 jobs");
        const auto& job = schedule.jobs.front();
        validate_task7_job(job);
        require_probe(job.root_seed == 4 && job.placement == "NORMAL" &&
                          job.starting_player == 0,
                      FailureKind::Internal,
                      "frozen Task7 Job0 is not seed4 NORMAL starting-player0");
        collect_baseline(job, report);
        report.prefix_replay_exact = true;
        explore_counterfactuals(job, report);
    } catch (const ProbeFailure& failure) {
        report.error = failure.what();
        report.failure_kind = failure.kind;
        if (failure.kind == FailureKind::Replay ||
            failure.kind == FailureKind::Counterfactual) {
            report.prefix_replay_exact = false;
        }
    } catch (const std::exception& error) {
        report.error = error.what();
        report.failure_kind = FailureKind::Internal;
    } catch (...) {
        report.error = "diagnostic probe failed";
        report.failure_kind = FailureKind::Internal;
    }
    render_report(report);
    return successful_report(report) ? 0 : 2;
}
