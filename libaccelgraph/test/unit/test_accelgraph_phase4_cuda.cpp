// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>

#include <string>

#include "accelgraph/CudaAcceleratorProvider.hpp"

TEST(AccelGraphPhase4CudaTest, ProviderInfoUsesCudaIdentityWithoutNativeDeviceClaims) {
    const accelgraph::CudaAcceleratorProvider provider;
    const auto info = provider.Info();

    EXPECT_EQ(info.provider_id.value, "cuda.default");
    EXPECT_EQ(info.backend, accelgraph::AcceleratorBackend::Cuda);
    EXPECT_EQ(info.execution_mode, accelgraph::AcceleratorExecutionMode::HostAsynchronous);
    EXPECT_TRUE(info.devices.empty());
}

TEST(AccelGraphPhase4CudaTest, CreateSessionReportsStructuredPhase4ShellDiagnostic) {
    accelgraph::CudaAcceleratorProvider provider;
    auto result = provider.CreateSession(accelgraph::AcceleratorSessionCreateRequest{});

    ASSERT_FALSE(result.has_value());
    const auto& error = result.error();
    EXPECT_EQ(error.backend, accelgraph::AcceleratorBackend::Cuda);
    EXPECT_EQ(error.provider_id.value, "cuda.default");
    EXPECT_EQ(error.operation, "CreateSession");
    EXPECT_FALSE(error.session_id.has_value());

#if !ACCELGRAPH_ENABLE_CUDA
    EXPECT_EQ(error.category, accelgraph::AcceleratorErrorCategory::Unsupported);
    EXPECT_EQ(error.diagnostic, accelgraph::kCudaSupportNotCompiledDiagnostic);
#elif !ACCELGRAPH_CUDA_TOOLKIT_AVAILABLE
    EXPECT_EQ(error.category, accelgraph::AcceleratorErrorCategory::Unavailable);
    EXPECT_EQ(error.diagnostic, accelgraph::kCudaToolkitUnavailableDiagnostic);
#else
    EXPECT_EQ(error.category, accelgraph::AcceleratorErrorCategory::Unsupported);
    EXPECT_EQ(error.diagnostic, accelgraph::kCudaNativeNotImplementedDiagnostic);
#endif

    EXPECT_EQ(error.diagnostic.find("Host CPU"), std::string::npos);
    EXPECT_EQ(error.diagnostic.find("metal"), std::string::npos);
}