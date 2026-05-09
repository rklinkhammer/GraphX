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

namespace app::policies
{

    static auto completion_logger = log4cxx::Logger::getLogger("app.policies.CompletionPolicy");

    /**
     * @class CompletionPolicy
     * @brief Execution policy that detects and handles graph completion
     *
     * CompletionPolicy monitors the graph for completion signals and manages
     * graceful shutdown when all work is done. Integrates with sink nodes
     * (like CompletionAggregatorNode) that report completion status.
     *
     * Key responsibilities:
     * 1. **Completion Detection**: Register callbacks with completion aggregator nodes
     * 2. **Signal Waiting**: Monitor for completion signals from nodes
     * 3. **Graceful Shutdown**: Trigger execution stop when completion detected
     * 4. **Timeout Handling**: Optional timeout to stop if completion takes too long
     * 5. **Status Reporting**: Provide completion statistics to dashboard
     *
     * Integration:
     * - Discovers CompletionAggregatorNode instances in the graph
     * - Registers callbacks to receive completion notifications
     * - Waits for completion signal with optional timeout
     * - Signals ExecutionContext to stop when complete
     *
     * Thread Model:
     * - Spins up thread during OnStart()
     * - Thread waits on condition variable for completion signal
     * - Completion callback from executor thread notifies condition variable
     * - Main thread can also check completion status
     *
     * @see IExecutionPolicy, CompletionAggregatorNode
     */
    class CompletionPolicy : public graph::IExecutionPolicy
    {
    public:
        /**
         * @brief Construct a completion policy
         */
        CompletionPolicy()
        {
            LOG4CXX_TRACE(completion_logger, "CompletionPolicy initialized");
        }

        /**
         * @brief Virtual destructor for proper cleanup
         */
        virtual ~CompletionPolicy() = default;

        /**
         * @brief Initialize completion detection during graph setup
         *
         * Called by GraphExecutor during Init() phase.
         * Discovers completion nodes and registers callbacks.
         *
         * @param context GraphExecutorContext with graph reference
         * @return True if initialization succeeded, false on error
         *
         * @see OnStart
         */
        bool OnInit(app::capabilities::GraphCapability &context) override
        {
            LOG4CXX_TRACE(completion_logger, "CompletionPolicy OnInit called");
            // Register callbacks with completion nodes
            InitCompletionCallbacks(context);
            LOG4CXX_TRACE(completion_logger, "CompletionPolicy OnInit completed");            
            return true;
        }

        /**
         * @brief Start the completion detection thread
         *
         * Called by GraphExecutor during Start() phase.
         * Spawns thread that waits for completion signal.
         *
         * @param context GraphExecutorContext for accessing graph
         * @return True if thread startup succeeded, false on error
         *
         * @see OnStop
         */
        bool OnStart(app::capabilities::GraphCapability &context) override
        {
            LOG4CXX_TRACE(completion_logger, "CompletionPolicy OnStart called");
            if(!cli_mode_) {
                return true;
            }
            auto fn = [this, &context]()
            {
                LOG4CXX_TRACE(completion_logger, "CompletionPolicy::OnRun() - waiting for completion signal or timeout");
                std::unique_lock lock(completion_mutex_);
                if (!completion_signaled_)
                {
                    auto completed = completion_cv_.wait_for(lock, max_duration_);
                    LOG4CXX_TRACE(completion_logger, "CompletionPolicy::OnRun() - wait completed, signaled=" 
                        << (completed == std::cv_status::no_timeout));
                }
                else
                {
                    LOG4CXX_TRACE(completion_logger, "CompletionPolicy::OnRun() - completion already signaled");
                }
                context.SetStopped();
                LOG4CXX_TRACE(completion_logger, "CompletionPolicy::OnRun() - signaling shutdown");
            };
            completion_thread_ = std::thread(fn);
            return true;
        }

        void OnStop(app::capabilities::GraphCapability &) override
        {
            LOG4CXX_TRACE(completion_logger, "CompletionPolicy OnStop called");
            SetCompletionSignaled();
            // Stop metrics collection and cleanup here if needed
        }

        void OnJoin(app::capabilities::GraphCapability &) override
        {
            LOG4CXX_TRACE(completion_logger, "CompletionPolicy OnJoin called");
            if (completion_thread_.joinable()) {
                completion_thread_.join();
            }   
            // Finalize metrics reporting here if needed
        }

        void SetMaxDuration(std::chrono::milliseconds duration)
        {
            max_duration_ = duration;
            LOG4CXX_TRACE(completion_logger, "CompletionPolicy max duration set to "
                                                 << max_duration_.count() << " ms");
        }

        void SetCompletionSignaled()
        {
            {
                std::lock_guard lock(completion_mutex_);
                completion_signaled_ = true;
            }
            completion_cv_.notify_all();
            LOG4CXX_TRACE(completion_logger, "CompletionPolicy signaled completion");
        }
        
        void SetCliMode(bool enabled)
        {
            cli_mode_ = enabled;
        }

        bool IsCliMode() const
        {
            return cli_mode_;
        }

    private:
        bool InitCompletionCallbacks(app::capabilities::GraphCapability &context);

        std::thread completion_thread_;
        std::condition_variable completion_cv_;
        std::mutex completion_mutex_;
        bool completion_signaled_ = false;
        bool cli_mode_ = false;
        std::chrono::milliseconds max_duration_;
        std::vector<std::shared_ptr<void>> completion_callbacks_;

    }; // class CompletionPolicy

} // namespace app::policies