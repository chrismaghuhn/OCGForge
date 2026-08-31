#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "ygo/observation/player_observation.hpp"
#include "ygo/policy/random_legal.hpp"
#include "ygo/policy/rng.hpp"
#include "ygo/environment/public_action_identity.hpp"

namespace {

struct Arguments final {
    std::optional<std::uint64_t> root;
    std::string assignment;
    std::string stream;
    std::string episode_id;
    std::vector<std::string> candidate_specs;
    std::size_t repeat = 1;
    bool interleave_shadow = false;
};

struct CandidateSpec final {
    std::string action_kind;
    ygo::environment::EnvironmentActionKind action_type =
        ygo::environment::EnvironmentActionKind::Unsupported;
    ygo::environment::PublicChoice choice;
};

void require(const bool condition, const std::string& message) {
    if (!condition) {
        throw std::invalid_argument(message);
    }
}

std::string next_argument(const int argc, char* argv[], int& index, const char* flag) {
    require(index + 1 < argc, std::string("missing value for ") + flag);
    return argv[++index];
}

std::uint64_t parse_u64(const std::string& value, const char* flag) {
    std::size_t consumed = 0;
    try {
        const auto result = std::stoull(value, &consumed, 0);
        require(consumed == value.size(), std::string("invalid value for ") + flag);
        return static_cast<std::uint64_t>(result);
    } catch (const std::exception&) {
        throw std::invalid_argument(std::string("invalid value for ") + flag);
    }
}

CandidateSpec parse_candidate_spec(const std::string& value) {
    const auto separator = value.find(':');
    require(separator != std::string::npos && separator != 0 && separator + 1 < value.size(),
            "candidate must use kind:value syntax");
    const auto kind = value.substr(0, separator);
    const auto number = parse_u64(value.substr(separator + 1), "--candidate");

    CandidateSpec result;
    if (kind == "yes_no") {
        require(number <= 1, "yes_no candidate value must be 0 or 1");
        result.action_kind = "yes_no";
        result.action_type = ygo::environment::EnvironmentActionKind::YesNo;
        result.choice = {ygo::environment::PublicChoiceKind::YesNo, number, std::nullopt};
    } else if (kind == "effect_choice") {
        require(number <= std::numeric_limits<std::uint32_t>::max(),
                "effect_choice candidate exceeds u32");
        result.action_kind = "chain";
        result.action_type = ygo::environment::EnvironmentActionKind::Chain;
        result.choice = {ygo::environment::PublicChoiceKind::EffectChoice, number, std::nullopt};
    } else {
        throw std::invalid_argument("unsupported candidate kind: " + kind);
    }
    return result;
}

Arguments parse_arguments(const int argc, char* argv[]) {
    Arguments result;
    for (int index = 1; index < argc; ++index) {
        const std::string flag = argv[index];
        if (flag == "--root") {
            result.root = parse_u64(next_argument(argc, argv, index, "--root"), "--root");
        } else if (flag == "--assignment") {
            result.assignment = next_argument(argc, argv, index, "--assignment");
        } else if (flag == "--stream") {
            result.stream = next_argument(argc, argv, index, "--stream");
        } else if (flag == "--episode-id") {
            result.episode_id = next_argument(argc, argv, index, "--episode-id");
        } else if (flag == "--candidate") {
            result.candidate_specs.push_back(next_argument(argc, argv, index, "--candidate"));
        } else if (flag == "--repeat") {
            const auto repeat = parse_u64(next_argument(argc, argv, index, "--repeat"), "--repeat");
            require(repeat > 0 && repeat <= 1024, "--repeat must be between 1 and 1024");
            result.repeat = static_cast<std::size_t>(repeat);
        } else if (flag == "--interleave-shadow") {
            result.interleave_shadow = true;
        } else {
            throw std::invalid_argument("unknown argument: " + flag);
        }
    }
    require(result.root.has_value(), "--root is required");
    require(!result.assignment.empty(), "--assignment is required");
    require(!result.stream.empty(), "--stream is required");
    require(!result.episode_id.empty(), "--episode-id is required");
    require(!result.candidate_specs.empty(), "at least one --candidate is required");
    return result;
}

std::vector<ygo::environment::EnvironmentActionCandidate> make_candidates(
    const std::vector<std::string>& specs) {
    std::vector<ygo::environment::EnvironmentActionCandidate> result;
    result.reserve(specs.size());
    for (const auto& raw : specs) {
        const auto spec = parse_candidate_spec(raw);
        ygo::environment::PublicActionKeyInput key_input;
        key_input.action_kind = spec.action_kind;
        key_input.choice = spec.choice;

        ygo::environment::EnvironmentActionCandidate candidate;
        candidate.action_kind = spec.action_type;
        candidate.public_action_key = ygo::environment::public_action_key(key_input);
        candidate.choice = spec.choice;
        result.push_back(std::move(candidate));
    }
    return result;
}

ygo::environment::PublicEnvironmentObservation make_observation() {
    ygo::observation::PlayerObservation observation;
    observation.perspective_player = 0;
    observation.match_context.perspective_player = 0;
    return ygo::environment::project_public_observation(observation);
}

std::string json_escape(const std::string& value) {
    std::string result;
    result.reserve(value.size());
    for (const auto character : value) {
        if (character == '\\' || character == '"') {
            result.push_back('\\');
        }
        result.push_back(character);
    }
    return result;
}

void print_selection(const ygo::policy::PolicySelectionResult& selection, const bool first) {
    if (!first) {
        std::cout << ',';
    }
    std::cout << "{\"public_action_key\":\"" << json_escape(selection.public_action_key)
              << "\",\"pre_cursor\":" << selection.rng_cursor->pre_cursor
              << ",\"post_cursor\":" << selection.rng_cursor->post_cursor << '}';
}

int run(const Arguments& arguments) {
    ygo::policy::PolicyRngInitializationInput input;
    input.policy_rng_root_seed = arguments.root;
    input.participant_policy_assignment_id = arguments.assignment;
    input.policy_rng_stream_id = arguments.stream;
    auto created = ygo::policy::create_random_legal_policy(input);
    require(static_cast<bool>(created),
            created.error.has_value() ? created.error->message : "policy construction failed");
    auto policy = std::move(*created.value);

    std::optional<ygo::policy::RandomLegalPolicy> shadow;
    if (arguments.interleave_shadow) {
        auto shadow_created = ygo::policy::create_random_legal_policy(input);
        require(static_cast<bool>(shadow_created), "shadow policy construction failed");
        shadow = std::move(*shadow_created.value);
    }

    const auto observation = make_observation();
    const auto candidates = make_candidates(arguments.candidate_specs);
    const ygo::policy::PolicyInput policy_input{observation, candidates};
    std::cout << "{\"selections\":[";
    for (std::size_t index = 0; index < arguments.repeat; ++index) {
        if (shadow.has_value()) {
            const auto shadow_selection = shadow->select(policy_input);
            require(static_cast<bool>(shadow_selection), "shadow policy selection failed");
        }
        const auto selection = policy.select(policy_input);
        require(static_cast<bool>(selection),
                selection.error.has_value() ? selection.error->message
                                             : "policy selection failed");
        require(selection.value->rng_cursor.has_value(),
                "RandomLegal selection did not return a cursor span");
        print_selection(*selection.value, index == 0);
    }
    std::cout << "]}\n";
    return EXIT_SUCCESS;
}

}  // namespace

int main(int argc, char* argv[]) {
    try {
        return run(parse_arguments(argc, argv));
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
