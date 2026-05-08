
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
#include "app/capabilities/GraphCapability.hpp"

namespace graph {

struct IExecutionPolicy {
    virtual ~IExecutionPolicy() = default;

    virtual bool OnInit(app::capabilities::GraphCapability& context) { (void)context; return true; }
    virtual bool OnStart(app::capabilities::GraphCapability& context) { (void)context; return true; }
    virtual bool OnRun(app::capabilities::GraphCapability& context) { (void)context; return true; }
    virtual void OnStop(app::capabilities::GraphCapability& context) { (void)context; }
    virtual void OnJoin(app::capabilities::GraphCapability& context) { (void)context; }
};

class ExecutionPolicyChain : public IExecutionPolicy {
public:
    explicit ExecutionPolicyChain(std::unique_ptr<IExecutionPolicy> policy,
                                  std::unique_ptr<ExecutionPolicyChain> next = nullptr)
        : policy_(std::move(policy)), next_(std::move(next)) {}

    bool OnInit(app::capabilities::GraphCapability& ctx) override {
        if (!policy_->OnInit(ctx)) return false;
        return next_ ? next_->OnInit(ctx) : true;
    }

    bool OnStart(app::capabilities::GraphCapability& ctx) override {
        if (!policy_->OnStart(ctx)) return false;
        return next_ ? next_->OnStart(ctx) : true;
    }

    bool OnRun(app::capabilities::GraphCapability& ctx) override {
        if (!policy_->OnRun(ctx)) return false;
        return next_ ? next_->OnRun(ctx) : true;
    }

    void OnStop(app::capabilities::GraphCapability& ctx) override {
        policy_->OnStop(ctx);
        if (next_) next_->OnStop(ctx);
    }

    void OnJoin(app::capabilities::GraphCapability& ctx) override {
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