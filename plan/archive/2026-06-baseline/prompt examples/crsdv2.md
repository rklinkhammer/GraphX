> ARCHIVAL STATUS (2026-06-14): This prompt template is retained for historical context and contains deprecated lane names/flags. Use the active CRSD-only operations guide at `plan/prompt examples/doc.md`.

Act as PLANNER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Goal:
Implement a GraphX command-line utility that converts ordered GOTCHA MATLAB `.mat` phase-history files into ordered CRSD received-signal files, and define a SarPy-based Python reference workflow for generating reference imagery for later GraphX comparison.

Use these reports as context:

* `plan/reviews/SAR_INSPECTOR_REPORT.md`
* `plan/reviews/SAR_SIMPLIFIER_REPORT.md`
* `plan/reviews/SAR_PLANNER_REPORT.md`

Stop after the planner report.

Save the report as:

```text
plan/reviews/SAR_PLANNER_REPORT.md
```

---

# Important Constraint

Do not use MATLAB directly.

MATLAB must never become:

* a build dependency
* a runtime dependency
* a test dependency

GOTCHA `.mat` files must be read from C++ using open file-format support.

Support strategy:

### MATLAB v7.3

Read through HDF5.

### Classic/non-HDF5 MAT files

Support using:

* a C++ MAT reader library,
* a documented supported subset,
* or a deterministic unsupported-format error.

Python reference tools may use:

* scipy.io.loadmat
* h5py

for validation only.

---

# Background

CRSD means Compensated Received Signal Data.

Processing chain:

```text
CRSD
↓
CPHD
↓
SICD
↓
formed images
```

CRSD contains:

* collection metadata
* timing
* channels
* signal arrays
* support arrays
* PVP arrays
* geometry
* antenna metadata

SarPy provides:

```text
sarpy.io.received
sarpy.io.received.crsd
sarpy.io.received.crsd1_elements
```

SarPy is a validation tool.

It must not shape GraphX runtime architecture.

---

# Important Caveat

GOTCHA is not raw AESA receive data.

It is closer to compensated phase history.

Therefore:

Initial support may produce:

```text
single-channel pseudo-CRSD
```

Document all assumptions.

---

# Planning Principle

Do not combine:

* architecture cleanup
* repository discovery
* MAT inspection
* normalization
* graphx-crsd-lite
* CRSD writing
* SarPy validation
* image formation
* comparison tooling

into one PR.

Use many small PRs.

---

# Mandatory Repository Discovery PR

Repository discovery is a dedicated planning PR.

No implementation PR may precede it.

Repository discovery shall identify:

* build system placement
* CLI conventions
* test framework
* fixture conventions
* existing SAR abstractions
* dependency policy
* HDF5 availability
* MAT reader gaps
* proposed files

Deliver:

```text
docs/sar/gotcha_crsd_repo_discovery.md
```

---

# Important Architectural Rule

GOTCHA is an importer.

CRSD is an exporter.

Future formats are exporters.

The internal model is independent.

Architecture:

```text
GOTCHA
    ↓
Importer
    ↓
GraphX normalized model
    ↓
Writers

graphx-crsd-lite
CRSD
CPHD
SICD
```

No GOTCHA field names may leak beyond importer code.

No external SAR package may shape GraphX contracts.

SarPy remains validation infrastructure only.

---

# Full CRSD Is The Final Target

Do not assume CRSD writing exists.

Planner must first determine:

* whether a writer already exists
* whether SarPy validation succeeds

If not:

Implement:

```text
graphx-crsd-lite
```

first.

Never fake standards compliance.

In:

```text
--mode crsd
```

files must either:

* pass SarPy open/read validation

or

* fail clearly.

---

# graphx-crsd-lite

graphx-crsd-lite is not a temporary debugging format.

It is a permanent GraphX intermediate representation.

It must:

* preserve provenance
* preserve pulse ordering
* preserve assumptions
* support round-trip tests

It must be clearly labeled:

```text
NON-STANDARD
```

---

# Deliverables

---

# 1. CRSD Definition Document

Create:

```text
docs/sar/crsd_definition.md
```

Include:

* CRSD overview
* CRSD → CPHD → SICD
* collection metadata
* timing
* channel metadata
* PVP
* support arrays
* signal arrays
* geometry
* antenna concepts

Mappings:

```text
GOTCHA
↓
GraphX
↓
CRSD
```

Schemas:

```text
gotcha_crsd_index.json
conversion_report.json
```

Explicitly state:

```text
MATLAB is not used.
```

---

# 2. Repository Discovery

Create:

```text
docs/sar/gotcha_crsd_repo_discovery.md
```

Document:

* build system
* target placement
* CLI conventions
* test framework
* fixture conventions
* dependency policy
* HDF5 gaps
* MAT gaps
* SAR abstractions

---

# 3. MAT Inspection Phase

Purpose:

Understand GOTCHA before normalization.

Outputs:

```text
field_inventory.json
conversion_assumptions.json
```

Inventory:

* keys
* shapes
* dtypes

No normalized products yet.

No CRSD yet.

---

# 4. C++ Data Model

Namespace:

```cpp
graphx::sar
```

Core types:

```cpp
ComplexSample
PulseVector
ChannelSignal
WaveformMetadata
PlatformState
PerVectorParameters
CollectionMetadata
ReferenceGeometry
NormalizedSarProduct
```

Signal model:

```text
signal[pulse][channel][sample]
```

---

# Reader/Writer Interfaces

```cpp
class ISarReader
{
};

class ISarWriter
{
};
```

Concrete readers:

```cpp
GotchaMatReader
GraphxCrsdLiteReader
CrsdReader
```

Writers:

```cpp
GraphxCrsdLiteWriter
CrsdWriter
```

Utility objects:

```cpp
SarProductChunker
SarProductValidator
```

---

# 5. GotchaMatReader

Requirements:

* ordered directory
* manifest ordering
* provenance
* deterministic ordering

Extract:

* IQ samples
* frequency axis
* positions
* times
* polarization

Preserve original field names in diagnostics.

---

# 6. Product Validator

Checks:

* NaN
* Inf
* metadata completeness
* pulse ordering
* shape consistency
* sample types

Validator reused by:

* GotchaMatReader
* graphx-crsd-lite
* CRSD writer

---

# 7. Internal Normalization

Never write CRSD directly from GOTCHA.

Normalize:

```text
signal[pulse][channel][sample]
```

Initial channel count:

```text
1
```

Metadata:

* collection id
* source files
* pulse count
* sample count
* positions
* bandwidth
* center frequency
* times
* velocity
* polarization

---

# 8. graphx-crsd-lite

Outputs:

```text
signal.bin
metadata.json
index.json
conversion_report.json
```

Round-trip support required.

Reader required.

Tests:

* write/read
* checksum
* metadata propagation

---

# 9. CRSD Writer

Modes:

```text
--mode graphx-crsd-lite
--mode crsd
```

Chunking:

Never split pulses.

Naming:

```text
gotcha_crsd_000000.crsd
```

Metadata:

* provenance
* chunk index
* pulse ranges
* PVP

CRSD validity must be verified by SarPy.

---

# 10. CLI

Executable:

```text
graphx-gotcha-to-crsd
```

Arguments:

```text
--input-dir
--output-dir
--collection-id
--max-output-size-mb
--sort
--manifest
--mode
--validate
--emit-index
```

Errors must be deterministic.

---

# 11. Reports

Generate:

```text
gotcha_crsd_index.json
conversion_report.json
conversion_warnings.log
```

Include:

* provenance
* assumptions
* checksums
* warnings
* validation status

---

# 12. Validation

C++ validation:

* ordering
* shape
* NaN
* metadata

Python validation:

```text
tools/sarpy/validate_crsd.py
```

Checks:

* CRSD version
* dimensions
* dtype
* sample slices
* PVP arrays

Emit JSON report.

---

# 13. Python Reference Tools

Files:

```text
reference_image_from_gotcha.py
reference_image_from_crsd.py
compare_images.py
```

Libraries:

```text
numpy
scipy
h5py
matplotlib
sarpy
```

These tools are:

* local-only
* comparison infrastructure

not runtime dependencies.

---

# 14. End-to-End graphx-crsd-lite

Exercise:

```text
GOTCHA
↓
normalized model
↓
graphx-crsd-lite
↓
reports
```

without CRSD requirements.

---

# 15. Full CRSD

Only after:

* graphx-crsd-lite
* validator
* SarPy harness

exist.

Acceptance:

SarPy must open generated files.

---

# 16. Image Formation

Reference:

```text
GOTCHA
↓
Python backprojection
↓
reference image
```

GraphX:

```text
GOTCHA
↓
GraphX
↓
image
```

Comparison metrics:

* RMSE
* phase error
* peak error
* correlation
* SSIM

Outputs:

```text
comparison_report.json
difference_magnitude.png
phase_difference.png
```

---

# 17. Local-Only Real GOTCHA Validation

Environment:

```text
GRAPHX_SAR_GOTCHA_DATASET
```

No downloads.

No checked-in data.

Explicit enable required.

---

# 18. Future Export Targets

Future writers:

```text
graphx-crsd-lite
CRSD
CPHD
SICD
```

All implement:

```cpp
ISarWriter
```

---

# Tests

C++:

* ordering
* chunking
* metadata
* validator
* graphx-crsd-lite round-trip
* CRSD smoke tests

Python:

* field discovery
* reference images
* SarPy validation
* image metrics

Use synthetic fixtures.

Never require GOTCHA data in CI.

---

# Phased Delivery

Phase 0

Documentation

Phase 1

Repository discovery

Phase 2

MAT inspection

Phase 3

Internal model

Phase 4

graphx-crsd-lite

Phase 5

CLI and reports

Phase 6

SarPy harness

Phase 7

Full CRSD

Phase 8

Reference images

Phase 9

GraphX comparison

Stop after the planner report.


```text
Act as IMPLEMENTER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Task:
Update the top-level README.md to document all examples/SAR testing information related to GOTCHA.

Scope:
- Edit README.md only unless a README link target is missing and a minimal docs link fix is required.
- Do not implement code.
- Do not add dependencies.
- Do not change tests or CMake behavior.
- Do not download datasets.
- Do not check in GOTCHA data.
- Preserve existing README structure and add a clear SAR/GOTCHA testing section.

Content to include:
1. SAR test target overview:
   - Build target: `test_sar_example_unit`
   - Executable: `build-ninja/ninja-debug/examples/SAR/test/test_sar_example_unit`
   - CTest lanes:
     - `sar_example_unit`
     - `sar_example_ci_lane`
     - `sar_example_main_executable`
     - `sar_example_sarpy_probe_lane`
     - `sar_example_sarpy_integration_lane`
     - `sar_real_gotcha_local_validation` as disabled/local-only/real-data/gated

2. CI-safe GOTCHA/SAR tests:
   - Tiny synthetic fixtures and normalized replay fixtures are used in CI.
   - No real GOTCHA `.mat` files are required for CI.
   - No MATLAB dependency exists or should be added.
   - `graphx-crsd-lite` is permanent, non-standard, and distinct from full CRSD.
   - Full CRSD validation is not required for lite lanes.

3. Important focused test commands:
   - Build SAR unit tests:
     `cmake --build build-ninja/ninja-debug --target test_sar_example_unit`
   - Run all SAR unit tests:
     `./build-ninja/ninja-debug/examples/SAR/test/test_sar_example_unit`
   - GOTCHA CLI tests:
     `./build-ninja/ninja-debug/examples/SAR/test/test_sar_example_unit '--gtest_filter=GraphxGotchaToCrsdCliTest.*'`
   - End-to-end lite lane:
     `./build-ninja/ninja-debug/examples/SAR/test/test_sar_example_unit '--gtest_filter=Pr16GraphxCrsdLiteLaneTest.*'`
   - GraphX image comparison lane:
     `./build-ninja/ninja-debug/examples/SAR/test/test_sar_example_unit '--gtest_filter=Pr17GraphxImageComparisonLaneTest.*'`
   - Local GOTCHA validation gate:
     `./build-ninja/ninja-debug/examples/SAR/test/test_sar_example_unit '--gtest_filter=Pr18LocalGotchaValidationTest.*'`

4. GOTCHA conversion CLI examples:
   - Help:
     `build-ninja/ninja-debug/examples/SAR/graphx-gotcha-to-crsd --help`
   - Synthetic/local lite conversion example using:
     `--input-dir`
     `--output-dir`
     `--collection-id`
     `--max-output-size-mb`
     `--sort lexical|manifest`
     `--manifest`
     `--mode graphx-crsd-lite`
     `--validate`
     `--emit-index`
   - Explain generated lite outputs:
     - `gotcha_crsd_index.json`
     - `conversion_report.json`
     - `conversion_warnings.log`
     - `gotcha_crsd_chunk_*.graphx-crsd-lite/`

5. Environment variables:
   - `GRAPHX_SAR_ALLOW_EXTERNAL_DATA`
     - Used to allow external fixture paths in local/manual replay-style tests.
   - `GRAPHX_SAR_GOTCHA_DATASET`
     - Required for real local GOTCHA `.mat` validation.
   - `GRAPHX_SAR_GOTCHA_MANIFEST`
     - Optional override, defaults to `${GRAPHX_SAR_GOTCHA_DATASET}/manifest.json`.
   - `GRAPHX_SAR_GOTCHA_CHECKSUMS`
     - Optional override, defaults to `${GRAPHX_SAR_GOTCHA_DATASET}/checksums.sha256`.
   - `GRAPHX_SAR_GOTCHA_TO_CRSD_BIN`
     - Optional override for `graphx-gotcha-to-crsd`.
   - `GRAPHX_SAR_GOTCHA_OUTPUT_DIR`
     - Optional local validation output directory.
   - `GRAPHX_SAR_GOTCHA_COLLECTION_ID`
     - Optional collection id for local validation.
   - `GRAPHX_SAR_GOTCHA_MAX_OUTPUT_SIZE_MB`
     - Optional chunk size limit for local validation.
   - `GRAPHX_SARPY_CRSD_FILE`
     - Optional local-only CRSD smoke validation input.
   - `GOTCHA_DIR`
     - Used by gotcha-back local reference scripts.
   - `GOTCHA_BACK_BIN`
     - Used by gotcha-back local reference scripts.

6. Local-only real GOTCHA workflow:
   - Document `scripts/verify_gotcha_dataset.sh`.
   - Document `scripts/convert_gotcha_subdata_to_crsd.sh`.
   - Show setup:
     `export GRAPHX_SAR_GOTCHA_DATASET=/path/to/local/gotcha_mat_directory`
     `bash scripts/convert_gotcha_subdata_to_crsd.sh /path/to/local/gotcha_mat_directory /tmp/gotcha_crsd_out`
   - State this workflow is disabled by default, local-only, and never required by normal CI.

7. Python/SarPy reference and comparison tools:
   - Mention `tools/sarpy/reference_image_from_gotcha.py`
   - Mention `tools/sarpy/compare_images.py`
   - Mention `tools/sarpy/validate_crsd.py`
   - Mention `tools/sarpy/reference_image_from_crsd.py`
   - State these are local/reference tooling and do not alter GraphX runtime contracts.
   - Include probe command examples.

8. gotcha-back local reference workflow:
   - Mention `examples/SAR/tools/rrp3_gotcha_back_adapter.py`
   - Mention `GOTCHA_DIR` and `GOTCHA_BACK_BIN`
   - State it is comparator/reference tooling only and not required by CI.

Validation:
- Run a README-focused grep or markdown sanity check if available.
- Do not run real GOTCHA validation unless `GRAPHX_SAR_GOTCHA_DATASET` is already set.
- If tests are run, prefer README-adjacent focused checks only; do not modify code to satisfy docs.

Output the standard IMPLEMENTER summary:
1. Files changed.
2. Files deleted.
3. Tests added.
4. Tests removed.
5. Build/test command, if any.
6. Remaining follow-up work.
```