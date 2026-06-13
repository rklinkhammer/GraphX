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
