#pragma once

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

namespace graphx::sar {

enum class GotchaInputOrderingMode {
    Lexical,
    Manifest,
};

struct GotchaInputOrderingError {
    std::string code{};
    std::filesystem::path path{};
    std::string message{};
};

struct GotchaInputOrderingResult {
    std::vector<std::filesystem::path> files{};
    std::vector<GotchaInputOrderingError> errors{};

    [[nodiscard]] bool ok() const noexcept {
        return errors.empty();
    }
};

struct GotchaInputOrderingOptions {
    std::filesystem::path input_directory{};
    std::filesystem::path manifest_path{};
    GotchaInputOrderingMode mode{GotchaInputOrderingMode::Lexical};
    std::string extension{".mat"};
};

class GotchaInputOrdering {
public:
    [[nodiscard]] static GotchaInputOrderingResult Discover(
        const GotchaInputOrderingOptions& options) {
        if (options.mode == GotchaInputOrderingMode::Manifest) {
            return DiscoverManifest(options.input_directory, options.manifest_path);
        }
        return DiscoverLexical(options.input_directory, options.extension);
    }

    [[nodiscard]] static GotchaInputOrderingResult DiscoverLexical(
        const std::filesystem::path& input_directory,
        const std::string& extension = ".mat") {
        GotchaInputOrderingResult result{};
        if (!RequireDirectory(input_directory, result)) {
            return result;
        }

        std::error_code error{};
        for (const auto& entry : std::filesystem::directory_iterator(input_directory, error)) {
            std::error_code status_error{};
            if (!entry.is_regular_file(status_error)) {
                continue;
            }
            const auto path = entry.path();
            if (path.extension() == extension) {
                result.files.push_back(path.lexically_normal());
            }
        }
        if (error) {
            result.errors.push_back(GotchaInputOrderingError{
                .code = "input_directory_read_error",
                .path = input_directory,
                .message = error.message(),
            });
            result.files.clear();
            return result;
        }

        std::sort(result.files.begin(), result.files.end(), [](const auto& left, const auto& right) {
            return left.filename().generic_string() < right.filename().generic_string();
        });

        if (result.files.empty()) {
            result.errors.push_back(GotchaInputOrderingError{
                .code = "empty_input_directory",
                .path = input_directory,
                .message = "no input files matched extension " + extension,
            });
        }

        return result;
    }

    [[nodiscard]] static GotchaInputOrderingResult DiscoverManifest(
        const std::filesystem::path& input_directory,
        const std::filesystem::path& manifest_path) {
        GotchaInputOrderingResult result{};
        if (!RequireDirectory(input_directory, result)) {
            return result;
        }

        std::error_code error{};
        if (!std::filesystem::exists(manifest_path, error)) {
            result.errors.push_back(GotchaInputOrderingError{
                .code = "manifest_not_found",
                .path = manifest_path,
                .message = "input manifest file does not exist",
            });
            return result;
        }
        if (error) {
            result.errors.push_back(GotchaInputOrderingError{
                .code = "manifest_status_error",
                .path = manifest_path,
                .message = error.message(),
            });
            return result;
        }

        nlohmann::json manifest{};
        try {
            std::ifstream stream{manifest_path};
            if (!stream) {
                result.errors.push_back(GotchaInputOrderingError{
                    .code = "manifest_open_failed",
                    .path = manifest_path,
                    .message = "input manifest file could not be opened",
                });
                return result;
            }
            stream >> manifest;
        } catch (const nlohmann::json::exception& ex) {
            result.errors.push_back(GotchaInputOrderingError{
                .code = "manifest_json_error",
                .path = manifest_path,
                .message = ex.what(),
            });
            return result;
        }

        if (!manifest.is_object()) {
            result.errors.push_back(GotchaInputOrderingError{
                .code = "manifest_schema_error",
                .path = manifest_path,
                .message = "manifest root must be a JSON object",
            });
            return result;
        }

        const auto schema_it = manifest.find("schema");
        const auto has_expected_schema =
            schema_it != manifest.end() && schema_it->is_string() &&
            schema_it->get<std::string>() == kSchemaName;
        if (!has_expected_schema) {
            result.errors.push_back(GotchaInputOrderingError{
                .code = "manifest_schema_error",
                .path = manifest_path,
                .message = "schema must be " + std::string{kSchemaName},
            });
        }

        const auto files_it = manifest.find("files");
        if (files_it == manifest.end() || !files_it->is_array()) {
            result.errors.push_back(GotchaInputOrderingError{
                .code = "manifest_schema_error",
                .path = manifest_path,
                .message = "files must be an array",
            });
            return ClearFilesOnError(result);
        }

        if (files_it->empty()) {
            result.errors.push_back(GotchaInputOrderingError{
                .code = "empty_manifest",
                .path = manifest_path,
                .message = "files must contain at least one entry",
            });
            return ClearFilesOnError(result);
        }

        std::set<std::string> seen_entries{};
        for (std::size_t index = 0; index < files_it->size(); ++index) {
            const auto entry_path = ReadManifestEntryPath((*files_it)[index], index, manifest_path, result);
            if (entry_path.empty()) {
                continue;
            }
            const auto normalized_entry = entry_path.lexically_normal();
            const auto normalized_entry_string = normalized_entry.generic_string();
            if (!seen_entries.insert(normalized_entry_string).second) {
                result.errors.push_back(GotchaInputOrderingError{
                    .code = "duplicate_manifest_entry",
                    .path = normalized_entry,
                    .message = "manifest file entry appears more than once",
                });
                continue;
            }
            if (normalized_entry.is_absolute() || ContainsParentTraversal(normalized_entry)) {
                result.errors.push_back(GotchaInputOrderingError{
                    .code = "manifest_entry_not_relative",
                    .path = normalized_entry,
                    .message = "manifest file entries must stay relative to the input directory",
                });
                continue;
            }

            const auto resolved_path = (input_directory / normalized_entry).lexically_normal();
            if (!std::filesystem::exists(resolved_path, error)) {
                result.errors.push_back(GotchaInputOrderingError{
                    .code = "manifest_entry_not_found",
                    .path = normalized_entry,
                    .message = "manifest file entry does not exist under the input directory",
                });
                continue;
            }
            if (error) {
                result.errors.push_back(GotchaInputOrderingError{
                    .code = "manifest_entry_status_error",
                    .path = normalized_entry,
                    .message = error.message(),
                });
                error.clear();
                continue;
            }
            result.files.push_back(resolved_path);
        }

        return ClearFilesOnError(result);
    }

    static constexpr const char* kSchemaName = "graphx.gotcha.input_manifest.v1";

private:
    [[nodiscard]] static bool RequireDirectory(
        const std::filesystem::path& input_directory,
        GotchaInputOrderingResult& result) {
        std::error_code error{};
        if (!std::filesystem::exists(input_directory, error)) {
            result.errors.push_back(GotchaInputOrderingError{
                .code = "input_directory_not_found",
                .path = input_directory,
                .message = "input directory does not exist",
            });
            return false;
        }
        if (error) {
            result.errors.push_back(GotchaInputOrderingError{
                .code = "input_directory_status_error",
                .path = input_directory,
                .message = error.message(),
            });
            return false;
        }
        if (!std::filesystem::is_directory(input_directory, error)) {
            result.errors.push_back(GotchaInputOrderingError{
                .code = "input_directory_not_directory",
                .path = input_directory,
                .message = "input path is not a directory",
            });
            return false;
        }
        if (error) {
            result.errors.push_back(GotchaInputOrderingError{
                .code = "input_directory_status_error",
                .path = input_directory,
                .message = error.message(),
            });
            return false;
        }
        return true;
    }

    [[nodiscard]] static std::filesystem::path ReadManifestEntryPath(
        const nlohmann::json& entry,
        std::size_t index,
        const std::filesystem::path& manifest_path,
        GotchaInputOrderingResult& result) {
        const auto path_for_error = manifest_path.string() + "#/files/" + std::to_string(index);
        if (!entry.is_object()) {
            result.errors.push_back(GotchaInputOrderingError{
                .code = "manifest_schema_error",
                .path = path_for_error,
                .message = "file entry must be an object",
            });
            return {};
        }
        const auto path_it = entry.find("path");
        if (path_it == entry.end() || !path_it->is_string() || path_it->get<std::string>().empty()) {
            result.errors.push_back(GotchaInputOrderingError{
                .code = "manifest_schema_error",
                .path = path_for_error,
                .message = "file entry path must be a non-empty string",
            });
            return {};
        }
        return std::filesystem::path{path_it->get<std::string>()};
    }

    [[nodiscard]] static bool ContainsParentTraversal(const std::filesystem::path& path) {
        for (const auto& component : path) {
            if (component == "..") {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] static GotchaInputOrderingResult ClearFilesOnError(
        GotchaInputOrderingResult result) {
        if (!result.ok()) {
            result.files.clear();
        }
        return result;
    }
};

} // namespace graphx::sar
