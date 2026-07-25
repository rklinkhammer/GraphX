#pragma once

#include "dsp/configuration/ConfigurationStateMachine.hpp"
#include <string>
#include <vector>
#include <memory>
#include <nlohmann/json.hpp>

namespace graphx::dsp::configuration {

// Using declarations for types from dsp::configuration
using ::dsp::configuration::ConfigurationStateMachine;
using ::dsp::configuration::SourceConfiguration;
using ::dsp::configuration::EffectiveConfiguration;

/**
 * @brief HTTP API wrapper for FHSS Configuration State Machine.
 *
 * Provides REST endpoints for configuration management:
 * - GET    /api/v2/fhss/config              - Retrieve source configuration
 * - GET    /api/v2/fhss/config/effective    - Retrieve effective configuration + derived fields
 * - GET    /api/v2/fhss/config/history      - Retrieve revision history
 * - POST   /api/v2/fhss/config/staged       - Create staged edit
 * - PATCH  /api/v2/fhss/config/staged/{id}  - Update staged edit field
 * - POST   /api/v2/fhss/config/validate     - Validate staged edit
 * - POST   /api/v2/fhss/config/commit       - Commit staged edit (with If-Match ETag)
 * - DELETE /api/v2/fhss/config/staged/{id}  - Discard staged edit
 * - POST   /api/v2/fhss/config/undo         - Undo last committed edit
 * - POST   /api/v2/fhss/config/redo         - Redo last undone edit
 *
 * All responses use RFC 9457 Problem Details format for errors.
 * Deterministic JSON with sorted keys for byte-identical output validation.
 */
class FHSSConfigurationHttpServer {
public:
    struct Options {
        /// Host address (loopback-only: 127.x.x.x or ::1)
        std::string host = "127.0.0.1";
        
        /// Listen port
        uint16_t port = 8766;
        
        /// Max request body size (default: 16 MiB)
        uint32_t max_body_bytes = 16 * 1024 * 1024;
        
        /// Request timeout (seconds)
        uint32_t request_timeout_seconds = 30;
    };

    /**
     * @brief HTTP request/response handler signature (matches DashboardHttpServer pattern).
     */
    using RequestHandler = std::function<bool(
        std::string_view method,
        std::string_view path,
        const std::vector<std::pair<std::string, std::string>>& headers,
        std::string_view body,
        int& response_status,
        std::vector<std::pair<std::string, std::string>>& response_headers,
        std::string& response_body
    )>;

    /**
     * @brief Construct HTTP server wrapping a configuration state machine.
     *
     * @param state_machine Shared pointer to ConfigurationStateMachine instance
     * @param options Server configuration
     */
    explicit FHSSConfigurationHttpServer(
        std::shared_ptr<ConfigurationStateMachine> state_machine,
        const Options& options
    );

    /**
     * @brief Get request handler lambda compatible with DashboardHttpServer.
     *
     * Returns a bound lambda that can be registered with RegisterHandler().
     * The lambda maintains ownership of the state_machine and handles all
     * HTTP request routing internally.
     *
     * @return Handler lambda compatible with DashboardHttpServer::RegisterHandler()
     */
    RequestHandler GetRequestHandler();

private:
    std::shared_ptr<ConfigurationStateMachine> state_machine_;
    Options options_;

    /// Internal helper: Serialize with sorted keys for determinism
    static nlohmann::json SerializeWithSortedKeys(const nlohmann::json& j);

    /// Internal helper: Create RFC 9457 Problem Details error response
    static nlohmann::json CreateProblemDetails(
        int status,
        const std::string& code,
        const std::string& message,
        const std::string& detail = ""
    );

    /// Internal helper: Create successful JSON response
    nlohmann::json CreateSuccessResponse(const nlohmann::json& data);

    /// Route dispatcher
    bool HandleRequest(
        std::string_view method,
        std::string_view path,
        const std::vector<std::pair<std::string, std::string>>& headers,
        std::string_view body,
        int& response_status,
        std::vector<std::pair<std::string, std::string>>& response_headers,
        std::string& response_body
    );

    /// Route handlers
    bool HandleGetConfig(int& status, std::string& body, std::vector<std::pair<std::string, std::string>>& response_headers);
    bool HandleGetEffectiveConfig(int& status, std::string& body, std::vector<std::pair<std::string, std::string>>& response_headers);
    bool HandleGetHistory(int& status, std::string& body, std::vector<std::pair<std::string, std::string>>& response_headers);
    bool HandleCreateStagedEdit(const nlohmann::json& request_body, int& status, std::string& body);
    bool HandleUpdateStagedField(const std::string& id, const std::string& field, const nlohmann::json& value, int& status, std::string& body);
    bool HandleValidateStagedEdit(const std::string& id, int& status, std::string& body);
    bool HandleCommitStagedEdit(const std::string& id, const std::string& if_match_etag, int& status, std::string& body);
    bool HandleDiscardStagedEdit(const std::string& id, int& status, std::string& body);
    bool HandleUndo(int& status, std::string& body);
    bool HandleRedo(int& status, std::string& body);
};

}  // namespace graphx::dsp::configuration
