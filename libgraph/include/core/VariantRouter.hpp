/**
 * @file VariantRouter.hpp
 * @brief Variant Router Graph runtime support.
 *
 * @details Provides core utility types used by the graph runtime. This file is documented for Doxygen so public APIs and test support surfaces can be browsed consistently.
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


#pragma once

#include <variant>
#include <functional>
#include <map>
#include <typeinfo>
#include <utility>

namespace graph {

// ===================================================================================
// VariantRouter - Generic Type-Based Dispatcher
// -----------------------------------------------------------------------------------
// Template-based routing for arbitrary std::variant types using C++17 fold expressions.
// Zero overhead: dispatch is expanded at compile-time.
// ===================================================================================

/**
 * @brief Generic type-based dispatcher for std::variant payloads
 *
 * @tparam Variant The std::variant type to dispatch over
 *         (e.g., std::variant<TypeA, TypeB, TypeC>)
 *
 * Provides type-safe handler registration and polymorphic dispatch for ANY variant type.
 * Adding new variant types requires NO changes to router code - just register handlers
 * for the new types.
 *
 * **Design Benefits**:
 * - Decoupled from specific types: Works with any std::variant
 * - Extensible without modification: No hardcoded type checks
 * - Type-safe: Compile-time type checking via templates
 * - Zero overhead: Fold expressions expand at compile-time
 * - Header-only: Easy integration across projects
 *
 * **Handler Dispatch Mechanism**:
 * Uses C++17 fold expressions to automatically expand dispatch checks
 * for all types in the variant at compile-time. Each Dispatch() call
 * iterates through variant alternatives using std::get_if<T>() and
 * calls the registered handler if one exists.
 */
template<typename Variant>
/**
 * @class VariantRouter
 * @brief Variant Router type.
 *
 * @details Part of the GraphX public API for libgraph. The type documents its runtime role, ownership expectations, and interaction with neighboring graph components.
 */
class VariantRouter {
public:
    // ====================================================================
    // Type Definitions
    // ====================================================================

    /**
     * @brief Handler function type for variant data
     * @tparam T The data type (one of the Variant's alternative types)
     *
     * Handler takes a const reference to typed data.
     * Called when data of matching type is dispatched.
     */
    template<typename T>
    using Handler = std::function<void(const T&)>;

    // ====================================================================
    // Construction / Destruction / Lifecycle
    // ====================================================================

    /// Default constructor
    VariantRouter() = default;

    /// Destructor
    ~VariantRouter() = default;

    /// Delete copy to avoid handler registration duplication
    VariantRouter(const VariantRouter&) = delete;
    /**
     * @brief Executes the Operator overload operation.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     * @param VariantRouter Input or configuration value consumed by the method.
     */
    VariantRouter& operator=(const VariantRouter&) = delete;

    /// Allow move semantics for flexible container usage
    VariantRouter(VariantRouter&&) = default;
    /**
     * @brief Executes the Operator overload operation.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     * @param VariantRouter Input or configuration value consumed by the method.
     */
    VariantRouter& operator=(VariantRouter&&) = default;

    // ====================================================================
    // Handler Registration
    // ====================================================================

    /**
     * @brief Register a handler for a specific variant alternative type
     * @tparam T One of the Variant's alternative types
     * @param handler Function to call when data of type T is dispatched
     *
     * Example:
     * @code
     *   router.RegisterHandler<EventA>([this](const auto& event) {
     *       OnEventA(event.x);
     *   });
     * @endcode
     *
     * Multiple registrations for the same type replace previous handler.
     */
    template<typename T>
    /**
     * @brief Updates or queries runtime registration through Register Handler.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     * @param handler Input or configuration value consumed by the method.
     * @return Method-specific result, status, or produced value when the signature provides one.
     */
    void RegisterHandler(Handler<T> handler) {
        handlers_[typeid(T).hash_code()] = [h = std::move(handler)](const Variant& payload) {
            if (const auto* ptr = std::get_if<T>(&payload)) {
                /**
                 * @brief Executes the H operation.
                 *
                 * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
                 * @param ptr Input or configuration value consumed by the method.
                 * @return Method-specific result, status, or produced value when the signature provides one.
                 */
                h(*ptr);
            }
        };
    }

    // ====================================================================
    // Dispatch and Handler Management
    // ====================================================================

    /**
     * @brief Dispatch a variant payload to registered handler
     * @param payload The variant data to dispatch
     *
     * Automatically routes to the correct handler based on the payload's
     * active type. Uses C++17 fold expressions for zero-overhead dispatch.
     *
     * Silently ignores payloads with no registered handler (intentional design).
     *
     * Example:
     * @code
     *   Variant payload(std::in_place_type<EventA>, ...);
     *   router.Dispatch(payload);  // Routes to EventA handler if registered
     * @endcode
     */
    void Dispatch(const Variant& payload) {
        DispatchHelper(payload, std::make_index_sequence<std::variant_size_v<Variant>>{});
    }

    /**
     * @brief Clear all registered handlers
     *
     * Useful for resetting router state or cleaning up before destruction.
     */
    void ClearHandlers() {
        handlers_.clear();
    }

    /**
     * @brief Get count of registered handlers
     * @return Number of distinct type handlers registered
     *
     * Useful for debugging and validation of router configuration.
     */
    int GetHandlerCount() const {
        return static_cast<int>(handlers_.size());
    }

    /**
     * @brief Check if handler is registered for a specific type
     * @tparam T One of the Variant's alternative types
     * @return true if handler registered for type T, false otherwise
     */
    template<typename T>
    /**
     * @brief Reports whether Has Handler is true.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     * @return Method-specific result, status, or produced value when the signature provides one.
     */
    bool HasHandler() const {
        return handlers_.find(typeid(T).hash_code()) != handlers_.end();
    }

private:
    // ====================================================================
    // Private Helper Methods
    // ====================================================================

    /**
     * @brief Dispatch helper using fold expression
     *
     * Expands at compile-time to try each variant alternative type
     * in sequence. Uses C++17 fold expression for clean, efficient code.
     *
     * @tparam Is Index sequence for all Variant alternatives
     * @param payload The variant data to dispatch
     */
    template<std::size_t... Is>
    /**
     * @brief Executes the Dispatch Helper operation.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     * @param payload Input or configuration value consumed by the method.
     * @return Method-specific result, status, or produced value when the signature provides one.
     */
    void DispatchHelper(const Variant& payload, std::index_sequence<Is...>) {
        // Fold expression: try dispatch for each variant alternative type
        // This expands at compile-time to:
        //   TryDispatch<Alt0>(payload);
        //   TryDispatch<Alt1>(payload);
        //   TryDispatch<Alt2>(payload);
        //   ... etc for all alternatives
        (TryDispatch<std::variant_alternative_t<Is, Variant>>(payload), ...);
    }

    /**
     * @brief Try to dispatch payload to handler for type T
     * @tparam T One of the Variant's alternative types
     * @param payload The variant payload
     *
     * Safely extracts data of type T using std::get_if.
     * Calls handler if registered for this type.
     */
    template<typename T>
    /**
     * @brief Attempts Dispatch without throwing on expected failure.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     * @param payload Input or configuration value consumed by the method.
     * @return Method-specific result, status, or produced value when the signature provides one.
     */
    void TryDispatch(const Variant& payload) {
        if (const auto* ptr = std::get_if<T>(&payload)) {
            auto type_hash = typeid(T).hash_code();
            auto it = handlers_.find(type_hash);
            if (it != handlers_.end()) {
                it->second(payload);
            }
        }
    }

    // ====================================================================
    // Private Data Members
    // ====================================================================

    /// Map of type hash codes to dispatch functions
    /// Each function captures a Handler<T> and safely extracts/calls it
    std::map<size_t, std::function<void(const Variant&)>> handlers_;
};

}  // namespace graph

