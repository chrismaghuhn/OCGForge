#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "ygo/protocol/decision_request.hpp"

namespace ygo::trace {

struct TraceManifest {
    std::string trace_schema_version = "ygo.engine_trace.v1";
    std::string rules_bundle_id;
    std::string core_repository;
    std::string core_commit;
    std::string cardscripts_repository;
    std::string cardscripts_commit;
    std::string database_repository;
    std::string database_commit;
    std::string core_api_version;
    std::string compiler_identity;
    std::string build_type;
    std::string platform_identity;
    std::uint64_t duel_flags = 0;
    std::array<std::uint64_t, 4> seed_bundle{};
    std::vector<std::string> fixture_deck_hashes;
    std::string policy_identifier;
};

struct TraceStep {
    std::uint32_t step_index = 0;
    std::uint8_t player_to_act = 255;
    std::uint8_t engine_message_type = 0;
    std::uint32_t raw_message_length = 0;
    std::string raw_message_sha256;
    std::string decision_request_kind;
    std::size_t complete_candidate_count = 0;
    std::vector<std::string> ordered_candidate_semantic_keys;
    std::string selected_semantic_key;
    std::string selected_response_sha256;
    std::string public_state_hash;
    bool terminal = false;
    std::uint8_t winner = 255;
    std::uint8_t win_reason = 255;
};

struct EngineTrace {
    TraceManifest manifest;
    std::vector<TraceStep> steps;
};

std::string canonical_trace_jsonl(const EngineTrace& trace);
std::string canonical_trace_hash(const EngineTrace& trace);

TraceStep make_decision_step(std::uint32_t step_index, const std::vector<std::uint8_t>& raw_message,
                             const ygo::protocol::DecisionRequest& request,
                             const std::string& public_state_hash);

}  // namespace ygo::trace
