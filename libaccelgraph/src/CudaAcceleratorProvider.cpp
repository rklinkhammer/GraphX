// SPDX-License-Identifier: MIT

#include "accelgraph/CudaAcceleratorProvider.hpp"

#include <optional>

namespace accelgraph {

namespace {

AcceleratorError MakeProviderError(const AcceleratorProviderInfo& info,
                                   AcceleratorErrorCategory category,
                                   const char* operation,
                                   const char* diagnostic,
                                   std::optional<AcceleratorDeviceId> device_id = std::nullopt) {
    AcceleratorError error;
    error.category = category;
    error.backend = info.backend;
    error.execution_mode = info.execution_mode;
    error.provider_id = info.provider_id;
    error.device_id = std::move(device_id);
    error.operation = operation;
    error.diagnostic = diagnostic;
    return error;
}

}  // namespace

CudaAcceleratorProvider::CudaAcceleratorProvider() {
    info_.provider_id = AcceleratorProviderId{"cuda.default"};
    info_.backend = AcceleratorBackend::Cuda;
    info_.execution_mode = AcceleratorExecutionMode::HostAsynchronous;
}

AcceleratorProviderInfo CudaAcceleratorProvider::Info() const {
    return info_;
}

std::expected<std::shared_ptr<IAcceleratorSession>, AcceleratorError>
CudaAcceleratorProvider::CreateSession(const AcceleratorSessionCreateRequest& request) {
#if !ACCELGRAPH_ENABLE_CUDA
    return std::unexpected(MakeProviderError(info_,
                                             AcceleratorErrorCategory::Unsupported,
                                             "CreateSession",
                                             kCudaSupportNotCompiledDiagnostic,
                                             request.requested_device));
#elif !ACCELGRAPH_CUDA_TOOLKIT_AVAILABLE
    return std::unexpected(MakeProviderError(info_,
                                             AcceleratorErrorCategory::Unavailable,
                                             "CreateSession",
                                             kCudaToolkitUnavailableDiagnostic,
                                             request.requested_device));
#else
    return std::unexpected(MakeProviderError(info_,
                                             AcceleratorErrorCategory::Unsupported,
                                             "CreateSession",
                                             kCudaNativeNotImplementedDiagnostic,
                                             request.requested_device));
#endif
}

}  // namespace accelgraph