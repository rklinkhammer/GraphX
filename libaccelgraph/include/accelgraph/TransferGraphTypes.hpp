// SPDX-License-Identifier: MIT

#pragma once

#include <cstddef>
#include <variant>

#include "accelgraph/Accelerator.hpp"

namespace accelgraph {

struct HostBufferToken {
    HostAllocationHandle handle;
    std::size_t byte_size{0};
};

struct DeviceBufferToken {
    DeviceAllocationHandle handle;
    std::size_t byte_size{0};
};

struct QueueToken {
    QueueHandle handle;
};

struct TransferCompletionToken {
    TransferCompletion completion;
    std::size_t byte_size{0};
};

struct HostIngressOutput {
    HostBufferToken host_buffer;
};

struct HostToDeviceOutput {
    HostBufferToken source_host_buffer;
    DeviceBufferToken device_buffer;
    QueueToken queue;
    TransferCompletionToken transfer_completion;
};

struct DeviceToHostOutput {
    HostBufferToken source_host_buffer;
    DeviceBufferToken source_device_buffer;
    HostBufferToken output_host_buffer;
    QueueToken queue;
    TransferCompletionToken transfer_completion;
};

using ReleaseLeaseTokenVariant =
    std::variant<HostBufferToken, DeviceBufferToken, QueueToken, TransferCompletionToken>;

struct ReleaseLeaseInput {
    ReleaseLeaseTokenVariant token;
    ReleaseRequest request{};
};

}  // namespace accelgraph
