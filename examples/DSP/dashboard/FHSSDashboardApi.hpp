// SPDX-License-Identifier: MIT

#pragma once

#include "graph/dashboard/EmbeddedDashboardServer.hpp"

#include <memory>

namespace graph::dashboard {
class GraphConfigurationService;
class GraphRuntimeSession;
} // namespace graph::dashboard

namespace dsp::fhss::dashboard {
class FHSSJobController;

graph::dashboard::EmbeddedDashboardServer::ApiHandler MakeApiHandler(
    std::shared_ptr<graph::dashboard::GraphConfigurationService>
        configuration_service,
    std::shared_ptr<graph::dashboard::GraphRuntimeSession> runtime_session,
    std::shared_ptr<FHSSJobController> job_controller = {});

} // namespace dsp::fhss::dashboard
