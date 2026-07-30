// SPDX-License-Identifier: MIT

#include "graph/GraphConfigurationSnapshot.hpp"

#include <iomanip>
#include <sstream>
#include <string_view>

namespace graph {
namespace {

std::string StableContentIdentity(const nlohmann::json& document) {
    constexpr std::uint64_t kOffsetBasis = 14695981039346656037ULL;
    constexpr std::uint64_t kPrime = 1099511628211ULL;

    std::uint64_t digest = kOffsetBasis;
    const std::string serialized = document.dump();
    for (const unsigned char byte : std::string_view{serialized}) {
        digest ^= static_cast<std::uint64_t>(byte);
        digest *= kPrime;
    }

    std::ostringstream encoded;
    encoded << "fnv1a64:" << std::hex << std::setw(16) << std::setfill('0')
            << digest;
    return encoded.str();
}

}  // namespace

GraphConfigurationSnapshot::GraphConfigurationSnapshot(
    nlohmann::json document, const std::uint64_t revision)
    : document_(std::make_shared<const nlohmann::json>(std::move(document))),
      revision_(revision),
      content_identity_(StableContentIdentity(*document_)) {}

}  // namespace graph
