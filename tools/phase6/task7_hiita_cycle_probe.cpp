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

constexpr std::string_view kTask = "P6_TASK7_HIITA_PLACE_COMPLETION_TRACE";
constexpr std::string_view kSemanticMain =
    "f929de0b4d4157327dba003067d2e21e42f7ad75";
constexpr std::string_view kDiagnosticBase =
    "827f73db843636e289e5687698bb77996b4692ef";
constexpr std::string_view kCollectorSemanticSourceCommit =
    "d0cf9f8e9168aef304474930a28722bc7e1d1e4a";
constexpr std::uint64_t kEngineProcessBudget = 5000;
constexpr std::uint64_t kSemanticActionBudget = 2000;
constexpr std::size_t kMaxContinuationDepth = 4;
constexpr std::size_t kOriginalCounterfactualPathLimit = 16;
// This is diagnostic-only path recording capacity. The original 16-path
// cutoff is retained and measured; two additional records are the smallest
// capacity needed to exhaust the observed depth-4 frontier.
constexpr std::size_t kMaxCounterfactualPaths = 18;
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

std::string frontier_public_key(const DecisionFrame& frame) {
    std::ostringstream output;
    output << "acting_player=" << static_cast<unsigned>(frame.acting_player)
           << ";request_kind="
           << ygo::environment::environment_decision_kind_name(frame.request.kind)
           << ";public_observation_digest=" << frame.public_observation_digest
           << ";public_candidate_domain_digest="
           << frame.public_candidate_domain_digest;
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

struct SelectedPublicAction final {
    EnvironmentActionCandidate candidate;
    ygo::environment::PublicEnvironmentObservation observation;
};

struct FrontierNode final {
    std::vector<std::string> path;
    std::string frontier_public_key;
    DecisionFrame frame;
    FrameProgress progress;
    std::string material_path_references = "ABSENT";
    std::string material_operation = "UNPROVEN";
    bool expanded = false;
    bool has_finish = false;
    bool has_cancel = false;
    std::size_t non_cancel_candidate_count = 0;
};

struct FrontierEdge final {
    std::size_t source_node_index = 0;
    std::size_t source_depth = 0;
    std::size_t candidate_ordinal = 0;
    std::string source_frontier_key;
    std::string action_kind;
    std::string public_action_key;
    std::string material_operation = "UNPROVEN";
    std::string result_boundary_kind;
    std::string edge_class;
    std::string target_frontier_key = "ABSENT";
    std::optional<std::size_t> target_node_index;
};

struct FrontierSeen final {
    std::string public_key;
    std::size_t first_node_index = 0;
    std::vector<std::string> first_path;
    std::size_t occurrence_count = 0;
};

struct PendingPath final {
    std::vector<std::string> path;
    std::size_t parent_node_index = 0;
    std::size_t parent_candidate_ordinal = 0;
};

struct InitialMaterialSummary final {
    std::string public_action_key = "ABSENT";
    std::string source_locator = "ABSENT";
    std::string passcode = "ABSENT";
    std::string next_request_kind = "ABSENT";
    std::string next_candidate_count = "ABSENT";
    std::string next_finish_present = "NO";
    std::string next_cancel_present = "NO";
    std::string next_public_boundary_key = "ABSENT";
};

struct PublicHiitaFieldObservation final {
    std::string state = "UNPROVEN";
    std::string locator = "ABSENT";
};

struct PlaceCandidateTrace final {
    EnvironmentActionCandidate candidate;
    bool step_accepted = false;
    std::string next_boundary_kind = "ABSENT";
    std::optional<DecisionFrame> next_frame;
    FrameProgress next_progress;
    PublicHiitaFieldObservation hiita_on_field;
    bool returned_to_same_hiita_idle_boundary = false;
};

struct PlaceRouteTrace final {
    std::string material_route = "ABSENT";
    std::vector<std::string> path;
    DecisionFrame place_frame;
    FrameProgress place_progress;
    std::vector<PlaceCandidateTrace> candidates;
};

struct ProbeReport final {
    bool target_found = false;
    bool unselect_found = false;
    bool visible_reference_resolution = false;
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
    std::vector<FrontierNode> frontier_nodes;
    std::vector<FrontierEdge> frontier_edges;
    std::vector<FrontierSeen> frontier_seen;
    std::array<InitialMaterialSummary, 2> initial_material;
    std::vector<PlaceRouteTrace> place_routes;
    bool legal_non_cancel_route_out_of_unselect = false;
    bool place_trace_complete = false;
    std::size_t place_candidate_trace_count = 0;
    std::size_t hiita_field_completion_count = 0;
    std::size_t hiita_field_absent_count = 0;
    std::size_t hiita_field_unproven_count = 0;
    bool hiita_summon_completion = false;
    bool hiita_summon_completion_all_routes = false;
    bool place_returned_to_same_hiita_idle = false;
    std::size_t counterfactual_path_count = 0;
    bool counterfactual_search_bound_exhausted = false;
    bool original_path_bound_reached = false;
    std::size_t queued_unexplored_at_original_bound = 0;
    std::size_t queued_unexplored_at_effective_bound = 0;
    bool path_bound_exhausted = false;
    bool depth_bound_exhausted = false;
    bool depth4_frontier_exhaustive = false;
    std::size_t revisited_public_boundary_count = 0;
    std::size_t converged_public_boundary_count = 0;
    std::size_t max_observed_depth = 0;
    std::size_t max_candidate_count = 0;
    std::size_t total_non_cancel_edges = 0;
    std::size_t total_non_cancel_candidates_seen = 0;
    std::size_t total_finish_edges = 0;
    std::size_t total_cancel_edges = 0;
    std::size_t public_frontier_cycle_count = 0;
    std::size_t cycle_length = 0;
    std::string cycle_path = "ABSENT";
    std::string cycle_boundary_keys = "ABSENT";
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
                                   const std::vector<std::string>& path,
    std::vector<SelectedPublicAction>*
                                       selected_path_candidates = nullptr) {
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
        const auto* candidate = candidate_for_key(*frame, public_action_key);
        require_probe(candidate != nullptr, FailureKind::Counterfactual,
                      "counterfactual path action is absent from its public domain");
        if (selected_path_candidates != nullptr) {
            selected_path_candidates->push_back(
                SelectedPublicAction{*candidate, frame->public_observation});
        }
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
           frontier_public_key(actual) == frontier_public_key(target_idle);
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
            report.visible_reference_resolution = true;
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

PublicHiitaFieldObservation inspect_public_hiita_field(
    const ygo::environment::PublicEnvironmentObservation& observation,
    const FailureKind failure_kind) {
    const auto decoded = ygo::environment::decode_canonical_public_safe_state(
        observation.canonical_safe_state_bytes());
    require_probe(decoded && decoded.value.has_value(), failure_kind,
                  "Hiita field trace could not decode the public observation");

    const ygo::observation::ObservedCard* known_hiita = nullptr;
    for (const auto& entity : decoded.value->entities()) {
        if (!entity.identity_known || !entity.passcode.has_value() ||
            *entity.passcode != kHiitaPasscode) {
            continue;
        }
        require_probe(known_hiita == nullptr, failure_kind,
                      "public observation contained multiple known Hiita entities");
        known_hiita = &entity;
    }
    if (known_hiita == nullptr) {
        return PublicHiitaFieldObservation{};
    }
    if (known_hiita->zone != ygo::observation::SemanticZone::MonsterZone) {
        return PublicHiitaFieldObservation{"NO", "ABSENT"};
    }
    require_probe(!known_hiita->locator.empty(), failure_kind,
                  "known public Hiita on the field has no observation locator");
    return PublicHiitaFieldObservation{"YES", known_hiita->locator.value};
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

std::string material_path_references(
    const std::vector<SelectedPublicAction>& selected_actions,
    const FailureKind failure_kind) {
    std::ostringstream output;
    bool first = true;
    for (const auto& selected : selected_actions) {
        const auto& candidate = selected.candidate;
        if (!candidate.source_reference.has_value()) {
            continue;
        }
        if (!first) {
            output << ',';
        }
        first = false;
        output << candidate.source_reference->observation_locator << '('
               << visible_passcode(selected.observation, candidate.source_reference,
                                   failure_kind)
               << ')';
    }
    return first ? "ABSENT" : output.str();
}

std::string material_operation_name(const EnvironmentActionCandidate& candidate) {
    if (candidate.action_kind == EnvironmentActionKind::CardSelection ||
        candidate.action_kind == EnvironmentActionKind::Pick) {
        return "UNPROVEN";
    }
    if (candidate.action_kind == EnvironmentActionKind::Finish) {
        return "FINISH";
    }
    if (candidate.action_kind == EnvironmentActionKind::Cancel) {
        return "CANCEL";
    }
    return "NON_MATERIAL";
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
        } else {
            output << prefix << "_TARGET_VISIBLE_PASSCODE=ABSENT\n";
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
    } else {
        output << prefix << "_TARGET_VISIBLE_PASSCODE="
               << visible_passcode(observation, reference, FailureKind::Internal)
               << '\n';
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

std::string continuation_indices(const std::vector<std::uint32_t>& indices) {
    std::ostringstream output;
    for (std::size_t index = 0; index < indices.size(); ++index) {
        if (index != 0) {
            output << ',';
        }
        output << indices[index];
    }
    return indices.empty() ? "EMPTY" : output.str();
}

void emit_continuation(std::ostream& output, const std::string& prefix,
                       const std::optional<
                           ygo::environment::EnvironmentContinuationView>& continuation) {
    if (!continuation.has_value()) {
        output << prefix << "_CONTINUATION_KIND=ABSENT\n"
               << prefix << "_CONTINUATION_STEP=ABSENT\n"
               << prefix << "_CONTINUATION_SELECTED_INDICES=ABSENT\n"
               << prefix << "_CONTINUATION_REMAINING_INDICES=ABSENT\n"
               << prefix << "_CONTINUATION_MIN_COUNT=ABSENT\n"
               << prefix << "_CONTINUATION_MAX_COUNT=ABSENT\n"
               << prefix << "_CONTINUATION_CAN_FINISH=ABSENT\n"
               << prefix << "_CONTINUATION_CAN_CANCEL=ABSENT\n";
        return;
    }
    output << prefix << "_CONTINUATION_KIND=" << continuation->continuation_kind
           << '\n'
           << prefix << "_CONTINUATION_STEP=" << continuation->continuation_step
           << '\n'
           << prefix << "_CONTINUATION_SELECTED_INDICES="
           << continuation_indices(continuation->selected_indices) << '\n'
           << prefix << "_CONTINUATION_REMAINING_INDICES="
           << continuation_indices(continuation->remaining_indices) << '\n'
           << prefix << "_CONTINUATION_MIN_COUNT=" << continuation->min_count
           << '\n'
           << prefix << "_CONTINUATION_MAX_COUNT=" << continuation->max_count
           << '\n'
           << prefix << "_CONTINUATION_CAN_FINISH="
           << (continuation->can_finish ? "YES" : "NO") << '\n'
           << prefix << "_CONTINUATION_CAN_CANCEL="
           << (continuation->can_cancel ? "YES" : "NO") << '\n';
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

bool is_path_prefix(const std::vector<std::string>& prefix,
                    const std::vector<std::string>& value) {
    return prefix.size() < value.size() &&
           std::equal(prefix.begin(), prefix.end(), value.begin());
}

std::string material_path_operation(
    const std::vector<SelectedPublicAction>& selected_actions) {
    if (selected_actions.empty()) {
        return "NONE";
    }
    for (const auto& selected : selected_actions) {
        if (selected.candidate.action_kind == EnvironmentActionKind::CardSelection ||
            selected.candidate.action_kind == EnvironmentActionKind::Pick) {
            return "UNPROVEN";
        }
    }
    return material_operation_name(selected_actions.back().candidate);
}

struct FrontierRegistration final {
    std::size_t node_index = 0;
    std::size_t occurrence_before = 0;
};

FrontierRegistration register_frontier_node(
    ProbeReport& report, const std::vector<std::string>& path,
    const DecisionFrame& frame, const FrameProgress& progress,
    const std::vector<SelectedPublicAction>& selected_actions) {
    const auto public_key = frontier_public_key(frame);
    auto seen = std::find_if(
        report.frontier_seen.begin(), report.frontier_seen.end(),
        [&public_key](const auto& value) { return value.public_key == public_key; });
    std::size_t occurrence_before = 0;
    if (seen == report.frontier_seen.end()) {
        report.frontier_seen.push_back(
            FrontierSeen{public_key, report.frontier_nodes.size(), path, 1});
    } else {
        occurrence_before = seen->occurrence_count;
        ++seen->occurrence_count;
        if (occurrence_before == 1) {
            ++report.converged_public_boundary_count;
        } else {
            ++report.revisited_public_boundary_count;
        }
        if (is_path_prefix(seen->first_path, path)) {
            ++report.public_frontier_cycle_count;
            if (report.cycle_length == 0) {
                report.cycle_length = path.size() - seen->first_path.size();
                report.cycle_path = path_text(path);
                report.cycle_boundary_keys =
                    seen->public_key + " -> " + public_key;
            }
        }
    }

    FrontierNode node;
    node.path = path;
    node.frontier_public_key = public_key;
    node.frame = frame;
    node.progress = progress;
    node.material_path_references = material_path_references(
        selected_actions, FailureKind::Counterfactual);
    node.material_operation = material_path_operation(selected_actions);
    for (const auto& candidate : frame.request.candidates) {
        if (candidate.action_kind == EnvironmentActionKind::Cancel) {
            node.has_cancel = true;
            ++report.total_cancel_edges;
        } else {
            ++node.non_cancel_candidate_count;
            ++report.total_non_cancel_candidates_seen;
        }
        if (candidate.action_kind == EnvironmentActionKind::Finish) {
            node.has_finish = true;
        }
    }
    report.max_observed_depth = std::max(report.max_observed_depth, path.size());
    report.max_candidate_count = std::max(
        report.max_candidate_count, frame.request.candidates.size());
    const auto node_index = report.frontier_nodes.size();
    report.frontier_nodes.push_back(std::move(node));
    return FrontierRegistration{node_index, occurrence_before};
}

void append_frontier_edge(
    ProbeReport& report, const std::size_t source_node_index,
    const std::size_t candidate_ordinal, const std::string& result_boundary_kind,
    const std::string& edge_class, const std::string& target_frontier_key,
    const std::optional<std::size_t> target_node_index) {
    require_probe(
        source_node_index < report.frontier_nodes.size() &&
            candidate_ordinal <
                report.frontier_nodes[source_node_index].frame.request.candidates.size(),
        FailureKind::Internal, "frontier edge source is not in the public node domain");
    const auto& source = report.frontier_nodes[source_node_index];
    const auto& candidate = source.frame.request.candidates[candidate_ordinal];
    FrontierEdge edge;
    edge.source_node_index = source_node_index;
    edge.source_depth = source.path.size();
    edge.candidate_ordinal = candidate_ordinal;
    edge.source_frontier_key = source.frontier_public_key;
    edge.action_kind = std::string(
        ygo::environment::environment_action_kind_name(candidate.action_kind));
    edge.public_action_key = candidate.public_action_key;
    edge.material_operation = material_operation_name(candidate);
    edge.result_boundary_kind = result_boundary_kind;
    edge.edge_class = edge_class;
    edge.target_frontier_key = target_frontier_key;
    edge.target_node_index = target_node_index;
    report.frontier_edges.push_back(std::move(edge));
}

std::string public_edge_class_for_boundary(const Boundary& boundary) {
    if (std::holds_alternative<EpisodeTerminal>(boundary)) {
        return "TERMINAL";
    }
    if (std::holds_alternative<EpisodeInterrupted>(boundary)) {
        return "INTERRUPTED";
    }
    if (std::holds_alternative<EpisodeFailure>(boundary)) {
        return "FAILURE";
    }
    return "FAILURE";
}

struct FinishReplayOutcome final {
    bool accepted = false;
    std::string result_boundary_kind = "STEP_REJECTED";
    std::string target_frontier_key = "ABSENT";
};

FinishReplayOutcome replay_finish(ProbeReport& report,
                                  const Task7CollectionJobV1& job,
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
        return FinishReplayOutcome{};
    }
    const auto* accepted = std::get_if<StepAccepted>(&result);
    require_probe(accepted != nullptr, FailureKind::Counterfactual,
                  "Finish replay returned an unknown step result");
    report.finish_accepted = true;
    state.next = accepted->next;
    record_next_boundary(report, state.next, state.progress);
    FinishReplayOutcome outcome;
    outcome.accepted = true;
    outcome.result_boundary_kind = boundary_kind(state.next);
    if (const auto* next_frame = frame_of(state.next); next_frame != nullptr) {
        outcome.target_frontier_key = frontier_public_key(*next_frame);
    }
    return outcome;
}

void evaluate_finish_edge(ProbeReport& report, const Task7CollectionJobV1& job,
                          const std::size_t source_node_index,
                          const std::size_t candidate_ordinal) {
    const auto& source = report.frontier_nodes[source_node_index];
    const auto& candidate =
        source.frame.request.candidates[candidate_ordinal];
    ++report.total_non_cancel_edges;
    ++report.total_finish_edges;
    report.finish_reachable = true;
    report.finish_path = source.path;
    report.finish_path.push_back(candidate.public_action_key);
    const auto replay = replay_finish(report, job, source.path,
                                      candidate.public_action_key);
    append_frontier_edge(
        report, source_node_index, candidate_ordinal,
        replay.result_boundary_kind,
        replay.accepted ? "FINISH_REACHED" : "FAILURE",
        replay.target_frontier_key, std::nullopt);
}

std::optional<std::size_t> evaluate_counterfactual_path(
    ProbeReport& report, const Task7CollectionJobV1& job,
    const PendingPath& pending_path) {
    require_probe(report.counterfactual_path_count < kMaxCounterfactualPaths,
                  FailureKind::Counterfactual,
                  "counterfactual path budget was exceeded");
    ++report.counterfactual_path_count;
    require_probe(
        pending_path.parent_node_index < report.frontier_nodes.size(),
        FailureKind::Internal, "counterfactual parent node is not recorded");
    const auto& parent = report.frontier_nodes[pending_path.parent_node_index];
    require_probe(
        pending_path.parent_candidate_ordinal <
            parent.frame.request.candidates.size(),
        FailureKind::Internal,
        "counterfactual parent candidate ordinal is not in the public domain");
    const auto& source_candidate =
        parent.frame.request.candidates[pending_path.parent_candidate_ordinal];
    require_probe(
        !pending_path.path.empty() &&
            pending_path.path.back() == source_candidate.public_action_key,
        FailureKind::Internal,
        "counterfactual path does not end in its parent public action");

    ++report.total_non_cancel_edges;
    std::vector<SelectedPublicAction> selected_actions;
    auto state = replay_prefix_and_path(job, report, pending_path.path,
                                        &selected_actions);
    const auto* frame = frame_of(state.next);
    if (frame == nullptr) {
        append_frontier_edge(
            report, pending_path.parent_node_index,
            pending_path.parent_candidate_ordinal, boundary_kind(state.next),
            public_edge_class_for_boundary(state.next), "ABSENT", std::nullopt);
        return std::nullopt;
    }
    const auto progress = progress_for(state).value_or(FrameProgress{});
    validate_public_references(*frame, FailureKind::Counterfactual);
    const auto registration = register_frontier_node(
        report, pending_path.path, *frame, progress, selected_actions);
    std::string edge_class;
    if (registration.occurrence_before == 0) {
        edge_class = is_material_continuation_boundary(*frame)
                         ? "NEW_DECISION_BOUNDARY"
                         : "NON_MATERIAL_DECISION_BOUNDARY";
    } else if (registration.occurrence_before == 1) {
        edge_class = "CONVERGED_PUBLIC_BOUNDARY";
    } else {
        edge_class = "REVISITED_PUBLIC_BOUNDARY";
    }
    append_frontier_edge(
        report, pending_path.parent_node_index,
        pending_path.parent_candidate_ordinal, "DECISION_FRAME", edge_class,
        report.frontier_nodes[registration.node_index].frontier_public_key,
        registration.node_index);
    return registration.node_index;
}

std::size_t initial_material_index(const ProbeReport& report,
                                   const std::size_t candidate_ordinal) {
    require_probe(report.unselect_frame.has_value(), FailureKind::Internal,
                  "initial material summary lacks its target frame");
    std::size_t non_cancel_ordinal = 0;
    for (std::size_t ordinal = 0;
         ordinal < report.unselect_frame->request.candidates.size(); ++ordinal) {
        if (report.unselect_frame->request.candidates[ordinal].action_kind ==
            EnvironmentActionKind::Cancel) {
            continue;
        }
        if (ordinal == candidate_ordinal) {
            return non_cancel_ordinal < 2 ? non_cancel_ordinal : 2;
        }
        ++non_cancel_ordinal;
    }
    return 2;
}

void record_initial_material_summary(
    ProbeReport& report, const PendingPath& pending_path,
    const Boundary& next) {
    if (pending_path.path.size() != 1 || pending_path.parent_node_index != 0) {
        return;
    }
    const auto summary_index =
        initial_material_index(report, pending_path.parent_candidate_ordinal);
    if (summary_index >= report.initial_material.size()) {
        return;
    }
    auto& summary = report.initial_material[summary_index];
    const auto& source = report.frontier_nodes[0].frame.request.candidates[
        pending_path.parent_candidate_ordinal];
    require_probe(
        source.source_reference.has_value() &&
            source.source_reference->kind ==
                ygo::environment::PublicCardReferenceKind::VisibleCard,
        FailureKind::Counterfactual,
        "initial material candidate lacks a visible public source reference");
    summary.public_action_key = source.public_action_key;
    summary.source_locator = source.source_reference->observation_locator;
    summary.passcode = visible_passcode(
        report.frontier_nodes[0].frame.public_observation,
        source.source_reference, FailureKind::Counterfactual);
    if (const auto* frame = frame_of(next); frame != nullptr) {
        summary.next_request_kind = std::string(
            ygo::environment::environment_decision_kind_name(frame->request.kind));
        summary.next_candidate_count = std::to_string(frame->request.candidates.size());
        summary.next_finish_present = finish_key_for(*frame).has_value() ? "YES" : "NO";
        summary.next_cancel_present = std::any_of(
            frame->request.candidates.begin(), frame->request.candidates.end(),
            [](const auto& candidate) {
                return candidate.action_kind == EnvironmentActionKind::Cancel;
            })
                                           ? "YES"
                                           : "NO";
        summary.next_public_boundary_key = frontier_public_key(*frame);
    }
}

void expand_frontier_node(ProbeReport& report, const Task7CollectionJobV1& job,
                          const std::size_t node_index,
                          std::deque<PendingPath>& pending) {
    auto& node = report.frontier_nodes[node_index];
    const bool material_boundary = is_material_continuation_boundary(node.frame);
    for (std::size_t ordinal = 0;
         ordinal < node.frame.request.candidates.size(); ++ordinal) {
        const auto& candidate = node.frame.request.candidates[ordinal];
        if (candidate.action_kind == EnvironmentActionKind::Cancel) {
            continue;
        }
        if (candidate.action_kind == EnvironmentActionKind::Finish) {
            if (!report.finish_reachable) {
                node.expanded = true;
                evaluate_finish_edge(report, job, node_index, ordinal);
            }
            continue;
        }
        if (!material_boundary) {
            continue;
        }
        if (node.path.size() >= kMaxContinuationDepth) {
            report.depth_bound_exhausted = true;
            continue;
        }
        if (report.finish_reachable) {
            continue;
        }
        node.expanded = true;
        auto path = node.path;
        path.push_back(candidate.public_action_key);
        pending.push_back(PendingPath{std::move(path), node_index, ordinal});
    }
}

void explore_counterfactuals(const Task7CollectionJobV1& job,
                             ProbeReport& report) {
    require_probe(report.target_idle_frame.has_value() &&
                       report.unselect_frame.has_value() &&
                       report.unselect_ranking.has_value(),
                   FailureKind::Internal,
                   "counterfactual exploration lacks the captured public boundary");
    std::vector<std::size_t> roots;
    for (std::size_t ordinal = 0;
         ordinal < report.unselect_frame->request.candidates.size(); ++ordinal) {
        const auto& candidate = report.unselect_frame->request.candidates[ordinal];
        if (candidate.action_kind != EnvironmentActionKind::Cancel) {
            roots.push_back(ordinal);
        }
    }
    const auto root_progress = report.unselect_progress.value_or(FrameProgress{});
    const auto root_registration = register_frontier_node(
        report, {}, *report.unselect_frame, root_progress, {});
    require_probe(root_registration.node_index == 0, FailureKind::Internal,
                  "target frontier root was not recorded at index zero");
    report.frontier_nodes[root_registration.node_index].expanded = !roots.empty();
    std::deque<PendingPath> pending_paths;
    for (const auto ordinal : roots) {
        pending_paths.push_back(PendingPath{
            {report.unselect_frame->request.candidates[ordinal].public_action_key},
            root_registration.node_index, ordinal});
    }

    while (!pending_paths.empty()) {
        auto pending_path = std::move(pending_paths.front());
        pending_paths.pop_front();
        // Both initial non-Cancel choices are always replayed. Once one of
        // them proves a Finish route, deeper siblings are unnecessary.
        if (report.finish_reachable && pending_path.path.size() > 1) {
            break;
        }
        if (report.counterfactual_path_count >= kMaxCounterfactualPaths) {
            report.path_bound_exhausted = true;
            report.queued_unexplored_at_effective_bound = pending_paths.size();
            break;
        }
        const auto node_index = evaluate_counterfactual_path(
            report, job, pending_path);
        if (node_index.has_value()) {
            record_initial_material_summary(report, pending_path,
                                            report.frontier_nodes[*node_index].frame);
            expand_frontier_node(report, job, *node_index, pending_paths);
        }
        if (!report.original_path_bound_reached &&
            report.counterfactual_path_count >=
                kOriginalCounterfactualPathLimit) {
            report.original_path_bound_reached = true;
            report.queued_unexplored_at_original_bound = pending_paths.size();
        }
    }
    if (!report.finish_reachable && report.counterfactual_path_count >=
                                      kMaxCounterfactualPaths &&
        !pending_paths.empty()) {
        report.path_bound_exhausted = true;
        report.queued_unexplored_at_effective_bound = pending_paths.size();
    }
    report.depth4_frontier_exhaustive =
        !report.finish_reachable && !report.path_bound_exhausted &&
        pending_paths.empty();
    report.counterfactual_search_bound_exhausted =
        report.path_bound_exhausted || report.depth_bound_exhausted;
}

bool has_candidate_kind(const DecisionFrame& frame,
                        const EnvironmentActionKind action_kind) {
    return std::any_of(
        frame.request.candidates.begin(), frame.request.candidates.end(),
        [action_kind](const auto& candidate) {
            return candidate.action_kind == action_kind;
        });
}

std::size_t non_cancel_candidate_count(const DecisionFrame& frame) {
    return static_cast<std::size_t>(std::count_if(
        frame.request.candidates.begin(), frame.request.candidates.end(),
        [](const auto& candidate) {
            return candidate.action_kind != EnvironmentActionKind::Cancel;
        }));
}

const EnvironmentActionCandidate* material_candidate_for_locator(
    const DecisionFrame& frame, const std::string& locator,
    const std::string& passcode) {
    const EnvironmentActionCandidate* match = nullptr;
    for (const auto& candidate : frame.request.candidates) {
        if (!candidate.source_reference.has_value() ||
            candidate.source_reference->kind !=
                ygo::environment::PublicCardReferenceKind::VisibleCard ||
            candidate.source_reference->observation_locator != locator) {
            continue;
        }
        require_probe(
            visible_passcode(frame.public_observation, candidate.source_reference,
                             FailureKind::Counterfactual) == passcode,
            FailureKind::Counterfactual,
            "material candidate locator resolved to an unexpected public passcode");
        require_probe(match == nullptr, FailureKind::Counterfactual,
                      "material locator matched multiple public candidates");
        match = &candidate;
    }
    require_probe(match != nullptr, FailureKind::Counterfactual,
                  "material locator was absent from the regenerated public domain");
    return match;
}

std::vector<std::string> material_route_action_keys(
    const Task7CollectionJobV1& job, const ProbeReport& report,
    const std::size_t first_material_index,
    const std::size_t second_material_index) {
    const auto& first = report.initial_material[first_material_index];
    const auto& second = report.initial_material[second_material_index];
    require_probe(first.public_action_key != "ABSENT" &&
                       second.source_locator != "ABSENT" &&
                       second.passcode != "ABSENT",
                   FailureKind::Internal,
                   "material route lacks its public reference identity");
    std::vector<std::string> path{first.public_action_key};
    const auto state = replay_prefix_and_path(job, report, path);
    const auto* frame = frame_of(state.next);
    require_probe(frame != nullptr &&
                      frame->request.kind == EnvironmentDecisionKind::UnselectCard,
                  FailureKind::Counterfactual,
                  "first material action did not produce the expected UNSELECT_CARD domain");
    const auto* candidate = material_candidate_for_locator(
        *frame, second.source_locator, second.passcode);
    path.push_back(candidate->public_action_key);
    return path;
}

PlaceRouteTrace trace_place_route(const Task7CollectionJobV1& job,
                                  const ProbeReport& report,
                                  std::string material_route,
                                  std::vector<std::string> path) {
    auto place_state = replay_prefix_and_path(job, report, path);
    const auto* place = frame_of(place_state.next);
    require_probe(place != nullptr, FailureKind::Counterfactual,
                  "material route did not reach a PLACE decision boundary");
    require_probe(place->request.kind == EnvironmentDecisionKind::Place,
                  FailureKind::Counterfactual,
                  "material route reached an unexpected decision family instead of PLACE");
    validate_public_references(*place, FailureKind::Counterfactual);

    PlaceRouteTrace route;
    route.material_route = std::move(material_route);
    route.path = std::move(path);
    route.place_frame = *place;
    route.place_progress = progress_for(place_state).value_or(FrameProgress{});
    for (const auto& candidate : place->request.candidates) {
        PlaceCandidateTrace candidate_trace;
        candidate_trace.candidate = candidate;

        auto candidate_state = replay_prefix_and_path(job, report, route.path);
        const auto* replayed_place = frame_of(candidate_state.next);
        require_probe(replayed_place != nullptr &&
                          replayed_place->request.kind ==
                              EnvironmentDecisionKind::Place &&
                          frontier_public_key(*replayed_place) ==
                              frontier_public_key(route.place_frame),
                      FailureKind::Replay,
                      "fresh PLACE replay did not reproduce the recorded public boundary");
        const auto accepted = apply_public_key(
            candidate_state, *replayed_place, candidate.public_action_key,
            FailureKind::Counterfactual);
        candidate_trace.step_accepted = true;
        candidate_trace.next_boundary_kind = boundary_kind(accepted.next);
        if (const auto* next = frame_of(accepted.next); next != nullptr) {
            validate_public_references(*next, FailureKind::Counterfactual);
            candidate_trace.next_frame = *next;
            candidate_trace.next_progress =
                progress_for(candidate_state).value_or(FrameProgress{});
            candidate_trace.hiita_on_field = inspect_public_hiita_field(
                next->public_observation, FailureKind::Counterfactual);
            candidate_trace.returned_to_same_hiita_idle_boundary =
                is_same_hiita_idle_boundary(*next, *report.target_idle_frame);
        }
        route.candidates.push_back(std::move(candidate_trace));
    }
    return route;
}

void trace_place_completions(const Task7CollectionJobV1& job,
                             ProbeReport& report) {
    require_probe(report.initial_material[0].public_action_key != "ABSENT" &&
                       report.initial_material[1].public_action_key != "ABSENT",
                   FailureKind::Internal,
                   "PLACE trace lacks the two captured initial material action keys");
    require_probe(report.target_idle_frame.has_value(), FailureKind::Internal,
                  "PLACE trace lacks the target Hiita IDLE boundary");

    struct MaterialRouteSpec final {
        std::string label;
        std::size_t first_material_index = 0;
        std::size_t second_material_index = 0;
    };
    const std::array<MaterialRouteSpec, 2> routes = {
        MaterialRouteSpec{"A->B", 0, 1},
        MaterialRouteSpec{"B->A", 1, 0},
    };

    report.place_routes.clear();
    for (const auto& route_spec : routes) {
        auto path = material_route_action_keys(
            job, report, route_spec.first_material_index,
            route_spec.second_material_index);
        auto route = trace_place_route(job, report, route_spec.label,
                                       std::move(path));
        report.legal_non_cancel_route_out_of_unselect =
            report.legal_non_cancel_route_out_of_unselect ||
            route.place_frame.request.kind == EnvironmentDecisionKind::Place;
        report.place_candidate_trace_count += route.candidates.size();
        for (const auto& candidate : route.candidates) {
            if (candidate.hiita_on_field.state == "YES") {
                ++report.hiita_field_completion_count;
            } else if (candidate.hiita_on_field.state == "NO") {
                ++report.hiita_field_absent_count;
            } else {
                ++report.hiita_field_unproven_count;
            }
            report.place_returned_to_same_hiita_idle =
                report.place_returned_to_same_hiita_idle ||
                candidate.returned_to_same_hiita_idle_boundary;
        }
        report.place_routes.push_back(std::move(route));
    }

    report.place_trace_complete = report.place_routes.size() == routes.size() &&
                                  std::all_of(
                                      report.place_routes.begin(),
                                      report.place_routes.end(), [](const auto& route) {
                                          return !route.candidates.empty() &&
                                                 route.candidates.size() ==
                                                     route.place_frame.request.candidates.size();
                                      });
    report.hiita_summon_completion = report.hiita_field_completion_count != 0;
    report.hiita_summon_completion_all_routes =
        report.place_trace_complete && report.place_candidate_trace_count != 0 &&
        report.hiita_field_completion_count == report.place_candidate_trace_count;
}

bool teacher_selected_cancel(const ProbeReport& report) {
    return report.unselect_ranking.has_value() &&
           report.unselect_ranking->selected_public_action_key.has_value() &&
           report.unselect_frame.has_value() &&
           candidate_for_key(*report.unselect_frame,
                             *report.unselect_ranking->selected_public_action_key) !=
               nullptr &&
           candidate_for_key(*report.unselect_frame,
                             *report.unselect_ranking->selected_public_action_key)
                   ->action_kind == EnvironmentActionKind::Cancel;
}

bool legal_hiita_progression_proven(const ProbeReport& report) {
    return std::any_of(
        report.place_routes.begin(), report.place_routes.end(),
        [](const auto& route) {
            return std::any_of(
                route.candidates.begin(), route.candidates.end(),
                [](const auto& candidate) {
                    return candidate.step_accepted &&
                           candidate.hiita_on_field.state == "YES" &&
                           !candidate.returned_to_same_hiita_idle_boundary;
                });
        });
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
    if (report.place_trace_complete && teacher_selected_cancel(report) &&
        legal_hiita_progression_proven(report)) {
        return "TEACHER_CONTINUATION_COMMITMENT_LOSS_CONFIRMED";
    }
    if (!report.finish_reachable) {
        if (report.place_trace_complete) {
            return "COUNTERFACTUAL_SEARCH_INCONCLUSIVE";
        }
        if (report.path_bound_exhausted) {
            return "PATH_BOUND_EXHAUSTED";
        }
        if (report.depth4_frontier_exhaustive) {
            return "DEPTH_BOUNDED_FRONTIER_EXHAUSTED_NO_FINISH";
        }
        if (report.depth_bound_exhausted) {
            return "DEPTH_BOUND_EXHAUSTED";
        }
        return "COUNTERFACTUAL_SEARCH_INCONCLUSIVE";
    }
    if (teacher_selected_cancel(report) && report.finish_accepted &&
        !report.returned_immediately_to_same_hiita_idle_boundary) {
        if (report.unselect_ranking->fallback_level ==
            std::optional<TeacherFallbackLevel>{TeacherFallbackLevel::F4}) {
            return "TEACHER_CONTINUATION_COMMITMENT_LOSS_CONFIRMED";
        }
        return "TEACHER_SELECTION_BUG_CONFIRMED_F4_HYPOTHESIS_FALSE";
    }
    return "COUNTERFACTUAL_SEARCH_INCONCLUSIVE";
}

std::string optional_size(const std::optional<std::size_t>& value) {
    return value.has_value() ? std::to_string(*value) : "ABSENT";
}

std::size_t node_count_at_depth(const ProbeReport& report,
                                const std::size_t depth) {
    return static_cast<std::size_t>(std::count_if(
        report.frontier_nodes.begin(), report.frontier_nodes.end(),
        [depth](const auto& node) { return node.path.size() == depth; }));
}

std::size_t unique_node_count_at_depth(const ProbeReport& report,
                                       const std::size_t depth) {
    std::vector<std::string> keys;
    for (const auto& node : report.frontier_nodes) {
        if (node.path.size() != depth ||
            std::find(keys.begin(), keys.end(), node.frontier_public_key) !=
                keys.end()) {
            continue;
        }
        keys.push_back(node.frontier_public_key);
    }
    return keys.size();
}

std::size_t expanded_node_count(const ProbeReport& report) {
    return static_cast<std::size_t>(std::count_if(
        report.frontier_nodes.begin(), report.frontier_nodes.end(),
        [](const auto& node) { return node.expanded; }));
}

std::size_t unique_expanded_node_count(const ProbeReport& report) {
    std::vector<std::string> keys;
    for (const auto& node : report.frontier_nodes) {
        if (!node.expanded ||
            std::find(keys.begin(), keys.end(), node.frontier_public_key) !=
                keys.end()) {
            continue;
        }
        keys.push_back(node.frontier_public_key);
    }
    return keys.size();
}

void emit_initial_material_summary(std::ostream& output, const char* label,
                                   const InitialMaterialSummary& summary) {
    output << "INITIAL_MATERIAL_" << label << "_PASSCODE=" << summary.passcode
           << '\n'
           << "INITIAL_MATERIAL_" << label << "_SOURCE_LOCATOR="
           << summary.source_locator << '\n'
           << "INITIAL_MATERIAL_" << label << "_PUBLIC_ACTION_KEY="
           << summary.public_action_key << '\n'
           << "INITIAL_MATERIAL_" << label << "_NEXT_REQUEST_KIND="
           << summary.next_request_kind << '\n'
           << "INITIAL_MATERIAL_" << label << "_NEXT_CANDIDATE_COUNT="
           << summary.next_candidate_count << '\n'
           << "INITIAL_MATERIAL_" << label << "_FINISH_PRESENT="
           << summary.next_finish_present << '\n'
           << "INITIAL_MATERIAL_" << label << "_CANCEL_PRESENT="
           << summary.next_cancel_present << '\n'
           << "INITIAL_MATERIAL_" << label << "_PUBLIC_BOUNDARY_KEY="
           << summary.next_public_boundary_key << '\n'
           << "INITIAL_MATERIAL_" << label << "_CHARACTERIZED="
           << (summary.public_action_key == "ABSENT" ||
                       summary.next_request_kind == "ABSENT"
                   ? "NO"
                   : "YES")
           << '\n';
}

void emit_frontier_node(std::ostream& output, const std::size_t index,
                        const FrontierNode& node) {
    const auto prefix = "FRONTIER_NODE_" + std::to_string(index);
    output << prefix << "_INDEX=" << index << '\n'
           << prefix << "_PATH_DEPTH=" << node.path.size() << '\n'
           << prefix << "_PATH_PUBLIC_ACTION_KEYS=" << path_text(node.path) << '\n'
           << prefix << "_PUBLIC_KEY=" << node.frontier_public_key << '\n'
           << prefix << "_DECISION_INDEX=" << node.frame.decision_index << '\n'
           << prefix << "_ENGINE_STEP_INDEX=" << node.frame.engine_step_index << '\n'
           << prefix << "_ACTING_PLAYER="
           << static_cast<unsigned>(node.frame.acting_player) << '\n'
           << prefix << "_REQUEST_KIND="
           << ygo::environment::environment_decision_kind_name(
                  node.frame.request.kind)
           << '\n'
           << prefix << "_TURN_PLAYER=" << optional_u8(node.progress.turn_player)
           << '\n'
           << prefix << "_TURN_COUNT=" << optional_u32(node.progress.turn_count)
           << '\n'
           << prefix << "_PHASE=" << optional_u32(node.progress.phase) << '\n'
           << prefix << "_PUBLIC_OBSERVATION_DIGEST="
           << node.frame.public_observation_digest << '\n'
           << prefix << "_PUBLIC_CANDIDATE_DOMAIN_DIGEST="
           << node.frame.public_candidate_domain_digest << '\n'
           << prefix << "_PUBLIC_SEMANTIC_DECISION_ID="
           << node.frame.public_semantic_decision_id << '\n'
           << prefix << "_CANDIDATE_COUNT="
           << node.frame.request.candidates.size() << '\n'
           << prefix << "_CONTINUATION_PRESENT="
           << (node.frame.request.continuation.has_value() ? "YES" : "NO")
           << '\n'
           << prefix << "_HAS_FINISH=" << (node.has_finish ? "YES" : "NO")
           << '\n'
           << prefix << "_HAS_CANCEL=" << (node.has_cancel ? "YES" : "NO")
           << '\n'
           << prefix << "_NON_CANCEL_CANDIDATE_COUNT="
           << node.non_cancel_candidate_count << '\n'
           << prefix << "_EXPANDED=" << (node.expanded ? "YES" : "NO") << '\n'
           << prefix << "_MATERIAL_PATH_SELECTED="
           << (node.material_operation == "UNPROVEN" ? "UNPROVEN"
                                                      : node.material_path_references)
           << '\n'
           << prefix << "_MATERIAL_PATH_REFERENCES="
           << node.material_path_references << '\n'
           << prefix << "_MATERIAL_OPERATION=" << node.material_operation << '\n';
    emit_continuation(output, prefix, node.frame.request.continuation);
    emit_candidates(output, prefix + "_CANDIDATE", node.frame);
}

void emit_frontier_edge(std::ostream& output, const std::size_t index,
                        const FrontierEdge& edge) {
    const auto prefix = "FRONTIER_EDGE_" + std::to_string(index);
    output << prefix << "_INDEX=" << index << '\n'
           << prefix << "_SOURCE_NODE_INDEX=" << edge.source_node_index << '\n'
           << prefix << "_SOURCE_DEPTH=" << edge.source_depth << '\n'
           << prefix << "_SOURCE_FRONTIER_KEY=" << edge.source_frontier_key << '\n'
           << prefix << "_ORDINAL=" << edge.candidate_ordinal << '\n'
           << prefix << "_ACTION_KIND=" << edge.action_kind << '\n'
           << prefix << "_PUBLIC_ACTION_KEY=" << edge.public_action_key << '\n'
           << prefix << "_MATERIAL_OPERATION=" << edge.material_operation << '\n'
           << prefix << "_RESULT_BOUNDARY_KIND=" << edge.result_boundary_kind
           << '\n'
           << prefix << "_EDGE_CLASS=" << edge.edge_class << '\n'
           << prefix << "_TARGET_FRONTIER_KEY=" << edge.target_frontier_key << '\n'
           << prefix << "_TARGET_NODE_INDEX="
           << optional_size(edge.target_node_index) << '\n';
}

void emit_place_candidate_trace(std::ostream& output,
                                const std::string& prefix,
                                const DecisionFrame& place_frame,
                                const std::size_t ordinal,
                                const PlaceCandidateTrace& trace) {
    output << prefix << "_ORDINAL=" << ordinal << '\n';
    emit_candidate(output, prefix, place_frame.public_observation,
                   trace.candidate);
    output << prefix << "_STEP_ACCEPTED="
           << (trace.step_accepted ? "YES" : "NO") << '\n'
           << prefix << "_NEXT_BOUNDARY_KIND=" << trace.next_boundary_kind << '\n'
           << prefix << "_RETURNED_TO_SAME_HIITA_IDLE_BOUNDARY="
           << (trace.returned_to_same_hiita_idle_boundary ? "YES" : "NO")
           << '\n'
           << prefix << "_NEXT_HIITA_ON_FIELD="
           << trace.hiita_on_field.state << '\n'
           << prefix << "_NEXT_HIITA_FIELD_LOCATOR="
           << trace.hiita_on_field.locator << '\n';
    if (!trace.next_frame.has_value()) {
        output << prefix << "_NEXT_DECISION_INDEX=ABSENT\n"
               << prefix << "_NEXT_ENGINE_STEP_INDEX=ABSENT\n"
               << prefix << "_NEXT_ACTING_PLAYER=ABSENT\n"
               << prefix << "_NEXT_REQUEST_KIND=ABSENT\n"
               << prefix << "_NEXT_PUBLIC_BOUNDARY_KEY=ABSENT\n"
               << prefix << "_NEXT_PUBLIC_OBSERVATION_DIGEST=ABSENT\n"
               << prefix << "_NEXT_PUBLIC_CANDIDATE_DOMAIN_DIGEST=ABSENT\n"
               << prefix << "_NEXT_PUBLIC_SEMANTIC_DECISION_ID=ABSENT\n"
               << prefix << "_NEXT_CANDIDATE_COUNT=ABSENT\n"
               << prefix << "_NEXT_CONTINUATION_PRESENT=ABSENT\n"
               << prefix << "_NEXT_HAS_FINISH=ABSENT\n"
               << prefix << "_NEXT_HAS_CANCEL=ABSENT\n"
               << prefix << "_NEXT_NON_CANCEL_CANDIDATE_COUNT=ABSENT\n";
        emit_progress(output, prefix + "_NEXT", std::nullopt);
        return;
    }

    const auto& next = *trace.next_frame;
    output << prefix << "_NEXT_DECISION_INDEX=" << next.decision_index << '\n'
           << prefix << "_NEXT_ENGINE_STEP_INDEX=" << next.engine_step_index
           << '\n'
           << prefix << "_NEXT_ACTING_PLAYER="
           << static_cast<unsigned>(next.acting_player) << '\n'
           << prefix << "_NEXT_REQUEST_KIND="
           << ygo::environment::environment_decision_kind_name(next.request.kind)
           << '\n'
           << prefix << "_NEXT_PUBLIC_BOUNDARY_KEY="
           << frontier_public_key(next) << '\n'
           << prefix << "_NEXT_PUBLIC_OBSERVATION_DIGEST="
           << next.public_observation_digest << '\n'
           << prefix << "_NEXT_PUBLIC_CANDIDATE_DOMAIN_DIGEST="
           << next.public_candidate_domain_digest << '\n'
           << prefix << "_NEXT_PUBLIC_SEMANTIC_DECISION_ID="
           << next.public_semantic_decision_id << '\n'
           << prefix << "_NEXT_CANDIDATE_COUNT="
           << next.request.candidates.size() << '\n'
           << prefix << "_NEXT_CONTINUATION_PRESENT="
           << (next.request.continuation.has_value() ? "YES" : "NO") << '\n'
           << prefix << "_NEXT_HAS_FINISH="
           << (finish_key_for(next).has_value() ? "YES" : "NO") << '\n'
           << prefix << "_NEXT_HAS_CANCEL="
           << (has_candidate_kind(next, EnvironmentActionKind::Cancel) ? "YES"
                                                                         : "NO")
           << '\n'
           << prefix << "_NEXT_NON_CANCEL_CANDIDATE_COUNT="
           << non_cancel_candidate_count(next) << '\n';
    emit_progress(output, prefix + "_NEXT", trace.next_progress);
    emit_continuation(output, prefix + "_NEXT", next.request.continuation);
    emit_candidates(output, prefix + "_NEXT_CANDIDATE", next);
}

void emit_place_route(std::ostream& output, const std::size_t index,
                      const PlaceRouteTrace& route) {
    const auto prefix = "PLACE_ROUTE_" + std::to_string(index);
    const auto& frame = route.place_frame;
    output << prefix << "_MATERIAL_ROUTE=" << route.material_route << '\n'
           << prefix << "_PATH_PUBLIC_ACTION_KEYS=" << path_text(route.path) << '\n'
           << prefix << "_PUBLIC_BOUNDARY_KEY=" << frontier_public_key(frame)
           << '\n'
           << prefix << "_DECISION_INDEX=" << frame.decision_index << '\n'
           << prefix << "_ENGINE_STEP_INDEX=" << frame.engine_step_index << '\n'
           << prefix << "_ACTING_PLAYER="
           << static_cast<unsigned>(frame.acting_player) << '\n'
           << prefix << "_REQUEST_KIND="
           << ygo::environment::environment_decision_kind_name(frame.request.kind)
           << '\n'
           << prefix << "_PUBLIC_OBSERVATION_DIGEST="
           << frame.public_observation_digest << '\n'
           << prefix << "_PUBLIC_CANDIDATE_DOMAIN_DIGEST="
           << frame.public_candidate_domain_digest << '\n'
           << prefix << "_PUBLIC_SEMANTIC_DECISION_ID="
           << frame.public_semantic_decision_id << '\n'
           << prefix << "_CANDIDATE_COUNT=" << frame.request.candidates.size()
           << '\n'
           << prefix << "_CONTINUATION_PRESENT="
           << (frame.request.continuation.has_value() ? "YES" : "NO") << '\n'
           << prefix << "_HAS_FINISH="
           << (finish_key_for(frame).has_value() ? "YES" : "NO") << '\n'
           << prefix << "_HAS_CANCEL="
           << (has_candidate_kind(frame, EnvironmentActionKind::Cancel) ? "YES"
                                                                         : "NO")
           << '\n'
           << prefix << "_NON_CANCEL_CANDIDATE_COUNT="
           << non_cancel_candidate_count(frame) << '\n';
    emit_progress(output, prefix, route.place_progress);
    emit_continuation(output, prefix, frame.request.continuation);
    for (std::size_t candidate_index = 0;
         candidate_index < route.candidates.size(); ++candidate_index) {
        emit_place_candidate_trace(
            output, prefix + "_CANDIDATE_" + std::to_string(candidate_index),
            frame, candidate_index, route.candidates[candidate_index]);
    }
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

    std::cout << "VISIBLE_REFERENCE_RESOLUTION="
              << (report.visible_reference_resolution ? "PASS" : "FAIL") << '\n'
              << "FRONTIER_NODES_RECORDED="
              << (!report.frontier_nodes.empty() ? "YES" : "NO") << '\n'
              << "FRONTIER_TOPOLOGY_METRICS_PRESENT=YES\n"
              << "PUBLIC_BOUNDARY_REVISITS_CLASSIFIED=YES\n"
              << "PUBLIC_BOUNDARY_CONVERGENCE_CLASSIFIED=YES\n"
              << "PATH_BOUND_STATE_EXPLICIT=YES\n"
              << "DEPTH_BOUND_STATE_EXPLICIT=YES\n";
    emit_initial_material_summary(std::cout, "A", report.initial_material[0]);
    emit_initial_material_summary(std::cout, "B", report.initial_material[1]);
    std::cout << "LEGAL_NON_CANCEL_ROUTE_OUT_OF_UNSELECT="
              << (report.legal_non_cancel_route_out_of_unselect ? "YES" : "NO")
              << '\n'
              << "PLACE_TRACE_ROUTE_COUNT=" << report.place_routes.size() << '\n'
              << "PLACE_TRACE_CANDIDATE_COUNT="
              << report.place_candidate_trace_count << '\n'
              << "PLACE_TRACE_COMPLETE="
              << (report.place_trace_complete ? "YES" : "NO") << '\n'
              << "HIITA_FIELD_COMPLETION_COUNT="
              << report.hiita_field_completion_count << '\n'
              << "HIITA_FIELD_ABSENT_COUNT=" << report.hiita_field_absent_count
              << '\n'
              << "HIITA_FIELD_UNPROVEN_COUNT="
              << report.hiita_field_unproven_count << '\n'
              << "HIITA_SUMMON_COMPLETION="
              << (report.hiita_summon_completion
                      ? "YES"
                      : (report.place_trace_complete &&
                                 report.hiita_field_unproven_count == 0
                             ? "NO"
                             : "NOT_YET_PROVEN"))
              << '\n'
              << "HIITA_SUMMON_COMPLETION_ALL_PLACE_CANDIDATES="
              << (report.hiita_summon_completion_all_routes
                      ? "YES"
                      : (report.place_trace_complete &&
                                 report.hiita_field_unproven_count == 0
                             ? "NO"
                             : "NOT_YET_PROVEN"))
              << '\n'
              << "RETURN_TO_SAME_HIITA_IDLE="
              << (report.place_returned_to_same_hiita_idle ? "YES" : "NO")
              << '\n'
              << "DEPTH4_MATERIAL_FRONTIER_EXHAUSTIVE="
              << (report.depth4_frontier_exhaustive ? "YES" : "NO") << '\n'
              << "FULL_LEGAL_FRONTIER_EXHAUSTIVE=NO\n";
    for (std::size_t index = 0; index < report.place_routes.size(); ++index) {
        emit_place_route(std::cout, index, report.place_routes[index]);
    }
    std::cout << "MAX_CONTINUATION_DEPTH=" << kMaxContinuationDepth << '\n'
              << "ORIGINAL_COUNTERFACTUAL_PATH_LIMIT="
              << kOriginalCounterfactualPathLimit << '\n'
              << "COUNTERFACTUAL_PATH_LIMIT=" << kMaxCounterfactualPaths << '\n'
              << "COUNTERFACTUAL_PATH_RECORDING_CAPACITY_INCREASE="
              << (kMaxCounterfactualPaths - kOriginalCounterfactualPathLimit) << '\n'
              << "COUNTERFACTUAL_PATH_RECORDING_CAPACITY_REASON="
                 "original_16_path_cutoff_queue_was_measured" << '\n'
              << "UNIQUE_PUBLIC_FRONTIER_NODE_COUNT="
              << report.frontier_seen.size() << '\n'
              << "EXPANDED_PUBLIC_FRONTIER_NODE_COUNT="
              << expanded_node_count(report) << '\n'
              << "UNIQUE_EXPANDED_PUBLIC_FRONTIER_NODE_COUNT="
              << unique_expanded_node_count(report) << '\n'
              << "REVISITED_PUBLIC_BOUNDARY_COUNT="
              << report.revisited_public_boundary_count << '\n'
              << "CONVERGED_PUBLIC_BOUNDARY_COUNT="
              << report.converged_public_boundary_count << '\n';
    for (std::size_t depth = 1; depth <= kMaxContinuationDepth; ++depth) {
        std::cout << "DEPTH_" << depth << "_NODE_COUNT="
                  << node_count_at_depth(report, depth) << '\n'
                  << "DEPTH_" << depth << "_UNIQUE_PUBLIC_FRONTIER_NODE_COUNT="
                  << unique_node_count_at_depth(report, depth) << '\n';
    }
    std::cout << "MAX_OBSERVED_DEPTH=" << report.max_observed_depth << '\n'
              << "MAX_CANDIDATE_COUNT=" << report.max_candidate_count << '\n'
              << "TOTAL_NON_CANCEL_EDGES=" << report.total_non_cancel_edges << '\n'
              << "TOTAL_NON_CANCEL_CANDIDATES_SEEN="
              << report.total_non_cancel_candidates_seen << '\n'
              << "TOTAL_FINISH_EDGES=" << report.total_finish_edges << '\n'
              << "TOTAL_CANCEL_EDGES=" << report.total_cancel_edges << '\n'
              << "QUEUED_UNEXPLORED_AT_BOUND="
              << report.queued_unexplored_at_original_bound << '\n'
              << "QUEUED_UNEXPLORED_AT_EFFECTIVE_BOUND="
              << report.queued_unexplored_at_effective_bound << '\n'
              << "ORIGINAL_PATH_BOUND_REACHED="
              << (report.original_path_bound_reached ? "YES" : "NO") << '\n'
              << "PATH_BOUND_EXHAUSTED="
              << (report.path_bound_exhausted ? "YES" : "NO") << '\n'
              << "DEPTH_BOUND_EXHAUSTED="
              << (report.depth_bound_exhausted ? "YES" : "NO") << '\n'
              << "PUBLIC_FRONTIER_CYCLE_FOUND="
              << (report.public_frontier_cycle_count != 0 ? "YES" : "NO") << '\n'
              << "PUBLIC_FRONTIER_CYCLE_COUNT="
              << report.public_frontier_cycle_count << '\n'
              << "CYCLE_LENGTH="
              << (report.public_frontier_cycle_count == 0
                      ? "ABSENT"
                      : std::to_string(report.cycle_length))
              << '\n'
              << "CYCLE_PATH=" << report.cycle_path << '\n'
              << "CYCLE_BOUNDARY_KEYS=" << report.cycle_boundary_keys << '\n'
              << "FRONTIER_NODE_COUNT=" << report.frontier_nodes.size() << '\n';
    for (std::size_t index = 0; index < report.frontier_nodes.size(); ++index) {
        emit_frontier_node(std::cout, index, report.frontier_nodes[index]);
    }
    std::cout << "FRONTIER_EDGE_COUNT=" << report.frontier_edges.size() << '\n';
    for (std::size_t index = 0; index < report.frontier_edges.size(); ++index) {
        emit_frontier_edge(std::cout, index, report.frontier_edges[index]);
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
              << "PUBLIC_BOUNDARY_EQUIVALENCE_ONLY=YES\n"
              << "FULL_STATE_EQUIVALENCE_PROVEN=NO\n"
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
        classification == "DEPTH_BOUNDED_FRONTIER_EXHAUSTED_NO_FINISH" ||
        classification == "PATH_BOUND_EXHAUSTED" ||
        classification == "DEPTH_BOUND_EXHAUSTED" ||
        classification == "NO_COMPLETING_PUBLIC_CONTINUATION_PATH_FOUND" ||
        classification == "COUNTERFACTUAL_SEARCH_INCONCLUSIVE";
    return report.error.empty() && report.target_found && report.unselect_found &&
           report.visible_reference_resolution && report.prefix_replay_exact &&
           report.legal_non_cancel_route_out_of_unselect &&
           report.place_trace_complete && supported_classification;
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
        trace_place_completions(job, report);
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
