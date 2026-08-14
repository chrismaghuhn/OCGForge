#include <iostream>
#include <stdexcept>
#include <string>

#include "ygo/trace/engine_trace.hpp"
#include "ygo/trace/sha256.hpp"

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

ygo::trace::EngineTrace make_trace(const std::string& raw_hash, const std::string& response_hash,
                                   const std::string& selected_key) {
    ygo::trace::EngineTrace trace;
    trace.manifest.trace_schema_version = "ygo.engine_trace.v2";
    trace.manifest.rules_bundle_id = "bundle";
    trace.manifest.core_commit = "core";
    trace.manifest.cardscripts_commit = "scripts";
    trace.manifest.database_commit = "database";
    trace.manifest.core_api_version = "11.0";
    trace.manifest.seed_bundle = {1, 2, 3, 4};
    trace.manifest.fixture_deck_hashes = {"deck-a", "deck-b"};
    trace.manifest.policy_identifier = "oracle-policy";

    ygo::trace::TraceStep step;
    step.step_index = 4;
    step.decision_index = 4;
    step.engine_step_index = 12;
    step.player_to_act = 0;
    step.engine_message_type = 15;
    step.raw_message_length = 99;
    step.raw_message_sha256 = raw_hash;
    step.decision_request_kind = "card_selection";
    step.complete_candidate_count = 2;
    step.ordered_candidate_semantic_keys = {"pick.a", "pick.b"};
    step.selected_semantic_key = selected_key;
    step.selected_response_sha256 = response_hash;
    step.final_engine_response_hash = response_hash;
    step.public_state_hash = "public-state";
    step.decision_id = "raw-derived-decision-id" + raw_hash;
    step.continuation_id = "cont-id";
    step.continuation_state_hash = "continuation-state";
    step.continuation_step = 1;
    step.continuation_steps = 1;
    step.peak_candidate_count = 2;
    step.terminal_solution_count = 1;
    step.engine_advanced = true;
    trace.steps.push_back(step);
    return trace;
}

int run() {
    const auto first = make_trace("raw-a", "response-a", "pick.a");
    const auto second = make_trace("raw-b", "response-a", "pick.a");
    require(ygo::trace::semantic_gameplay_hash(first) == ygo::trace::semantic_gameplay_hash(second),
            "semantic gameplay hash included raw transport fields");
    require(ygo::trace::canonical_trace_hash(first) != ygo::trace::canonical_trace_hash(second),
            "legacy trace hash unexpectedly ignored raw transport fields");

    const auto changed = make_trace("raw-b", "response-b", "pick.b");
    require(ygo::trace::semantic_gameplay_hash(first) != ygo::trace::semantic_gameplay_hash(changed),
            "semantic gameplay hash ignored the selected semantic action");

    const auto serialized = ygo::trace::canonical_trace_jsonl_v2(first);
    require(serialized.find("ygo.engine_trace.v2") != std::string::npos,
            "v2 trace serializer did not version its manifest");
    require(serialized.find("\"continuation_steps\":1") != std::string::npos &&
                serialized.find("\"peak_candidate_count\":2") != std::string::npos &&
                serialized.find("\"terminal_solution_count\":1") != std::string::npos,
            "v2 trace serializer omitted continuation diagnostics");
    auto intermediate = first;
    intermediate.steps.front().engine_advanced = false;
    intermediate.steps.front().selected_response_sha256.clear();
    intermediate.steps.front().final_engine_response_hash.clear();
    const auto intermediate_serialized = ygo::trace::canonical_trace_jsonl_v2(intermediate);
    require(intermediate_serialized.find("\"engine_advanced\":false") != std::string::npos,
            "v2 trace serializer omitted continuation engine immobility");
    require(intermediate_serialized.find("\"final_engine_response_hash\":null") != std::string::npos,
            "v2 trace serializer did not represent an intermediate continuation as response-free");
    require(ygo::trace::semantic_gameplay_hash(first) !=
                ygo::trace::semantic_gameplay_hash(make_trace("raw-b", "response-b", "pick.a")),
            "semantic gameplay hash ignored the final engine response hash");
    require(ygo::trace::canonical_trace_hash_v2(first) == ygo::trace::sha256_string(serialized),
            "v2 trace hash was not the hash of canonical v2 bytes");
    std::cout << "trace_semantics=ok\n";
    return 0;
}

}  // namespace

int main() {
    try {
        return run();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
