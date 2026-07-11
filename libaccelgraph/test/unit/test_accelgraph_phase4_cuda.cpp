// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>

#include <string>

#include "accelgraph/CudaAcceleratorProvider.hpp"

TEST(CudaGraphExecutorContractTest, ProviderInfoUsesCudaIdentityWithoutNativeDeviceClaims) {
    const accelgraph::CudaAcceleratorProvider provider;
    const auto info = provider.Info();

    EXPECT_EQ(info.provider_id.value, "cuda.default");
    EXPECT_EQ(info.backend, accelgraph::AcceleratorBackend::Cuda);
    EXPECT_EQ(info.execution_mode, accelgraph::AcceleratorExecutionMode::HostAsynchronous);

    accelgraph::CudaAcceleratorProvider mutable_provider;
    const auto session = mutable_provider.CreateSession(accelgraph::AcceleratorSessionCreateRequest{});
    if (session.has_value()) {
        EXPECT_FALSE(info.devices.empty());
    } else {
        EXPECT_TRUE(info.devices.empty());
    }
}

TEST(CudaGraphExecutorContractTest, CreateSessionReportsStructuredPhase4ShellDiagnostic) {
    accelgraph::CudaAcceleratorProvider provider;
    auto result = provider.CreateSession(accelgraph::AcceleratorSessionCreateRequest{});

#if !ACCELGRAPH_ENABLE_CUDA
    ASSERT_FALSE(result.has_value());
    const auto& error = result.error();
    EXPECT_EQ(error.backend, accelgraph::AcceleratorBackend::Cuda);
    EXPECT_EQ(error.provider_id.value, "cuda.default");
    EXPECT_EQ(error.operation, "CreateSession");
    EXPECT_FALSE(error.session_id.has_value());
    EXPECT_EQ(error.category, accelgraph::AcceleratorErrorCategory::Unsupported);
    EXPECT_EQ(error.diagnostic, accelgraph::kCudaSupportNotCompiledDiagnostic);
#elif !ACCELGRAPH_CUDA_TOOLKIT_AVAILABLE
    ASSERT_FALSE(result.has_value());
    const auto& error = result.error();
    EXPECT_EQ(error.backend, accelgraph::AcceleratorBackend::Cuda);
    EXPECT_EQ(error.provider_id.value, "cuda.default");
    EXPECT_EQ(error.operation, "CreateSession");
    EXPECT_FALSE(error.session_id.has_value());
    EXPECT_EQ(error.category, accelgraph::AcceleratorErrorCategory::Unavailable);
    EXPECT_EQ(error.diagnostic, accelgraph::kCudaToolkitUnavailableDiagnostic);
#else
    if (result.has_value()) {
        const auto session = result.value();
        ASSERT_NE(session, nullptr);
        const auto session_info = session->Info();
        EXPECT_EQ(session_info.provider_id.value, "cuda.default");
        EXPECT_EQ(session_info.backend, accelgraph::AcceleratorBackend::Cuda);
        EXPECT_EQ(session_info.execution_mode, accelgraph::AcceleratorExecutionMode::HostAsynchronous);
        EXPECT_TRUE(session_info.device.id.value.rfind("cuda:", 0) == 0);
    } else {
        const auto& error = result.error();
        EXPECT_EQ(error.backend, accelgraph::AcceleratorBackend::Cuda);
        EXPECT_EQ(error.provider_id.value, "cuda.default");
        EXPECT_EQ(error.operation, "CreateSession");
        EXPECT_FALSE(error.session_id.has_value());
        EXPECT_TRUE(error.category == accelgraph::AcceleratorErrorCategory::Unavailable ||
                    error.category == accelgraph::AcceleratorErrorCategory::Unsupported);
    }
#endif
}