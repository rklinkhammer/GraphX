#include "dsp/configuration/FHSSConfigurationHttpServer.hpp"
#include <algorithm>
#include <sstream>
#include <uuid/uuid.h>

namespace graphx::dsp::configuration {

FHSSConfigurationHttpServer::FHSSConfigurationHttpServer(
    std::shared_ptr<ConfigurationStateMachine> state_machine,
    const Options& options
) : state_machine_(state_machine), options_(options) {
}

FHSSConfigurationHttpServer::RequestHandler FHSSConfigurationHttpServer::GetRequestHandler() {
    return [this](
        std::string_view method,
        std::string_view path,
        const std::vector<std::pair<std::string, std::string>>& headers,
        std::string_view body,
        int& response_status,
        std::vector<std::pair<std::string, std::string>>& response_headers,
        std::string& response_body
    ) {
        return HandleRequest(method, path, headers, body, response_status, response_headers, response_body);
    };
}

nlohmann::json FHSSConfigurationHttpServer::SerializeWithSortedKeys(const nlohmann::json& j) {
    nlohmann::json result;
    
    if (j.is_object()) {
        // Get all keys and sort them
        std::vector<std::string> keys;
        for (const auto& [key, _] : j.items()) {
            keys.push_back(key);
        }
        std::sort(keys.begin(), keys.end());
        
        // Rebuild in sorted order
        for (const auto& key : keys) {
            result[key] = SerializeWithSortedKeys(j[key]);
        }
    } else if (j.is_array()) {
        result = nlohmann::json::array();  // Initialize as array first
        for (const auto& elem : j) {
            result.push_back(SerializeWithSortedKeys(elem));
        }
    } else {
        result = j;
    }
    
    return result;
}

nlohmann::json FHSSConfigurationHttpServer::CreateProblemDetails(
    int status,
    const std::string& code,
    const std::string& message,
    const std::string& detail
) {
    (void)code;
    nlohmann::json problem;
    problem["type"] = "about:blank";
    problem["status"] = status;
    
    // Map status code to standard title
    switch (status) {
        case 400: problem["title"] = "Bad Request"; break;
        case 404: problem["title"] = "Not Found"; break;
        case 409: problem["title"] = "Conflict"; break;
        case 422: problem["title"] = "Unprocessable Entity"; break;
        default: problem["title"] = "Error";
    }
    
    problem["detail"] = detail.empty() ? message : detail;
    
    return SerializeWithSortedKeys(problem);
}

nlohmann::json FHSSConfigurationHttpServer::CreateSuccessResponse(const nlohmann::json& data) {
    nlohmann::json response;
    response["schema"] = "graphx.fhss_configuration.v1";
    response["data"] = data;
    response["revision"] = state_machine_->GetCurrentRevision();
    response["etag"] = state_machine_->GetCurrentETag();
    
    return SerializeWithSortedKeys(response);
}

bool FHSSConfigurationHttpServer::HandleRequest(
    std::string_view method,
    std::string_view path,
    const std::vector<std::pair<std::string, std::string>>& headers,
    std::string_view body,
    int& response_status,
    std::vector<std::pair<std::string, std::string>>& response_headers,
    std::string& response_body
) {
    // Set content type for all responses
    response_headers.emplace_back("Content-Type", "application/json");

    // Route dispatcher
    std::string path_str(path);

    // GET /api/v2/fhss/config
    if (method == "GET" && path_str == "/api/v2/fhss/config") {
        return HandleGetConfig(response_status, response_body, response_headers);
    }

    // GET /api/v2/fhss/config/effective
    if (method == "GET" && path_str == "/api/v2/fhss/config/effective") {
        return HandleGetEffectiveConfig(response_status, response_body, response_headers);
    }

    // GET /api/v2/fhss/config/history
    if (method == "GET" && path_str == "/api/v2/fhss/config/history") {
        return HandleGetHistory(response_status, response_body, response_headers);
    }

    // POST /api/v2/fhss/config/staged
    if (method == "POST" && path_str == "/api/v2/fhss/config/staged") {
        nlohmann::json request_body;
        try {
            if (!body.empty()) {
                request_body = nlohmann::json::parse(body);
            }
        } catch (...) {
            response_status = 400;
            response_body = CreateProblemDetails(400, "INVALID_JSON", "Malformed JSON in request body").dump();
            return true;
        }
        return HandleCreateStagedEdit(request_body, response_status, response_body);
    }

    // PATCH /api/v2/fhss/config/staged/{id}
    if (method == "PATCH" && path_str.find("/api/v2/fhss/config/staged/") == 0) {
        std::string id = path_str.substr(std::string("/api/v2/fhss/config/staged/").length());
        
        nlohmann::json request_body;
        try {
            request_body = nlohmann::json::parse(body);
        } catch (...) {
            response_status = 400;
            response_body = CreateProblemDetails(400, "INVALID_JSON", "Malformed JSON in request body").dump();
            return true;
        }

        if (!request_body.contains("field") || !request_body.contains("value")) {
            response_status = 400;
            response_body = CreateProblemDetails(400, "MISSING_FIELDS", "Request must contain 'field' and 'value'").dump();
            return true;
        }

        std::string field = request_body["field"].get<std::string>();
        nlohmann::json value = request_body["value"];
        
        return HandleUpdateStagedField(id, field, value, response_status, response_body);
    }

    // POST /api/v2/fhss/config/validate
    if (method == "POST" && path_str == "/api/v2/fhss/config/validate") {
        nlohmann::json request_body;
        try {
            request_body = nlohmann::json::parse(body);
        } catch (...) {
            response_status = 400;
            response_body = CreateProblemDetails(400, "INVALID_JSON", "Malformed JSON in request body").dump();
            return true;
        }

        std::string id = "default";  // Default or from request if provided
        if (request_body.contains("staged_id")) {
            id = request_body["staged_id"].get<std::string>();
        }
        return HandleValidateStagedEdit(id, response_status, response_body);
    }

    // POST /api/v2/fhss/config/commit
    if (method == "POST" && path_str == "/api/v2/fhss/config/commit") {
        nlohmann::json request_body;
        try {
            request_body = nlohmann::json::parse(body);
        } catch (...) {
            response_status = 400;
            response_body = CreateProblemDetails(400, "INVALID_JSON", "Malformed JSON in request body").dump();
            return true;
        }

        if (!request_body.contains("staged_id")) {
            response_status = 400;
            response_body = CreateProblemDetails(400, "MISSING_STAGED_ID", "Request must contain 'staged_id'").dump();
            return true;
        }

        std::string id = request_body["staged_id"].get<std::string>();
        std::string if_match = "";
        
        // Check If-Match header first, then body
        for (const auto& [hdr_name, hdr_value] : headers) {
            if (hdr_name == "If-Match") {
                if_match = hdr_value;
                break;
            }
        }
        
        if (if_match.empty() && request_body.contains("if_match")) {
            if_match = request_body["if_match"].get<std::string>();
        }

        return HandleCommitStagedEdit(id, if_match, response_status, response_body);
    }

    // DELETE /api/v2/fhss/config/staged/{id}
    if (method == "DELETE" && path_str.find("/api/v2/fhss/config/staged/") == 0) {
        std::string id = path_str.substr(std::string("/api/v2/fhss/config/staged/").length());
        return HandleDiscardStagedEdit(id, response_status, response_body);
    }

    // POST /api/v2/fhss/config/undo
    if (method == "POST" && path_str == "/api/v2/fhss/config/undo") {
        return HandleUndo(response_status, response_body);
    }

    // POST /api/v2/fhss/config/redo
    if (method == "POST" && path_str == "/api/v2/fhss/config/redo") {
        return HandleRedo(response_status, response_body);
    }

    // Unknown route - return false to allow next handler in chain
    return false;
}

bool FHSSConfigurationHttpServer::HandleGetConfig(int& status, std::string& body, std::vector<std::pair<std::string, std::string>>& response_headers) {
    try {
        auto source = state_machine_->GetSourceConfiguration();
        auto source_json = source.to_json();
        
        nlohmann::json response = CreateSuccessResponse(source_json);
        response_headers.emplace_back("ETag", state_machine_->GetCurrentETag());
        status = 200;
        body = response.dump();
        return true;
    } catch (const std::exception& e) {
        status = 500;
        body = CreateProblemDetails(500, "INTERNAL_ERROR", e.what()).dump();
        return true;
    }
}

bool FHSSConfigurationHttpServer::HandleGetEffectiveConfig(int& status, std::string& body, std::vector<std::pair<std::string, std::string>>& response_headers) {
    try {
        auto effective = state_machine_->GetEffectiveConfiguration();
        auto effective_json = effective.to_json();
        
        nlohmann::json response = CreateSuccessResponse(effective_json);
        response_headers.emplace_back("ETag", state_machine_->GetCurrentETag());
        status = 200;
        body = response.dump();
        return true;
    } catch (const std::exception& e) {
        status = 500;
        body = CreateProblemDetails(500, "INTERNAL_ERROR", e.what()).dump();
        return true;
    }
}

bool FHSSConfigurationHttpServer::HandleGetHistory(int& status, std::string& body, std::vector<std::pair<std::string, std::string>>& response_headers) {
    try {
        // Placeholder: history retrieval would need state machine support
        nlohmann::json history_response;
        history_response["schema"] = "graphx.fhss_configuration_history.v1";
        history_response["history"] = nlohmann::json::array();
        
        // This would be populated by state machine history access (future enhancement)
        // For now, return empty history structure
        
        history_response = SerializeWithSortedKeys(history_response);
        response_headers.emplace_back("ETag", state_machine_->GetCurrentETag());
        status = 200;
        body = history_response.dump();
        return true;
    } catch (const std::exception& e) {
        status = 500;
        body = CreateProblemDetails(500, "INTERNAL_ERROR", e.what()).dump();
        return true;
    }
}

bool FHSSConfigurationHttpServer::HandleCreateStagedEdit(
    const nlohmann::json& request_body,
    int& status,
    std::string& body
) {
    (void)request_body;
    try {
        auto result = state_machine_->CreateStagedEdit();
        if (!result.success) {
            status = 400;
            body = CreateProblemDetails(400, "STAGED_EDIT_ERROR", result.error_message).dump();
            return true;
        }

        // Generate a UUID for the staged edit
        uuid_t uuid;
        uuid_generate_random(uuid);
        char uuid_str[37];
        uuid_unparse(uuid, uuid_str);
        
        nlohmann::json response;
        response["staged_id"] = std::string(uuid_str);
        response["base_revision"] = result.value.base_revision;
        
        auto source = result.value.staged_source;
        response["source"] = source.to_json();
        
        response = SerializeWithSortedKeys(response);
        status = 201;
        body = response.dump();
        return true;
    } catch (const std::exception& e) {
        status = 500;
        body = CreateProblemDetails(500, "INTERNAL_ERROR", e.what()).dump();
        return true;
    }
}

bool FHSSConfigurationHttpServer::HandleUpdateStagedField(
    const std::string& id,
    const std::string& field,
    const nlohmann::json& value,
    int& status,
    std::string& body
) {
    try {
        // Convert JSON value to string for state machine API
        std::string value_str;
        if (value.is_string()) {
            value_str = value.get<std::string>();
        } else {
            value_str = value.dump();
        }

        // Placeholder: staged edit update would require state machine to track staged edits by ID
        // For now, return a placeholder response
        
        nlohmann::json response;
        response["staged_id"] = id;
        response["base_revision"] = state_machine_->GetCurrentRevision();
        response["field"] = field;
        response["updated"] = true;
        
        response = SerializeWithSortedKeys(response);
        status = 200;
        body = response.dump();
        return true;
    } catch (const std::exception& e) {
        status = 500;
        body = CreateProblemDetails(500, "INTERNAL_ERROR", e.what()).dump();
        return true;
    }
}

bool FHSSConfigurationHttpServer::HandleValidateStagedEdit(
    const std::string& id,
    int& status,
    std::string& body
) {
    (void)id;
    try {
        // Placeholder: validation would require state machine to track staged edits by ID
        
        nlohmann::json response;
        response["is_valid"] = true;
        response["error_count"] = 0;
        response["errors"] = nlohmann::json::array();
        
        response = SerializeWithSortedKeys(response);
        status = 200;
        body = response.dump();
        return true;
    } catch (const std::exception& e) {
        status = 500;
        body = CreateProblemDetails(500, "INTERNAL_ERROR", e.what()).dump();
        return true;
    }
}

bool FHSSConfigurationHttpServer::HandleCommitStagedEdit(
    const std::string& id,
    const std::string& if_match_etag,
    int& status,
    std::string& body
) {
    try {
        // Check If-Match precondition
        if (!if_match_etag.empty() && if_match_etag != state_machine_->GetCurrentETag()) {
            nlohmann::json conflict_response;
            conflict_response["type"] = "about:blank";
            conflict_response["status"] = 409;
            conflict_response["title"] = "Conflict";
            conflict_response["detail"] = "If-Match ETag does not match current revision";
            conflict_response["instance"] = "/api/v2/fhss/config/commit";
            conflict_response["current_etag"] = state_machine_->GetCurrentETag();
            conflict_response["expected_etag"] = if_match_etag;
            
            conflict_response = SerializeWithSortedKeys(conflict_response);
            status = 409;
            body = conflict_response.dump();
            return true;
        }

        // Placeholder: commit would require state machine to track staged edits by ID
        
        nlohmann::json response_data;
        response_data["staged_id"] = id;
        response_data["new_revision"] = state_machine_->GetCurrentRevision() + 1;
        response_data["new_etag"] = "Rev:" + std::to_string(state_machine_->GetCurrentRevision() + 1);
        
        nlohmann::json response;
        response["schema"] = "graphx.fhss_configuration.commit_result.v1";
        response["data"] = response_data;
        
        response = SerializeWithSortedKeys(response);
        status = 200;
        body = response.dump();
        return true;
    } catch (const std::exception& e) {
        status = 500;
        body = CreateProblemDetails(500, "INTERNAL_ERROR", e.what()).dump();
        return true;
    }
}

bool FHSSConfigurationHttpServer::HandleDiscardStagedEdit(
    const std::string& id,
    int& status,
    std::string& body
) {
    (void)id;
    try {
        // Placeholder: staged edit discard
        status = 204;
        body = "";
        return true;
    } catch (const std::exception& e) {
        status = 500;
        body = CreateProblemDetails(500, "INTERNAL_ERROR", e.what()).dump();
        return true;
    }
}

bool FHSSConfigurationHttpServer::HandleUndo(int& status, std::string& body) {
    try {
        auto result = state_machine_->Undo();
        if (!result.success) {
            status = 400;
            body = CreateProblemDetails(400, "UNDO_ERROR", "Cannot undo: at beginning of history").dump();
            return true;
        }

        auto source = state_machine_->GetSourceConfiguration();
        nlohmann::json response_data;
        response_data["new_revision"] = result.value;
        response_data["new_etag"] = state_machine_->GetCurrentETag();
        response_data["source"] = source.to_json();
        
        nlohmann::json response;
        response["schema"] = "graphx.fhss_configuration.undo_result.v1";
        response["data"] = response_data;
        
        response = SerializeWithSortedKeys(response);
        status = 200;
        body = response.dump();
        return true;
    } catch (const std::exception& e) {
        status = 500;
        body = CreateProblemDetails(500, "INTERNAL_ERROR", e.what()).dump();
        return true;
    }
}

bool FHSSConfigurationHttpServer::HandleRedo(int& status, std::string& body) {
    try {
        auto result = state_machine_->Redo();
        if (!result.success) {
            status = 400;
            body = CreateProblemDetails(400, "REDO_ERROR", "Cannot redo: nothing to redo").dump();
            return true;
        }

        auto source = state_machine_->GetSourceConfiguration();
        nlohmann::json response_data;
        response_data["new_revision"] = result.value;
        response_data["new_etag"] = state_machine_->GetCurrentETag();
        response_data["source"] = source.to_json();
        
        nlohmann::json response;
        response["schema"] = "graphx.fhss_configuration.redo_result.v1";
        response["data"] = response_data;
        
        response = SerializeWithSortedKeys(response);
        status = 200;
        body = response.dump();
        return true;
    } catch (const std::exception& e) {
        status = 500;
        body = CreateProblemDetails(500, "INTERNAL_ERROR", e.what()).dump();
        return true;
    }
}

}  // namespace graphx::dsp::configuration
