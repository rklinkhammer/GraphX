// SPDX-License-Identifier: MIT

/**
 * @file CSVDataInjectionCapability.hpp
 * @brief GraphX source file.
 */

#pragma once

#include <string>
#include <memory>
#include "core/ActiveQueue.hpp"
#include "config/DataTypes.hpp"


namespace capabilities {

struct CSVDataInjectionCommand {
    int nrow;
};

/**
 * @class CSVDataInjectionCapability
 * @brief CSVDataInjectionCapability class.
 */
class CSVDataInjectionCapability {
public:
    CSVDataInjectionCapability() = default;
    virtual ~CSVDataInjectionCapability() = default;
 
    bool EnqueueCommand(const CSVDataInjectionCommand& command) {
        return csv_command_queue_.Enqueue(command);
    }   

    bool DequeueCommand(CSVDataInjectionCommand& command) {
        return csv_command_queue_.Dequeue(command);
    }

    void DisableCommand() {
        csv_command_queue_.Disable();
    }

private:
    core::ActiveQueue<CSVDataInjectionCommand> csv_command_queue_;
};

}  // namespace capabilities
