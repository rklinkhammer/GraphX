#include "dsp/ProblemDetails.hpp"

#include <sstream>
#include <iomanip>

namespace graphx::dsp::dashboard {

ProblemDetails::ProblemDetails(int status_code, const std::string& title_str,
                               const std::string& detail_str,
                               const std::string& instance_str)
    : status(status_code), type("about:blank"), title(title_str),
      detail(detail_str), instance(instance_str) {}

ProblemDetails ProblemDetails::BadRequest(const std::string& detail,
                                          const std::string& instance) {
    return ProblemDetails(400, "Bad Request", detail, instance);
}

ProblemDetails ProblemDetails::NotFound(const std::string& instance) {
    return ProblemDetails(404, "Not Found",
                          "The requested resource was not found", instance);
}

ProblemDetails ProblemDetails::MethodNotAllowed(const std::string& method,
                                                const std::string& instance) {
    std::string detail = "The " + method + " method is not allowed for this resource";
    return ProblemDetails(405, "Method Not Allowed", detail, instance);
}

ProblemDetails ProblemDetails::PayloadTooLarge(size_t max_bytes,
                                               const std::string& instance) {
    std::ostringstream detail;
    detail << "Request payload exceeds maximum of " << max_bytes << " bytes";
    return ProblemDetails(413, "Payload Too Large", detail.str(), instance);
}

ProblemDetails ProblemDetails::UriTooLong(size_t max_length,
                                          const std::string& instance) {
    std::ostringstream detail;
    detail << "Request path exceeds maximum of " << max_length << " characters";
    return ProblemDetails(414, "URI Too Long", detail.str(), instance);
}

ProblemDetails ProblemDetails::TooManyRequests(const std::string& instance) {
    return ProblemDetails(429, "Too Many Requests",
                          "Connection limit exceeded", instance);
}

ProblemDetails ProblemDetails::PreconditionFailed(const std::string& instance) {
    return ProblemDetails(412, "Precondition Failed",
                          "Request precondition was not met", instance);
}

std::string ProblemDetails::ToJson() const {
    std::ostringstream json;
    json << "{";
    json << "\"type\":\"" << type << "\",";
    json << "\"title\":\"" << title << "\",";
    json << "\"status\":" << status << ",";

    // Escape special characters in detail
    std::string escaped_detail = detail;
    size_t pos = 0;
    while ((pos = escaped_detail.find('"', pos)) != std::string::npos) {
        escaped_detail.insert(pos, 1, '\\');
        pos += 2;
    }
    pos = 0;
    while ((pos = escaped_detail.find('\n', pos)) != std::string::npos) {
        escaped_detail.replace(pos, 1, "\\n");
        pos += 2;
    }
    pos = 0;
    while ((pos = escaped_detail.find('\r', pos)) != std::string::npos) {
        escaped_detail.replace(pos, 1, "\\r");
        pos += 2;
    }

    json << "\"detail\":\"" << escaped_detail << "\",";
    json << "\"instance\":\"" << instance << "\"";
    json << "}";

    return json.str();
}

}  // namespace graphx::dsp::dashboard
