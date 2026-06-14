#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

#include <nlohmann/json.hpp>

#if defined(GRAPHX_SAR_HAS_HDF5) && GRAPHX_SAR_HAS_HDF5
#include <hdf5.h>
#endif

namespace graphx::sar {

struct GotchaMatInspectionOptions {
    std::filesystem::path input_path{};
    std::filesystem::path output_directory{};
};

struct GotchaMatInspectionError {
    std::string code{};
    std::filesystem::path path{};
    std::string message{};
};

struct GotchaMatInspectionResult {
    bool success{false};
    std::string mat_format{"unknown"};
    std::string status{"not_run"};
    std::filesystem::path field_inventory_path{};
    std::filesystem::path conversion_assumptions_path{};
    std::vector<GotchaMatInspectionError> errors{};

    [[nodiscard]] bool ok() const noexcept {
        return success && errors.empty();
    }
};

struct GotchaFieldValidationError {
    std::string field_name{};
    std::string expected_type{};
    std::string message{};
    std::filesystem::path source_path{};
};

struct GotchaFieldValidationResult {
    bool ok{true};
    std::vector<GotchaFieldValidationError> missing_fields{};
    std::vector<GotchaFieldValidationError> type_errors{};

    [[nodiscard]] bool is_valid() const noexcept {
        return ok && missing_fields.empty() && type_errors.empty();
    }
};

class GotchaMatInspector {
public:
    [[nodiscard]] static GotchaMatInspectionResult Inspect(
        const GotchaMatInspectionOptions& options) {
        GotchaMatInspectionResult result{};
        result.field_inventory_path = options.output_directory / "field_inventory.json";
        result.conversion_assumptions_path = options.output_directory / "conversion_assumptions.json";

        std::error_code fs_error{};
        std::filesystem::create_directories(options.output_directory, fs_error);
        if (fs_error) {
            result.status = "output_directory_error";
            result.errors.push_back(GotchaMatInspectionError{
                .code = "output_directory_error",
                .path = options.output_directory,
                .message = fs_error.message(),
            });
            return result;
        }

        const auto format = DetectFormat(options.input_path, result);
        result.mat_format = format;

        nlohmann::json inventory = MakeInventorySkeleton(options.input_path, format);
        nlohmann::json assumptions = MakeAssumptionsSkeleton(options.input_path, format);

        if (!result.errors.empty()) {
            result.status = "input_error";
            CopyErrors(result.errors, inventory, assumptions);
            WriteReports(result, inventory, assumptions);
            return result;
        }

        if (format == "matlab_v7_3_hdf5") {
            InspectHdf5File(options.input_path, inventory, assumptions, result);
        } else if (format == "classic_or_non_hdf5_mat") {
            result.status = "unsupported_format";
            result.errors.push_back(GotchaMatInspectionError{
                .code = "classic_mat_unsupported",
                .path = options.input_path,
                .message = "classic/non-HDF5 MAT inspection is not implemented in this PR",
            });
            inventory["inspection_status"] = result.status;
            assumptions["classic_mat_policy"] = "unsupported_format_error";
            assumptions["assumptions"].push_back("classic_non_hdf5_mat_not_parsed");
        } else {
            result.status = "unknown_format";
            result.errors.push_back(GotchaMatInspectionError{
                .code = "unknown_mat_format",
                .path = options.input_path,
                .message = "input file is neither HDF5-signature MAT v7.3 nor recognized classic MAT",
            });
            inventory["inspection_status"] = result.status;
            assumptions["assumptions"].push_back("unknown_mat_format_not_parsed");
        }

        CopyErrors(result.errors, inventory, assumptions);
        result.success = WriteReports(result, inventory, assumptions);
        return result;
    }

    [[nodiscard]] static bool HasHdf5Signature(const std::filesystem::path& path) {
        std::ifstream stream{path, std::ios::binary};
        if (!stream) {
            return false;
        }
        std::array<unsigned char, 8> header{};
        stream.read(reinterpret_cast<char*>(header.data()), static_cast<std::streamsize>(header.size()));
        return stream.gcount() == static_cast<std::streamsize>(header.size()) && header == kHdf5Signature;
    }

    [[nodiscard]] static bool Hdf5SupportAvailable() noexcept {
#if defined(GRAPHX_SAR_HAS_HDF5) && GRAPHX_SAR_HAS_HDF5
        return true;
#else
        return false;
#endif
    }

    // Validate GOTCHA required fields according to docs/sar/gotcha_large_scene_data_description.md
    // These are the required fields that must be present in a GOTCHA sidecar JSON or MAT inspection.
    // See https://github.com/... for the field specification.
    [[nodiscard]] static GotchaFieldValidationResult ValidateRequiredFields(
        const std::filesystem::path& sidecar_path) {
        GotchaFieldValidationResult result{};

        std::ifstream stream{sidecar_path};
        if (!stream) {
            result.ok = false;
            result.missing_fields.push_back(GotchaFieldValidationError{
                .field_name = "sidecar_json",
                .expected_type = "file",
                .message = "sidecar JSON file could not be opened",
                .source_path = sidecar_path,
            });
            return result;
        }

        nlohmann::json json{};
        try {
            stream >> json;
        } catch (const nlohmann::json::exception& ex) {
            result.ok = false;
            result.type_errors.push_back(GotchaFieldValidationError{
                .field_name = "sidecar_json",
                .expected_type = "valid_json",
                .message = std::string{"sidecar JSON parsing failed: "} + ex.what(),
                .source_path = sidecar_path,
            });
            return result;
        }

        // Required GOTCHA fields per docs/sar/gotcha_large_scene_data_description.md:
        // Np, K, deltaF, minF, AntX, AntY, AntZ, R0, phdata
        const std::vector<std::pair<std::string, std::string>> required_fields{
            {"Np", "number"},
            {"K", "number"},
            {"deltaF", "number"},
            {"minF", "number"},
            {"AntX", "number"},
            {"AntY", "number"},
            {"AntZ", "number"},
            {"R0", "number"},
            {"phdata", "array|object|string"},
        };

        for (const auto& [field_name, expected_type] : required_fields) {
            if (!json.contains(field_name)) {
                result.ok = false;
                result.missing_fields.push_back(GotchaFieldValidationError{
                    .field_name = field_name,
                    .expected_type = expected_type,
                    .message = "required GOTCHA field is missing from sidecar JSON",
                    .source_path = sidecar_path,
                });
                continue;
            }

            const auto& value = json[field_name];

            // Type validation
            bool type_ok = false;
            if (expected_type == "number") {
                type_ok = value.is_number();
            } else if (expected_type == "array|object|string") {
                // phdata can be array, object, or string representation
                type_ok = value.is_array() || value.is_object() || value.is_string();
            }

            if (!type_ok) {
                result.ok = false;
                result.type_errors.push_back(GotchaFieldValidationError{
                    .field_name = field_name,
                    .expected_type = expected_type,
                    .message = "field has incorrect type (expected " + expected_type + ", got " +
                               value.type_name() + ")",
                    .source_path = sidecar_path,
                });
            }
        }

        return result;
    }

private:
    static constexpr std::array<unsigned char, 8> kHdf5Signature{
        0x89U, 0x48U, 0x44U, 0x46U, 0x0dU, 0x0aU, 0x1aU, 0x0aU};

    [[nodiscard]] static std::string DetectFormat(
        const std::filesystem::path& path,
        GotchaMatInspectionResult& result) {
        std::error_code fs_error{};
        if (!std::filesystem::exists(path, fs_error)) {
            result.errors.push_back(GotchaMatInspectionError{
                .code = "input_file_not_found",
                .path = path,
                .message = "input MAT file does not exist",
            });
            return "unknown";
        }
        if (fs_error) {
            result.errors.push_back(GotchaMatInspectionError{
                .code = "input_file_status_error",
                .path = path,
                .message = fs_error.message(),
            });
            return "unknown";
        }
        if (!std::filesystem::is_regular_file(path, fs_error)) {
            result.errors.push_back(GotchaMatInspectionError{
                .code = "input_path_not_file",
                .path = path,
                .message = "input path is not a regular file",
            });
            return "unknown";
        }
        if (fs_error) {
            result.errors.push_back(GotchaMatInspectionError{
                .code = "input_file_status_error",
                .path = path,
                .message = fs_error.message(),
            });
            return "unknown";
        }

        if (HasHdf5Signature(path)) {
            return "matlab_v7_3_hdf5";
        }

        std::ifstream stream{path, std::ios::binary};
        std::array<char, 128> header{};
        stream.read(header.data(), static_cast<std::streamsize>(header.size()));
        const std::string header_text{header.data(), static_cast<std::size_t>(stream.gcount())};
        if (header_text.find("MATLAB") != std::string::npos ||
            header_text.find("MAT-file") != std::string::npos) {
            return "classic_or_non_hdf5_mat";
        }

        return "unknown";
    }

    [[nodiscard]] static nlohmann::json MakeInventorySkeleton(
        const std::filesystem::path& input_path,
        const std::string& format) {
        return nlohmann::json{
            {"schema", "graphx.sar.mat_field_inventory.v1"},
            {"source_path", input_path.string()},
            {"mat_format", format},
            {"hdf5_signature_detected", format == "matlab_v7_3_hdf5"},
            {"hdf5_support", Hdf5SupportAvailable() ? "available" : "unavailable"},
            {"inspection_status", "pending"},
            {"fields", nlohmann::json::array()},
            {"errors", nlohmann::json::array()},
        };
    }

    [[nodiscard]] static nlohmann::json MakeAssumptionsSkeleton(
        const std::filesystem::path& input_path,
        const std::string& format) {
        return nlohmann::json{
            {"schema", "graphx.sar.conversion_assumptions.v1"},
            {"source_path", input_path.string()},
            {"mat_format", format},
            {"matlab_dependency", "not_used"},
            {"normalized_product_emitted", false},
            {"graphx_sar_normalized_emitted", false},
            {"crsd_emitted", false},
            {"classic_mat_policy", "deterministic_unsupported_error"},
            {"assumptions", nlohmann::json::array({
                                "inspection_only_no_normalized_product",
                                "matlab_not_used",
                                "no_intermediate_or_crsd_output",
                            })},
            {"errors", nlohmann::json::array()},
        };
    }

    static void InspectHdf5File(
        const std::filesystem::path& input_path,
        nlohmann::json& inventory,
        nlohmann::json& assumptions,
        GotchaMatInspectionResult& result) {
        inventory["fields"].push_back(nlohmann::json{
            {"path", "/"},
            {"key", "/"},
            {"kind", "hdf5_file"},
            {"shape", nlohmann::json::array()},
            {"dtype", "hdf5_container"},
        });

#if defined(GRAPHX_SAR_HAS_HDF5) && GRAPHX_SAR_HAS_HDF5
        const hid_t file_id = H5Fopen(input_path.string().c_str(), H5F_ACC_RDONLY, H5P_DEFAULT);
        if (file_id < 0) {
            result.status = "hdf5_open_failed";
            result.errors.push_back(GotchaMatInspectionError{
                .code = "hdf5_open_failed",
                .path = input_path,
                .message = "HDF5 support is available but the file could not be opened",
            });
            inventory["inspection_status"] = result.status;
            assumptions["assumptions"].push_back("hdf5_signature_detected_but_open_failed");
            return;
        }

        Hdf5VisitContext context{.inventory = &inventory};
        herr_t iterate_status = H5Literate(file_id, H5_INDEX_NAME, H5_ITER_INC, nullptr, &VisitHdf5Link, &context);
        H5Fclose(file_id);

        if (iterate_status < 0) {
            result.status = "hdf5_inventory_incomplete";
            result.errors.push_back(GotchaMatInspectionError{
                .code = "hdf5_inventory_incomplete",
                .path = input_path,
                .message = "HDF5 file opened but root field iteration failed",
            });
            inventory["inspection_status"] = result.status;
            assumptions["assumptions"].push_back("hdf5_root_iteration_failed");
            return;
        }

        result.status = "ok";
        inventory["inspection_status"] = result.status;
        assumptions["assumptions"].push_back("hdf5_root_inventory_emitted");
#else
        (void)input_path;
        result.status = "hdf5_reader_unavailable";
        inventory["inspection_status"] = result.status;
        assumptions["assumptions"].push_back("hdf5_signature_detected");
        assumptions["assumptions"].push_back("open_hdf5_support_not_configured");
#endif
    }

    static void CopyErrors(
        const std::vector<GotchaMatInspectionError>& errors,
        nlohmann::json& inventory,
        nlohmann::json& assumptions) {
        inventory["errors"] = nlohmann::json::array();
        assumptions["errors"] = nlohmann::json::array();
        for (const auto& error : errors) {
            const auto json_error = nlohmann::json{
                {"code", error.code},
                {"path", error.path.string()},
                {"message", error.message},
            };
            inventory["errors"].push_back(json_error);
            assumptions["errors"].push_back(json_error);
        }
    }

    static bool WriteReports(
        GotchaMatInspectionResult& result,
        const nlohmann::json& inventory,
        const nlohmann::json& assumptions) {
        try {
            {
                std::ofstream stream{result.field_inventory_path};
                if (!stream) {
                    result.errors.push_back(GotchaMatInspectionError{
                        .code = "field_inventory_write_failed",
                        .path = result.field_inventory_path,
                        .message = "field inventory report could not be opened for writing",
                    });
                    return false;
                }
                stream << inventory.dump(2) << '\n';
            }
            {
                std::ofstream stream{result.conversion_assumptions_path};
                if (!stream) {
                    result.errors.push_back(GotchaMatInspectionError{
                        .code = "conversion_assumptions_write_failed",
                        .path = result.conversion_assumptions_path,
                        .message = "conversion assumptions report could not be opened for writing",
                    });
                    return false;
                }
                stream << assumptions.dump(2) << '\n';
            }
        } catch (const std::exception& ex) {
            result.errors.push_back(GotchaMatInspectionError{
                .code = "inspection_report_write_exception",
                .path = result.field_inventory_path.parent_path(),
                .message = ex.what(),
            });
            return false;
        }

        return true;
    }

#if defined(GRAPHX_SAR_HAS_HDF5) && GRAPHX_SAR_HAS_HDF5
    struct Hdf5VisitContext {
        nlohmann::json* inventory{};
    };

    static herr_t VisitHdf5Link(
        hid_t location_id,
        const char* name,
        const H5L_info_t*,
        void* user_data) {
        auto* context = static_cast<Hdf5VisitContext*>(user_data);
        if (context == nullptr || context->inventory == nullptr || name == nullptr) {
            return -1;
        }

        H5O_info_t object_info{};
        if (H5Oget_info_by_name(location_id, name, &object_info, H5P_DEFAULT) < 0) {
            return -1;
        }

        nlohmann::json field = {
            {"path", std::string{"/"} + name},
            {"key", std::string{name}},
            {"shape", nlohmann::json::array()},
            {"dtype", "unknown"},
        };

        if (object_info.type == H5O_TYPE_GROUP) {
            field["kind"] = "group";
            field["dtype"] = "hdf5_group";
        } else if (object_info.type == H5O_TYPE_DATASET) {
            field["kind"] = "dataset";
            PopulateDatasetMetadata(location_id, name, field);
        } else {
            field["kind"] = "unknown";
        }

        (*context->inventory)["fields"].push_back(field);
        return 0;
    }

    static void PopulateDatasetMetadata(hid_t location_id, const char* name, nlohmann::json& field) {
        const hid_t dataset_id = H5Dopen2(location_id, name, H5P_DEFAULT);
        if (dataset_id < 0) {
            field["dtype"] = "unavailable";
            return;
        }

        const hid_t dataspace_id = H5Dget_space(dataset_id);
        if (dataspace_id >= 0) {
            const int rank = H5Sget_simple_extent_ndims(dataspace_id);
            if (rank > 0) {
                std::vector<hsize_t> dimensions(static_cast<std::size_t>(rank), 0);
                if (H5Sget_simple_extent_dims(dataspace_id, dimensions.data(), nullptr) >= 0) {
                    for (const auto dimension : dimensions) {
                        field["shape"].push_back(static_cast<std::uint64_t>(dimension));
                    }
                }
            }
            H5Sclose(dataspace_id);
        }

        const hid_t type_id = H5Dget_type(dataset_id);
        if (type_id >= 0) {
            field["dtype"] = DescribeHdf5Type(type_id);
            H5Tclose(type_id);
        }

        H5Dclose(dataset_id);
    }

    [[nodiscard]] static std::string DescribeHdf5Type(hid_t type_id) {
        const auto type_class = H5Tget_class(type_id);
        const auto type_size = H5Tget_size(type_id);
        if (type_class == H5T_INTEGER) {
            return "integer:" + std::to_string(type_size);
        }
        if (type_class == H5T_FLOAT) {
            return "float:" + std::to_string(type_size);
        }
        if (type_class == H5T_STRING) {
            return "string:" + std::to_string(type_size);
        }
        if (type_class == H5T_COMPOUND) {
            return "compound:" + std::to_string(type_size);
        }
        if (type_class == H5T_ARRAY) {
            return "array:" + std::to_string(type_size);
        }
        return "unknown:" + std::to_string(type_size);
    }
#endif
};

} // namespace graphx::sar
