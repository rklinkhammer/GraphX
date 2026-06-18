/**
 * @file IExecutionPolicy.hpp
 * @brief Iexecution Policy Graph runtime support.
 *
 * @details Provides graph construction, node execution, ports, messages, and runtime orchestration. This file is documented for Doxygen so public APIs and test support surfaces can be browsed consistently.
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

#pragma once


#include <memory>
#include "core/ReflectionHelper.hpp"
#include "core/VariantHelper.hpp"
#include "capabilities/GraphCapability.hpp"

namespace graph {

/**

 * @struct IExecutionPolicy

 * @brief Iexecution Policy data record.

 *

 * @details Groups related fields passed through GraphX runtime, DSP, or GPU boundaries. The type is intentionally documented as a value object so callers understand ownership, lifetime, and validation expectations.

 */

struct IExecutionPolicy {
    /**
     * @brief Releases resources owned by Iexecution Policy.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     */
    virtual ~IExecutionPolicy() = default;

    virtual bool OnInit(capabilities::GraphCapability& context) { (void)context; return true; }
    virtual bool OnStart(capabilities::GraphCapability& context) { (void)context; return true; }
    virtual bool OnRun(capabilities::GraphCapability& context) { (void)context; return true; }
    virtual void OnStop(capabilities::GraphCapability& context) { (void)context; }
    virtual void OnJoin(capabilities::GraphCapability& context) { (void)context; }
};

/**
 * @class ExecutionPolicyChain
 * @brief Execution Policy Chain execution policy.
 *
 * @details Extends executor behavior at well-defined lifecycle points. Policies keep cross-cutting runtime concerns separate from graph node implementations.
 */
class ExecutionPolicyChain : public IExecutionPolicy {
public:
    explicit ExecutionPolicyChain(std::unique_ptr<IExecutionPolicy> policy,
                                  std::unique_ptr<ExecutionPolicyChain> next = nullptr)
        : policy_(std::move(policy)), next_(std::move(next)) {}

    /**
     * @brief Performs the On Init lifecycle step.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     * @param ctx Input or configuration value consumed by the method.
     * @return Method-specific result, status, or produced value when the signature provides one.
     */
    bool OnInit(capabilities::GraphCapability& ctx) override {
        if (!policy_->OnInit(ctx)) return false;
        return next_ ? next_->OnInit(ctx) : true;
    }

    /**
     * @brief Executes the On Start operation.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     * @param ctx Input or configuration value consumed by the method.
     * @return Method-specific result, status, or produced value when the signature provides one.
     */
    bool OnStart(capabilities::GraphCapability& ctx) override {
        if (!policy_->OnStart(ctx)) return false;
        return next_ ? next_->OnStart(ctx) : true;
    }

    /**
     * @brief Executes the On Run operation.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     * @param ctx Input or configuration value consumed by the method.
     * @return Method-specific result, status, or produced value when the signature provides one.
     */
    bool OnRun(capabilities::GraphCapability& ctx) override {
        if (!policy_->OnRun(ctx)) return false;
        return next_ ? next_->OnRun(ctx) : true;
    }

    /**
     * @brief Executes the On Stop operation.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     * @param ctx Input or configuration value consumed by the method.
     * @return Method-specific result, status, or produced value when the signature provides one.
     */
    void OnStop(capabilities::GraphCapability& ctx) override {
        policy_->OnStop(ctx);
        if (next_) next_->OnStop(ctx);
    }

    /**
     * @brief Executes the On Join operation.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     * @param ctx Input or configuration value consumed by the method.
     * @return Method-specific result, status, or produced value when the signature provides one.
     */
    void OnJoin(capabilities::GraphCapability& ctx) override {
        policy_->OnJoin(ctx);
        if (next_) next_->OnJoin(ctx);
    }

    /// @brief Append a policy to the end of the chain
    /// @param policy The execution policy to append
    void AppendPolicy(std::unique_ptr<IExecutionPolicy> policy) {
        if (!next_) {
            next_ = std::make_unique<ExecutionPolicyChain>(std::move(policy));
        } else {
            next_->AppendPolicy(std::move(policy));
        }
    }

    /// @brief Append a policy chain to the end of this chain
    /// @param next_chain The policy chain to append
    void AppendNext(std::unique_ptr<ExecutionPolicyChain> next_chain) {
        if (!next_) {
            next_ = std::move(next_chain);
        } else {
            next_->AppendNext(std::move(next_chain));
        }
    }

private:
    std::unique_ptr<IExecutionPolicy> policy_;
    std::unique_ptr<ExecutionPolicyChain> next_;
};
}  // namespace graph
