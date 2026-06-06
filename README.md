# GraphX Build and CMake Reference

GraphX is a C++26 project built with CMake and organized into multiple libraries:
- libgraph
- libsensor
- libdsp
- libgpu

This README is the top-level build guide and CMake option reference.

## Requirements

- CMake 3.23 or newer
- A C++ compiler with C++26 support
- Ninja (required by default)
- Platform/toolchain dependencies vary by enabled GPU backends:
  - CUDA: CUDAToolkit discoverable by CMake
  - SYCL: compiler support for -fsycl (Clang or IntelLLVM)
  - Metal: Apple platform, Metal/Foundation/QuartzCore frameworks, and metal-cpp headers

## Quick Start (Presets)

Configure + build debug:

```bash
cmake --preset ninja-debug
cmake --build --preset build-debug
```

Run libgraph unit tests:

```bash
ctest --preset test-libgraph-unit
```

Run Metal runtime tests:

```bash
ctest --preset test-libgpu-metal-runtime --output-on-failure
```

Strict native-Metal validation lane:

```bash
cmake --preset ninja-debug-metal-native-strict
cmake --build --preset build-debug-metal-native-strict
ctest --preset test-libgpu-metal-runtime-strict --output-on-failure
```

## Presets

### Configure presets

- ninja-debug: Default development preset (Ninja, Debug)
- ninja-release: Release build (Ninja)
- ninja-debug-modules: Debug with module pilot enabled
- ninja-debug-metal-native: Debug with native Metal runtime requested
- ninja-debug-metal-native-strict: Same as above, but fails if native Metal runtime prerequisites are missing

### Build presets

- build-debug
- build-release
- build-libgraph-unit
- build-debug-metal-native
- build-debug-metal-native-strict

### Test presets

- test-libgraph-unit: Runs CTest tests matching libgraph_unit
- test-libgpu-metal-runtime: Runs CTest tests matching libgpu_metal_runtime
- test-libgpu-metal-runtime-strict: Same runtime test lane under strict native-Metal requirement

## Manual CMake Configure/Build

If you are not using presets:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

If you intentionally need a non-Ninja generator, disable the Ninja requirement:

```bash
cmake -S . -B build -G "Unix Makefiles" -DGRAPHX_REQUIRE_NINJA=OFF
```

## CMake Options (Complete)

The following options are defined by this repository.

### Top-level options (CMakeLists.txt)

| Option | Default | Description |
|---|---|---|
| GRAPHX_ENABLE_MODULE_PILOT | OFF | Enables experimental C++ module pilot mode. |
| GRAPHX_REQUIRE_NINJA | ON | Fails configure if generator is not Ninja. |
| BUILD_TESTS | ON | Builds test targets and enables CTest integration. |
| BUILD_DOCS | OFF | Enables docs build lane (for Doxygen/doc targets where available). |
| ENABLE_TSAN | OFF | Adds ThreadSanitizer compile/link flags (non-MSVC only). |
| ENABLE_CUDA_GRAPH_NODES | OFF | Requests CUDA graph node support in libgpu. |
| ENABLE_SYCL_GRAPH_NODES | ON | Requests SYCL graph node support in libgpu. |
| ENABLE_METAL_GRAPH_NODES | ON | Requests Metal graph node support in libgpu. |
| ENABLE_METAL_NATIVE_RUNTIME | ON | Requests native Metal runtime on Apple when prerequisites exist. |
| GRAPHX_REQUIRE_METAL_NATIVE_RUNTIME | OFF | Hard-fails configure/startup if native Metal runtime cannot be enabled. |
| MULTI_GPU_TESTS | OFF | Enables multi-GPU test lanes when supported by enabled backends. |

### Top-level cache variables (non-option settings)

| Variable | Default | Description |
|---|---|---|
| GRAPHX_METAL_CPP_INCLUDE_DIR | empty | Optional path to metal-cpp include root containing Metal/Metal.hpp. |
| CMAKE_CXX_STANDARD | 26 | Required C++ standard (project hard-requires C++26). |
| CMAKE_EXPORT_COMPILE_COMMANDS | ON | Emits compile_commands.json. |

### Subdirectory options

Defined in module/test CMake files and available at configure time:

| Option | Location | Default | Description |
|---|---|---|---|
| GRAPHX_BUILD_GPU_STUB_PLUGINS | libgpu/plugins/CMakeLists.txt | ON | Builds CPU-safe stub GPU plugins used by topology/lifecycle tests. |
| ENABLE_THREADPOOL_EXTENDED_TESTS | libgraph/test/CMakeLists.txt | OFF | Builds/runs extended ThreadPool benchmark/chaos/scaling tests. |

## GPU Backend Behavior and Gating

GraphX uses request options plus capability detection. Requesting a backend does not always guarantee activation.

- CUDA activation requires CUDAToolkit discovery.
- SYCL activation requires compiler support for -fsycl.
- Metal activation requires Apple platform.
- Native Metal runtime activation additionally requires Apple frameworks and metal-cpp headers.

When prerequisites are missing, GraphX emits warnings and disables the backend/runtime lane unless strict mode is requested.

## Strict Native Metal Mode

Enable strict mode to fail fast when native Metal runtime cannot be provided:

```bash
cmake --preset ninja-debug-metal-native-strict
```

Equivalent manual flags:

```bash
cmake -S . -B build-metal-strict -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DENABLE_METAL_GRAPH_NODES=ON \
  -DENABLE_METAL_NATIVE_RUNTIME=ON \
  -DGRAPHX_REQUIRE_METAL_NATIVE_RUNTIME=ON \
  -DGRAPHX_METAL_CPP_INCLUDE_DIR=/opt/homebrew/include
```

## Test Execution

With BUILD_TESTS=ON, top-level configure enables CTest and adds library test trees.

Examples:

```bash
# all tests known in a build directory
ctest --test-dir build --output-on-failure

# all tests in preset-configured tree
ctest --preset test-libgraph-unit
ctest --preset test-libgpu-metal-runtime --output-on-failure

# run specific regex/lane manually
ctest --test-dir build-ninja/ninja-debug -R libgraph_unit --output-on-failure
```

A summary target is also available in test-enabled builds:

```bash
cmake --build build-ninja/ninja-debug --target test-summary
```

## Install and Package Config

Default install prefix (when not explicitly set) is:

- <build-dir>/install

Install:

```bash
cmake --build build --target install
```

Top-level package config files are generated/installed for downstream consumers:

- GraphXConfig.cmake
- GraphXConfigVersion.cmake

Installed under:

- lib/cmake/GraphX (or platform-equivalent CMAKE_INSTALL_LIBDIR path)

## Troubleshooting

- Configure fails about Ninja:
  - Use Ninja generator, or set GRAPHX_REQUIRE_NINJA=OFF.
- Metal native runtime requested but unavailable:
  - Verify Apple platform.
  - Verify Metal/Foundation/QuartzCore frameworks.
  - Provide GRAPHX_METAL_CPP_INCLUDE_DIR if headers are not in default paths.
  - Use strict preset to enforce fail-fast behavior.
- CUDA or SYCL lane requested but not activated:
  - Check toolkit/compiler prerequisites and configure output warnings.
