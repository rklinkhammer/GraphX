// SPDX-License-Identifier: MIT
#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <type_traits>

#include <log4cxx/logger.h>

#include "core/ActiveQueue.hpp"
#include "graph/INode.hpp"
#include "graph/InputFunction.hpp"
#include "graph/Lifecycle.hpp"
#include "graph/MergeFunction.hpp"
#include "graph/NamedType.hpp"
#include "graph/OutputFunction.hpp"
#include "graph/PortSpec.hpp"
#include "graph/PortTypes.hpp"
#include "graph/ThreadMetrics.hpp"

namespace graph {
    // ===================================================================================
    // Edge<TSrc,SrcPort,TDst,DstPort>
    // -----------------------------------------------------------------------------------
    // - Holds a deque<Message> as the queue between producer and consumer.
    // - TryProduce asks src->Produce; if value available and queue has room, wraps value in Message and pushes.
    // - TryConsume peeks front Message, checks type, and passes by reference to dst->Consume.
    // - All queue operations are protected by an internal mutex.
    // ===================================================================================

    /**
     * @brief Type-safe edge connecting two nodes
     * @tparam SrcNode Source node type
     * @tparam SrcPort Source output port ID
     * @tparam DstNode Destination node type
     * @tparam DstPort Destination input port ID
     *
     * Edge provides a typed connection between nodes, transferring data from
     * a source output port to a destination input port. The connection is
     * type-checked at compile time to ensure compatibility.
     *
     * Features:
     * - Runs in its own thread to decouple producer and consumer
     * - Validates type compatibility at compile time
     * - Provides Init/Start/Stop/Join lifecycle
     * - Integrated logging for debugging
     *
     * Example:
     * @code
     *   auto src = std::make_shared<MySource>();
     *   auto dst = std::make_shared<MySink>();
     *   Edge<MySource, 0, MySink, 0> edge(src, dst);
     *   edge.Init();
     *   edge.Start();
     * @endcode
     */
    template <typename SrcNode, std::size_t SrcPort, typename DstNode, std::size_t DstPort>
/**
 * @class Edge
 * @brief Edge type.
 *
 * @details Part of the GraphX public API for libgraph. The type documents its runtime role, ownership expectations, and interaction with neighboring graph components.
 */
    /**
     * @class Edge
     * @brief Edge type.
     *
     * @details Part of the GraphX public API for libgraph. The type documents its runtime role, ownership expectations, and interaction with neighboring graph components.
     */
    class Edge
    {
    public:
        using ValueType = typename SrcNode::template OutputType<SrcPort>; ///< Data type flowing through this edge
        using DstPortType = typename DstNode::template InputPortType<DstPort>;

        // Static assert: DstNode must inherit from either IInputFn or IInputCommonFn for the port type
        // - IInputFn: Used by regular input ports (per-port queues, per-port threads)
        // - IInputCommonFn: Used by MergeNode input ports (shared unified queue, single thread)
        static_assert(
            std::is_base_of_v<IInputFn<DstPortType>, DstNode> ||
            std::is_base_of_v<IInputCommonFn<DstPortType>, DstNode>,
            "DstNode must inherit from IInputFn or IInputCommonFn for the specified port type");
 
        Edge(std::shared_ptr<SrcNode> src, std::shared_ptr<DstNode> dst, std::size_t buffer_size = 8)
            : src_(std::move(src)),
              dst_(std::move(dst)),
              buffer_size_(buffer_size)
        {
        }

        /**
         * @brief Releases resources owned by Edge.
         *
         * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
         */
        virtual ~Edge() = default;
    
        virtual bool Init()
        {
            /**
             * @brief Executes the Log4 Cxx Trace operation.
             *
             * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
             * @return Method-specific result, status, or produced value when the signature provides one.
             */
            LOG4CXX_TRACE(log4cxx::Logger::getLogger("graph.edge"), "Edge from port " << SrcPort << " to port " << DstPort << " Init.");
            return true;
        }

        virtual bool Start()
        {
            /**
             * @brief Executes the Log4 Cxx Trace operation.
             *
             * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
             * @return Method-specific result, status, or produced value when the signature provides one.
             */
            LOG4CXX_TRACE(log4cxx::Logger::getLogger("graph.edge"), "Edge from port " << SrcPort << " to port " << DstPort << " Start.");
            
            // Perform type checking once at startup (not in hot loop)
            // This determines which enqueue path we'll use for all subsequent iterations
            //using InputPortType = typename DstNode::template InputPortType<DstPort>;
            
            // Try IInputFn first (regular input ports with per-port queues)
            enqueue_fn_iinput_ = dynamic_cast<IInputFn<DstPortType> *>(dst_.get());
            if (enqueue_fn_iinput_) {
                LOG4CXX_TRACE(log4cxx::Logger::getLogger("graph.edge"),
                             "Edge from port " << SrcPort << " to port " << DstPort << " using IInputFn enqueue path.");
            } else {
                // Try IInputCommonFn (MergeNode with unified queue)
                enqueue_fn_common_ = dynamic_cast<IInputCommonFn<DstPortType> *>(dst_.get());
                if (enqueue_fn_common_) {
                    LOG4CXX_TRACE(log4cxx::Logger::getLogger("graph.edge"),
                                 "Edge from port " << SrcPort << " to port " << DstPort << " using IInputCommonFn enqueue path.");
                } else {
                    LOG4CXX_ERROR(log4cxx::Logger::getLogger("graph.edge"),
                                 "Edge failed to resolve input interface for port " << DstPort);
                    return false;
                }
            }
            
            // Launch edge thread with type-specific enqueue path already determined
            auto t = [this]() -> void {
                /**
                 * @brief Executes the Edge Thread Func operation.
                 *
                 * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
                 * @return Method-specific result, status, or produced value when the signature provides one.
                 */
                EdgeThreadFunc();
            };
            thread_ = std::thread(t);
            return true;
        }

        virtual void Stop()
        {
            /**
             * @brief Executes the Log4 Cxx Trace operation.
             *
             * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
             * @return Method-specific result, status, or produced value when the signature provides one.
             */
            LOG4CXX_TRACE(log4cxx::Logger::getLogger("graph.edge"), "Edge from port " << SrcPort << " to port " << DstPort << " Stop.");
            stop_requested_.store(true, std::memory_order_release);
            src_->Stop();
            dst_->Stop();
        }

        virtual void Join()
        {
            /**
             * @brief Executes the Log4 Cxx Trace operation.
             *
             * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
             * @return Method-specific result, status, or produced value when the signature provides one.
             */
            LOG4CXX_TRACE(log4cxx::Logger::getLogger("graph.edge"), "Edge from port " << SrcPort << " to port " << DstPort << " Join.");
            if (thread_.joinable())
            {
                thread_.join();
            }
        }

        /**
         * @brief Join with timeout
         * @param timeout_ms Maximum milliseconds to wait for edge thread
         * @return true if thread completed within timeout, false if timeout exceeded
         *
         * NOTE: This is a best-effort non-blocking implementation.
         * If the timeout expires and the thread is still running, this function
         * returns false WITHOUT joining the thread (to avoid blocking indefinitely).
         * The thread will eventually complete when the system is idle, or will be
         * detached when the Edge object is destroyed.
         */
        bool JoinWithTimeout(std::chrono::milliseconds timeout_ms)
        {
            /**
             * @brief Executes the Log4 Cxx Trace operation.
             *
             * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
             * @return Method-specific result, status, or produced value when the signature provides one.
             */
            LOG4CXX_TRACE(log4cxx::Logger::getLogger("graph.edge"), "Edge from port " << SrcPort << " to port " << DstPort << " JoinWithTimeout.");
            if (!thread_.joinable())
                return true;

            /**
             * @brief Executes the Lock operation.
             *
             * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
             * @param mtx_ Input or configuration value consumed by the method.
             * @return Method-specific result, status, or produced value when the signature provides one.
             */
            std::unique_lock lock(mtx_);
            if (!cv_.wait_for(lock, timeout_ms, [&] { return stop_requested_.load(std::memory_order_acquire); }))
            {
                /**
                 * @brief Executes the Log4 Cxx Trace operation.
                 *
                 * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
                 * @return Method-specific result, status, or produced value when the signature provides one.
                 */
                LOG4CXX_TRACE(log4cxx::Logger::getLogger("graph.edge"), "Thread did not finish within timeout");
                return false;
            }

            thread_.join();
            return true;
        }

        std::shared_ptr<INode> source_ptr() const noexcept { return src_; }
        std::shared_ptr<INode> dest_ptr() const noexcept { return dst_; }
        std::size_t src_port() const noexcept { return SrcPort; }
        std::size_t dst_port() const noexcept { return DstPort; }

        /// Get thread metrics for this edge (transfer operations)
        /// @return Const reference to ThreadMetrics (valid until edge destroyed)
        const ThreadMetrics& GetThreadMetrics() const {
            return thread_metrics_;
        }

        /// Reset all collected metrics
        void ResetMetrics() {
            thread_metrics_.total_iterations.store(0, std::memory_order_release);
            thread_metrics_.produce_calls.store(0, std::memory_order_release);
            thread_metrics_.consume_calls.store(0, std::memory_order_release);
            thread_metrics_.transfer_calls.store(0, std::memory_order_release);
            thread_metrics_.total_produce_time_ns.store(0, std::memory_order_release);
            thread_metrics_.total_consume_time_ns.store(0, std::memory_order_release);
            thread_metrics_.total_transfer_time_ns.store(0, std::memory_order_release);
            thread_metrics_.total_idle_time_ns.store(0, std::memory_order_release);
        }

        //...
    private:
        void EdgeThreadFunc()
        {
            /**
             * @brief Executes the Log4 Cxx Trace operation.
             *
             * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
             * @return Method-specific result, status, or produced value when the signature provides one.
             */
            LOG4CXX_TRACE(log4cxx::Logger::getLogger("graph.edge"), "Edge from port " << SrcPort << " to port " << DstPort << " running.");
            
            // Mark thread as active
            thread_metrics_.thread_active.store(true, std::memory_order_release);
            
            while (!stop_requested_.load(std::memory_order_acquire))
            {
                // Measure transfer operation time (Dequeue + Enqueue)
                auto start_time = std::chrono::steady_clock::now();
                
                // Try to produce from source - call through IOutputFn interface
                auto maybe_value = static_cast<IOutputFn<typename SrcNode::template OutputPortType<SrcPort>> *>(src_.get())->Dequeue(std::integral_constant<std::size_t, SrcPort>{});
                
                auto duration_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now() - start_time).count();
                
                if (maybe_value.has_value())
                {
                    // Record transfer operation
                    thread_metrics_.transfer_calls.fetch_add(1, std::memory_order_acq_rel);
                    thread_metrics_.total_transfer_time_ns.fetch_add(duration_ns, std::memory_order_acq_rel);
                    
                    // HOT PATH: No dynamic_cast here - type was resolved in Start()
                    // Simply use the pre-determined interface pointer
                    bool enqueued = false;
                    if (enqueue_fn_iinput_) {
                        // Use IInputFn path (per-port queues)
                        enqueued = enqueue_fn_iinput_->Enqueue(maybe_value.value());
                    } else if (enqueue_fn_common_) {
                        // Use IInputCommonFn path (unified queue)
                        enqueued = enqueue_fn_common_->Enqueue(maybe_value.value());
                    } else {
                        // This should never happen - Start() would have failed
                        LOG4CXX_ERROR(log4cxx::Logger::getLogger("graph.edge"),
                                     "Edge from port " << SrcPort << " to port " << DstPort << " no enqueue function available.");
                        stop_requested_.store(true, std::memory_order_release);
                        break;
                    }
                    
                    if (!enqueued) {
                        LOG4CXX_TRACE(log4cxx::Logger::getLogger("graph.edge"),
                                     "Edge from port " << SrcPort << " to port " << DstPort << " failed to enqueue.");
                        stop_requested_.store(true, std::memory_order_release);
                        break;
                    }
                }
                else
                {
                    LOG4CXX_TRACE(log4cxx::Logger::getLogger("graph.edge"),
                                 "Edge from port " << SrcPort << " to port " << DstPort << " Queue disabled.");
                    // No more data to produce
                    stop_requested_.store(true, std::memory_order_release);
                    break;
                }
                
                // Count iterations
                thread_metrics_.total_iterations.fetch_add(1, std::memory_order_acq_rel);
            }
            
            // Mark thread as inactive
            thread_metrics_.thread_active.store(false, std::memory_order_release);
            
            /**
             * @brief Executes the Log4 Cxx Trace operation.
             *
             * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
             * @return Method-specific result, status, or produced value when the signature provides one.
             */
            LOG4CXX_TRACE(log4cxx::Logger::getLogger("graph.edge"), "Edge from port " << SrcPort << " to port " << DstPort << " stopped.");
            cv_.notify_all();
       }

        std::shared_ptr<SrcNode> src_;
        std::shared_ptr<DstNode> dst_;
        std::size_t buffer_size_ = 8;                           ///< Queue buffer size from EdgeConfig
        std::atomic<bool> stop_requested_{false};
        std::thread thread_;
        std::mutex mtx_;
        std::condition_variable cv_;
        
        // Interface pointers resolved at startup (avoids dynamic_cast in hot loop)
        using InputPortType = typename DstNode::template InputPortType<DstPort>;
        IInputFn<InputPortType>* enqueue_fn_iinput_ = nullptr;           // For regular input ports
        IInputCommonFn<InputPortType>* enqueue_fn_common_ = nullptr;     // For MergeNode unified queue
        
        // Thread metrics member for recording transfer operations
        mutable ThreadMetrics thread_metrics_;
    };

    // ===================================================================================
    // MergeNodeBase - Multi-input merge node with unified queue
    // -----------------------------------------------------------------------------------
    // Combines N input ports (all CommonInput type) into single output port.
    // Uses unified queue and dedicated merge thread.
    // Follows NodeLifecycleMixin CRTP pattern for lifecycle management.
    // ===================================================================================

    /**
     * @brief Base class for merge nodes with N inputs and single output
     * @tparam N Number of input ports
     * @tparam CommonInput Type shared by all N input ports
     * @tparam OutputType Type of the single output port
     *
     * MergeNodeBase combines:
     * - N input ports of identical type (CommonInput)
     * - Single output port (OutputType)
     * - Unified merge queue receiving from all N input ports
     * - Dedicated merge processing thread
     * - NodeLifecycleMixin CRTP lifecycle management
     * - ExpandInputPorts pack expansion for N input port bases
     */
    template <std::size_t N, typename CommonInput, typename OutputT>
/**
 * @class MergeNodeBase
 * @brief Merge Node Base graph node.
 *
 * @details Implements a GraphX node boundary with typed inputs, outputs, configuration, and lifecycle hooks. The node participates in graph execution through the standard port and message contracts.
 */
    /**
     * @class MergeNodeBase
     * @brief Merge Node Base graph node.
     *
     * @details Implements a GraphX node boundary with typed inputs, outputs, configuration, and lifecycle hooks. The node participates in graph execution through the standard port and message contracts.
     */
    class MergeNodeBase
        : public IMergeFn<CommonInput, OutputT>,
          public ExpandInputPorts<N, CommonInput>::InputBases,
          public NodeLifecycleMixin<MergeNodeBase<N, CommonInput, OutputT>>,
          public IOutputFn<Port<OutputT, 0>>
    {
    public:
        using input_type = CommonInput;
        using output_type = OutputT;
        static constexpr std::size_t NInputs = N;
        static constexpr std::size_t NOutputs = 1;

        template <std::size_t PortID>
        using InputType = CommonInput;
        template <std::size_t PortID>
        using OutputType = OutputT;
    
        /// Constructor that initializes the unified queue and passes it to ExpandInputPorts
        explicit MergeNodeBase()
            : ExpandInputPorts<N, CommonInput>::InputBases(unified_queue_) {
        }

        /**
         * @brief Releases resources owned by Merge Node Base.
         *
         * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
         */
        virtual ~MergeNodeBase() {
            try {
                /**
                 * @brief Updates the Stop Request.
                 *
                 * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
                 * @param true Input or configuration value consumed by the method.
                 * @return Method-specific result, status, or produced value when the signature provides one.
                 */
                SetStopRequest(true);
                if (thread_.joinable()) {
                    thread_.join();
                }
            } catch (const std::exception& e) {
                LOG4CXX_ERROR(log4cxx::Logger::getLogger("graph.node"),
                              "MergeNodeBase cleanup failed: " << e.what());
            }
        }

        // ====================================================================
        // PUBLIC LIFECYCLE INTERFACE (from INode via NodeLifecycleMixin)
        // ====================================================================

        /**
         * @brief Returns the Lifecycle State.
         *
         * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
         * @return Method-specific result, status, or produced value when the signature provides one.
         */
        virtual LifecycleState GetLifecycleState() const override {
            LOG4CXX_TRACE(log4cxx::Logger::getLogger("graph.node"), 
                          "MergeNodeBase GetLifecycleState.");
            return this->GetLifecycleStateImpl();
        }

        /**
         * @brief Performs the Init lifecycle step.
         *
         * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
         * @return Method-specific result, status, or produced value when the signature provides one.
         */
        virtual bool Init() override {
            LOG4CXX_TRACE(log4cxx::Logger::getLogger("graph.node"), 
                          "MergeNodeBase Init.");
            return this->InitImpl();
        }

        /**
         * @brief Performs the Start lifecycle step.
         *
         * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
         * @return Method-specific result, status, or produced value when the signature provides one.
         */
        virtual bool Start() override {
            LOG4CXX_TRACE(log4cxx::Logger::getLogger("graph.node"), 
                          "MergeNodeBase Start.");
            return this->StartImpl();
        }

        /**
         * @brief Performs the Stop lifecycle step.
         *
         * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
         * @return Method-specific result, status, or produced value when the signature provides one.
         */
        virtual void Stop() override {
            LOG4CXX_TRACE(log4cxx::Logger::getLogger("graph.node"), 
                          "MergeNodeBase Stop.");
            this->StopImpl();
        }

        /**
         * @brief Performs the Join lifecycle step.
         *
         * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
         * @return Method-specific result, status, or produced value when the signature provides one.
         */
        virtual void Join() override {
            LOG4CXX_TRACE(log4cxx::Logger::getLogger("graph.node"), 
                          "MergeNodeBase Join.");
            this->JoinImpl();
        }

        /**
         * @brief Executes the Join With Timeout operation.
         *
         * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
         * @param timeout_ms Input or configuration value consumed by the method.
         * @return Method-specific result, status, or produced value when the signature provides one.
         */
        virtual bool JoinWithTimeout(std::chrono::milliseconds timeout_ms) override {
            LOG4CXX_TRACE(log4cxx::Logger::getLogger("graph.node"), 
                          "MergeNodeBase JoinWithTimeout.");
            return this->JoinWithTimeoutImpl(timeout_ms);
        }

        // ====================================================================
        // PORT METADATA
        // ====================================================================

        /**
         * @brief Creates or builds the object described by Build Inputs.
         *
         * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
         * @return Method-specific result, status, or produced value when the signature provides one.
         */
        static consteval auto build_inputs() {
            return ExpandInputPorts<N, CommonInput>::build_all_metadata();
        }

        /**
         * @brief Creates or builds the object described by Build Outputs.
         *
         * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
         * @return Method-specific result, status, or produced value when the signature provides one.
         */
        static consteval auto build_outputs() {
            return build_port_table<PortDirection::Output>(
                TypeList<Port<OutputT, 0>>{});
        }

        static constexpr auto input_table = build_inputs();
        static constexpr auto output_table = build_outputs();

        /**
         * @brief Executes the Input Ports operation.
         *
         * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
         * @return Method-specific result, status, or produced value when the signature provides one.
         */
        virtual std::span<const PortInfo> InputPorts() const final {
            return input_table;
        }

        /**
         * @brief Executes the Output Ports operation.
         *
         * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
         * @return Method-specific result, status, or produced value when the signature provides one.
         */
        virtual std::span<const PortInfo> OutputPorts() const final {
            return output_table;
        }

        int GetOutputPortCount() const 
        {
            return 1;
        }

        int GetInputPortCount() const 
        {
            return N;
        }   

       void SetInputComparator(std::function<bool(const CommonInput &, const CommonInput &)> comp)
        {
            // Optional: set a comparator for merging logic
            unified_queue_.SetComparator(comp);
        }

        // void SetOutputComparator(std::function<bool(const OutputT &, const OutputT &)> comp)
        // {
        //     // Optional: set a comparator for merging logic
        //     //output_queue_.SetComparator(comp);
        // }

        // ====================================================================
        // TYPE ACCESSORS (for AddEdge type verification)
        // ====================================================================

        template <std::size_t PortID>
        using InputPortType = typename std::enable_if<
            PortID < N,
            Port<CommonInput, PortID>
        >::type;

        template <std::size_t PortID>
        using OutputPortType = typename std::enable_if<
            PortID == 0,
            Port<OutputT, 0>
        >::type;

    private:
        // ====================================================================
        // PRIVATE STATE
        // ====================================================================

        // Unified queue that all N input ports feed into
        core::ActiveQueue<CommonInput> unified_queue_;

        // Merge processing thread
        std::thread thread_;
        std::atomic<bool> stop_requested_{false};
        std::mutex mtx_;
        std::condition_variable cv_;

        // ====================================================================
        // LIFECYCLE IMPLEMENTATION (via NodeLifecycleMixin CRTP)
        // ====================================================================

        friend class NodeLifecycleMixin<MergeNodeBase<N, CommonInput, OutputT>>;

        /**
         * @brief Performs the Init Ports Impl lifecycle step.
         *
         * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
         * @return Method-specific result, status, or produced value when the signature provides one.
         */
        bool InitPortsImpl() {
            LOG4CXX_TRACE(log4cxx::Logger::getLogger("graph.node"),
                          "MergeNodeBase InitPortsImpl: setting up unified queue with "
                          << N << " input ports.");
            
            // Initialize output port
            if (!IOutputFn<Port<OutputT, 0>>::Init()) {
                LOG4CXX_ERROR(log4cxx::Logger::getLogger("graph.node"),
                              "MergeNodeBase failed to init output port.");
                return false;
            }

            // Unified queue is managed internally, initialized implicitly
            return true;
        }

        /**
         * @brief Executes the Start Ports Impl operation.
         *
         * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
         * @return Method-specific result, status, or produced value when the signature provides one.
         */
        bool StartPortsImpl() {
            LOG4CXX_TRACE(log4cxx::Logger::getLogger("graph.node"),
                          "MergeNodeBase StartPortsImpl: launching merge thread.");
            
            // Enable output queue
            IOutputFn<Port<OutputT, 0>>::EnableQueue();
            
            // Launch merge thread
            auto t = [this]() -> void {
                /**
                 * @brief Executes the Merge Thread Func operation.
                 *
                 * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
                 * @return Method-specific result, status, or produced value when the signature provides one.
                 */
                MergeThreadFunc();
            };
            thread_ = std::thread(t);
            return true;
        }

        /**
         * @brief Executes the Stop Ports Impl operation.
         *
         * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
         * @return Method-specific result, status, or produced value when the signature provides one.
         */
        void StopPortsImpl() {
            LOG4CXX_TRACE(log4cxx::Logger::getLogger("graph.node"),
                          "MergeNodeBase StopPortsImpl: signaling merge thread.");
            
            // Signal merge thread to stop
            /**
             * @brief Updates the Stop Request.
             *
             * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
             * @param true Input or configuration value consumed by the method.
             * @return Method-specific result, status, or produced value when the signature provides one.
             */
            SetStopRequest(true);
            unified_queue_.Disable();
            // Disable output queue
            IOutputFn<Port<OutputT, 0>>::DisableQueue();
            IOutputFn<Port<OutputT, 0>>::Stop();
        }

        /**
         * @brief Executes the Join Ports Impl operation.
         *
         * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
         * @return Method-specific result, status, or produced value when the signature provides one.
         */
        void JoinPortsImpl() {
            LOG4CXX_TRACE(log4cxx::Logger::getLogger("graph.node"),
                          "MergeNodeBase JoinPortsImpl: waiting for merge thread.");
            
            // Wait for merge thread to exit
            if (thread_.joinable()) {
                thread_.join();
            }
            
            // Wait for output port
            //IOutputFn<Port<OutputT, 0>>::Join();
        }

        /**
         * @brief Executes the Join With Timeout Ports Impl operation.
         *
         * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
         * @param timeout_ms Input or configuration value consumed by the method.
         * @return Method-specific result, status, or produced value when the signature provides one.
         */
        bool JoinWithTimeoutPortsImpl(std::chrono::milliseconds timeout_ms) {
            LOG4CXX_TRACE(log4cxx::Logger::getLogger("graph.node"),
                          "MergeNodeBase JoinWithTimeoutPortsImpl.");
            
            // Split timeout between merge thread and output port
            auto half = timeout_ms / 2;
            
            // Wait for merge thread with timeout
            if (thread_.joinable()) {
                // Wait with timeout using cv
                /**
                 * @brief Executes the Lock operation.
                 *
                 * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
                 * @param mtx_ Input or configuration value consumed by the method.
                 * @return Method-specific result, status, or produced value when the signature provides one.
                 */
                std::unique_lock<std::mutex> lock(mtx_);
                if (!cv_.wait_for(lock, half, [this] { return !thread_.joinable(); })) {
                    LOG4CXX_TRACE(log4cxx::Logger::getLogger("graph.node"),
                                 "MergeNodeBase merge thread timeout exceeded.");
                    return false;
                }
            }
            
            // Wait for output port with remaining timeout
            return IOutputFn<Port<OutputT, 0>>::JoinWithTimeout(timeout_ms / 2);
        }

        /**
         * @brief Reports whether Is Stop Requested is true.
         *
         * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
         * @return Method-specific result, status, or produced value when the signature provides one.
         */
        bool IsStopRequested() const {
            return stop_requested_.load(std::memory_order_acquire);
        }

        /**
         * @brief Updates the Stop Request.
         *
         * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
         * @param value Input or configuration value consumed by the method.
         * @return Method-specific result, status, or produced value when the signature provides one.
         */
        void SetStopRequest(bool value) {
            stop_requested_.store(value, std::memory_order_release);
        }

        // ====================================================================
        // MERGE THREAD IMPLEMENTATION
        // ====================================================================

        /**
         * @brief Executes the Merge Thread Func operation.
         *
         * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
         * @return Method-specific result, status, or produced value when the signature provides one.
         */
        void MergeThreadFunc() {
            LOG4CXX_TRACE(log4cxx::Logger::getLogger("graph.node"),
                          "MergeNodeBase MergeThreadFunc started.");
            
            while (!IsStopRequested()) {
                CommonInput event;
                
                // Dequeue from unified queue
                if (!unified_queue_.Dequeue(event)) {
                    // Queue disabled or empty with timeout
                    LOG4CXX_TRACE(log4cxx::Logger::getLogger("graph.node"),
                                  "MergeNodeBase dequeue returned false, checking stop request.");
                    if (IsStopRequested()) {
                        break;
                    }
                    continue;  // Try again
                }

                // Call user's Process() method with integral_constant<0> for output port
                auto maybe_result = this->Process(event, std::integral_constant<std::size_t, 0>{});

                // If Process returned a value, enqueue to output
                if (maybe_result.has_value()) {
                    auto& output_queue = IOutputFn<Port<OutputT, 0>>::GetQueue();
                    if (!output_queue.Enqueue(maybe_result.value())) {
                        LOG4CXX_TRACE(log4cxx::Logger::getLogger("graph.node"),
                                     "MergeNodeBase output enqueue failed (queue disabled).");
                    }
                }
            }

            LOG4CXX_TRACE(log4cxx::Logger::getLogger("graph.node"),
                          "MergeNodeBase MergeThreadFunc exited.");
        }
    };

    // ===================================================================================
    // MergeNode - Convenience Template for MergeNodeBase
    // -----------------------------------------------------------------------------------
    // Provides a cleaner interface with 4 template parameters:
    // - N: Number of input ports
    // - InputType: Type of input messages
    // - OutputT: Type of output messages
    // - Derived: The derived node class (CRTP pattern)
    // 
    // This is just a type alias that maps to MergeNodeBase<N, InputType, OutputT>
    // The Derived class parameter is for CRTP but not used in the template itself.
    // ===================================================================================
    
    /**
     * @brief Convenience template for merge nodes
     * 
     * Template Parameters:
     * - N: Number of input ports
     * - InputType: Type of all input messages
     * - OutputT: Type of output messages  
     * - Derived: The derived class implementing this node (CRTP pattern)
     *
     * Usage:
     * @code
     *   class MyMerge : public MergeNode<3, Message, Result, MyMerge> {
     *       std::optional<Result> Process(const Message&, 
     *                                      std::integral_constant<0>) override {
     *           // Process and return result
     *       }
     *   };
     * @endcode
     */
    template <std::size_t N, typename InputType, typename OutputT, typename Derived>
/**
 * @class MergeNode
 * @brief Merge Node graph node.
 *
 * @details Implements a GraphX node boundary with typed inputs, outputs, configuration, and lifecycle hooks. The node participates in graph execution through the standard port and message contracts.
 */
    /**
     * @class MergeNode
     * @brief Merge Node graph node.
     *
     * @details Implements a GraphX node boundary with typed inputs, outputs, configuration, and lifecycle hooks. The node participates in graph execution through the standard port and message contracts.
     */
    class MergeNode : public MergeNodeBase<N, InputType, OutputT>, public NamedType<Derived> {
    public:
        /**
         * @brief Releases resources owned by Merge Node.
         *
         * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
         */
        virtual ~MergeNode() = default;

        /**
         * @brief Get input port metadata for visualization
         * 
         * Override in derived classes to provide runtime port information.
         * Used by the NodeSerializer template to export port list to JSON.
         * 
         * @return Vector of PortMetadata for all input ports
         */
        virtual std::vector<PortMetadata> GetInputPortMetadata() const override {
            return MakePortMetadataForDirection<Derived, PortDirection::Input>();
        }    

        /**
         * @brief Get output port metadata for visualization
         * 
         * Override in derived classes to provide runtime port information.
         * Used by the NodeSerializer template to export port list to JSON.
         * 
         * @return Vector of PortMetadata for all output ports
         */
        virtual std::vector<PortMetadata> GetOutputPortMetadata() const override {
            return MakePortMetadataForDirection<Derived, PortDirection::Output>();
        }

    };

    // Forward declaration for trait specialization
    /**
     * @struct NodeToINodeConverter
     * @brief Node To Inode Converter data record.
     *
     * @details Groups related fields passed through GraphX runtime, DSP, or GPU boundaries. The type is intentionally documented as a value object so callers understand ownership, lifetime, and validation expectations.
     */
    template <typename T>
    struct NodeToINodeConverter {
        // Default: direct conversion
        /**
         * @brief Executes the Convert operation.
         *
         * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
         * @param node Input or configuration value consumed by the method.
         * @return Method-specific result, status, or produced value when the signature provides one.
         */
        static std::shared_ptr<INode> Convert(std::shared_ptr<T> node) {
            return std::dynamic_pointer_cast<INode>(node);
        }
    };

} // namespace graph

