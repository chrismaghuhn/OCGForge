#include "ygo/trace/engine_trace.hpp"

#include <cstddef>
#include <sstream>
#include <string>

#include "ygo/protocol/message_decoder.hpp"
#include "ygo/trace/sha256.hpp"

namespace ygo::trace {
namespace {

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
                const char* hex = "0123456789abcdef";
                result << "\\u00" << hex[character >> 4] << hex[character & 0xf];
            } else {
                result << static_cast<char>(character);
            }
            break;
        }
    }
    result << '"';
    return result.str();
}

void write_string_array(std::ostringstream& out, const std::vector<std::string>& values) {
    out << '[';
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index != 0) {
            out << ',';
        }
        out << json_escape(values[index]);
    }
    out << ']';
}

void write_seed_array(std::ostringstream& out, const std::array<std::uint64_t, 4>& values) {
    out << '[' << values[0] << ',' << values[1] << ',' << values[2] << ',' << values[3] << ']';
}

std::string continuation_state_hash(const ygo::protocol::SelectionContinuation& continuation) {
    std::ostringstream state;
    const auto write_item = [&state](const ygo::protocol::ContinuationItem& item) {
        state << item.source_index << ':' << item.card.code << ':' << static_cast<unsigned>(item.card.controller)
              << ':' << item.card.location << ':' << item.card.sequence << ':' << item.card.position << ':'
              << item.primary_value << ':' << item.secondary_value << ':' << item.capacity << ':' << item.mask_value
              << ';';
    };
    state << ygo::protocol::continuation_kind_name(continuation.continuation_kind) << '|'
          << static_cast<unsigned>(continuation.original_message_type) << '|'
          << continuation.continuation_step << '|' << continuation.min_count << '|' << continuation.max_count << '|'
          << continuation.target_sum << '|' << continuation.required_amount << '|'
          << continuation.available_mask << '|' << continuation.selected_mask << '|'
          << (continuation.exact_sum ? 1 : 0) << '|' << (continuation.greater_sum ? 1 : 0) << '|';
    state << "items:";
    for (const auto& item : continuation.items) {
        write_item(item);
    }
    state << "|mandatory:";
    for (const auto& item : continuation.mandatory_items) {
        write_item(item);
    }
    state << '|';
    for (const auto index : continuation.selected_indices) {
        state << index << ',';
    }
    state << '|';
    for (const auto amount : continuation.assigned_amounts) {
        state << amount << ',';
    }
    return sha256_string(state.str());
}

void write_nullable_string(std::ostringstream& out, const std::string& value) {
    if (value.empty()) {
        out << "null";
    } else {
        out << json_escape(value);
    }
}

}  // namespace

TraceStep make_decision_step(std::uint32_t step_index, const std::vector<std::uint8_t>& raw_message,
                             const ygo::protocol::DecisionRequest& request,
                             const std::string& public_state_hash) {
    TraceStep step;
    step.step_index = step_index;
    step.decision_index = step_index;
    step.engine_step_index = request.engine_step_index;
    step.player_to_act = request.player;
    step.engine_message_type = request.engine_message_type;
    step.raw_message_length = static_cast<std::uint32_t>(raw_message.size());
    step.raw_message_sha256 = sha256_bytes(raw_message);
    step.decision_request_kind = ygo::protocol::decision_kind_name(request.kind);
    step.complete_candidate_count = request.candidates.size();
    for (const auto& candidate : request.candidates) {
        step.ordered_candidate_semantic_keys.push_back(candidate.semantic_key);
    }
    step.public_state_hash = public_state_hash;
    step.decision_id = request.decision_id;
    if (request.continuation.has_value()) {
        step.continuation_id = request.continuation->continuation_id;
        step.continuation_step = request.continuation->continuation_step;
        step.continuation_state_hash = continuation_state_hash(*request.continuation);
        step.continuation_steps = request.continuation->continuation_steps;
        step.peak_candidate_count = request.continuation->peak_candidate_count;
        step.terminal_solution_count = request.continuation->terminal_solution_count;
    }
    return step;
}

std::string canonical_trace_jsonl(const EngineTrace& trace) {
    std::ostringstream out;
    const auto& manifest = trace.manifest;
    out << "{\"build_type\":" << json_escape(manifest.build_type)
        << ",\"cardscripts_commit\":" << json_escape(manifest.cardscripts_commit)
        << ",\"cardscripts_repository\":" << json_escape(manifest.cardscripts_repository)
        << ",\"compiler_identity\":" << json_escape(manifest.compiler_identity)
        << ",\"core_api_version\":" << json_escape(manifest.core_api_version)
        << ",\"core_commit\":" << json_escape(manifest.core_commit)
        << ",\"core_repository\":" << json_escape(manifest.core_repository)
        << ",\"database_commit\":" << json_escape(manifest.database_commit)
        << ",\"database_repository\":" << json_escape(manifest.database_repository)
        << ",\"duel_flags\":" << manifest.duel_flags << ",\"fixture_deck_hashes\":";
    write_string_array(out, manifest.fixture_deck_hashes);
    out << ",\"platform_identity\":" << json_escape(manifest.platform_identity)
        << ",\"policy_identifier\":" << json_escape(manifest.policy_identifier)
        << ",\"rules_bundle_id\":" << json_escape(manifest.rules_bundle_id)
        << ",\"seed_bundle\":";
    write_seed_array(out, manifest.seed_bundle);
    out << ",\"trace_schema_version\":" << json_escape(manifest.trace_schema_version) << "}\n";

    for (const auto& step : trace.steps) {
        out << "{\"complete_candidate_count\":" << step.complete_candidate_count
            << ",\"decision_request_kind\":" << json_escape(step.decision_request_kind)
            << ",\"engine_message_type\":" << static_cast<unsigned>(step.engine_message_type)
            << ",\"ordered_candidate_semantic_keys\":";
        write_string_array(out, step.ordered_candidate_semantic_keys);
        out << ",\"player_to_act\":" << static_cast<unsigned>(step.player_to_act)
            << ",\"public_state_hash\":" << json_escape(step.public_state_hash)
            << ",\"raw_message_length\":" << step.raw_message_length
            << ",\"raw_message_sha256\":" << json_escape(step.raw_message_sha256)
            << ",\"selected_response_sha256\":" << json_escape(step.selected_response_sha256)
            << ",\"selected_semantic_key\":" << json_escape(step.selected_semantic_key)
            << ",\"step_index\":" << step.step_index << ",\"terminal\":"
            << (step.terminal ? "true" : "false") << ",\"winner\":";
        if (step.winner == 255) {
            out << "null";
        } else {
            out << static_cast<unsigned>(step.winner);
        }
        out << ",\"win_reason\":";
        if (step.win_reason == 255) {
            out << "null";
        } else {
            out << static_cast<unsigned>(step.win_reason);
        }
        out << "}\n";
    }
    return out.str();
}

std::string canonical_trace_hash(const EngineTrace& trace) {
    return sha256_string(canonical_trace_jsonl(trace));
}

std::string canonical_trace_jsonl_v2(const EngineTrace& trace) {
    std::ostringstream out;
    const auto& manifest = trace.manifest;
    out << "{\"build_type\":" << json_escape(manifest.build_type)
        << ",\"cardscripts_commit\":" << json_escape(manifest.cardscripts_commit)
        << ",\"cardscripts_repository\":" << json_escape(manifest.cardscripts_repository)
        << ",\"compiler_identity\":" << json_escape(manifest.compiler_identity)
        << ",\"core_api_version\":" << json_escape(manifest.core_api_version)
        << ",\"core_commit\":" << json_escape(manifest.core_commit)
        << ",\"core_repository\":" << json_escape(manifest.core_repository)
        << ",\"database_commit\":" << json_escape(manifest.database_commit)
        << ",\"database_repository\":" << json_escape(manifest.database_repository)
        << ",\"duel_flags\":" << manifest.duel_flags << ",\"fixture_deck_hashes\":";
    write_string_array(out, manifest.fixture_deck_hashes);
    out << ",\"platform_identity\":" << json_escape(manifest.platform_identity)
        << ",\"policy_identifier\":" << json_escape(manifest.policy_identifier)
        << ",\"rules_bundle_id\":" << json_escape(manifest.rules_bundle_id)
        << ",\"seed_bundle\":";
    write_seed_array(out, manifest.seed_bundle);
    out << ",\"trace_schema_version\":" << json_escape("ygo.engine_trace.v2") << "}\n";

    for (const auto& step : trace.steps) {
        out << "{\"complete_candidate_count\":" << step.complete_candidate_count
            << ",\"continuation_id\":" << json_escape(step.continuation_id)
            << ",\"continuation_state_hash\":" << json_escape(step.continuation_state_hash)
            << ",\"continuation_step\":" << step.continuation_step
            << ",\"continuation_steps\":" << step.continuation_steps
            << ",\"decision_index\":" << step.decision_index
            << ",\"decision_id\":" << json_escape(step.decision_id)
            << ",\"decision_request_kind\":" << json_escape(step.decision_request_kind)
            << ",\"engine_advanced\":" << (step.engine_advanced ? "true" : "false")
            << ",\"engine_message_type\":" << static_cast<unsigned>(step.engine_message_type)
            << ",\"engine_step_index\":" << step.engine_step_index
            << ",\"final_engine_response_hash\":";
        write_nullable_string(out, step.final_engine_response_hash);
        out << ",\"ordered_candidate_semantic_keys\":";
        write_string_array(out, step.ordered_candidate_semantic_keys);
        out << ",\"player_to_act\":" << static_cast<unsigned>(step.player_to_act)
            << ",\"peak_candidate_count\":" << step.peak_candidate_count
            << ",\"public_state_hash\":" << json_escape(step.public_state_hash)
            << ",\"raw_message_length\":" << step.raw_message_length
            << ",\"raw_message_sha256\":" << json_escape(step.raw_message_sha256)
            << ",\"selected_semantic_key\":" << json_escape(step.selected_semantic_key)
            << ",\"step_index\":" << step.step_index << ",\"terminal\":"
            << (step.terminal ? "true" : "false") << ",\"terminal_solution_count\":"
            << step.terminal_solution_count << ",\"winner\":";
        if (step.winner == 255) {
            out << "null";
        } else {
            out << static_cast<unsigned>(step.winner);
        }
        out << ",\"win_reason\":";
        if (step.win_reason == 255) {
            out << "null";
        } else {
            out << static_cast<unsigned>(step.win_reason);
        }
        out << "}\n";
    }
    return out.str();
}

std::string canonical_trace_hash_v2(const EngineTrace& trace) {
    return sha256_string(canonical_trace_jsonl_v2(trace));
}

std::string semantic_gameplay_jsonl(const EngineTrace& trace) {
    std::ostringstream out;
    const auto& manifest = trace.manifest;
    out << "{\"cardscripts_commit\":" << json_escape(manifest.cardscripts_commit)
        << ",\"core_api_version\":" << json_escape(manifest.core_api_version)
        << ",\"core_commit\":" << json_escape(manifest.core_commit)
        << ",\"database_commit\":" << json_escape(manifest.database_commit)
        << ",\"duel_flags\":" << manifest.duel_flags << ",\"fixture_deck_hashes\":";
    write_string_array(out, manifest.fixture_deck_hashes);
    out << ",\"policy_identifier\":" << json_escape(manifest.policy_identifier)
        << ",\"rules_bundle_id\":" << json_escape(manifest.rules_bundle_id)
        << ",\"seed_bundle\":";
    write_seed_array(out, manifest.seed_bundle);
    out << ",\"schema\":\"ygo.semantic_gameplay.v1\"}\n";
    for (const auto& step : trace.steps) {
        out << "{\"continuation_id\":" << json_escape(step.continuation_id)
            << ",\"continuation_state_hash\":" << json_escape(step.continuation_state_hash)
            << ",\"continuation_step\":" << step.continuation_step
            << ",\"decision_index\":" << step.decision_index
            << ",\"decision_request_kind\":" << json_escape(step.decision_request_kind)
            << ",\"engine_advanced\":" << (step.engine_advanced ? "true" : "false")
            << ",\"engine_step_index\":" << step.engine_step_index
            << ",\"final_engine_response_hash\":";
        write_nullable_string(out, step.final_engine_response_hash);
        out << ",\"ordered_candidate_semantic_keys\":";
        write_string_array(out, step.ordered_candidate_semantic_keys);
        out << ",\"player_to_act\":" << static_cast<unsigned>(step.player_to_act)
            << ",\"public_state_hash\":" << json_escape(step.public_state_hash)
            << ",\"selected_semantic_key\":" << json_escape(step.selected_semantic_key)
            << ",\"step_index\":" << step.step_index << ",\"terminal\":"
            << (step.terminal ? "true" : "false") << ",\"winner\":";
        if (step.winner == 255) {
            out << "null";
        } else {
            out << static_cast<unsigned>(step.winner);
        }
        out << ",\"win_reason\":";
        if (step.win_reason == 255) {
            out << "null";
        } else {
            out << static_cast<unsigned>(step.win_reason);
        }
        out << "}\n";
    }
    return out.str();
}

std::string semantic_gameplay_hash(const EngineTrace& trace) {
    return sha256_string(semantic_gameplay_jsonl(trace));
}

}  // namespace ygo::trace
