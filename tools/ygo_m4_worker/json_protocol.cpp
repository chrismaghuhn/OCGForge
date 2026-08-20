#include "json_protocol.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <iomanip>
#include <limits>
#include <map>
#include <sstream>
#include <stdexcept>
#include <utility>
#include <vector>

namespace ygo::m4::worker {
namespace {

enum class JsonKind {
    Null,
    Boolean,
    Unsigned,
    String,
    Array,
    Object,
};

struct JsonValue {
    JsonKind kind = JsonKind::Null;
    bool boolean = false;
    std::uint64_t unsigned_value = 0;
    std::string string_value;
    std::vector<JsonValue> array_value;
    std::map<std::string, JsonValue> object_value;
};

class JsonParseException final : public std::runtime_error {
public:
    explicit JsonParseException(const std::string& message) : std::runtime_error(message) {}
};

bool is_json_space(const char value) {
    return value == ' ' || value == '\t' || value == '\n' || value == '\r';
}

bool is_hex_digit(const char value) {
    return (value >= '0' && value <= '9') || (value >= 'a' && value <= 'f') ||
           (value >= 'A' && value <= 'F');
}

std::uint32_t hex_value(const char value) {
    if (value >= '0' && value <= '9') {
        return static_cast<std::uint32_t>(value - '0');
    }
    if (value >= 'a' && value <= 'f') {
        return static_cast<std::uint32_t>(value - 'a' + 10);
    }
    return static_cast<std::uint32_t>(value - 'A' + 10);
}

bool is_valid_utf8(const std::string& value) {
    for (std::size_t index = 0; index < value.size();) {
        const auto first = static_cast<unsigned char>(value[index]);
        if (first <= 0x7f) {
            ++index;
            continue;
        }
        if (first >= 0xc2 && first <= 0xdf) {
            if (index + 1 >= value.size()) {
                return false;
            }
            const auto second = static_cast<unsigned char>(value[index + 1]);
            if ((second & 0xc0) != 0x80) {
                return false;
            }
            index += 2;
            continue;
        }
        if (first >= 0xe0 && first <= 0xef) {
            if (index + 2 >= value.size()) {
                return false;
            }
            const auto second = static_cast<unsigned char>(value[index + 1]);
            const auto third = static_cast<unsigned char>(value[index + 2]);
            if ((second & 0xc0) != 0x80 || (third & 0xc0) != 0x80 ||
                (first == 0xe0 && second < 0xa0) || (first == 0xed && second >= 0xa0)) {
                return false;
            }
            index += 3;
            continue;
        }
        if (first >= 0xf0 && first <= 0xf4) {
            if (index + 3 >= value.size()) {
                return false;
            }
            const auto second = static_cast<unsigned char>(value[index + 1]);
            const auto third = static_cast<unsigned char>(value[index + 2]);
            const auto fourth = static_cast<unsigned char>(value[index + 3]);
            if ((second & 0xc0) != 0x80 || (third & 0xc0) != 0x80 || (fourth & 0xc0) != 0x80 ||
                (first == 0xf0 && second < 0x90) || (first == 0xf4 && second > 0x8f)) {
                return false;
            }
            index += 4;
            continue;
        }
        return false;
    }
    return true;
}

void append_utf8(std::string& output, const std::uint32_t codepoint) {
    if (codepoint <= 0x7f) {
        output.push_back(static_cast<char>(codepoint));
    } else if (codepoint <= 0x7ff) {
        output.push_back(static_cast<char>(0xc0 | (codepoint >> 6)));
        output.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
    } else if (codepoint <= 0xffff) {
        output.push_back(static_cast<char>(0xe0 | (codepoint >> 12)));
        output.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f)));
        output.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
    } else if (codepoint <= 0x10ffff) {
        output.push_back(static_cast<char>(0xf0 | (codepoint >> 18)));
        output.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3f)));
        output.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f)));
        output.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
    } else {
        throw JsonParseException("unicode code point is out of range");
    }
}

class JsonParser final {
public:
    explicit JsonParser(const std::string_view input) : input_(input) {}

    JsonValue parse_document() {
        skip_space();
        // Recovery is permitted only for a job_id directly owned by the
        // document's root object. Nested objects are data, never job identity.
        auto value = parse_value(true);
        skip_space();
        if (position_ != input_.size()) {
            fail("trailing data after JSON value");
        }
        return value;
    }

    const std::optional<std::string>& recovered_job_id() const noexcept {
        return recovered_job_id_;
    }

private:
    [[noreturn]] void fail(const std::string& message) const {
        throw JsonParseException(message + " at byte " + std::to_string(position_));
    }

    void skip_space() {
        while (position_ < input_.size() && is_json_space(input_[position_])) {
            ++position_;
        }
    }

    char take() {
        if (position_ >= input_.size()) {
            fail("unexpected end of input");
        }
        return input_[position_++];
    }

    void expect(const char expected) {
        if (take() != expected) {
            fail(std::string("expected '") + expected + "'");
        }
    }

    JsonValue parse_value(const bool recover_root_job_id = false) {
        skip_space();
        if (position_ >= input_.size()) {
            fail("expected JSON value");
        }
        switch (input_[position_]) {
        case 'n':
            parse_literal("null");
            return JsonValue{};
        case 't': {
            parse_literal("true");
            JsonValue value;
            value.kind = JsonKind::Boolean;
            value.boolean = true;
            return value;
        }
        case 'f': {
            parse_literal("false");
            JsonValue value;
            value.kind = JsonKind::Boolean;
            value.boolean = false;
            return value;
        }
        case '"': {
            JsonValue value;
            value.kind = JsonKind::String;
            value.string_value = parse_string();
            return value;
        }
        case '[':
            return parse_array();
        case '{':
            return parse_object(recover_root_job_id);
        case '-':
            fail("negative unsigned values are not allowed");
        default:
            if (input_[position_] >= '0' && input_[position_] <= '9') {
                JsonValue value;
                value.kind = JsonKind::Unsigned;
                value.unsigned_value = parse_unsigned();
                return value;
            }
            fail("invalid JSON value");
        }
    }

    void parse_literal(const std::string_view literal) {
        if (input_.substr(position_, literal.size()) != literal) {
            fail("invalid JSON literal");
        }
        position_ += literal.size();
    }

    std::uint32_t parse_hex_code_unit() {
        if (position_ + 4 > input_.size()) {
            fail("incomplete unicode escape");
        }
        std::uint32_t value = 0;
        for (std::size_t index = 0; index < 4; ++index) {
            if (!is_hex_digit(input_[position_])) {
                fail("invalid unicode escape");
            }
            value = (value << 4) | hex_value(input_[position_++]);
        }
        return value;
    }

    std::string parse_string() {
        expect('"');
        std::string result;
        while (position_ < input_.size()) {
            const char value = input_[position_++];
            if (value == '"') {
                if (!is_valid_utf8(result)) {
                    fail("string contains invalid UTF-8");
                }
                return result;
            }
            if (static_cast<unsigned char>(value) < 0x20) {
                fail("unescaped control character in string");
            }
            if (value != '\\') {
                result.push_back(value);
                continue;
            }
            if (position_ >= input_.size()) {
                fail("incomplete string escape");
            }
            const char escape = input_[position_++];
            switch (escape) {
            case '"':
                result.push_back('"');
                break;
            case '\\':
                result.push_back('\\');
                break;
            case '/':
                result.push_back('/');
                break;
            case 'b':
                result.push_back('\b');
                break;
            case 'f':
                result.push_back('\f');
                break;
            case 'n':
                result.push_back('\n');
                break;
            case 'r':
                result.push_back('\r');
                break;
            case 't':
                result.push_back('\t');
                break;
            case 'u': {
                const auto high = parse_hex_code_unit();
                std::uint32_t codepoint = high;
                if (high >= 0xd800 && high <= 0xdbff) {
                    if (position_ + 2 > input_.size() || input_[position_] != '\\' ||
                        input_[position_ + 1] != 'u') {
                        fail("high surrogate is not followed by a low surrogate");
                    }
                    position_ += 2;
                    const auto low = parse_hex_code_unit();
                    if (low < 0xdc00 || low > 0xdfff) {
                        fail("invalid low surrogate");
                    }
                    codepoint = 0x10000 + ((high - 0xd800) << 10) + (low - 0xdc00);
                } else if (high >= 0xdc00 && high <= 0xdfff) {
                    fail("unpaired low surrogate");
                }
                append_utf8(result, codepoint);
                break;
            }
            default:
                fail("invalid string escape");
            }
        }
        fail("unterminated string");
    }

    std::uint64_t parse_unsigned() {
        if (position_ >= input_.size() || input_[position_] < '0' || input_[position_] > '9') {
            fail("expected unsigned integer");
        }
        if (input_[position_] == '0') {
            ++position_;
            if (position_ < input_.size() && input_[position_] >= '0' && input_[position_] <= '9') {
                fail("leading zero in integer");
            }
            return 0;
        }
        std::uint64_t value = 0;
        while (position_ < input_.size() && input_[position_] >= '0' && input_[position_] <= '9') {
            const auto digit = static_cast<std::uint64_t>(input_[position_++] - '0');
            if (value > (std::numeric_limits<std::uint64_t>::max() - digit) / 10) {
                fail("unsigned integer overflows uint64");
            }
            value = value * 10 + digit;
        }
        if (position_ < input_.size() && (input_[position_] == '.' || input_[position_] == 'e' ||
                                          input_[position_] == 'E')) {
            fail("only unsigned integer values are accepted");
        }
        return value;
    }

    JsonValue parse_array() {
        expect('[');
        JsonValue value;
        value.kind = JsonKind::Array;
        skip_space();
        if (position_ < input_.size() && input_[position_] == ']') {
            ++position_;
            return value;
        }
        while (true) {
            value.array_value.push_back(parse_value(false));
            skip_space();
            if (position_ >= input_.size()) {
                fail("unterminated array");
            }
            if (input_[position_] == ']') {
                ++position_;
                return value;
            }
            expect(',');
            skip_space();
            if (position_ < input_.size() && input_[position_] == ']') {
                fail("trailing comma in array");
            }
        }
    }

    JsonValue parse_object(const bool recover_job_id) {
        expect('{');
        JsonValue value;
        value.kind = JsonKind::Object;
        skip_space();
        if (position_ < input_.size() && input_[position_] == '}') {
            ++position_;
            return value;
        }
        while (true) {
            skip_space();
            if (position_ >= input_.size() || input_[position_] != '"') {
                fail("object key must be a string");
            }
            const auto key = parse_string();
            if (value.object_value.find(key) != value.object_value.end()) {
                fail("duplicate object key: " + key);
            }
            skip_space();
            expect(':');
            auto child = parse_value(false);
            if (recover_job_id && key == "job_id" && child.kind == JsonKind::String &&
                !child.string_value.empty()) {
                recovered_job_id_ = child.string_value;
            }
            value.object_value.emplace(key, std::move(child));
            skip_space();
            if (position_ >= input_.size()) {
                fail("unterminated object");
            }
            if (input_[position_] == '}') {
                ++position_;
                return value;
            }
            expect(',');
            skip_space();
            if (position_ < input_.size() && input_[position_] == '}') {
                fail("trailing comma in object");
            }
        }
    }

    std::string_view input_;
    std::size_t position_ = 0;
    std::optional<std::string> recovered_job_id_;
};

const JsonValue& require_field(const JsonValue& object, const std::string& key) {
    const auto found = object.object_value.find(key);
    if (found == object.object_value.end()) {
        throw JsonParseException("missing required field: " + key);
    }
    return found->second;
}

const JsonValue& require_kind(const JsonValue& object, const std::string& key,
                              const JsonKind expected) {
    const auto& value = require_field(object, key);
    if (value.kind != expected) {
        throw JsonParseException("field has wrong JSON type: " + key);
    }
    return value;
}

std::string require_string(const JsonValue& object, const std::string& key) {
    return require_kind(object, key, JsonKind::String).string_value;
}

std::uint64_t require_unsigned(const JsonValue& object, const std::string& key) {
    return require_kind(object, key, JsonKind::Unsigned).unsigned_value;
}

bool require_boolean(const JsonValue& object, const std::string& key) {
    return require_kind(object, key, JsonKind::Boolean).boolean;
}

void reject_unknown_fields(const JsonValue& object, const std::vector<std::string_view>& allowed) {
    for (const auto& [key, unused] : object.object_value) {
        static_cast<void>(unused);
        if (std::find(allowed.begin(), allowed.end(), key) == allowed.end()) {
            throw JsonParseException("unknown request field: " + key);
        }
    }
}

std::vector<std::string> require_string_array(const JsonValue& object, const std::string& key) {
    const auto& value = require_kind(object, key, JsonKind::Array);
    std::vector<std::string> result;
    result.reserve(value.array_value.size());
    for (const auto& item : value.array_value) {
        if (item.kind != JsonKind::String) {
            throw JsonParseException("array contains a non-string value: " + key);
        }
        result.push_back(item.string_value);
    }
    return result;
}

std::vector<std::uint32_t> require_code_array(const JsonValue& object, const std::string& key) {
    const auto& value = require_kind(object, key, JsonKind::Array);
    std::vector<std::uint32_t> result;
    result.reserve(value.array_value.size());
    for (const auto& item : value.array_value) {
        if (item.kind != JsonKind::Unsigned || item.unsigned_value > std::numeric_limits<std::uint32_t>::max()) {
            throw JsonParseException("array contains an invalid code: " + key);
        }
        result.push_back(static_cast<std::uint32_t>(item.unsigned_value));
    }
    return result;
}

std::string json_escape(const std::string_view value) {
    std::ostringstream output;
    output << '"';
    constexpr char hex[] = "0123456789abcdef";
    for (const auto byte : value) {
        const auto character = static_cast<unsigned char>(byte);
        switch (character) {
        case '"':
            output << "\\\"";
            break;
        case '\\':
            output << "\\\\";
            break;
        case '\b':
            output << "\\b";
            break;
        case '\f':
            output << "\\f";
            break;
        case '\n':
            output << "\\n";
            break;
        case '\r':
            output << "\\r";
            break;
        case '\t':
            output << "\\t";
            break;
        default:
            if (character < 0x20) {
                output << "\\u00" << hex[character >> 4] << hex[character & 0x0f];
            } else {
                output << static_cast<char>(character);
            }
            break;
        }
    }
    output << '"';
    return output.str();
}

bool is_sha256_hex(const std::string& value) {
    if (value.size() != 64) {
        return false;
    }
    return std::all_of(value.begin(), value.end(), [](const char character) {
        return is_hex_digit(character);
    });
}

void append_json_string(std::ostringstream& output, const std::string_view key,
                        const std::string_view value, const bool comma = true) {
    if (comma) {
        output << ',';
    }
    output << json_escape(key) << ':' << json_escape(value);
}

void append_json_unsigned(std::ostringstream& output, const std::string_view key,
                          const std::uint64_t value, const bool comma = true) {
    if (comma) {
        output << ',';
    }
    output << json_escape(key) << ':' << value;
}

void append_json_bool(std::ostringstream& output, const std::string_view key, const bool value,
                      const bool comma = true) {
    if (comma) {
        output << ',';
    }
    output << json_escape(key) << ':' << (value ? "true" : "false");
}

void append_json_null(std::ostringstream& output, const std::string_view key,
                      const bool comma = true) {
    if (comma) {
        output << ',';
    }
    output << json_escape(key) << ":null";
}

void append_error_object(std::ostringstream& output, const ygo::simulation::ErrorCounters& errors) {
    output << ",\"errors\":{";
    append_json_unsigned(output, "retries", errors.retries, false);
    append_json_unsigned(output, "unsupported", errors.unsupported);
    append_json_unsigned(output, "automatic", errors.automatic);
    append_json_unsigned(output, "truncated", errors.truncated);
    append_json_unsigned(output, "core_errors", errors.core_errors);
    append_json_unsigned(output, "worker_errors", errors.worker_errors);
    output << '}';
}

void append_timing_object(std::ostringstream& output, const ygo::simulation::SimulationResult& result) {
    output << ",\"timing_us\":{";
    append_json_unsigned(output, "core_process", result.timing.core_process_us, false);
    append_json_unsigned(output, "protocol_candidate", result.timing.protocol_candidate_us);
    append_json_unsigned(output, "continuation", result.timing.continuation_us);
    append_json_unsigned(output, "observation", result.timing.observation_us);
    append_json_unsigned(output, "trace_hash", result.timing.trace_hash_us);
    append_json_unsigned(output, "serialization", result.timing.serialization_us);
    append_json_unsigned(output, "other", result.timing.other_us);
    append_json_unsigned(output, "trace_persistence", result.trace_persistence_us);
    output << '}';
}

void append_counter_object(std::ostringstream& output,
                           const ygo::simulation::OperationCounters& counters) {
    output << ",\"counters\":{";
    append_json_unsigned(output, "ocg_duel_process", counters.ocg_duel_process, false);
    append_json_unsigned(output, "ocg_duel_query", counters.ocg_duel_query);
    append_json_unsigned(output, "ocg_duel_query_location", counters.ocg_duel_query_location);
    append_json_unsigned(output, "ocg_duel_query_field", counters.ocg_duel_query_field);
    append_json_unsigned(output, "ocg_duel_query_count", counters.ocg_duel_query_count);
    append_json_unsigned(output, "script_reader_requests", counters.script_reader_requests);
    append_json_unsigned(output, "script_loads", counters.script_loads);
    append_json_unsigned(output, "observations", counters.observations);
    append_json_unsigned(output, "entities_projected", counters.entities_projected);
    append_json_unsigned(output, "candidate_sets", counters.candidate_sets);
    append_json_unsigned(output, "candidate_total", counters.candidate_total);
    append_json_unsigned(output, "candidate_max", counters.candidate_max);
    append_json_unsigned(output, "semantic_hashes", counters.semantic_hashes);
    append_json_unsigned(output, "trace_bytes_serialized", counters.trace_bytes_serialized);
    output << '}';
}

void append_worker_object(std::ostringstream& output, const ygo::simulation::SimulationResult& result,
                          const WorkerReadyInfo& worker) {
    const auto pid = result.worker_pid == 0 ? worker.pid : result.worker_pid;
    output << ",\"worker\":{";
    append_json_unsigned(output, "pid", pid, false);
    append_json_unsigned(output, "restart_index", result.worker_restart_index);
    append_json_bool(output, "crashed", result.worker_crashed);
    append_json_bool(output, "restarted", result.worker_restarted);
    output << '}';
}

#ifdef YGO_M4_PERFORMANCE_AUDIT
void append_audit_timing_bucket(std::ostringstream& output, const std::string_view name,
                                const ygo::observation::PerformanceAuditTiming& timing,
                                const bool comma) {
    if (comma) {
        output << ',';
    }
    output << json_escape(name) << ":{";
    append_json_unsigned(output, "total_us", timing.total_us, false);
    append_json_unsigned(output, "calls", timing.calls);
    append_json_unsigned(output, "mean_us_per_call", timing.mean_us_per_call());
    output << '}';
}

void append_observation_audit_timing(std::ostringstream& output,
                                     const ygo::observation::PerformanceAuditSnapshot& audit) {
    output << ",\"observation_timing_us\":{";
    for (std::size_t index = 0; index < audit.observation_timing.size(); ++index) {
        append_audit_timing_bucket(
            output,
            ygo::observation::PerformanceAuditCollector::bucket_name(
                static_cast<ygo::observation::PerformanceAuditBucket>(index)),
            audit.observation_timing[index], index != 0);
    }
    output << '}';
}

void append_auxiliary_audit_timing(std::ostringstream& output,
                                   const ygo::observation::PerformanceAuditSnapshot& audit) {
    output << ",\"auxiliary_timing_us\":{";
    for (std::size_t index = 0; index < audit.auxiliary_timing.size(); ++index) {
        append_audit_timing_bucket(
            output,
            ygo::observation::PerformanceAuditCollector::auxiliary_bucket_name(
                static_cast<ygo::observation::PerformanceAuditAuxiliaryBucket>(index)),
            audit.auxiliary_timing[index], index != 0);
    }
    output << '}';
}

void append_setup_audit_timing(std::ostringstream& output,
                               const ygo::observation::PerformanceAuditSnapshot& audit) {
    output << ",\"setup_timing_us\":{";
    for (std::size_t index = 0; index < audit.setup_timing.size(); ++index) {
        append_audit_timing_bucket(
            output,
            ygo::observation::PerformanceAuditCollector::setup_bucket_name(
                static_cast<ygo::observation::PerformanceAuditSetupBucket>(index)),
            audit.setup_timing[index], index != 0);
    }
    output << '}';
}

void append_performance_audit_counters(
    std::ostringstream& output, const ygo::observation::PerformanceAuditSnapshot& audit) {
    const auto& counters = audit.counters;
    output << ",\"observation_counters\":{";
    append_json_unsigned(output, "observations", counters.observations, false);
    append_json_unsigned(output, "query_field_calls", counters.query_field_calls);
    append_json_unsigned(output, "query_location_calls", counters.query_location_calls);
    append_json_unsigned(output, "query_individual_calls", counters.query_individual_calls);
    append_json_unsigned(output, "entities_projected", counters.entities_projected);
    append_json_unsigned(output, "identity_known_entities", counters.identity_known_entities);
    append_json_unsigned(output, "redacted_entities", counters.redacted_entities);
    append_json_unsigned(output, "static_card_data_lookups", counters.static_card_data_lookups);
    append_json_unsigned(output, "current_property_projections", counters.current_property_projections);
    append_json_unsigned(output, "relationship_objects", counters.relationship_objects);
    append_json_unsigned(output, "allocation_copy_events", counters.allocation_copy_events);
    append_json_unsigned(output, "script_loads", counters.script_loads);
    output << '}';

    const auto& detail = audit.detail_counters;
    output << ",\"observation_detail_counters\":{";
    append_json_unsigned(output, "query_decode_calls", detail.query_decode_calls, false);
    append_json_unsigned(output, "zone_projection_calls", detail.zone_projection_calls);
    append_json_unsigned(output, "relationship_resolution_events", detail.relationship_resolution_events);
    append_json_unsigned(output, "observation_query_field_calls", detail.observation_query_field_calls);
    append_json_unsigned(output, "public_state_hash_query_field_calls",
                         detail.public_state_hash_query_field_calls);
    append_json_unsigned(output, "script_reader_requests", detail.script_reader_requests);
    output << '}';
}

void append_entities_by_zone(std::ostringstream& output,
                             const ygo::observation::PerformanceAuditSnapshot& audit) {
    output << ",\"entities_by_zone\":{";
    for (std::size_t index = 0; index < audit.entities_by_zone.size(); ++index) {
        if (index != 0) {
            output << ',';
        }
        const auto zone = static_cast<ygo::observation::SemanticZone>(index);
        output << json_escape(ygo::observation::semantic_zone_name(zone)) << ":{";
        const auto& counters = audit.entities_by_zone[index];
        append_json_unsigned(output, "entities_projected", counters.entities_projected, false);
        append_json_unsigned(output, "identity_known", counters.identity_known);
        append_json_unsigned(output, "redacted", counters.redacted);
        output << '}';
    }
    output << '}';
}

#endif

}  // namespace

std::string serialize_ready(const WorkerReadyInfo& worker,
                            const ygo::simulation::CanonicalSimulationConfig& config) {
    if (!ygo::simulation::is_canonical_identity(config) || worker.pid == 0) {
        throw std::runtime_error("cannot serialize a non-canonical worker ready envelope");
    }
    std::ostringstream output;
    output << '{';
    append_json_string(output, "schema", kProtocolSchema, false);
    append_json_string(output, "type", "ready");
    append_json_string(output, "protocol_version", kProtocolVersion);
    append_json_unsigned(output, "pid", worker.pid);
    append_json_string(output, "rules_bundle_id", config.rules_bundle_id);
    append_json_string(output, "core_patchset_sha256", config.patchset_sha256);
    output << ",\"deck_hashes\":[" << json_escape(config.locked_deck_hashes[0]) << ','
            << json_escape(config.locked_deck_hashes[1]) << ']';
    append_json_string(output, "format_id", config.format);
    append_json_string(output, "duel_mode_name", config.duel_mode);
    append_json_unsigned(output, "duel_flags", config.duel_flags);
    append_json_string(output, "compiler_identity", worker.compiler_identity);
    append_json_string(output, "build_type", worker.build_type);
    append_json_string(output, "worker_identity", kWorkerIdentity);
    output << '}';
    return output.str();
}

std::string serialize_result(const ygo::simulation::SimulationResult& result,
                             const WorkerReadyInfo& worker) {
    const bool complete_pass =
        result.pass && result.terminal && result.winner.has_value() && result.win_reason.has_value() &&
        is_sha256_hex(result.gameplay_hash) &&
        (!result.trace_hash.has_value() || is_sha256_hex(*result.trace_hash)) &&
        result.failure_code.empty() && result.error_message.empty() && result.errors.retries == 0 &&
        result.errors.unsupported == 0 && result.errors.automatic == 0 &&
        result.errors.truncated == 0 && result.errors.core_errors == 0 &&
        result.errors.worker_errors == 0;
    const bool wire_pass = complete_pass;
    auto wire_errors = result.errors;
    std::string wire_failure_code = result.failure_code;
    std::string wire_error_message = result.error_message;
    if (result.pass && !complete_pass) {
        wire_errors.worker_errors += 1;
        wire_failure_code = "incomplete_result";
        wire_error_message = "passed simulation result omitted complete terminal evidence";
    }

    std::ostringstream output;
    output << '{';
    append_json_string(output, "schema", kProtocolSchema, false);
    append_json_string(output, "type", "result");
    append_json_string(output, "status", wire_pass ? "passed" : "failed");
    append_json_string(output, "job_id", result.job_id);
    append_json_bool(output, "terminal", wire_pass && result.terminal);
    if (result.winner.has_value() && wire_pass) {
        append_json_unsigned(output, "winner", *result.winner);
    } else {
        append_json_null(output, "winner");
    }
    if (result.win_reason.has_value() && wire_pass) {
        append_json_unsigned(output, "win_reason", *result.win_reason);
    } else {
        append_json_null(output, "win_reason");
    }
    append_json_unsigned(output, "engine_steps", result.engine_steps);
    append_json_unsigned(output, "interactive_decisions", result.interactive_decisions);
    append_json_unsigned(output, "semantic_action_count", result.semantic_action_count);
    if (wire_pass) {
        append_json_string(output, "gameplay_hash", result.gameplay_hash);
    } else {
        append_json_null(output, "gameplay_hash");
    }
    if (wire_pass && result.trace_hash.has_value()) {
        append_json_string(output, "trace_hash", *result.trace_hash);
    } else {
        append_json_null(output, "trace_hash");
    }
    append_json_unsigned(output, "simulation_elapsed_us", result.simulation_elapsed_us);
    append_json_null(output, "coordinator_elapsed_us");
    append_error_object(output, wire_errors);
    append_timing_object(output, result);
    append_counter_object(output, result.operations);
    append_worker_object(output, result, worker);
    if (!wire_failure_code.empty()) {
        append_json_string(output, "failure_code", wire_failure_code);
    } else {
        append_json_null(output, "failure_code");
    }
    if (!wire_error_message.empty()) {
        append_json_string(output, "error_message", wire_error_message);
    } else {
        append_json_null(output, "error_message");
    }
    output << '}';
    return output.str();
}

#ifdef YGO_M4_PERFORMANCE_AUDIT
std::string serialize_performance_audit(
    const std::string& job_id,
    const ygo::observation::PerformanceAuditSnapshot& audit) {
    std::ostringstream output;
    output << '{';
    append_json_string(output, "schema", "ocgforge.m4.performance_audit.v1", false);
    append_json_string(output, "type", "performance_audit");
    append_json_string(output, "job_id", job_id);
    append_json_unsigned(output, "observation_total_us", audit.observation_total_us);
    append_observation_audit_timing(output, audit);
    append_performance_audit_counters(output, audit);
    append_setup_audit_timing(output, audit);
    append_auxiliary_audit_timing(output, audit);
    append_entities_by_zone(output, audit);
    output << '}';
    return output.str();
}
#endif

std::string serialize_protocol_error(const ProtocolParseResult& parse_result) {
    std::ostringstream output;
    output << '{';
    append_json_string(output, "schema", kProtocolSchema, false);
    append_json_string(output, "type", "protocol_error");
    if (parse_result.recoverable_job_id.has_value()) {
        append_json_string(output, "job_id", *parse_result.recoverable_job_id);
    } else {
        append_json_null(output, "job_id");
    }
    append_json_string(output, "failure_code", parse_result.failure_code);
    append_json_string(output, "error_message", parse_result.error_message);
    output << '}';
    return output.str();
}

ProtocolParseResult parse_job_request(const std::string_view line) {
    ProtocolParseResult result;
    JsonParser parser(line);
    JsonValue root;
    try {
        root = parser.parse_document();
    } catch (const JsonParseException& error) {
        result.recoverable_job_id = parser.recovered_job_id();
        result.failure_code = "malformed_request";
        result.error_message = error.what();
        return result;
    }
    result.recoverable_job_id = parser.recovered_job_id();
    try {
        if (root.kind != JsonKind::Object) {
            throw JsonParseException("job request must be a JSON object");
        }
        reject_unknown_fields(root, {"schema", "type", "job_id", "seed", "seat_assignment",
                                     "starting_player", "max_steps", "canonical_rules_id", "mode",
                                     "observation_mode", "instrumentation", "persist_trace",
                                     "replay_actions", "focus_codes", "setup_script", "force_unsupported",
                                     "trace_output"});
        if (require_string(root, "schema") != kProtocolSchema || require_string(root, "type") != "job") {
            throw JsonParseException("invalid job schema or type");
        }
        auto job_id = require_string(root, "job_id");
        if (job_id.empty()) {
            throw JsonParseException("job_id must not be empty");
        }
        ygo::simulation::SimulationJob job;
        job.job_id = std::move(job_id);
        job.seed = require_unsigned(root, "seed");
        const auto seat = require_string(root, "seat_assignment");
        if (seat == "normal") {
            job.seat_assignment = ygo::simulation::SeatAssignment::Normal;
        } else if (seat == "mirror") {
            job.seat_assignment = ygo::simulation::SeatAssignment::Mirror;
        } else {
            throw JsonParseException("seat_assignment must be normal or mirror");
        }
        const auto starting_player = require_unsigned(root, "starting_player");
        if (starting_player > 1) {
            throw JsonParseException("starting_player must be 0 or 1");
        }
        job.starting_player = static_cast<std::uint8_t>(starting_player);
        const auto max_steps = require_unsigned(root, "max_steps");
        if (max_steps > std::numeric_limits<std::uint32_t>::max()) {
            throw JsonParseException("max_steps exceeds uint32");
        }
        job.max_steps = static_cast<std::uint32_t>(max_steps);
        job.canonical_rules_id = require_string(root, "canonical_rules_id");
        const auto mode = require_string(root, "mode");
        if (mode == "conformance") {
            job.mode = ygo::simulation::SimulationMode::Conformance;
        } else if (mode == "throughput") {
            job.mode = ygo::simulation::SimulationMode::Throughput;
        } else {
            throw JsonParseException("mode must be conformance or throughput");
        }
        const auto observation_mode = require_string(root, "observation_mode");
        if (observation_mode == "full") {
            job.observation_mode = ygo::simulation::ObservationMode::Full;
        } else if (observation_mode == "off_diagnostic") {
            job.observation_mode = ygo::simulation::ObservationMode::OffDiagnostic;
        } else {
            throw JsonParseException("observation_mode has an unsupported value");
        }
        job.instrumentation = require_boolean(root, "instrumentation");
        job.persist_trace = require_boolean(root, "persist_trace");
        job.replay_actions = require_string_array(root, "replay_actions");
        job.focus_codes = require_code_array(root, "focus_codes");
        job.setup_script = require_string(root, "setup_script");
        job.force_unsupported = require_boolean(root, "force_unsupported");
        if (root.object_value.find("trace_output") != root.object_value.end()) {
            job.trace_output = require_string(root, "trace_output");
        }
        result.job = std::move(job);
        result.failure_code.clear();
        result.error_message.clear();
    } catch (const JsonParseException& error) {
        result.failure_code = "invalid_request";
        result.error_message = error.what();
    }
    return result;
}

std::optional<std::string> recover_job_id(const std::string_view line) {
    JsonParser parser(line);
    try {
        static_cast<void>(parser.parse_document());
    } catch (const JsonParseException&) {
        return parser.recovered_job_id();
    }
    return parser.recovered_job_id();
}

}  // namespace ygo::m4::worker
