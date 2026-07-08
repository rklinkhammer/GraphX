// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>

#include <memory>

#include "accelgraph/AcceleratorRegistry.hpp"
#include "accelgraph/CpuAcceleratorProvider.hpp"

namespace {

using accelgraph::AcceleratorBackend;
using accelgraph::AcceleratorErrorCategory;
using accelgraph::AcceleratorRegistry;
using accelgraph::AcceleratorSessionCreateRequest;
using accelgraph::CpuAcceleratorProvider;
using accelgraph::DeviceAllocationRequest;
using accelgraph::HostAllocationRequest;
using accelgraph::QueueRequest;
using accelgraph::ReleaseRequest;
using accelgraph::TransferRequest;
using accelgraph::WaitRequest;

std::shared_ptr<accelgraph::IAcceleratorSession>
CreateCpuSession(const std::shared_ptr<accelgraph::IAcceleratorProvider>& provider) {
    auto session_result = provider->CreateSession(AcceleratorSessionCreateRequest{});
    EXPECT_TRUE(session_result.has_value());
    return session_result.value();
}

}  // namespace

TEST(AccelGraphPhase1CpuTest, ProviderDiscovery) {
    AcceleratorRegistry registry;
    auto provider = std::make_shared<CpuAcceleratorProvider>();

    EXPECT_TRUE(registry.RegisterProvider(provider));
    EXPECT_FALSE(registry.RegisterProvider(provider));

    const auto providers = registry.ListProviders();
    ASSERT_EQ(providers.size(), 1U);
    EXPECT_EQ(providers.front().backend, AcceleratorBackend::Cpu);
    EXPECT_EQ(providers.front().provider_id.value, "cpu.default");
    EXPECT_FALSE(providers.front().devices.empty());

    EXPECT_NE(registry.FindProviderByBackend(AcceleratorBackend::Cpu), nullptr);
}

TEST(AccelGraphPhase1CpuTest, SessionCreationAndTeardown) {
    auto provider = std::make_shared<CpuAcceleratorProvider>();
    auto session = CreateCpuSession(provider);
    ASSERT_NE(session, nullptr);

    const auto info = session->Info();
    EXPECT_TRUE(info.session_id.IsValid());
    EXPECT_EQ(info.backend, AcceleratorBackend::Cpu);
    EXPECT_EQ(info.provider_id.value, "cpu.default");

    session.reset();
}

TEST(AccelGraphPhase1CpuTest, AllocationAndDeterministicRelease) {
    auto provider = std::make_shared<CpuAcceleratorProvider>();
    auto session = CreateCpuSession(provider);

    auto host_result = session->AllocateHost(HostAllocationRequest{.byte_size = 256, .debug_label = "host-a"});
    ASSERT_TRUE(host_result.has_value());
    EXPECT_TRUE(host_result->handle.IsValid());
    EXPECT_FALSE(host_result->handle.IsReleased());

    auto release_result = session->Release(host_result->handle);
    ASSERT_TRUE(release_result.has_value());
    EXPECT_TRUE(release_result->released);
    EXPECT_TRUE(host_result->handle.IsReleased());
}

TEST(AccelGraphPhase1CpuTest, MoveCopySemantics) {
    auto provider = std::make_shared<CpuAcceleratorProvider>();
    auto session = CreateCpuSession(provider);

    auto allocation = session->AllocateDevice(DeviceAllocationRequest{.byte_size = 64, .debug_label = "device-a"});
    ASSERT_TRUE(allocation.has_value());

    auto copy = allocation->handle;
    auto moved = std::move(copy);

    EXPECT_TRUE(moved.IsValid());
    EXPECT_EQ(moved.SessionId(), allocation->handle.SessionId());
    EXPECT_EQ(moved.ProviderId().value, allocation->handle.ProviderId().value);
    EXPECT_EQ(moved.DebugInfo().byte_size, 64U);

    auto release_result = session->Release(moved);
    ASSERT_TRUE(release_result.has_value());
    EXPECT_TRUE(allocation->handle.IsReleased());
}

TEST(AccelGraphPhase1CpuTest, DoubleReleasePrevention) {
    auto provider = std::make_shared<CpuAcceleratorProvider>();
    auto session = CreateCpuSession(provider);

    auto allocation = session->AllocateHost(HostAllocationRequest{.byte_size = 32, .debug_label = "host-double"});
    ASSERT_TRUE(allocation.has_value());

    auto first = session->Release(allocation->handle);
    ASSERT_TRUE(first.has_value());
    EXPECT_TRUE(first->released);

    auto second = session->Release(allocation->handle);
    ASSERT_FALSE(second.has_value());
    EXPECT_EQ(second.error().category, AcceleratorErrorCategory::InvalidState);

    auto allowed = session->Release(allocation->handle, ReleaseRequest{.allow_if_released = true});
    ASSERT_TRUE(allowed.has_value());
    EXPECT_FALSE(allowed->released);
}

TEST(AccelGraphPhase1CpuTest, CrossSessionRejection) {
    auto provider = std::make_shared<CpuAcceleratorProvider>();
    auto session_a = CreateCpuSession(provider);
    auto session_b = CreateCpuSession(provider);

    auto host = session_a->AllocateHost(HostAllocationRequest{.byte_size = 64, .debug_label = "host-a"});
    auto device = session_a->AllocateDevice(DeviceAllocationRequest{.byte_size = 64, .debug_label = "device-a"});
    auto queue = session_a->AcquireQueue(QueueRequest{.debug_label = "queue-a"});
    ASSERT_TRUE(host.has_value());
    ASSERT_TRUE(device.has_value());
    ASSERT_TRUE(queue.has_value());

    auto transfer = session_b->EnqueueHostToDevice(host->handle,
                                                   device->handle,
                                                   queue->handle,
                                                   TransferRequest{.byte_size = 16});
    ASSERT_FALSE(transfer.has_value());
    EXPECT_EQ(transfer.error().category, AcceleratorErrorCategory::CrossSessionResource);
}

TEST(AccelGraphPhase1CpuTest, StructuredFailureCategories) {
    auto provider = std::make_shared<CpuAcceleratorProvider>();
    auto bad_session = provider->CreateSession(
        AcceleratorSessionCreateRequest{.requested_device = accelgraph::AcceleratorDeviceId{"cpu:999"}});
    ASSERT_FALSE(bad_session.has_value());
    EXPECT_EQ(bad_session.error().category, AcceleratorErrorCategory::Unavailable);
    EXPECT_EQ(bad_session.error().operation, "CreateSession");

    auto session = CreateCpuSession(provider);

    auto invalid_allocation = session->AllocateHost(HostAllocationRequest{.byte_size = 0});
    ASSERT_FALSE(invalid_allocation.has_value());
    EXPECT_EQ(invalid_allocation.error().category, AcceleratorErrorCategory::InvalidArgument);
    EXPECT_EQ(invalid_allocation.error().provider_id.value, "cpu.default");
    EXPECT_TRUE(invalid_allocation.error().session_id.has_value());

    auto host = session->AllocateHost(HostAllocationRequest{.byte_size = 16});
    auto device = session->AllocateDevice(DeviceAllocationRequest{.byte_size = 16});
    auto queue = session->AcquireQueue(QueueRequest{});
    ASSERT_TRUE(host.has_value());
    ASSERT_TRUE(device.has_value());
    ASSERT_TRUE(queue.has_value());

    auto bad_transfer = session->EnqueueHostToDevice(host->handle,
                                                     device->handle,
                                                     queue->handle,
                                                     TransferRequest{.byte_size = 32});
    ASSERT_FALSE(bad_transfer.has_value());
    EXPECT_EQ(bad_transfer.error().category, AcceleratorErrorCategory::TransferFailed);
}

TEST(AccelGraphPhase1CpuTest, QueueLifecycle) {
    auto provider = std::make_shared<CpuAcceleratorProvider>();
    auto session = CreateCpuSession(provider);

    auto queue = session->AcquireQueue(QueueRequest{.debug_label = "queue-life"});
    ASSERT_TRUE(queue.has_value());
    EXPECT_TRUE(queue->handle.IsValid());
    EXPECT_FALSE(queue->handle.IsReleased());

    auto released = session->Release(queue->handle);
    ASSERT_TRUE(released.has_value());
    EXPECT_TRUE(released->released);
    EXPECT_TRUE(queue->handle.IsReleased());

    auto second = session->Release(queue->handle);
    ASSERT_FALSE(second.has_value());
    EXPECT_EQ(second.error().category, AcceleratorErrorCategory::InvalidState);
}

TEST(AccelGraphPhase1CpuTest, TransferCompletionLifecycle) {
    auto provider = std::make_shared<CpuAcceleratorProvider>();
    auto session = CreateCpuSession(provider);

    auto host = session->AllocateHost(HostAllocationRequest{.byte_size = 32, .debug_label = "host-xfer"});
    auto device = session->AllocateDevice(DeviceAllocationRequest{.byte_size = 32, .debug_label = "dev-xfer"});
    auto queue = session->AcquireQueue(QueueRequest{.debug_label = "queue-xfer"});
    ASSERT_TRUE(host.has_value());
    ASSERT_TRUE(device.has_value());
    ASSERT_TRUE(queue.has_value());

    auto transfer = session->EnqueueHostToDevice(host->handle,
                                                 device->handle,
                                                 queue->handle,
                                                 TransferRequest{.byte_size = 32, .debug_label = "xfer"});
    ASSERT_TRUE(transfer.has_value());
    EXPECT_TRUE(transfer->completion.IsValid());
    EXPECT_TRUE(transfer->completion.Event().IsValid());

    auto waited = session->Wait(transfer->completion, WaitRequest{.timeout = std::chrono::milliseconds{10}});
    ASSERT_TRUE(waited.has_value());
    EXPECT_TRUE(waited->completed);

    auto released = session->Release(transfer->completion);
    ASSERT_TRUE(released.has_value());
    EXPECT_TRUE(released->released);
    EXPECT_TRUE(transfer->completion.IsReleased());
    EXPECT_TRUE(transfer->completion.Event().IsReleased());
}

TEST(AccelGraphPhase1CpuTest, RepeatedCreationTeardown) {
    auto provider = std::make_shared<CpuAcceleratorProvider>();

    for (int i = 0; i < 64; ++i) {
        auto session = CreateCpuSession(provider);
        ASSERT_NE(session, nullptr);

        auto host = session->AllocateHost(HostAllocationRequest{.byte_size = 128, .debug_label = "repeat-host"});
        auto device = session->AllocateDevice(DeviceAllocationRequest{.byte_size = 128, .debug_label = "repeat-device"});
        auto queue = session->AcquireQueue(QueueRequest{.debug_label = "repeat-queue"});
        ASSERT_TRUE(host.has_value());
        ASSERT_TRUE(device.has_value());
        ASSERT_TRUE(queue.has_value());

        auto transfer = session->EnqueueHostToDevice(host->handle,
                                                     device->handle,
                                                     queue->handle,
                                                     TransferRequest{.byte_size = 64});
        ASSERT_TRUE(transfer.has_value());
        ASSERT_TRUE(session->Wait(transfer->completion, WaitRequest{}).has_value());
        ASSERT_TRUE(session->Release(transfer->completion).has_value());
        ASSERT_TRUE(session->Release(queue->handle).has_value());
        ASSERT_TRUE(session->Release(device->handle).has_value());
        ASSERT_TRUE(session->Release(host->handle).has_value());
    }
}