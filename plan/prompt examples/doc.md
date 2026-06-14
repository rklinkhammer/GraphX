# GraphX Operations And Architecture Guide

## Quick Start

Minimum sequence for a first successful local run (CI-safe, no real GOTCHA data required):

```bash
# 1) Configure + build
cmake --preset ninja-debug
cmake --build --preset build-debug

# 2) Run core CTest lanes
ctest --preset test-libgraph-unit
ctest --preset test-libgpu-metal-runtime --output-on-failure

# 3) Build and run SAR unit binary
cmake --build --preset build-sar-example-unit
./build-ninja/ninja-debug-metal-native/examples/SAR/test/test_sar_example_unit

# 4) Optional: if you have local GOTCHA dataset, run full-aperture local conversion
export GRAPHX_SAR_GOTCHA_DATASET=/path/to/gotcha/root
bash scripts/convert_gotcha_subdata_to_graphx_crsd_lite.sh
```

MATLAB is not required.

## 1. Architecture Specification

### High-Level Architecture Summary

GraphX is a C++26 graph runtime with modular libraries and plugin-resolved execution backends. The SAR path is token-based and uses explicit accel-token contracts for GPU transfer/kernel stages.

Core top-level libraries:

- `libgraph`: graph runtime, config parser, resolver/provider composition, execution.
- `libgpu`: backend-agnostic and backend-specific GPU abstractions/capabilities.
- `libdsp`: DSP components.
- `libsensor`: sensor-related components.
- `examples/SAR`: SAR pipeline nodes, configs, tooling, and tests.

### Major Modules/Libraries And Responsibilities

- `libgraph`
	- Parses JSON topology config (`execution_backend`, `edge_contract`, `resolver_mappings`).
	- Builds graph with resolver overlays and provider-backed node instantiation.
- `libgpu`
	- Owns accel transport types (`DeviceBufferView`, `HostPinnedBufferView`, `TransferTicket`, `KernelTicket`, etc.).
	- Provides Metal capabilities and kernel execution primitives.
- `examples/SAR`
	- Defines SAR token contract (`SarSidecar`, `SarAccelControlToken`).
	- Implements SAR stages (`RangeWindowNode`, `RangeCompressionNode`, `AzimuthTileSplitNode`, `H2DAsyncAccelNode`, `SarBackprojectionTransformAccelNode`, `D2HAsyncAccelNode`, `ImageTileMergeNode`, `SarDiagnosticsSinkNode`).
	- Provides GOTCHA conversion utility `graphx-gotcha-to-crsd` and local scripts.

### Data Flow For SAR And GOTCHA Conversion Paths

SAR runtime path (definitive topology):

```mermaid
flowchart LR
	SRC[SyntheticApertureIqSourceNode]
	WIN[RangeWindowNode]
	RC[RangeCompressionNode]
	SPLIT[AzimuthTileSplitNode]
	H2D[H2DAsyncAccelNode]
	BP[SarBackprojectionTransformAccelNode]
	D2H[D2HAsyncAccelNode]
	MERGE[ImageTileMergeNode]
	DIAG[SarDiagnosticsSinkNode]

	SRC --> WIN --> RC --> SPLIT --> H2D --> BP --> D2H --> MERGE --> DIAG
```

GOTCHA conversion path (current implemented behavior):

- Input `.mat` + sidecars -> ordering/validation -> normalized SAR product -> non-standard GraphX intermediate output + reports.
- Local-only validation path exists and is environment-gated.

### Runtime/Plugin/Resolver Model

- JSON config declares portable node intents and resolver settings.
- `edge_contract: "accel-token"` is used for SAR tokenized edges.
- Resolver mappings in SAR configs bind SAR intents to concrete backend-capable nodes.
- Backend preference supports `auto` and explicit backend selection with fallback policy.
- Plugin loading is dynamic; SAR plugins and shared GPU plugins can be provided via plugin directories.

### Standard Vs Non-Standard Output Format

- Non-standard intermediate format is explicitly used for the implemented conversion lane.
- Repository documents and tests indicate the non-standard lane is the operational path today.
- Standards CRSD writer path is represented in CLI mode surface but repository state documents unresolved/limited implementation status.

Important ambiguity in repo docs/scripts:

- Some docs/scripts use `graphx-crsd-lite` naming.
- Other docs/scripts use `graphx-sar-normalized` naming.

Safest operational guidance:

- Use the maintained script `scripts/convert_gotcha_subdata_to_graphx_crsd_lite.sh` and inspect output metadata/index labels to confirm actual format fields in your build.

## 2. Build Instructions

### Prerequisites

- CMake 3.23+
- C++26-capable compiler
- Ninja (recommended/default)
- macOS Metal-native lanes: Apple frameworks + metal-cpp headers
- Python 3 for SAR tooling/SarPy scripts

### Configure Commands (Recommended First)

Recommended debug configure:

```bash
cmake --preset ninja-debug
```

Recommended debug Metal-native configure:

```bash
cmake --preset ninja-debug-metal-native
```

Strict Metal-native configure (fail-fast if native prerequisites missing):

```bash
cmake --preset ninja-debug-metal-native-strict
```

Alternative release configure:

```bash
cmake --preset ninja-release-metal-native
```

### Build Commands For Default And Common Variants

Default debug build:

```bash
cmake --build --preset build-debug
```

Release build:

```bash
cmake --build --preset build-release
```

Metal-native debug build:

```bash
cmake --build --preset build-debug-metal-native
```

Strict Metal-native debug build:

```bash
cmake --build --preset build-debug-metal-native-strict
```

### Build Targets Important For SAR/GOTCHA

```bash
cmake --build --preset build-debug --target sar_example test_sar_example_unit graphx_gotcha_to_crsd
```

Dedicated SAR unit target via preset:

```bash
cmake --build --preset build-sar-example-unit
```

### Expected Outputs And Where Binaries Are Located

Preset binary directories are under:

- `build-ninja/<configure-preset-name>/...`

Common outputs:

- SAR example executable:
	- `build-ninja/ninja-debug/examples/SAR/sar_example`
	- or `build-ninja/ninja-debug-metal-native/examples/SAR/sar_example`
- SAR unit test binary:
	- `build-ninja/ninja-debug/examples/SAR/test/test_sar_example_unit`
	- or `build-ninja/ninja-debug-metal-native/examples/SAR/test/test_sar_example_unit`
- GOTCHA converter:
	- `build-ninja/ninja-debug/examples/SAR/graphx-gotcha-to-crsd`
	- or `build-ninja/ninja-debug-metal-native/examples/SAR/graphx-gotcha-to-crsd`

### Quick Validation Checks After Build

```bash
# smoke: list converter options
build-ninja/ninja-debug/examples/SAR/graphx-gotcha-to-crsd --help || true

# run SAR unit binary
./build-ninja/ninja-debug-metal-native/examples/SAR/test/test_sar_example_unit

# run main SAR executable with definitive config
./build-ninja/ninja-debug-metal-native/examples/SAR/sar_example \
	examples/SAR/config/sar_stripmap_definitive.json \
	./build-ninja/ninja-debug-metal-native/examples/SAR/plugins
```

## 3. CTest Instructions

### How To List Tests

Using preset build tree:

```bash
ctest --preset test-libgraph-unit -N
ctest --preset test-libgpu-metal-runtime -N
```

Direct by test directory:

```bash
ctest --test-dir build-ninja/ninja-debug-metal-native -N
```

### How To Run Full Test Suites

Core lanes:

```bash
ctest --preset test-libgraph-unit
ctest --preset test-libgpu-metal-runtime --output-on-failure
```

Strict lane:

```bash
ctest --preset test-libgpu-metal-runtime-strict --output-on-failure
```

SAR unit binary full run:

```bash
./build-ninja/ninja-debug-metal-native/examples/SAR/test/test_sar_example_unit
```

### How To Run Focused SAR/GOTCHA Subsets

```bash
# GOTCHA CLI behavior
./build-ninja/ninja-debug-metal-native/examples/SAR/test/test_sar_example_unit \
	--gtest_filter='GraphxGotchaToCrsdCliTest.*'

# Non-standard conversion lane
./build-ninja/ninja-debug-metal-native/examples/SAR/test/test_sar_example_unit \
	--gtest_filter='GraphxSarNormalizedLaneTest.*'

# Local real-data full-aperture validation (requires env var)
./build-ninja/ninja-debug-metal-native/examples/SAR/test/test_sar_example_unit \
	--gtest_filter='RealGotchaFullApertureValidationTest.*'
```

### How To Run Tests With Labels/Filters

SarPy-labeled CTest lane:

```bash
ctest --test-dir build-ninja/ninja-debug-metal-native -L sarpy --output-on-failure
```

Named preset for SarPy-labeled tests:

```bash
ctest --preset test-sar-sarpy-lane --output-on-failure
```

### How To Interpret Pass/Skip/Fail In This Project

- `PASS`: expected behavior in current environment.
- `SKIP`: often expected for local-only/data-gated tests when required env vars/tools are absent.
- `FAIL`: regression, missing dependency, invalid config, or unsupported runtime setup.

### Typical Causes Of Skips And How To Enable Skipped Local-Only Tests

Common skip gates:

- Real GOTCHA tests: `GRAPHX_SAR_GOTCHA_DATASET` not set.
- SarPy smoke/integration: SarPy packages not installed or CRSD input env var not set.

Enable local-only GOTCHA tests:

```bash
export GRAPHX_SAR_GOTCHA_DATASET=/path/to/gotcha/root
export GRAPHX_SAR_GOTCHA_MANIFEST=/path/to/gotcha/root/manifest.json
export GRAPHX_SAR_GOTCHA_CHECKSUMS=/path/to/gotcha/root/checksums.sha256
bash scripts/verify_gotcha_dataset.sh
```

Enable optional SarPy CRSD smoke:

```bash
python3 -m pip install -r tools/sarpy/requirements.txt
export GRAPHX_SARPY_CRSD_FILE=/path/to/local/file.crsd
```

## 4. GOTCHA To CRSD Conversion Instructions

### Required Inputs And Dataset Assumptions

- Local GOTCHA dataset directory with `.mat` files (commonly `subData01.mat` ... `subData10.mat`).
- Dataset-side artifacts expected by preflight scripts:
	- `manifest.json`
	- `checksums.sha256`
- Source data is treated as processed phase history.

### Required Environment Variables

Required for local data-backed conversion:

```bash
export GRAPHX_SAR_GOTCHA_DATASET=/path/to/gotcha/root
```

Recommended explicit preflight variables:

```bash
export GRAPHX_SAR_GOTCHA_MANIFEST=/path/to/gotcha/root/manifest.json
export GRAPHX_SAR_GOTCHA_CHECKSUMS=/path/to/gotcha/root/checksums.sha256
```

Optional overrides:

```bash
export GRAPHX_SAR_GOTCHA_TO_CRSD_BIN=/path/to/graphx-gotcha-to-crsd
export GRAPHX_SAR_GOTCHA_OUTPUT_DIR=/tmp/graphx_crsd_lite_full_aperture_conversion
export GRAPHX_SAR_GOTCHA_COLLECTION_ID=local-gotcha-example
export GRAPHX_SAR_GOTCHA_MAX_OUTPUT_SIZE_MB=512
export GRAPHX_SAR_GOTCHA_MANIFEST=/path/to/gotcha/root/manifest.json
```

### Preflight Checks

```bash
bash scripts/verify_gotcha_dataset.sh
```

If sidecars/manifest/checksums need generation:

```bash
bash scripts/prepare_gotcha_subdata_json.sh /path/to/gotcha/subData
```

### Command(s) To Run Conversion

Recommended local full-aperture script:

```bash
bash scripts/convert_gotcha_subdata_to_graphx_crsd_lite.sh
```

Alternative wrappers:

```bash
bash scripts/convert_gotcha_subdata_to_graphx_sar_normalized.sh /path/to/gotcha/subData /tmp/out
bash scripts/convert_gotcha_subdata_to_crsd.sh /path/to/gotcha/subData /tmp/out
```

Direct CLI invocation:

```bash
build-ninja/ninja-debug-metal-native/examples/SAR/graphx-gotcha-to-crsd \
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

### Meaning Of Key CLI Options

- `--input-dir`: input dataset directory.
- `--output-dir`: output root directory.
- `--collection-id`: collection identifier in metadata/reporting.
- `--max-output-size-mb`: chunking threshold.
- `--sort manifest|lexical`: ordering mode.
- `--manifest`: manifest file path when manifest sort is used.
- `--mode`: output mode (`graphx-sar-normalized` or `crsd`, depending on build support).
- `--validate`: run normalized product validation.
- `--emit-index`: emit root-level index/report artifacts.
- `--allow-classic-mat-with-sidecar`: allow sidecar-assisted classic MAT handling.

### Output Files Produced And How To Validate Them

Typical output artifacts:

- `gotcha_sar_normalized_index.json`
- `conversion_report.json`
- `conversion_warnings.log`
- chunk directories such as `gotcha_sar_normalized_chunk_*.graphx-sar-normalized/`

Validate outputs:

```bash
ls -la "$GRAPHX_SAR_GOTCHA_OUTPUT_DIR"
cat "$GRAPHX_SAR_GOTCHA_OUTPUT_DIR"/conversion_report.json
```

Check aperture accounting in report:

- `total_files_read`
- `total_pulses_read`
- `pulses_per_file`

### Distinguish graphx-sar-normalized/lite Outputs Vs Standards CRSD Outputs

- Non-standard GraphX intermediate output is the operationally documented path.
- Standards CRSD output path exists in interface/docs/scripts but repository docs also indicate incomplete writer support.
- Safest fallback when strict standards CRSD output is required:
	- run non-standard conversion lane for deterministic artifacts,
	- then run SarPy probe/validation commands against candidate CRSD files you already have,
	- treat CRSD conversion mode as experimental until validated in your exact build lane.

## 5. SarPy Image Creation From CRSD

### Required Python/SarPy Dependencies

```bash
python3 -m pip install -r tools/sarpy/requirements.txt
```

### How To Validate CRSD Inputs First

Environment probe:

```bash
python3 tools/sarpy/validate_crsd.py probe-environment --output-json /tmp/crsd_probe.json
```

Validation pass:

```bash
python3 tools/sarpy/validate_crsd.py \
	validate \
	--input-crsd /path/to/local/file.crsd \
	--output-json /tmp/crsd_validation_report.json
```

### Command(s) For Generating Reference Images From CRSD

```bash
python3 tools/sarpy/reference_image_from_crsd.py \
	generate-reference \
	--input-crsd /path/to/local/file.crsd \
	--output-magnitude-png /tmp/crsd_reference_magnitude.png \
	--output-metadata-json /tmp/crsd_reference_metadata.json
```

### Output Artifacts And How To Compare/Inspect Them

From CRSD reference generation:

- `/tmp/crsd_reference_magnitude.png`
- `/tmp/crsd_reference_metadata.json`

If you also have GraphX candidate arrays and a reference array, compare with:

```bash
python3 tools/sarpy/compare_images.py \
	compare \
	--reference-npy /tmp/reference_image.npy \
	--candidate-npy /tmp/candidate_image.npy \
	--output-report-json /tmp/comparison_report.json \
	--output-diff-magnitude-png /tmp/difference_magnitude.png \
	--output-phase-difference-png /tmp/phase_difference.png
```

### Local-Only Limitations And Gating Behavior

- SarPy tooling is local-only and optional.
- It is not a GraphX runtime dependency.
- Optional tests are skip-gated when SarPy packages or required environment variables are missing.
- Typical gate for CRSD smoke tests:
	- `GRAPHX_SARPY_CRSD_FILE` must point to a readable CRSD file.

## 6. Troubleshooting Appendix

### Build Errors

- Generator/toolchain mismatch:
	- Use presets first (`ninja-debug`, `ninja-debug-metal-native`).
- Missing metal-cpp headers for Metal-native lane:
	- Run `scripts/install_metal_cpp.sh`.
	- Or set `GRAPHX_METAL_CPP_INCLUDE_DIR` during configure.
- Strict Metal-native preset fails:
	- Use non-strict preset while validating host prerequisites.

### Missing Plugin/Provider Issues

- SAR executable cannot resolve nodes:
	- Ensure SAR plugin directory is passed.
	- If using shared GPU plugins, pass additional plugin directory.
- Resolver backend mismatch:
	- Confirm `execution_backend` and `backend_fallback_policy` in JSON config.

### CTest Discovery Or Runtime Errors

- No tests found in directory:
	- Ensure configure/build completed with test-enabled preset.
	- Use `ctest --test-dir <build-dir> -N` to confirm discovery.
- Unexpected skips:
	- Check gating env vars (`GRAPHX_SAR_GOTCHA_DATASET`, `GRAPHX_SARPY_CRSD_FILE`).
	- Check SarPy packages installed.

### Conversion Failures (Manifest/Order/Field Validation)

- Preflight failures:
	- Verify dataset root, manifest path, checksums path.
	- Run `scripts/verify_gotcha_dataset.sh` before conversion.
- Missing required sidecar fields:
	- Regenerate sidecars using `scripts/prepare_gotcha_subdata_json.sh`.
- Ordering/manifest mismatch:
	- Use explicit `--sort` and `--manifest` parameters.

### SarPy Environment And Package Issues

- Probe first:
	- `reference_image_from_gotcha.py probe-environment`
	- `compare_images.py probe-environment`
	- `validate_crsd.py probe-environment`
	- `reference_image_from_crsd.py probe-environment`
- Package import errors:
	- Reinstall from `tools/sarpy/requirements.txt` in active Python environment.
- CRSD validation errors:
	- Ensure input is a readable CRSD file and inspect generated validation JSON for exact failure point.
