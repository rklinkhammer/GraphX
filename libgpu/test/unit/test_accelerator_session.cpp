// SPDX-License-Identifier: MIT

/**
 * @file test_accelerator_session.cpp
 * @brief Test accelerator session backend-neutral GPU acceleration support.
 */
#include <gtest/gtest.h>

#include "gpu/session/AcceleratorSession.hpp"
#include "gpu/session/AcceleratorSessionProviders.hpp"
#include "gpu/bootstrap/GpuCapabilityBootstrap.hpp"

#include <memory>
#include <cstring>
#include <sstream>

namespace {

class NoopMemoryCapability final : public graph::gpu::IMemoryCapability {
public:
    bool AllocateDevice(std::uint64_t, std::uint32_t,
                        graph::gpu::accel::BufferLease&) override {
        return false;
    }

    bool AllocateHostVisible(std::uint64_t, graph::gpu::accel::BufferLease&) override {
        return false;
    }

    bool Release(const graph::gpu::accel::BufferLease&) override {
        return true;
    }
};

class NoopTransferCapability final : public graph::gpu::ITransferCapability {
public:
    bool EnqueueH2D(const graph::gpu::accel::HostPinnedBufferView&,
                    graph::gpu::accel::DeviceBufferView&,
                    std::uint64_t,
                    graph::gpu::accel::TransferTicket&) override {
        return false;
    }

    bool EnqueueD2H(const graph::gpu::accel::DeviceBufferView&,
                    graph::gpu::accel::HostPinnedBufferView&,
                    std::uint64_t,
                    graph::gpu::accel::TransferTicket&) override {
        return false;
    }

    bool EnqueueD2D(const graph::gpu::accel::DeviceBufferView&,
                    graph::gpu::accel::DeviceBufferView&,
                    std::uint64_t,
                    graph::gpu::accel::TransferTicket&) override {
        return false;
    }
};

class NoopEventCapability final : public graph::gpu::IEventCapability {
public:
    std::uint64_t Create() override { return 1; }
    bool IsComplete(std::uint64_t) const override { return true; }
    bool Wait(std::uint64_t, std::uint64_t) override { return true; }
    void Destroy(std::uint64_t) override {}
};

class NoopExecutionCapability final : public graph::gpu::IExecutionCapability {
public:
    std::uint64_t AcquireQueue() override { return 7; }
    void ReleaseQueue(std::uint64_t) override {}
    bool Submit(const graph::gpu::accel::KernelTicket&, void* const*, std::size_t) override {
        return true;
    }
};

class NoopTelemetryCapability final : public graph::gpu::ITelemetryCapability {
public:
    void RecordTransfer(const graph::gpu::accel::TransferTicket&, std::uint64_t) override {}
    void RecordExecution(const graph::gpu::accel::KernelTicket&, std::uint64_t) override {}
    void IncrementErrorCounter(std::string_view) override {}
};

class FakeAcceleratorSession final : public graph::gpu::IAcceleratorSession {
public:
    explicit FakeAcceleratorSession(graph::gpu::BackendDescriptor descriptor)
        : descriptor_(std::move(descriptor)) {}

    const graph::gpu::BackendDescriptor& Describe() const override {
        return descriptor_;
    }

    graph::gpu::IMemoryCapability& Memory() override { return memory_; }
    graph::gpu::ITransferCapability& Transfer() override { return transfer_; }
    graph::gpu::IEventCapability& Events() override { return events_; }
    graph::gpu::IExecutionCapability& Execution() override { return execution_; }
    graph::gpu::ITelemetryCapability& Telemetry() override { return telemetry_; }
    bool CapabilitiesMatchDescriptor() const override { return true; }

private:
    graph::gpu::BackendDescriptor descriptor_{};
    NoopMemoryCapability memory_{};
    NoopTransferCapability transfer_{};
    NoopEventCapability events_{};
    NoopExecutionCapability execution_{};
    NoopTelemetryCapability telemetry_{};
};

graph::gpu::BackendDescriptor MakeDescriptor(graph::gpu::accel::BackendKind backend,
                                             graph::gpu::ExecutionMode mode,
                                             std::uint32_t device_id,
                                             std::string provider_name) {
    graph::gpu::BackendDescriptor descriptor{};
    descriptor.backend = backend;
    descriptor.execution_mode = mode;
    descriptor.provider_name = std::move(provider_name);
    descriptor.runtime_version = "1.0";
    descriptor.device_name = backend == graph::gpu::accel::BackendKind::CPU
        ? "Host CPU"
        : "Accelerator";
    descriptor.device_id = device_id;
    descriptor.architecture = backend == graph::gpu::accel::BackendKind::CPU
        ? "cpu-fallback"
        : "accelerator";
    descriptor.session_id = 1000U + device_id +
        (static_cast<std::uint64_t>(backend) << 16U) +
        (static_cast<std::uint64_t>(mode) << 8U);
    descriptor.features.device_memory = true;
    descriptor.features.host_visible_memory = true;
    descriptor.features.transfers = true;
    descriptor.features.events = true;
    descriptor.features.execution = true;
    descriptor.features.telemetry = true;
    return descriptor;
}

} // namespace

TEST(AcceleratorSessionTest, FormatsAndSerializesBackendDescriptor) {
    auto descriptor = MakeDescriptor(graph::gpu::accel::BackendKind::Metal,
                                     graph::gpu::ExecutionMode::Native,
                                     2U,
                                     "native-metal-provider");

    std::ostringstream stream;
    stream << descriptor;
    EXPECT_NE(stream.str().find("Metal"), std::string::npos);
    EXPECT_NE(stream.str().find("Native"), std::string::npos);
    EXPECT_NE(stream.str().find("native-metal-provider"), std::string::npos);

    const nlohmann::json json = descriptor;
    const auto round_tripped = json.get<graph::gpu::BackendDescriptor>();
    EXPECT_EQ(round_tripped.backend, graph::gpu::accel::BackendKind::Metal);
    EXPECT_EQ(round_tripped.execution_mode, graph::gpu::ExecutionMode::Native);
    EXPECT_EQ(round_tripped.provider_name, "native-metal-provider");
    EXPECT_EQ(round_tripped.device_id, 2U);
}

TEST(AcceleratorSessionTest, SerializesAndRestoresSessionRequest) {
    graph::gpu::AcceleratorSessionRequest request{};
    request.required_backend = graph::gpu::accel::BackendKind::Metal;
    request.preferred_backend = graph::gpu::accel::BackendKind::Metal;
    request.required_execution_mode = graph::gpu::ExecutionMode::Native;
    request.device_id = 3U;
    request.required_features.device_memory = true;
    request.required_features.transfers = true;
    request.fallback_policy = graph::gpu::SessionFallbackPolicy::AllowFallback;

    const nlohmann::json json = request;
    const auto restored = json.get<graph::gpu::AcceleratorSessionRequest>();

    ASSERT_TRUE(restored.required_backend.has_value());
    ASSERT_TRUE(restored.preferred_backend.has_value());
    ASSERT_TRUE(restored.required_execution_mode.has_value());
    ASSERT_TRUE(restored.device_id.has_value());
    EXPECT_EQ(*restored.required_backend, graph::gpu::accel::BackendKind::Metal);
    EXPECT_EQ(*restored.preferred_backend, graph::gpu::accel::BackendKind::Metal);
    EXPECT_EQ(*restored.required_execution_mode, graph::gpu::ExecutionMode::Native);
    EXPECT_EQ(*restored.device_id, 3U);
    EXPECT_TRUE(restored.required_features.device_memory);
    EXPECT_TRUE(restored.required_features.transfers);
    EXPECT_EQ(restored.fallback_policy, graph::gpu::SessionFallbackPolicy::AllowFallback);
}

TEST(AcceleratorSessionTest, RegistryPrefersNativeMetalAndRejectsStubForNativeRequests) {
    graph::gpu::AcceleratorSessionRegistry registry;
    ASSERT_TRUE(registry.Register(std::make_shared<FakeAcceleratorSession>(
        MakeDescriptor(graph::gpu::accel::BackendKind::Metal,
                       graph::gpu::ExecutionMode::Stub,
                       0U,
                       "stub-metal-provider"))));
    ASSERT_TRUE(registry.Register(std::make_shared<FakeAcceleratorSession>(
        MakeDescriptor(graph::gpu::accel::BackendKind::Metal,
                       graph::gpu::ExecutionMode::Native,
                       0U,
                       "native-metal-provider"))));
    ASSERT_TRUE(registry.Register(std::make_shared<FakeAcceleratorSession>(
        MakeDescriptor(graph::gpu::accel::BackendKind::CPU,
                       graph::gpu::ExecutionMode::CpuFallback,
                       0U,
                       "cpu-fallback-provider"))));

    graph::gpu::AcceleratorSessionRequest request{};
    request.required_backend = graph::gpu::accel::BackendKind::Metal;
    request.required_execution_mode = graph::gpu::ExecutionMode::Native;
    request.required_features.device_memory = true;
    request.fallback_policy = graph::gpu::SessionFallbackPolicy::AllowFallback;

    const auto resolved = registry.Resolve(request);
    ASSERT_TRUE(resolved.has_value());
    EXPECT_EQ(resolved->descriptor.backend, graph::gpu::accel::BackendKind::Metal);
    EXPECT_EQ(resolved->descriptor.execution_mode, graph::gpu::ExecutionMode::Native);
    EXPECT_EQ(resolved->descriptor.provider_name, "native-metal-provider");
    EXPECT_FALSE(resolved->fallback_used);
    ASSERT_FALSE(resolved->diagnostics.empty());
    EXPECT_EQ(resolved->diagnostics.back().state, graph::gpu::SessionResolutionState::Selected);

    graph::gpu::AcceleratorSessionRegistry stub_only_registry;
    ASSERT_TRUE(stub_only_registry.Register(std::make_shared<FakeAcceleratorSession>(
        MakeDescriptor(graph::gpu::accel::BackendKind::Metal,
                       graph::gpu::ExecutionMode::Stub,
                       0U,
                       "stub-metal-provider"))));

    request.required_execution_mode = graph::gpu::ExecutionMode::Native;
    const auto stub_resolved = stub_only_registry.Resolve(request);
    ASSERT_FALSE(stub_resolved.has_value());
    EXPECT_EQ(stub_resolved.error().requested_backend,
              graph::gpu::accel::BackendKind::Metal);
    EXPECT_EQ(stub_resolved.error().requested_execution_mode,
              graph::gpu::ExecutionMode::Native);
    EXPECT_NE(stub_resolved.error().detail.find("required backend"), std::string::npos);
}

TEST(AcceleratorSessionTest, RegistryFallsBackToCpuWhenAllowed) {
    graph::gpu::AcceleratorSessionRegistry registry;
    ASSERT_TRUE(registry.Register(std::make_shared<FakeAcceleratorSession>(
        MakeDescriptor(graph::gpu::accel::BackendKind::CPU,
                       graph::gpu::ExecutionMode::CpuFallback,
                       0U,
                       "cpu-fallback-provider"))));

    graph::gpu::AcceleratorSessionRequest request{};
    request.preferred_backend = graph::gpu::accel::BackendKind::Metal;
    request.required_features.device_memory = true;
    request.fallback_policy = graph::gpu::SessionFallbackPolicy::AllowFallback;

    const auto resolved = registry.Resolve(request);
    ASSERT_TRUE(resolved.has_value());
    EXPECT_EQ(resolved->descriptor.backend, graph::gpu::accel::BackendKind::CPU);
    EXPECT_EQ(resolved->descriptor.execution_mode, graph::gpu::ExecutionMode::CpuFallback);
    EXPECT_TRUE(resolved->fallback_used);
    ASSERT_FALSE(resolved->diagnostics.empty());
    EXPECT_EQ(resolved->diagnostics.back().state, graph::gpu::SessionResolutionState::Fallback);
}

TEST(AcceleratorSessionTest, RegistryReportsUnavailableWhenNoSessionMatches) {
    graph::gpu::AcceleratorSessionRegistry registry;
    ASSERT_TRUE(registry.Register(std::make_shared<FakeAcceleratorSession>(
        MakeDescriptor(graph::gpu::accel::BackendKind::Metal,
                       graph::gpu::ExecutionMode::Stub,
                       0U,
                       "stub-metal-provider"))));

    graph::gpu::AcceleratorSessionRequest request{};
    request.required_backend = graph::gpu::accel::BackendKind::Metal;
    request.required_execution_mode = graph::gpu::ExecutionMode::Native;
    request.required_features.runtime_compilation = true;

    const auto resolved = registry.Resolve(request);
    ASSERT_FALSE(resolved.has_value());
    EXPECT_EQ(resolved.error().detail,
              "no registered accelerator session matched the required backend");
    ASSERT_FALSE(resolved.error().diagnostics.empty());
    EXPECT_EQ(resolved.error().diagnostics.front().state,
              graph::gpu::SessionResolutionState::Rejected);
}

TEST(AcceleratorSessionTest, BackendKindIncludesCpuLabeling) {
    EXPECT_STREQ(graph::gpu::accel::ToString(graph::gpu::accel::BackendKind::CPU), "CPU");
}

TEST(AcceleratorSessionTest, RejectsInvalidAndDuplicateProviders) {
    graph::gpu::AcceleratorSessionRegistry registry;
    auto unavailable = MakeDescriptor(graph::gpu::accel::BackendKind::Metal,
                                      graph::gpu::ExecutionMode::Unavailable, 0U,
                                      "unavailable-metal");
    auto invalid = registry.Register(std::make_shared<FakeAcceleratorSession>(unavailable));
    ASSERT_FALSE(invalid.has_value());
    EXPECT_EQ(invalid.error().code,
              graph::gpu::SessionValidationCode::UnavailableExecutionMode);

    auto cpu = std::make_shared<FakeAcceleratorSession>(
        MakeDescriptor(graph::gpu::accel::BackendKind::CPU,
                       graph::gpu::ExecutionMode::CpuFallback, 0U, "cpu"));
    ASSERT_TRUE(registry.Register(cpu));
    auto duplicate = registry.Register(cpu);
    ASSERT_FALSE(duplicate.has_value());
    EXPECT_EQ(duplicate.error().code, graph::gpu::SessionValidationCode::DuplicateSessionId);
}

TEST(AcceleratorSessionTest, RejectsMalformedJsonEnums) {
    auto descriptor = nlohmann::json{{"backend", "Vulkan"},
                                     {"execution_mode", "Native"},
                                     {"provider_name", "bad"},
                                     {"runtime_version", "1"},
                                     {"device_name", "bad"},
                                     {"device_id", 0},
                                     {"architecture", "bad"},
                                     {"session_id", 1},
                                     {"features", graph::gpu::FeatureSet{}}};
    EXPECT_THROW((void)descriptor.get<graph::gpu::BackendDescriptor>(),
                 nlohmann::json::exception);

    graph::gpu::AcceleratorSessionRequest request{};
    nlohmann::json request_json = request;
    request_json["fallback_policy"] = "SurpriseMe";
    EXPECT_THROW((void)request_json.get<graph::gpu::AcceleratorSessionRequest>(),
                 nlohmann::json::exception);
}

TEST(AcceleratorSessionTest, BootstrapsAuthoritativeRegistryInGraphBus) {
    graph::CapabilityBus bus;
    graph::gpu::GpuCapabilityBootstrapOptions options{};
    graph::gpu::RegisterDefaultGpuCapabilities(bus, options);
    auto registry = bus.Get<graph::gpu::AcceleratorSessionRegistry>();
    ASSERT_NE(registry, nullptr);
    EXPECT_EQ(registry->Size(), 4U);

    graph::gpu::AcceleratorSessionRequest native_request{};
    native_request.required_execution_mode = graph::gpu::ExecutionMode::Native;
    EXPECT_FALSE(registry->Resolve(native_request).has_value());

    graph::gpu::AcceleratorSessionRequest request{};
    auto selected = registry->Resolve(request);
    ASSERT_TRUE(selected.has_value());
    EXPECT_NE(selected->descriptor.execution_mode, graph::gpu::ExecutionMode::Unavailable);
}

TEST(AcceleratorSessionTest, CpuProviderPerformsCopiesAndRejectsCrossSessionResources) {
    auto cpu = graph::gpu::CreateCpuAcceleratorSession();
    ASSERT_NE(cpu, nullptr);
    EXPECT_EQ(cpu->Describe().backend, graph::gpu::accel::BackendKind::CPU);
    EXPECT_EQ(cpu->Describe().execution_mode, graph::gpu::ExecutionMode::CpuFallback);

    graph::gpu::accel::BufferLease host{};
    graph::gpu::accel::BufferLease device{};
    ASSERT_TRUE(cpu->Memory().AllocateHostVisible(16, host));
    ASSERT_TRUE(cpu->Memory().AllocateDevice(16, 0, device));
    std::memset(host.host_view.host_ptr, 0x5a, 16);

    const auto queue = cpu->Execution().AcquireQueue();
    ASSERT_NE(queue, 0U);
    graph::gpu::accel::TransferTicket h2d{};
    ASSERT_TRUE(cpu->Transfer().EnqueueH2D(host.host_view, device.device_view, queue, h2d));
    EXPECT_EQ(std::memcmp(host.host_view.host_ptr, device.device_view.device_ptr, 16), 0);
    EXPECT_EQ(h2d.session_id, cpu->Describe().session_id);

    auto foreign = host.host_view;
    ++foreign.session_id;
    graph::gpu::accel::TransferTicket rejected{};
    EXPECT_FALSE(cpu->Transfer().EnqueueH2D(foreign, device.device_view, queue, rejected));

    EXPECT_TRUE(cpu->Memory().Release(host));
    EXPECT_FALSE(cpu->Memory().Release(host));
    EXPECT_TRUE(cpu->Memory().Release(device));
    cpu->Execution().ReleaseQueue(queue);
}
