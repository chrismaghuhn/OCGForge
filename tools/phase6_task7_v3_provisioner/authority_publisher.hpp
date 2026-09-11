#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace ygo::phase6::tooling {

using AuthorityPublicationFinalizer = std::function<bool()>;

bool publication_paths_are_distinct(const std::filesystem::path& output_path,
                                     const std::filesystem::path& diagnostics_path);

bool publish_staged_authority(
    const std::filesystem::path& output_path,
    const std::vector<std::uint8_t>& authority_bytes,
    const AuthorityPublicationFinalizer& finalizer,
    std::string* error = nullptr);

}  // namespace ygo::phase6::tooling
