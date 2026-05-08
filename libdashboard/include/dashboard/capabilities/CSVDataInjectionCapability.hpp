#pragma once

#include <string>
#include <memory>
#include "sensor/SensorClassificationType.hpp"
#include "core/ActiveQueue.hpp"
#include "sensor/SensorDataTypes.hpp"


namespace app::capabilities {

struct CSVDataInjectionCommand {
    int nrow;
};

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

}  // namespace app::capabilities
