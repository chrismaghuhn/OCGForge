#include "ygo/environment/identity_contract.hpp"

#include <algorithm>
#include <cstddef>
#include <limits>
#include <stdexcept>

#include "ygo/trace/sha256.hpp"

namespace ygo::environment {
namespace {

void append_u32be(std::vector<std::uint8_t>& bytes, const std::uint32_t integer) {
    bytes.push_back(static_cast<std::uint8_t>(integer >> 24));
    bytes.push_back(static_cast<std::uint8_t>(integer >> 16));
    bytes.push_back(static_cast<std::uint8_t>(integer >> 8));
    bytes.push_back(static_cast<std::uint8_t>(integer));
}

void append_count(std::vector<std::uint8_t>& bytes, const std::size_t value) {
    if (value > std::numeric_limits<std::uint32_t>::max()) {
        throw std::length_error("canonical identity field exceeds u32 length");
    }
    append_u32be(bytes, static_cast<std::uint32_t>(value));
}

void append_string(std::vector<std::uint8_t>& bytes, const std::string_view value) {
    append_count(bytes, value.size());
    bytes.insert(bytes.end(), value.begin(), value.end());
}

void append_string_vector(std::vector<std::uint8_t>& bytes,
                          const std::vector<std::string>& values) {
    append_count(bytes, values.size());
    for (const auto& value : values) {
        append_string(bytes, value);
    }
}

void append_u32_vector(std::vector<std::uint8_t>& bytes,
                       std::vector<std::uint32_t> values) {
    std::sort(values.begin(), values.end());
    values.erase(std::unique(values.begin(), values.end()), values.end());
    append_count(bytes, values.size());
    for (const auto value : values) {
        append_u32be(bytes, value);
    }
}

void validate_logical_script_name(const std::string& value) {
    if (value.empty() || value.front() == '/' || value.find('\\') != std::string::npos ||
        value.find("//") != std::string::npos || value.rfind("./", 0) == 0) {
        throw std::invalid_argument("global script name is not a normalized logical relative name");
    }
    std::size_t segment_start = 0;
    while (segment_start <= value.size()) {
        const auto separator = value.find('/', segment_start);
        const auto segment_length = separator == std::string::npos
                                        ? value.size() - segment_start
                                        : separator - segment_start;
        const auto segment = value.substr(segment_start, segment_length);
        if (segment.empty() || segment == "." || segment == "..") {
            throw std::invalid_argument("global script name contains a traversal segment");
        }
        if (separator == std::string::npos) {
            break;
        }
        segment_start = separator + 1;
    }
}

}  // namespace

std::vector<std::uint8_t> canonical_required_script_closure_bytes(
    const RequiredScriptClosureInput& input) {
    if (input.card_scripts_commit.empty() || input.card_scripts_tree_sha256.empty() ||
        input.script_resolution_contract_id.empty() || input.required_global_script_names.empty()) {
        throw std::invalid_argument("required-script closure input contains an empty identity field");
    }
    for (const auto& name : input.required_global_script_names) {
        validate_logical_script_name(name);
    }

    std::vector<std::uint8_t> bytes;
    bytes.reserve(256);
    append_string(bytes, kRequiredScriptClosureDomain);
    append_string(bytes, kRequiredScriptClosureSchemaId);
    append_string(bytes, input.card_scripts_commit);
    append_string(bytes, input.card_scripts_tree_sha256);
    append_string(bytes, input.script_resolution_contract_id);
    append_string_vector(bytes, input.required_global_script_names);
    append_u32_vector(bytes, input.required_script_codes);
    return bytes;
}

std::string required_script_closure_identity(const RequiredScriptClosureInput& input) {
    return trace::sha256_bytes(canonical_required_script_closure_bytes(input));
}

}  // namespace ygo::environment
