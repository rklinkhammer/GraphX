// SPDX-License-Identifier: MIT

#include "accelgraph/AcceleratorSessionRegistry.hpp"

namespace accelgraph {

namespace {

AcceleratorError MakeResolveError(const std::string& diagnostic) {
    AcceleratorError error;
    error.category = AcceleratorErrorCategory::InvalidState;
    error.backend = AcceleratorBackend::Cpu;
    error.execution_mode = AcceleratorExecutionMode::HostSynchronous;
    error.operation = "ResolveSession";
    error.diagnostic = diagnostic;
    return error;
}

}  // namespace

bool AcceleratorSessionRegistry::RegisterSession(const std::string& key,
                                                 std::shared_ptr<IAcceleratorSession> session) {
    if (key.empty() || !session) {
        return false;
    }

    std::scoped_lock<std::mutex> lock(mutex_);
    sessions_[key].push_back(std::move(session));
    return true;
}

std::expected<std::shared_ptr<IAcceleratorSession>, AcceleratorError>
AcceleratorSessionRegistry::ResolveExactlyOne(const std::string& key) const {
    if (key.empty()) {
        return std::unexpected(MakeResolveError("session registry key must not be empty"));
    }

    std::scoped_lock<std::mutex> lock(mutex_);
    const auto it = sessions_.find(key);
    if (it == sessions_.end() || it->second.empty()) {
        return std::unexpected(MakeResolveError("no session registered for key '" + key + "'"));
    }
    if (it->second.size() != 1U) {
        return std::unexpected(
            MakeResolveError("expected exactly one session for key '" + key + "'"));
    }

    return it->second.front();
}

}  // namespace accelgraph
