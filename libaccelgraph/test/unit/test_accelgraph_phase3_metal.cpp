// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "accelgraph/AcceleratorSessionRegistry.hpp"
#include "accelgraph/MetalAcceleratorProvider.hpp"
#include "accelgraph/TransferGraphNodes.hpp"

namespace {

std::vector<std::byte> BuildPayload(std::size_t size) {
    std::vector<std::byte> payload(size);
    for (std::size_t i = 0; i < payload.size(); ++i) {
        payload[i] = static_cast<std::byte>((i * 13U + 5U) & 0xFFU);
    }
    return payload;
}

std::filesystem::path TransferTopologyConfigPath() {
    return std::filesystem::path(__FILE__).parent_path().parent_path() /
           "config" / "topologies" / "accelgraph_phase2_transfer_topology.json";
}

bool IsMetalSkippableError(const accelgraph::AcceleratorError& error) {
    return (error.category == accelgraph::AcceleratorErrorCategory::Unsupported &&
            error.diagnostic == accelgraph::kMetalSupportNotCompiledDiagnostic) ||
           (error.category == accelgraph::AcceleratorErrorCategory::Unavailable &&
            error.diagnostic == accelgraph::kMetalNoCompatibleDeviceDiagnostic) ||
           (error.category == accelgraph::AcceleratorErrorCategory::Unavailable &&
            error.diagnostic == accelgraph::kMetalRuntimeUnavailableDiagnostic);
}

std::expected<std::shared_ptr<accelgraph::IAcceleratorSession>, accelgraph::AcceleratorError>
CreateMetalSession() {
    auto provider = std::make_shared<accelgraph::MetalAcceleratorProvider>();
    return provider->CreateSession(accelgraph::AcceleratorSessionCreateRequest{});
}

}  // namespace

TEST(AccelGraphPhase3MetalProviderTest, DiagnosticsDistinguishMetalAvailabilityStates) {
    accelgraph::MetalAcceleratorProvider provider;

    auto session_result = provider.CreateSession(accelgraph::AcceleratorSessionCreateRequest{});
    if (session_result.has_value()) {
        const auto info = session_result.value()->Info();
        EXPECT_EQ(info.backend, accelgraph::AcceleratorBackend::Metal);
        EXPECT_EQ(info.provider_id.value, "metal.default");
        EXPECT_TRUE(info.session_id.IsValid());
        EXPECT_EQ(info.device.backend, accelgraph::AcceleratorBackend::Metal);
        return;
    }

    const auto& error = session_result.error();
    EXPECT_EQ(error.operation, "CreateSession");

#if ACCELGRAPH_ENABLE_METAL
    if (error.diagnostic == accelgraph::kMetalRuntimeUnavailableDiagnostic) {
        EXPECT_EQ(error.category, accelgraph::AcceleratorErrorCategory::Unavailable);
        return;
    }
    if (error.diagnostic == accelgraph::kMetalNoCompatibleDeviceDiagnostic) {
        EXPECT_EQ(error.category, accelgraph::AcceleratorErrorCategory::Unavailable);
        GTEST_SKIP() << error.diagnostic;
    }
    EXPECT_NE(error.diagnostic.find(accelgraph::kMetalSessionCreationFailureDiagnostic),
              std::string::npos);
    EXPECT_EQ(error.category, accelgraph::AcceleratorErrorCategory::BackendFailure);
#else
    EXPECT_EQ(error.category, accelgraph::AcceleratorErrorCategory::Unsupported);
    EXPECT_EQ(error.diagnostic, accelgraph::kMetalSupportNotCompiledDiagnostic);
#endif
}

TEST(AccelGraphPhase3MetalProviderTest, TransferRoundTripPassesOrSkipsWithExactHardwareDiagnostic) {
    auto session_result = CreateMetalSession();
    if (!session_result.has_value()) {
        if (IsMetalSkippableError(session_result.error())) {
            GTEST_SKIP() << session_result.error().diagnostic;
        }
        FAIL() << "Metal session creation failed: " << session_result.error().diagnostic;
    }
    auto session = session_result.value();

    auto host_src =
        session->AllocateHost(accelgraph::HostAllocationRequest{.byte_size = 256, .debug_label = "metal.in"});
    ASSERT_TRUE(host_src.has_value());

    const auto payload = BuildPayload(256);
    auto write_result = session->WriteHost(
        host_src->handle,
        accelgraph::HostWriteRequest{.source = std::span<const std::byte>(payload), .destination_offset = 0});
    ASSERT_TRUE(write_result.has_value());

    auto device = session->AllocateDevice(
        accelgraph::DeviceAllocationRequest{.byte_size = 256, .debug_label = "metal.device"});
    ASSERT_TRUE(device.has_value());

    auto queue = session->AcquireQueue(accelgraph::QueueRequest{.debug_label = "metal.queue"});
    ASSERT_TRUE(queue.has_value());

    auto h2d = session->EnqueueHostToDevice(
        host_src->handle,
        device->handle,
        queue->handle,
        accelgraph::TransferRequest{.byte_size = 256,
                                    .source_offset = 0,
                                    .destination_offset = 0,
                                    .debug_label = "metal.h2d"});
    ASSERT_TRUE(h2d.has_value()) << h2d.error().diagnostic;

    auto h2d_wait = session->Wait(h2d->completion, accelgraph::WaitRequest{});
    ASSERT_TRUE(h2d_wait.has_value()) << h2d_wait.error().diagnostic;
    EXPECT_TRUE(h2d_wait->completed);

    auto host_dst =
        session->AllocateHost(accelgraph::HostAllocationRequest{.byte_size = 256, .debug_label = "metal.out"});
    ASSERT_TRUE(host_dst.has_value());

    auto d2h = session->EnqueueDeviceToHost(
        device->handle,
        host_dst->handle,
        queue->handle,
        accelgraph::TransferRequest{.byte_size = 256,
                                    .source_offset = 0,
                                    .destination_offset = 0,
                                    .debug_label = "metal.d2h"});
    ASSERT_TRUE(d2h.has_value()) << d2h.error().diagnostic;

    auto d2h_wait = session->Wait(d2h->completion, accelgraph::WaitRequest{});
    ASSERT_TRUE(d2h_wait.has_value()) << d2h_wait.error().diagnostic;
    EXPECT_TRUE(d2h_wait->completed);

    auto read_back = session->ReadHost(
        host_dst->handle,
        accelgraph::HostReadRequest{.byte_size = payload.size(), .source_offset = 0});
    ASSERT_TRUE(read_back.has_value());
    EXPECT_EQ(read_back->bytes, payload);

    EXPECT_TRUE(session->Release(h2d->completion).has_value());
    EXPECT_TRUE(session->Release(d2h->completion).has_value());
    EXPECT_TRUE(session->Release(queue->handle).has_value());
    EXPECT_TRUE(session->Release(device->handle).has_value());
    EXPECT_TRUE(session->Release(host_src->handle).has_value());
    EXPECT_TRUE(session->Release(host_dst->handle).has_value());
}

TEST(AccelGraphPhase3MetalProviderTest, TransferFailureDiagnosticIsDistinguished) {
    auto session_result = CreateMetalSession();
    if (!session_result.has_value()) {
        if (IsMetalSkippableError(session_result.error())) {
            GTEST_SKIP() << session_result.error().diagnostic;
        }
        FAIL() << "Metal session creation failed: " << session_result.error().diagnostic;
    }
    auto session = session_result.value();

    auto host = session->AllocateHost(accelgraph::HostAllocationRequest{.byte_size = 32, .debug_label = "host"});
    ASSERT_TRUE(host.has_value());

    auto device =
        session->AllocateDevice(accelgraph::DeviceAllocationRequest{.byte_size = 32, .debug_label = "device"});
    ASSERT_TRUE(device.has_value());

    auto queue = session->AcquireQueue(accelgraph::QueueRequest{.debug_label = "queue"});
    ASSERT_TRUE(queue.has_value());

    auto transfer = session->EnqueueHostToDevice(
        host->handle,
        device->handle,
        queue->handle,
        accelgraph::TransferRequest{.byte_size = 33,
                                    .source_offset = 0,
                                    .destination_offset = 0,
                                    .debug_label = "fail"});
    ASSERT_FALSE(transfer.has_value());
    EXPECT_EQ(transfer.error().category, accelgraph::AcceleratorErrorCategory::TransferFailed);
    EXPECT_NE(transfer.error().diagnostic.find(accelgraph::kMetalTransferFailureDiagnostic),
              std::string::npos);

    EXPECT_TRUE(session->Release(queue->handle).has_value());
    EXPECT_TRUE(session->Release(device->handle).has_value());
    EXPECT_TRUE(session->Release(host->handle).has_value());
}

TEST(AccelGraphPhase3MetalProviderTest, GenericTransferTopologyRequestsMetalProviderAndExecutesOrSkips) {
    const auto config_path = TransferTopologyConfigPath();
    ASSERT_TRUE(std::filesystem::exists(config_path));

    std::ifstream config_stream(config_path);
    ASSERT_TRUE(config_stream.is_open());

    std::string config_text((std::istreambuf_iterator<char>(config_stream)),
                            std::istreambuf_iterator<char>());
    EXPECT_NE(config_text.find("\"provider_id\": \"metal.default\""), std::string::npos);

    auto session_result = CreateMetalSession();
    if (!session_result.has_value()) {
        if (IsMetalSkippableError(session_result.error())) {
            GTEST_SKIP() << session_result.error().diagnostic;
        }
        FAIL() << "Metal session creation failed: " << session_result.error().diagnostic;
    }
    auto session = session_result.value();

    accelgraph::AcceleratorSessionRegistry registry;
    ASSERT_TRUE(registry.RegisterSession("graph.default", session));

    accelgraph::HostIngressNode ingress;
    accelgraph::HostToDeviceNode h2d;
    accelgraph::DeviceToHostNode d2h;
    accelgraph::HostEgressNode egress;
    accelgraph::ReleaseLeaseNode release;

    ASSERT_TRUE(ingress.Initialize(registry, "graph.default"));
    ASSERT_TRUE(h2d.Initialize(registry, "graph.default"));
    ASSERT_TRUE(d2h.Initialize(registry, "graph.default"));
    ASSERT_TRUE(egress.Initialize(registry, "graph.default"));
    ASSERT_TRUE(release.Initialize(registry, "graph.default"));

    const auto payload = BuildPayload(128);

    auto ingress_out = ingress.Execute(payload, "phase3.ingress");
    ASSERT_TRUE(ingress_out.has_value());

    auto h2d_out = h2d.Execute(ingress_out->host_buffer, "phase3.h2d");
    ASSERT_TRUE(h2d_out.has_value()) << h2d_out.error().diagnostic;

    auto d2h_out = d2h.Execute(*h2d_out, "phase3.d2h");
    ASSERT_TRUE(d2h_out.has_value()) << d2h_out.error().diagnostic;

    auto output_payload = egress.Execute(d2h_out->output_host_buffer);
    ASSERT_TRUE(output_payload.has_value());
    EXPECT_EQ(output_payload.value(), payload);

    ASSERT_TRUE(release.Execute(h2d_out->transfer_completion).has_value());
    ASSERT_TRUE(release.Execute(d2h_out->transfer_completion).has_value());
    ASSERT_TRUE(release.Execute(h2d_out->queue).has_value());
    ASSERT_TRUE(release.Execute(h2d_out->device_buffer).has_value());
    ASSERT_TRUE(release.Execute(ingress_out->host_buffer).has_value());
    ASSERT_TRUE(release.Execute(d2h_out->output_host_buffer).has_value());
}
