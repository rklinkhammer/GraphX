// SPDX-License-Identifier: MIT

/**
 * @file ConfigLoader.hpp
 * @brief Config Loader Graph runtime support.
 *
 * @details Provides configuration parsing, validation, and JSON utility support. This file is documented for Doxygen so public APIs and test support surfaces can be browsed consistently.
 */
#pragma once

#include <expected>
#include <nlohmann/json.hpp>
#include "JsonUtilities.hpp"
#include "config/JsonUtilities.hpp"

namespace app::config {

using json = nlohmann::json;

/**
 * @struct WindowHeightConfig
 * @brief Dashboard window height configuration
 *
 * Percentages must sum to 100 for valid layout.
 */
/**
 * @struct WindowHeightConfig
 * @brief Window Height Config data record.
 *
 * @details Groups related fields passed through GraphX runtime, DSP, or GPU boundaries. The type is intentionally documented as a value object so callers understand ownership, lifetime, and validation expectations.
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
