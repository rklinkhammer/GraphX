#include "dsp/SecurityHeaders.hpp"

#include <sstream>
#include <random>
#include <iomanip>
#include <cstring>
#include <map>

// SHA256 implementation (simple fallback if OpenSSL not available)
#ifndef __APPLE__
#include <openssl/sha.h>
#else
// On macOS, use CommonCrypto
#include <CommonCrypto/CommonDigest.h>
#define SHA256_CTX CC_SHA256_CTX
#define SHA256_Init(ctx) CC_SHA256_Init(ctx)
#define SHA256_Update(ctx, data, len) CC_SHA256_Update(ctx, data, len)
#define SHA256_Final(hash, ctx) CC_SHA256_Final(hash, ctx)
#define SHA256_DIGEST_LENGTH CC_SHA256_DIGEST_LENGTH
#endif

namespace graphx::dsp::dashboard {

// Base64 encoding helper
static std::string Base64Encode(const uint8_t* data, size_t len) {
    static const char* const base64_chars =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string result;
    int i = 0;
    uint8_t char_array_3[3];
    uint8_t char_array_4[4];

    for (size_t n = 0; n < len; n++) {
        char_array_3[i++] = data[n];
        if (i == 3) {
            char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
            char_array_4[1] =
                ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
            char_array_4[2] =
                ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
            char_array_4[3] = char_array_3[2] & 0x3f;

            for (int j = 0; j < 4; j++) {
                result += base64_chars[char_array_4[j]];
            }
            i = 0;
        }
    }

    if (i > 0) {
        for (int j = i; j < 3; j++) {
            char_array_3[j] = '\0';
        }

        char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
        char_array_4[1] = ((char_array_3[0] & 0x03) << 4) +
                         ((char_array_3[1] & 0xf0) >> 4);
        char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) +
                         ((char_array_3[2] & 0xc0) >> 6);

        for (int j = 0; j <= i; j++) {
            result += base64_chars[char_array_4[j]];
        }

        while (i++ < 3) {
            result += '=';
        }
    }

    return result;
}

std::string SecurityHeaders::GenerateCspHeaderHashStrategy(
    const std::vector<std::string>& script_hashes,
    const std::vector<std::string>& style_hashes) {
    std::ostringstream csp;
    csp << "default-src 'none'; ";
    csp << "script-src 'self'";
    for (const auto& hash : script_hashes) {
        csp << " '" << hash << "'";
    }
    csp << "; ";
    csp << "style-src 'self'";
    for (const auto& hash : style_hashes) {
        csp << " '" << hash << "'";
    }
    csp << "; ";
    csp << "connect-src 'self'; ";
    csp << "frame-ancestors 'none'; ";
    csp << "base-uri 'self'; ";
    csp << "form-action 'self'";

    return csp.str();
}

std::string SecurityHeaders::GenerateCspHeaderNonceStrategy(
    std::string& nonce_out) {
    nonce_out = GenerateNonce();
    std::ostringstream csp;
    csp << "default-src 'none'; ";
    csp << "script-src 'self' 'nonce-" << nonce_out << "'; ";
    csp << "style-src 'self' 'nonce-" << nonce_out << "'; ";
    csp << "connect-src 'self'; ";
    csp << "frame-ancestors 'none'; ";
    csp << "base-uri 'self'; ";
    csp << "form-action 'self'";

    return csp.str();
}

std::vector<std::pair<std::string, std::string>>
SecurityHeaders::GetMandatorySecurityHeaders() {
    return {
        {"X-Content-Type-Options", "nosniff"},
        {"X-Frame-Options", "DENY"},
        {"X-XSS-Protection", "1; mode=block"},
        {"Referrer-Policy", "no-referrer"},
        {"Permissions-Policy",
         "camera=(), microphone=(), geolocation=()"},
    };
}

std::string SecurityHeaders::GenerateNonce(size_t length) {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 255);

    std::vector<uint8_t> random_bytes(length);
    for (size_t i = 0; i < length; i++) {
        random_bytes[i] = static_cast<uint8_t>(dis(gen));
    }

    return Base64Encode(random_bytes.data(), random_bytes.size());
}

std::string SecurityHeaders::ComputeSha256Hash(std::string_view content) {
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256_CTX sha256;
    SHA256_Init(&sha256);
    SHA256_Update(&sha256, content.data(), content.size());
    SHA256_Final(hash, &sha256);

    std::string encoded = Base64Encode(hash, SHA256_DIGEST_LENGTH);
    return "sha256-" + encoded;
}

bool SecurityHeaders::ValidateCspHeader(std::string_view csp_header) {
    // Basic validation: ensure it contains required directives
    std::string csp_str(csp_header);
    return !csp_str.empty() && csp_str.find("default-src") != std::string::npos;
}

bool SecurityHeaders::VerifyNoUnsafeDirectives(std::string_view csp_header) {
    std::string csp_str(csp_header);
    return csp_str.find("'unsafe-inline'") == std::string::npos &&
           csp_str.find("'unsafe-eval'") == std::string::npos;
}

}  // namespace graphx::dsp::dashboard
