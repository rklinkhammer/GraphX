// SPDX-License-Identifier: MIT

#pragma once

#include <expected>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "accelgraph/Accelerator.hpp"

namespace accelgraph {

class AcceleratorSessionRegistry {
public:
    bool RegisterSession(const std::string& key, std::shared_ptr<IAcceleratorSession> session);

    [[nodiscard]] std::expected<std::shared_ptr<IAcceleratorSession>, AcceleratorError>
    ResolveExactlyOne(const std::string& key) const;

private:
    mutable std::mutex mutex_;
    std::unordered_map<std::string, std::vector<std::shared_ptr<IAcceleratorSession>>> sessions_;
};

}  // namespace accelgraph
