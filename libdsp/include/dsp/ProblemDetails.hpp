#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <utility>

namespace graphx::dsp::dashboard {

/**
 * @brief RFC 9457 Problem Details error format.
 *
 * Implements standard error responses with:
 * - type: URI reference to problem type
 * - title: Human-readable summary
 * - status: HTTP status code
 * - detail: Human-readable description
 * - instance: Request path for debugging
 *
 * Never exposes:
 * - File system paths
 * - Compiler information
 * - System details
 */
class ProblemDetails {
public:
    /// HTTP status code
    int status = 0;

    /// Type URL (e.g., "about:blank" or "/problems/not-found")
    std::string type;

    /// Human-readable title (e.g., "Not Found")
    std::string title;

    /// Detailed description (e.g., "The requested resource was not found")
    std::string detail;

    /// Request path for debugging (e.g., "/assets/missing.js")
    std::string instance;

    /**
     * @brief Create a generic problem details object.
     *
     * @param status HTTP status code
     * @param title Human-readable title
     * @param detail Description (generic, no file paths)
     * @param instance Request path for context
     */
    ProblemDetails(int status, const std::string& title,
                   const std::string& detail, const std::string& instance);

    /**
     * @brief Create error for bad request.
     *
     * @param detail Generic error description
     * @param instance Request path
     * @return ProblemDetails for 400 Bad Request
     */
    static ProblemDetails BadRequest(const std::string& detail,
                                     const std::string& instance);

    /**
     * @brief Create error for not found.
     *
     * @param instance Request path that was not found
     * @return ProblemDetails for 404 Not Found
     */
    static ProblemDetails NotFound(const std::string& instance);

    /**
     * @brief Create error for method not allowed.
     *
     * @param method HTTP method that was not allowed (e.g., "POST")
     * @param instance Request path
     * @return ProblemDetails for 405 Method Not Allowed
     */
    static ProblemDetails MethodNotAllowed(const std::string& method,
                                          const std::string& instance);

    /**
     * @brief Create error for payload too large.
     *
     * @param max_bytes Maximum allowed size
     * @param instance Request path
     * @return ProblemDetails for 413 Payload Too Large
     */
    static ProblemDetails PayloadTooLarge(size_t max_bytes,
                                          const std::string& instance);

    /**
     * @brief Create error for URI too long.
     *
     * @param max_length Maximum allowed path length
     * @param instance Request path
     * @return ProblemDetails for 414 URI Too Long
     */
    static ProblemDetails UriTooLong(size_t max_length,
                                     const std::string& instance);

    /**
     * @brief Create error for too many requests.
     *
     * @param instance Request path
     * @return ProblemDetails for 429 Too Many Requests
     */
    static ProblemDetails TooManyRequests(const std::string& instance);

    /**
     * @brief Create error for precondition failed.
     *
     * @param instance Request path
     * @return ProblemDetails for 412 Precondition Failed
     */
    static ProblemDetails PreconditionFailed(const std::string& instance);

    /**
     * @brief Serialize to JSON format.
     *
     * Returns RFC 9457 compliant JSON:
     * {
     *   "type": "...",
     *   "title": "...",
     *   "status": ...,
     *   "detail": "...",
     *   "instance": "..."
     * }
     *
     * @return JSON string representation
     */
    std::string ToJson() const;

    /**
     * @brief Get Content-Type header for error responses.
     *
     * @return "application/problem+json"
     */
    static constexpr std::string_view ContentType() {
        return "application/problem+json";
    }
};

}  // namespace graphx::dsp::dashboard
