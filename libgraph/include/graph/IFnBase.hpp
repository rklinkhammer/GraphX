// MIT License
//
// Copyright (c) 2025 graphlib contributors
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

#include "core/ActiveQueue.hpp"
#include "ThreadMetrics.hpp"
#include <atomic>
#include <log4cxx/logger.h>

namespace graph
{
    /**
     * @brief Base class for all port functions
     * @tparam P Port type (must have P::type for data type and P::id for port identifier)
     *
     * IFnBase provides common queue management operations for all port types.
     * It holds a reference to a queue (either owned by derived class or external).
     * Derived classes customize behavior through lifecycle callbacks (Join, JoinWithTimeout).
     *
     * Usage:
     * - IFn<P>: Owns its own queue (typical for single-instance ports)
     * - IInputCommonFn<P>: Uses external shared queue (for MergeNode's N input ports)
     */
    template <typename P>
    class IFnBase 
    {
    public:
        using T = typename P::type;                   ///< Data type for this port
        static constexpr std::size_t port_id = P::id; ///< Port identifier
        IFnBase(core::ActiveQueue<T>& queue) : queue_(queue) {}
        virtual ~IFnBase() = default;

        bool Init()
        {
            LOG4CXX_TRACE(log4cxx::Logger::getLogger("graph.node"), "IFn port " << port_id << " Init.");
            queue_.Enable();
            return true;
        }

        void EnableQueue()
        {
            queue_.Enable();
        }

        void DisableQueue()
        {
            queue_.Disable();
        }

        void Stop()
        {
            LOG4CXX_TRACE(log4cxx::Logger::getLogger("graph.node"), "IFn port " << port_id << " Stop.");
            SetStopRequest();
            queue_.Disable();
            LOG4CXX_TRACE(log4cxx::Logger::getLogger("graph.node"), "IFn port " << port_id << " Size before clear: " << queue_.Size());
        }

        // Default implementations - can be overridden in derived classes
        virtual void Join() {}

        /**
         * @brief Join with timeout - default implementation
         * @param timeout_ms Maximum milliseconds to wait for thread completion
         * @return true if thread completed within timeout, false if timeout exceeded
         */
        virtual bool JoinWithTimeout(std::chrono::milliseconds) { return true; }

        bool IsStopRequested() const
        {
            return stop_requested_.load(std::memory_order_acquire);
        }

        void ClearStopRequest()
        {
            stop_requested_.store(false, std::memory_order_release);
        }

        void SetStopRequest()
        {
            stop_requested_.store(true, std::memory_order_release);
        }

        void SetCapacity(std::size_t capacity)
        {
            queue_.SetCapacity(capacity);
        }

        core::ActiveQueue<T> &GetQueue()
        {
            return queue_;
        }

        bool Enqueue(const T &value)
        {
            return queue_.Enqueue(value);
        }

        bool Dequeue(T &value)
        {
            return queue_.Dequeue(value);
        }

    protected:
        std::atomic<bool> stop_requested_{false};
        core::ActiveQueue<T>& queue_;
    };

    /**
     * @brief Port function with owned queue
     * @tparam P Port type
     *
     * IFn creates and owns its own ActiveQueue<T>.
     * This is used by most single-instance ports (InputFn, OutputFn).
     * The queue is created at construction and managed for its lifetime.
     */
    template <typename P>
    class IFn : public IFnBase<P>
    {
    public:
        using T = typename P::type; ///< Data type for this port
        IFn() : IFnBase<P>(queue_) {}    
        virtual ~IFn() = default;

    private:
        core::ActiveQueue<T> queue_;
    };

}
