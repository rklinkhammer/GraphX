/**
 * @file Lifecycle.hpp
 * @brief GraphX source file.
 */

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

/**
 * @file Lifecycle.hpp
 * @brief CRTP mixin for node lifecycle management
 * 
 * Provides default implementations of Init, Start, Stop, and Join lifecycle
 * methods for nodes using the Curiously Recurring Template Pattern (CRTP).
 */

#pragma once

#include <atomic>
#include <chrono>
#include <log4cxx/logger.h>
#include "graph/INode.hpp"

namespace graph
{

    /**
     * @brief CRTP mixin providing default lifecycle implementations for nodes
     * @tparam Derived The derived node class (using CRTP pattern)
     *
     * Eliminates boilerplate lifecycle code in SourceNodeBase, SinkNodeBase, etc.
     * Derived classes should implement:
     * - bool InitPortsImpl()
     * - bool StartPortsImpl()
     * - void StopPortsImpl()
     * - void JoinPortsImpl()
     * - bool JoinWithTimeoutPortsImpl(std::chrono::milliseconds)
     *
     * Enforces single-start semantics: Start() returns false if already started
     * and not yet joined. Join() must be called to reset the started state.
     *
     * This mixin automatically implements all INode virtual lifecycle methods with
     * appropriate logging. Derived classes inherit from this mixin to get the complete
     * INode interface implementation without duplicating lifecycle code.
     */
    template <typename Derived>
/**
 * @class NodeLifecycleMixin
 * @brief NodeLifecycleMixin class.
 */
/**
 * @class NodeLifecycleMixin
 * @brief Node lifecycle mixin implementation for GraphX.
 */
    class NodeLifecycleMixin : public INode
    {
     protected:

        ~NodeLifecycleMixin() = default;

        enum LifecycleState GetLifecycleStateImpl() const {
            return lifecycle_state_;
        }

        bool InitImpl()
        {
            if (static_cast<Derived *>(this)->InitPortsImpl()) {
                lifecycle_state_ = LifecycleState::Initialized;
                return true;
            } else {
                lifecycle_state_ = LifecycleState::Uninitialized;
                return false;
            }   
        }

        bool StartImpl()
        {
            // Enforce single-start semantics: don't allow Start if already started
            if (started_.exchange(true))
            {
                return false; // Already started and not yet joined
            }
            if (static_cast<Derived *>(this)->StartPortsImpl()) {
                lifecycle_state_ = LifecycleState::Started;
                return true;
            } else {
                // Start failed, reset started state
                return false;
            }
        }

        void StopImpl()
        {
            static_cast<Derived *>(this)->StopPortsImpl();
            lifecycle_state_ = LifecycleState::Stopped;
        }

        void JoinImpl()
        {
            static_cast<Derived *>(this)->JoinPortsImpl();
            lifecycle_state_ = LifecycleState::Joined;
            // Reset started state after join completes
            started_ = false;
        }

        bool JoinWithTimeoutImpl(std::chrono::milliseconds timeout_ms)
        {
            if(static_cast<Derived *>(this)->JoinWithTimeoutPortsImpl(timeout_ms)) {
                lifecycle_state_ = LifecycleState::Joined;
                return true;
            } else {
                return false;
            }
        }

    public:
        // Virtual INode interface implementations (auto-generated for all derived classes)
        // These methods provide logging and forwarding to the Impl methods above.
        // Derived classes should NOT override these - override the *PortsImpl() methods instead.

        /**
         * @brief Get current lifecycle state with automatic logging
         */
        virtual LifecycleState GetLifecycleState() const override {
            auto logger = log4cxx::Logger::getLogger("graph.node");
            LOG4CXX_TRACE(logger, GetDerivedClassName() + " GetLifecycleState");
            return this->GetLifecycleStateImpl();
        }

        /**
         * @brief Initialize the node with automatic logging
         */
        virtual bool Init() override {
            auto logger = log4cxx::Logger::getLogger("graph.node");
            LOG4CXX_TRACE(logger, GetDerivedClassName() + " Init");
            return this->InitImpl();
        }

        /**
         * @brief Start the node with automatic logging
         */
        virtual bool Start() override {
            auto logger = log4cxx::Logger::getLogger("graph.node");
            LOG4CXX_TRACE(logger, GetDerivedClassName() + " Start");
            return this->StartImpl();
        }

        /**
         * @brief Stop the node with automatic logging
         */
        virtual void Stop() override {
            auto logger = log4cxx::Logger::getLogger("graph.node");
            LOG4CXX_TRACE(logger, GetDerivedClassName() + " Stop");
            this->StopImpl();
        }

        /**
         * @brief Join the node with automatic logging
         */
        virtual void Join() override {
            auto logger = log4cxx::Logger::getLogger("graph.node");
            LOG4CXX_TRACE(logger, GetDerivedClassName() + " Join");
            this->JoinImpl();
        }

        /**
         * @brief Join with timeout with automatic logging
         */
        virtual bool JoinWithTimeout(std::chrono::milliseconds timeout_ms) override {
            auto logger = log4cxx::Logger::getLogger("graph.node");
            LOG4CXX_TRACE(logger, GetDerivedClassName() + " JoinWithTimeout");
            return this->JoinWithTimeoutImpl(timeout_ms);
        }

    protected:
        /**
         * @brief Get the derived class name for logging purposes using RTTI
         * 
         * Uses type_info::name() which is available at runtime for polymorphic types.
         * This provides accurate class names without requiring manual overrides.
         * 
         * Example: For SourceNodeBase<...>, returns "SourceNodeBase"
         */
        virtual std::string GetDerivedClassName() const {
            // Use RTTI to get the actual class name at runtime
            return typeid(*static_cast<const Derived*>(this)).name();
        }

    private:
        std::atomic<bool> started_{false};
        enum LifecycleState lifecycle_state_{LifecycleState::Uninitialized};


    };

} // namespace graph

