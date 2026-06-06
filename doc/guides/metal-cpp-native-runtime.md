# Metal Native Runtime (metal-cpp)

This project uses a pure C++ metal-cpp path for native Metal runtime support.

## Requirements

- Apple platform (macOS)
- Apple frameworks: Metal, Foundation, QuartzCore
- metal-cpp headers available at an include root containing:
  - Metal/Metal.hpp
  - Foundation/Foundation.hpp
  - QuartzCore/QuartzCore.hpp

## Configure and Build

Recommended: install headers into the repository-managed location first:

```bash
./scripts/install_metal_cpp.sh /path/to/metal-cpp-archive.zip
```

This installs to third_party/metal-cpp, which CMake auto-detects.

Use the native preset:

```bash
cmake --preset ninja-debug-metal-native
cmake --build --preset build-debug-metal-native
```

By default, the preset uses:

- GRAPHX_METAL_CPP_INCLUDE_DIR=/opt/homebrew/include

If your headers are elsewhere, override on configure:

```bash
cmake --preset ninja-debug-metal-native -DGRAPHX_METAL_CPP_INCLUDE_DIR=/path/to/metal-cpp/include
```

## Verify Native Path

Check configure output for:

- GraphX Metal native runtime: ON

Then run native runtime smoke tests:

```bash
ctest --preset test-libgpu-metal-runtime
```

If CTest reports "No tests were found", native runtime is still disabled for
that configure directory. Re-run configure and confirm it prints
"GraphX Metal native runtime: ON".

## Fallback Behavior

If headers are not found, GraphX automatically falls back to simulated Metal capabilities and prints a warning.
