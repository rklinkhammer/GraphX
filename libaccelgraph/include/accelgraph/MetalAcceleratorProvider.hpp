// SPDX-License-Identifier: MIT

#pragma once

#include "accelgraph/Accelerator.hpp"

namespace accelgraph {

inline constexpr const char* kMetalSupportNotCompiledDiagnostic =
    "Metal support not compiled (ACCELGRAPH_ENABLE_METAL=OFF).";
inline constexpr const char* kMetalRuntimeUnavailableDiagnostic =
    "Metal runtime unavailable.";
inline constexpr const char* kMetalNoCompatibleDeviceDiagnostic =
    "Metal runtime unavailable: no compatible device.";
inline constexpr const char* kMetalSessionCreationFailureDiagnostic =
    "Metal session creation failure.";
inline constexpr const char* kMetalTransferFailureDiagnostic =
    "Metal transfer failure.";

class MetalAcceleratorProvider final : public IAcceleratorProvider {
public:
    MetalAcceleratorProvider();

    [[nodiscard]] AcceleratorProviderInfo Info() const override;

    std::expected<std::shared_ptr<IAcceleratorSession>, AcceleratorError>
    CreateSession(const AcceleratorSessionCreateRequest& request) override;

private:
    AcceleratorProviderInfo info_;
};

}  // namespace accelgraph
