// SPDX-License-Identifier: MIT

#pragma once

#include "accelgraph/Accelerator.hpp"

namespace accelgraph {

inline constexpr const char* kCudaSupportNotCompiledDiagnostic =
    "CUDA support not compiled (ACCELGRAPH_ENABLE_CUDA=OFF).";
inline constexpr const char* kCudaToolkitUnavailableDiagnostic =
    "CUDA toolkit unavailable (ACCELGRAPH_CUDA_TOOLKIT_AVAILABLE=OFF).";
inline constexpr const char* kCudaNativeNotImplementedDiagnostic =
    "CUDA native provider not implemented in Phase 4 shell.";

class CudaAcceleratorProvider final : public IAcceleratorProvider {
public:
    CudaAcceleratorProvider();

    [[nodiscard]] AcceleratorProviderInfo Info() const override;

    std::expected<std::shared_ptr<IAcceleratorSession>, AcceleratorError>
    CreateSession(const AcceleratorSessionCreateRequest& request) override;

private:
    AcceleratorProviderInfo info_;
};

}  // namespace accelgraph