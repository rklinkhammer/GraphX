# GOTCHA Large Scene Data Description Summary

**Authoritative Reference:** This document is the authoritative reference for GOTCHA field validation in GraphX. The field inventory, names, and types defined below are used to validate GOTCHA MAT ingestion and ensure conversion completeness.

Source reviewed:

- `/Users/rklinkhammer/workspace/Gotcha-Large-Scene-Data/subData/Data_Description.pdf`

## Summary

The PDF describes the "Large Scene Gotcha Data Example" dataset. The dataset
contains ten MATLAB files named `subData01.mat` through `subData10.mat`, with a
combined size of about 5.9 GB.

Each file contains the same data fields:

| Field | Meaning | Type | Required |
| --- | --- | --- | --- |
| `Np` | Number of pulses in the file | integer | ✅ |
| `K` | Number of data samples per pulse | integer | ✅ |
| `deltaF` | Frequency step size between samples, in hertz | number | ✅ |
| `minF` | Frequency of the first sample, in hertz | number | ✅ |
| `AntX` | Radar antenna phase-center x position relative to scene center, in meters | number | ✅ |
| `AntY` | Radar antenna phase-center y position relative to scene center, in meters | number | ✅ |
| `AntZ` | Radar antenna phase-center z position relative to scene center, in meters | number | ✅ |
| `R0` | Distance from radar antenna phase center to scene center, in meters | number | ✅ |
| `phdata` | Processed radar phase-history data array | array/object/string | ✅ |

The antenna phase-center coordinates are in a local Cartesian coordinate system
whose origin is the scene center. The PDF text spells the z-coordinate field as
`AntZ` in the field list and later as `antZ` in prose; the implementation should
treat `AntZ` as the authoritative field name unless field inventory proves
otherwise.

The ten files may be concatenated to form one single aperture. The PDF states
that this combined aperture produces a SAR image with roughly one-foot azimuth
resolution.

## Project Implications

The project should account for the following dataset facts:

- The source is processed phase history, not raw radar collection data.
- `phdata` is the signal array source for GOTCHA ingestion.
- `Np` pulses per file should be preserved. A full-aperture conversion should
  ingest all pulses from all ten files, not only one selected pulse from each
  file.
- Lexical ordering of `subData01.mat` through `subData10.mat` is meaningful for
  the single-aperture concatenation unless an explicit manifest overrides it.
- `K`, `deltaF`, and `minF` define the frequency/sample axis and should drive
  normalized waveform metadata, bandwidth, center/carrier-frequency derivation,
  and CRSD signal metadata.
- `AntX`, `AntY`, `AntZ`, and `R0` provide the geometry needed for platform or
  phase-center position metadata, reference range, and PVP/support-array
  population.
- The coordinate frame is local Cartesian with scene center as origin. CRSD
  output should either preserve that clearly as local/derived geometry or map it
  through a documented geodetic reference if one is later introduced.
- The PDF does not document polarization, antenna pattern, absolute collection
  time, geodetic scene center, platform velocity, transmit waveform details, or
  calibration terms beyond the listed fields. Those CRSD fields must therefore
  be derived, marked unknown/not modeled, or supplied by an additional source.

## Local Validation and Conversion

When a local copy of the GOTCHA dataset is available, use the full-aperture
conversion workflow to ingest all pulses from all ten files and generate CRSD
output.

### Required Environment

```bash
export GRAPHX_SAR_GOTCHA_DATASET=/path/to/local/subData
```

The dataset directory must contain top-level `.mat` files (`subData01.mat`
through `subData10.mat`) and manifest/checksum artifacts:

```text
manifest.json
checksums.sha256
```

### Full-Aperture Conversion

```bash
bash scripts/convert_gotcha_subdata_to_crsd.sh /path/to/local/subData /tmp/gotcha_crsd_out
```

This script:

- Verifies the dataset integrity using `scripts/verify_gotcha_dataset.sh`.
- Runs `graphx-gotcha-to-crsd` in full-aperture mode.
- Outputs CRSD products (`*/product.crsd`) with all pulses from all files.
- Emits `conversion_report.json` with aperture accounting showing total files
  read, total pulses read, and per-file pulse counts.
- Does not download any data or require MATLAB.

### Local Test Suite

When `GRAPHX_SAR_GOTCHA_DATASET` is set, the test suite includes additional
validation tests that verify:

- All ten GOTCHA files are processed.
- The conversion preserves all pulses from each file.
- The conversion report correctly accounts for total and per-file pulse counts.
- CRSD output metadata is valid and complete.

Run tests with:

```bash
# Full-aperture validation tests (requires GRAPHX_SAR_GOTCHA_DATASET)
export GRAPHX_SAR_GOTCHA_DATASET=/path/to/local/subData
./build/examples/SAR/test/test_sar_example_unit \
  --gtest_filter='RealGotchaFullApertureValidationTest.*'
```

Tests are skipped gracefully in CI and default builds when the environment
variable is not set.

## PR9 Local-Only CRSD Validation Workflow

Use this workflow to validate end-to-end focused-image processing from generated
CRSD segment layout while keeping CI independent of real GOTCHA data.

### Input layout

The expected local CRSD root contains an ordered set:

- `subData01.crsd_output/gotcha_crsd_chunk_0000.crsd/product.crsd`
- ...
- `subData10.crsd_output/gotcha_crsd_chunk_0000.crsd/product.crsd`

Treat these ten products as one ordered aperture set for one focused output.

### Optional local validation lane

```bash
export GRAPHX_SAR_CRSD_ROOT=/path/to/crsd/root
./build-ninja/ninja-debug-metal-native/examples/SAR/test/test_sar_example_unit \
  --gtest_filter='LocalGotchaValidationLaneTest.OptionalSmokeRunsOnlyWhenRealDatasetEnvironmentIsSet'
```

The lane verifies, when local data exists:

- one focused artifact set (`.bin`, `.json`, `.pgm`) is produced
- nonzero focused-image response
- per-segment input checksums, ordered-set checksum, and output checksum are recorded
- dropping or reordering one segment fails or changes output deterministically

Boundary rules:

- local-only/opt-in; CI does not require this lane
- no dataset download and no generated-output check-in
- `product.crsd` is authoritative for signal/PVP
- `metadata.json`, `pvp.json`, `chunk_index.json`, `provenance.json`, and SarPy validation JSON are optional sidecar evidence only

## Follow-Up Work To Consider

- Update GOTCHA ingestion so the normalized product can represent every pulse in
  each MAT file and then concatenate all ten files into a single aperture.
- Verify the actual `phdata` shape and orientation for each file and document
  whether samples are stored as `K x Np`, `Np x K`, or another layout.
- Add field-inventory checks that require or report `Np`, `K`, `deltaF`,
  `minF`, `AntX`, `AntY`, `AntZ`, `R0`, and `phdata`.
- Ensure conversion reports state when only a subset of pulses is used.
- Use `R0` explicitly in reference geometry and PVP mapping rather than
  replacing it with an inferred scene-center range.
- Keep MATLAB out of the build, runtime, and test dependency chain. The PDF
  documents MATLAB file content, but it does not require MATLAB execution.

