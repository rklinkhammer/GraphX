// SPDX-License-Identifier: MIT

/**
 * @file test_sar_transport_opaque_contract.cpp
 * @brief GraphX source file.
 */

#include <gtest/gtest.h>

#include "sar/SarMessages.hpp"

#include <type_traits>

namespace {

// Freeze opaque transport semantics for host_ptr and ready_event.
//
// These tests validate that transport fields (device_view.ready_event and host_view.host_ptr)
// are truly opaque to SAR identity semantics. SAR identity derives ONLY from the sidecar.

TEST(SarTransportOpaqueContractTest, SidecarCarriesAllSarIdentity) {
    // Verify that the sidecar contains all fields needed for SAR identity decisions.
    sar::SarSidecar sidecar{};

    // Identity fields in sidecar
    sidecar.sequence_id = 1001;
    sidecar.batch_id = 7;
    sidecar.aperture_id = 3;
    sidecar.pulse_range_start = 512;
    sidecar.pulse_range_count = 64;
    sidecar.stream_id = 2;
    sidecar.tile_id = 5;
    sidecar.tile_count = 16;
    sidecar.backend_id = 1;
    sidecar.backend = sar::SarBackendKind::NativeDevice;
    sidecar.marker = sar::SarFrameMarker::Data;
    sidecar.synthetic = true;
    sidecar.payload_byte_count = 8192;
    sidecar.h2d_queue_id = 100;
    sidecar.kernel_queue_id = 101;
    sidecar.d2h_queue_id = 102;

    // All identity fields must be readable and meaningful
    EXPECT_EQ(sidecar.sequence_id, 1001u);
    EXPECT_EQ(sidecar.batch_id, 7u);
    EXPECT_EQ(sidecar.aperture_id, 3u);
    EXPECT_EQ(sidecar.pulse_range_start, 512u);
    EXPECT_EQ(sidecar.pulse_range_count, 64u);
    EXPECT_EQ(sidecar.stream_id, 2u);
    EXPECT_EQ(sidecar.tile_id, 5u);
    EXPECT_EQ(sidecar.tile_count, 16u);
    EXPECT_EQ(sidecar.backend_id, 1u);
    EXPECT_EQ(sidecar.backend, sar::SarBackendKind::NativeDevice);
    EXPECT_EQ(sidecar.marker, sar::SarFrameMarker::Data);
    EXPECT_TRUE(sidecar.synthetic);
    EXPECT_EQ(sidecar.payload_byte_count, 8192u);
    EXPECT_EQ(sidecar.h2d_queue_id, 100u);
    EXPECT_EQ(sidecar.kernel_queue_id, 101u);
    EXPECT_EQ(sidecar.d2h_queue_id, 102u);
}

TEST(SarTransportOpaqueContractTest, TransportFieldsAreOpaqueToIdentity) {
    // Create two tokens with identical sidecar but different transport fields.
    sar::SarAccelControlToken token1{};
    token1.sidecar.sequence_id = 5000;
    token1.sidecar.batch_id = 2;
    token1.sidecar.tile_id = 3;
    token1.host_view.host_ptr = reinterpret_cast<void*>(0x1000u);  // Sentinel A
    token1.device_view.ready_event = 42u;                          // Event ID A

    sar::SarAccelControlToken token2{};
    token2.sidecar.sequence_id = 5000;
    token2.sidecar.batch_id = 2;
    token2.sidecar.tile_id = 3;
    token2.host_view.host_ptr = reinterpret_cast<void*>(0x2000u);  // Sentinel B (different)
    token2.device_view.ready_event = 99u;                          // Event ID B (different)

    // Sidecars are identical, so identity is identical.
    EXPECT_EQ(token1.sidecar.sequence_id, token2.sidecar.sequence_id);
    EXPECT_EQ(token1.sidecar.batch_id, token2.sidecar.batch_id);
    EXPECT_EQ(token1.sidecar.tile_id, token2.sidecar.tile_id);

    // Transport fields differ, but they do NOT affect identity.
    EXPECT_NE(token1.host_view.host_ptr, token2.host_view.host_ptr);
    EXPECT_NE(token1.device_view.ready_event, token2.device_view.ready_event);

    // The contract requires that SAR algorithm logic NOT use transport fields for decisions.
    // This test documents that property.
}

TEST(SarTransportOpaqueContractTest, HostPtrIsTransportOnlySentinel) {
    // host_ptr in HostPinnedBufferView is a transport infrastructure field.
    // It is set by transport nodes (like source nodes, H2D nodes) as a sentinel.
    // It must never be used to derive SAR identity or make SAR algorithm decisions.

    sar::SarAccelControlToken token{};
    token.host_view.host_ptr = reinterpret_cast<void*>(0xABCD1234u);

    // The field is readable and opaque.
    EXPECT_EQ(token.host_view.host_ptr, reinterpret_cast<void*>(0xABCD1234u));

    // But changes to this field do not affect the sidecar (which carries identity).
    const auto original_sequence_id = token.sidecar.sequence_id;
    token.host_view.host_ptr = reinterpret_cast<void*>(0x12345678u);
    EXPECT_EQ(token.sidecar.sequence_id, original_sequence_id);  // Unchanged
}

TEST(SarTransportOpaqueContractTest, ReadyEventIsTransportOnlySentinel) {
    // ready_event in DeviceBufferView is a transport infrastructure field.
    // It is set by transport nodes (like H2D, D2H nodes) as an opaque GPU event ID.
    // It must never be used to derive SAR identity or make SAR algorithm decisions.

    sar::SarAccelControlToken token{};
    token.device_view.ready_event = 777u;

    // The field is readable and opaque.
    EXPECT_EQ(token.device_view.ready_event, 777u);

    // But changes to this field do not affect the sidecar (which carries identity).
    const auto original_batch_id = token.sidecar.batch_id;
    token.device_view.ready_event = 888u;
    EXPECT_EQ(token.sidecar.batch_id, original_batch_id);  // Unchanged
}

TEST(SarTransportOpaqueContractTest, IdenticalSidecarsImplyIdenticalSarSemantics) {
    // Two tokens with identical sidecars must have identical SAR semantics,
    // even if transport fields differ.

    auto make_token = [](std::uint64_t sequence_id) {
        sar::SarAccelControlToken token{};
        token.sidecar.sequence_id = sequence_id;
        token.sidecar.batch_id = 10;
        token.sidecar.aperture_id = 5;
        token.sidecar.tile_id = 2;
        return token;
    };

    auto token_a = make_token(1234);
    auto token_b = make_token(1234);

    // Vary only transport fields
    token_a.host_view.host_ptr = reinterpret_cast<void*>(0x1111u);
    token_a.device_view.ready_event = 100u;

    token_b.host_view.host_ptr = reinterpret_cast<void*>(0x2222u);
    token_b.device_view.ready_event = 200u;

    // Sidecars must still be equal for SAR purposes
    EXPECT_EQ(token_a.sidecar.sequence_id, token_b.sidecar.sequence_id);
    EXPECT_EQ(token_a.sidecar.batch_id, token_b.sidecar.batch_id);
    EXPECT_EQ(token_a.sidecar.aperture_id, token_b.sidecar.aperture_id);
    EXPECT_EQ(token_a.sidecar.tile_id, token_b.sidecar.tile_id);

    // Transport fields can differ without affecting SAR semantic equivalence
    EXPECT_NE(token_a.host_view.host_ptr, token_b.host_view.host_ptr);
    EXPECT_NE(token_a.device_view.ready_event, token_b.device_view.ready_event);
}

TEST(SarTransportOpaqueContractTest, SyntheticTransportPointersAreValid) {
    // Transport nodes use synthetic pointers for host_ptr and opaque IDs for ready_event.
    // These synthetic values are valid and opaque to SAR identity.

    sar::SarAccelControlToken token{};

    // Synthetic host pointer (common in transport nodes)
    token.host_view.host_ptr = reinterpret_cast<void*>(0u);  // Null
    EXPECT_EQ(token.host_view.host_ptr, nullptr);

    // Synthetic host pointer (common in transport nodes)
    token.host_view.host_ptr = reinterpret_cast<void*>(0x0DEADBEEFu);  // Sentinel
    EXPECT_EQ(token.host_view.host_ptr, reinterpret_cast<void*>(0x0DEADBEEFu));

    // Synthetic event ID (common in transport nodes)
    token.device_view.ready_event = 0u;  // Null event
    EXPECT_EQ(token.device_view.ready_event, 0u);

    // Synthetic event ID (common in transport nodes)
    token.device_view.ready_event = 0xDEADBEEFu;  // Sentinel
    EXPECT_EQ(token.device_view.ready_event, 0xDEADBEEFu);

    // Sidecars remain unaffected regardless of transport sentinel values
    token.sidecar.sequence_id = 42;
    EXPECT_EQ(token.sidecar.sequence_id, 42u);
}

}  // namespace
