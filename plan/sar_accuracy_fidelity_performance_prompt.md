# Prompt: SAR Accuracy, Fidelity, Performance, and Accel-Token Audit

Use this prompt when you want a senior SAR/GPU/GraphX reviewer or coding agent to examine the current GraphX SAR example and related Metal enhancements, then propose the next implementation steps for improving algorithm accuracy, image fidelity, runtime performance, validation depth, and strict accel-token architecture compliance.

Prompt:

You are a senior SAR image-formation engineer, GPU performance engineer, and C++ GraphX architecture reviewer working inside the GraphX repository. Analyze the current SAR example under `examples/SAR`, the SAR planning documents under `plan/`, and the related GPU/Metal implementation under `libgpu`.

Do not assume the repository is at a previous revision. Inspect the actual code first.

Evidence discipline is mandatory:

- Tag every recommendation as one of:
  - Observed in code.
  - Inferred from code.
  - Repository search evidence.
  - External SAR best practice.
  - Speculative.
- Do not present speculative recommendations as established facts.
- Do not assume that files, nodes, contracts, or tests mentioned in this prompt still exist.
- Repository inspection overrides prompt assumptions.

Important current architectural context:

- The SAR example lives under `examples/SAR`.
- SAR PR3 uses resolver-driven generic node intents and accel-token edge contracts.
- Graph edges must carry `graph::gpu::accel` tokens/views/tickets plus lightweight SAR sidecar identity, not raw SAR payload-envelope messages across transfer/kernel boundaries.
- Historical `SarPulseBlockMessage`, `SarRangeTileMessage`, `SarImageTileMessage`, `SarDeviceLeaseMessage`, and `SarTransferTicketMessage` names are compatibility or adapter vocabulary only unless they wrap/reference accel tokens.
- `DeviceKernelNodeMetal` exists as the preferred general Metal boundary for one device input tile to one device output tile.
- `SarBackprojectionTransformNode` may delegate to `DeviceKernelNodeMetal` in native-device mode while preserving SAR sidecar identity.
- `H2DAsyncNode` and `D2HAsyncNode` should resolve to backend-specific variants through the provider/resolver path where available.

Primary objective:

Produce a concrete, reviewable next-step plan for improving:

1. SAR algorithm accuracy.
2. Image fidelity and physical realism.
3. GPU/CPU performance and graph overhead attribution.
4. External-data validation.
5. Display, inspection, and debug workflows.
6. Strict enforcement of the GPU accel-token model.

This is an analysis prompt first. Only propose code changes unless explicitly asked to implement them.

## Required Repository Inspection

Inspect at minimum:

- `examples/SAR/include/sar/SarMessages.hpp`
- `examples/SAR/include/sar/SyntheticApertureIqSourceNode.hpp`
- `examples/SAR/include/sar/RangeWindowNode.hpp`
- `examples/SAR/include/sar/RangeCompressionNode.hpp`
- `examples/SAR/include/sar/AzimuthTileSplitNode.hpp`
- `examples/SAR/include/sar/SarBackprojectionTransformNode.hpp`
- `examples/SAR/include/sar/ImageTileMergeNode.hpp`
- `examples/SAR/src/sar_benchmark.cpp`
- `examples/SAR/config/*.json`
- `examples/SAR/test/*.cpp`
- `libgpu/include/gpu/metal/nodes/*.hpp`
- `libgpu/include/gpu/metal/capabilities/*.hpp`
- `libgraph/src/graph/ResolvingNodeProvider.cpp`
- `libgraph/src/graph/GraphConfigParser.cpp`
- `plan/SAR.md`
- `plan/SAR_PR3_CHECKLIST.md`

If a named file does not exist, record it as missing, then continue through repository search rather than assuming the expected design exists.

Also inspect any new files discovered by `rg -n "SAR|Sar|DeviceKernelNode|accel-token|payload_contract|resolver|Metal"`.

## Analysis Questions

Answer these questions explicitly:

1. What parts of the SAR processing are currently synthetic, symbolic, or metadata-only?
2. Which stages are mathematically meaningful today, and which are placeholders?
3. What is the highest-impact accuracy/fidelity improvement?
4. What is the highest-impact performance improvement?
5. What is the highest-impact architecture cleanup for enforcing accel-token edges?
6. Where does the current graph still risk confusing transfer payload bytes with graph edge copies?
7. Which tests prove sidecar preservation, and what sidecar fields remain under-tested?
8. Which nodes can use `DeviceKernelNodeMetal` directly, and which need SAR-specific adapters?
9. What should remain in `examples/SAR`, and what, if anything, should be promoted to `libgpu`, `libdsp`, or `libgraph`?
10. What would make the SAR example credible to a SAR/domain reviewer rather than only a GraphX architecture reviewer?

## Mathematical Correctness Audit

Determine whether each stage currently performs physically meaningful SAR operations.

For every stage classify its current behavior as:

- Physically correct.
- Approximation.
- Placeholder.
- Symbolic.
- Unknown.

Audit at minimum:

1. Range windowing.
2. Matched-filter generation.
3. FFT implementation and scaling.
4. Range compression.
5. Geometry handling.
6. Coordinate frames.
7. Backprojection equations.
8. Interpolation.
9. Image accumulation.
10. Dynamic-range scaling.

For every equation or signal-processing transform identified in code, report:

- Assumptions.
- Units.
- Numerical precision.
- Missing parameters.
- Whether the implementation is testable with a tiny deterministic fixture.

Do not recommend replacing an algorithm until you identify which correctness gap it fixes and how the replacement will be tested.

## External Data Sources To Evaluate

Verify source details before making implementation commitments. For each source, report access path, license/use constraints, redistribution restrictions, data format, metadata completeness, expected preprocessing burden, suitability for CI fixtures, and suitability for local/benchmark validation.

For each external dataset, also report confidence and metadata status:

- Known.
- Requires download.
- Requires experiment.
- Unknown.

Do not assume metadata availability. List exact fields verified, fields assumed, and missing information.

Evaluate at least:

1. AFRL Gotcha Volumetric SAR Dataset
   - Official overview: https://www.sdms.afrl.af.mil/index.php?collection=gotcha
   - Challenge problem PDF: https://www.sdms.afrl.af.mil/content/challenge_areas/documents/A_challenge_problem_for_2D-3D_imaging_of_targets_from_a_volumetric_data_set_in_an_urban_environment.pdf
   - Use for: phase-history/backprojection validation, circular/volumetric SAR geometry, point-target/urban-scene image checks.

2. AFRL Wide Angle SAR / related SDMS challenge data
   - Official overview: https://www.sdms.afrl.af.mil/index.php?collection=wide_angle_sar
   - Use for: wider aspect-angle behavior, target recognition/image-quality stress tests.

3. AFRL MSTAR
   - Official source family: https://www.sdms.afrl.af.mil
   - Use for: image-domain target-recognition sanity checks, not primary phase-history image-formation validation unless phase history is available.

4. NASA/JPL UAVSAR
   - Mission/data portal: https://uavsar.jpl.nasa.gov/
   - Data search: https://uavsar.jpl.nasa.gov/cgi-bin/asar-data.pl
   - Use for: real-world SAR imagery/metadata validation, polarimetric products, geospatial display workflows.

5. Sentinel-1 through Alaska Satellite Facility
   - ASF Sentinel-1 information: https://asf.alaska.edu/data-sets/sar-data-sets/sentinel-1/
   - ASF search/data portal entry point: https://search.asf.alaska.edu/
   - Use for: image-product validation, geospatial metadata/display pipeline, not raw custom backprojection unless working from suitable SLC products.

6. Sandia SAR imagery archive
   - Public SAR imagery: https://www.sandia.gov/radar/pathfinder-radar-isr-and-synthetic-aperture-radar-sar-systems/archive-imagery-clone-2-clone-2/
   - Use for: visual reference examples, display target aesthetics, documentation/examples, not raw algorithm validation unless raw data is available.

7. OpenSARShip / Sentinel-1 ship interpretation datasets
   - Publication overview: https://research.monash.edu/en/publications/opensarship-a-dataset-dedicated-to-sentinel-1-ship-interpretation/
   - Use for: downstream detection/classification display validation, not primary image-formation correctness.

Classify each candidate as:

- Phase-history validation source.
- Image-product validation source.
- Metadata/display validation source.
- Classification/detection benchmark source.
- Documentation/reference-only source.

## Accuracy and Fidelity Improvements To Consider

Evaluate and rank:

1. Replace placeholder backprojection with physically meaningful backprojection.
2. Add geometry parameter blocks: platform position, scene center, sample frequency axis, carrier frequency, bandwidth, pulse timing, range origin, and units.
3. Add proper range compression using matched filtering and FFT-backed implementation.
4. Add windowing options and verify sidelobe behavior.
5. Add point-target synthetic scene generator with known ground truth.
6. Add phase-history fixture ingestion path, starting with a tiny deterministic subset.
7. Add image-quality metrics:
   - peak location error,
   - impulse response width,
   - peak sidelobe ratio,
   - integrated sidelobe ratio,
   - contrast,
   - entropy,
   - dynamic range,
   - focus/sharpness score,
   - phase consistency where applicable.
8. Add golden-reference tolerance tests for small scenes.
9. Add metadata/unit consistency checks.
10. Add motion compensation/calibration hooks, even if initially stubbed.

For each proposed improvement, state:

- Accuracy impact.
- Performance impact.
- Implementation complexity.
- Testability.
- Whether it belongs in PR3 or later.

## Reference Implementation Requirements

Every GPU kernel, Metal node, or GPU-backed SAR adapter must have:

- CPU scalar reference implementation.
- Deterministic output for a fixed fixture.
- Numerical tolerance definition.

Report parity against the CPU reference using:

- L-infinity error.
- RMS error.
- Relative error.
- Optional image-quality metric deltas when applicable.

GPU parity must be measured against the CPU reference, not against another GPU implementation. If a CPU reference does not exist, recommend creating it before optimizing the GPU path.

## Numerical Stability Audit

Evaluate numerical stability for each math-heavy stage:

- float vs double behavior.
- Accumulation precision.
- Interpolation precision.
- Dynamic range.
- Overflow risk.
- Underflow risk.
- Normalization.
- Window scaling.
- FFT scaling.

Classify each concern as:

- Safe.
- Needs review.
- High risk.

State the smallest test that would expose the risk.

## Performance Improvements To Consider

Evaluate and rank:

1. Native Metal backprojection kernel implementation through `DeviceKernelNodeMetal`.
2. Native Metal range window and/or range compression kernels.
3. FFT acceleration options and whether FFT belongs in `libdsp`, `libgpu`, or an adapter.
4. Buffer lease reuse/pooling and avoiding per-pulse/tile allocation churn.
5. Transfer/kernel overlap with explicit queues and `QueueSyncNodeMetal`.
6. Real fan-out/fan-in parallelism across CPU workers and GPU queues.
7. Device-side tile accumulation or partial reduction.
8. Host merge optimization while preserving deterministic EOS/watermark diagnostics.
9. Benchmark separation:
   - graph build time,
   - graph run time,
   - lifecycle teardown,
   - baseline non-graph execution,
   - plugin/provider lookup,
   - transfer payload bytes,
   - token-edge copy count.
10. Trace export that can be loaded by Chrome trace, Perfetto, or a simple local viewer.

For each proposed performance improvement, identify:

- Expected speedup or expected direction of improvement.
- Expected bottleneck addressed.
- Required measurements.
- Required tests.
- Memory impact.
- Graph overhead impact.
- Complexity increase.
- Risk of breaking accel-token contract.
- Whether it should be validated against non-graph baseline to estimate graph overhead.

Separate the following costs in the measurement plan:

- Algorithm cost.
- DSP cost.
- Transfer cost.
- Kernel cost.
- Graph scheduling/routing cost.
- Diagnostics cost.

Do not recommend an optimization unless it identifies the bottleneck it addresses and the measurement that will prove it helped.

## Display and Inspection Mechanisms

Recommend display mechanisms suitable for this repo. Include at least:

1. CI-safe static outputs:
   - PGM/PPM grayscale magnitude image.
   - CSV or JSON image/tile statistics.
   - Tiny golden image artifacts generated during tests but not necessarily committed.

2. Local developer tools:
   - PNG writer for magnitude/log-magnitude images.
   - HTML report with image, histogram, tile grid, metadata summary, and benchmark table.
   - Optional Python or C++ tool to compare two images and emit error heatmap.

3. Graph/trace visualization:
   - JSON trace of token lifecycle.
   - DAG topology summary.
   - Per-node timing table.
   - Queue/overlap timeline.

4. Domain-specific SAR displays:
   - linear magnitude,
   - log magnitude/dB,
   - phase image,
   - impulse response cross-sections,
   - range/azimuth slices,
   - tile boundary overlay,
   - peak marker and ground-truth marker overlay.

For each display mechanism, state whether it should be:

- CI artifact.
- Local-only tool.
- Documentation example.
- Future interactive viewer.

## Validation Mechanisms

Recommend validation at six levels:

1. Tier 1 unit tests:
   - token/view validity,
   - sidecar preservation,
   - kernel descriptor parsing,
   - geometry parameter validation,
   - range compression known-vector tests.

2. Tier 2 algorithm tests:
   - point target at known location,
   - multiple point targets,
   - known impulse response behavior,
   - matched-filter expected output,
   - backprojection parity with CPU reference.

3. Tier 3 graph integration tests:
   - source -> range stage -> split/fanout -> H2D -> kernel -> D2H -> merge -> diagnostics,
   - EOS/watermark behavior,
   - missing/duplicate/out-of-order tile handling,
   - graph vs non-graph deterministic parity.

4. Tier 4 CPU vs GPU parity tests:
   - CPU scalar reference vs Metal implementation,
   - fixed fixture and fixed tolerances,
   - L-infinity/RMS/relative error reporting,
   - sidecar identity parity across resolver substitution.

5. Tier 5 external dataset validation:
   - tiny fixture smoke test,
   - local medium fixture,
   - optional external dataset benchmark,
   - metadata hash check,
   - image metric tolerance check,
   - visual artifact generation.

6. Tier 6 performance benchmarks:
   - graph vs non-graph baseline,
   - transfer/kernel/graph/diagnostics attribution,
   - token lifecycle trace,
   - local GPU benchmark when hardware is available,
   - CI-safe deterministic proxy when hardware is unavailable.

State which tests should run in CI and which require local data or hardware.

## Determinism Requirements

All validation fixtures must be reproducible.

All tests must specify:

- Seed.
- Numerical tolerances.
- Expected image metrics.
- Golden metadata hashes.

Graph scheduling variations must not affect image correctness. Benchmark tests and correctness tests must be separated.

## Architecture Guardrails

Protect GraphX architecture while improving SAR fidelity:

1. Do not propose SAR-specific abstractions inside `libgraph`.
2. Do not bypass GraphX DAG semantics to make the SAR algorithm easier.
3. Do not recommend moving example-specific concepts into framework libraries unless at least two independent use cases exist.
4. Do not allow SAR message types to leak into GPU abstractions.
5. Do not allow dataset formats to influence GraphX internal contracts.
6. GraphX architecture takes precedence over SAR convenience.
7. Algorithm improvements belong in `examples/SAR`, `libdsp`, or a narrow adapter unless repository evidence proves they are general infrastructure.

## Mandatory Accel-Token Enforcement Audit

This is non-negotiable. The plan must enforce the GPU accel-token model strictly.

Audit and report:

1. All edges in PR3/native/resolved SAR topologies must use `edge_contract: "accel-token"` when crossing transfer/kernel stages.
2. No PR3 transfer/kernel edge may declare or imply raw `SarRangeTileMessage`, `SarImageTileMessage`, `SarTransferTicketMessage`, or other SAR payload-envelope contracts unless a named compatibility adapter is explicitly declared.
3. H2D/D2H/kernel stages must pass `HostPinnedBufferView`, `DeviceBufferView`, `BufferLease`, `TransferTicket`, and `KernelTicket` semantics through the established `graph::gpu::accel` types.
4. SAR identity must travel as sidecar metadata and must be validated:
   - `sequence_id`,
   - `batch_id`,
   - `aperture_id`,
   - `pulse_range_start`,
   - `pulse_range_count`,
   - `stream_id`,
   - `tile_id`,
   - `tile_count`,
   - EOS/watermark marker,
   - backend/device/queue ids.
5. Benchmarks must report transfer payload bytes separately from graph edge copy overhead.
6. Trace output must include token/lease/ticket identifiers and resolved node diagnostics.
7. Parser/schema validation must reject legacy SAR payload contracts under `edge_contract: "accel-token"`.
8. Resolver diagnostics must report:
   - intent type,
   - concrete type,
   - selected backend,
   - fallback reason,
   - input token type,
   - output token type.
9. Tests must prove that sidecars survive resolver substitution and backend-specific node selection.
10. Any proposed display, data-ingestion, or algorithm improvement must preserve this token model.

If the code violates any of these constraints, classify the issue:

- Blocker.
- PR3 required fix.
- Follow-up.
- Documentation mismatch.

## Required Output Format

Produce the following sections:

1. Executive recommendation
   - Best next PR.
   - Why it is highest leverage.
   - What not to do next.
   - Evidence tag for each recommendation.

2. Current-state findings
   - Accuracy/fidelity findings.
   - Performance findings.
   - Architecture/token-contract findings.
   - Test/display/validation findings.
   - Classification of each finding as observed, inferred, search-backed, external best practice, or speculative.

3. Mathematical correctness audit
   - Stage-by-stage correctness classification.
   - Equation/transform assumptions, units, precision, and missing parameters.
   - Highest-risk algorithm gaps.

4. External validation data matrix
   - Source.
   - Access URL.
   - Type of data.
   - License/use notes.
   - Confidence classification.
   - Verified fields.
   - Assumed fields.
   - Missing fields.
   - GraphX suitability.
   - CI suitability.
   - Local benchmark suitability.
   - First experiment to run.

5. Reference parity and numerical stability plan
   - CPU scalar references required.
   - GPU parity tolerances.
   - L-infinity/RMS/relative error reporting.
   - Numerical stability risks and smallest exposing tests.

6. Recommended implementation roadmap
   - PR-sized phases.
   - File-level changes.
   - Tests for each phase.
   - Risks and exit criteria.

7. Display and artifact plan
   - CI artifacts.
   - Local developer artifacts.
   - Future interactive tools.

8. Accel-token enforcement checklist
   - Existing tests.
   - Missing tests.
   - Parser/schema gaps.
   - Resolver gaps.
   - Benchmark/trace gaps.

9. Measurement and performance-attribution plan
   - Accuracy metrics.
   - Fidelity metrics.
   - GPU/DSP performance metrics.
   - Graph overhead metrics.
   - Algorithm/DSP/transfer/kernel/graph/diagnostics cost separation.
   - Baseline comparison method.
   - Required bottleneck evidence before each optimization.

10. Deterministic validation and CI tier plan
   - Tier 1 through Tier 6 tests.
   - CI vs local-only split.
   - Seeds, tolerances, image metrics, and metadata hashes.

11. Concrete next PR proposal
   - Title.
   - Scope.
   - Files to touch.
   - Tests to add.
   - Acceptance criteria.

## Preferred Next-PR Bias

Unless repository inspection strongly contradicts this, prefer a next PR that:

1. Adds a deterministic point-target SAR validation harness.
2. Adds CPU reference backprojection for a tiny scene.
3. Defines CPU-vs-GPU parity tolerances before expanding native Metal kernels.
4. Adds a Metal `DeviceKernelNodeMetal` backprojection parity path or adapter test if not already present.
5. Emits a simple image artifact and image-quality metrics.
6. Adds strict accel-token topology/schema tests.
7. Measures graph overhead separately from SAR algorithm, DSP, transfer, kernel, and diagnostics costs.
8. Does not require committing large external datasets.

Large dataset ingestion should be proposed as the following PR unless a tiny legal fixture already exists.
