#include "dsp/configuration/FHSSConfigurationCli.hpp"
#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>

namespace graphx::dsp::configuration {

FHSSConfigurationCli::FHSSConfigurationCli(
    std::shared_ptr<ConfigurationStateMachine> state_machine
) : state_machine_(state_machine) {
}

FHSSConfigurationCli::CommandResult FHSSConfigurationCli::ExecuteCommand(
    int argc,
    const char* argv[]
) {
    if (argc < 2) {
        return ShowHelp();
    }

    std::string command = argv[1];

    if (command == "--help" || command == "-h") {
        return ShowHelp();
    }

    if (command == "--set-config") {
        std::vector<std::string> pairs;
        for (int i = 2; i < argc; ++i) {
            pairs.push_back(argv[i]);
        }
        return SetConfig(pairs);
    }

    if (command == "--config-patch") {
        if (argc < 3) {
            CommandResult result;
            result.exit_code = 1;
            result.error = "Missing FILE argument for --config-patch";
            return result;
        }
        
        std::string patch_file = argv[2];
        std::string if_match_etag = "";
        
        // Check for --if-match option
        for (int i = 3; i < argc; ++i) {
            if (std::string(argv[i]) == "--if-match" && i + 1 < argc) {
                if_match_etag = argv[i + 1];
                break;
            }
        }
        
        return ConfigPatch(patch_file, if_match_etag);
    }

    if (command == "--validate-config") {
        if (argc < 3) {
            CommandResult result;
            result.exit_code = 1;
            result.error = "Missing FILE argument for --validate-config";
            return result;
        }
        return ValidateConfig(argv[2]);
    }

    if (command == "--show-config") {
        bool show_effective = false;
        bool show_history = false;
        
        for (int i = 2; i < argc; ++i) {
            if (std::string(argv[i]) == "--effective") {
                show_effective = true;
            }
            if (std::string(argv[i]) == "--history") {
                show_history = true;
            }
        }
        
        return ShowConfig(show_effective, show_history);
    }

    CommandResult result;
    result.exit_code = 1;
    result.error = std::string("Unknown command: ") + command;
    return result;
}

FHSSConfigurationCli::CommandResult FHSSConfigurationCli::SetConfig(
    const std::vector<std::string>& key_value_pairs
) {
    CommandResult result;
    
    if (key_value_pairs.empty()) {
        result.exit_code = 1;
        result.error = "No KEY=VALUE pairs provided";
        return result;
    }

    // Parse KEY=VALUE pairs
    auto parsed = ParseKeyValuePairs(key_value_pairs);
    if (!parsed) {
        result.exit_code = 1;
        result.error = "Failed to parse KEY=VALUE pairs";
        return result;
    }

    try {
        // Create staged edit
        auto staged_result = state_machine_->CreateStagedEdit();
        if (!staged_result.success) {
            result.exit_code = 1;
            result.error = staged_result.error_message;
            return result;
        }

        auto handle = staged_result.value;

        // Apply updates to staged edit
        for (const auto& [key, value] : parsed.value().items()) {
            std::string value_str;
            if (value.is_string()) {
                value_str = value.get<std::string>();
            } else {
                value_str = value.dump();
            }
            
            auto update_result = state_machine_->UpdateStagedField(handle, key, value_str);
            if (!update_result.success) {
                result.exit_code = 1;
                result.error = "Failed to update field " + key + ": " + update_result.error_message;
                return result;
            }
        }

        // Validate staged edit
        auto validate_result = state_machine_->ValidateStagedEdit(handle);
        if (!validate_result.success || !validate_result.value.empty()) {
            result.exit_code = 1;
            result.error = FormatValidationErrors(validate_result.value);
            return result;
        }

        // Commit staged edit
        auto commit_result = state_machine_->CommitStagedEdit(handle);
        if (!commit_result.success) {
            result.exit_code = 1;
            result.error = commit_result.error_message;
            return result;
        }

        // Get updated config
        auto source = state_machine_->GetSourceConfiguration();
        nlohmann::json response = CreateSuccessResponse("Configuration updated successfully", source.to_json());
        
        result.exit_code = 0;
        result.output = response.dump(2);
        result.result_json = response;
        return result;
    } catch (const std::exception& e) {
        result.exit_code = 1;
        result.error = std::string("Exception: ") + e.what();
        return result;
    }
}

FHSSConfigurationCli::CommandResult FHSSConfigurationCli::ConfigPatch(
    const std::string& patch_file,
    const std::string& if_match_etag
) {
    CommandResult result;

    // Read patch file
    auto patch_json = ReadJsonFile(patch_file);
    if (!patch_json) {
        result.exit_code = 1;
        result.error = "Failed to read patch file: " + patch_file;
        return result;
    }

    try {
        // Create staged edit
        auto staged_result = state_machine_->CreateStagedEdit();
        if (!staged_result.success) {
            result.exit_code = 1;
            result.error = staged_result.error_message;
            return result;
        }

        auto handle = staged_result.value;

        // Convert JSON patch to key=value operations (simplified RFC 6902)
        std::vector<std::pair<std::string, std::string>> operations;
        
        if (patch_json.value().is_array()) {
            for (const auto& op : patch_json.value()) {
                if (op.contains("op") && op.contains("path")) {
                    std::string operation = op["op"].get<std::string>();
                    std::string path = op["path"].get<std::string>();
                    
                    // Remove leading '/' from path
                    if (!path.empty() && path[0] == '/') {
                        path = path.substr(1);
                    }
                    
                    if (operation == "replace" && op.contains("value")) {
                        std::string value_str;
                        if (op["value"].is_string()) {
                            value_str = op["value"].get<std::string>();
                        } else {
                            value_str = op["value"].dump();
                        }
                        operations.emplace_back(path, value_str);
                    }
                }
            }
        }

        // Apply patch operations
        for (const auto& [field, value] : operations) {
            auto update_result = state_machine_->UpdateStagedField(handle, field, value);
            if (!update_result.success) {
                result.exit_code = 1;
                result.error = "Failed to apply patch operation on field " + field + ": " + update_result.error_message;
                return result;
            }
        }

        // Validate staged edit
        auto validate_result = state_machine_->ValidateStagedEdit(handle);
        if (!validate_result.success || !validate_result.value.empty()) {
            result.exit_code = 1;
            result.error = FormatValidationErrors(validate_result.value);
            return result;
        }

        // Commit staged edit (with optional If-Match)
        auto commit_result = state_machine_->CommitStagedEdit(handle, if_match_etag);
        if (!commit_result.success) {
            result.exit_code = 1;
            result.error = commit_result.error_message;
            return result;
        }

        // Get updated config
        auto source = state_machine_->GetSourceConfiguration();
        nlohmann::json response = CreateSuccessResponse("Patch applied successfully", source.to_json());
        
        result.exit_code = 0;
        result.output = response.dump(2);
        result.result_json = response;
        return result;
    } catch (const std::exception& e) {
        result.exit_code = 1;
        result.error = std::string("Exception: ") + e.what();
        return result;
    }
}

FHSSConfigurationCli::CommandResult FHSSConfigurationCli::ValidateConfig(
    const std::string& config_file
) {
    CommandResult result;

    // Read config file
    auto config_json = ReadJsonFile(config_file);
    if (!config_json) {
        result.exit_code = 1;
        result.error = "Failed to read config file: " + config_file;
        return result;
    }

    try {
        // Create staged edit from config
        auto staged_result = state_machine_->CreateStagedEdit();
        if (!staged_result.success) {
            result.exit_code = 1;
            result.error = staged_result.error_message;
            return result;
        }

        auto handle = staged_result.value;

        // Apply config values to staged edit
        for (const auto& [key, value] : config_json.value().items()) {
            std::string value_str;
            if (value.is_string()) {
                value_str = value.get<std::string>();
            } else {
                value_str = value.dump();
            }
            
            auto update_result = state_machine_->UpdateStagedField(handle, key, value_str);
            if (!update_result.success) {
                result.exit_code = 1;
                result.error = "Failed to update field " + key + ": " + update_result.error_message;
                return result;
            }
        }

        // Validate (but don't commit)
        auto validate_result = state_machine_->ValidateStagedEdit(handle);
        
        nlohmann::json validation_response;
        validation_response["schema"] = "graphx.fhss_configuration.validation_result.v1";
        validation_response["is_valid"] = validate_result.value.empty();
        validation_response["validation_errors"] = nlohmann::json::array();
        
        for (const auto& error : validate_result.value) {
            nlohmann::json error_obj;
            error_obj["error_code"] = error.error_code;
            error_obj["field"] = error.field;
            error_obj["message"] = error.message;
            error_obj["expected_constraint"] = error.expected_constraint;
            error_obj["current_value"] = error.current_value;
            validation_response["validation_errors"].push_back(error_obj);
        }

        // Discard staged edit (we didn't commit)
        state_machine_->DiscardStagedEdit(handle);

        result.exit_code = validate_result.value.empty() ? 0 : 1;
        result.output = validation_response.dump(2);
        result.result_json = validation_response;
        return result;
    } catch (const std::exception& e) {
        result.exit_code = 1;
        result.error = std::string("Exception: ") + e.what();
        return result;
    }
}

FHSSConfigurationCli::CommandResult FHSSConfigurationCli::ShowConfig(
    bool show_effective,
    bool show_history
) {
    CommandResult result;

    try {
        nlohmann::json response;
        
        if (show_effective) {
            auto effective = state_machine_->GetEffectiveConfiguration();
            response = CreateSuccessResponse("Effective configuration", effective.to_json());
        } else {
            auto source = state_machine_->GetSourceConfiguration();
            response = CreateSuccessResponse("Source configuration", source.to_json());
        }

        if (show_history) {
            // Add history information (if available)
            response["history"] = nlohmann::json::array();
        }

        result.exit_code = 0;
        result.output = response.dump(2);
        result.result_json = response;
        return result;
    } catch (const std::exception& e) {
        result.exit_code = 1;
        result.error = std::string("Exception: ") + e.what();
        return result;
    }
}

FHSSConfigurationCli::CommandResult FHSSConfigurationCli::ShowHelp() {
    CommandResult result;
    result.exit_code = 0;
    result.output = R"(
FHSS Configuration CLI

Usage:
  graphx-config --set-config KEY1=VALUE1 [KEY2=VALUE2 ...]
  graphx-config --config-patch FILE [--if-match ETAG]
  graphx-config --validate-config FILE
  graphx-config --show-config [--effective] [--history]
  graphx-config --help

Commands:
  --set-config            Set configuration field(s) and commit
  --config-patch          Apply JSON Patch (RFC 6902) to configuration
  --validate-config       Validate configuration file without committing
  --show-config           Display current configuration
  --help                  Show this help message

Options:
  --effective             Show effective config with derived fields (with --show-config)
  --history               Include revision history (with --show-config)
  --if-match ETAG         Conditional commit with ETag check (with --config-patch)

Examples:
  graphx-config --set-config iq_center_frequency_hz=2400000000
  graphx-config --config-patch changes.json
  graphx-config --validate-config config.json
  graphx-config --show-config --effective
)";
    return result;
}

std::optional<nlohmann::json> FHSSConfigurationCli::ParseKeyValuePairs(
    const std::vector<std::string>& pairs
) {
    nlohmann::json result;
    
    for (const auto& pair : pairs) {
        size_t eq_pos = pair.find('=');
        if (eq_pos == std::string::npos || eq_pos == 0) {
            return std::nullopt;
        }
        
        std::string key = pair.substr(0, eq_pos);
        std::string value = pair.substr(eq_pos + 1);
        
        // Try to parse value as JSON (number, boolean, etc.)
        try {
            result[key] = nlohmann::json::parse(value);
        } catch (...) {
            // Fall back to string value
            result[key] = value;
        }
    }
    
    return result;
}

std::optional<nlohmann::json> FHSSConfigurationCli::ReadJsonFile(const std::string& file_path) {
    try {
        std::ifstream file(file_path);
        if (!file.is_open()) {
            return std::nullopt;
        }
        
        nlohmann::json json;
        file >> json;
        return json;
    } catch (...) {
        return std::nullopt;
    }
}

std::string FHSSConfigurationCli::FormatValidationErrors(
    const std::vector<ValidationError>& errors
) {
    if (errors.empty()) {
        return "Validation passed";
    }
    
    std::stringstream ss;
    ss << "Validation errors (" << errors.size() << "):\n";
    
    for (const auto& error : errors) {
        ss << "  [" << error.error_code << "] " << error.field << ": " << error.message << "\n";
    }
    
    return ss.str();
}

nlohmann::json FHSSConfigurationCli::CreateSuccessResponse(
    const std::string& message,
    const nlohmann::json& data
) {
    nlohmann::json response;
    response["schema"] = "graphx.fhss_configuration.v1";
    response["message"] = message;
    response["data"] = data;
    response["revision"] = 0;  // Placeholder
    response["etag"] = "Rev:0"; // Placeholder
    return response;
}

nlohmann::json FHSSConfigurationCli::CreateErrorResponse(
    const std::string& error,
    const std::string& detail
) {
    nlohmann::json response;
    response["type"] = "about:blank";
    response["status"] = 400;
    response["title"] = "Error";
    response["detail"] = detail.empty() ? error : detail;
    return response;
}

}  // namespace graphx::dsp::configuration
