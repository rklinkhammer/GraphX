// SPDX-License-Identifier: MIT

#pragma once

#include "graph/dashboard/EmbeddedDashboardServer.hpp"

#include <memory>

namespace graph::dashboard {
class GraphConfigurationService;
class GraphRuntimeSession;
}

namespace dsp::fhss::dashboard {

graph::dashboard::EmbeddedDashboardServer::ApiHandler MakeApiHandler(
    std::shared_ptr<graph::dashboard::GraphConfigurationService>
        configuration_service,
    std::shared_ptr<graph::dashboard::GraphRuntimeSession> runtime_session);

} // namespace dsp::fhss::dashboard
