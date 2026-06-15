# SAR CRSD To Focused Image Planner Report

Role: PLANNER (per plan/agents/GRAPHX_SAR_AGENT_ROLES.md)

Scope: PR plan only. No implementation.

## 1. Executive Summary

This plan defines a small, reviewable path to add CRSD input to the GraphX SAR pipeline and produce a fully focused SAR image that can be validated against reference outputs generated from the same ordered CRSD input set with local-only SarPy/reference tooling.

Key decisions:
- First CRSD ingestion path should be a dedicated SAR source node with JSON-configured CRSD file paths or a CRSD directory plus ordering policy, matching existing SAR source-node conventions.
- The real generated `data/crsd` layout represents one logical GOTCHA scene/image split across 10 per-file CRSD products (`subData01` through `subData10`), not 10 independent final images.
- SarAccelControlToken is the required edge contract for all SAR-related graph edges so GPU backfill (H2D/kernel/D2H execution metadata and transport semantics) remains intact end-to-end.
- No GraphX runtime dependency on SarPy. SarPy remains local-only/gated for validation and reference generation.
- CI uses tiny deterministic CRSD fixtures and deterministic acceptance thresholds.
- Real GOTCHA-derived CRSD validation remains local-only and explicitly gated.
- The local GOTCHA-derived lane must assemble the ordered CRSD aperture set into one focused SAR image product path (not CRSD signal magnitude quick-look and not one image per CRSD segment).
- Tests must prove that CRSD signal/PVP content enters the graph, affects the focused image, and is not replaced by synthetic placeholder processing.
- Focused-image correctness tests are failure-oriented: they must fail if execution is diagnostics-only or if CRSD samples/PVP are ignored.
- Metal support must be demonstrated with explicit transfer/kernel execution evidence when the Metal lane is selected.
- Parallelizable focused-image computation should be planned with explicit split/merge nodes so branch-level work can execute concurrently while preserving deterministic merge semantics.

Planned delivery is split into 10 independent PRs, each with one primary concern, compile/test integrity, and explicit rollback boundaries.

## 2. Current-State Findings

Observed:
- Existing SAR runtime topology is JSON + plugin-driven through NamedSourceNode/NamedInteriorNode/NamedSink patterns and plugin facades in examples/SAR/plugins.
- Existing source nodes emit SarAccelControlToken (SyntheticApertureIqSourceNode, GotchaReplaySourceNode).
- Existing SarBackprojectionTransformAccelNode currently operates as a transport/control-token transform with synthetic/native kernel ticket flow; it does not consume CRSD metadata/PVP directly.
- Existing SarMaterializedImageSinkNode currently materializes deterministic reference vectors keyed by token identity metadata, not a focused image reconstructed from CRSD signal content.
- Existing tools/sarpy scripts already provide:
  - CRSD environment probe and validation summary (tools/sarpy/validate_crsd.py)
  - CRSD sample block magnitude export (tools/sarpy/reference_image_from_crsd.py)
  - image comparison metrics and artifacts (tools/sarpy/compare_images.py)
- Existing GOTCHA-to-CRSD path writes CRSD via C++ writer interface plus SarPy writer adapter (CrsdIO + write_crsd_from_graphx_product.py) and emits metadata/pvp/provenance/chunk artifacts.
- Generated real-data CRSD output under `data/crsd` is one logical scene split across per-file CRSD output directories:
  - each `subDataNN.crsd_output/gotcha_crsd_chunk_0000.crsd/product.crsd` is a CRSD phase-history segment for the same final image,
  - each segment has sidecar JSON (`metadata.json`, `pvp.json`, `chunk_index.json`, `provenance.json`) and SarPy validation JSON,
  - the sidecar JSON files are supporting evidence/preflight/provenance, while `product.crsd` is the authoritative signal/PVP/metadata container for focused image formation.
- Existing SAR tests and CTest lanes already separate CI-safe lanes from local-only/gated SarPy lanes.

Inferred:
- A CRSD-specific adapter/model layer is needed before current downstream SAR token path can represent true CRSD-derived phase history semantics.
- Existing backprojection/reference utilities in examples/SAR are suitable as the initial deterministic focused-image algorithm baseline, provided inputs are adapted from CRSD consistently.

Unknowns requiring explicit PR investigation/closure:
- Exact minimum CRSD metadata/PVP subset required for first deterministic focused-image path across tiny fixture and real GOTCHA-derived CRSD.
- Whether first focused image should be single-channel/single-pol only (recommended for initial scope) or multi-channel from day one.
- Whether SarPy can produce a true focused image directly from CRSD in the local environment. If not, the reference lane must use an explicitly documented local reference image-formation implementation rather than a CRSD signal quick-look.
- Whether all 10 generated CRSD segments have sufficient pulse/PVP continuity to form one deterministic full-aperture image without additional external metadata.

## 3. CSV Input Node Pattern Findings

Observed CSV/source-node patterns in GraphX:
- Graph-level CSV injection exists in libgraph via GraphExecutorBuilder::WithCSVInput(s), CSVInjectionPolicy, and CSVDataInjectionManager.
- CSV support is policy/capability-driven data injection into IDataInjectionSource-capable nodes, not the dominant pattern used by SAR example source nodes.
- SAR source nodes in examples/SAR are node-local and JSON-configurable (fixture_path, stream sizing, backend metadata, etc.), and are registered as standard plugins.

Planning conclusion:
- For first CRSD ingestion, use a dedicated SAR source node with JSON-configured `crsd_paths`, a `crsd_directory`, or a manifest path plus focused-image-relevant parameters.
- Do not copy CSV injection policy architecture into SAR CRSD ingestion v1.
- Keep future optional generalization open (if cross-domain file-injection standardization becomes a separate effort), but not in this scope.

## 4. Target Architecture

Target data path for CRSD-focused image formation:

Canonical focused-image lane:
- OrderedCrsdSetInputSourceNode -> CrsdApertureAssemblyAdapterNode -> SarSplitNode -> FocusedImageTransformNode -> SarMergeNode -> FocusedImageOutputSinkNode

Generated-data target:
- `data/crsd/subData01.crsd_output/.../product.crsd` through `subData10.crsd_output/.../product.crsd` form one ordered aperture set for one focused image.
- The first local real-data workflow must produce one focused image artifact set from all selected CRSD segments.

1. OrderedCrsdSetInputSourceNode
- Input: ordered CRSD file paths, CRSD directory plus lexical ordering, or manifest plus optional channel/polarization selectors and deterministic limits.
- Output: CRSD-derived aperture-segment messages/tokens carrying required metadata and per-vector signal context.
- Required proof: emitted payload hashes, per-segment vector counts, total vector count, sample counts, selected PVP fields, and selected geometry fields must match the CRSD fixture/set.
- `product.crsd` is authoritative. Sidecar JSON may be used only for fast preflight, provenance, and expected-count sanity checks.

2. CrsdApertureAssemblyAdapterNode
- Converts the ordered CRSD segment set to the SAR phase-history model expected by focused image transform.
- Ensures explicit geometry/sampling assumptions, deterministic segment ordering, pulse/vector continuity checks, and full-aperture accounting.

3. SarSplitNode (parallel fan-out)
- Splits SarAccelControlToken phase-history flow into deterministic parallel branches (for example tile, aperture, or pulse-block partitioning).
- Guarantees branch partition metadata and ordering contract required by merge.

4. FocusedImageTransformNode (initial CPU-deterministic path)
- Performs actual SAR focusing/backprojection from the assembled full-aperture phase history.
- Produces focused image grid + metadata contract.
- Keeps current accel-token contracts intact elsewhere; no runtime redesign.
- Required proof: changing CRSD signal samples or relevant PVP/geometry fields must change the focused image output.

5. SarMergeNode (parallel fan-in)
- Merges branch outputs from SarSplitNode/FocusedImageTransformNode branches.
- Enforces deterministic completion criteria (expected branch count, EOS handling, stable output ordering).

6. FocusedImageOutputSinkNode
- Emits one deterministic focused-image artifact set for the ordered CRSD aperture set:
  - focused_image.bin (float32 row-major)
  - focused_image.json (shape, spacing, geometry assumptions, hashes)
  - convenience visualization: focused_image.pgm or focused_image.png

7. Local-only SarPy reference lane
- Uses the same ordered CRSD input set to generate reference artifact(s) with local-only scripts.
- Compares GraphX focused image vs reference and emits:
  - comparison_report.json
  - difference_magnitude.png
  - phase_difference.png

Boundary rules:
- GraphX runtime remains pure C++ (no SarPy dependency).
- SarPy scripts stay in local/gated tooling and tests.
- CI default remains SarPy-free; optional/gated lanes cover local reference comparison.
- All SAR-related graph edges must carry SarAccelControlToken to preserve GPU backfill and resolver/transport compatibility.
- CRSD signal magnitude quick-look images are not acceptable substitutes for focused SAR images.
- Diagnostic-only graph execution is not acceptable; focused-image tests must verify data-dependent output artifacts.
- The real-data lane produces one focused image from the ordered CRSD set. Producing one unrelated image per CRSD segment is not acceptable unless explicitly marked as a segment-level diagnostic mode.
- Generated sidecar JSON files are not authoritative signal/PVP sources. They are optional validation/reporting aids.

Generated JSON sidecar classification:
- `metadata.json`: optional preflight/report comparison for dimensions, frequency bounds, channel id, source files.
- `pvp.json`: optional field-list/byte-size sanity check only; it does not contain PVP arrays.
- `chunk_index.json`: optional pulse-range and sample-count sanity check per CRSD segment.
- `provenance.json`: optional lineage/reporting input.
- `sarpy_validation/*.json`: optional local validation evidence only.
- `_preprocessed_hdf5_mat/*`: conversion staging only; not focused-image input.

Focused-image processing proof matrix (required):
- all-zero CRSD signal fixture -> near-zero focused image response
- known coherent tiny fixture -> deterministic expected peak location/value envelope
- one-sample CRSD perturbation -> focused image hash/metric delta from baseline
- relevant PVP/geometry perturbation -> focused image shift/delta from baseline
- dropping, duplicating, or reordering a CRSD segment -> deterministic failure or changed focused-image artifact according to configured policy
- repeated identical input runs -> deterministic identical output hashes and peak location
- These tests must fail if the pipeline only forwards tokens/timing metadata and does not numerically process CRSD payloads.

## 5. Required Type/Model Changes

Required new/extended models for planned PRs:

1. CRSD ingest model (examples/SAR/include/sar/io)
- CrsdReadOptions
- CrsdReadResult
- CrsdChannelSelection
- CrsdVectorRecord (signal samples + minimal PVP fields)
- CrsdMetadataSummary (collection/channel timing/frequency/geometry subset)
- CrsdSegmentRecord (segment path, segment index, vector range, sample count, signal checksum, optional sidecar summaries)
- OrderedCrsdSetReadResult (ordered segment records, total vector count, common channel/sample/frequency metadata, aperture continuity diagnostics)

2. SAR phase-history transfer model (examples/SAR/include/sar)
- SarPhaseHistoryFrame or equivalent message contract for focused-image transform input.
- Deterministic ordering fields (vector index, pulse index, channel id, sample count).
- Explicit frame contract requirements:
  - Ownership: frame payload must declare ownership mode (owned host buffer, shared immutable buffer view, or device-backed buffer view with lifetime ticket).
  - Buffer layout: frame payload must declare TensorLayout-like shape/stride and complex sample representation (complex_f32 interleaved or explicitly documented equivalent).
  - Integrity: frame payload must carry deterministic checksum/hash over the signal payload for boundary verification.
  - Control semantics: EOS/watermark/control frames must be explicit frame markers and must not carry ambiguous synthetic sample payloads.
  - Physics boundary: CRSD physics fields (samples, timing, PVP, geometry) must be read from frame payload/metadata contract, not inferred from SarSidecar.
- Explicit payload ownership/transport rules:
  - CRSD complex samples must be carried in a typed phase-history frame or in an explicitly described host/device buffer view with TensorLayout.
  - SarSidecar fields remain routing, identity, progress, and diagnostics fields; they must not be used as a substitute for CRSD physics.
  - SarAccelControlToken remains the graph-edge envelope for all SAR nodes; typed phase-history payload is attached to or referenced by the token contract, not sent through a separate non-token edge type.
  - H2D/D2H nodes move sample/image buffers only; algorithm decisions must come from the typed phase-history metadata and payload.
  - Tests must verify payload checksums before and after adapter/split/merge/transport boundaries.

3. Parallel execution model (examples/SAR/include/sar)
- SarSplitPartitionMetadata (partition id/count, pulse/vector range, deterministic ordering key)
- SarMergeCompletionPolicy (expected_partitions, EOS policy, stable reduction order)
- Deterministic merge contract proving identical output across repeated parallel runs

4. Focused image product model (examples/SAR/include/sar)
- FocusedImageGrid metadata (width, height, pixel spacing, coordinate frame assumptions, dtype/layout).
- FocusedImageArtifactContract for sink/comparison lanes.

5. Validation thresholds/config model
- Threshold config for RMSE, phase RMSE, peak error, correlation, optional SSIM.
- CI-safe deterministic defaults and local override support.

Minimum CRSD metadata/PVP fields for first focused-image path:
- Signal array (complex samples) per vector
- Vector timing (rcv time or equivalent)
- Platform position and velocity vectors (or equivalent geometry terms)
- Frequency metadata (reference/carrier + receive band bounds)
- Sufficient indexing to preserve deterministic vector order
- Segment identity and vector-range accounting across the ordered CRSD set
- Consistent sample count, channel id, signal array format, and frequency metadata across all segments unless an explicit documented mode supports variation

Initial algorithm definition for fully focused image:
- Deterministic backprojection-based image formation from the ordered CRSD aperture set into one 2D image grid.
- Explicit documented geometry assumptions (single channel/pol for v1, fixed frame assumptions, row-major output).
- The transform must consume CRSD-derived complex samples and PVP/geometry fields; synthetic fallback output is forbidden in the focused-image path.
- Deterministic tolerances for validation:
  - magnitude RMSE
  - phase RMSE
  - peak magnitude error and peak location error
  - magnitude correlation
  - optional SSIM

Metal execution requirements:
- CPU focused-image output is the correctness baseline.
- Metal execution is a separate, explicit lane using the same CRSD-derived phase-history contract.
- A tiny multi-segment CRSD fixture lane must run both CPU and Metal focused-image paths from the same ordered input fixture and compare outputs.
- Metal tests must verify resolver selection, bytes_h2d > 0, bytes_d2h > 0, kernel_dispatches > 0, and CPU-vs-Metal output parity within documented tolerances.
- Dual-lane evidence must include input checksum, CPU output hash, Metal output hash, and parity metrics (at minimum RMSE and peak location/value deltas).
- A passing Metal lane must prove actual kernel execution, not just token forwarding.

## 6. Planned PRs

## PR1

Title:
Repository discovery for CRSD source-node and focused-image output patterns

Purpose:
Freeze repository-grounded implementation map before code changes; document exact SAR node/plugin/config/test conventions and CRSD/SarPy boundary constraints.

Files to touch:
- docs/sar/crsd_focused_image_repo_discovery.md
- plan/reviews/SAR_CRSD_TO_FOCUSED_IMAGE_PLANNER_REPORT.md (link/reference update only if needed)

Files to delete:
- None

Tests to add:
- None

Tests to delete:
- None

Acceptance criteria:
- Discovery doc maps CSV injection vs SAR source-node patterns and justifies JSON-configured OrderedCrsdSetInputSourceNode-first approach.
- Discovery doc enumerates existing SAR plugin registration and fixture/test conventions.
- Discovery doc records CRSD writer/validator/reference tool hooks and local-only SarPy boundaries.

Risks:
- Discovery may expose additional CRSD format constraints that refine downstream PR scope.

Rollback plan:
- Revert discovery doc.

CI-safe or local-only classification:
- CI-safe

## PR2

Title:
Ordered CRSD set reader/source-node interface and tiny fixture strategy

Purpose:
Introduce non-invasive interfaces and node contract for ordered CRSD set ingestion with deterministic tiny-fixture support.

Files to touch:
- examples/SAR/include/sar/OrderedCrsdSetInputSourceNode.hpp
- examples/SAR/src/OrderedCrsdSetInputSourceNode.cpp
- examples/SAR/plugins/crsd_input_source_node_plugin.cpp
- examples/SAR/plugins/CMakeLists.txt
- examples/SAR/include/sar/io/CrsdReader.hpp
- examples/SAR/src/io/CrsdReader.cpp
- examples/SAR/test/test_crsd_input_source_node.cpp
- examples/SAR/test/CMakeLists.txt
- examples/SAR/config/sar_crsd_tiny_fixture_set_input.json

Files to delete:
- None

Tests to add:
- OrderedCrsdSetInputSourceNode configuration/validation tests for `crsd_paths`, `crsd_directory`, and manifest modes.
- Tiny multi-segment CRSD fixture ingestion tests (segment order, per-segment shape, total vector count, type checks).
- Plugin load + JSON topology smoke test for OrderedCrsdSetInputSourceNode.
- Focused CRSD input-node contract test proving:
  - the configured CRSD file set or directory is used
  - emitted total vector count equals the sum of CRSD segment vector counts
  - emitted per-segment vector ranges are deterministic and contiguous
  - emitted sample count equals CRSD sample count for every segment
  - payload checksum/hash matches CRSD signal data per segment and for the ordered set
  - first/last vector PVP metadata matches the fixture
  - first/last vector geometry metadata (for example platform position/velocity or mapped equivalent) matches the fixture
  - sidecar JSON is treated as optional preflight/report data, not as authoritative signal/PVP data
  - duplicate, missing, or out-of-order CRSD segments fail deterministically unless an explicit manifest orders them
  - missing/unsupported CRSD fields fail with deterministic diagnostics

Tests to delete:
- None

Acceptance criteria:
- OrderedCrsdSetInputSourceNode accepts ordered CRSD paths, CRSD directory, or manifest via node_config.
- Node reads CRSD metadata + signal + required PVP subset from each `product.crsd` through C++ reader interfaces.
- Node emits one ordered aperture-set stream intended to produce one focused image, not one focused image per segment.
- CI fixture strategy is defined and enforced without requiring real GOTCHA data.
- Tests prove the node emits CRSD-derived signal payload and PVP metadata, not synthetic placeholder tokens.
- Tests prove CRSD geometry metadata required for focused image formation is emitted and validated.
- Tests prove generated sidecars (`metadata.json`, `pvp.json`, `chunk_index.json`, `provenance.json`, validation JSON) are optional sanity/provenance inputs only.
- The node exposes clear unsupported-format diagnostics for CRSD files outside the initial supported subset.

Risks:
- CRSD parser subset may initially be narrow; clear unsupported diagnostics required.
- Directory-based discovery may accidentally include non-product files unless discovery is constrained to `*/product.crsd` or manifest entries.

Rollback plan:
- Revert new node/reader/plugin and tests.

CI-safe or local-only classification:
- CI-safe

## PR3

Title:
CRSD aperture assembly to SAR phase-history adapter/model

Purpose:
Bridge ordered CRSD segment ingest model to a full-aperture SAR phase-history model consumed by focused-image transform, without changing GraphX runtime contracts globally.

Files to touch:
- examples/SAR/include/sar/CrsdApertureAssemblyAdapterNode.hpp
- examples/SAR/src/CrsdApertureAssemblyAdapterNode.cpp
- examples/SAR/plugins/crsd_phase_history_adapter_node_plugin.cpp
- examples/SAR/plugins/CMakeLists.txt
- examples/SAR/include/sar/SarPhaseHistoryMessages.hpp
- examples/SAR/test/test_crsd_phase_history_adapter.cpp
- examples/SAR/test/CMakeLists.txt
- examples/SAR/config/sar_crsd_tiny_fixture_adapter.json

Files to delete:
- None

Tests to add:
- Adapter segment ordering, full-aperture accounting, and determinism tests
- Adapter metadata/PVP mapping correctness tests
- Adapter EOS/control-marker propagation tests
- Token/payload contract tests proving:
  - CRSD samples are carried in SarPhaseHistoryFrame or an explicitly described host/device buffer payload
  - SarPhaseHistoryFrame ownership mode is explicit and enforced (no dangling/implicit ownership)
  - SarPhaseHistoryFrame buffer layout metadata is explicit and validated at adapter output
  - payload checksum is stable at adapter output and preserved across split/merge boundaries
  - EOS/control markers propagate through adapter/split/merge without sample payload corruption
  - SarSidecar is used only for routing/identity/diagnostics
  - sample payload checksum survives adapter boundaries
  - vector index/channel/sample ordering survives adapter boundaries
  - SarAccelControlToken is preserved on all SAR edges through adapter output topology
- Ordered-aperture tests proving:
  - total output vector count equals sum of segment vectors
  - sample/channel/frequency metadata consistency is enforced across segments
  - segment gaps, duplicates, and unexpected ordering produce deterministic diagnostics
  - segment sidecar pulse ranges are used only as optional cross-checks

Tests to delete:
- None

Acceptance criteria:
- Adapter emits assembled full-aperture SAR phase-history messages/tokens compatible with downstream focused-image transform.
- Required geometry/sampling fields are present and validated.
- Deterministic ordering and repeatability are demonstrated in CI.
- The report/docs for this PR explain exactly how token-based messages carry CRSD samples, PVP fields, geometry, and EOS/control markers.
- Tests fail if the adapter drops payload data and emits only diagnostic/control tokens.
- Split/merge partition metadata contract is defined so PR4 can parallelize transform branches without changing edge type.
- SarPhaseHistoryFrame (or equivalent) contract is documented with explicit ownership, buffer layout, checksum, EOS semantics, and sidecar-vs-physics boundaries.
- Tests fail if physics/algorithm logic reads CRSD semantics from sidecar-only metadata instead of typed phase-history payload.
- Tests fail if the adapter treats each CRSD segment as a separate final image instead of one ordered aperture.

Risks:
- Mapping ambiguity between CRSD field names and existing SAR model assumptions.

Rollback plan:
- Revert adapter/message additions.

CI-safe or local-only classification:
- CI-safe

## PR4

Title:
Focused image formation path in GraphX

Purpose:
Add first true focused-image transform (backprojection-based) from CRSD-derived phase history.

Files to touch:
- examples/SAR/include/sar/CrsdFocusedImageTransformNode.hpp
- examples/SAR/src/CrsdFocusedImageTransformNode.cpp
- examples/SAR/plugins/crsd_focused_image_transform_node_plugin.cpp
- examples/SAR/plugins/CMakeLists.txt
- examples/SAR/include/sar/SarCpuReference.hpp (reuse-only integration points as needed)
- examples/SAR/test/test_crsd_focused_image_transform.cpp
- examples/SAR/config/sar_crsd_tiny_fixture_focused_image.json

Files to delete:
- None

Tests to add:
- Focused-image determinism tests (same input => same output hash)
- Geometry assumption tests (grid dimensions and coordinate mapping)
- Basic image quality sanity tests (finite, non-zero, stable peak)
- Input-dependence tests:
  - all-zero ordered CRSD set produces a near-zero image
  - known tiny coherent multi-segment fixture produces one deterministic peak location
  - changing one CRSD sample in one segment changes the focused image hash
  - changing relevant PVP/platform geometry changes the focused image output
  - dropping or reordering a segment fails or changes the focused image according to policy
  - output change is verified with artifact hash delta and image-metric delta (at minimum RMSE or peak location/value delta)
  - CRSD signal magnitude quick-look output is rejected as a focused-image result
- Diagnostics-only failure tests:
  - token-forwarding/timing-only execution path must fail focused-image correctness assertions
  - fixture payload ignored/mocked path must fail hash/peak-change assertions
- Parallel split/merge tests:
  - split branches preserve SarAccelControlToken contract per branch
  - merge enforces deterministic output given identical branch inputs
  - parallel branch count > 1 yields same output as single-branch baseline (within tolerance)

Tests to delete:
- None

Acceptance criteria:
- GraphX path computes one real focused SAR image from the ordered CRSD-derived full-aperture phase history.
- Path is not a CRSD magnitude quick-look.
- Image dimensions, geometry assumptions, and output dtype/layout are explicit and stable.
- Focused-image output is data-dependent on CRSD signal and PVP/geometry fields.
- Tests prove actual processing happens, not diagnostic-only token forwarding or synthetic placeholder image generation.
- Focused-image topology supports split/merge parallel execution with SarAccelControlToken preserved on every SAR edge.
- Tests provide explicit evidence that changing CRSD samples changes the produced focused-image artifacts.
- The focused-image proof matrix (all-zero, coherent peak, one-sample perturbation, PVP perturbation, deterministic peak repeatability) passes in CI.
- A diagnostics-only or payload-ignored implementation fails the PR4 suite.
- Tests fail if the implementation writes one final focused image per CRSD segment instead of one focused image for the ordered aperture set.

Risks:
- Algorithmic mismatch if adapter assumptions are incomplete.

Rollback plan:
- Revert focused-image transform node and topology additions.

CI-safe or local-only classification:
- CI-safe

## PR5

Title:
Metal focused-image execution proof

Purpose:
Add an explicit Metal execution lane for CRSD-derived focused-image processing and prove that selected Metal nodes perform real transfer/kernel work.

Files to touch:
- examples/SAR/include/sar/CrsdFocusedImageTransformMetal.hpp (or equivalent if implementation keeps one configurable transform)
- examples/SAR/src/CrsdFocusedImageTransformMetal.cpp (or equivalent)
- examples/SAR/plugins/crsd_focused_image_transform_node_plugin.cpp
- examples/SAR/plugins/CMakeLists.txt
- examples/SAR/config/sar_crsd_tiny_fixture_focused_image_cpu.json
- examples/SAR/config/sar_crsd_tiny_fixture_focused_image_metal.json
- examples/SAR/test/test_crsd_focused_image_metal.cpp
- examples/SAR/test/CMakeLists.txt

Files to delete:
- None

Tests to add:
- Resolver-selection test proving the Metal lane selects Metal-capable H2D/kernel/D2H nodes.
- Metal execution diagnostics test requiring bytes_h2d > 0, bytes_d2h > 0, and kernel_dispatches > 0.
- CPU-vs-Metal focused-image parity test on the same tiny multi-segment CRSD fixture.
- Tiny dual-lane runner test that executes CPU then Metal paths and emits a comparison evidence bundle (checksums/hashes/metrics).
- Negative guardrail test that fails if the Metal lane only forwards tokens without kernel execution.

Tests to delete:
- None

Acceptance criteria:
- Metal lane uses the same CRSD-derived phase-history payload contract as the CPU lane.
- Metal execution reports nonzero transfer and kernel diagnostics.
- Metal output matches CPU baseline within documented deterministic tolerances.
- The lane remains optional/gated where native Metal is unavailable, without weakening CPU CI coverage.
- Metal split/merge lane keeps SarAccelControlToken edge flow and preserves GPU backfill diagnostics across branch fan-out/fan-in.
- A tiny multi-segment-fixture CPU+Metal focused-image lane runs in automation and records the same ordered-set input checksum with explicit CPU/Metal output hashes and parity metrics.

Risks:
- Native Metal availability varies by platform/build preset.
- Numerical parity tolerance may need tight but realistic platform-aware bounds.

Rollback plan:
- Revert Metal-specific transform/config/test additions while preserving CPU focused-image path.

CI-safe or local-only classification:
- CI-safe when simulated/stub Metal diagnostics can prove contract; native Metal execution is platform-gated/local where unavailable.

## PR6

Title:
Deterministic focused-image output sink and artifact contract

Purpose:
Persist focused-image artifacts in deterministic machine-readable form with convenience visualization outputs.

Files to touch:
- examples/SAR/include/sar/CrsdFocusedImageSinkNode.hpp
- examples/SAR/src/CrsdFocusedImageSinkNode.cpp
- examples/SAR/plugins/crsd_focused_image_sink_node_plugin.cpp
- examples/SAR/plugins/CMakeLists.txt
- examples/SAR/tools/sar_focused_image_artifact_schema.json
- examples/SAR/test/test_crsd_focused_image_sink.cpp
- examples/SAR/config/sar_crsd_tiny_fixture_with_sink.json

Files to delete:
- None

Tests to add:
- Artifact schema/contract tests
- Deterministic binary + JSON consistency tests
- PNG/PGM convenience artifact generation tests

Tests to delete:
- None

Acceptance criteria:
- Sink writes deterministic binary/JSON artifact pair and convenience image output.
- Metadata needed for comparison is preserved (shape, spacing, assumptions, hashes, provenance).
- Artifact contract records whether the image came from CPU or Metal execution and includes ordered CRSD segment list, per-segment input hashes, ordered-set hash, output hash, and enough lineage to prove one image was formed from the complete aperture set.

Risks:
- Cross-platform file encoding/hashing drift if serialization rules are underspecified.

Rollback plan:
- Revert sink and artifact schema additions.

CI-safe or local-only classification:
- CI-safe

## PR7

Title:
SarPy/reference focused-image generation harness from CRSD

Purpose:
Provide local-only/gated reference generation from the same ordered CRSD input set, producing one focused reference image artifact compatible with comparison lane.

Files to touch:
- tools/sarpy/reference_image_from_crsd.py
- tools/sarpy/README.md
- examples/SAR/test/test_sarpy_crsd_validation_harness.cpp
- examples/SAR/test/test_sarpy_reference_compare_tools.cpp
- examples/SAR/test/CMakeLists.txt

Files to delete:
- None

Tests to add:
- Probe-only tests asserting local_only=true and ci_safe=false
- Optional local smoke test for focused reference generation path
- Discovery/probe test documenting whether installed SarPy can form a true focused image from CRSD.
- Guardrail test/documentation that rejects CRSD signal block magnitude extraction as a focused reference.

Tests to delete:
- None

Acceptance criteria:
- SarPy/reference script can generate reference-focused artifact metadata from an ordered CRSD input set.
- SarPy workflow remains local-only/gated and does not become runtime dependency.
- If SarPy cannot generate a true focused image from CRSD, this PR must document the limitation and provide or select an independent local reference image-formation harness instead.
- Reference output must be generated from the same ordered CRSD signal/PVP/geometry fields used by GraphX and must produce one focused image for the set.

Risks:
- SarPy API variation across local environments.

Rollback plan:
- Revert SarPy harness changes; keep existing probe/validation scripts intact.

CI-safe or local-only classification:
- Local-only

## PR8

Title:
GraphX-vs-SarPy focused-image comparison lane

Purpose:
Add deterministic comparison lane producing required metrics and diff artifacts.

Files to touch:
- tools/sarpy/compare_images.py
- tools/sarpy/README.md
- examples/SAR/test/test_graphx_image_comparison_lane.cpp
- examples/SAR/test/test_image_comparator_metrics.cpp
- examples/SAR/test/CMakeLists.txt
- examples/SAR/tools/sar_image_comparison_report.schema.json (if schema extension needed)

Files to delete:
- None

Tests to add:
- Comparison metrics contract tests for RMSE/phase RMSE/peak error/correlation/optional SSIM
- Artifact emission tests for comparison_report.json, difference_magnitude.png, phase_difference.png
- Deterministic tiny-fixture lane test (CI-safe baseline path)

Tests to delete:
- None

Acceptance criteria:
- Same ordered CRSD input set is used by both GraphX focused-image path and reference lane.
- Comparison outputs include required JSON + diff images.
- Threshold evaluation is deterministic and documented.
- Comparison lane records exact per-segment CRSD input checksums, ordered-set checksum, GraphX output hash, reference output hash, and focused-image algorithm/geometry assumptions.

Risks:
- Metric threshold instability if fixtures or scaling normalization are not frozen.

Rollback plan:
- Revert comparison lane additions and schema updates.

CI-safe or local-only classification:
- CI-safe (with tiny deterministic fixture); optional extended SarPy run remains gated/local

## PR9

Title:
Local-only GOTCHA-derived CRSD validation workflow

Purpose:
Add explicitly gated workflow for real GOTCHA-derived CRSD to validate end-to-end focused image behavior outside CI.

Files to touch:
- scripts/convert_gotcha_subdata_to_crsd.sh
- docs/sar/gotcha_large_scene_data_description.md
- docs/CONSOLIDATED_OPERATIONS.md
- examples/SAR/config/sar_crsd_gotcha_local_validation.json
- examples/SAR/test/test_gotcha_real_full_aperture_validation.cpp
- examples/SAR/test/CMakeLists.txt

Files to delete:
- None

Tests to add:
- Disabled-by-default local-only end-to-end validation test using env-gated dataset path
- Local workflow smoke checks for required artifacts and reports
- Local real-data test using the generated layout:
  - `data/crsd/subData01.crsd_output/.../product.crsd` through `subData10.../product.crsd`
  - verifies all selected CRSD products are treated as one ordered aperture set
  - verifies one final focused image artifact set is emitted
  - verifies dropping/reordering one segment fails or changes output deterministically

Tests to delete:
- None

Acceptance criteria:
- Real-data workflow is explicitly local-only and opt-in.
- CI remains independent of real GOTCHA datasets and SarPy installation.
- Workflow reuses the same ordered CRSD input set for GraphX focused-image and reference comparison.
- Workflow proves processing happened by checking one focused-image artifact set, nonzero image metrics, per-segment input checksums, ordered-set checksum, and output checksum.
- Workflow treats `metadata.json`, `pvp.json`, `chunk_index.json`, `provenance.json`, and SarPy validation JSON as optional sidecar evidence, not required signal/PVP input.

Risks:
- User environment variability (dataset layout, package availability, platform tooling).

Rollback plan:
- Revert local-only workflow wiring and docs.

CI-safe or local-only classification:
- Local-only

## PR10

Title:
Documentation finalization and guardrail verification

Purpose:
Consolidate docs, examples, and guardrail tests for CRSD-to-focused-image path and operational boundaries.

Files to touch:
- README.md
- examples/SAR/README.md
- docs/CONSOLIDATED_OPERATIONS.md
- docs/sar/crsd_definition.md
- docs/sar/crsd_to_focused_image.md (new)
- examples/SAR/config/sar_crsd_tiny_fixture_full_pipeline.json
- examples/SAR/test/test_ci_validation_lane.cpp
- examples/SAR/test/test_sar_json_pipeline.cpp

Files to delete:
- None

Tests to add:
- Guardrail tests that reject quick-look-only misuse in focused-image lane
- Contract tests ensuring SarPy remains local-only/non-runtime
- Config example validation tests (tiny CI and local GOTCHA-derived)
- Guardrail tests that reject diagnostic-only graph execution in the focused-image lane.
- Guardrail tests that verify CRSD input payload hashes and focused output hashes are recorded in artifacts.
- Guardrail tests that fail if any SAR-related topology edge switches away from SarAccelControlToken.
- Guardrail tests that verify split/merge parallel topology remains deterministic and preserves GPU backfill evidence.

Tests to delete:
- Obsolete tests tied to superseded pre-CRSD focused-image assumptions (if any, determined during implementation)

Acceptance criteria:
- Documentation clearly distinguishes CRSD signal quick-look vs focused SAR image.
- End-to-end config examples exist for tiny CI fixture and local GOTCHA-derived CRSD.
- Guardrails enforce planning rules (no MATLAB dependency, no SarPy runtime dependency, local-only real-data lane).
- Documentation explains token-based CRSD phase-history flow, CPU focused-image path, Metal focused-image path, and required processing evidence.
- Documentation and config examples include split/merge parallel topology guidance and explicitly require SarAccelControlToken on all SAR edges.
- Documentation includes the focused-image processing proof matrix and explicitly states that placeholder/timing-only stages are insufficient for focused-image acceptance.

Risks:
- Documentation drift if implementation details change late.

Rollback plan:
- Revert doc and guardrail updates; preserve core implementation PRs.

CI-safe or local-only classification:
- CI-safe (with local-only sections explicitly labeled)

---

Cross-PR sequencing notes:
- PR2 and PR3 establish ingestion/model compatibility before focused-image algorithm work.
- PR4 delivers CPU true focused-image production.
- PR5 proves Metal execution and CPU-vs-Metal parity.
- PR6 persists deterministic artifacts.
- PR7 and PR9 are local-only/gated integration envelopes.
- PR8 provides formal comparison outputs and metrics contract.
- PR10 closes user-facing docs and guardrail enforcement.

Stop condition met: planner report created only; no code implementation performed.
