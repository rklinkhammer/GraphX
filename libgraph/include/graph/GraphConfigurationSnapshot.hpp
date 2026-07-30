// SPDX-License-Identifier: MIT

#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include <nlohmann/json.hpp>

namespace graph {

/**
 * Immutable, atomically publishable graph configuration value.
 *
 * The document is held behind shared ownership and is exposed only through a
 * const reference.  The identity is a stable FNV-1a digest of the canonical
 * nlohmann JSON serialization and is therefore independent of object address
 * or process-local hash randomization.
 */
class GraphConfigurationSnapshot {
public:
    GraphConfigurationSnapshot(nlohmann::json document, std::uint64_t revision);

    [[nodiscard]] const nlohmann::json& Document() const noexcept {
        return *document_;
    }

    [[nodiscard]] std::uint64_t Revision() const noexcept {
        return revision_;
    }

    [[nodiscard]] const std::string& ContentIdentity() const noexcept {
        return content_identity_;
    }

private:
    std::shared_ptr<const nlohmann::json> document_;
    std::uint64_t revision_;
    std::string content_identity_;
};

}  // namespace graph
