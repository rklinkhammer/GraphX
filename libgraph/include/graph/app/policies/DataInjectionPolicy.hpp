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
#include "app/capabilities/GraphCapability.hpp"
#include "app/capabilities/DataInjectionCapability.hpp"



namespace app::policies {

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
class DataInjectionPolicy : public graph::IExecutionPolicy {
public:
    /**
     * @brief Construct a data injection policy
     */
    DataInjectionPolicy() {
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
    bool OnInit(app::capabilities::GraphCapability &context) override {
        LOG4CXX_TRACE(data_injection_logger_, "DataInjectionPolicy::OnInit() - creating DataInjectionManager");
        data_injection_capability_ = std::make_shared<app::capabilities::DataInjectionCapability>();
        context.GetCapabilityBus().Register<app::capabilities::DataInjectionCapability>(data_injection_capability_);
        InitDataInjectionSources(context);
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
    bool OnStart(app::capabilities::GraphCapability &) override {
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
    void OnStop(app::capabilities::GraphCapability &) override {
        LOG4CXX_TRACE(data_injection_logger_, "DataInjectionPolicy OnStop called");
        // Stop data injection and cleanup here
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
    void OnJoin(app::capabilities::GraphCapability &) override {
        LOG4CXX_TRACE(data_injection_logger_, "DataInjectionPolicy OnJoin called");
        // Finalize data injection reporting here
    }   
private:
    std::shared_ptr<app::capabilities::DataInjectionCapability> data_injection_capability_;
    void InitDataInjectionSources(app::capabilities::GraphCapability& context);

}; // class DataInjectionPolicy
    
}// namespace app::policies