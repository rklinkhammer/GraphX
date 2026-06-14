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

## SAR/GOTCHA Testing

The SAR example tests live under `examples/SAR` and are built into the
`test_sar_example_unit` target.

```bash
cmake --build build-ninja/ninja-debug --target test_sar_example_unit
```

The test executable is:

```bash
build-ninja/ninja-debug/examples/SAR/test/test_sar_example_unit
```

Registered CTest lanes include:

| CTest lane | Purpose |
|---|---|
| `sar_example_unit` | Full SAR example unit test binary. |
| `sar_example_ci_lane` | CI-safe SAR validation lane. |
| `sar_example_main_executable` | `examples/SAR/main.cpp` executable coverage. |
| `sar_example_sarpy_probe_lane` | Local-only/gated SarPy probe checks. |
| `sar_example_sarpy_integration_lane` | Local-only/gated SarPy integration checks. |
| `sar_real_gotcha_local_validation` | Disabled local-only real-data GOTCHA validation. |

### CI-Safe GOTCHA Coverage

Normal CI uses tiny synthetic fixtures and normalized replay fixtures. It does
not require real GOTCHA `.mat` data, MATLAB, or CRSD validation. MATLAB is not a
GraphX build-time, runtime, or test-time dependency and should not be added as
one.

`graphx-crsd-lite` is a permanent, non-standard GraphX intermediate format. It
is distinct from full CRSD, and full CRSD validation is not required for
`graphx-crsd-lite` lanes.

Useful focused commands:

```bash
# Run all SAR unit tests.
./build-ninja/ninja-debug/examples/SAR/test/test_sar_example_unit

# GOTCHA conversion CLI tests.
./build-ninja/ninja-debug/examples/SAR/test/test_sar_example_unit \
  '--gtest_filter=GraphxGotchaToCrsdCliTest.*'

# End-to-end GOTCHA -> normalized product -> graphx-crsd-lite -> reports.
./build-ninja/ninja-debug/examples/SAR/test/test_sar_example_unit \
  '--gtest_filter=Pr16GraphxCrsdLiteLaneTest.*'

# GraphX image output comparison against Python reference outputs.
./build-ninja/ninja-debug/examples/SAR/test/test_sar_example_unit \
  '--gtest_filter=Pr17GraphxImageComparisonLaneTest.*'

# Local GOTCHA validation gate. Real-data smoke skips unless explicitly enabled.
./build-ninja/ninja-debug/examples/SAR/test/test_sar_example_unit \
  '--gtest_filter=Pr18LocalGotchaValidationTest.*'
```

### GOTCHA Conversion CLI

The GOTCHA conversion utility is:

```bash
build-ninja/ninja-debug/examples/SAR/graphx-gotcha-to-crsd --help
```

Example `graphx-crsd-lite` conversion:

```bash
build-ninja/ninja-debug/examples/SAR/graphx-gotcha-to-crsd \
  --input-dir /path/to/gotcha_or_synthetic_mat_dir \
  --output-dir /tmp/graphx_gotcha_lite \
  --collection-id local-gotcha-example \
  --max-output-size-mb 512 \
  --sort manifest \
  --manifest /path/to/manifest.json \
  --mode graphx-crsd-lite \
  --validate \
  --emit-index
```

Use `--sort lexical` when deterministic filename ordering is sufficient. Use
`--sort manifest --manifest <path>` when the input order must be pinned by a
manifest.

The lite conversion emits:

| Output | Description |
|---|---|
| `gotcha_crsd_index.json` | Deterministic root index for converted outputs. |
| `conversion_report.json` | Root conversion summary, validation status, and checksums. |
| `conversion_warnings.log` | Deterministic warning log, or `none`. |
| `gotcha_crsd_chunk_*.graphx-crsd-lite/` | Non-standard lite chunk directories. |

### GOTCHA Environment Variables

| Variable | Use |
|---|---|
| `GRAPHX_SAR_ALLOW_EXTERNAL_DATA` | Allows external fixture paths in local/manual replay-style tests. |
| `GRAPHX_SAR_GOTCHA_DATASET` | Required for real local GOTCHA `.mat` validation. |
| `GRAPHX_SAR_GOTCHA_MANIFEST` | Optional manifest override; defaults to `${GRAPHX_SAR_GOTCHA_DATASET}/manifest.json`. |
| `GRAPHX_SAR_GOTCHA_CHECKSUMS` | Optional checksum override; defaults to `${GRAPHX_SAR_GOTCHA_DATASET}/checksums.sha256`. |
| `GRAPHX_SAR_GOTCHA_TO_CRSD_BIN` | Optional override for the `graphx-gotcha-to-crsd` executable. |
| `GRAPHX_SAR_GOTCHA_OUTPUT_DIR` | Optional output directory for local real-data validation. |
| `GRAPHX_SAR_GOTCHA_COLLECTION_ID` | Optional collection id for local real-data validation. |
| `GRAPHX_SAR_GOTCHA_MAX_OUTPUT_SIZE_MB` | Optional chunk size limit for local real-data validation. |
| `GRAPHX_SARPY_CRSD_FILE` | Optional local-only CRSD file for SarPy smoke validation. |
| `GOTCHA_DIR` | Local GOTCHA dataset path used by gotcha-back reference scripts. |
| `GOTCHA_BACK_BIN` | Local gotcha-back `sarbp` executable used by reference scripts. |

### Local-Only Real GOTCHA Validation

Real GOTCHA `.mat` validation is explicitly local-only and disabled by default.
It is never required by normal CI, performs no dataset download, and must not add
or check in GOTCHA data.

Preflight checks are handled by:

```bash
scripts/verify_gotcha_dataset.sh
```

The local conversion workflow is:

```bash
export GRAPHX_SAR_GOTCHA_DATASET=/path/to/local/gotcha_mat_directory
export GRAPHX_SAR_GOTCHA_MANIFEST=/path/to/manifest.json
export GRAPHX_SAR_GOTCHA_CHECKSUMS=/path/to/checksums.sha256

bash scripts/verify_gotcha_dataset.sh
bash examples/SAR/tools/local_gotcha_validation.sh
```

`examples/SAR/tools/local_gotcha_validation.sh` runs the existing
`graphx-gotcha-to-crsd` `graphx-crsd-lite` lane, verifies root report artifacts,
and requires `GRAPHX_SAR_GOTCHA_DATASET` before doing any work.

To inspect the disabled CTest lane:

```bash
ctest --test-dir build-ninja/ninja-debug -N -V | grep sar_real_gotcha_local_validation
```

### Python/SarPy Reference Tools

Python/SarPy tools are local/reference tooling only. They do not alter GraphX
runtime contracts and are not required by normal CI.

Relevant tools:

| Tool | Purpose |
|---|---|
| `tools/sarpy/reference_image_from_gotcha.py` | Local GOTCHA field discovery and deterministic reference image generation. |
| `tools/sarpy/compare_images.py` | Image comparison report and difference PNG generation. |
| `tools/sarpy/validate_crsd.py` | Optional local-only SarPy CRSD validation harness. |
| `tools/sarpy/reference_image_from_crsd.py` | Optional local-only CRSD reference magnitude extraction. |

Probe examples:

```bash
python3 tools/sarpy/reference_image_from_gotcha.py \
  probe-environment --output-json /tmp/ref_probe.json

python3 tools/sarpy/compare_images.py \
  probe-environment --output-json /tmp/cmp_probe.json

python3 tools/sarpy/validate_crsd.py \
  probe-environment --output-json /tmp/crsd_validate_probe.json

python3 tools/sarpy/reference_image_from_crsd.py \
  probe-environment --output-json /tmp/crsd_ref_probe.json
```

Optional CRSD smoke validation is gated by `GRAPHX_SARPY_CRSD_FILE`:

```bash
export GRAPHX_SARPY_CRSD_FILE=/path/to/local/file.crsd
./build-ninja/ninja-debug/examples/SAR/test/test_sar_example_unit \
  '--gtest_filter=Pr14SarpyCrsdHarnessTest.OptionalLocalSmokeRunsWhenSarpyAndCrsdPathAreAvailable'
```

### gotcha-back Reference Workflow

`examples/SAR/tools/gotcha_back_adapter.py` prepares local gotcha-back
reference invocations and normalizes gotcha-back output artifacts for comparison.
This is comparator/reference tooling only and is not required by CI.

Set local paths before using the generated reference script:

```bash
export GOTCHA_DIR=/path/to/unpacked/GOTCHA
export GOTCHA_BACK_BIN=/path/to/gotcha-back/sarbp
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
