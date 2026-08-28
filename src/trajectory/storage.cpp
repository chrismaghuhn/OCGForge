#include "ygo/trajectory/storage.hpp"

#include <atomic>
#include <fstream>
#include <limits>
#include <string>
#include <system_error>

#include "ygo/trajectory/codec.hpp"
#include "ygo/trace/sha256.hpp"

namespace ygo::trajectory::storage {
namespace {

std::atomic<std::uint64_t> temporary_directory_sequence{0};

void set_error(std::string* error, std::string message) {
    if (error != nullptr) {
        *error = std::move(message);
    }
}

bool valid_artifact_kind(const std::string_view value) noexcept {
    if (value.empty()) {
        return false;
    }
    for (const auto character : value) {
        if (!((character >= 'a' && character <= 'z') ||
              (character >= '0' && character <= '9') || character == '.' || character == '-' ||
              character == '_')) {
            return false;
        }
    }
    return true;
}

bool read_exact_file(const std::filesystem::path& path,
                     std::vector<std::uint8_t>& output,
                     std::string* error) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        set_error(error, "cannot open published artifact for verification");
        return false;
    }
    input.seekg(0, std::ios::end);
    const auto end = input.tellg();
    if (end < 0 || static_cast<std::uintmax_t>(end) >
                       static_cast<std::uintmax_t>(std::numeric_limits<std::size_t>::max())) {
        set_error(error, "published artifact length is invalid");
        return false;
    }
    const auto size = static_cast<std::size_t>(end);
    if (size > static_cast<std::size_t>(std::numeric_limits<std::streamsize>::max())) {
        set_error(error, "published artifact is too large for verification");
        return false;
    }
    input.seekg(0, std::ios::beg);
    output.resize(size);
    if (size != 0) {
        input.read(reinterpret_cast<char*>(output.data()), static_cast<std::streamsize>(size));
        if (input.gcount() != static_cast<std::streamsize>(size)) {
            set_error(error, "published artifact is truncated");
            return false;
        }
    }
    return true;
}

bool remove_temporary_directory(const std::filesystem::path& path) noexcept {
    std::error_code ignored;
    std::filesystem::remove_all(path, ignored);
    return !ignored;
}

}  // namespace

std::optional<PublishedArtifact> publish_content_addressed_artifact(
    const std::filesystem::path& directory,
    const std::string_view artifact_kind,
    const std::string_view expected_artifact_sha256,
    const std::vector<std::uint8_t>& bytes,
    std::string* error) {
    if (!valid_artifact_kind(artifact_kind)) {
        set_error(error, "artifact kind is not a safe token");
        return std::nullopt;
    }
    if (!is_lower_hex_digest(expected_artifact_sha256)) {
        set_error(error, "expected artifact digest is not canonical");
        return std::nullopt;
    }
    if (trace::sha256_bytes(bytes) != expected_artifact_sha256) {
        set_error(error, "expected artifact digest does not match bytes");
        return std::nullopt;
    }
    if (bytes.size() > static_cast<std::size_t>(std::numeric_limits<std::streamsize>::max())) {
        set_error(error, "artifact is too large for the local publication stream");
        return std::nullopt;
    }

    std::error_code filesystem_error;
    std::filesystem::create_directories(directory, filesystem_error);
    if (filesystem_error) {
        set_error(error, "cannot create artifact publication directory: " +
                            filesystem_error.message());
        return std::nullopt;
    }
    if (!std::filesystem::is_directory(directory, filesystem_error) || filesystem_error) {
        set_error(error, "artifact publication target is not a directory");
        return std::nullopt;
    }

    const std::string filename = std::string(artifact_kind) + "." +
                                 std::string(expected_artifact_sha256) + ".bin";
    const auto final_path = directory / filename;
    if (std::filesystem::exists(final_path, filesystem_error) && !filesystem_error) {
        std::vector<std::uint8_t> existing;
        if (!read_exact_file(final_path, existing, error)) {
            return std::nullopt;
        }
        if (existing != bytes) {
            set_error(error, "content-addressed artifact already exists with different bytes");
            return std::nullopt;
        }
        return PublishedArtifact{final_path, std::string(expected_artifact_sha256)};
    }
    if (filesystem_error) {
        set_error(error, "cannot inspect final artifact path: " + filesystem_error.message());
        return std::nullopt;
    }

    std::filesystem::path temporary_directory;
    for (std::uint32_t attempt = 0; attempt != 1024; ++attempt) {
        const auto sequence = temporary_directory_sequence.fetch_add(1, std::memory_order_relaxed);
        temporary_directory = directory /
                              (filename + ".tmp." + std::to_string(sequence));
        filesystem_error.clear();
        if (std::filesystem::create_directory(temporary_directory, filesystem_error)) {
            break;
        }
        if (filesystem_error) {
            set_error(error, "cannot create temporary artifact directory: " +
                                filesystem_error.message());
            return std::nullopt;
        }
        temporary_directory.clear();
    }
    if (temporary_directory.empty()) {
        set_error(error, "could not allocate a unique temporary artifact directory");
        return std::nullopt;
    }

    const auto temporary_file = temporary_directory / "payload";
    bool success = false;
    do {
        std::ofstream output(temporary_file, std::ios::binary | std::ios::trunc);
        if (!output) {
            set_error(error, "cannot open temporary artifact");
            break;
        }
        if (!bytes.empty()) {
            output.write(reinterpret_cast<const char*>(bytes.data()),
                         static_cast<std::streamsize>(bytes.size()));
        }
        output.flush();
        if (!output) {
            set_error(error, "temporary artifact flush failed");
            break;
        }
        output.close();
        if (output.fail()) {
            set_error(error, "temporary artifact close failed");
            break;
        }

        std::vector<std::uint8_t> verified;
        if (!read_exact_file(temporary_file, verified, error) || verified != bytes ||
            trace::sha256_bytes(verified) != expected_artifact_sha256) {
            set_error(error, "temporary artifact verification failed");
            break;
        }

        filesystem_error.clear();
        std::filesystem::rename(temporary_file, final_path, filesystem_error);
        if (filesystem_error) {
            if (std::filesystem::exists(final_path, filesystem_error) && !filesystem_error) {
                std::vector<std::uint8_t> existing;
                if (read_exact_file(final_path, existing, error) && existing == bytes) {
                    success = true;
                    break;
                }
                set_error(error, "content-addressed artifact publication conflicts with existing bytes");
                break;
            }
            set_error(error, "atomic artifact publication failed: " + filesystem_error.message());
            break;
        }
        success = true;
    } while (false);

    remove_temporary_directory(temporary_directory);
    if (!success) {
        return std::nullopt;
    }
    return PublishedArtifact{final_path, std::string(expected_artifact_sha256)};
}

}  // namespace ygo::trajectory::storage
