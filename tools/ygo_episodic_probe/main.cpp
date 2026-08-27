#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

#include "ygo/environment/episodic_environment.hpp"

namespace {

using namespace ygo::environment;
using Next = std::variant<DecisionFrame, EpisodeTerminal, EpisodeInterrupted, EpisodeFailure>;

enum class SelectionPolicy : std::uint8_t {
    Front,
    Last,
    Cycle,
    Hash,
};

struct Arguments final {
    std::uint64_t seed = 2;
    std::uint64_t max_actions = 512;
    std::uint64_t engine_process_budget = 4096;
    std::uint64_t semantic_action_budget = 4096;
    std::optional<std::uint8_t> starting_player;
    bool mirror_seats = false;
    SelectionPolicy selection_policy = SelectionPolicy::Front;
    std::uint64_t policy_salt = 0;
    bool stop_on_continuation = false;
    std::optional<std::uint64_t> interrupt_after;
    std::string replay_path;
    std::string output_path;
};

std::uint64_t parse_u64(const std::string& value, const char* name) {
    std::size_t consumed = 0;
    const auto parsed = std::stoull(value, &consumed, 0);
    if (consumed != value.size()) {
        throw std::runtime_error(std::string("invalid ") + name + ": " + value);
    }
    return static_cast<std::uint64_t>(parsed);
}

SelectionPolicy parse_selection_policy(const std::string& value) {
    if (value == "front") {
        return SelectionPolicy::Front;
    }
    if (value == "last") {
        return SelectionPolicy::Last;
    }
    if (value == "cycle") {
        return SelectionPolicy::Cycle;
    }
    if (value == "hash") {
        return SelectionPolicy::Hash;
    }
    throw std::runtime_error("selection-policy must be front, last, cycle, or hash");
}

Arguments parse_arguments(const int argc, char** argv) {
    Arguments result;
    for (int index = 1; index < argc; ++index) {
        const std::string argument(argv[index]);
        if (argument == "--seed" && index + 1 < argc) {
            result.seed = parse_u64(argv[++index], "seed");
        } else if (argument == "--max-actions" && index + 1 < argc) {
            result.max_actions = parse_u64(argv[++index], "max-actions");
        } else if (argument == "--engine-process-budget" && index + 1 < argc) {
            result.engine_process_budget = parse_u64(argv[++index], "engine-process-budget");
        } else if (argument == "--semantic-action-budget" && index + 1 < argc) {
            result.semantic_action_budget = parse_u64(argv[++index], "semantic-action-budget");
        } else if (argument == "--starting-player" && index + 1 < argc) {
            const auto value = parse_u64(argv[++index], "starting-player");
            if (value > 1) {
                throw std::runtime_error("starting-player must be 0 or 1");
            }
            result.starting_player = static_cast<std::uint8_t>(value);
        } else if (argument == "--mirror-seats") {
            result.mirror_seats = true;
        } else if (argument == "--selection-policy" && index + 1 < argc) {
            result.selection_policy = parse_selection_policy(argv[++index]);
        } else if (argument == "--policy-salt" && index + 1 < argc) {
            result.policy_salt = parse_u64(argv[++index], "policy-salt");
        } else if (argument == "--stop-on-continuation") {
            result.stop_on_continuation = true;
        } else if (argument == "--choose-last-candidate") {
            result.selection_policy = SelectionPolicy::Last;
        } else if (argument == "--interrupt-after" && index + 1 < argc) {
            result.interrupt_after = parse_u64(argv[++index], "interrupt-after");
        } else if (argument == "--replay-public-actions" && index + 1 < argc) {
            result.replay_path = argv[++index];
        } else if (argument == "--output" && index + 1 < argc) {
            result.output_path = argv[++index];
        } else {
            throw std::runtime_error(
                "usage: ygo_episodic_probe [--seed N] [--max-actions N] "
                "[--engine-process-budget N] [--semantic-action-budget N] "
                "[--starting-player 0|1] [--mirror-seats] [--interrupt-after N] "
                "[--selection-policy front|last|cycle|hash] [--policy-salt N] "
                "[--stop-on-continuation] [--choose-last-candidate] "
                "[--replay-public-actions PATH] [--output PATH]");
        }
    }
    return result;
}

std::uint64_t fnv1a_append(std::uint64_t hash, const std::string& value) {
    for (const auto character : value) {
        hash ^= static_cast<unsigned char>(character);
        hash *= 1099511628211ULL;
    }
    return hash;
}

std::uint64_t fnv1a_append_u64(std::uint64_t hash, const std::uint64_t value) {
    auto remaining = value;
    for (unsigned int index = 0; index < 8; ++index) {
        hash ^= static_cast<unsigned char>(remaining & 0xffU);
        hash *= 1099511628211ULL;
        remaining >>= 8;
    }
    return hash;
}

std::size_t select_candidate_index(const DecisionFrame& frame, const std::uint64_t action_count,
                                   const Arguments& arguments) {
    const auto candidate_count = frame.request.candidates.size();
    if (candidate_count == 0) {
        throw std::runtime_error("public probe encountered an empty candidate domain");
    }
    switch (arguments.selection_policy) {
    case SelectionPolicy::Front:
        return 0;
    case SelectionPolicy::Last:
        return candidate_count - 1;
    case SelectionPolicy::Cycle:
        return static_cast<std::size_t>((action_count + arguments.policy_salt) % candidate_count);
    case SelectionPolicy::Hash: {
        auto hash = fnv1a_append(1469598103934665603ULL, frame.public_semantic_decision_id);
        hash = fnv1a_append(hash, frame.public_candidate_domain_digest);
        hash = fnv1a_append_u64(hash, action_count);
        hash = fnv1a_append_u64(hash, arguments.policy_salt);
        return static_cast<std::size_t>(hash % candidate_count);
    }
    }
    throw std::runtime_error("public probe selection policy is not implemented");
}

std::string json_escape(const std::string& value) {
    std::ostringstream result;
    result << '"';
    for (const unsigned char character : value) {
        switch (character) {
        case '"':
            result << "\\\"";
            break;
        case '\\':
            result << "\\\\";
            break;
        case '\n':
            result << "\\n";
            break;
        case '\r':
            result << "\\r";
            break;
        case '\t':
            result << "\\t";
            break;
        default:
            if (character < 0x20) {
                constexpr char hex[] = "0123456789abcdef";
                result << "\\u00" << hex[character >> 4] << hex[character & 0x0f];
            } else {
                result << static_cast<char>(character);
            }
            break;
        }
    }
    result << '"';
    return result.str();
}

std::string hex(const std::vector<std::uint8_t>& bytes) {
    constexpr char digits[] = "0123456789abcdef";
    std::string result;
    result.reserve(bytes.size() * 2);
    for (const auto byte : bytes) {
        result.push_back(digits[byte >> 4]);
        result.push_back(digits[byte & 0x0f]);
    }
    return result;
}

std::vector<std::string> read_replay_actions(const std::string& path) {
    if (path.empty()) {
        return {};
    }
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("cannot open public replay action file");
    }
    std::vector<std::string> actions;
    std::string line;
    while (std::getline(input, line)) {
        const auto first = line.find_first_not_of(" \t\r\n");
        if (first == std::string::npos) {
            continue;
        }
        const auto last = line.find_last_not_of(" \t\r\n");
        actions.push_back(line.substr(first, last - first + 1));
    }
    return actions;
}

void append_string_field(std::ostringstream& output, const char* name, const std::string& value,
                         bool& first) {
    if (!first) {
        output << ',';
    }
    first = false;
    output << json_escape(name) << ':' << json_escape(value);
}

void append_u64_field(std::ostringstream& output, const char* name, const std::uint64_t value,
                      bool& first) {
    if (!first) {
        output << ',';
    }
    first = false;
    output << json_escape(name) << ':' << value;
}

std::string frame_json(const DecisionFrame& frame) {
    std::ostringstream output;
    output << '{';
    bool first = true;
    append_u64_field(output, "decision_index", frame.decision_index, first);
    if (!first) output << ',';
    first = false;
    output << "\"acting_player\":" << static_cast<unsigned>(frame.acting_player);
    append_u64_field(output, "engine_step_index", frame.engine_step_index, first);
    append_string_field(output, "public_semantic_decision_id", frame.public_semantic_decision_id, first);
    append_string_field(output, "public_observation_digest", frame.public_observation_digest, first);
    append_string_field(output, "public_candidate_domain_digest", frame.public_candidate_domain_digest, first);
    append_string_field(output, "request_kind",
                        std::string(environment_decision_kind_name(frame.request.kind)), first);
    if (!first) output << ',';
    first = false;
    output << "\"continuation\":" << (frame.request.continuation.has_value() ? "true" : "false");
    if (!first) output << ',';
    first = false;
    output << "\"candidate_count\":" << frame.request.candidates.size() << ",\"public_action_keys\":[";
    for (std::size_t index = 0; index < frame.request.candidates.size(); ++index) {
        if (index != 0) output << ',';
        output << json_escape(frame.request.candidates[index].public_action_key);
    }
    output << "]";
    append_string_field(output, "safe_state_hex", hex(frame.public_observation.canonical_safe_state_bytes()), first);
    output << '}';
    return output.str();
}

void append_action_json(std::ostringstream& output, const StepAccepted& accepted,
                        const std::uint64_t decision_index, const std::string& public_key,
                        const bool first) {
    if (!first) output << ',';
    output << '{' << "\"decision_index\":" << decision_index
           << ",\"public_action_key\":" << json_escape(public_key)
           << ",\"core_response_submitted\":"
           << (accepted.transition.core_response_submitted ? "true" : "false") << '}';
}

void append_closure(std::ostringstream& output, const Next& next, const std::uint64_t action_count,
                    const bool limited) {
    output << "\"closure\":{";
    if (limited) {
        output << "\"kind\":\"LIMIT_REACHED\",\"action_count\":" << action_count;
    } else if (const auto* terminal = std::get_if<EpisodeTerminal>(&next)) {
        output << "\"kind\":\"TERMINAL\",\"winner\":"
               << static_cast<unsigned>(terminal->winner) << ",\"win_reason\":"
               << static_cast<unsigned>(terminal->win_reason) << ",\"semantic_action_count\":"
               << terminal->semantic_action_count << ",\"final_engine_step_index\":"
               << terminal->final_engine_step_index << ",\"semantic_gameplay_hash\":"
               << json_escape(terminal->semantic_gameplay_hash) << ",\"final_audit_prefix_hash\":"
               << json_escape(terminal->final_audit_prefix_hash);
        if (terminal->last_decision_index.has_value()) {
            output << ",\"last_decision_index\":" << *terminal->last_decision_index;
        }
    } else if (const auto* interrupted = std::get_if<EpisodeInterrupted>(&next)) {
        output << "\"kind\":\"INTERRUPTED\",\"reason\":"
               << json_escape(std::string(interruption_reason_name(interrupted->reason)))
               << ",\"semantic_action_count\":" << interrupted->semantic_action_count
               << ",\"final_engine_step_index\":" << interrupted->final_engine_step_index
               << ",\"last_valid_audit_prefix_hash\":"
               << json_escape(interrupted->last_valid_audit_prefix_hash)
               << ",\"engine_process_count\":"
               << interrupted->run_control_evidence.engine_process_count;
        if (interrupted->last_decision_index.has_value()) {
            output << ",\"last_decision_index\":" << *interrupted->last_decision_index;
        }
    } else if (const auto* failure = std::get_if<EpisodeFailure>(&next)) {
        output << "\"kind\":\"FAILURE\",\"failure_code\":"
               << json_escape(std::string(failure_code_name(failure->failure_code)))
               << ",\"failure_stage\":"
               << json_escape(std::string(failure_stage_name(failure->failure_stage)))
               << ",\"semantic_action_count\":" << failure->semantic_action_count
               << ",\"mutation_may_have_occurred\":"
               << (failure->mutation_may_have_occurred ? "true" : "false");
    } else {
        throw std::runtime_error("probe reached an unsupported closure state");
    }
    output << '}';
}

std::string run_probe(const Arguments& arguments) {
    auto factory = EpisodicEnvironment::create(CertifiedEnvironmentConfig::canonical());
    if (!std::holds_alternative<std::unique_ptr<EpisodicEnvironment>>(factory)) {
        throw std::runtime_error("canonical V2 environment factory rejected the probe");
    }
    auto environment = std::move(std::get<std::unique_ptr<EpisodicEnvironment>>(factory));

    EpisodeSpec spec;
    spec.root_seed = arguments.seed;
    spec.seat_assignment = arguments.mirror_seats ? SeatAssignment::Mirror : SeatAssignment::Normal;
    if (arguments.starting_player.has_value()) {
        spec.starting_player = *arguments.starting_player;
    }
    RunControl control;
    control.engine_process_budget = arguments.engine_process_budget;
    control.semantic_action_budget = arguments.semantic_action_budget;
    control.cancellation.source = "episodic-probe";
    const auto reset = environment->reset(spec, control);
    if (!std::holds_alternative<ResetAccepted>(reset)) {
        throw std::runtime_error("probe reset was rejected: " +
                                 std::string(reset_rejection_code_name(
                                     std::get<ResetRejected>(reset).rejection_code)));
    }
    Next next = std::get<ResetAccepted>(reset).next;
    const auto episode_id = episode_semantic_id(environment->config(), spec);
    const auto replay_actions = read_replay_actions(arguments.replay_path);
    std::size_t replay_index = 0;
    std::uint64_t action_count = 0;
    std::ostringstream prefix;
    prefix << '{';
    bool first = true;
    append_string_field(prefix, "contract_id", std::string(kEpisodicEnvironmentContractId), first);
    append_string_field(prefix, "environment_semantic_id", environment->config().environment_semantic_id, first);
    append_string_field(prefix, "episode_semantic_id", episode_id, first);
    append_u64_field(prefix, "root_seed", arguments.seed, first);
    if (!first) prefix << ',';
    first = false;
    prefix << "\"seat_assignment\":"
           << json_escape(arguments.mirror_seats ? "mirror" : "normal") << ','
           << "\"frames\":[";
    bool first_frame = true;
    bool first_action = true;

    // The arrays are assembled separately so each frame remains a value-only
    // public record and no token or internal key can enter the output.
    std::ostringstream frames;
    std::ostringstream actions;
    while (true) {
        const auto* frame = std::get_if<DecisionFrame>(&next);
        if (frame == nullptr) {
            break;
        }
        if (!first_frame) frames << ',';
        first_frame = false;
        frames << frame_json(*frame);
        if (arguments.stop_on_continuation && frame->request.continuation.has_value()) {
            break;
        }
        if (action_count >= arguments.max_actions) {
            break;
        }
        if (arguments.interrupt_after.has_value() && action_count >= *arguments.interrupt_after) {
            const auto interrupted = environment->interrupt(
                InterruptRequest{std::string(kEpisodicEnvironmentV2ContractId),
                                 InterruptionReason::AdministrativeCancel});
            if (!std::holds_alternative<InterruptAccepted>(interrupted)) {
                throw std::runtime_error("probe administrative interrupt was rejected");
            }
            next = std::get<InterruptAccepted>(interrupted).interruption;
            break;
        }

        std::string public_key;
        if (replay_index < replay_actions.size()) {
            public_key = replay_actions[replay_index++];
            const auto known = std::find_if(
                frame->request.candidates.begin(), frame->request.candidates.end(),
                [&public_key](const EnvironmentActionCandidate& candidate) {
                    return candidate.public_action_key == public_key;
                });
            if (known == frame->request.candidates.end()) {
                throw std::runtime_error("public replay action is not in the regenerated domain");
            }
        } else {
            public_key = frame->request.candidates[select_candidate_index(*frame, action_count, arguments)]
                             .public_action_key;
        }
        const auto selection = ActionSelection{frame->contract_id, frame->episode_semantic_id,
                                               frame->public_semantic_decision_id,
                                               frame->submission_token, public_key};
        const auto step = environment->step(selection);
        if (!std::holds_alternative<StepAccepted>(step)) {
            throw std::runtime_error("probe public selection was rejected: " +
                                     std::string(rejection_code_name(
                                         std::get<StepRejected>(step).rejection_code)));
        }
        const auto& accepted = std::get<StepAccepted>(step);
        append_action_json(actions, accepted, frame->decision_index, public_key, first_action);
        first_action = false;
        ++action_count;
        next = accepted.next;
    }
    if (replay_index != replay_actions.size()) {
        throw std::runtime_error("public replay contains actions after the regenerated closure");
    }

    std::ostringstream result;
    result << prefix.str() << frames.str()
           << "],\"actions\":[" << actions.str() << "],";
    const bool limited = std::get_if<DecisionFrame>(&next) != nullptr;
    append_closure(result, next, action_count, limited);
    result << '}';
    return result.str();
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const auto arguments = parse_arguments(argc, argv);
        const auto result = run_probe(arguments);
        if (!arguments.output_path.empty()) {
            std::ofstream output(arguments.output_path, std::ios::binary | std::ios::trunc);
            if (!output) {
                throw std::runtime_error("cannot open probe output path");
            }
            output << result << '\n';
        }
        std::cout << result << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "EPISODIC_PROBE_ERROR " << error.what() << '\n';
        return 1;
    }
}
