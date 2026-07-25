#pragma once

#include "dsp/configuration/ConfigurationStateMachine.hpp"
#include "dsp/configuration/FHSSConfigurationDeriver.hpp"
#include "dsp/configuration/FHSSCrossNodeValidator.hpp"
#include <string>
#include <vector>
#include <memory>
#include <optional>
#include <nlohmann/json.hpp>

namespace graphx::dsp::configuration {

// Using declarations for types from dsp::configuration
using ::dsp::configuration::ConfigurationStateMachine;
using ::dsp::configuration::SourceConfiguration;
using ::dsp::configuration::EffectiveConfiguration;
using ::dsp::configuration::ValidationError;
using ::dsp::configuration::FHSSCrossNodeValidator;

/**
 * @brief CLI command handler for FHSS Configuration Management.
 *
 * Provides command-line interface to ConfigurationStateMachine:
 * - --set-config KEY=VALUE [KEY=VALUE ...]        - Set source configuration field(s)
 * - --config-patch FILE [--if-match ETAG]         - Apply JSON Patch to configuration
 * - --validate-config FILE                        - Validate configuration file without committing
 * - --show-config [--effective] [--history]       - Display current configuration
 *
 * Example usage:
 *   graphx-config --set-config iq_center_frequency_hz=2400000000 occupied_bandwidth_hz=40000000
 *   graphx-config --config-patch changes.json
 *   graphx-config --show-config --effective
 *   graphx-config --validate-config config.json
 */
class FHSSConfigurationCli {
public:
    struct CommandResult {
        int exit_code = 0;
        std::string output;
        std::string error;
        nlohmann::json result_json = nlohmann::json::object();
    };

    /**
     * @brief Construct CLI handler wrapping a configuration state machine.
     *
     * @param state_machine Shared pointer to ConfigurationStateMachine instance
     */
    explicit FHSSConfigurationCli(
        std::shared_ptr<ConfigurationStateMachine> state_machine
    );

    /**
     * @brief Parse and execute command.
     *
     * @param argc Argument count (same as main())
     * @param argv Argument vector (same as main())
     * @return Command result with exit code and output
     */
    CommandResult ExecuteCommand(int argc, const char* argv[]);

    /**
     * @brief Execute --set-config command.
     *
     * @param key_value_pairs Vector of "KEY=VALUE" strings
     * @return Command result
     */
    CommandResult SetConfig(const std::vector<std::string>& key_value_pairs);

    /**
     * @brief Execute --config-patch command.
     *
     * @param patch_file Path to JSON Patch file
     * @param if_match_etag Optional ETag for conditional commit
     * @return Command result
     */
    CommandResult ConfigPatch(const std::string& patch_file, const std::string& if_match_etag = "");

    /**
     * @brief Execute --validate-config command.
     *
     * @param config_file Path to configuration JSON file
     * @return Command result (with validation errors if any)
     */
    CommandResult ValidateConfig(const std::string& config_file);

    /**
     * @brief Execute --show-config command.
     *
     * @param show_effective If true, show effective config + derived fields; otherwise source config
     * @param show_history If true, also include revision history
     * @return Command result with current configuration
     */
    CommandResult ShowConfig(bool show_effective = false, bool show_history = false);

    /**
     * @brief Execute --help command.
     *
     * @return Command result with usage information
     */
    CommandResult ShowHelp();

private:
    std::shared_ptr<ConfigurationStateMachine> state_machine_;

    /// Internal helper: Parse KEY=VALUE pairs
    static std::optional<nlohmann::json> ParseKeyValuePairs(
        const std::vector<std::string>& pairs
    );

    /// Internal helper: Read JSON file
    static std::optional<nlohmann::json> ReadJsonFile(const std::string& file_path);

    /// Internal helper: Format error message with RFC 9457 details
    static std::string FormatValidationErrors(
        const std::vector<ValidationError>& errors
    );

    /// Internal helper: Create success response JSON
    static nlohmann::json CreateSuccessResponse(const std::string& message, const nlohmann::json& data = nlohmann::json::object());

    /// Internal helper: Create error response JSON
    static nlohmann::json CreateErrorResponse(const std::string& error, const std::string& detail = "");
};

}  // namespace graphx::dsp::configuration
