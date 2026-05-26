// MIT License
//
// Copyright (c) 2025 GraphX Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

/// @file PortMetadataHelper.hpp
/// @brief Helper utilities for converting C++ PortMetadata to C-compatible format
///
/// This header provides template functions to assist plugin implementations in creating
/// NodeFacade function pointers for GetInputPortMetadata, GetOutputPortMetadata, and
/// FreePortMetadata. It handles the conversion from C++ nodes to C-compatible structs
/// with owned string storage.
///
/// Usage Example:
/// @code
/// struct MyNodeInstance {
///     std::unique_ptr<MyNode> node;
/// };
///
/// extern "C" {
///     PortMetadataC* my_get_input_metadata(NodeHandle handle, size_t* out_count) {
///         return graph::GetInputPortMetadataImpl<MyNode>(handle, out_count);
///     }
///     
///     PortMetadataC* my_get_output_metadata(NodeHandle handle, size_t* out_count) {
///         return graph::GetOutputPortMetadataImpl<MyNode>(handle, out_count);
///     }
///     
///     void my_free_metadata(PortMetadataC* metadata) {
///         graph::FreePortMetadataImpl(metadata);
///     }
/// }
/// @endcode

#pragma once

#include "graph/NodeFacade.hpp"
#include "graph/NamedNodes.hpp"
#include <cstring>
#include <log4cxx/logger.h>

namespace graph {


}  // namespace graph

