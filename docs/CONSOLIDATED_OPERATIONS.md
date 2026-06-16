# GraphX Consolidated Operations Guide

This is the consolidated operational documentation for GraphX, grouped by:

1. build
2. install
3. test
4. GOTCHA
5. SarPy
6. CRSD conversion and testing

## Canonical SAR Lanes

Use these three lanes as the canonical SAR workflow model:

1. GOTCHA -> CRSD conversion
  - Primary command: `bash scripts/convert_gotcha_subdata_to_crsd.sh <input_dir> <output_dir>`
2. CRSD quick-look validation (local-only)
  - Primary command: `python3 tools/sarpy/reference_image_from_crsd.py generate-reference ...`
3. CRSD -> focused GraphX image
  - Primary lane: focused SAR test/config execution (for example `GraphxImageComparisonLaneTest.*`)

Boundary:
- Quick-look output is validation/inspection only and not a focused-image substitute.
- Focused-image evidence must come from GraphX focused-image path tests/artifacts.

Primary source docs used for this consolidation:

- README.md
- examples/SAR/README.md
- doc/guides/metal-cpp-native-runtime.md
- docs/sar/crsd_definition.md
- docs/sar/gotcha_input_manifest_schema.md
- docs/sar/gotcha_crsd_repo_discovery.md
- tools/CRSD_Convert.md
- tools/sarpy/README.md
- examples/SAR/tools/local_gotcha_validation.md
- examples/SAR/tools/sar_local_runner.md
- examples/SAR/tools/sarpy_metadata_harness.md

## 1) Build

### Requirements

- CMake 3.23+
- C++26-capable compiler
- Ninja (default and recommended)

### Configure and build (default)

```bash
cmake --preset ninja-debug
cmake --build --preset build-debug
```

### Configure and build (release)

```bash
cmake --preset ninja-release-metal-native
cmake --build --preset build-release-metal-native
```

### Configure and build (debug metal-native)

```bash
cmake --preset ninja-debug-metal-native
cmake --build --preset build-debug-metal-native
```

### Strict native Metal lane

```bash
cmake --preset ninja-debug-metal-native-strict
cmake --build --preset build-debug-metal-native-strict
```

### Build SAR targets explicitly

```bash
cmake --build --preset build-debug --target sar_example test_sar_example_unit graphx_gotcha_to_crsd
cmake --build --preset build-debug-metal-native --target sar_example test_sar_example_unit graphx_gotcha_to_crsd
```

### Build libgraph unit test executable

```bash
cmake --build --preset build-libgraph-unit
cmake --build --preset build-libgraph-unit-metal-native
cmake --build --preset build-libgraph-unit-metal-native-strict
cmake --build --preset build-sar-example-unit
cmake --build --preset build-sar-example-unit-metal-native
cmake --build --preset build-sar-example-unit-metal-native-strict
```

## 2) Install

### Project/tool prerequisites

- CMake + Ninja + C++26 compiler
- On macOS Metal-native lanes: Apple frameworks (Metal, Foundation, QuartzCore)
- Python 3 for SAR utility tooling and SarPy helpers

### Install metal-cpp headers

```bash
./scripts/install_metal_cpp.sh /path/to/metal-cpp-archive.zip
```

Default install destination is `third_party/metal-cpp`.

If headers are elsewhere, override at configure time:

```bash
cmake --preset ninja-debug-metal-native -DGRAPHX_METAL_CPP_INCLUDE_DIR=/path/to/metal-cpp/include
```

### Install SarPy tool dependencies (local-only)

```bash
python3 -m pip install -r tools/sarpy/requirements.txt
```

`tools/sarpy/requirements.txt` includes: `numpy`, `scipy`, `h5py`, `matplotlib`, `sarpy`.

## 3) Test

### Core CTest lanes

```bash
ctest --preset test-libgraph-unit
ctest --preset test-libgraph-unit-metal-native
ctest --preset test-libgraph-unit-metal-native-strict
ctest --preset test-sar-example-unit
ctest --preset test-sar-example-unit-metal-native
ctest --preset test-sar-example-unit-metal-native-strict
ctest --preset test-libgpu-metal-runtime --output-on-failure
ctest --preset test-libgpu-metal-runtime-strict --output-on-failure
```

`test-libgraph-unit` uses the plain `ninja-debug` configure preset.
`test-libgraph-unit-metal-native` uses `ninja-debug-metal-native` and matches the default `build-debug` lane.
`test-libgraph-unit-metal-native-strict` uses `ninja-debug-metal-native-strict`.
`test-sar-example-unit` uses the plain `ninja-debug` configure preset.
`test-sar-example-unit-metal-native` uses `ninja-debug-metal-native`.
`test-sar-example-unit-metal-native-strict` uses `ninja-debug-metal-native-strict`.

### SAR unit test binary

```bash
./build-ninja/ninja-debug/examples/SAR/test/test_sar_example_unit
```

### Useful SAR focused filters

```bash
./build-ninja/ninja-debug/examples/SAR/test/test_sar_example_unit \
  --gtest_filter='GraphxGotchaToCrsdCliTest.*'

./build-ninja/ninja-debug/examples/SAR/test/test_sar_example_unit \
  --gtest_filter='GraphxImageComparisonLaneTest.*'
```

### Main executable coverage test

`test_sar_main_executable.cpp` validates `examples/SAR/src/main.cpp` runtime and diagnostics output.

## 4) GOTCHA

### Dataset Reference

For authoritative GOTCHA field documentation and full-aperture conversion instructions, see:
[docs/sar/gotcha_large_scene_data_description.md](../sar/gotcha_large_scene_data_description.md)

### GOTCHA Preflight (Local Data-Backed Workflows)

```bash
export GRAPHX_SAR_GOTCHA_DATASET=/path/to/gotcha/root
```

### Full-Aperture GOTCHA Conversion to CRSD

When a local GOTCHA dataset is available, convert all pulses from all files using:

```bash
export GRAPHX_SAR_GOTCHA_DATASET=/path/to/gotcha/root
bash scripts/convert_gotcha_subdata_to_crsd.sh /path/to/gotcha/root /tmp/gotcha_crsd_out
```

This performs:
- Full-aperture read (all `Np` pulses from every ordered file)
- Output to CRSD chunk directories (`*/product.crsd`)
- Optional report artifacts when enabled (`ENABLE_EMIT_INDEX=1`)

This is lane 1 of the canonical SAR workflow.

### Local Full-Aperture Validation Tests (Optional, Local-Only)

When `GRAPHX_SAR_GOTCHA_DATASET` is set, run validation tests that verify:

```bash
./build-ninja/ninja-debug/examples/SAR/test/test_sar_example_unit \
  --gtest_filter='RealGotchaFullApertureValidationTest.*'
```

Tests:
- Verify all files are read and processed
- Confirm all pulses are preserved in conversion
- Validate CRSD output structure and metadata
- Check aperture accounting in conversion report

**Note:** These tests are **skipped in CI** when `GRAPHX_SAR_GOTCHA_DATASET` is not set. No dataset download is performed. This is a local-only optional workflow not required by CI.

### PR9 Local CRSD Validation Lane (Optional, Local-Only)

After converting GOTCHA MAT files to CRSD segment outputs, validate focused-image
processing from the ordered CRSD set:

```bash
export GRAPHX_SAR_CRSD_ROOT=/path/to/crsd/root
./build-ninja/ninja-debug-metal-native/examples/SAR/test/test_sar_example_unit \
  --gtest_filter='LocalGotchaValidationLaneTest.OptionalSmokeRunsOnlyWhenRealDatasetEnvironmentIsSet'
```

Expected CRSD layout under `GRAPHX_SAR_CRSD_ROOT`:

- `subData01.crsd_output/gotcha_crsd_chunk_0000.crsd/product.crsd`
- ...
- `subData10.crsd_output/gotcha_crsd_chunk_0000.crsd/product.crsd`

The lane is disabled by default in CTest and confirms:

- one focused artifact set for the full ordered aperture
- nonzero focused-image response
- per-segment checksums, ordered-set checksum, output checksum
- deterministic change or failure on dropped/reordered segments

Sidecar JSON (`metadata.json`, `pvp.json`, `chunk_index.json`, `provenance.json`,
SarPy validation JSON) is evidence only and not authoritative signal/PVP input.

### Local Frozen Scenario Replay Harness

```bash
python3 examples/SAR/tools/sar_local_runner.py \
  --scenario examples/SAR/scenarios/scenario_001.json \
  --output-dir /tmp/graphx_sar_scenario_001
```

This scaffolds GraphX and reference boundary scripts/contracts without auto-running external tools.

## 5) SarPy

SarPy integration is local-only and optional. It is not a GraphX runtime dependency.

This section primarily describes lane 2 (CRSD quick-look validation).

### Environment probes

```bash
python3 tools/sarpy/reference_image_from_gotcha.py probe-environment --output-json /tmp/ref_probe.json
python3 tools/sarpy/compare_images.py probe-environment --output-json /tmp/cmp_probe.json
python3 tools/sarpy/validate_crsd.py probe-environment --output-json /tmp/crsd_probe.json
python3 tools/sarpy/reference_image_from_crsd.py probe-environment --output-json /tmp/crsd_ref_probe.json
```

### Generate reference image from fixture JSON

```bash
python3 tools/sarpy/reference_image_from_gotcha.py \
  generate-reference \
  --input-json /path/to/complex_fixture.json \
  --output-reference-npy /tmp/reference.npy \
  --output-magnitude-png /tmp/reference.png \
  --output-metadata-json /tmp/reference_metadata.json
```

### Compare GraphX candidate vs reference image

```bash
python3 tools/sarpy/compare_images.py \
  compare \
  --reference-npy /tmp/reference.npy \
  --candidate-npy /tmp/candidate.npy \
  --output-report-json /tmp/comparison_report.json \
  --output-diff-magnitude-png /tmp/diff.png \
  --output-phase-difference-png /tmp/phase_diff.png
```

### SarPy metadata harness

```bash
python3 examples/SAR/tools/sarpy_metadata_harness.py \
  probe-environment \
  --output-json /tmp/graphx_sarpy_metadata_probe.json

python3 examples/SAR/tools/sarpy_metadata_harness.py \
  normalize-metadata \
  --scenario examples/SAR/scenarios/scenario_001.json \
  --input-json /path/to/local/sarpy_metadata.json \
  --output-json /tmp/graphx_sarpy_metadata_contract.json
```

## 6) CRSD Conversion And Testing

### Mode

`graphx-gotcha-to-crsd` supports CRSD conversion for the supported operational lane.

This section plus focused SAR unit lanes represent lane 3 (CRSD -> focused GraphX image).

Important input constraint:

- HDF5-backed MAT v7.3 input is consumed directly.
- Classic MATLAB MAT v5 input is now auto-preprocessed to HDF5-backed MAT files by `scripts/convert_gotcha_subdata_to_crsd.sh` before conversion.
- Set `PREPROCESS_CLASSIC_MAT=0` to disable auto-preprocessing and enforce strict HDF5-only input.
- The converter binary must be built with HDF5 support; when preprocessing is enabled, the script will attempt to reconfigure the selected build directory with `HDF5_ROOT` (or Homebrew `hdf5` on macOS).

### Build converter if needed

```bash
cmake --build build-ninja/ninja-debug --target graphx_gotcha_to_crsd -j8
```

### Convert to CRSD

```bash
bash scripts/convert_gotcha_subdata_to_crsd.sh \
  /path/to/gotcha/subData \
  /tmp/gotcha_crsd_output
```

For multi-file GOTCHA directories, the wrapper defaults to per-file conversion:
each input `.mat` file is staged alone and written under a separate
`<subDataNN>.crsd_output/` directory. This keeps the current C++ normalized
product and Python CRSD handoff bounded. Use `PROCESS_PER_FILE=0` only for small
debug datasets where a single full-aperture process is intentional.

Classic MAT preprocessing reuses existing converted HDF5 files by default. Set
`PREPROCESS_OVERWRITE=1` to force regeneration.

Optional preprocessing controls:

```bash
PREPROCESS_CLASSIC_MAT=1 \
PREPROCESS_OUTPUT_DIR=/tmp/gotcha_hdf5_preprocessed \
bash scripts/convert_gotcha_subdata_to_crsd.sh /path/to/gotcha/subData /tmp/gotcha_crsd_output
```

### Direct converter invocation

```bash
build-ninja/ninja-debug/examples/SAR/graphx-gotcha-to-crsd \
  --input-dir /path/to/gotcha/subData \
  --output-dir /tmp/gotcha_output \
  --collection-id local-gotcha-example \
  --max-output-size-mb 0 \
  --sort lexical \
  --mode crsd
```

### Expected conversion root artifacts

- `*/product.crsd`
- Optional (when enabled): `gotcha_crsd_index.json`, `conversion_report.json`, `conversion_warnings.log`

### CRSD validation testing (SarPy local-only)

```bash
python3 tools/sarpy/validate_crsd.py \
  validate \
  --input-crsd /path/to/file.crsd \
  --output-json /tmp/crsd_validation_report.json
```

### C++ tests to validate conversion lanes

- `GraphxGotchaToCrsdCliTest.*`

Run from the SAR unit binary:

```bash
./build-ninja/ninja-debug/examples/SAR/test/test_sar_example_unit \
  --gtest_filter='GraphxGotchaToCrsdCliTest.*'
```

## Notes and boundaries

- MATLAB is not a GraphX build/runtime/test dependency.
- SarPy tools are optional, local-only, and intended for validation/comparison.
- For local data-backed lanes, keep dataset artifacts external to the repository.
