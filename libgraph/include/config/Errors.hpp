// SPDX-License-Identifier: MIT

/**
 * @file Errors.hpp
 * @brief Errors Graph runtime support.
 *
 * @details Provides configuration parsing, validation, and JSON utility support. This file is documented for Doxygen so public APIs and test support surfaces can be browsed consistently.
 */
#pragma once

#include <string>
#include <string_view>
#include <utility>
#include "core/FormatUtilities.hpp"

namespace app::error {

// ============================================================================
// JSON Parsing Errors
// ============================================================================

/**
 * @enum JsonParseError
 * @brief Errors related to JSON parsing and validation
 *
 * Used with std::expected<nlohmann::json, JsonParseError> for safe JSON handling.
 *
 * @see ParseJsonSafe() in JsonUtilities.hpp
 */
/**
 * @enum JsonParseError
 * @brief JSON Parse Error values.
 *
 * @details Enumerates stable options or status values used by the libgraph API. Keep additions explicit so configuration, diagnostics, and generated documentation remain readable.
 */
enum class JsonParseError {
    /// JSON is syntactically invalid
    InvalidSyntax = 1,
    
    /// JSON contains unexpected structure (object instead of array, etc.)
    UnexpectedStructure = 2,
    
    /// JSON is missing required fields
    MissingRequiredField = 3,
    
    /// JSON field has wrong type (string instead of number, etc.)
    TypeMismatch = 4,
    
    /// Unknown or unrecoverable parsing error
    Unknown = 99,
};

/**
 * @brief Convert JsonParseError to human-readable string
 * @param error Error code
 * @return Error description
 *
 * @code
 *   auto result = ParseJsonSafe(config_str);
 *   if (!result) {
 *       std::cerr << ErrorMessage(result.error()) << std::endl;
 *   }
 * @endcode
 */
[[nodiscard]] inline std::string ErrorMessage(JsonParseError error) {
    switch (error) {
        case JsonParseError::InvalidSyntax:
            return "JSON syntax error (invalid characters or structure)";
        case JsonParseError::UnexpectedStructure:
            return "JSON has unexpected structure (type mismatch)";
        case JsonParseError::MissingRequiredField:
            return "JSON missing required field";
        case JsonParseError::TypeMismatch:
            return "JSON field has incorrect type";
        case JsonParseError::Unknown:
            return "Unknown JSON parsing error";
        default:
            return "Unrecognized JSON error";
    }
}

// ============================================================================
// Plugin Loading Errors
// ============================================================================

/**
 * @enum PluginLoadError
 * @brief Errors related to dynamic plugin loading
 *
 * Used with std::expected<Plugin, PluginLoadError> for safe plugin loading.
 *
 * @see LoadPluginSafe() in PluginLoader.hpp
 */
/**
 * @enum PluginLoadError
 * @brief Plugin Load Error values.
 *
 * @details Enumerates stable options or status values used by the libgraph API. Keep additions explicit so configuration, diagnostics, and generated documentation remain readable.
 */
enum class PluginLoadError {
    /// Plugin file not found at specified path
    FileNotFound = 1,
    
    /// File exists but is not a valid plugin library
    InvalidFormat = 2,
    
    /// Plugin doesn't export required entry points
    MissingSymbol = 3,
    
    /// Plugin with this ID already loaded
    AlreadyLoaded = 4,
    
    /// Plugin version incompatible with dashboard
    VersionMismatch = 5,
    
    /// System error loading dynamic library (dlopen failed)
    SystemError = 6,
    
    /// Plugin initialization failed
    InitializationFailed = 7,
    
    /// Unknown plugin loading error
    Unknown = 99,
};

/**
 * @brief Convert PluginLoadError to human-readable string
 * @param error Error code
 * @return Error description
 */
[[nodiscard]] inline std::string ErrorMessage(PluginLoadError error) {
    switch (error) {
        case PluginLoadError::FileNotFound:
            return "Plugin file not found";
        case PluginLoadError::InvalidFormat:
            return "Plugin file is not a valid dynamic library";
        case PluginLoadError::MissingSymbol:
            return "Plugin missing required entry point symbols";
        case PluginLoadError::AlreadyLoaded:
            return "Plugin with this ID already loaded";
        case PluginLoadError::VersionMismatch:
            return "Plugin version incompatible with dashboard";
        case PluginLoadError::SystemError:
            return "System error loading plugin library";
        case PluginLoadError::InitializationFailed:
            return "Plugin initialization failed";
        case PluginLoadError::Unknown:
            return "Unknown plugin loading error";
        default:
            return "Unrecognized plugin error";
    }
}

// ============================================================================
// Configuration Errors
// ============================================================================

/**
 * @enum ConfigError
 * @brief Errors related to configuration parsing and validation
 *
 * Used with std::expected<Config, ConfigError> for type-safe config loading.
 *
 * @see LoadConfigFromFile() in Config.hpp
 */
/**
 * @enum ConfigError
 * @brief Config Error values.
 *
 * @details Enumerates stable options or status values used by the libgraph API. Keep additions explicit so configuration, diagnostics, and generated documentation remain readable.
 */
enum class ConfigError {
    /// Configuration file not found
    FileNotFound = 1,
    
    /// Configuration file is not readable
    PermissionDenied = 2,
    
    /// Configuration file is not valid JSON
    InvalidFormat = 3,
    
    /// Configuration value out of valid range
    OutOfRange = 4,
    
    /// Required configuration field is missing
    MissingRequired = 5,
    
    /// Configuration validation failed (custom validation logic)
    ValidationFailed = 6,
    
    /// Configuration field has incompatible type
    TypeMismatch = 7,
    
    /// Unknown configuration error
    Unknown = 99,
};

/**
 * @brief Convert ConfigError to human-readable string
 * @param error Error code
 * @return Error description
 */
[[nodiscard]] inline std::string ErrorMessage(ConfigError error) {
    switch (error) {
        case ConfigError::FileNotFound:
            return "Configuration file not found";
        case ConfigError::PermissionDenied:
            return "Configuration file permission denied";
        case ConfigError::InvalidFormat:
            return "Configuration file is not valid JSON";
        case ConfigError::OutOfRange:
            return "Configuration value out of valid range";
        case ConfigError::MissingRequired:
            return "Required configuration field is missing";
        case ConfigError::ValidationFailed:
            return "Configuration validation failed";
        case ConfigError::TypeMismatch:
            return "Configuration field has incompatible type";
        case ConfigError::Unknown:
            return "Unknown configuration error";
        default:
            return "Unrecognized configuration error";
    }
}

// ============================================================================
// Deserialization Errors
// ============================================================================

/**
 * @enum DeserializationError
 * @brief Errors related to JSON deserialization into C++ types
 *
 * Used with std::expected<T, DeserializationError> for type-safe deserialization.
 *
 * @see Deserialize<T>() in JsonDeserialization.hpp
 */
/**
 * @enum DeserializationError
 * @brief Deserialization Error values.
 *
 * @details Enumerates stable options or status values used by the libgraph API. Keep additions explicit so configuration, diagnostics, and generated documentation remain readable.
 */
enum class DeserializationError {
    /// JSON input is not valid JSON
    InvalidJson = 1,
    
    /// JSON type doesn't match C++ type (e.g., string for int)
    TypeMismatch = 2,
    
    /// Required field in JSON is missing
    MissingRequiredField = 3,
    
    /// Field value is invalid (e.g., negative when positive required)
    InvalidValue = 4,
    
    /// JSON has unexpected fields
    UnexpectedField = 5,
    
    /// Value conversion failed (e.g., string to int)
    ConversionFailed = 6,
    
    /// Value violates constraint (e.g., min/max bounds)
    ConstraintViolation = 7,
    
    /// Unknown deserialization error
    Unknown = 99,
};

/**
 * @brief Convert DeserializationError to human-readable string
 * @param error Error code
 * @return Error description
 */
[[nodiscard]] inline std::string ErrorMessage(DeserializationError error) {
    switch (error) {
        case DeserializationError::InvalidJson:
            return "JSON input is not valid";
        case DeserializationError::TypeMismatch:
            return "JSON type doesn't match expected C++ type";
        case DeserializationError::MissingRequiredField:
            return "Required field is missing in JSON";
        case DeserializationError::InvalidValue:
            return "Field value is invalid";
        case DeserializationError::UnexpectedField:
            return "JSON contains unexpected field";
        case DeserializationError::ConversionFailed:
            return "Value conversion failed";
        case DeserializationError::ConstraintViolation:
            return "Value violates defined constraints";
        case DeserializationError::Unknown:
            return "Unknown deserialization error";
        default:
            return "Unrecognized deserialization error";
    }
}

// ============================================================================
// Graph Execution Errors
// ============================================================================

/**
 * @enum GraphExecutionError
 * @brief Errors related to graph execution
 *
 * Used with std::expected<ExecutionResult, GraphExecutionError> for safe execution.
 *
 * @see GraphExecutor::Execute() in GraphExecutor.hpp
 */
/**
 * @enum GraphExecutionError
 * @brief Graph Execution Error values.
 *
 * @details Enumerates stable options or status values used by the libgraph API. Keep additions explicit so configuration, diagnostics, and generated documentation remain readable.
 */
enum class GraphExecutionError {
    /// Graph is not initialized
    NotInitialized = 1,
    
    /// Graph is already running
    AlreadyRunning = 2,
    
    /// Required node is missing
    MissingNode = 3,
    
    /// Node dependency cycle detected
    CyclicDependency = 4,
    
    /// Node failed to initialize
    NodeInitFailed = 5,
    
    /// Execution was stopped/cancelled
    Stopped = 6,

    /// Execution policy hook failed
    PolicyFailed = 7,

    /// Graph manager lifecycle operation failed
    GraphManagerFailed = 8,

    /// Lifecycle method was called in an invalid executor state
    InvalidState = 9,

    /// Graph executor builder configuration is invalid
    ConfigurationInvalid = 10,

    /// Graph executor builder failed to create an executor
    BuilderFailed = 11,
    
    /// Unknown execution error
    Unknown = 99,
};

/**
 * @brief Rich graph execution failure for std::expected error channels.
 *
 * The code is stable and suitable for branching; message preserves the lifecycle
 * context that legacy ExecutionResult callers used for diagnostics.
 */
/**
 * @struct GraphExecutionFailure
 * @brief Graph Execution Failure data record.
 *
 * @details Groups related fields passed through GraphX runtime, DSP, or GPU boundaries. The type is intentionally documented as a value object so callers understand ownership, lifetime, and validation expectations.
 */
struct GraphExecutionFailure {
    GraphExecutionError code = GraphExecutionError::Unknown;
    std::string message;
};

/**
 * @brief Convert GraphExecutionError to human-readable string
 * @param error Error code
 * @return Error description
 */
[[nodiscard]] inline std::string ErrorMessage(GraphExecutionError error) {
    switch (error) {
        case GraphExecutionError::NotInitialized:
            return "Graph is not initialized";
        case GraphExecutionError::AlreadyRunning:
            return "Graph is already running";
        case GraphExecutionError::MissingNode:
            return "Required graph node is missing";
        case GraphExecutionError::CyclicDependency:
            return "Graph has cyclic dependencies";
        case GraphExecutionError::NodeInitFailed:
            return "Node initialization failed";
        case GraphExecutionError::Stopped:
            return "Graph execution was stopped";
        case GraphExecutionError::PolicyFailed:
            return "Execution policy hook failed";
        case GraphExecutionError::GraphManagerFailed:
            return "Graph manager lifecycle operation failed";
        case GraphExecutionError::InvalidState:
            return "Executor lifecycle method called in invalid state";
        case GraphExecutionError::ConfigurationInvalid:
            return "Graph executor builder configuration is invalid";
        case GraphExecutionError::BuilderFailed:
            return "Graph executor builder failed";
        case GraphExecutionError::Unknown:
            return "Unknown graph execution error";
        default:
            return "Unrecognized graph error";
    }
}

[[nodiscard]] inline GraphExecutionFailure MakeGraphExecutionFailure(
    GraphExecutionError code,
    std::string message = {}) {
    if (message.empty()) {
        message = ErrorMessage(code);
    }
    return GraphExecutionFailure{.code = code, .message = std::move(message)};
}

// ============================================================================
// Metrics Errors
// ============================================================================

/**
 * @enum MetricsError
 * @brief Errors related to metrics collection and reporting
 *
 * Used with std::expected<Metrics, MetricsError> for safe metrics operations.
 *
 * @see MetricsCapability in MetricsCapability.hpp
 */
/**
 * @enum MetricsError
 * @brief Metrics Error values.
 *
 * @details Enumerates stable options or status values used by the libgraph API. Keep additions explicit so configuration, diagnostics, and generated documentation remain readable.
 */
enum class MetricsError {
    /// Requested metric not found
    MetricNotFound = 1,
    
    /// Node not found in metrics collection
    NodeNotFound = 2,
    
    /// Metrics callback is not registered
    CallbackNotRegistered = 3,
    
    /// Metrics collection is disabled
    Disabled = 4,
    
    /// Unknown metrics error
    Unknown = 99,
};

/**
 * @brief Convert MetricsError to human-readable string
 * @param error Error code
 * @return Error description
 */
[[nodiscard]] inline std::string ErrorMessage(MetricsError error) {
    switch (error) {
        case MetricsError::MetricNotFound:
            return "Requested metric not found";
        case MetricsError::NodeNotFound:
            return "Node not found in metrics collection";
        case MetricsError::CallbackNotRegistered:
            return "Metrics callback is not registered";
        case MetricsError::Disabled:
            return "Metrics collection is disabled";
        case MetricsError::Unknown:
            return "Unknown metrics error";
        default:
            return "Unrecognized metrics error";
    }
}

// ============================================================================
// Generic Conversion Function
// ============================================================================

/**
 * @brief Generic error-to-string conversion (for generics and logging)
 * @param error_code Numeric error code (from error enum)
 * @param context Error context/category
 * @return Formatted error message
 *
 * Useful when error code is stored as int and context is known separately.
 *
 * @code
 *   std::string msg = ErrorMessageWithContext(
 *       static_cast<int>(JsonParseError::InvalidSyntax),
 *       "JSON parsing"
 *   );
 * @endcode
 */
[[nodiscard]] inline std::string ErrorMessageWithContext(
    int error_code,
    std::string_view context) {
    return format::FormatError(context, "Error code: " + std::to_string(error_code));
}

}  // namespace app::error
