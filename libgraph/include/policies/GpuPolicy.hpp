/**
 * @file GpuPolicy.hpp
 * @brief GPU Policy Graph runtime support.
 *
 * @details Provides executor policy integration for commands, metrics, completion, and data injection. This file is documented for Doxygen so public APIs and test support surfaces can be browsed consistently.
 */
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

#if GRAPHX_ENABLE_CUDA_GRAPH_NODES || GRAPHX_ENABLE_SYCL_GRAPH_NODES || GRAPHX_ENABLE_METAL_GRAPH_NODES || GRAPHX_GPU_STUB_BACKENDS
#include "gpu/bootstrap/GpuCapabilityBootstrap.hpp"
#include "gpu/metal/capabilities/IMetalCapabilities.hpp"
#endif

namespace policies {

static auto gpu_policy_logger = log4cxx::Logger::getLogger("app.policies.GpuPolicy");

/**
 * @class GpuPolicy
 * @brief GPU Policy execution policy.
 *
 * @details Extends executor behavior at well-defined lifecycle points. Policies keep cross-cutting runtime concerns separate from graph node implementations.
 */
class GpuPolicy : public graph::IExecutionPolicy {
public:
    /**
     * @brief Executes the GPU Policy operation.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     * @return Method-specific result, status, or produced value when the signature provides one.
     */
    GpuPolicy() {
        /**
         * @brief Executes the Log4 Cxx Trace operation.
         *
         * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
         * @param gpu_policy_logger Input or configuration value consumed by the method.
         * @return Method-specific result, status, or produced value when the signature provides one.
         */
        LOG4CXX_TRACE(gpu_policy_logger, "GpuPolicy initialized");
    }

    /**
     * @brief Releases resources owned by GPU Policy.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     */
    ~GpuPolicy() override = default;

    /**
     * @brief Performs the On Init lifecycle step.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     * @param context Input or configuration value consumed by the method.
     * @return Method-specific result, status, or produced value when the signature provides one.
     */
    bool OnInit(capabilities::GraphCapability& context) override {
        /**
         * @brief Executes the Log4 Cxx Trace operation.
         *
         * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
         * @param gpu_policy_logger Input or configuration value consumed by the method.
         * @return Method-specific result, status, or produced value when the signature provides one.
         */
        LOG4CXX_TRACE(gpu_policy_logger, "GpuPolicy OnInit called");

#if GRAPHX_ENABLE_CUDA_GRAPH_NODES || GRAPHX_ENABLE_SYCL_GRAPH_NODES || GRAPHX_ENABLE_METAL_GRAPH_NODES || GRAPHX_GPU_STUB_BACKENDS
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
            /**
             * @brief Executes the Log4 Cxx Warn operation.
             *
             * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
             * @param gpu_policy_logger Input or configuration value consumed by the method.
             * @return Method-specific result, status, or produced value when the signature provides one.
             */
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

    /**
     * @brief Executes the On Start operation.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     * @param context Input or configuration value consumed by the method.
     * @return Method-specific result, status, or produced value when the signature provides one.
     */
    bool OnStart(capabilities::GraphCapability& context) override {
        (void)context;
        /**
         * @brief Executes the Log4 Cxx Trace operation.
         *
         * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
         * @param gpu_policy_logger Input or configuration value consumed by the method.
         * @return Method-specific result, status, or produced value when the signature provides one.
         */
        LOG4CXX_TRACE(gpu_policy_logger, "GpuPolicy OnStart called");
        return true;
    }

    /**
     * @brief Executes the On Stop operation.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     * @param context Input or configuration value consumed by the method.
     * @return Method-specific result, status, or produced value when the signature provides one.
     */
    void OnStop(capabilities::GraphCapability& context) override {
        (void)context;
        /**
         * @brief Executes the Log4 Cxx Trace operation.
         *
         * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
         * @param gpu_policy_logger Input or configuration value consumed by the method.
         * @return Method-specific result, status, or produced value when the signature provides one.
         */
        LOG4CXX_TRACE(gpu_policy_logger, "GpuPolicy OnStop called");
    }

    /**
     * @brief Executes the On Join operation.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     * @param context Input or configuration value consumed by the method.
     * @return Method-specific result, status, or produced value when the signature provides one.
     */
    void OnJoin(capabilities::GraphCapability& context) override {
        (void)context;
        /**
         * @brief Executes the Log4 Cxx Trace operation.
         *
         * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
         * @param gpu_policy_logger Input or configuration value consumed by the method.
         * @return Method-specific result, status, or produced value when the signature provides one.
         */
        LOG4CXX_TRACE(gpu_policy_logger, "GpuPolicy OnJoin called");
    }
};

}  // namespace policies
