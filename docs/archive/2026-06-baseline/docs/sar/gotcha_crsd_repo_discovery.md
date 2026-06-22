# GOTCHA To CRSD Repository Discovery

## Scope

This document records the current repository state for GOTCHA-to-CRSD repository
discovery. It is discovery only: no code, dependency, build, test, or
CRSD definition changes are included here.

MATLAB is not used by this repository for this work and must not become a
build-time, runtime, or test-time dependency. GOTCHA MATLAB files, when supported,
must be read from C++ through file format readers such as HDF5 for MAT v7.3 or a
separate C++ classic MAT strategy.

## Build System Placement

The repository uses CMake from the root `CMakeLists.txt`. The root build requires
CMake 3.23 or newer, requires Ninja through the `GRAPHX_REQUIRE_NINJA` check, and
sets C++26 as the required language standard. The root build adds the core
library directories `libgraph`, `libsensor`, `libdsp`, and `libgpu`.

SAR code is currently placed under `examples/SAR` and is controlled by the
`GRAPHX_BUILD_EXAMPLES_SAR` option. When enabled, `examples/SAR/CMakeLists.txt`
builds the `sar_example` and `sar_benchmark` executables from local SAR source
files and links them with existing GraphX libraries such as `graph`, `gpu`,
`dsp`, and `log4cxx`.

The CMake presets define Ninja debug and release configurations, along with
module and Metal-native variants. Existing test presets are focused on libgraph
and libgpu; SAR tests are wired through the SAR test CMake file rather than a
dedicated preset.

The most natural current placement for GOTCHA-to-CRSD work is therefore under
`examples/SAR`, with headers in `examples/SAR/include/sar/...`, implementation
files in `examples/SAR/src/...`, CLI entry points under `examples/SAR/src` or a
future local tools subdirectory, and tests under `examples/SAR/test`. Nothing in
this discovery suggests placing GOTCHA or CRSD conversion code in `libgraph`,
`libgpu`, or another core library at this stage.

## CLI Conventions

The existing SAR example executable uses a direct C++ CLI convention rather than
a shared command-line framework. `examples/SAR/src/main.cpp` accepts a scenario
configuration path, a plugin directory, and optional additional plugin
directories.

The SAR benchmark executable is also a C++ command-line program. Repository-local
Python SAR tools use `argparse`, but those tools are support utilities rather
than the convention for production C++ execution.

No repository-wide C++ CLI abstraction was identified for the GOTCHA-to-CRSD
utility. A future converter should use an explicit C++ executable with stable,
deterministic arguments and should be added to the SAR CMake area when the
implementation PR reaches that point.

## Test Framework

SAR tests are defined in `examples/SAR/test/CMakeLists.txt`. They use GoogleTest,
CTest, `Threads`, and `log4cxx`, and build one broad executable named
`test_sar_example_unit`. That target compiles both test files and selected SAR
implementation files directly.

The SAR test target defines fixture and configuration paths with CMake compile
definitions, including paths for SAR test fixtures, SAR scenarios, the source
root, build output directories, and optional GPU plugin outputs. CTest entries
include `sar_example_unit`, `sar_example_ci_lane`, and
`sar_example_main_executable`.

Future GOTCHA-to-CRSD tests should follow this existing GTest and CTest placement
unless the repository first introduces a separate SAR test target layout.

## Fixture Conventions

Existing SAR fixtures live under `examples/SAR/test/fixtures`,
`examples/SAR/fixtures`, and `examples/SAR/scenarios`. The current GOTCHA replay
path uses normalized JSON fixtures, including the small CI-oriented fixture
`examples/SAR/fixtures/scenario_001/scenario_001_ci_tiny_gotcha_fixture.json`.

The existing fixture set is designed for deterministic local testing and does
not include raw GOTCHA `.mat` files. JSON fixture metadata includes provenance
and checksum-oriented fields for the existing replay path.

Future GOTCHA-to-CRSD tests should keep CI fixtures small and deterministic.
Large external GOTCHA data should remain outside normal CI unless a later PR
adds an explicit gated local-data path.

## Dependency Policy

The SAR CMake area currently depends on existing repository libraries and common
test/runtime dependencies already used by the project, including GoogleTest,
Threads, and log4cxx. Optional GPU backends are already handled through existing
repository build options and plugin targets.

No CMake integration for MATLAB, MATIO, HDF5, h5py, SarPy, or another CRSD/MAT
dependency was identified in the active C++ build files inspected for this PR.
The existing SarPy harness is a local Python validation helper and is not part of
the C++ runtime architecture.

New dependencies for GOTCHA-to-CRSD must be introduced explicitly in later PRs.
MATLAB must remain excluded from build, runtime, and test dependency graphs.

## HDF5 Availability

No existing `find_package(HDF5)` integration or repository-local HDF5 wrapper was
identified in the inspected CMake and SAR source tree. HDF5 availability is
therefore currently unknown from the repository build system's point of view.

If MAT v7.3 support is implemented later, that PR must add explicit HDF5
detection, target wiring, and test gating. The discovery result here does not
establish HDF5 as currently available.

## Classic MAT Reader Gaps

No C++ classic MAT reader implementation was identified in the repository. No
MATIO integration was found, and the current GOTCHA support path is not a MAT
reader.

`examples/SAR/include/sar/GotchaReplaySourceNode.hpp` describes the existing
GOTCHA input as normalized JSON replay records. It also contains a TODO stating
that direct AFRL GOTCHA reading should replace fixture-based replay only after
raw data layout, calibration terms, and pulse redistribution are confirmed.

Future GOTCHA MAT support must therefore choose one of these paths explicitly:
support MAT v7.3 through HDF5, add a C++ classic MAT reader strategy, or reject
unsupported classic MAT files with a clear diagnostic. MATLAB itself is not an
acceptable strategy.

## Existing SAR Abstractions

The current SAR pipeline has reusable abstractions in `examples/SAR/include/sar`.
Important existing types include:

- `SarSidecar`, which carries SAR identity and metadata.
- `SarAccelControlToken`, which combines SAR sidecar metadata with acceleration
  and transfer control metadata.
- `SarIqSample`, which represents complex I/Q sample values.
- Source, range compression, backprojection, sink, and comparison nodes used by
  the existing example pipeline.
- `GotchaReplaySourceNode`, which emits normalized GOTCHA replay pulses from
  JSON rather than raw MAT data.

The comments in `SarMessages.hpp` are explicit that SAR identity is carried by
the sidecar, while transfer events and host/device pointers are transport
metadata. Future CRSD and GOTCHA conversion code should preserve that separation.

Repository-local SAR support tools include benchmark and replay helpers, product
comparison helpers, and a SarPy metadata harness. The SarPy harness is local-only
support and should not become the production C++ conversion mechanism.

## Existing CRSD Writer Support

No existing C++ CRSD writer support was identified in the repository. Searches
for CRSD-related implementation code found planning and review documents, but no
current CRSD writer target, CRSD file model, CRSD XML model, CRSD binary block
writer, or CRSD validation executable in the active source tree.

The existing SarPy-related script is not CRSD writer support. It is a local
validation harness for SAR product metadata behavior and does not provide a C++
CRSD output path.

## Proposed Files For Later PRs

The following file areas are consistent with the current repository layout, but
they should be created only in the implementation PRs that own them:

- `docs/sar/crsd_definition.md` for the CRSD definition document.
- `examples/SAR/include/sar/io/...` for GOTCHA MAT and
  CRSD public C++ interfaces if the project keeps this work under the SAR
  example tree.
- `examples/SAR/src/...` for future converter, reader, writer, chunking, and
  validation implementations.
- `examples/SAR/test/...` for future GTest coverage and deterministic fixtures.
- `examples/SAR/tools/...` for future local-only validation helpers, including
  SarPy-based checks if needed.

Likely implementation units include a GOTCHA MAT reader, a CRSD writer,
product chunking utilities, a SarPy validation harness, and a C++
`graphx-gotcha-to-crsd` CLI. These names are discovery outputs, not files
created by this PR.

## Follow-Up Decisions

The following decisions block detailed implementation planning in later PRs:

- Whether HDF5 should be a required dependency for the converter or an optional
  gated dependency.
- Whether classic MAT support should use a third-party C++ library, a limited
  in-repository reader, or a deliberate unsupported-file diagnostic.
- The final namespace and target naming for GOTCHA-to-CRSD code under the current
  `examples/SAR` structure.
- The shape and size limits for CI-safe synthetic or reduced GOTCHA fixtures.
- CRSD validation and interchange scope boundaries for long-term maintenance.
