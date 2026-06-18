// SPDX-License-Identifier: MIT

/**
 * @file CSVDataInjectionCapability.hpp
 * @brief Csvdata Injection Capability Graph runtime support.
 *
 * @details Provides capability API used to share runtime services between policies, nodes, and executors. This file is documented for Doxygen so public APIs and test support surfaces can be browsed consistently.
 */
#pragma once

#include <string>
#include <memory>
#include "core/ActiveQueue.hpp"
#include "config/DataTypes.hpp"


namespace capabilities {

/**

 * @struct CSVDataInjectionCommand

 * @brief Csvdata Injection Command data record.

 *

 * @details Groups related fields passed through GraphX runtime, DSP, or GPU boundaries. The type is intentionally documented as a value object so callers understand ownership, lifetime, and validation expectations.

 */

struct CSVDataInjectionCommand {
    int nrow;
};

/**
 * @class CSVDataInjectionCapability
 * @brief Csvdata Injection Capability capability contract.
 *
 * @details Describes a runtime service obtained through the capability bus. Implementations provide backend or policy services without coupling graph nodes to concrete subsystems.
 */
class CSVDataInjectionCapability {
public:
    /**
     * @brief Executes the Csvdata Injection Capability operation.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     * @return Method-specific result, status, or produced value when the signature provides one.
     */
    CSVDataInjectionCapability() = default;
    /**
     * @brief Releases resources owned by Csvdata Injection Capability.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     */
    virtual ~CSVDataInjectionCapability() = default;
 
    /**
     * @brief Executes the Enqueue Command operation.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     * @param command Input or configuration value consumed by the method.
     * @return Method-specific result, status, or produced value when the signature provides one.
     */
    bool EnqueueCommand(const CSVDataInjectionCommand& command) {
        return csv_command_queue_.Enqueue(command);
    }   

    /**
     * @brief Executes the Dequeue Command operation.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     * @param command Input or configuration value consumed by the method.
     * @return Method-specific result, status, or produced value when the signature provides one.
     */
    bool DequeueCommand(CSVDataInjectionCommand& command) {
        return csv_command_queue_.Dequeue(command);
    }

    /**
     * @brief Executes the Disable Command operation.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     * @return Method-specific result, status, or produced value when the signature provides one.
     */
    void DisableCommand() {
        csv_command_queue_.Disable();
    }

private:
    core::ActiveQueue<CSVDataInjectionCommand> csv_command_queue_;
};

}  // namespace capabilities
