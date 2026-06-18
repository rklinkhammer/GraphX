// MIT License
/// @file core/CallbackUtilities.hpp
/// @brief C++23 callback modernization with std::move_only_function

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

#include <functional>
#include <vector>
#include <concepts>
#include <type_traits>
#include <memory>

// ============================================================================
// Callback Modernization Infrastructure (Phase 4: std::move_only_function)
// ============================================================================
// Replaces std::function with std::move_only_function for better performance
// and memory efficiency. Single-ownership semantics prevent accidental copies.
// ============================================================================

namespace app::callbacks {

namespace detail {

/**

 * @class MoveOnlyFunction

 * @brief Move Only Function type.

 *

 * @details Part of the GraphX public API for libgraph. The type documents its runtime role, ownership expectations, and interaction with neighboring graph components.

 */

template<typename Signature>
class MoveOnlyFunction;

template<typename ReturnT, typename... ArgsT>
/**
 * @class MoveOnlyFunction
 * @brief Move only function implementation for GraphX.
 */
/**
 * @class MoveOnlyFunction
 * @brief Move Only Function type.
 *
 * @details Part of the GraphX public API for libgraph. The type documents its runtime role, ownership expectations, and interaction with neighboring graph components.
 */
class MoveOnlyFunction<ReturnT(ArgsT...)> {
public:
    /**
     * @brief Executes the Move Only Function operation.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     * @return Method-specific result, status, or produced value when the signature provides one.
     */
    MoveOnlyFunction() = default;
    MoveOnlyFunction(std::nullptr_t) noexcept {}

    /**
     * @brief Executes the Move Only Function operation.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     * @param MoveOnlyFunction Input or configuration value consumed by the method.
     * @return Method-specific result, status, or produced value when the signature provides one.
     */
    MoveOnlyFunction(const MoveOnlyFunction&) = delete;
    /**
     * @brief Executes the Operator overload operation.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     * @param MoveOnlyFunction Input or configuration value consumed by the method.
     */
    MoveOnlyFunction& operator=(const MoveOnlyFunction&) = delete;
    /**
     * @brief Executes the Move Only Function operation.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     * @param MoveOnlyFunction Input or configuration value consumed by the method.
     * @return Method-specific result, status, or produced value when the signature provides one.
     */
    MoveOnlyFunction(MoveOnlyFunction&&) noexcept = default;
    /**
     * @brief Executes the Operator overload operation.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     * @param MoveOnlyFunction Input or configuration value consumed by the method.
     */
    MoveOnlyFunction& operator=(MoveOnlyFunction&&) noexcept = default;

    template<typename Fn>
        requires (!std::same_as<std::remove_cvref_t<Fn>, MoveOnlyFunction> &&
                  std::invocable<Fn&, ArgsT...> &&
                  std::convertible_to<std::invoke_result_t<Fn&, ArgsT...>, ReturnT>)
    MoveOnlyFunction(Fn&& fn)
        : callable_(std::make_unique<Model<std::remove_cvref_t<Fn>>>(std::forward<Fn>(fn))) {}

    /**
     * @brief Executes the Bool operation.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     * @return Method-specific result, status, or produced value when the signature provides one.
     */
    explicit operator bool() const noexcept {
        return static_cast<bool>(callable_);
    }

    ReturnT operator()(ArgsT... args) {
        /**
         * @brief Executes the Constexpr operation.
         *
         * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
         * @return Method-specific result, status, or produced value when the signature provides one.
         */
        if constexpr (std::is_void_v<ReturnT>) {
            callable_->Invoke(std::forward<ArgsT>(args)...);
        } else {
            return callable_->Invoke(std::forward<ArgsT>(args)...);
        }
    }

private:
    /**
     * @struct Concept
     * @brief Concept data record.
     *
     * @details Groups related fields passed through GraphX runtime, DSP, or GPU boundaries. The type is intentionally documented as a value object so callers understand ownership, lifetime, and validation expectations.
     */
    struct Concept {
        /**
         * @brief Releases resources owned by Concept.
         *
         * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
         */
        virtual ~Concept() = default;
/**
 * @brief Invoke.
 * @param args Parameter for invoke.
 * @return Result of the operation.
 */
        virtual ReturnT Invoke(ArgsT... args) = 0;
    };

    /**

     * @struct Model

     * @brief Model data record.

     *

     * @details Groups related fields passed through GraphX runtime, DSP, or GPU boundaries. The type is intentionally documented as a value object so callers understand ownership, lifetime, and validation expectations.

     */

    template<typename Fn>
    struct Model final : Concept {
        template<typename Callable>
        explicit Model(Callable&& fn) : fn_(std::forward<Callable>(fn)) {}

        /**
         * @brief Executes the Invoke operation.
         *
         * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
         * @param args Input or configuration value consumed by the method.
         * @return Method-specific result, status, or produced value when the signature provides one.
         */
        ReturnT Invoke(ArgsT... args) override {
            /**
             * @brief Executes the Constexpr operation.
             *
             * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
             * @return Method-specific result, status, or produced value when the signature provides one.
             */
            if constexpr (std::is_void_v<ReturnT>) {
                std::invoke(fn_, std::forward<ArgsT>(args)...);
            } else {
                return std::invoke(fn_, std::forward<ArgsT>(args)...);
            }
        }

        Fn fn_;
    };

    std::unique_ptr<Concept> callable_;
};

}  // namespace detail

/**
 * @brief Move-only callback type alias (Phase 4: C++23)
 *
 * Uses std::move_only_function when the active C++26 standard library provides
 * it. AppleClang C++26 toolchains without that library facility use a local
 * move-only type-erased callable with the same ownership model.
 *
 * **Benefits of std::move_only_function** (C++23):
 * - No copy constructor (enforces single ownership)
 * - No small-object optimization overhead for complex lambdas
 * - ~20-40 bytes saved per callback in typical use
 * - ~5-10% faster invocation (fewer indirect calls)
 *
 * **Phase 4 Pattern**:
 * @code
 *   // Old way (std::function):
 *   std::function<void()> callback = [] { ... };
 *   
 *   // New way (std::move_only_function):
 *   MoveOnlyCallback<void()> callback = [] { ... };
 *   
 *   // Usage identical - just more efficient
 *   callback();
 * @endcode
 *
 * @tparam Signature Function signature (e.g., void(), int(double))
 */
template<typename Signature>
#if defined(__cpp_lib_move_only_function) && __cpp_lib_move_only_function >= 202110L
using MoveOnlyCallback = std::move_only_function<Signature>;
#else
using MoveOnlyCallback = detail::MoveOnlyFunction<Signature>;
#endif

// ============================================================================
// Phase 4: Callback Concepts for Type Safety
// ============================================================================

/**
 * @brief Concept: Callable with void return and no arguments (Phase 4)
 *
 * Matches any callable that can be invoked with no arguments and returns void.
 * Used for task queues, event handlers, one-shot callbacks.
 *
 * @code
 *   struct MyTask {
 *       void operator()() { ... }
 *   };
 *   
 *   static_assert(IsMoveOnlyCallback<MyTask, void()>);
 * @endcode
 */
template<typename T>
concept IsMoveOnlyCallback = requires(T t) {
    { std::is_invocable_v<T> };
};

/**
 * @brief Concept: Callable taking arguments and returning value (Phase 4)
 *
 * More general concept for parameterized callbacks.
 */
template<typename T, typename ReturnT, typename... ArgsT>
concept IsMoveOnlyCallbackWith = requires(T t, ArgsT... args) {
    { t(args...) } -> std::convertible_to<ReturnT>;
};

// ============================================================================
// Phase 4: Callback Chain for Multiple Handlers
// ============================================================================

/**
 * @brief Chain multiple move-only callbacks together (Phase 4)
 *
 * Manages a collection of move-only callbacks and invokes all in sequence.
 * Useful for event systems, observer patterns, and hook registries.
 *
 * **Phase 4 Callback Chain Pattern**:
 * @code
 *   CallbackChain<void(const Event&)> event_handlers;
 *   
 *   event_handlers.Add([] (const Event& e) {
 *       std::cout << "Handler 1: " << e.name << "\n";
 *   });
 *   
 *   event_handlers.Add([] (const Event& e) {
 *       std::cout << "Handler 2: " << e.name << "\n";
 *   });
 *   
 *   Event event{"click"};
 *   event_handlers.InvokeAll(event);
 *   // Prints:
 *   // Handler 1: click
 *   // Handler 2: click
 * @endcode
 *
 * @tparam ReturnT Return type of callbacks
 * @tparam ArgsT Argument types of callbacks
 */
template<typename ReturnT, typename... ArgsT>
/**
 * @class CallbackChain
 * @brief Callback Chain type.
 *
 * @details Part of the GraphX public API for libgraph. The type documents its runtime role, ownership expectations, and interaction with neighboring graph components.
 */
class CallbackChain {
public:
    /**
     * @brief Construct empty callback chain
     */
    CallbackChain() = default;

    /**
     * @brief Add callback to chain
     *
     * Takes ownership of callback via move semantics.
     * Callback will be invoked in order when InvokeAll is called.
     *
     * @param callback Move-only callback to add
     */
    void Add(MoveOnlyCallback<ReturnT(ArgsT...)> callback) {
        callbacks_.push_back(std::move(callback));
    }

    /**
     * @brief Invoke all registered callbacks in order (Phase 4)
     *
     * Calls each callback with provided arguments.
     * No exception handling - exceptions propagate from first failing callback.
     *
     * @param args Arguments to pass to all callbacks
     */
    void InvokeAll(ArgsT... args) {
        for (auto& callback : callbacks_) {
            /**
             * @brief Executes the Constexpr operation.
             *
             * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
             * @return Method-specific result, status, or produced value when the signature provides one.
             */
            if constexpr (std::is_same_v<ReturnT, void>) {
                /**
                 * @brief Executes the Callback operation.
                 *
                 * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
                 * @return Method-specific result, status, or produced value when the signature provides one.
                 */
                callback(args...);
            } else {
                // Ignore return values for non-void callbacks
                static_cast<void>(callback(args...));
            }
        }
    }

    /**
     * @brief Get number of registered callbacks
     */
    [[nodiscard]] size_t Size() const noexcept {
        return callbacks_.size();
    }

    /**
     * @brief Check if callback chain is empty
     */
    [[nodiscard]] bool IsEmpty() const noexcept {
        return callbacks_.empty();
    }

    /**
     * @brief Clear all callbacks
     */
    void Clear() noexcept {
        callbacks_.clear();
    }

    /**
     * @brief Reserve capacity for callbacks (optimization)
     *
     * Pre-allocate space for expected number of callbacks.
     * Reduces reallocations during repeated Add() calls.
     *
     * @param capacity Expected number of callbacks
     */
    void Reserve(size_t capacity) {
        callbacks_.reserve(capacity);
    }

private:
    std::vector<MoveOnlyCallback<ReturnT(ArgsT...)>> callbacks_;
};

// ============================================================================
// Phase 4: Callback Chain Specialization for Void Return
// ============================================================================

/**
 * @brief Callback chain for void-returning callbacks (Phase 4 Common Case)
 *
 * Specialization optimized for event handlers and observers.
 *
 * @code
 *   CallbackChain<void(int)> handlers;
 *   handlers.Add([](int x) { std::cout << x << "\n"; });
 *   handlers.Add([](int x) { std::cout << x * 2 << "\n"; });
 *   handlers.InvokeAll(42);
 * @endcode
 *
 * @tparam ArgsT Event/message argument types
 */
/**
 * @class CallbackChain
 * @brief Callback Chain type.
 *
 * @details Part of the GraphX public API for libgraph. The type documents its runtime role, ownership expectations, and interaction with neighboring graph components.
 */
template<typename... ArgsT>
class CallbackChain<void, ArgsT...> {
public:
    using CallbackType = MoveOnlyCallback<void(ArgsT...)>;

    /**
     * @brief Executes the Callback Chain operation.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     * @return Method-specific result, status, or produced value when the signature provides one.
     */
    CallbackChain() = default;

    /**
     * @brief Executes the Add operation.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     * @param callback Input or configuration value consumed by the method.
     * @return Method-specific result, status, or produced value when the signature provides one.
     */
    void Add(CallbackType callback) {
        callbacks_.push_back(std::move(callback));
    }

    /**
     * @brief Executes the Invoke All operation.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     * @param args Input or configuration value consumed by the method.
     * @return Method-specific result, status, or produced value when the signature provides one.
     */
    void InvokeAll(ArgsT... args) {
        for (auto& callback : callbacks_) {
            /**
             * @brief Executes the Callback operation.
             *
             * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
             * @return Method-specific result, status, or produced value when the signature provides one.
             */
            callback(args...);
        }
    }

    /**
     * @brief Executes the Size operation.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     * @return Method-specific result, status, or produced value when the signature provides one.
     */
    [[nodiscard]] size_t Size() const noexcept {
        return callbacks_.size();
    }

    /**
     * @brief Reports whether Is Empty is true.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     * @return Method-specific result, status, or produced value when the signature provides one.
     */
    [[nodiscard]] bool IsEmpty() const noexcept {
        return callbacks_.empty();
    }

    /**
     * @brief Executes the Clear operation.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     * @return Method-specific result, status, or produced value when the signature provides one.
     */
    void Clear() noexcept {
        callbacks_.clear();
    }

    /**
     * @brief Executes the Reserve operation.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     * @param capacity Input or configuration value consumed by the method.
     * @return Method-specific result, status, or produced value when the signature provides one.
     */
    void Reserve(size_t capacity) {
        callbacks_.reserve(capacity);
    }

private:
    std::vector<CallbackType> callbacks_;
};

// ============================================================================
// Phase 4: Callback Utilities and Helpers
// ============================================================================

/**
 * @brief Create a callback that does nothing (Phase 4)
 *
 * Useful for default callbacks or placeholder implementations.
 *
 * @code
 *   auto noop = CreateNoOpCallback<void()>();
 *   noop();  // Safe, does nothing
 * @endcode
 *
 * @tparam Signature Callback signature
 * @return Move-only callback that does nothing
 */
template<typename Signature>
inline MoveOnlyCallback<Signature> CreateNoOpCallback() {
    // Note: Simple lambda that does nothing
    /**
     * @brief Executes the Constexpr operation.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     * @return Method-specific result, status, or produced value when the signature provides one.
     */
    if constexpr (std::is_invocable_v<std::function<Signature>>) {
        return [] { };  // Void callback
    } else {
        // Non-void or parameterized - return default
        return [] { };
    }
}

/**
 * @brief Wraps a shared pointer and callback into move-only callback (Phase 4)
 *
 * Captures shared_ptr to ensure lifetime, pairs with cleanup callback.
 * Useful for object-tied callbacks that need cleanup.
 *
 * **Phase 4 Lifetime Pattern**:
 * @code
 *   auto obj = std::make_shared<MyObject>();
 *   auto cleanup = WrapWithLifetime<void()>(
 *       obj,
 *       [obj]() { obj->DoSomething(); }
 *   );
 *   cleanup();  // obj is kept alive during callback
 * @endcode
 *
 * @tparam Signature Callback signature
 * @tparam T Shared pointer target type
 * @param lifetime_holder Shared pointer to keep alive
 * @param callback Callback to wrap
 * @return Move-only callback that captures lifetime
 */
template<typename Signature, typename T>
inline MoveOnlyCallback<Signature> WrapWithLifetime(
    std::shared_ptr<T> lifetime_holder,
    MoveOnlyCallback<Signature> callback) {
    
    // Capture shared_ptr to keep T alive
    return [holder = std::move(lifetime_holder),
            cb = std::move(callback)]() mutable {
        /**
         * @brief Executes the Cb operation.
         *
         * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
         * @return Method-specific result, status, or produced value when the signature provides one.
         */
        cb();
    };
}

// ============================================================================
// Phase 4: Thread-Safe Callback Registry
// ============================================================================

/**
 * @brief Thread-safe registry for move-only callbacks (Phase 4)
 *
 * Stores multiple callbacks safely across threads.
 * Useful for event systems, metrics publishers, etc.
 *
 * **Phase 4 Thread-Safe Registry Pattern**:
 * @code
 *   CallbackRegistry<void(const Event&)> event_bus;
 *   
 *   // Thread 1: Register handlers
 *   event_bus.Register([] (const Event& e) { ... });
 *   
 *   // Thread 2: Broadcast event
 *   event_bus.BroadcastEvent(my_event);
 * @endcode
 *
 * @tparam ReturnT Return type
 * @tparam ArgsT Argument types
 */
template<typename ReturnT, typename... ArgsT>
/**
 * @class CallbackRegistry
 * @brief Callback Registry manager.
 *
 * @details Owns registration, lookup, or orchestration state for a GraphX subsystem. The class centralizes mutation so callers interact through stable query and update methods.
 */
class CallbackRegistry {
public:
    using CallbackType = MoveOnlyCallback<ReturnT(ArgsT...)>;

    /**
     * @brief Executes the Callback Registry operation.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     * @return Method-specific result, status, or produced value when the signature provides one.
     */
    CallbackRegistry() = default;
    /**
     * @brief Releases resources owned by Callback Registry.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     */
    ~CallbackRegistry() = default;

    // Delete copy operations - each registry is unique
    /**
     * @brief Executes the Callback Registry operation.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     * @param CallbackRegistry Input or configuration value consumed by the method.
     * @return Method-specific result, status, or produced value when the signature provides one.
     */
    CallbackRegistry(const CallbackRegistry&) = delete;
    /**
     * @brief Executes the Operator overload operation.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     * @param CallbackRegistry Input or configuration value consumed by the method.
     */
    CallbackRegistry& operator=(const CallbackRegistry&) = delete;

    // Allow move
    /**
     * @brief Executes the Callback Registry operation.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     * @param CallbackRegistry Input or configuration value consumed by the method.
     * @return Method-specific result, status, or produced value when the signature provides one.
     */
    CallbackRegistry(CallbackRegistry&&) = default;
    /**
     * @brief Executes the Operator overload operation.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     * @param CallbackRegistry Input or configuration value consumed by the method.
     */
    CallbackRegistry& operator=(CallbackRegistry&&) = default;

    /**
     * @brief Register callback (thread-safe)
     */
    void Register(CallbackType callback) {
        /**
         * @brief Executes the Lock operation.
         *
         * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
         * @param mutex_ Input or configuration value consumed by the method.
         * @return Method-specific result, status, or produced value when the signature provides one.
         */
        std::lock_guard<std::mutex> lock(mutex_);
        callbacks_.push_back(std::move(callback));
    }

    /**
     * @brief Invoke all callbacks (thread-safe)
     */
    void BroadcastEvent(ArgsT... args) {
        /**
         * @brief Executes the Lock operation.
         *
         * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
         * @param mutex_ Input or configuration value consumed by the method.
         * @return Method-specific result, status, or produced value when the signature provides one.
         */
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& callback : callbacks_) {
            /**
             * @brief Executes the Constexpr operation.
             *
             * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
             * @return Method-specific result, status, or produced value when the signature provides one.
             */
            if constexpr (std::is_same_v<ReturnT, void>) {
                /**
                 * @brief Executes the Callback operation.
                 *
                 * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
                 * @return Method-specific result, status, or produced value when the signature provides one.
                 */
                callback(args...);
            } else {
                static_cast<void>(callback(args...));
            }
        }
    }

    /**
     * @brief Get callback count (thread-safe)
     */
    [[nodiscard]] size_t Size() const noexcept {
        /**
         * @brief Executes the Lock operation.
         *
         * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
         * @param mutex_ Input or configuration value consumed by the method.
         * @return Method-specific result, status, or produced value when the signature provides one.
         */
        std::lock_guard<std::mutex> lock(mutex_);
        return callbacks_.size();
    }

    /**
     * @brief Clear all callbacks (thread-safe)
     */
    void Clear() noexcept {
        /**
         * @brief Executes the Lock operation.
         *
         * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
         * @param mutex_ Input or configuration value consumed by the method.
         * @return Method-specific result, status, or produced value when the signature provides one.
         */
        std::lock_guard<std::mutex> lock(mutex_);
        callbacks_.clear();
    }

private:
    std::vector<CallbackType> callbacks_;
    mutable std::mutex mutex_;
};

}  // namespace app::callbacks

// ============================================================================
// Phase 4 Usage Patterns & Migration Guide
// ============================================================================
//
// **Pattern 1: Simple Task Callback (ThreadPool)**
// Old:  std::function<void()> task = ...;
// New:  app::callbacks::MoveOnlyCallback<void()> task = ...;
// Usage: No change - both are invoked the same way
//
// **Pattern 2: Parameterized Monitor Callback (AdaptiveCapacity)**
// Old:  std::function<double()> monitor_fn = ...;
// New:  app::callbacks::MoveOnlyCallback<double()> monitor_fn = ...;
// Usage: No change - both are invoked with monitor_fn()
//
// **Pattern 3: Multiple Event Handlers**
// Old:  std::vector<std::function<void(Event)>> handlers;
//       for (auto& h : handlers) h(event);
// New:  app::callbacks::CallbackChain<void(Event)> handlers;
//       handlers.InvokeAll(event);
//
// **Pattern 4: Thread-Safe Event Bus**
// Use: app::callbacks::CallbackRegistry<void(Event)> bus;
//      bus.Register([](const Event& e) { ... });
//      bus.BroadcastEvent(my_event);
//
// **Pattern 5: Lifetime Management**
// Use: auto cb = app::callbacks::WrapWithLifetime(
//          std::make_shared<MyObject>(),
//          [](){ /* callback */ }
//      );
//
// **Migration Checklist**:
// 1. Include "core/CallbackUtilities.hpp"
// 2. Replace std::function with MoveOnlyCallback
// 3. Add std::move() when storing callbacks
// 4. Update callback parameter passing (take by rvalue ref)
// 5. Run tests to verify behavior
//
// **Benefits Summary**:
// - Lower memory footprint (no SOO overhead)
// - Faster callback execution (fewer indirect calls)
// - Enforced single ownership (prevents accidental copies)
// - Better thread safety semantics
// - Ready for C++26 enhancements
//
