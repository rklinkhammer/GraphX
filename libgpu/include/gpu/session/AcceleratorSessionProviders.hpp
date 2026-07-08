// SPDX-License-Identifier: MIT
#pragma once

#include "gpu/session/AcceleratorSession.hpp"

#include <memory>
#include <vector>

namespace graph::gpu {

struct AcceleratorProviderOptions {
    bool enable_cpu{true};
    bool enable_cuda{true};
    bool enable_sycl{true};
    bool enable_metal{true};
};

[[nodiscard]] std::shared_ptr<IAcceleratorSession> CreateCpuAcceleratorSession();
[[nodiscard]] std::shared_ptr<IAcceleratorSession> CreateCudaStubAcceleratorSession();
[[nodiscard]] std::shared_ptr<IAcceleratorSession> CreateSyclStubAcceleratorSession();
[[nodiscard]] std::shared_ptr<IAcceleratorSession> CreateMetalStubAcceleratorSession();

[[nodiscard]] std::shared_ptr<AcceleratorSessionRegistry>
CreateDefaultAcceleratorSessionRegistry(const AcceleratorProviderOptions& options = {});

} // namespace graph::gpu
