#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <utility>

namespace graphx::dsp::dashboard {

/**
 * @brief Security header generation and management.
 *
 * Implements OWASP ASVS 5.0 security headers:
 * - Content-Security-Policy (CSP) with nonce/hash strategy
 * - X-Content-Type-Options: nosniff
 * - X-Frame-Options: DENY
 * - X-XSS-Protection: 1; mode=block
 * - Referrer-Policy: no-referrer
 * - Permissions-Policy: camera=(), microphone=(), geolocation=()
 */
class SecurityHeaders {
public:
    /**
     * @brief Generate CSP header using build-time hash strategy.
     *
     * Strategy: Hash approach with build-time computed hashes for:
     * - Inline scripts (sha256 hash)
     * - Inline styles (sha256 hash)
     * - Self-hosted scripts and styles
     *
     * @param script_hashes Vector of script hashes (e.g., "sha256-abc123...")
     * @param style_hashes Vector of style hashes (e.g., "sha256-def456...")
     * @return CSP header value
     */
    static std::string GenerateCspHeaderHashStrategy(
        const std::vector<std::string>& script_hashes,
        const std::vector<std::string>& style_hashes);

    /**
     * @brief Generate CSP header using per-response nonce strategy.
     *
     * Strategy: Nonce approach with per-request nonce regeneration:
     * - Generate random nonce for each response
     * - Use nonce in CSP header
     * - Return nonce to caller for use in inline script/style tags
     *
     * @param nonce_out Out: Generated nonce value
     * @return CSP header value with nonce
     */
    static std::string GenerateCspHeaderNonceStrategy(std::string& nonce_out);

    /**
     * @brief Get all mandatory security headers (not CSP).
     *
     * Returns vector of header pairs:
     * - X-Content-Type-Options: nosniff
     * - X-Frame-Options: DENY
     * - X-XSS-Protection: 1; mode=block
     * - Referrer-Policy: no-referrer
     * - Permissions-Policy: camera=(), microphone=(), geolocation=()
     *
     * @return Vector of (header_name, header_value) pairs
     */
    static std::vector<std::pair<std::string, std::string>>
    GetMandatorySecurityHeaders();

    /**
     * @brief Generate a cryptographically secure random nonce.
     *
     * @param length Nonce length in bytes (default: 16)
     * @return Base64-encoded random nonce suitable for CSP
     */
    static std::string GenerateNonce(size_t length = 16);

    /**
     * @brief Compute SHA256 hash of content for CSP.
     *
     * @param content Content to hash
     * @return Hash in CSP format: "sha256-base64encodedvalue"
     */
    static std::string ComputeSha256Hash(std::string_view content);

    /**
     * @brief Validate that CSP header is properly formed.
     *
     * @param csp_header CSP header value to validate
     * @return true if header follows CSP spec, false if malformed
     */
    static bool ValidateCspHeader(std::string_view csp_header);

    /**
     * @brief Verify no unsafe-inline or unsafe-eval in CSP.
     *
     * @param csp_header CSP header value to check
     * @return true if no unsafe directives found, false if any present
     */
    static bool VerifyNoUnsafeDirectives(std::string_view csp_header);
};

}  // namespace graphx::dsp::dashboard
