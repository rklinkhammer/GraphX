// MIT License
//
// Copyright (c) 2026 GraphX contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#pragma once

#include <log4cxx/logger.h>
#include <memory>

#include "capabilities/GraphCapability.hpp"
#include "graph/CapabilityContext.hpp"
#include "graph/IGpuCapabilityBinding.hpp"
#include "graph/IExecutionPolicy.hpp"

#if GRAPHX_ENABLE_CUDA_GRAPH_NODES || GRAPHX_ENABLE_SYCL_GRAPH_NODES || GRAPHX_GPU_STUB_BACKENDS
#include "gpu/bootstrap/GpuCapabilityBootstrap.hpp"
#include "gpu/metal/capabilities/IMetalCapabilities.hpp"
#endif

namespace policies {

static auto gpu_policy_logger = log4cxx::Logger::getLogger("app.policies.GpuPolicy");

class GpuPolicy : public graph::IExecutionPolicy {
public:
    GpuPolicy() {
        LOG4CXX_TRACE(gpu_policy_logger, "GpuPolicy initialized");
    }

    ~GpuPolicy() override = default;

    bool OnInit(capabilities::GraphCapability& context) override {
        LOG4CXX_TRACE(gpu_policy_logger, "GpuPolicy OnInit called");

#if GRAPHX_ENABLE_CUDA_GRAPH_NODES || GRAPHX_ENABLE_SYCL_GRAPH_NODES || GRAPHX_GPU_STUB_BACKENDS
    if (context.IsGpuBootstrapEnabled()) {
        graph::gpu::GpuCapabilityBootstrapOptions options{};
        graph::gpu::RegisterDefaultGpuCapabilities(context.GetCapabilityBus(), options);
        LOG4CXX_TRACE(gpu_policy_logger,
              "GpuPolicy registered default GPU capabilities into CapabilityBus");

        auto metal_context = context.GetCapabilityBus().Get<graph::gpu::metal::capabilities::IMetalContextCapability>();
        if (metal_context != nullptr &&
            !context.GetCapabilityBus().Has<graph::gpu::metal::capabilities::IMetalSharedQueueCapability>()) {
            context.GetCapabilityBus().Register<graph::gpu::metal::capabilities::IMetalSharedQueueCapability>(
                std::make_shared<graph::gpu::metal::capabilities::MetalSharedQueueCapability>(
                    std::move(metal_context)));
            LOG4CXX_TRACE(gpu_policy_logger,
                          "GpuPolicy registered graph-level shared Metal queue capability");
        }
    } else {
        LOG4CXX_TRACE(gpu_policy_logger,
              "GpuPolicy GPU bootstrap disabled by GraphCapability");
    }
#else
        (void)context;
        LOG4CXX_TRACE(gpu_policy_logger,
                      "GpuPolicy GPU capability bootstrap skipped (GPU backends disabled)");
#endif

        graph::CapabilityContext capability_context{context};
        auto nodes_result = capability_context.Nodes();
        if (!nodes_result) {
            LOG4CXX_WARN(gpu_policy_logger, "GpuPolicy OnInit no GraphManager available");
            return false;
        }

        std::size_t bindings_attempted = 0;
        std::size_t bindings_succeeded = 0;

        for (const auto& node : *nodes_result) {
            auto gpu_binding = capability_context.NodeCapability<graph::IGpuCapabilityBinding>(node);
            if (!gpu_binding) {
                continue;
            }

            ++bindings_attempted;
            if ((*gpu_binding)->BindGpuCapabilities(context.GetCapabilityBus())) {
                ++bindings_succeeded;
            }
        }

        LOG4CXX_TRACE(gpu_policy_logger,
                      "GpuPolicy bound GPU capabilities on "
                          << bindings_succeeded << "/" << bindings_attempted << " nodes");
        if (bindings_succeeded != bindings_attempted) {
            LOG4CXX_WARN(gpu_policy_logger,
                         "GpuPolicy failed to bind required GPU capabilities on "
                             << (bindings_attempted - bindings_succeeded)
                             << " node(s)");
            return false;
        }

        return true;
    }

    bool OnStart(capabilities::GraphCapability& context) override {
        (void)context;
        LOG4CXX_TRACE(gpu_policy_logger, "GpuPolicy OnStart called");
        return true;
    }

    void OnStop(capabilities::GraphCapability& context) override {
        (void)context;
        LOG4CXX_TRACE(gpu_policy_logger, "GpuPolicy OnStop called");
    }

    void OnJoin(capabilities::GraphCapability& context) override {
        (void)context;
        LOG4CXX_TRACE(gpu_policy_logger, "GpuPolicy OnJoin called");
    }
};

}  // namespace policies
