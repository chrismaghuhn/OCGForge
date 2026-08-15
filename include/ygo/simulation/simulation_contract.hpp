#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "ygo/core/rules_bundle.hpp"

namespace ygo::simulation {

inline constexpr char kCanonicalFormat[] = "TCG_ADVANCED_2026_05_18";
inline constexpr char kCanonicalDuelMode[] = "DUEL_MODE_MR5";
inline constexpr std::uint64_t kCanonicalDuelFlags = 0x2E800;
inline constexpr char kCanonicalRulesBundleId[] =
    "3adfe6b4cfe2c2805e50b389fc0eb4e70a3b0b6107436614d328fddc865e585f";
inline constexpr char kCanonicalCoreCommit[] = "9a0c558c2d686542f7914a6d529fd7aa57746aed";
inline constexpr char kCanonicalCardScriptsCommit[] =
    "f337c87018ca723c1aded5143e616bb649555273";
inline constexpr char kCanonicalDatabaseCommit[] =
    "89ad6837b0766a52984d8c715a7d5d4f8447946b";
inline constexpr char kCanonicalPatchsetId[] = "ocgforge.ocgcore.api_hardening.v1";
inline constexpr char kCanonicalPatchsetSha256[] =
    "6b5421b3a852085f48fa161a5ba1540f902aa00784a337694b21c9efc34f69bd";
inline constexpr char kCanonicalDeckASha256[] =
    "8ee4b699de19ff256e388d46f35b8696a60ff6ec59f0324f060a2468876711b7";
inline constexpr char kCanonicalDeckBSha256[] =
    "6041abe0a59463d0715ae1da9100090ad487de02a02794e8ec0686d4c0513188";

enum class SimulationMode {
    Conformance,
    Throughput,
};

enum class ObservationMode {
    Full,
    OffDiagnostic,
};

enum class SeatAssignment {
    Normal,
    Mirror,
};

struct SimulationJob {
    std::string job_id;
    std::uint64_t seed = 0;
    SeatAssignment seat_assignment = SeatAssignment::Normal;
    std::uint8_t starting_player = 0;
    std::uint32_t max_steps = 2200;
    std::string canonical_rules_id = kCanonicalRulesBundleId;
    std::vector<std::string> replay_actions;
    std::vector<std::uint32_t> focus_codes;
    std::filesystem::path setup_script;
    bool force_unsupported = false;
    SimulationMode mode = SimulationMode::Throughput;
    ObservationMode observation_mode = ObservationMode::Full;
    bool instrumentation = false;
    bool persist_trace = false;
    std::filesystem::path trace_output;
};

struct ErrorCounters {
    std::uint64_t retries = 0;
    std::uint64_t unsupported = 0;
    std::uint64_t automatic = 0;
    std::uint64_t truncated = 0;
    std::uint64_t core_errors = 0;
    std::uint64_t worker_errors = 0;
};

struct TimingBuckets {
    std::uint64_t core_process_us = 0;
    std::uint64_t protocol_candidate_us = 0;
    std::uint64_t continuation_us = 0;
    std::uint64_t observation_us = 0;
    std::uint64_t trace_hash_us = 0;
    std::uint64_t serialization_us = 0;
    std::uint64_t other_us = 0;
};

struct OperationCounters {
    std::uint64_t ocg_duel_process = 0;
    std::uint64_t ocg_duel_query = 0;
    std::uint64_t ocg_duel_query_location = 0;
    std::uint64_t ocg_duel_query_field = 0;
    std::uint64_t ocg_duel_query_count = 0;
    std::uint64_t script_reader_requests = 0;
    std::uint64_t script_loads = 0;
    std::uint64_t observations = 0;
    std::uint64_t entities_projected = 0;
    std::uint64_t candidate_sets = 0;
    std::uint64_t candidate_total = 0;
    std::uint64_t candidate_max = 0;
    std::uint64_t semantic_hashes = 0;
    std::uint64_t trace_bytes_serialized = 0;
};

struct CanonicalSimulationConfig {
    core::RulesBundlePaths rules;
    core::FixtureDeck deck_a;
    core::FixtureDeck deck_b;
    std::vector<std::uint32_t> required_script_codes;

    // These fields are the authoritative canonical identity surface for
    // worker handshakes. Payload paths/decks must not override them.
    std::string format = kCanonicalFormat;
    std::string duel_mode = kCanonicalDuelMode;
    std::uint64_t duel_flags = kCanonicalDuelFlags;
    std::string rules_bundle_id = kCanonicalRulesBundleId;
    std::string patchset_id = kCanonicalPatchsetId;
    std::string patchset_sha256 = kCanonicalPatchsetSha256;
    std::vector<std::string> locked_deck_hashes = {
        kCanonicalDeckASha256,
        kCanonicalDeckBSha256,
    };

    SimulationMode mode = SimulationMode::Throughput;
    ObservationMode observation_mode = ObservationMode::Full;
    bool instrumentation = false;
    bool persist_trace = false;
};

inline bool is_canonical_identity(const CanonicalSimulationConfig& config) {
    return config.format == kCanonicalFormat &&
           config.duel_mode == kCanonicalDuelMode &&
           config.duel_flags == kCanonicalDuelFlags &&
           config.rules_bundle_id == kCanonicalRulesBundleId &&
           config.patchset_id == kCanonicalPatchsetId &&
           config.patchset_sha256 == kCanonicalPatchsetSha256 &&
           config.locked_deck_hashes.size() == 2 &&
           config.locked_deck_hashes[0] == kCanonicalDeckASha256 &&
           config.locked_deck_hashes[1] == kCanonicalDeckBSha256 &&
           config.rules.bundle_id == kCanonicalRulesBundleId &&
           config.rules.core_commit == kCanonicalCoreCommit &&
           config.rules.cardscripts_commit == kCanonicalCardScriptsCommit &&
           config.rules.database_commit == kCanonicalDatabaseCommit &&
           config.rules.core_patchset_id == kCanonicalPatchsetId &&
           config.rules.core_patchset_sha256 == kCanonicalPatchsetSha256 &&
           config.deck_a.sha256 == kCanonicalDeckASha256 &&
           config.deck_b.sha256 == kCanonicalDeckBSha256;
}

struct SimulationResult {
    std::string job_id;
    bool pass = false;
    bool terminal = false;
    std::optional<std::uint8_t> winner;
    std::optional<std::uint8_t> win_reason;
    std::uint32_t engine_steps = 0;
    std::uint32_t interactive_decisions = 0;
    std::uint32_t semantic_action_count = 0;
    std::uint32_t turns = 0;
    std::uint32_t battle_command_count = 0;
    std::uint32_t continuation_intermediate_steps = 0;
    std::uint64_t visible_life_points_event_count = 0;
    std::uint64_t visible_destroyed_event_count = 0;
    std::uint64_t visible_win_event_count = 0;
    std::uint64_t observation_entity_total = 0;
    std::uint64_t observation_event_total = 0;
    double candidate_count_mean = 0.0;
    std::string gameplay_hash;
    std::optional<std::string> trace_hash;
    std::optional<std::string> trace_jsonl;
    std::uint64_t response_build_time_us_total = 0;
    std::uint64_t response_build_time_us_max = 0;
    std::string failure_code;
    std::string error_message;
    ErrorCounters errors;
    TimingBuckets timing;
    OperationCounters operations;

    // These fields are intentionally value data for JSONL publication. The
    // worker owns no mutable engine object beyond the duration of a job.
    // Worker-local primary timing domain; coordinator timing is separate.
    std::uint64_t simulation_elapsed_us = 0;
    std::uint64_t coordinator_elapsed_us = 0;
    std::optional<std::uint64_t> peak_process_memory_bytes;
    std::optional<std::uint64_t> memory_per_environment_bytes;
    std::uint32_t worker_pid = 0;
    std::uint32_t worker_restart_index = 0;
    bool worker_crashed = false;
    bool worker_restarted = false;
};

}  // namespace ygo::simulation
