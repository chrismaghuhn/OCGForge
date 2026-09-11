#include "authority_publisher.hpp"

#include <fstream>
#include <optional>
#include <system_error>
#include <utility>

#if defined(_WIN32)
#include <cwchar>
#endif

namespace ygo::phase6::tooling {
namespace {

bool set_error(std::string* error, std::string message) {
    if (error != nullptr) *error = std::move(message);
    return false;
}

std::optional<std::filesystem::path> normalized_path(
    const std::filesystem::path& path) {
    if (path.empty()) return std::nullopt;
    std::error_code error;
    auto result = std::filesystem::weakly_canonical(path, error);
    if (!error) return result.lexically_normal();
    error.clear();
    result = std::filesystem::absolute(path, error);
    if (error) return std::nullopt;
    return result.lexically_normal();
}

bool paths_equal(const std::filesystem::path& left,
                 const std::filesystem::path& right) {
#if defined(_WIN32)
    return _wcsicmp(left.native().c_str(), right.native().c_str()) == 0;
#else
    return left == right;
#endif
}

void remove_staging_file(const std::filesystem::path& path) noexcept {
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
}

}  // namespace

bool publication_paths_are_distinct(
    const std::filesystem::path& output_path,
    const std::filesystem::path& diagnostics_path) {
    const auto normalized_output = normalized_path(output_path);
    const auto normalized_diagnostics = normalized_path(diagnostics_path);
    if (!normalized_output.has_value() || !normalized_diagnostics.has_value()) {
        return false;
    }
    return !paths_equal(*normalized_output, *normalized_diagnostics);
}

bool publish_staged_authority(
    const std::filesystem::path& output_path,
    const std::vector<std::uint8_t>& authority_bytes,
    const AuthorityPublicationFinalizer& finalizer,
    std::string* error) {
    const auto staging_path = std::filesystem::path(
        output_path.string() + ".staging");
    try {
        std::error_code exists_error;
        if (std::filesystem::exists(output_path, exists_error)) {
            return set_error(error,
                             "Task7 V3 authority output already exists");
        }
        if (exists_error) {
            return set_error(error,
                             "Task7 V3 authority output could not be inspected");
        }
        exists_error.clear();
        if (std::filesystem::exists(staging_path, exists_error)) {
            return set_error(error,
                             "Task7 V3 authority staging output already exists");
        }
        if (exists_error) {
            return set_error(error,
                             "Task7 V3 authority staging output could not be inspected");
        }

        {
            std::ofstream staging(staging_path,
                                  std::ios::binary | std::ios::out |
                                      std::ios::trunc);
            if (!staging) {
                return set_error(error,
                                 "Task7 V3 authority staging output could not be opened");
            }
            staging.write(
                reinterpret_cast<const char*>(authority_bytes.data()),
                static_cast<std::streamsize>(authority_bytes.size()));
            staging.flush();
            if (!staging) {
                remove_staging_file(staging_path);
                return set_error(error,
                                 "Task7 V3 authority staging output could not be written");
            }
            staging.close();
            if (!staging) {
                remove_staging_file(staging_path);
                return set_error(error,
                                 "Task7 V3 authority staging output could not be closed");
            }
        }

        if (finalizer) {
            bool finalized = false;
            try {
                finalized = finalizer();
            } catch (...) {
                finalized = false;
            }
            if (!finalized) {
                remove_staging_file(staging_path);
                return set_error(error,
                                 "Task7 V3 final diagnostic RUN_END failed");
            }
        }

        std::error_code rename_error;
        std::filesystem::rename(staging_path, output_path, rename_error);
        if (rename_error) {
            remove_staging_file(staging_path);
            return set_error(error,
                             "Task7 V3 authority output could not be published");
        }
        return true;
    } catch (const std::exception& exception) {
        remove_staging_file(staging_path);
        return set_error(error, exception.what());
    } catch (...) {
        remove_staging_file(staging_path);
        return set_error(error,
                         "Task7 V3 authority publication threw");
    }
}

}  // namespace ygo::phase6::tooling
