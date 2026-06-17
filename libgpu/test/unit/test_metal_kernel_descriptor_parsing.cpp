// SPDX-License-Identifier: MIT

/**
 * @file test_metal_kernel_descriptor_parsing.cpp
 * @brief GraphX source file.
 */

#include <gtest/gtest.h>

#include "gpu/metal/capabilities/MetalKernelDescriptorParsing.hpp"

#include "config/JsonView.hpp"

#include <stdexcept>

namespace {

/**
 * @brief Make valid kernel descriptor json.
 */
nlohmann::json MakeValidKernelDescriptorJson() {
    return {
        {"kernel_id", 101},
        {"function_name", "graphx_identity_u8_inplace"},
        {"source_kind", "builtin"},
        {"arg_layout", nlohmann::json::array({
             {
                 {"kind", "device_buffer"},
                 {"access", "read_write"},
             },
         })},
    };
}

}  // namespace

TEST(MetalKernelDescriptorParsing, RejectsMissingKernelId) {
    auto descriptor = MakeValidKernelDescriptorJson();
    descriptor.erase("kernel_id");

    EXPECT_ANY_THROW(
        (void)graph::gpu::metal::capabilities::ParseMetalKernelDescriptor(graph::JsonView(descriptor)));
}

TEST(MetalKernelDescriptorParsing, RejectsEmptyFunctionName) {
    auto descriptor = MakeValidKernelDescriptorJson();
    descriptor["function_name"] = "";

    EXPECT_THROW(
        (void)graph::gpu::metal::capabilities::ParseMetalKernelDescriptor(graph::JsonView(descriptor)),
        std::invalid_argument);
}

TEST(MetalKernelDescriptorParsing, RejectsInvalidSourceKind) {
    auto descriptor = MakeValidKernelDescriptorJson();
    descriptor["source_kind"] = "bad_source_kind";

    EXPECT_THROW(
        (void)graph::gpu::metal::capabilities::ParseMetalKernelDescriptor(graph::JsonView(descriptor)),
        std::invalid_argument);
}

TEST(MetalKernelDescriptorParsing, RejectsInvalidArgAccess) {
    auto descriptor = MakeValidKernelDescriptorJson();
    descriptor["arg_layout"] = nlohmann::json::array({
        {
            {"kind", "device_buffer"},
            {"access", "bad_access"},
        },
    });

    EXPECT_THROW(
        (void)graph::gpu::metal::capabilities::ParseMetalKernelDescriptor(graph::JsonView(descriptor)),
        std::invalid_argument);
}
