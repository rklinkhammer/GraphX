# GraphX Consolidated Operations Guide

This is the consolidated operational documentation for GraphX, grouped by:

1. build
2. install
3. test
4. GOTCHA
5. SarPy
6. CRSD conversion and testing

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
- plan/reviews/SAR_GOTCHA_TO_CRSD_CURRENT_STATE.md

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
ctest --preset test-libgpu-metal-runtime --output-on-failure
ctest --preset test-libgpu-metal-runtime-strict --output-on-failure
```

### SAR unit test binary

```bash
./build-ninja/ninja-debug/examples/SAR/test/test_sar_example_unit
```

### Useful SAR focused filters

```bash
./build-ninja/ninja-debug/examples/SAR/test/test_sar_example_unit \
  --gtest_filter='GraphxGotchaToCrsdCliTest.*'

./build-ninja/ninja-debug/examples/SAR/test/test_sar_example_unit \
  --gtest_filter='GraphxSarNormalizedLaneTest.*'

./build-ninja/ninja-debug/examples/SAR/test/test_sar_example_unit \
  --gtest_filter='GraphxImageComparisonLaneTest.*'
```

### Main executable coverage test

`test_sar_main_executable.cpp` validates `examples/SAR/src/main.cpp` runtime and diagnostics output.

## 4) GOTCHA

### Dataset Reference

For authoritative GOTCHA field documentation and full-aperture conversion instructions, see:
[docs/sar/gotcha_large_scene_data_description.md](../sar/gotcha_large_scene_data_description.md)

### GOTCHA Preflight (Required for Local Data-Backed Workflows)

```bash
export GRAPHX_SAR_GOTCHA_DATASET=/path/to/gotcha/root
export GRAPHX_SAR_GOTCHA_MANIFEST=/path/to/gotcha/root/manifest.json
export GRAPHX_SAR_GOTCHA_CHECKSUMS=/path/to/gotcha/root/checksums.sha256
bash scripts/verify_gotcha_dataset.sh
```

### Generate Deterministic Sidecars + Manifest + Checksums

```bash
bash scripts/prepare_gotcha_subdata_json.sh /path/to/gotcha/subData
```

### Full-Aperture GOTCHA Conversion to Lite Format

When a local GOTCHA dataset is available, convert all pulses from all files using:

```bash
export GRAPHX_SAR_GOTCHA_DATASET=/path/to/gotcha/root
bash scripts/convert_gotcha_subdata_to_graphx_crsd_lite.sh
```

This performs:
- Full-aperture read (all `Np` pulses from every ordered file)
- Validation using PR1 required-field checks
- Output to `graphx-sar-normalized` (lite) format with all pulses preserved
- Aperture accounting in `conversion_report.json` showing:
  - `total_files_read`: Number of input files
  - `total_pulses_read`: Sum of Np across all files
  - `pulses_per_file`: Per-file pulse counts with filenames

Expected outputs in `$GRAPHX_SAR_GOTCHA_OUTPUT_DIR` (default: `/tmp/graphx_crsd_lite_full_aperture_conversion`):

- `gotcha_sar_normalized_index.json` — Product index and metadata
- `conversion_report.json` — Conversion results with aperture accounting
- `conversion_warnings.log` — Any warnings from the conversion
- `gotcha_sar_normalized_chunk_*.graphx-sar-normalized/` — Signal data chunks

### Local Full-Aperture Validation Tests (Optional, Local-Only)

When `GRAPHX_SAR_GOTCHA_DATASET` is set, run validation tests that verify:

```bash
./build-ninja/ninja-debug/examples/SAR/test/test_sar_example_unit \
  --gtest_filter='RealGotchaFullApertureValidationTest.*'
```

Tests:
- Verify all files are read and processed
- Confirm all pulses are preserved in conversion
- Validate lite output structure and metadata
- Check aperture accounting in conversion report

**Note:** These tests are **skipped in CI** when `GRAPHX_SAR_GOTCHA_DATASET` is not set. No dataset download is performed. This is a local-only optional workflow not required by CI.

### Legacy Real GOTCHA Validation (graphx-sar-normalized lane, Deprecated)

```bash
bash examples/SAR/tools/local_gotcha_validation.sh
```

Expected outputs include:

- `gotcha_sar_normalized_index.json`
- `conversion_report.json`
- `conversion_warnings.log`
- `gotcha_sar_normalized_chunk_*.graphx-sar-normalized/`

**Note:** Prefer `scripts/convert_gotcha_subdata_to_graphx_crsd_lite.sh` for full-aperture conversion.

### Local Frozen Scenario Replay Harness

```bash
python3 examples/SAR/tools/sar_local_runner.py \
  --scenario examples/SAR/scenarios/scenario_001.json \
  --output-dir /tmp/graphx_sar_scenario_001
```

This scaffolds GraphX and reference boundary scripts/contracts without auto-running external tools.

## 5) SarPy

SarPy integration is local-only and optional. It is not a GraphX runtime dependency.

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

### Modes

`graphx-gotcha-to-crsd` supports:

- `--mode graphx-sar-normalized` (non-standard GraphX intermediate)
- `--mode crsd` (standards-targeted/export path)

### Build converter if needed

```bash
cmake --build build-ninja/ninja-debug --target graphx_gotcha_to_crsd -j8
```

### Convert to graphx-sar-normalized

```bash
bash scripts/convert_gotcha_subdata_to_graphx_sar_normalized.sh \
  /path/to/gotcha/subData \
  /tmp/gotcha_graphx_sar_normalized_output
```

### Convert to CRSD

```bash
bash scripts/convert_gotcha_subdata_to_crsd.sh \
  /path/to/gotcha/subData \
  /tmp/gotcha_crsd_output
```

### Direct converter invocation

```bash
build-ninja/ninja-debug/examples/SAR/graphx-gotcha-to-crsd \
  --input-dir /path/to/gotcha/subData \
  --output-dir /tmp/gotcha_output \
  --collection-id local-gotcha-example \
  --max-output-size-mb 512 \
  --sort manifest \
  --manifest /path/to/gotcha/subData/manifest.json \
  --mode graphx-sar-normalized \
  --validate \
  --emit-index \
  --allow-classic-mat-with-sidecar
```

### Expected conversion root artifacts

- `gotcha_sar_normalized_index.json`
- `conversion_report.json`
- `conversion_warnings.log`

### CRSD validation testing (SarPy local-only)

```bash
python3 tools/sarpy/validate_crsd.py \
  validate \
  --input-crsd /path/to/file.crsd \
  --output-json /tmp/crsd_validation_report.json
```

### C++ tests to validate conversion lanes

- `GraphxGotchaToCrsdCliTest.*`
- `GraphxSarNormalizedLaneTest.*`
- `LocalGotchaValidationLaneTest.*`

Run from the SAR unit binary:

```bash
./build-ninja/ninja-debug/examples/SAR/test/test_sar_example_unit \
  --gtest_filter='GraphxGotchaToCrsdCliTest.*:GraphxSarNormalizedLaneTest.*:LocalGotchaValidationLaneTest.*'
```

## Notes and boundaries

- MATLAB is not a GraphX build/runtime/test dependency.
- SarPy tools are optional, local-only, and intended for validation/comparison.
- `graphx-sar-normalized` is a permanent non-standard GraphX intermediate format.
- For local data-backed lanes, keep dataset artifacts external to the repository.
