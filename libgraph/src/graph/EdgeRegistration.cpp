// MIT License
//
// Copyright (c) 2025 Robert Klinkhammer
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

#include "graph/EdgeRegistration.hpp"
#include "graph/GraphManager.hpp"

// Note: Test node includes are conditionally compiled when building tests
// For now, we leave RegisterAllEdges() as a placeholder to be populated
// by test code

namespace graph::config {

/**
 * RegisterAllEdges()
 *
 * Called at program startup to register all valid edge type combinations.
 * This enables JSON graph loading to find the correct edge creator for any
 * (source_type, source_port, dest_type, dest_port) combination.
 *
 * When new node types are added, add corresponding registrations here.
 * 
 * Note: This is defined in the test code when running tests.
 */
void RegisterAllEdges() {
    // Placeholder: Edge registrations will be added as node types are implemented
    // Example registrations:
    // 
    // EdgeRegistration::Register<SimpleProducerNode, 0, SimpleConsumerNode, 0>();
    // EdgeRegistration::Register<DataInjectionAccelerometerNode, 0, FlightFSMNode, 0>();
}

}  // namespace graph::config
