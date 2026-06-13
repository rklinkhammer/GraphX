
Act as PLANNER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Goal:
Implement a GraphX command-line utility that converts ordered GOTCHA MATLAB `.mat` phase-history files into ordered CRSD received-signal files, and define a SarPy-based Python reference workflow for generating reference imagery for later GraphX comparison.

Use these reports as context:
- `plan/reviews/SAR_INSPECTOR_REPORT.md`
- `plan/reviews/SAR_PLANNER_REPORT.md`
- `plan/reviews/SAR_SIMPLIFIER_REPORT.md`
- `plan/reviews/SAR_PLANNER_REPORT.md`


Important constraint:
Do not use MATLAB directly. MATLAB must not be a build-time or runtime dependency. GOTCHA `.mat` files must be read from C++ using open file-format support:
- MATLAB v7.3 `.mat` files through HDF5.
- Classic/non-HDF5 `.mat` files through a C++ MAT-file reader library, a clearly documented supported subset, or a clear unsupported-format error.
Python tools may use `scipy.io.loadmat` and `h5py` for validation/reference workflows only.

Background:
CRSD means Compensated Received Signal Data. It is an NGA SAR standard for received signal data, below CPHD and SICD in the SAR product chain:

    CRSD -> CPHD -> SICD -> formed image / derived products

CRSD represents received complex signal vectors plus metadata such as collection ID, global timing, channel definitions, per-vector parameters, reference geometry, support arrays, antenna/receive-channel information, and signal arrays.

SarPy provides CRSD reader/writer support under `sarpy.io.received`, including CRSD 1.0 elements and CRSD reader APIs. SarPy documentation says CRSD data can be opened with the received-signal reader and sliced as complex data; it also exposes CRSD version, data size, PVP arrays, and support arrays.

Primary references:
- SarPy docs: https://sarpy.readthedocs.io/
- SarPy CRSD docs: `sarpy.io.received`, `sarpy.io.received.crsd`, `sarpy.io.received.crsd1_elements`
- SarPy GitHub states it supports reading/writing SICD, SIDD, CPHD, and CRSD.

Important caveat:
GOTCHA is not raw AESA element data. It is closer to compensated phase-history data. Therefore the first implementation may produce a single-channel or pseudo-received CRSD product. Document all assumptions clearly.

Implementation guardrails:
Before implementing, inspect the existing GraphX repository structure, build system, dependency policy, CLI conventions, test framework, formatting rules, and any existing SAR or complex-signal abstractions. Reuse existing project patterns.

Do not introduce large third-party dependencies without documenting:
- Why they are needed
- Whether they are already available in the project/toolchain
- How they integrate with the existing build
- What alternatives were considered

Treat valid CRSD writing as the final target, not as a guaranteed Phase 1 deliverable. First determine whether the repository already has a viable CRSD-writing path. If not, implement `graphx-crsd-lite` first and isolate the full CRSD writer behind the same `ISarWriter` interface.

Do not fake standards compliance:
- In `--mode crsd`, generated files must either pass SarPy open/read validation or the command must fail clearly with an actionable error.
- In `--mode graphx-crsd-lite`, outputs must be explicitly labeled as non-standard intermediate GraphX products.

Required repo-discovery report before code changes:
- Existing build system and where the new target will be added
- Existing CLI framework, if any
- Existing test framework and test-data conventions
- Available HDF5/MAT dependencies or dependency gaps
- Existing SAR/complex-data types, if any
- Proposed files to create or modify

Deliverables:

1. CRSD definition document

Create:

    docs/sar/crsd_definition.md

Include:
- What CRSD is
- Where it sits relative to CPHD and SICD
- Required conceptual sections:
  - Collection metadata
  - Global timing
  - Channel metadata
  - Per-vector parameters, PVP
  - Support arrays
  - Signal arrays
  - Reference geometry
  - Antenna / receive-channel abstractions
- Mapping from GOTCHA `.mat` fields to GraphX internal fields
- Mapping from GraphX internal fields to CRSD fields
- Known limitations of GOTCHA -> CRSD conversion
- `gotcha_crsd_index.json` schema
- `conversion_report.json` schema
- Explicit statement that MATLAB itself is not used

2. C++ data model

Implement C++ classes for a GraphX SAR utility layer.

Suggested namespace:

    graphx::sar

Core classes:

    ComplexSample
    PulseVector
    ChannelSignal
    WaveformMetadata
    PlatformState
    PerVectorParameters
    CollectionMetadata
    ReferenceGeometry
    CrsdProduct
    GotchaMatProduct

Reader/writer interfaces:

    class ISarReader {
    public:
        virtual ~ISarReader() = default;
        virtual SarProduct read(const std::filesystem::path& path) = 0;
    };

    class ISarWriter {
    public:
        virtual ~ISarWriter() = default;
        virtual void write(const SarProduct& product,
                           const std::filesystem::path& outputPath) = 0;
    };

Concrete classes:

    GotchaMatReader
    CrsdWriter
    CrsdReader
    SarProductChunker
    SarProductValidator

3. GOTCHA `.mat` reader

Implement:

    GotchaMatReader

Requirements:
- Accept a directory containing ordered `.mat` files.
- Discover files deterministically by lexical sort unless the user provides a manifest.
- Read MATLAB files without requiring MATLAB.
- Use a C++ MAT/HDF5 strategy:
  - Support MATLAB v7.3 through HDF5.
  - For non-HDF5 `.mat`, either use an available C++ MAT-file reader library, implement a documented supported subset, or produce a clear unsupported-format error.
- Extract:
  - complex phase-history samples
  - frequency/sample axis if available
  - pulse/platform position
  - pulse time or synthetic pulse index
  - polarization if available
  - pass/collection identifiers if available
- Preserve original field names in diagnostic metadata.
- Preserve source file order and per-file provenance.

4. Internal normalized model

Do not write CRSD directly from GOTCHA structures.

Normalize into:

    signal[pulse][channel][sample]

For initial GOTCHA support:
- channel count may be 1
- each `.mat` file may contribute one or more pulse vectors
- channel metadata should explicitly say `derived_from_gotcha_phase_history`

Required internal output fields:
- collection_id
- source_dataset
- source_file_list
- pulse_count
- channel_count
- samples_per_pulse
- sample_type
- center_frequency
- bandwidth
- frequency_vector
- platform_position[pulse]
- platform_velocity[pulse], if available
- pulse_time[pulse], if available
- polarization, if available

5. CRSD writer

Implement:

    CrsdWriter

Requirements:
- Produce ordered CRSD files from the normalized internal model.
- User defines maximum output size.
- Split output into chunks while preserving pulse order.
- Never split a pulse vector across chunks.
- Name outputs deterministically.

Example naming:

    gotcha_crsd_000000.crsd
    gotcha_crsd_000001.crsd
    gotcha_crsd_000002.crsd

Each CRSD output file must include:
- valid metadata
- signal array
- per-vector parameters
- source provenance metadata
- chunk index
- pulse start/end index using clearly documented inclusive/exclusive semantics
- enough information for SarPy to read the file

If fully standard-compliant CRSD writing is too large for the first implementation, create two writer modes:

    --mode graphx-crsd-lite
    --mode crsd

`graphx-crsd-lite` may be an intermediate binary+JSON product, but the final target remains valid CRSD.

6. Command-line utility

Create executable:

    graphx-gotcha-to-crsd

Required arguments:

    --input-dir <directory>
    --output-dir <directory>
    --collection-id <string>
    --max-output-size-mb <integer>
    --sort lexical|manifest
    --manifest <file optional>
    --mode graphx-crsd-lite|crsd
    --validate
    --emit-index

Example:

    graphx-gotcha-to-crsd \
      --input-dir /data/gotcha/large_scene \
      --output-dir /data/graphx/crsd \
      --collection-id GOTCHA_LARGE_SCENE_PASS_1 \
      --max-output-size-mb 512 \
      --sort lexical \
      --mode crsd \
      --validate \
      --emit-index

Output:

    output/
      gotcha_crsd_000000.crsd
      gotcha_crsd_000001.crsd
      gotcha_crsd_index.json
      conversion_report.json
      conversion_warnings.log

CLI requirements:
- `graphx-gotcha-to-crsd --help` documents every option and mode.
- Invalid input directories, empty file sets, malformed manifests, unsupported MAT versions, inconsistent sample shapes, and missing required metadata produce deterministic non-zero exits with clear messages.

7. Index file

Generate:

    gotcha_crsd_index.json

Include:
- collection_id
- source directory
- ordered input file list
- output CRSD file list
- pulse ranges per output
- sample shape
- channel count
- frequency metadata
- checksum per output file
- conversion assumptions
- warnings

8. Conversion report

Generate:

    conversion_report.json

Include:
- tool version, if available
- command-line arguments
- timestamp
- input file count
- output file count
- total pulse count
- total sample count
- selected mode
- MAT reader strategy used
- assumptions
- warnings
- validation status

9. Validation

Implement validation in two layers.

C++ validation:
- file existence
- deterministic ordering
- shape consistency
- complex sample sanity
- NaN/Inf checks
- pulse count consistency
- metadata completeness
- chunk boundary correctness

Python/SarPy validation:

Create:

    tools/sarpy/validate_crsd.py

It should:
- open each generated CRSD file using SarPy received-signal APIs
- print CRSD version
- print data size
- read a small signal slice
- read PVP array if available
- verify complex dtype
- verify pulse/sample dimensions
- emit JSON validation report

SarPy reference note:
SarPy documentation exposes CRSD under `sarpy.io.received`; CRSD readers expose CRSD version, data size, slicing access to complex received-signal data, and PVP accessors.

10. Python reference image workflow

Define a Python workflow that will later be used to compare GraphX image formation against a known Python reference.

Create:

    tools/sarpy/reference_image_from_gotcha.py
    tools/sarpy/reference_image_from_crsd.py

`reference_image_from_gotcha.py`:
- Reads GOTCHA `.mat` files using Python for reference/validation only.
- Use `scipy.io.loadmat` for classic MAT files.
- Use `h5py` for MATLAB v7.3/HDF5 files.
- Extracts phase-history and platform metadata.
- Forms a reference image using a simple, documented backprojection implementation.
- Writes:
  - reference_image.npy
  - reference_magnitude.png
  - reference_metadata.json

`reference_image_from_crsd.py`:
- Opens generated CRSD using SarPy.
- Reads signal vectors.
- Reads PVP metadata.
- Forms the same reference image where metadata is sufficient.
- Writes:
  - crsd_reference_image.npy
  - crsd_reference_magnitude.png
  - crsd_reference_metadata.json

Comparison script:

    tools/sarpy/compare_images.py

Inputs:

    --graphx-image graphx_image.npy
    --reference-image reference_image.npy

Metrics:
- normalized RMSE
- peak magnitude difference
- phase error where magnitude is above threshold
- image correlation
- optional SSIM on magnitude image

Outputs:
- comparison_report.json
- difference_magnitude.png
- phase_difference.png

11. Python package requirements

Create:

    tools/sarpy/requirements.txt

Include:

    numpy
    scipy
    h5py
    matplotlib
    sarpy

12. Tests

Create unit/integration tests.

C++:
- test lexical ordering
- test manifest ordering
- test chunk splitting
- test metadata propagation
- test invalid `.mat` handling
- test unsupported non-HDF5 MAT handling when no classic MAT reader is available
- test graphx-crsd-lite round trip
- test CRSD writer smoke test, if available

Python:
- test MAT reader field discovery
- test reference image generation on a tiny fixture
- test SarPy CRSD open/read on generated output
- test image comparison metrics

Test-data requirement:
Use at least one tiny synthetic MAT fixture or generated fixture so CI does not require proprietary GOTCHA data.

13. Implementation strategy

Use phased delivery.

Phase 0:
- Documentation only.
- CRSD definition.
- GOTCHA-to-CRSD mapping.
- Identify required GOTCHA fields.
- Repo-discovery report.

Phase 1:
- C++ GOTCHA inspection utility or mode.
- Print `.mat` keys, shapes, dtypes.
- Produce field inventory JSON.
- No MATLAB dependency.

Phase 2:
- C++ internal model.
- C++ GOTCHA reader skeleton.
- `graphx-crsd-lite` writer.

Phase 3:
- Chunking and CLI.
- Deterministic ordered conversion.
- Index and reports.

Phase 4:
- SarPy validation scripts.
- Python reference backprojection.
- Image comparison workflow.

Phase 5:
- Full CRSD writer.
- SarPy open/read validation must pass.

Phase 6:
- GraphX image formation comparison against SarPy/Python reference.

Acceptance criteria:
- Utility accepts a directory of ordered GOTCHA `.mat` files.
- It reads MATLAB `.mat` files from C++ without using MATLAB.
- It produces ordered output chunks of user-defined maximum size.
- Output includes index and conversion report.
- Conversion is deterministic.
- All assumptions are documented.
- `graphx-crsd-lite` mode is clearly labeled as non-standard.
- SarPy can open and inspect generated CRSD files in full CRSD mode.
- Reference image generation works from original GOTCHA data.
- Reference image generation works from generated CRSD data when metadata is sufficient.
- GraphX output can be compared numerically against the reference image.
- Tests cover deterministic ordering, chunking, metadata propagation, invalid inputs, and at least one tiny fixture path.

Important design rule:
Do not let GOTCHA-specific field names leak throughout GraphX. GOTCHA is an importer. CRSD is an exporter. The internal GraphX SAR model should be independent:

    GOTCHA .mat
        -> GotchaMatReader
        -> GraphX normalized Pulse/Channel/Sample model
        -> CrsdWriter
        -> SarPy validation
        -> reference image comparison

Stop after the planner report.
Save the report as `plan/reviews/SAR_PLANNER_REPORT.md`.