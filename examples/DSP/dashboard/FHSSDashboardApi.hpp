// SPDX-License-Identifier: MIT

#pragma once

#include "graph/dashboard/EmbeddedDashboardServer.hpp"

#include <filesystem>
#include <memory>

namespace graph::dashboard {
class GraphConfigurationService;
}

namespace dsp::fhss::dashboard {

graph::dashboard::EmbeddedDashboardServer::ApiHandler MakeApiHandler(
    std::shared_ptr<graph::dashboard::GraphConfigurationService> configuration_service,
    std::filesystem::path artifact_root);

} // namespace dsp::fhss::dashboard
