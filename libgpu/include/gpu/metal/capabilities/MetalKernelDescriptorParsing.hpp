/**
 * @file MetalKernelDescriptorParsing.hpp
 * @brief GraphX source file.
 */

// MIT License
//
// Copyright (c) 2026 GraphX Contributors

#pragma once

#include "gpu/metal/capabilities/IMetalCapabilities.hpp"
#include "config/JsonView.hpp"

#include <algorithm>
#include <stdexcept>

namespace graph::gpu::metal::capabilities {

inline MetalKernelDescriptor ParseMetalKernelDescriptor(const graph::JsonView& descriptor_obj) {
    MetalKernelDescriptor descriptor{};

    auto kernel_id = descriptor_obj.TryGetInt("kernel_id");
    if (!kernel_id) {
        throw kernel_id.error();
    }
    if (kernel_id.value() <= 0) {
        throw std::invalid_argument("kernel_descriptor.kernel_id must be > 0");
    }
    descriptor.kernel_id = static_cast<std::uint64_t>(kernel_id.value());

    auto function_name = descriptor_obj.TryGetString("function_name");
    if (!function_name) {
        throw function_name.error();
    }
    if (function_name->empty()) {
        throw std::invalid_argument("kernel_descriptor.function_name must be non-empty");
    }
    descriptor.function_name = function_name.value();

    if (descriptor_obj.Contains("source_kind")) {
        auto source_kind = descriptor_obj.TryGetString("source_kind");
        if (!source_kind) {
            throw source_kind.error();
        }
        if (*source_kind == "builtin") {
            descriptor.source_kind = MetalKernelSourceKind::Builtin;
        } else if (*source_kind == "inline_source") {
            descriptor.source_kind = MetalKernelSourceKind::InlineSource;
        } else if (*source_kind == "metallib_path") {
            descriptor.source_kind = MetalKernelSourceKind::MetallibPath;
        } else {
            throw std::invalid_argument(
                "kernel_descriptor.source_kind must be one of builtin|inline_source|metallib_path");
        }
    }

    if (descriptor_obj.Contains("source_payload")) {
        auto source_payload = descriptor_obj.TryGetString("source_payload");
        if (!source_payload) {
            throw source_payload.error();
        }
        descriptor.source_payload = source_payload.value();
    }

    if (descriptor_obj.Contains("dispatch")) {
        auto dispatch_obj = descriptor_obj.TryGetObject("dispatch");
        if (!dispatch_obj) {
            throw dispatch_obj.error();
        }

        if (dispatch_obj->Contains("default_grid_x")) {
            auto v = dispatch_obj->TryGetInt("default_grid_x");
            if (!v) {
                throw v.error();
            }
            descriptor.dispatch.default_grid_x = static_cast<std::uint32_t>(std::max(1, v.value()));
        }
        if (dispatch_obj->Contains("default_grid_y")) {
            auto v = dispatch_obj->TryGetInt("default_grid_y");
            if (!v) {
                throw v.error();
            }
            descriptor.dispatch.default_grid_y = static_cast<std::uint32_t>(std::max(1, v.value()));
        }
        if (dispatch_obj->Contains("default_grid_z")) {
            auto v = dispatch_obj->TryGetInt("default_grid_z");
            if (!v) {
                throw v.error();
            }
            descriptor.dispatch.default_grid_z = static_cast<std::uint32_t>(std::max(1, v.value()));
        }
        if (dispatch_obj->Contains("default_block_x")) {
            auto v = dispatch_obj->TryGetInt("default_block_x");
            if (!v) {
                throw v.error();
            }
            descriptor.dispatch.default_block_x = static_cast<std::uint32_t>(std::max(1, v.value()));
        }
        if (dispatch_obj->Contains("default_block_y")) {
            auto v = dispatch_obj->TryGetInt("default_block_y");
            if (!v) {
                throw v.error();
            }
            descriptor.dispatch.default_block_y = static_cast<std::uint32_t>(std::max(1, v.value()));
        }
        if (dispatch_obj->Contains("default_block_z")) {
            auto v = dispatch_obj->TryGetInt("default_block_z");
            if (!v) {
                throw v.error();
            }
            descriptor.dispatch.default_block_z = static_cast<std::uint32_t>(std::max(1, v.value()));
        }
    }

    if (descriptor_obj.Contains("arg_layout")) {
        auto arg_layout_array = descriptor_obj.TryGetArray("arg_layout");
        if (!arg_layout_array) {
            throw arg_layout_array.error();
        }

        for (const auto& arg_view : arg_layout_array.value()) {
            MetalKernelArgDescriptor arg{};
            if (arg_view.Contains("kind")) {
                auto kind = arg_view.TryGetString("kind");
                if (!kind) {
                    throw kind.error();
                }
                if (*kind == "device_buffer") {
                    arg.kind = MetalKernelArgKind::DeviceBuffer;
                } else {
                    throw std::invalid_argument("kernel_descriptor.arg_layout.kind must be device_buffer");
                }
            }
            if (arg_view.Contains("access")) {
                auto access = arg_view.TryGetString("access");
                if (!access) {
                    throw access.error();
                }
                if (*access == "read_only") {
                    arg.access = MetalKernelArgAccess::ReadOnly;
                } else if (*access == "write_only") {
                    arg.access = MetalKernelArgAccess::WriteOnly;
                } else if (*access == "read_write") {
                    arg.access = MetalKernelArgAccess::ReadWrite;
                } else {
                    throw std::invalid_argument(
                        "kernel_descriptor.arg_layout.access must be read_only|write_only|read_write");
                }
            }
            descriptor.arg_layout.push_back(arg);
        }
    }

    return descriptor;
}

} // namespace graph::gpu::metal::capabilities
