/**
 * @file CSVInjectionPolicy.hpp
 * @brief Csvinjection Policy Graph runtime support.
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
#include "csv/CSVDataInjectionManager.hpp"
#include "capabilities/GraphCapability.hpp"
#include "capabilities/DataInjectionCapability.hpp"
#include "capabilities/CSVDataInjectionCapability.hpp"



namespace policies {

static auto csv_injection_logger_ = log4cxx::Logger::getLogger("app.policies.CSVInjectionPolicy");

/**
 * @class CSVInjectionPolicy
 * @brief Execution policy for CSV data injection into graph sensor nodes
 *
 * CSVInjectionPolicy specializes DataInjectionPolicy for CSV-based test data.
 * Configures CSV sensor nodes to read from CSV files during execution.
 *
 * Key responsibilities:
 * 1. **CSV Configuration**: Set up paths to CSV files for each sensor node
 * 2. **Node Discovery**: Find CSV-capable sensor nodes in the graph
 * 3. **File Binding**: Bind CSV files to specific sensor node inputs
 * 4. **Row Iteration**: Control CSV row injection during stepping/continuous execution
 * 5. **Data Streaming**: Manage streaming of CSV data to graph during execution
 *
 * CSV File Format:
 * - First row contains column headers (sensor names, timestamps, etc.)
 * - Remaining rows contain sensor values
 * - Each row represents a time-synchronized sample across all sensors
 * - Format: timestamp_ms,sensor1_value,sensor2_value,...
 *
 * Integration:
 * - Called after DataInjectionPolicy in policy chain
 * - Configures CSV input paths before graph execution
 * - Used for deterministic simulation and testing
 * - Essential for flight test data replay
 *
 * Thread Safety:
 * - CSV files read on executor thread during OnStart
 * - No concurrent file access
 *
 * @see DataInjectionPolicy, IDataInjectionSource
 */
/**
 * @class CSVInjectionPolicy
 * @brief Csvinjection Policy execution policy.
 *
 * @details Extends executor behavior at well-defined lifecycle points. Policies keep cross-cutting runtime concerns separate from graph node implementations.
 */
class CSVInjectionPolicy : public graph::IExecutionPolicy {
public:
    /**
     * @brief Construct a CSV injection policy
     */
    CSVInjectionPolicy() {
        /**
         * @brief Executes the Log4 Cxx Trace operation.
         *
         * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
         * @param csv_injection_logger_ Input or configuration value consumed by the method.
         * @return Method-specific result, status, or produced value when the signature provides one.
         */
        LOG4CXX_TRACE(csv_injection_logger_, "CSVInjectionPolicy initialized");
    }   

    /**
     * @brief Virtual destructor for proper cleanup
     */
    virtual ~CSVInjectionPolicy() = default;

    /**
     * @brief Initialize CSV injection infrastructure
     *
     * Called by GraphExecutor during Init() phase.
     * Validates CSV file paths and configures sensor nodes.
     *
     * @param context GraphExecutorContext with graph reference
     * @return True if initialization succeeded, false on error
     *
     * @see OnStart, SetCSVInputPaths
     */
    bool OnInit(capabilities::GraphCapability &context) override {
        /**
         * @brief Executes the Log4 Cxx Trace operation.
         *
         * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
         * @param csv_injection_logger_ Input or configuration value consumed by the method.
         * @return Method-specific result, status, or produced value when the signature provides one.
         */
        LOG4CXX_TRACE(csv_injection_logger_, "CSVInjectionPolicy OnInit called");
        auto data_injection_capability = context.GetCapabilityBus().Get<capabilities::DataInjectionCapability>();
        if (!data_injection_capability) {
            /**
             * @brief Executes the Log4 Cxx Warn operation.
             *
             * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
             * @param csv_injection_logger_ Input or configuration value consumed by the method.
             * @return Method-specific result, status, or produced value when the signature provides one.
             */
            LOG4CXX_WARN(csv_injection_logger_, "CSVDataInjectionPolicy::OnInit() - no DataInjectionCapability");
            return false;
        }

        csv_data_injection_manager_ = std::make_shared<csv::CSVDataInjectionManager>();
        csv_data_injection_manager_->ProcessCSVInputs(csv_input_paths_, context);
        auto node_configs = data_injection_capability->GetDataInjectionNodeConfigs();
        csv_data_injection_manager_->BindCSVColumnsToDataInjectionNodes(node_configs, context);
        csv_data_injection_capability_ = std::make_shared<capabilities::CSVDataInjectionCapability>();
        context.GetCapabilityBus().Register<capabilities::CSVDataInjectionCapability>(csv_data_injection_capability_);
        return true;
    }

    /**
     * @brief Start CSV data injection during execution
     *
     * Called by GraphExecutor during Start() phase.
     * Opens CSV files and begins streaming data to sensor nodes.
     *
     * @param context GraphExecutorContext for accessing graph
     * @return True if startup succeeded, false on error
     *
     * @see OnStop, SetCSVInputPaths
     */
    bool OnStart(capabilities::GraphCapability &context) override {
        /**
         * @brief Executes the Log4 Cxx Trace operation.
         *
         * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
         * @param csv_injection_logger_ Input or configuration value consumed by the method.
         * @return Method-specific result, status, or produced value when the signature provides one.
         */
        LOG4CXX_TRACE(csv_injection_logger_, "CSVInjectionPolicy OnStart called");
        if (!csv_data_injection_capability_) {
            LOG4CXX_WARN(csv_injection_logger_,
                         "CSVInjectionPolicy OnStart failed - no CSVDataInjectionCapability registered");
            return false;
        }
        if (!csv_data_injection_manager_) {
            LOG4CXX_WARN(csv_injection_logger_,
                         "CSVInjectionPolicy OnStart failed - no CSVDataInjectionManager initialized");
            return false;
        }
 
        auto fn = [this, &context]() {
            /**
             * @brief Executes the Log4 Cxx Trace operation.
             *
             * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
             * @param csv_injection_logger_ Input or configuration value consumed by the method.
             * @return Method-specific result, status, or produced value when the signature provides one.
             */
            LOG4CXX_TRACE(csv_injection_logger_, "CSVInjectionPolicy::CSVInjectionThread started");
            while (!context.IsStopped()) {
                capabilities::CSVDataInjectionCommand command;
                if (csv_data_injection_capability_->DequeueCommand(command)) {
                    LOG4CXX_TRACE(csv_injection_logger_, "CSVInjectionPolicy::Processing command to inject " 
                                     << command.nrow << " rows");
                    for (int i = 0; i < command.nrow; ++i) {
                        if (!csv_data_injection_manager_->InjectRowToNodes(context)) {
                            LOG4CXX_WARN(csv_injection_logger_, "CSVInjectionPolicy::Failed to inject row " 
                                             << i << ", stopping injection");
                            context.SetStopped();
                            break;
                        }
                        std::this_thread::sleep_for(std::chrono::milliseconds(250));
                    }
                    // Here you would call the appropriate method on csv_data_injection_manager_ to inject data
                } else {
                    context.SetStopped();
                }
            }
        };
        csv_injection_thread_ = std::jthread(fn);
        return true;
    }

    /**
     * @brief Stop CSV data injection during execution shutdown
     *
     * Called by GraphExecutor during Stop() phase.
     * Closes all open CSV files and stops data streaming.
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
         * @param csv_injection_logger_ Input or configuration value consumed by the method.
         * @return Method-specific result, status, or produced value when the signature provides one.
         */
        LOG4CXX_TRACE(csv_injection_logger_, "CSVInjectionPolicy OnStop called");
        if (!csv_data_injection_capability_) {
            LOG4CXX_TRACE(csv_injection_logger_,
                          "CSVInjectionPolicy OnStop skipped - no CSVDataInjectionCapability registered");
            return;
        }

        csv_data_injection_capability_->DisableCommand();
     }

    /**
     * @brief Finalize CSV injection after execution completes
     *
     * Called by GraphExecutor during Join() phase after all nodes complete.
     * Performs final cleanup and reports statistics.
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
         * @param csv_injection_logger_ Input or configuration value consumed by the method.
         * @return Method-specific result, status, or produced value when the signature provides one.
         */
        LOG4CXX_TRACE(csv_injection_logger_, "CSVInjectionPolicy OnJoin called");
        if (csv_injection_thread_.joinable()) {
            csv_injection_thread_.join();
        }
    }   

    /**
     * @brief Configure CSV input file paths for sensor nodes
     *
     * Maps CSV file paths to sensor node names. Typically called before graph
     * execution to set up which CSV files provide data to which sensor nodes.
     *
     * @param paths Vector of (node_name, file_path) pairs
     *               Example: {("CSVAccelerometerNode", "/path/to/accel.csv"),
     *                         ("CSVBarometricNode", "/path/to/baro.csv")}
     *
     * @see OnInit, OnStart
     */
    void SetCSVInputPaths(const std::vector<std::pair<std::string, std::string>>& paths) {
        csv_input_paths_ = paths;
        LOG4CXX_TRACE(csv_injection_logger_, "CSVInjectionPolicy::SetCSVInputPaths() - " 
                         << paths.size() << " paths set");
    }   

private:

    std::jthread csv_injection_thread_;
    std::shared_ptr<csv::CSVDataInjectionManager> csv_data_injection_manager_;
    std::shared_ptr<capabilities::CSVDataInjectionCapability> csv_data_injection_capability_;
    std::vector<std::pair<std::string, std::string>> csv_input_paths_;

}; // class CSVInjectionPolicy
    
}// namespace policies
