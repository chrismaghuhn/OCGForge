#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "ygo/core/core_error.hpp"
#include "ygo/environment/candidate_domain_evidence.hpp"
#include "ygo/environment/episode_driver.hpp"
#include "ygo/environment/episodic_environment.hpp"
#include "ygo/m3/canonical_rules.hpp"
#include "ygo/protocol/message_decoder.hpp"

namespace {

using namespace ygo::environment;
using ygo::protocol::DecisionRequest;

using DriverNext = std::variant<DriverDecisionBoundary, DriverGameTerminal,
                                DriverProcessBudgetExceeded, DriverSemanticActionBudgetExceeded,
                                DriverAdministrativeInterrupt, DriverFailure>;

struct Arguments final {
    std::uint64_t max_actions = 64;
    std::uint64_t seeds = 4;
    std::string output_path;
};

struct WitnessRow final {
    CandidateDomainWitness witness;
    std::size_t job_index = 0;
    std::size_t frame_index = 0;
};

struct JobTrace final {
    EpisodeSpec spec;
    std::vector<CandidateDomainWitness> rows;
    std::vector<std::string> actions;
};

void require(const bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

std::uint64_t parse_u64(const std::string& value, const char* name) {
    std::size_t consumed = 0;
    const auto parsed = std::stoull(value, &consumed, 0);
    if (consumed != value.size()) {
        throw std::runtime_error(std::string("invalid ") + name + ": " + value);
    }
    return static_cast<std::uint64_t>(parsed);
}

Arguments parse_arguments(const int argc, char** argv) {
    Arguments result;
    for (int index = 1; index < argc; ++index) {
        const std::string argument(argv[index]);
        if (argument == "--max-actions" && index + 1 < argc) {
            result.max_actions = parse_u64(argv[++index], "max-actions");
        } else if (argument == "--seeds" && index + 1 < argc) {
            result.seeds = parse_u64(argv[++index], "seeds");
        } else if (argument == "--output" && index + 1 < argc) {
            result.output_path = argv[++index];
        } else {
            throw std::invalid_argument(
                "usage: ygo_episodic_internal_witness [--max-actions N] [--seeds N] [--output path]");
        }
    }
    if (result.seeds == 0) {
        throw std::invalid_argument("seeds must be positive");
    }
    return result;
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

void append_string(std::ostringstream& output, const std::string& name, const std::string& value,
                   bool& first) {
    if (!first) {
        output << ',';
    }
    first = false;
    output << json_escape(name) << ':' << json_escape(value);
}

void append_u64(std::ostringstream& output, const std::string& name, const std::uint64_t value,
                bool& first) {
    if (!first) {
        output << ',';
    }
    first = false;
    output << json_escape(name) << ':' << value;
}

void append_bool(std::ostringstream& output, const std::string& name, const bool value,
                 bool& first) {
    if (!first) {
        output << ',';
    }
    first = false;
    output << json_escape(name) << ':' << (value ? "true" : "false");
}

EpisodeDriverConfig make_driver_config(const CertifiedEnvironmentConfig& environment_config,
                                       const EpisodeSpec& spec) {
#ifndef YGO_M0_SOURCE_DIR
#error "YGO_M0_SOURCE_DIR must be supplied by CMake"
#endif
#ifndef YGO_M0_CARDSCRIPTS_ROOT
#error "YGO_M0_CARDSCRIPTS_ROOT must be supplied by CMake"
#endif
#ifndef YGO_M0_CARD_DATA_TSV
#error "YGO_M0_CARD_DATA_TSV must be supplied by CMake"
#endif
#ifndef YGO_M0_CORE_API_VERSION
#error "YGO_M0_CORE_API_VERSION must be supplied by CMake"
#endif
#ifndef YGO_M0_CORE_COMMIT
#error "YGO_M0_CORE_COMMIT must be supplied by CMake"
#endif
#ifndef YGO_M0_CARDSCRIPTS_COMMIT
#error "YGO_M0_CARDSCRIPTS_COMMIT must be supplied by CMake"
#endif
#ifndef YGO_M0_DATABASE_COMMIT
#error "YGO_M0_DATABASE_COMMIT must be supplied by CMake"
#endif

    const auto& rules = ygo::m3::canonical_rules();
    const auto source_root = std::filesystem::path(YGO_M0_SOURCE_DIR);
    const auto deck_a = ygo::core::load_fixture_deck(
        source_root / "fixtures" / "decks" / "swordsoul_tenyi_ml_v1.ydk");
    const auto deck_b = ygo::core::load_fixture_deck(
        source_root / "fixtures" / "decks" / "salamangreat_ml_v1.ydk");
    const auto seat_decks = spec.seat_assignment == SeatAssignment::Normal
                                ? std::vector<ygo::core::FixtureDeck>{deck_a, deck_b}
                                : std::vector<ygo::core::FixtureDeck>{deck_b, deck_a};

    EpisodeDriverConfig driver;
    driver.rules.card_scripts_root = YGO_M0_CARDSCRIPTS_ROOT;
    driver.rules.card_data_tsv = YGO_M0_CARD_DATA_TSV;
    driver.rules.bundle_id = environment_config.rules_bundle_id;
    driver.rules.core_api_version = YGO_M0_CORE_API_VERSION;
    driver.rules.core_commit = YGO_M0_CORE_COMMIT;
    driver.rules.cardscripts_commit = YGO_M0_CARDSCRIPTS_COMMIT;
    driver.rules.database_commit = YGO_M0_DATABASE_COMMIT;
    driver.rules.core_patchset_id = environment_config.core_patchset_id;
    driver.rules.core_patchset_sha256 = environment_config.core_patchset_sha256;
    driver.player_zero_deck = seat_decks[0];
    driver.player_one_deck = seat_decks[1];
    driver.seed = spec.root_seed;
    driver.duel_flags = environment_config.duel_flags;
    driver.starting_player = spec.starting_player;
    driver.engine_process_budget = 4096;
    driver.semantic_action_budget = 4096;
    driver.build_full_observation = true;
    driver.required_script_codes = ygo::core::canonical_required_script_codes(
        seat_decks[0], seat_decks[1]);
    driver.fixture_setup_script.clear();
    driver.instrumentation = false;
    return driver;
}

std::string failure_description(const DriverNext& next) {
    if (const auto* failure = std::get_if<DriverFailure>(&next)) {
        return failure->failure_code + ":" + failure->error_message;
    }
    if (std::holds_alternative<DriverProcessBudgetExceeded>(next)) {
        return "process budget exhausted";
    }
    if (std::holds_alternative<DriverSemanticActionBudgetExceeded>(next)) {
        return "semantic action budget exhausted";
    }
    if (std::holds_alternative<DriverAdministrativeInterrupt>(next)) {
        return "unexpected administrative interrupt";
    }
    return "unexpected driver closure";
}

CandidateDomainWitness make_row(const DecisionRequest& request,
                                const std::string& episode_id,
                                const std::uint64_t environment_decision_index) {
    CandidateDomainWitness result;
    result.candidate_count = request.candidates.size();
    result.request_kind = ygo::protocol::decision_kind_name(request.kind);
    result.episode_semantic_id = episode_id;
    result.environment_decision_index = environment_decision_index;
    result.engine_step_index = request.engine_step_index;
    result.protocol_decision_id = request.decision_id;
    result.ordered_semantic_keys.reserve(request.candidates.size());
    for (const auto& candidate : request.candidates) {
        result.ordered_semantic_keys.push_back(candidate.semantic_key);
    }
    result.candidate_domain_digest = candidate_domain_digest(
        result.request_kind, result.ordered_semantic_keys);
    return result;
}

void require_same_row(const CandidateDomainWitness& expected,
                      const CandidateDomainWitness& actual,
                      const std::string& context) {
    require(expected.candidate_count == actual.candidate_count,
            context + ": candidate count changed");
    require(expected.request_kind == actual.request_kind,
            context + ": request kind changed");
    require(expected.episode_semantic_id == actual.episode_semantic_id,
            context + ": episode identity changed");
    require(expected.environment_decision_index == actual.environment_decision_index,
            context + ": environment decision index changed");
    require(expected.engine_step_index == actual.engine_step_index,
            context + ": engine step index changed");
    require(expected.protocol_decision_id == actual.protocol_decision_id,
            context + ": protocol decision identity changed");
    require(expected.candidate_domain_digest == actual.candidate_domain_digest,
            context + ": candidate domain digest changed");
    require(expected.ordered_semantic_keys == actual.ordered_semantic_keys,
            context + ": ordered semantic domain changed");
}

JobTrace collect_job(const CertifiedEnvironmentConfig& environment_config,
                     const EpisodeSpec& spec, const std::uint64_t max_actions,
                     const std::size_t job_index,
                     std::vector<WitnessRow>& all_rows) {
    EpisodeDriver driver(make_driver_config(environment_config, spec));
    const auto episode_id = episode_semantic_id(environment_config, spec);
    JobTrace result{spec, {}, {}};
    auto next = driver.advance_until_boundary();
    std::uint64_t accepted_actions = 0;
    while (const auto* boundary = std::get_if<DriverDecisionBoundary>(&next)) {
        require(boundary->request != nullptr,
                "internal G28 witness received a null decision request");
        const auto row = make_row(*boundary->request, episode_id, accepted_actions);
        all_rows.push_back(WitnessRow{row, job_index, result.rows.size()});
        result.rows.push_back(row);
        if (accepted_actions >= max_actions) {
            break;
        }
        require(!boundary->request->candidates.empty(),
                "internal G28 witness received an empty candidate domain");
        const auto selected = boundary->request->candidates.front().semantic_key;
        result.actions.push_back(selected);
        auto applied = driver.apply_semantic_key(selected);
        require(applied.accepted.has_value(),
                "internal G28 witness policy action was not accepted");
        require(applied.accepted->selected_semantic_key == selected,
                "internal G28 witness changed the selected semantic key");
        ++accepted_actions;
        next = std::move(applied.next);
    }
    require(!result.rows.empty(), "internal G28 witness published no complete domains");
    require(!std::holds_alternative<DriverFailure>(next),
            "internal G28 witness failed: " + failure_description(next));
    require(!std::holds_alternative<DriverProcessBudgetExceeded>(next),
            "internal G28 witness exhausted its process budget");
    require(!std::holds_alternative<DriverSemanticActionBudgetExceeded>(next),
            "internal G28 witness exhausted its semantic budget");
    return result;
}

void replay_selected(const CertifiedEnvironmentConfig& environment_config,
                     const JobTrace& job, const std::size_t selected_frame_index) {
    EpisodeDriver driver(make_driver_config(environment_config, job.spec));
    auto next = driver.advance_until_boundary();
    for (std::size_t frame_index = 0; frame_index <= selected_frame_index; ++frame_index) {
        const auto* boundary = std::get_if<DriverDecisionBoundary>(&next);
        require(boundary != nullptr && boundary->request != nullptr,
                "G28 independent replay did not reach the selected decision boundary");
        const auto actual = make_row(
            *boundary->request, job.rows[frame_index].episode_semantic_id, frame_index);
        require_same_row(job.rows[frame_index], actual,
                         "G28 independent replay frame " + std::to_string(frame_index));
        if (frame_index == selected_frame_index) {
            return;
        }
        require(frame_index < job.actions.size(),
                "G28 selected replay prefix has no action for a prior frame");
        auto applied = driver.apply_semantic_key(job.actions[frame_index]);
        require(applied.accepted.has_value(),
                "G28 independent replay rejected a recorded semantic action");
        require(applied.accepted->selected_semantic_key == job.actions[frame_index],
                "G28 independent replay changed a recorded semantic action");
        next = std::move(applied.next);
    }
}

std::string row_json(const CandidateDomainWitness& row) {
    std::ostringstream output;
    output << '{';
    bool first = true;
    append_u64(output, "candidate_count", row.candidate_count, first);
    append_string(output, "request_kind", row.request_kind, first);
    append_string(output, "episode_semantic_id", row.episode_semantic_id, first);
    append_u64(output, "environment_decision_index", row.environment_decision_index, first);
    append_u64(output, "engine_step_index", row.engine_step_index, first);
    append_string(output, "protocol_decision_id", row.protocol_decision_id, first);
    append_string(output, "candidate_domain_digest", row.candidate_domain_digest, first);
    if (!first) {
        output << ',';
    }
    output << "\"ordered_semantic_keys\":[";
    for (std::size_t index = 0; index < row.ordered_semantic_keys.size(); ++index) {
        if (index != 0) {
            output << ',';
        }
        output << json_escape(row.ordered_semantic_keys[index]);
    }
    output << "]}";
    return output.str();
}

std::string make_evidence(const CertifiedEnvironmentConfig& environment_config,
                          const Arguments& arguments,
                          const std::vector<JobTrace>& jobs,
                          const std::vector<WitnessRow>& all_rows,
                          const WitnessRow& selected,
                          const std::uint64_t aggregate_max_total) {
    std::ostringstream output;
    output << '{';
    bool first = true;
    append_string(output, "schema", std::string(kCandidateDomainEvidenceSchemaId), first);
    append_string(output, "gate", "G28", first);
    append_string(output, "result", "PASS", first);
    append_string(output, "contract_id", std::string(kEpisodicEnvironmentContractId), first);
    append_string(output, "environment_semantic_id", environment_config.environment_semantic_id, first);
    append_u64(output, "candidate_domain_max", selected.witness.candidate_count, first);
    append_u64(output, "candidate_max_total", aggregate_max_total, first);
    append_u64(output, "complete_domain_count", all_rows.size(), first);
    append_u64(output, "job_count", jobs.size(), first);
    append_u64(output, "max_actions_per_job", arguments.max_actions, first);
    if (!first) {
        output << ',';
    }
    output << "\"tie_break\":[\"candidate_count descending\",\"episode_semantic_id ascending\","
              "\"environment_decision_index ascending\",\"engine_step_index ascending\","
              "\"protocol_decision_id ascending\",\"candidate_domain_digest ascending\"],";
    output << "\"witness\":" << row_json(selected.witness);
    append_u64(output, "witness_job_index", selected.job_index, first);
    append_u64(output, "witness_frame_index", selected.frame_index, first);
    output << ",\"rows\":[";
    for (std::size_t index = 0; index < all_rows.size(); ++index) {
        if (index != 0) {
            output << ',';
        }
        output << row_json(all_rows[index].witness);
    }
    output << ']';
    output << "}";
    return output.str();
}

void write_output(const std::string& path, const std::string& value) {
    if (path.empty()) {
        return;
    }
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    require(static_cast<bool>(output), "could not open G28 internal evidence output");
    output << value << '\n';
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const auto arguments = parse_arguments(argc, argv);
        const auto environment_config = CertifiedEnvironmentConfig::canonical();
        std::vector<JobTrace> jobs;
        std::vector<WitnessRow> all_rows;
        for (std::uint64_t seed = 0; seed < arguments.seeds; ++seed) {
            for (const auto mirror : {false, true}) {
                for (std::uint8_t starting_player = 0; starting_player < 2; ++starting_player) {
                    EpisodeSpec spec;
                    spec.root_seed = seed;
                    spec.seat_assignment = mirror ? SeatAssignment::Mirror : SeatAssignment::Normal;
                    spec.starting_player = starting_player;
                    const auto job_index = jobs.size();
                    jobs.push_back(collect_job(environment_config, spec, arguments.max_actions,
                                               job_index, all_rows));
                }
            }
        }
        require(!all_rows.empty(), "G28 internal witness corpus is empty");
        std::vector<CandidateDomainWitness> witnesses;
        witnesses.reserve(all_rows.size());
        for (const auto& row : all_rows) {
            witnesses.push_back(row.witness);
        }
        const auto selected_index = select_g28_witness_index(witnesses);
        const auto& selected = all_rows[selected_index];
        require(selected.witness.candidate_count == candidate_domain_max(witnesses),
                "G28 internal witness count does not equal candidate_domain_max");
        replay_selected(environment_config, jobs[selected.job_index], selected.frame_index);

        std::uint64_t aggregate_max_total = 0;
        for (const auto& job : jobs) {
            std::uint64_t job_max = 0;
            for (const auto& row : job.rows) {
                job_max = std::max(job_max, row.candidate_count);
            }
            require(std::numeric_limits<std::uint64_t>::max() - aggregate_max_total >= job_max,
                    "G28 aggregate candidate maximum overflow");
            aggregate_max_total += job_max;
        }
        const auto evidence = make_evidence(environment_config, arguments, jobs, all_rows,
                                             selected, aggregate_max_total);
        write_output(arguments.output_path, evidence);
        std::cout << "{\"gate\":\"G28\",\"result\":\"PASS\",\"candidate_domain_max\":"
                  << selected.witness.candidate_count << ",\"complete_domain_count\":"
                  << all_rows.size() << "}\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
