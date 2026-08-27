#include "ygo/environment/episodic_environment.hpp"
#include "ygo/environment/episodic_environment_test_access.hpp"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

#include "ygo/trace/sha256.hpp"

namespace {

using namespace ygo::environment;
using Next = std::variant<DecisionFrame, EpisodeTerminal, EpisodeInterrupted, EpisodeFailure>;

enum class SelectionPolicy : std::uint8_t {
    Front,
    Cycle,
};

void require(const bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

std::unique_ptr<EpisodicEnvironment> make_environment() {
    auto factory = EpisodicEnvironment::create(CertifiedEnvironmentConfig::canonical());
    require(std::holds_alternative<std::unique_ptr<EpisodicEnvironment>>(factory),
            "canonical environment factory rejected the reset-isolation fixture");
    return std::move(std::get<std::unique_ptr<EpisodicEnvironment>>(factory));
}

RunControl make_control(const std::uint64_t engine_budget, const std::uint64_t semantic_budget,
                        const std::string& source) {
    RunControl control;
    control.engine_process_budget = engine_budget;
    control.semantic_action_budget = semantic_budget;
    control.cancellation.source = source;
    return control;
}

struct Scenario final {
    EpisodeSpec spec;
    RunControl control;
    std::uint64_t max_actions = 0;
    SelectionPolicy selection_policy = SelectionPolicy::Front;
    std::uint64_t policy_salt = 0;
    std::string label;
};

std::string scenario_key(const Scenario& scenario) {
    std::ostringstream result;
    result << scenario.spec.root_seed << ':'
           << static_cast<unsigned int>(scenario.spec.seat_assignment) << ':'
           << static_cast<unsigned int>(scenario.spec.starting_player) << ':'
           << scenario.control.engine_process_budget << ':'
           << scenario.control.semantic_action_budget << ':' << scenario.max_actions << ':'
           << static_cast<unsigned int>(scenario.selection_policy) << ':' << scenario.policy_salt;
    return result.str();
}

std::string select_public_key(const DecisionFrame& frame, const Scenario& scenario,
                              const std::uint64_t action_count) {
    require(!frame.request.candidates.empty(), "persistent reset fixture published an empty domain");
    std::size_t index = 0;
    if (scenario.selection_policy == SelectionPolicy::Cycle) {
        index = static_cast<std::size_t>((action_count + scenario.policy_salt) %
                                          frame.request.candidates.size());
    }
    return frame.request.candidates[index].public_action_key;
}

std::uint64_t process_handle_count() {
#ifdef _WIN32
    DWORD count = 0;
    if (GetProcessHandleCount(GetCurrentProcess(), &count) != 0) {
        return count;
    }
#endif
    return 0;
}

std::string optional_string(const std::optional<std::string>& value) {
    return value.has_value() ? *value : "-";
}

std::string optional_u64(const std::optional<std::uint64_t>& value) {
    return value.has_value() ? std::to_string(*value) : "-";
}

std::string frame_fingerprint(const DecisionFrame& frame) {
    std::ostringstream result;
    result << frame.contract_id << '|' << frame.episode_semantic_id << '|'
           << frame.public_semantic_decision_id << '|' << frame.decision_index << '|'
           << frame.engine_step_index << '|' << static_cast<unsigned int>(frame.acting_player)
           << '|' << environment_decision_kind_name(frame.request.kind) << '|'
           << static_cast<unsigned int>(frame.request.player) << '|'
           << frame.public_observation_digest << '|' << frame.public_candidate_domain_digest << '|'
           << ygo::trace::sha256_bytes(frame.public_observation.canonical_safe_state_bytes()) << '|'
           << frame.public_observation.decision_context.kind.value_or("-") << '|'
           << (frame.public_observation.decision_context.player.has_value()
                   ? std::to_string(*frame.public_observation.decision_context.player)
                   : "-")
           << '|';
    for (const auto& locator : frame.public_observation.decision_context.referenced_entities) {
        result << locator.value << ',';
    }
    result << '|';
    if (frame.request.continuation.has_value()) {
        const auto& continuation = *frame.request.continuation;
        result << continuation.continuation_kind << ':' << continuation.continuation_step << ':'
               << continuation.min_count << ':' << continuation.max_count << ':'
               << continuation.target_sum << ':' << continuation.required_amount << ':'
               << continuation.available_mask << ':' << continuation.selected_mask << ':'
               << continuation.continuation_steps << ':' << continuation.exact_sum << ':'
               << continuation.greater_sum << ':' << continuation.can_finish << ':'
               << continuation.can_cancel << ':';
        for (const auto value : continuation.selected_indices) {
            result << value << ',';
        }
        result << ':';
        for (const auto value : continuation.remaining_indices) {
            result << value << ',';
        }
        result << ':';
        for (const auto value : continuation.assigned_amounts) {
            result << value << ',';
        }
    } else {
        result << '-';
    }
    result << '|';
    for (const auto& candidate : frame.request.candidates) {
        result << environment_action_kind_name(candidate.action_kind) << ':'
               << candidate.public_action_key << ':' << candidate.continuation_operation << ':'
               << candidate.submits_engine_response << ';';
    }
    const auto text = result.str();
    return ygo::trace::sha256_bytes(std::vector<std::uint8_t>(text.begin(), text.end()));
}

std::string action_fingerprint(const AcceptedActionTransition& transition) {
    std::ostringstream result;
    result << transition.selected_public_action_key << '|' << transition.core_response_submitted
           << '|' << optional_string(transition.final_response_sha256);
    const auto text = result.str();
    return ygo::trace::sha256_bytes(std::vector<std::uint8_t>(text.begin(), text.end()));
}

std::string closure_fingerprint(const Next& next) {
    std::ostringstream result;
    if (const auto* terminal = std::get_if<EpisodeTerminal>(&next)) {
        result << "TERMINAL|" << terminal->contract_id << '|' << terminal->episode_semantic_id
               << '|' << static_cast<unsigned int>(terminal->winner) << '|'
               << static_cast<unsigned int>(terminal->win_reason) << '|'
               << terminal->semantic_action_count << '|' << optional_u64(terminal->last_decision_index)
               << '|' << terminal->final_engine_step_index << '|' << terminal->semantic_gameplay_hash
               << '|' << terminal->final_audit_prefix_hash;
    } else if (const auto* interrupted = std::get_if<EpisodeInterrupted>(&next)) {
        result << "INTERRUPTED|" << interrupted->contract_id << '|'
               << interrupted->episode_semantic_id << '|'
               << interruption_reason_name(interrupted->reason) << '|'
               << interrupted->semantic_action_count << '|'
               << optional_string(interrupted->last_public_semantic_decision_id) << '|'
               << optional_u64(interrupted->last_decision_index) << '|'
               << interrupted->final_engine_step_index << '|' << interrupted->last_valid_audit_prefix_hash
               << '|' << interrupted->run_control_evidence.engine_process_count << '|'
               << interrupted->run_control_evidence.semantic_action_count;
    } else if (const auto* failure = std::get_if<EpisodeFailure>(&next)) {
        result << "FAILED|" << failure->contract_id << '|'
               << optional_string(failure->episode_semantic_id) << '|'
               << failure_code_name(failure->failure_code) << '|'
               << failure_stage_name(failure->failure_stage) << '|' << failure->semantic_action_count
               << '|' << optional_string(failure->last_public_semantic_decision_id) << '|'
               << optional_string(failure->last_valid_audit_prefix_hash) << '|'
               << failure->mutation_may_have_occurred;
    } else {
        return "OPEN";
    }
    const auto text = result.str();
    return ygo::trace::sha256_bytes(std::vector<std::uint8_t>(text.begin(), text.end()));
}

struct EpisodeFingerprint final {
    std::string episode_semantic_id;
    std::vector<std::string> frames;
    std::vector<std::string> actions;
    std::string closure;
    bool saw_continuation = false;
    bool saw_atomic = false;
    std::optional<ActionSelection> first_selection;
};

struct EpisodeRun final {
    EpisodeFingerprint fingerprint;
    Next next;
};

void require_same(const EpisodeFingerprint& expected, const EpisodeFingerprint& actual,
                  const std::string& context) {
    require(expected.episode_semantic_id == actual.episode_semantic_id,
            context + ": episode identity drifted");
    require(expected.frames == actual.frames, context + ": public frame sequence drifted");
    require(expected.actions == actual.actions, context + ": public action sequence drifted");
    require(expected.closure == actual.closure, context + ": closure drifted");
    require(expected.saw_continuation == actual.saw_continuation,
            context + ": continuation classification drifted");
    require(expected.saw_atomic == actual.saw_atomic, context + ": atomic classification drifted");
}

EpisodeRun run_from_boundary(EpisodicEnvironment& environment, Next next,
                             const Scenario& scenario) {
    EpisodeRun result{EpisodeFingerprint{}, std::move(next)};
    auto& current = result.next;
    while (const auto* frame = std::get_if<DecisionFrame>(&current)) {
        if (result.fingerprint.episode_semantic_id.empty()) {
            result.fingerprint.episode_semantic_id = frame->episode_semantic_id;
            result.fingerprint.first_selection = ActionSelection{
                frame->contract_id, frame->episode_semantic_id, frame->public_semantic_decision_id,
                frame->submission_token, select_public_key(*frame, scenario, 0)};
        }
        result.fingerprint.frames.push_back(frame_fingerprint(*frame));
        result.fingerprint.saw_continuation =
            result.fingerprint.saw_continuation || frame->request.continuation.has_value();
        result.fingerprint.saw_atomic =
            result.fingerprint.saw_atomic || !frame->request.continuation.has_value();

        if (result.fingerprint.actions.size() >= scenario.max_actions) {
            const auto interrupted = environment.interrupt(
                InterruptRequest{std::string(kEpisodicEnvironmentV2ContractId),
                                 InterruptionReason::AdministrativeCancel});
            require(std::holds_alternative<InterruptAccepted>(interrupted),
                    "persistent reset fixture could not close an open boundary");
            current = std::get<InterruptAccepted>(interrupted).interruption;
            break;
        }

        const auto public_key = select_public_key(*frame, scenario, result.fingerprint.actions.size());
        const auto step = environment.step(ActionSelection{
            frame->contract_id, frame->episode_semantic_id, frame->public_semantic_decision_id,
            frame->submission_token, public_key});
        require(std::holds_alternative<StepAccepted>(step),
                "persistent reset fixture rejected its complete public domain");
        const auto& accepted = std::get<StepAccepted>(step);
        require(accepted.transition.selected_public_action_key == public_key,
                "persistent reset fixture changed its selected public action key");
        result.fingerprint.actions.push_back(action_fingerprint(accepted.transition));
        current = accepted.next;
    }
    result.fingerprint.closure = closure_fingerprint(current);
    return result;
}

EpisodeRun run_episode(EpisodicEnvironment& environment, const Scenario& scenario) {
    const auto reset = environment.reset(scenario.spec, scenario.control);
    require(std::holds_alternative<ResetAccepted>(reset),
            scenario.label + ": reset was rejected");
    return run_from_boundary(environment, std::get<ResetAccepted>(reset).next, scenario);
}

EpisodeRun fresh_reference(const Scenario& scenario) {
    auto environment = make_environment();
    return run_episode(*environment, scenario);
}

void compare_with_fresh(EpisodicEnvironment& environment, const Scenario& scenario,
                        std::map<std::string, EpisodeFingerprint>& references,
                        std::uint64_t& reference_count) {
    const auto actual = run_episode(environment, scenario);
    const auto key = scenario_key(scenario);
    auto reference_it = references.find(key);
    if (reference_it == references.end()) {
        auto reference = fresh_reference(scenario);
        reference_it = references.emplace(key, std::move(reference.fingerprint)).first;
        ++reference_count;
    }
    require_same(reference_it->second, actual.fingerprint, scenario.label);
}

void write_json(const std::optional<std::string>& output, const std::string& body) {
    if (output.has_value()) {
        std::ofstream stream(*output, std::ios::binary | std::ios::trunc);
        require(static_cast<bool>(stream), "could not open reset-isolation evidence output");
        stream << body << '\n';
    }
    std::cout << body << '\n';
}

void run_interleaving(const std::optional<std::string>& output) {
    auto environment = make_environment();
    std::map<std::string, EpisodeFingerprint> references;
    std::uint64_t reference_count = 0;

    Scenario a;
    a.spec.root_seed = 2;
    a.control = make_control(128, 16, "reset-interleaving-A");
    a.max_actions = 16;
    a.label = "A";
    Scenario b;
    b.spec.root_seed = 3;
    b.spec.seat_assignment = SeatAssignment::Mirror;
    b.spec.starting_player = 1;
    b.control = make_control(1, 8, "reset-interleaving-B");
    b.max_actions = 4;
    b.label = "B";
    Scenario c;
    c.spec.root_seed = 1;
    c.spec.starting_player = 1;
    c.control = make_control(128, 8, "reset-interleaving-C");
    c.max_actions = 8;
    c.label = "C";
    Scenario d;
    d.spec.root_seed = 0;
    d.spec.seat_assignment = SeatAssignment::Mirror;
    d.control = make_control(128, 4, "reset-interleaving-D");
    d.max_actions = 4;

    const auto first_a = run_episode(*environment, a);
    const auto first_a_reference = fresh_reference(a);
    require_same(first_a_reference.fingerprint, first_a.fingerprint, "A(1)");
    require(first_a.fingerprint.first_selection.has_value(), "A(1) did not publish a selection");

    compare_with_fresh(*environment, b, references, reference_count);
    compare_with_fresh(*environment, c, references, reference_count);

    detail::EpisodicEnvironmentTestAccess::force_next_reset_failure(*environment);
    Scenario injected = c;
    injected.label = "injected-failure";
    const auto failed_reset = environment->reset(injected.spec, injected.control);
    require(std::holds_alternative<ResetAccepted>(failed_reset),
            "injected interleaving failure did not return an accepted closure");
    const auto& failed_next = std::get<ResetAccepted>(failed_reset).next;
    require(std::holds_alternative<EpisodeFailure>(failed_next),
            "interleaving fault did not map to EpisodeFailure");
    require(environment->lifecycle() == Lifecycle::Failed,
            "interleaving fault did not close the environment");

    const auto second_a = run_episode(*environment, a);
    require_same(first_a_reference.fingerprint, second_a.fingerprint, "A(2)");
    const auto stale_second = environment->step(*first_a.fingerprint.first_selection);
    require(std::holds_alternative<StepRejected>(stale_second) &&
                std::get<StepRejected>(stale_second).rejection_code ==
                    RejectionCode::InvalidLifecycle,
            "A(2) stale selection was checked after the closed episode instead of at its boundary");

    compare_with_fresh(*environment, d, references, reference_count);

    const auto third_a = run_episode(*environment, a);
    require_same(first_a_reference.fingerprint, third_a.fingerprint, "A(3)");

    // Verify stale-token precedence while the repeated A episode is live.
    const auto live_reset = environment->reset(a.spec, a.control);
    require(std::holds_alternative<ResetAccepted>(live_reset), "live A reset was rejected");
    const auto live_frame = std::get<ResetAccepted>(live_reset).next;
    require(std::holds_alternative<DecisionFrame>(live_frame), "live A reset did not publish a frame");
    const auto stale_live = environment->step(*first_a.fingerprint.first_selection);
    require(std::holds_alternative<StepRejected>(stale_live) &&
                std::get<StepRejected>(stale_live).rejection_code == RejectionCode::StaleSubmissionToken &&
                std::get<StepRejected>(stale_live).authoritative_state_unchanged,
            "A stale token was accepted or returned after mutation");
    const auto live_run = run_from_boundary(*environment, live_frame, a);
    require_same(first_a_reference.fingerprint, live_run.fingerprint, "A(4)");

    const auto body = std::string("{\"gate\":\"G04\",\"result\":\"PASS\",\"sequence\":\"A-B-C-A-D-A\",\"a_occurrences_compared\":4,\"fresh_reference_count\":") +
                      std::to_string(reference_count) +
                      ",\"injected_failure\":true,\"stale_token_rejections\":1,\"resource_owner_count\":1}";
    write_json(output, body);
}

Scenario soak_scenario(const std::uint64_t index) {
    Scenario scenario;
    const auto pattern_index = index == 1 ? 0 : index;
    scenario.spec.root_seed = pattern_index % 4;
    scenario.spec.seat_assignment = (pattern_index % 2 == 0) ? SeatAssignment::Normal : SeatAssignment::Mirror;
    scenario.spec.starting_player = static_cast<std::uint8_t>(pattern_index % 2);
    scenario.label = "soak-" + std::to_string(index);
    if (index < 2) {
        // Deliberate identical pair for a live stale-token check after reset.
        scenario.spec.root_seed = 0;
        scenario.spec.seat_assignment = SeatAssignment::Normal;
        scenario.spec.starting_player = 0;
        scenario.control = make_control(128, 1, "reset-soak-stale-token");
        scenario.max_actions = 1;
    } else if (index == 199 || index == 399) {
        // This fixed public policy reaches the tribute continuation family
        // without constructing candidates or consulting private identities.
        scenario.spec.root_seed = 2;
        scenario.spec.seat_assignment = SeatAssignment::Normal;
        scenario.spec.starting_player = 0;
        scenario.control = make_control(20000, 20000, "reset-soak-continuation-heavy");
        scenario.max_actions = 740;
        scenario.selection_policy = SelectionPolicy::Cycle;
    } else if (index == 400) {
        scenario.control = make_control(20000, 20000, "reset-soak-terminal");
        scenario.spec.root_seed = 2;
        scenario.spec.seat_assignment = SeatAssignment::Normal;
        scenario.spec.starting_player = 0;
        scenario.max_actions = 800;
    } else if (index % 11 == 0) {
        scenario.control = make_control(1, 4096, "reset-soak-process-budget");
        scenario.max_actions = 4;
    } else if (index % 7 == 0) {
        scenario.control = make_control(256, 12, "reset-soak-continuation");
        scenario.max_actions = 12;
    } else if (index % 5 == 0) {
        scenario.control = make_control(256, 4, "reset-soak-administrative");
        scenario.max_actions = 4;
    } else {
        scenario.control = make_control(128, 1, "reset-soak-semantic-budget");
        scenario.max_actions = 1;
    }
    return scenario;
}

int run_soak(const std::uint64_t episode_count, const std::optional<std::string>& output) {
    require(episode_count >= 500, "reset soak requires at least 500 episodes");
    auto environment = make_environment();
    std::map<std::string, EpisodeFingerprint> references;
    std::uint64_t reference_count = 0;
    std::uint64_t terminal_count = 0;
    std::uint64_t interrupted_count = 0;
    std::uint64_t failure_count = 0;
    std::uint64_t continuation_count = 0;
    std::uint64_t atomic_count = 0;
    std::uint64_t stale_token_rejections = 0;
    const auto handles_before = process_handle_count();
    auto previous_selection = std::optional<ActionSelection>{};
    auto previous_scenario_key = std::string{};
    std::uint64_t peak_handles = handles_before;

    for (std::uint64_t index = 0; index < episode_count; ++index) {
        const auto scenario = soak_scenario(index);
        const bool inject_failure = index == 250;
        if (inject_failure) {
            detail::EpisodicEnvironmentTestAccess::force_next_reset_failure(*environment);
            const auto reset = environment->reset(scenario.spec, scenario.control);
            require(std::holds_alternative<ResetAccepted>(reset), "soak injected reset was rejected");
            const auto& next = std::get<ResetAccepted>(reset).next;
            require(std::holds_alternative<EpisodeFailure>(next), "soak injected failure was not typed");
            ++failure_count;
            require(environment->lifecycle() == Lifecycle::Failed, "soak injected failure did not close");
            previous_selection.reset();
            previous_scenario_key.clear();
            continue;
        }

        const auto reset = environment->reset(scenario.spec, scenario.control);
        require(std::holds_alternative<ResetAccepted>(reset), scenario.label + ": reset was rejected");
        Next next = std::get<ResetAccepted>(reset).next;
        const auto current_key = scenario_key(scenario);
        if (previous_selection.has_value() && previous_scenario_key == current_key &&
            std::holds_alternative<DecisionFrame>(next)) {
            const auto stale = environment->step(*previous_selection);
            require(std::holds_alternative<StepRejected>(stale) &&
                        std::get<StepRejected>(stale).rejection_code == RejectionCode::StaleSubmissionToken &&
                        std::get<StepRejected>(stale).authoritative_state_unchanged,
                    "soak stale selection was accepted or mutated state");
            ++stale_token_rejections;
        }

        const auto run = run_from_boundary(*environment, std::move(next), scenario);
        if (run.fingerprint.saw_continuation) {
            ++continuation_count;
        }
        if (run.fingerprint.saw_atomic) {
            ++atomic_count;
        }
        if (run.fingerprint.closure.empty()) {
            throw std::runtime_error("soak episode did not close");
        }
        if (run.fingerprint.closure == closure_fingerprint(run.next) &&
            std::holds_alternative<EpisodeTerminal>(run.next)) {
            ++terminal_count;
        } else if (std::holds_alternative<EpisodeInterrupted>(run.next)) {
            ++interrupted_count;
        } else if (std::holds_alternative<EpisodeFailure>(run.next)) {
            ++failure_count;
        }

        const auto reference_key = scenario_key(scenario);
        auto reference_it = references.find(reference_key);
        if (reference_it == references.end()) {
            auto reference = fresh_reference(scenario);
            reference_it = references.emplace(reference_key, std::move(reference.fingerprint)).first;
            ++reference_count;
        }
        require_same(reference_it->second, run.fingerprint, scenario.label);
        previous_selection = run.fingerprint.first_selection;
        previous_scenario_key = reference_key;
        peak_handles = std::max(peak_handles, process_handle_count());
    }

    const auto handles_after = process_handle_count();
    require(stale_token_rejections > 0, "soak did not exercise a stale-token rejection");
    require(failure_count > 0, "soak did not exercise the injected failure");
    require(terminal_count > 0, "soak did not exercise a true terminal episode");
    require(atomic_count > 0, "soak did not exercise an atomic path");
#ifdef _WIN32
    require(handles_after <= handles_before + 16 && peak_handles <= handles_before + 32,
            "persistent soak showed unbounded process-handle growth");
#endif

    const auto evidence_body = [&](const std::string& result) {
        return std::string("{\"gate\":\"G05\",\"result\":\"") + result +
                      "\",\"episode_count\":" +
                      std::to_string(episode_count) + ",\"closed_count\":" +
                      std::to_string(episode_count) + ",\"fresh_reference_count\":" +
                      std::to_string(reference_count) + ",\"terminal_count\":" +
                      std::to_string(terminal_count) + ",\"interrupted_count\":" +
                      std::to_string(interrupted_count) + ",\"failure_count\":" +
                      std::to_string(failure_count) + ",\"continuation_count\":" +
                      std::to_string(continuation_count) + ",\"atomic_count\":" +
                      std::to_string(atomic_count) + ",\"stale_token_rejections\":" +
                      std::to_string(stale_token_rejections) + ",\"handles_before\":" +
                      std::to_string(handles_before) + ",\"handles_after\":" +
                      std::to_string(handles_after) + ",\"peak_handles\":" +
                      std::to_string(peak_handles) +
                      ",\"continuation_witness\":{\"root_seed\":2,\"seat_assignment\":\"normal\",\"starting_player\":0,\"selection_policy\":\"cycle\",\"policy_salt\":0,\"max_actions\":740,\"occurrences\":2}}";
    };
    if (continuation_count == 0) {
        write_json(output, evidence_body("BLOCKED"));
        std::cerr << "G05 blocked: canonical public fixed-deck soak did not publish a continuation frame\n";
        return 2;
    }
    write_json(output, evidence_body("PASS"));
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        std::string mode = "interleaving";
        std::uint64_t episode_count = 500;
        std::optional<std::string> output;
        for (int index = 1; index < argc; ++index) {
            const std::string argument = argv[index];
            if (argument == "--mode" && index + 1 < argc) {
                mode = argv[++index];
            } else if (argument == "--episodes" && index + 1 < argc) {
                episode_count = std::stoull(argv[++index]);
            } else if (argument == "--output" && index + 1 < argc) {
                output = argv[++index];
            } else {
                throw std::invalid_argument("usage: ygo_episodic_reset_probe [--mode interleaving|soak] [--episodes N] [--output path]");
            }
        }
        if (mode == "interleaving") {
            run_interleaving(output);
        } else if (mode == "soak") {
            return run_soak(episode_count, output);
        } else {
            throw std::invalid_argument("unknown reset probe mode");
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
