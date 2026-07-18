// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "dsp/DspGpuBufferLayout.hpp"
#include "dsp/IqPacket.hpp"
#include "dsp/MetalSpectrumDftNode.hpp"
#include "gpu/accel/types/AccelValidation.hpp"
#include "gpu/metal/capabilities/IMetalCapabilities.hpp"
#include "graph/CapabilityBus.hpp"
#include "graph/Message.hpp"
#include "test/PluginInfrastructure.hpp"

namespace {

constexpr std::size_t kPacketSize = 256;
constexpr std::uint64_t kQueueId = 5;
constexpr std::uint32_t kDeviceId = 2;

using IqPacketType = dsp::IqPacket<float, kPacketSize>;
using NodeType = dsp::MetalSpectrumDftNode<kPacketSize>;
using InputTokenType = NodeType::InputTokenType;

class FakeMetalMemoryPool final
    : public graph::gpu::metal::capabilities::IMetalMemoryPoolCapability {
public:
    bool AllocateDevice(std::uint64_t bytes,
                        std::uint32_t device_id,
                        graph::gpu::accel::BufferLease& out_lease) override {
        if (!allow_allocate || bytes == 0) {
            return false;
        }

        const auto allocation_id = next_allocation_id_++;
        auto [it, inserted] = device_allocations_.emplace(
            allocation_id,
            std::vector<std::byte>(static_cast<std::size_t>(bytes), std::byte{0}));
        if (!inserted) {
            return false;
        }

        out_lease.pool_id = 91;
        out_lease.allocation_id = allocation_id;
        out_lease.release_policy = graph::gpu::accel::ReleasePolicy::Manual;
        out_lease.device_view.backend = graph::gpu::accel::BackendKind::Metal;
        out_lease.device_view.device_id = device_id;
        out_lease.device_view.device_ptr = it->second.data();
        out_lease.device_view.bytes = bytes;
        out_lease.device_view.dtype = graph::gpu::accel::DataType::Float32;
        out_lease.device_view.layout = dsp::DspGpuBufferLayout<kPacketSize>::MagnitudeTensorLayout();
        ++allocate_device_count;
        last_device_id = device_id;
        last_bytes = bytes;
        return true;
    }

    bool AllocateShared(std::uint64_t, std::uint32_t, graph::gpu::accel::BufferLease&) override {
        return false;
    }

    bool AllocateHost(std::uint64_t, graph::gpu::accel::BufferLease&) override {
        return false;
    }

    bool Release(const graph::gpu::accel::BufferLease& lease) override {
        return device_allocations_.erase(lease.allocation_id) != 0;
    }

    [[nodiscard]] MemoryPoolSnapshot Snapshot() const override {
        MemoryPoolSnapshot snapshot{};
        snapshot.allocation_count = allocate_device_count;
        return snapshot;
    }

    bool allow_allocate{true};
    std::size_t allocate_device_count{0};
    std::uint32_t last_device_id{0};
    std::uint64_t last_bytes{0};

private:
    std::uint64_t next_allocation_id_{1};
    std::unordered_map<std::uint64_t, std::vector<std::byte>> device_allocations_{};
};

class FakeMetalKernel final
    : public graph::gpu::metal::capabilities::IMetalKernelCapability,
      public graph::gpu::metal::capabilities::IMetalKernelDescriptorCapability {
public:
    bool RegisterKernel(std::uint64_t kernel_id,
                        std::string_view kernel_name) override {
        registered_kernel_names[kernel_id] = std::string(kernel_name);
        return kernel_id != 0 && !kernel_name.empty();
    }

    bool RegisterKernelDescriptor(
        const graph::gpu::metal::capabilities::MetalKernelDescriptor& descriptor) override {
        if (descriptor.kernel_id == 0 || descriptor.function_name.empty() ||
            descriptor.source_kind != graph::gpu::metal::capabilities::MetalKernelSourceKind::InlineSource ||
            descriptor.source_payload.empty() || descriptor.arg_layout.size() != 2) {
            return false;
        }

        last_descriptor = descriptor;
        descriptor_registered = true;

        graph::gpu::metal::capabilities::IMetalKernelCapability::RegisteredKernelExecution execution{};
        execution.arg_count = static_cast<std::uint32_t>(descriptor.arg_layout.size());
        execution.dispatch.grid_x = descriptor.dispatch.default_grid_x;
        execution.dispatch.grid_y = descriptor.dispatch.default_grid_y;
        execution.dispatch.grid_z = descriptor.dispatch.default_grid_z;
        execution.dispatch.block_x = descriptor.dispatch.default_block_x;
        execution.dispatch.block_y = descriptor.dispatch.default_block_y;
        execution.dispatch.block_z = descriptor.dispatch.default_block_z;
        executions[descriptor.kernel_id] = execution;
        return true;
    }

    bool TryGetRegisteredKernelExecution(
        std::uint64_t kernel_id,
        RegisteredKernelExecution& out_execution) const override {
        const auto it = executions.find(kernel_id);
        if (it == executions.end()) {
            return false;
        }
        out_execution = it->second;
        return true;
    }

    bool Launch(const graph::gpu::accel::KernelTicket& ticket,
                void* const* args,
                std::size_t arg_count) override {
        if (!allow_launch || arg_count != 2 || args == nullptr ||
            !graph::gpu::accel::IsValidKernelTicket(ticket)) {
            return false;
        }

        const auto* input_view =
            static_cast<const graph::gpu::accel::DeviceBufferView*>(args[0]);
        auto* output_view =
            static_cast<graph::gpu::accel::DeviceBufferView*>(args[1]);
        if (input_view == nullptr || output_view == nullptr ||
            !graph::gpu::accel::IsValidView(*input_view) ||
            !graph::gpu::accel::IsValidView(*output_view)) {
            return false;
        }

        auto* output = static_cast<float*>(output_view->device_ptr);
        if (output != nullptr) {
            std::fill(output,
                      output + dsp::DspGpuBufferLayout<kPacketSize>::kMagnitudeBinCount,
                      0.0f);
            output[1] = 42.0f;
        }

        ++launch_count;
        last_ticket = ticket;
        last_input_view = *input_view;
        last_output_view = *output_view;
        return true;
    }

    bool allow_launch{true};
    bool descriptor_registered{false};
    std::size_t launch_count{0};
    graph::gpu::metal::capabilities::MetalKernelDescriptor last_descriptor{};
    graph::gpu::accel::KernelTicket last_ticket{};
    graph::gpu::accel::DeviceBufferView last_input_view{};
    graph::gpu::accel::DeviceBufferView last_output_view{};
    std::unordered_map<std::uint64_t, std::string> registered_kernel_names{};
    std::unordered_map<
        std::uint64_t,
        graph::gpu::metal::capabilities::IMetalKernelCapability::RegisteredKernelExecution>
        executions{};
};

class FakeMetalTelemetry final
    : public graph::gpu::metal::capabilities::IMetalTelemetryCapability {
public:
    void RecordTransfer(const graph::gpu::accel::TransferTicket&,
                        std::uint64_t) override {}

    void RecordKernel(const graph::gpu::accel::KernelTicket& ticket,
                      std::uint64_t duration_ns) override {
        ++kernel_samples;
        last_kernel_ticket = ticket;
        last_kernel_duration_ns = duration_ns;
    }

    void IncrementErrorCounter(std::string_view) override {
        ++error_count;
    }

    [[nodiscard]] TelemetrySnapshot Snapshot() const override {
        TelemetrySnapshot snapshot{};
        snapshot.kernel_samples = kernel_samples;
        snapshot.error_count = error_count;
        snapshot.last_kernel_duration_ns = last_kernel_duration_ns;
        return snapshot;
    }

    std::uint64_t kernel_samples{0};
    std::uint64_t error_count{0};
    std::uint64_t last_kernel_duration_ns{0};
    graph::gpu::accel::KernelTicket last_kernel_ticket{};
};

struct DeviceIqStorage {
    std::vector<float> iq;
    InputTokenType token;
};

DeviceIqStorage MakeDeviceIqToken() {
    DeviceIqStorage storage{};
    storage.iq.resize(kPacketSize * 2, 0.0f);
    storage.iq[0] = 1.0f;
    storage.iq[1] = -1.0f;
    storage.iq[2] = 0.5f;
    storage.iq[3] = 0.25f;

    IqPacketType packet;
    packet.packet_number = 42;
    packet.sample_rate_hz = 48000.0;
    packet.samples[0] = {1.0f, -1.0f};
    packet.samples[1] = {0.5f, 0.25f};

    storage.token.token_id = 99;
    storage.token.sidecar = graph::message::Message(packet);
    storage.token.device_view.backend = graph::gpu::accel::BackendKind::Metal;
    storage.token.device_view.device_ptr = storage.iq.data();
    storage.token.device_view.bytes = dsp::DspGpuBufferLayout<kPacketSize>::kIqBytes;
    storage.token.device_view.dtype = graph::gpu::accel::DataType::Float32;
    storage.token.device_view.layout = dsp::DspGpuBufferLayout<kPacketSize>::IqTensorLayout();
    storage.token.device_view.device_id = kDeviceId;
    storage.token.device_view.execution_queue_id = kQueueId;
    storage.token.device_view.ready_event = 11;
    storage.token.has_device_view = true;
    return storage;
}

void RegisterFakeCapabilities(graph::CapabilityBus& bus,
                              const std::shared_ptr<FakeMetalMemoryPool>& memory_pool,
                              const std::shared_ptr<FakeMetalKernel>& kernel,
                              const std::shared_ptr<FakeMetalTelemetry>& telemetry) {
    bus.Register<graph::gpu::metal::capabilities::IMetalMemoryPoolCapability>(memory_pool);
    bus.Register<graph::gpu::metal::capabilities::IMetalKernelCapability>(kernel);
    bus.Register<graph::gpu::metal::capabilities::IMetalTelemetryCapability>(telemetry);
}

}  // namespace

TEST(MetalSpectrumDftNodeTest, RegistersInlineMetalDftKernelDescriptor) {
    auto memory_pool = std::make_shared<FakeMetalMemoryPool>();
    auto kernel = std::make_shared<FakeMetalKernel>();
    auto telemetry = std::make_shared<FakeMetalTelemetry>();
    graph::CapabilityBus bus;
    RegisterFakeCapabilities(bus, memory_pool, kernel, telemetry);

    NodeType node;
    node.Configure(graph::JsonView(nlohmann::json{{"queue_id", kQueueId}, {"device_id", kDeviceId}}));
    ASSERT_TRUE(node.BindGpuCapabilities(bus));

    ASSERT_TRUE(kernel->descriptor_registered);
    EXPECT_EQ(kernel->last_descriptor.source_kind,
              graph::gpu::metal::capabilities::MetalKernelSourceKind::InlineSource);
    EXPECT_EQ(kernel->last_descriptor.function_name, "graphx_dsp_metal_spectrum_dft_256");
    EXPECT_NE(kernel->last_descriptor.source_payload.find("kernel void graphx_dsp_metal_spectrum_dft_256"),
              std::string::npos);
    EXPECT_NE(kernel->last_descriptor.source_payload.find("for (uint n = 0; n < kSampleCount; ++n)"),
              std::string::npos);
    EXPECT_NE(kernel->last_descriptor.source_payload.find("hann_window"),
              std::string::npos);
    EXPECT_NE(kernel->last_descriptor.source_payload.find("/ float(kSampleCount)"),
              std::string::npos);
    EXPECT_EQ(kernel->last_descriptor.source_payload.find("FFTManager"), std::string::npos);
    ASSERT_EQ(kernel->last_descriptor.arg_layout.size(), 2u);
    EXPECT_EQ(kernel->last_descriptor.dispatch.default_grid_x,
              dsp::DspGpuBufferLayout<kPacketSize>::kMagnitudeBinCount);
}

TEST(MetalSpectrumDftNodeTest, FailsWithoutValidDeviceInput) {
    auto memory_pool = std::make_shared<FakeMetalMemoryPool>();
    auto kernel = std::make_shared<FakeMetalKernel>();
    auto telemetry = std::make_shared<FakeMetalTelemetry>();
    graph::CapabilityBus bus;
    RegisterFakeCapabilities(bus, memory_pool, kernel, telemetry);

    NodeType node;
    node.Configure(graph::JsonView(nlohmann::json{{"queue_id", kQueueId}}));
    ASSERT_TRUE(node.BindGpuCapabilities(bus));

    InputTokenType missing_device{};
    missing_device.token_id = 3;
    missing_device.sidecar = graph::message::Message(IqPacketType{});
    auto missing_output = node.Transfer(missing_device,
                                        std::integral_constant<std::size_t, 0>{},
                                        std::integral_constant<std::size_t, 0>{});
    EXPECT_FALSE(missing_output.has_value());

    auto storage = MakeDeviceIqToken();
    storage.token.device_view.backend = graph::gpu::accel::BackendKind::Unknown;
    auto wrong_backend_output = node.Transfer(storage.token,
                                             std::integral_constant<std::size_t, 0>{},
                                             std::integral_constant<std::size_t, 0>{});
    EXPECT_FALSE(wrong_backend_output.has_value());
    EXPECT_EQ(kernel->launch_count, 0u);
}

TEST(MetalSpectrumDftNodeTest, FailsWithoutRequiredIqSidecar) {
    auto memory_pool = std::make_shared<FakeMetalMemoryPool>();
    auto kernel = std::make_shared<FakeMetalKernel>();
    auto telemetry = std::make_shared<FakeMetalTelemetry>();
    graph::CapabilityBus bus;
    RegisterFakeCapabilities(bus, memory_pool, kernel, telemetry);

    NodeType node;
    node.Configure(graph::JsonView(nlohmann::json{{"queue_id", kQueueId}}));
    ASSERT_TRUE(node.BindGpuCapabilities(bus));

    auto storage = MakeDeviceIqToken();
    storage.token.sidecar = graph::message::Message(17);
    auto output = node.Transfer(storage.token,
                                std::integral_constant<std::size_t, 0>{},
                                std::integral_constant<std::size_t, 0>{});
    EXPECT_FALSE(output.has_value());
    EXPECT_EQ(kernel->launch_count, 0u);
}

TEST(MetalSpectrumDftNodeTest, LaunchesKernelAndProducesDeviceBackedMagnitudeToken) {
    auto memory_pool = std::make_shared<FakeMetalMemoryPool>();
    auto kernel = std::make_shared<FakeMetalKernel>();
    auto telemetry = std::make_shared<FakeMetalTelemetry>();
    graph::CapabilityBus bus;
    RegisterFakeCapabilities(bus, memory_pool, kernel, telemetry);

    NodeType node;
    node.Configure(graph::JsonView(nlohmann::json{{"queue_id", kQueueId}, {"device_id", kDeviceId}}));
    ASSERT_TRUE(node.BindGpuCapabilities(bus));

    auto storage = MakeDeviceIqToken();
    const auto output = node.Transfer(storage.token,
                                      std::integral_constant<std::size_t, 0>{},
                                      std::integral_constant<std::size_t, 0>{});
    ASSERT_TRUE(output.has_value());

    EXPECT_EQ(output->token_id, storage.token.token_id);
    const auto& magnitude_packet = output->sidecar;
    EXPECT_EQ(magnitude_packet.packet_number, 42u);
    EXPECT_DOUBLE_EQ(magnitude_packet.sample_rate_hz, 48000.0);
    EXPECT_TRUE(magnitude_packet.valid);

    EXPECT_TRUE(output->has_device_view);
    EXPECT_TRUE(output->has_lease);
    EXPECT_TRUE(output->has_kernel_ticket);
    ASSERT_TRUE(graph::gpu::accel::IsValidView(output->device_view));
    ASSERT_TRUE(graph::gpu::accel::IsValidLease(output->lease));
    ASSERT_TRUE(graph::gpu::accel::IsValidKernelTicket(output->kernel_ticket));

    EXPECT_EQ(output->device_view.bytes, dsp::DspGpuBufferLayout<kPacketSize>::kMagnitudeBytes);
    EXPECT_EQ(output->device_view.layout.rank, 1u);
    EXPECT_EQ(output->device_view.layout.shape[0],
              dsp::DspGpuBufferLayout<kPacketSize>::kMagnitudeBinCount);
    EXPECT_EQ(output->device_view.device_id, kDeviceId);
    EXPECT_EQ(output->kernel_ticket.execution_queue_id, kQueueId);
    EXPECT_EQ(output->kernel_ticket.arg_count, 2u);
    EXPECT_EQ(output->kernel_ticket.launch.grid_x,
              dsp::DspGpuBufferLayout<kPacketSize>::kMagnitudeBinCount);

    EXPECT_EQ(kernel->launch_count, 1u);
    EXPECT_EQ(telemetry->kernel_samples, 1u);
    EXPECT_TRUE(graph::gpu::accel::IsValidView(node.last_input_view()));
    EXPECT_TRUE(graph::gpu::accel::IsValidView(node.last_output_view()));
    EXPECT_TRUE(graph::gpu::accel::IsValidKernelTicket(node.last_kernel_ticket()));

    const auto diagnostics = node.GetDiagnostics().Raw();
    EXPECT_EQ(diagnostics.at("backend").get<std::string>(), "Metal");
    EXPECT_TRUE(diagnostics.at("kernel_registered").get<bool>());
    EXPECT_TRUE(diagnostics.at("has_input_device_view").get<bool>());
    EXPECT_TRUE(diagnostics.at("has_output_device_view").get<bool>());
    EXPECT_TRUE(diagnostics.at("has_kernel_ticket").get<bool>());
    EXPECT_EQ(diagnostics.at("algorithm").get<std::string>(), "direct_dft");
}

TEST(MetalSpectrumDftNodeTest, PluginRegistrationExposesMetalSpectrumDftNode256) {
    auto provider = test::PluginInfrastructure::GetProvider();
    ASSERT_NE(provider, nullptr);
    ASSERT_TRUE(provider->IsNodeTypeAvailable("MetalSpectrumDftNode<256>"));

    auto node = provider->CreateNodeExpected("MetalSpectrumDftNode<256>");
    ASSERT_TRUE(node);
    auto adapter = std::make_shared<graph::NodeFacadeAdapter>(std::move(node).value());
    auto wrapper = std::make_shared<graph::NodeFacadeAdapterWrapper>(adapter);
    ASSERT_NE(wrapper, nullptr);
    EXPECT_TRUE(wrapper->GetType() == "MetalSpectrumDftNode" ||
                wrapper->GetType() == "MetalSpectrumDftNode<256>");
    EXPECT_NE(adapter->GetConfigurablePtr(), nullptr);
    EXPECT_NE(adapter->GetDiagnosablePtr(), nullptr);
}
