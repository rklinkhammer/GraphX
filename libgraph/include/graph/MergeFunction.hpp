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

#include "PortTypes.hpp"
#include "InputFunction.hpp"
#include <optional>

namespace graph
{
    // ===================================================================================
    // ExpandInputPorts - Generate N InputFn<Port<T, i>> base classes
    // -----------------------------------------------------------------------------------
    // For nodes with N inputs of the same type (like MergeNode), this utility expands
    // a single type T into N separate Port types with indices 0..N-1, along with
    // the corresponding InputFn base classes.
    //
    // Example: ExpandInputPorts<3, Message> generates:
    //   - IInputCommonFn<Port<Message, 0>>
    //   - IInputCommonFn<Port<Message, 1>>
    //   - IInputCommonFn<Port<Message, 2>>
    // ===================================================================================

    /**
     * @brief Expands a single type T into N indexed Port and InputFn bases
     *
     * Used by MergeNode to generate N input ports all of the same type without
     * requiring explicit specialization for each N value.
     *
     * @tparam N Number of input ports to generate
     * @tparam T The type for all N input ports
     *
     * Usage:
     * @code
     *   // Generate 5 Message input ports with InputFn bases
     *   class MyMerge : public ExpandInputPorts<5, Message>::InputBases { ... };
     * @endcode
     */
    template <std::size_t N, typename T>
    struct ExpandInputPorts
    {
    private:
        // Helper to generate index_sequence and unpack into base classes
        template <std::size_t... Is>
        using ExpandedBases = TypeList<IInputCommonFn<Port<T, Is>>...>;

        template <typename = std::make_index_sequence<N>>
        struct ExpandHelper;

        template <std::size_t... Is>
        struct ExpandHelper<std::index_sequence<Is...>> : public IInputCommonFn<Port<T, Is>>...
        {
            /// Constructor that initializes all N bases with a shared unified queue reference
            /// @param unified_queue The single unified queue shared by all input ports
            explicit ExpandHelper(core::ActiveQueue<T>& unified_queue)
                : IInputCommonFn<Port<T, Is>>(unified_queue)...
            {
            }
        };

    public:
        
        // The expanded set of InputFn base classes
        using InputBases = ExpandHelper<>;

        /**
         * @brief Generate port metadata for all N input ports
         *
         * Creates a compile-time array of PortInfo describing all N ports,
         * each with the same type T but different port IDs (0..N-1).
         */
        template <std::size_t... Is>
        static consteval auto build_metadata_helper(std::index_sequence<Is...>)
        {
            return std::array{make_port_info<Port<T, Is>, PortDirection::Input>()...};
        }

        /**
         * @brief Entry point for metadata generation with proper indexing
         */
        static consteval auto build_all_metadata()
        {
            return build_metadata_helper(std::make_index_sequence<N>{});
        }
    };

    // ===================================================================================
    // IMergeFn - Merge processing callback interface
    // -----------------------------------------------------------------------------------
    // Minimal interface defining only the Process() callback for merge nodes.
    // All lifecycle management (queue, threads) is handled by MergeNodeBase.
    // ===================================================================================

    /**
     * @brief Marker interface for merge-style multi-input processing
     * @tparam CommonInput Type of all N input ports
     * @tparam OutputType Type of the single output port
     *
     * IMergeFn defines the contract for nodes that merge multiple inputs
     * into a single unified queue and produce outputs via a dedicated thread.
     *
     * This interface is intentionally minimal - it only defines Process().
     * Lifecycle management (Init, Start, Stop, Join) is handled by MergeNodeBase
     * through NodeLifecycleMixin's *PortsImpl() methods.
     */
    template <typename CommonInput, typename OutputT>
    class IMergeFn
    {
    public:
        using InputType  = CommonInput;
        using OutputType = OutputT;

        virtual ~IMergeFn() = default;

        /**
         * @brief User-implemented merge processing callback
         *
         * Called by the merge thread for each event in the unified queue.
         * The port parameter (integral_constant<0>) indicates the single output port.
         *
         * @param event Event from the unified merge queue (CommonInput type)
         * @param port Output port indicator (always integral_constant<0>)
         * @return Optional OutputType; std::nullopt suppresses output for this event
         *
         * Contract:
         * - Called from merge thread context (thread-safe against other Consume calls)
         * - Should complete in reasonable time
         * - Can read node state (assumed immutable during operation)
         * - Return std::nullopt to skip output for this event
         *
         * Example:
         * @code
         *   std::optional<FlightState> Process(
         *       const Message& event,
         *       std::integral_constant<0> = {}) override
         *   {
         *       fsm_.Update(event);
         *       return fsm_.GetCurrentState();
         *   }
         * @endcode
         */
        virtual std::optional<OutputType> Process(
            const CommonInput& event,
            std::integral_constant<std::size_t, 0> = {}) = 0;
    };

}
