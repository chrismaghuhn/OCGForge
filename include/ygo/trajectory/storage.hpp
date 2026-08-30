#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ygo::trajectory::storage {

struct PublishedArtifact final {
    std::filesystem::path path;
    std::string artifact_sha256;
};

std::optional<PublishedArtifact> publish_content_addressed_artifact(
    const std::filesystem::path& directory,
    std::string_view artifact_kind,
    std::string_view expected_artifact_sha256,
    const std::vector<std::uint8_t>& bytes,
    std::string* error = nullptr);

}  // namespace ygo::trajectory::storage
