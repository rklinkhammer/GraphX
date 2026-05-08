/**
 * @file ConfigLoader.hpp
 * @brief Configuration loading with type-safe expected<> error handling (Phase 5b)
 * @author Robert Klinkhammer
 * @date May 7, 2026
 *
 * Type-safe configuration loading using JsonUtilities and expected<>.
 * Replaces try-catch blocks with composable error handling.
 *
 * ## Pattern
 *
 * @code
 *   // Load configuration with error handling
 *   auto config = LoadAppConfig("config.json");
 *   if (!config) {
 *       LOG_ERROR(app::error::ErrorMessage(config.error()));
 *       return false;
 *   }
 *
 *   // Use configuration
 *   auto port = ExtractField<int>(config.value(), "port");
 *   if (!port) {
 *       LOG_ERROR("Invalid port in configuration");
 *       return false;
 *   }
 * @endcode
 */

#pragma once

#include <expected>
#include <nlohmann/json.hpp>
#include "app/JsonUtilities.hpp"
#include "app/Errors.hpp"

namespace app::config {

using json = nlohmann::json;

/**
 * @struct WindowHeightConfig
 * @brief Dashboard window height configuration
 *
 * Percentages must sum to 100 for valid layout.
 */
struct WindowHeightConfig {
    int metrics_height_percent = 68;
    int logging_height_percent = 15;
    int command_height_percent = 15;
    int status_height_percent = 2;

    /// Validate that percentages sum to 100
    [[nodiscard]] bool Validate() const noexcept {
        return (metrics_height_percent + logging_height_percent +
                command_height_percent + status_height_percent) == 100;
    }

    /// Get error message if validation fails
    [[nodiscard]] std::string GetValidationError() const noexcept {
        int sum = metrics_height_percent + logging_height_percent +
                  command_height_percent + status_height_percent;
        return std::format("Window heights sum to {}%, expected 100%", sum);
    }
};

/**
 * @brief Load window height configuration from JSON
 * @param json_obj JSON object containing configuration
 * @return Expected<WindowHeightConfig, ConfigError>
 */
[[nodiscard]] std::expected<WindowHeightConfig, error::ConfigError>
LoadWindowHeightConfig(const json& json_obj) noexcept;

/**
 * @brief Load application configuration from file
 * @param filepath Path to configuration JSON file
 * @return Expected<json, ConfigError> - parsed configuration or error
 *
 * Loads and validates JSON configuration file. Returns either:
 * - Parsed JSON configuration object
 * - Specific error code (file not found, invalid JSON, etc.)
 *
 * @code
 *   auto config = LoadConfig("dashboard.json");
 *   if (!config) {
 *       std::cerr << app::error::ErrorMessage(config.error()) << std::endl;
 *       return false;
 *   }
 *   // Process config.value()
 * @endcode
 */
[[nodiscard]] std::expected<json, error::ConfigError>
LoadConfig(std::string_view filepath) noexcept;

/**
 * @brief Save configuration to file with validation
 * @param filepath Path where to save configuration
 * @param config Configuration object to save
 * @return Expected<void, ConfigError> - success or error
 */
[[nodiscard]] std::expected<void, error::ConfigError>
SaveConfig(std::string_view filepath, const json& config) noexcept;

/**
 * @brief Validate configuration structure
 * @param config Configuration to validate
 * @return Error code (ConfigError::Unknown = success)
 *
 * Validates that all required fields are present and have correct types.
 */
[[nodiscard]] error::ConfigError
ValidateConfig(const json& config) noexcept;

/**
 * @brief Get configuration field with automatic type conversion
 * @tparam T Target type
 * @param config Configuration object
 * @param key_path Dot-separated path (e.g., "window.height")
 * @param default_value Default if missing
 * @return Expected<T, ConfigError> - value or error
 *
 * Supports nested keys with dot notation:
 * @code
 *   auto height = GetConfigField<int>(config, "window.height", 24);
 *   if (height) {
 *       use(height.value());
 *   }
 * @endcode
 */
template<typename T>
[[nodiscard]] std::expected<T, error::ConfigError>
GetConfigField(const json& config, std::string_view key_path,
              const T& default_value) noexcept;

}  // namespace app::config
