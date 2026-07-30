/**
 * @file DataInjectionPolicy.hpp
 * @brief Data Injection Policy Graph runtime support.
 *
 * @details Provides executor policy integration for commands, metrics, completion, and data injection. This file is documented for Doxygen so public APIs and test support surfaces can be browsed consistently.
 */
// MIT License
//
// Copyright (c) 2025 Robert Klinkhammer
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

#include <memory>
#include <chrono>
#include <log4cxx/logger.h>
#include "graph/IExecutionPolicy.hpp"
#include "capabilities/GraphCapability.hpp"
#include "capabilities/DataInjectionCapability.hpp"



namespace policies {

static auto data_injection_logger_ = log4cxx::Logger::getLogger("app.policies.DataInjectionPolicy");

/**
 * @class DataInjectionPolicy
 * @brief Execution policy that manages data injection into the graph
 *
 * DataInjectionPolicy provides infrastructure for injecting data into graph
 * nodes during execution. Discovers IDataInjectionSource providers and manages
 * the injection lifecycle.
 *
 * Key responsibilities:
 * 1. **Source Discovery**: Find nodes that implement IDataInjectionSource
 * 2. **Initialization**: Set up injection sources with configuration
 * 3. **Lifecycle Management**: Control injection during execution phases
 * 4. **Cleanup**: Finalize injection sources on shutdown
 *
 * Integration:
 * - Discovers data injection nodes in the graph
 * - Called during OnInit to set up sources
 * - Manages injection throughout execution lifecycle
 * - Cleans up sources during OnJoin
 *
 * Thread Safety:
 * - Initialization happens on main thread during graph setup
 * - No concurrent injection operations expected
 *
 * @see IExecutionPolicy, IDataInjectionSource
 */
/**
 * @class DataInjectionPolicy
 * @brief Data Injection Policy execution policy.
 *
 * @details Extends executor behavior at well-defined lifecycle points. Policies keep cross-cutting runtime concerns separate from graph node implementations.
 */
class DataInjectionPolicy : public graph::IExecutionPolicy {
public:
    /**
     * @brief Construct a data injection policy
     */
    DataInjectionPolicy() {
        /**
         * @brief Executes the Log4 Cxx Trace operation.
         *
         * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
         * @param data_injection_logger_ Input or configuration value consumed by the method.
         * @return Method-specific result, status, or produced value when the signature provides one.
         */
        LOG4CXX_TRACE(data_injection_logger_, "DataInjectionPolicy initialized");
    }   

    /**
     * @brief Virtual destructor for proper cleanup
     */
    virtual ~DataInjectionPolicy() = default;

    /**
     * @brief Initialize data injection infrastructure
     *
     * Called by GraphExecutor during Init() phase.
     * Discovers and initializes all IDataInjectionSource nodes.
     *
     * @param context GraphExecutorContext with graph reference
     * @return True if initialization succeeded, false on error
     *
     * @see OnStart, OnJoin
     */
    bool OnInit(capabilities::GraphCapability &context) override {
        /**
         * @brief Executes the Log4 Cxx Trace operation.
         *
         * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
         * @param data_injection_logger_ Input or configuration value consumed by the method.
         * @return Method-specific result, status, or produced value when the signature provides one.
         */
        LOG4CXX_TRACE(data_injection_logger_, "DataInjectionPolicy::OnInit() - creating DataInjectionManager");
        data_injection_capability_ = std::make_shared<capabilities::DataInjectionCapability>();
        context.GetCapabilityBus().Register<capabilities::DataInjectionCapability>(data_injection_capability_);
        /**
         * @brief Performs the Init Data Injection Sources lifecycle step.
         *
         * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
         * @param context Input or configuration value consumed by the method.
         * @return Method-specific result, status, or produced value when the signature provides one.
         */
        InitDataInjectionSources(context);
        /**
         * @brief Executes the Log4 Cxx Trace operation.
         *
         * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
         * @param data_injection_logger_ Input or configuration value consumed by the method.
         * @return Method-specific result, status, or produced value when the signature provides one.
         */
        LOG4CXX_TRACE(data_injection_logger_, "DataInjectionPolicy::OnInit() - discovered " );
         return true;
    }

    /**
     * @brief Start data injection during execution
     *
     * Called by GraphExecutor during Start() phase.
     * Activates injection sources to begin feeding data to graph.
     *
     * @param context GraphExecutorContext for accessing graph
     * @return True if startup succeeded, false on error
     *
     * @see OnStop
     */
    bool OnStart(capabilities::GraphCapability &) override {
        /**
         * @brief Executes the Log4 Cxx Trace operation.
         *
         * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
         * @param data_injection_logger_ Input or configuration value consumed by the method.
         * @return Method-specific result, status, or produced value when the signature provides one.
         */
        LOG4CXX_TRACE(data_injection_logger_, "DataInjectionPolicy OnStart called");
        // Start data injection here
        return true;
    }

    /**
     * @brief Stop data injection during execution shutdown
     *
     * Called by GraphExecutor during Stop() phase.
     * Gracefully stops all data injection sources.
     *
     * @param context GraphExecutorContext for cleanup
     *
     * @see OnStart, OnJoin
     */
    void OnStop(capabilities::GraphCapability &) override {
        /**
         * @brief Executes the Log4 Cxx Trace operation.
         *
         * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
         * @param data_injection_logger_ Input or configuration value consumed by the method.
         * @return Method-specific result, status, or produced value when the signature provides one.
         */
        LOG4CXX_TRACE(data_injection_logger_, "DataInjectionPolicy OnStop called");
        if (data_injection_capability_) {
            data_injection_capability_->DisableAllInjectionQueues();
        }
    }

    /**
     * @brief Finalize data injection after execution completes
     *
     * Called by GraphExecutor during Join() phase after all nodes complete.
     * Performs final cleanup of injection sources.
     *
     * @param context GraphExecutorContext for final cleanup
     *
     * @see OnStop, OnInit
     */
    void OnJoin(capabilities::GraphCapability &) override {
        /**
         * @brief Executes the Log4 Cxx Trace operation.
         *
         * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
         * @param data_injection_logger_ Input or configuration value consumed by the method.
         * @return Method-specific result, status, or produced value when the signature provides one.
         */
        LOG4CXX_TRACE(data_injection_logger_, "DataInjectionPolicy OnJoin called");
        if (data_injection_capability_) {
            data_injection_capability_->DisableAllInjectionQueues();
        }
    }   
private:
    std::shared_ptr<capabilities::DataInjectionCapability> data_injection_capability_;
/**
 * @brief Init data injection sources.
 * @param context Parameter for init data injection sources.
 */
    void InitDataInjectionSources(capabilities::GraphCapability& context);

}; // class DataInjectionPolicy
    
}// namespace policies
