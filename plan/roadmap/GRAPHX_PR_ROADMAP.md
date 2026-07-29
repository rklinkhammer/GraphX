# GraphX Cleanup PR Roadmap

Date: 2026-06-28

Status: Proposed; no implementation authorized.

Inputs:

- `plan/reviews/GRAPHX_INSPECTOR_REPORT.md`
- `plan/reviews/GRAPHX_SIMPLIFIER_REPORT.md`
- `plan/agents/GRAPHX_AGENT_ROLES.md`

## Roadmap Rules

- Every PR builds and tests independently.
- Every PR has one architectural or correctness concern.
- No compatibility aliases, adapters, deprecated forwarding APIs, or dual
  canonical paths are introduced.
- When an old abstraction is replaced, its users and tests move in the same PR
  and the old abstraction is deleted.
- Correctness precedes architecture cleanup; contracts precede graph wiring;
  instrumentation precedes optimization; deterministic fixtures precede
  external data or production claims.
- `GraphExecutorBuilder` plus the repository JSON/plugin/provider flow remains
  the sole canonical execution path.
- CPU SAR focused-image formation is canonical. There is exactly one named SAR
  GPU path; it remains explicitly experimental until it executes a real GPU
  algorithm and passes parity tests.
- Existing SAR baseline tools are consolidated in place. No second runner,
  comparison harness, fixture format, or substitution path is added.

## Planning Gates

- PR7 requires confirmation that `StaticNodeAdapter` has no unique active
  capability. Any unique capability must first move to the typed node/facade
  model; the adapter is still deleted in PR7.
- PR11 assumes the embedded dashboard is an FHSS development application. If it
  is instead a supported generic GraphX product, only the generic server and
  read-only snapshot API remain in `libgraph`; all FHSS behavior still moves.
- PR13 requires an explicit supported-backend list and a test owner for every
  retained backend.
- PR19 cannot start until PR15 and PR16 establish stable SAR token/artifact
  contracts and basic performance instrumentation.
- A licensing failure in PR24 ends the CI-derived-fixture branch without
  blocking PR25 or PR26 local-only work.

---

## PR1: Restore Canonical FHSS Pulse Completeness

- **Purpose:** Fix the reproducible 36-versus-72 pulse truncation before any
  FHSS refactor.
- **Scope:** Trace source, detector, merge, decoder, and sink counts through the
  canonical JSON executor path; correct the first stage that loses or
  prematurely completes data. Do not redesign node APIs.
- **Files likely to touch:** `libdsp/src/dsp/fhss/`,
  `libdsp/include/dsp/fhss/`, `libgraph/test/unit/test_fhss_graphx_executor.cpp`,
  canonical FHSS config only if the defect is configuration-derived.
- **Files likely to delete:** Defect-preserving expected-count helpers or tests,
  if present.
- **Tests to add:** Per-stage pulse-count diagnostics; repeated canonical runs;
  scheduling-order regression with all 72 fixture pulses.
- **Tests to update/delete:** Replace any assertion accepting partial output;
  retain the 72-pulse end-to-end assertion.
- **Acceptance criteria:** Canonical executor emits, decodes, and assembles all
  72 truth pulses deterministically; no new sleeps or count-based workaround.
- **Truth-in-labeling:** Deterministic CPU fixture, not production acquisition.
- **Risks:** Root cause may be timing-sensitive or span more than one stage.
- **Rollback:** Revert the correctness patch and its tests as one unit; do not
  weaken the 72-pulse expectation.
- **Status:** CI-safe.

## PR2: Split Core, DSP/FHSS, and SAR Test Ownership

- **Purpose:** Make later cleanup reviewable and reduce monolithic rebuilds.
- **Scope:** Move FHSS/DSP tests out of the `libgraph` unit executable; split SAR
  tests into CRSD I/O, SAR node, runtime integration, and local-only targets.
  Preserve test behavior and fixtures.
- **Files likely to touch:** `libgraph/test/CMakeLists.txt`, new or existing
  `libdsp/test/CMakeLists.txt`, `examples/SAR/test/CMakeLists.txt`, test source
  locations, presets/README test commands.
- **Files likely to delete:** Duplicate target source lists and duplicate test
  registration blocks.
- **Tests to add:** CTest discovery guardrail proving each ownership lane is
  registered and independently runnable.
- **Tests to update/delete:** Update path macros and target names; delete no
  behavior tests.
- **Acceptance criteria:** Core, DSP/FHSS, and each SAR lane builds/runs alone;
  PR1 regression remains green.
- **Truth-in-labeling:** Local-only and device-required tests retain explicit
  labels and skips.
- **Risks:** Plugin output paths and compile definitions may be target-coupled.
- **Rollback:** Restore prior target grouping without changing test bodies.
- **Status:** CI-safe; local-only tests remain opt-in.

## PR3: Add Explicit Typed Completion Semantics

- **Purpose:** Replace implicit count/timing completion with typed control flow.
- **Scope:** Define domain-neutral EOS, watermark, cancellation, and failure
  state for typed edges; update one generic fixed-fan test node and the FHSS
  merge path. Required fan-in completes only after every required input reaches
  a terminal state.
- **Files likely to touch:** `libgraph/include/graph/`, `libgraph/test/`,
  `libdsp/include/dsp/fhss/FHSSPulseMergeNode.hpp`, corresponding source/tests.
- **Files likely to delete:** Implicit FHSS completion counters and sentinel
  completion helpers superseded by typed state.
- **Tests to add:** Out-of-order EOS, missing-input, cancellation, failure, and
  all-input completion tests; 64-channel merge regression.
- **Tests to update/delete:** Update fixed-fan and FHSS merge lifecycle tests;
  delete tests tied only to implicit completion.
- **Acceptance criteria:** Scheduling order cannot truncate data; missing input
  yields deterministic incomplete/failure diagnostics rather than success.
- **Truth-in-labeling:** EOS/watermark means graph control state, not RF silence.
- **Risks:** Lifecycle and queue shutdown interactions.
- **Rollback:** Revert typed completion and FHSS migration together; PR1 remains
  the correctness baseline.
- **Status:** CI-safe.

## PR4: Normalize FHSS Packets and Remove Runtime Truth

- **Purpose:** Separate domain evidence, transport, and fixture validation.
- **Scope:** Rename `FHSSGraphXPackets` to `FHSSPackets`; split catch-all node
  utilities into packet/port/fixture headers; move truth records out of runtime
  edge packets; preserve `ControlToken<PacketT>` contracts.
- **Files likely to touch:** `libdsp/include/dsp/fhss/`, `libdsp/src/dsp/fhss/`,
  FHSS plugins/config descriptors, DSP/FHSS tests.
- **Files likely to delete:** `FHSSGraphXPackets.hpp`,
  `FHSSGraphXNodeUtils.hpp`, runtime truth fields/helpers, stale PR-number docs.
- **Tests to add:** Compile-time packet/token separation; runtime proof that
  decoder output is derived from evidence without truth fields.
- **Tests to update/delete:** Rename includes/types; delete truth-propagation
  tests.
- **Acceptance criteria:** Runtime packets contain only required domain data;
  validation truth exists only in fixtures/tests; all canonical edges have one
  declared type.
- **Truth-in-labeling:** Complex evidence remains decoder input; magnitude is
  observational only.
- **Risks:** Broad include churn and plugin type-name updates.
- **Rollback:** Revert the complete rename/removal; no aliases are permitted.
- **Status:** CI-safe.

## PR5: Make One Fixed-Fan Node Mechanism Canonical

- **Purpose:** Remove overlapping routed/fixed-port machinery.
- **Scope:** Collapse routed input/output/transfer behavior into one typed
  fixed-fan base; migrate every active fixed-fan user, including FHSS
  channelizer and pulse merge; delete superseded helpers in the same PR.
- **Files likely to touch:** `libgraph/include/graph/FixedFanInOutNode.hpp`,
  `RoutedFunctions.hpp`, port descriptors, fixed-fan users and tests.
- **Files likely to delete:** Superseded routed helper/base declarations and
  tests that duplicate canonical fixed-fan behavior.
- **Tests to add:** Compile-time generated port table, no-output transfer,
  lifecycle, metrics, and high-fan-in/out smoke tests.
- **Tests to update/delete:** Migrate all fixed-fan node tests; delete tests for
  removed helper APIs.
- **Acceptance criteria:** One fixed-fan mechanism remains; FHSS still exposes
  64 separate channel outputs and merge completeness remains green.
- **Truth-in-labeling:** Generic helper code contains no FHSS semantics.
- **Risks:** Template diagnostics and broad header rebuild.
- **Rollback:** Revert migration and deletion together; no forwarding bases.
- **Status:** CI-safe.

## PR6: Truthfully Name and Generate the FHSS Fixture Topology

- **Purpose:** Remove misleading channelizer naming and hand-maintained 64-lane
  duplication without changing runtime JSON loading.
- **Scope:** Rename `ChannelizerNode` to
  `FHSSFixtureFrequencyChannelizerNode`; add a deterministic authoring tool that
  emits ordinary expanded GraphX JSON with 64 ports, detectors, and edges.
- **Files likely to touch:** FHSS node/plugin/config files,
  `examples/DSP/tools/`, generated canonical config, README/baseline, tests.
- **Files likely to delete:** Old channelizer files/plugin name and hand-edited
  canonical config source.
- **Tests to add:** Generator determinism; generated config equality; 64 distinct
  ports/detectors/edges; normal `GraphExecutorBuilder` execution.
- **Tests to update/delete:** Rename node/plugin assertions; delete brittle
  manually enumerated duplication checks replaced by structural checks.
- **Acceptance criteria:** Runtime consumes normal expanded JSON only; no graph
  adaptor or aggregate channel packet; generated output is deterministic.
- **Truth-in-labeling:** Mix/decimate fixture channelization has no production
  filter/separation claim.
- **Risks:** Generated-file drift and plugin type-name churn.
- **Rollback:** Revert rename/tool/config atomically; no old-name alias.
- **Status:** CI-safe.

## PR7: Delete `StaticNodeAdapter`

- **Purpose:** Remove the alternate static-node abstraction.
- **Scope:** Move any unique active behavior to real typed nodes or the existing
  plugin facade, migrate all call sites, then delete the adapter.
- **Files likely to touch:** `libgraph/include/graph/StaticNodeAdapter.hpp`,
  `libgraph/src/graph/StaticNodeAdapter.cpp`, call sites, CMake, tests/docs.
- **Files likely to delete:** Adapter header/source and adapter-only tests/docs.
- **Tests to add:** Direct typed-node and plugin-facade coverage for each moved
  capability.
- **Tests to update/delete:** Replace adapter construction tests; delete
  compatibility behavior tests.
- **Acceptance criteria:** No active reference remains; all graphs use real
  nodes through the one lifecycle; full core and domain lanes pass.
- **Truth-in-labeling:** No replacement type masquerades as a graph node.
- **Risks:** A hidden ABI or reflection dependency may surface.
- **Rollback:** Revert the entire PR; do not restore a partial shim.
- **Status:** CI-safe after planning-gate audit.

## PR8: Consolidate Node Registry and Plugin Facade

- **Purpose:** Establish one registry and one ABI boundary.
- **Scope:** Merge duplicate plugin/reflection registries; make direct and plugin
  providers consume the same concrete-node registry; keep the facade thin.
- **Files likely to touch:** `libgraph/include/plugins/`, `libgraph/src/plugins/`,
  `PluginReflection.hpp`, providers/bootstrap, plugin tests.
- **Files likely to delete:** Duplicate registry implementation and redundant
  reflection/facade utilities.
- **Tests to add:** Same descriptor and construction result through direct and
  plugin providers; duplicate-registration and ABI-error diagnostics.
- **Tests to update/delete:** Migrate registry tests; delete duplicate suites.
- **Acceptance criteria:** One authoritative registration record and lifecycle;
  plugins do not define a second node model.
- **Truth-in-labeling:** Diagnostics distinguish plugin load, registration, and
  node construction failures.
- **Risks:** Plugin ABI surface and independently built consumers.
- **Rollback:** Revert as one registry/facade unit; no bridge registry.
- **Status:** CI-safe; ABI policy must be recorded.

## PR9: Strongly Type Resolver Contracts

- **Purpose:** Replace ad hoc backend/fallback strings with explicit contracts.
- **Scope:** Introduce strong backend, fallback, capability, and resolution
  result types; update parser, provider, diagnostics, and JSON serialization.
- **Files likely to touch:** `GraphConfig*`, `ResolvingNodeProvider*`,
  `NodeResolutionRegistry*`, libgpu backend types, resolver tests/configs.
- **Files likely to delete:** Stringly typed resolver helpers and duplicate
  parsing branches.
- **Tests to add:** Compile-time type checks; deterministic JSON diagnostics;
  unsupported backend and fallback matrix.
- **Tests to update/delete:** Update resolver configs/assertions; delete tests of
  removed string helper APIs.
- **Acceptance criteria:** Invalid backend/fallback combinations fail at parse or
  resolution with stable structured errors.
- **Truth-in-labeling:** Selected, unavailable, fallback, and unsupported are
  distinct states.
- **Risks:** JSON schema and plugin descriptor churn.
- **Rollback:** Revert type/parser/provider changes together; no legacy parser.
- **Status:** CI-safe.

## PR10: Decompose Core Runtime Hotspot Headers

- **Purpose:** Reduce coupling without changing public execution behavior.
- **Scope:** Split `Nodes.hpp` into focused shape/lifecycle/port headers and
  split `GraphManager.hpp` internals into graph ownership, lifecycle, edge
  ownership, and metrics snapshot components. `GraphExecutor` remains public
  lifecycle owner.
- **Files likely to touch:** `libgraph/include/graph/Nodes.hpp`,
  `GraphManager.hpp`, new focused headers/sources, includes, CMake, core tests.
- **Files likely to delete:** Umbrella implementation bodies and duplicate
  forwarding helpers after all includes migrate.
- **Tests to add:** Header self-containment compilation; lifecycle/metrics parity
  tests.
- **Tests to update/delete:** Update includes; delete tests that only validate
  removed forwarding methods.
- **Acceptance criteria:** No behavior or schema change; focused headers compile
  independently; no deprecated include shim remains.
- **Truth-in-labeling:** Internal decomposition is not a performance claim.
- **Risks:** Large mechanical include changes and template build failures.
- **Rollback:** Revert each decomposition as one PR; old umbrella file is not
  retained alongside the new layout.
- **Status:** CI-safe.

## PR11: Move FHSS Dashboard Behavior Out of `libgraph`

- **Purpose:** Keep core domain-neutral.
- **Scope:** Retain only generic read-only graph metrics/snapshot interfaces in
  core; move FHSS configuration mutation, scenario stepping, event replay,
  visualization, artifacts, and application lifecycle composition under
  `examples/DSP/dashboard` with truthful FHSS names.
- **Files likely to touch:** `libgraph/include/graph/dashboard/`,
  `libgraph/src/dashboard/`, `examples/DSP/dashboard/`, FHSS demo/CMake/tests.
- **Files likely to delete:** FHSS-specific core dashboard classes/schemas and
  mock lifecycle behavior.
- **Tests to add:** Core snapshot API test; real builder/executor dashboard
  start/stop test; FHSS application API tests.
- **Tests to update/delete:** Relocate dashboard tests; delete test-double-only
  lifecycle tests.
- **Acceptance criteria:** `libgraph` has no FHSS type/schema; dashboard invokes
  `GraphExecutorBuilder`; no second runtime lifecycle.
- **Truth-in-labeling:** Dashboard is an optional FHSS development application.
- **Risks:** Socket-test portability and ownership of generic HTTP code.
- **Rollback:** Revert move atomically; no forwarding headers/namespaces.
- **Status:** CI-safe where localhost is available; socket-restricted runners
  use an explicit test label/skip, not silent success.

## PR12: Remove Build and Documentation Debris

- **Purpose:** Delete inactive build paths and duplicate declarations.
- **Scope:** Remove the module pilot if the gate has no owner/matrix; centralize
  C++26 enforcement; remove duplicate static-library links; delete the malformed
  simplifier report and stale active references.
- **Files likely to touch:** Top-level/library CMake files, presets, README,
  active plan/docs, package smoke tests.
- **Files likely to delete:** Module pilot files/options when unowned;
  `plan/reviews/GRAPHX_SIMPLIFIER_REPORT.m`; obsolete active-doc fragments.
- **Tests to add:** Configure/build matrix guardrail; installed package smoke;
  active-doc link/path check.
- **Tests to update/delete:** Delete module-pilot tests if the feature is
  removed; update documented commands.
- **Acceptance criteria:** One C++ standard declaration; no duplicate-link
  warnings attributable to GraphX targets; active docs reference `.md` report.
- **Truth-in-labeling:** Optional backends remain optional and accurately
  reported at configure time.
- **Risks:** Downstream target assumptions.
- **Rollback:** Revert build cleanup as one unit; do not restore dead options as
  ignored compatibility flags.
- **Status:** CI-safe.

## PR13: Delete Unsupported Backend Node Surfaces

- **Purpose:** Retain only executable accelerator capabilities.
- **Scope:** For each supported backend, require executable capability results
  and deterministic unsupported diagnostics; delete nodes that advertise but do
  not perform their operation. Do not add GPU algorithms.
- **Files likely to touch:** `libgpu/include/gpu/{cuda,metal}/`, matching
  sources/plugins, capability bootstrap, configs/tests/docs.
- **Files likely to delete:** Placeholder node/plugin/config surfaces for
  unsupported operations or unowned backends.
- **Tests to add:** Backend capability matrix; real-operation or explicit
  unsupported result for every retained node; no simulated-success guardrail.
- **Tests to update/delete:** Delete placeholder-success tests and tests for
  removed backend surfaces.
- **Acceptance criteria:** Every retained node performs named work when
  available or fails explicitly before graph execution; each backend has an
  owner and test lane.
- **Truth-in-labeling:** Accelerator-ready contract is not GPU execution;
  unavailable and unsupported are distinct.
- **Risks:** Large deletion if CUDA lacks support commitments.
- **Rollback:** Revert per backend, never by restoring placeholder success.
- **Status:** CI-safe stubs/capability checks; device execution is backend-CI or
  local-only.

## PR14: Split Native Metal Capability Implementation

- **Purpose:** Reduce the 2,199-line Metal hotspot without changing capability
  semantics.
- **Scope:** Separate device discovery, memory/transfer, kernel dispatch,
  synchronization, and diagnostics behind the existing one capability
  interface.
- **Files likely to touch:** `NativeMetalCapabilities.*`, new Metal internal
  implementation files, libgpu CMake/tests.
- **Files likely to delete:** Monolithic implementation body and duplicated
  internal helpers.
- **Tests to add:** Component-level failure injection and diagnostic parity;
  native-device smoke where available.
- **Tests to update/delete:** Update internal test seams; retain public
  capability contract tests.
- **Acceptance criteria:** Public behavior and diagnostics are unchanged; each
  implementation unit has one responsibility.
- **Truth-in-labeling:** Host-only tests do not claim native GPU execution.
- **Risks:** Objective-C++/metal-cpp ownership and lifetime boundaries.
- **Rollback:** Revert file split atomically; no parallel implementation.
- **Status:** CI-safe contract tests; native runtime lane device-dependent.

## PR15: Stabilize the SAR Token and Artifact Contract

- **Purpose:** Make one SAR semantic identity model authoritative before
  performance and external comparison work.
- **Scope:** Enforce `ControlToken<SarPacket>` boundaries; keep identity in the
  domain packet; replace pointer/event-shaped test sentinels with typed fake
  views/tickets; standardize focused-image artifact and diagnostics schema.
- **Files likely to touch:** `examples/SAR/include/sar/SarMessages.hpp`, SAR
  transfer/transform/sink nodes, runtime helpers, configs/plugins/tests.
- **Files likely to delete:** Opaque pointer/event identity helpers and tests
  that treat transport as semantics.
- **Tests to add:** Sidecar preservation across H2D/kernel/D2H/split/merge/error;
  artifact schema/hash determinism; compile-time port contracts.
- **Tests to update/delete:** Migrate synthetic transport tests to typed fakes;
  delete pointer-identity tests.
- **Acceptance criteria:** One packet/token contract across canonical SAR
  stages; identical domain packets have identical semantics regardless of
  transport fields.
- **Truth-in-labeling:** Token readiness does not claim GPU execution or image
  quality.
- **Risks:** Broad SAR fixture and plugin descriptor updates.
- **Rollback:** Revert token/artifact migration together; no dual contract.
- **Status:** CI-safe.

## PR16: Isolate Basic SAR Performance Instrumentation

- **Purpose:** Measure graph overhead separately from SAR algorithm cost before
  optimization or external comparison.
- **Scope:** Split `sar_benchmark.cpp` into a small runner and dataset,
  measurement, trace, and reporting components; preserve deterministic CI
  profile and host-qualified metrics.
- **Files likely to touch:** `examples/SAR/src/sar_benchmark.cpp`, new benchmark
  support files, CMake, trace schemas, benchmark tests/docs.
- **Files likely to delete:** Monolithic reporting/measurement code and duplicate
  timing helpers.
- **Tests to add:** Trace schema, deterministic fixture counters, phase
  attribution, one-run statistics, unavailable-device behavior.
- **Tests to update/delete:** Update benchmark contract tests; delete tests that
  infer general speedup from host timings.
- **Acceptance criteria:** Reports separate build, lifecycle, queue, transfer,
  kernel, synchronization, diagnostics, I/O, and algorithm timing where
  measurable.
- **Truth-in-labeling:** Results are current-host fixture measurements, not
  general CPU/GPU or production SAR claims.
- **Risks:** Instrumentation overhead and unstable wall-clock assertions.
- **Rollback:** Revert decomposition and schema together; retain correctness
  tests.
- **Status:** CI-safe small profile; performance characterization local-only.

## PR17: Make the Sole SAR GPU Path Real or Remove Its Canonical Label

- **Purpose:** Eliminate the experimental placeholder from canonical GPU
  semantics while preserving exactly one named SAR GPU path.
- **Scope:** Implement real Metal focused-image work behind the existing sole
  config and prove parity, or—if no implementation is approved—rename/demote the
  path as experimental and leave no claim that GraphX has a canonical GPU
  algorithm. Do not create another GPU config.
- **Files likely to touch:** `CrsdFocusedImageTransformMetal.*`, its plugin and
  sole config, Metal kernel descriptor/source, SAR parity tests/docs.
- **Files likely to delete:** CPU-seed/placeholder kernel behavior; any extra SAR
  GPU configs or aliases.
- **Tests to add:** Kernel-ticket diagnostics, deterministic CPU parity,
  unavailable-device result, sole-path guardrail.
- **Tests to update/delete:** Delete placeholder-status tests after real
  implementation; otherwise update them to explicit experimental demotion.
- **Acceptance criteria:** Exactly one named SAR GPU path exists; it either
  performs real GPU image formation with parity evidence or is unambiguously
  noncanonical/experimental.
- **Truth-in-labeling:** Transfer, placeholder, and domain algorithm work are
  never conflated.
- **Risks:** Real GPU work may not fit one reviewable PR or available hardware.
- **Rollback:** Restore the pre-PR experimental path and label only; never add a
  second canonical path.
- **Status:** Contract/guardrail CI-safe; native parity device-dependent.

## PR18: Keep DSP DFT and FFT Surfaces Truthful

- **Purpose:** Remove obsolete or misleading transform abstractions.
- **Scope:** Retain accurately named CPU/Metal direct DFT nodes; inventory and
  delete unused FFT-labeled managers/surfaces that do not implement FFT; keep
  magnitude output separate from complex evidence.
- **Files likely to touch:** `libdsp/include/dsp/FFTManager.hpp`, spectrum nodes,
  plugins/configs/tests/docs.
- **Files likely to delete:** Unused or DFT-backed FFT-labeled APIs and tests.
- **Tests to add:** Algorithm-name guardrail; known-vector direct DFT; packet
  separation compile-time test.
- **Tests to update/delete:** Delete tests for removed labels/surfaces; retain
  CPU/Metal DFT comparison as informational.
- **Acceptance criteria:** No active FFT claim without an FFT implementation;
  canonical FHSS input remains complex IQ.
- **Truth-in-labeling:** Host timing is informational and not GPU superiority.
- **Risks:** `FFTManager` may have noncanonical consumers.
- **Rollback:** Revert deletion only if a real active consumer is demonstrated;
  do not restore misleading labels.
- **Status:** CI-safe; Metal execution device-dependent.

---

## Required Later SAR Baseline Sequence

This phase starts only after PR15 and PR16 are stable. Existing local SAR tools
are inputs to these PRs and must be replaced or consolidated in place. Maintain
exactly one named SAR GPU path throughout.

## PR19: External SAR Baseline Survey

- **Purpose:** Establish comparison candidates without integrating one.
- **Scope:** Survey license, install complexity, CRSD/GOTCHA support, output
  compatibility, determinism, algorithm boundary, and CI/local feasibility.
- **Files likely to touch:** `plan/BASELINE.md`, one compact survey under
  `plan/reviews/`, policy registry tests.
- **Files likely to delete:** Stale duplicate surveys or candidate claims.
- **Tests to add:** Guardrail that no external package enters build/runtime/CI.
- **Tests to update/delete:** Update policy registry expectations.
- **Acceptance criteria:** Candidates are comparable; no package selected or
  imported.
- **Truth-in-labeling:** Planning-only survey.
- **Risks:** License/support facts can age.
- **Rollback:** Revert survey and guardrail only.
- **Status:** CI-safe documentation/policy PR.

## PR20: Select One SAR Baseline Package

- **Purpose:** Choose one package and one comparison boundary.
- **Scope:** Record selection rationale, version policy, license result, input
  format, output artifact, and the SAR stage being compared. Current SarPy
  selection must be revalidated rather than assumed.
- **Files likely to touch:** Baseline policy/registry, `plan/BASELINE.md`, local
  tool metadata/tests.
- **Files likely to delete:** Configuration/support claims for unselected
  candidates.
- **Tests to add:** Exactly-one-selected-package and no-runtime-dependency
  guardrails.
- **Tests to update/delete:** Remove multi-candidate execution expectations.
- **Acceptance criteria:** One package and boundary selected; default build and
  CI remain dependency-free.
- **Truth-in-labeling:** Selection is for validation, not production endorsement.
- **Risks:** License or reproducibility can block selection.
- **Rollback:** Return to survey-only state; do not retain partial integration.
- **Status:** CI-safe policy PR.

## PR21: Consolidate One Local-Only Baseline Runner

- **Purpose:** Provide one opt-in execution entry point for the selected package.
- **Scope:** Consolidate the existing local runner around explicit environment
  gates, pinned invocation metadata, input validation, deterministic skip/error
  output, and one artifact contract.
- **Files likely to touch:** `examples/SAR/tools/sar_local_baseline_runner.py`,
  local tool docs/schema/tests.
- **Files likely to delete:** Duplicate runners, package-specific alternate
  entry points, stale options.
- **Tests to add:** Probe, disabled skip, missing package/data, malformed input,
  invocation contract, deterministic artifact metadata.
- **Tests to update/delete:** Consolidate runner tests; delete duplicate paths.
- **Acceptance criteria:** Default CI never invokes/imports the package; explicit
  opt-in runs one runner and produces one artifact schema.
- **Truth-in-labeling:** Local-only reference runner, not GraphX runtime.
- **Risks:** External package CLI/API drift.
- **Rollback:** Revert runner consolidation; retain selection policy.
- **Status:** CI-safe contract tests; execution local-only.

## PR22: Consolidate GraphX-vs-Baseline Output Comparison

- **Purpose:** Compare artifacts without coupling runtimes.
- **Scope:** Normalize GraphX and baseline outputs at the selected boundary;
  emit deterministic dimensions, alignment, peak, error, and metadata metrics.
- **Files likely to touch:**
  `examples/SAR/tools/sar_graphx_vs_baseline_harness.py`, comparison schema,
  fixtures/tests/docs.
- **Files likely to delete:** Duplicate comparison scripts/schemas and
  package-specific logic outside adapters.
- **Tests to add:** Identical, bounded mismatch, dimension mismatch, metadata
  mismatch, deterministic report, missing artifact.
- **Tests to update/delete:** Consolidate existing harness/comparator tests.
- **Acceptance criteria:** File/artifact boundary only; stable pass/fail reasons;
  no package dependency in comparator.
- **Truth-in-labeling:** Metrics validate artifacts, not production image quality.
- **Risks:** Algorithm conventions may differ despite correct outputs.
- **Rollback:** Revert harness/schema together; runner remains local-only.
- **Status:** CI-safe comparator; external execution local-only.

## PR23: Add Tiny Deterministic Fixture Comparison

- **Purpose:** Exercise the comparison contract without restricted data.
- **Scope:** Use one tiny synthetic/derived-in-repo input to produce GraphX and
  deterministic reference artifacts at the selected boundary.
- **Files likely to touch:** Tiny SAR fixtures, comparison harness/tests,
  provenance metadata, CMake/CTest registration.
- **Files likely to delete:** Redundant tiny fixtures and alternate expected
  artifact formats.
- **Tests to add:** Repeated-byte/hash determinism, metadata lineage, strict
  comparison, intentional failure diagnostics.
- **Tests to update/delete:** Point existing CI comparison tests to the one
  fixture contract.
- **Acceptance criteria:** Runs without network/external package/dataset; stable
  artifacts and report.
- **Truth-in-labeling:** Tiny deterministic correctness fixture, not GOTCHA or
  real-data acceptance.
- **Risks:** Fixture may be too simple to expose algorithm differences.
- **Rollback:** Remove fixture lane without changing local comparison.
- **Status:** CI-safe.

## PR24: Add a CI-Safe Derived Fixture If Licensing Permits

- **Purpose:** Improve realism while retaining redistributable deterministic CI.
- **Scope:** Complete license/provenance review; add only the minimal derived
  data and generation record allowed; otherwise close the PR as no-go with no
  data committed.
- **Files likely to touch:** `examples/SAR/test/fixtures/`, provenance/license
  metadata, derived-fixture generator/validator, tests/docs.
- **Files likely to delete:** Unlicensed, ambiguous, or superseded derived data.
- **Tests to add:** Provenance fields, hashes, regeneration/validation, strict
  comparison.
- **Tests to update/delete:** Register the derived lane only after approval.
- **Acceptance criteria:** Written redistribution basis and deterministic
  artifact; otherwise no fixture enters the repository.
- **Truth-in-labeling:** Derived fixture is not the source dataset and not broad
  real-data validation.
- **Risks:** Licensing blocks the PR; repository size.
- **Rollback:** Delete fixture, metadata, and tests as one unit.
- **Status:** CI-safe only if licensed; otherwise no-go.

## PR25: Add Optional Local GOTCHA/OpenSAR Benchmark

- **Purpose:** Measure local real-data behavior without changing CI/runtime
  dependencies.
- **Scope:** One opt-in benchmark driver using existing converter/runner and
  PR16 instrumentation; dataset paths and package execution remain external.
- **Files likely to touch:** `examples/SAR/tools/`, benchmark profile/schema,
  README/local tests.
- **Files likely to delete:** Duplicate dataset-specific benchmark scripts and
  hard-coded paths.
- **Tests to add:** Disabled skip, missing dataset, profile validation, dry-run
  invocation, deterministic report schema.
- **Tests to update/delete:** Consolidate GOTCHA/OpenSAR local gates.
- **Acceptance criteria:** Default CI skips deterministically; local report
  separates I/O, algorithm, graph, transfer, and comparison costs.
- **Truth-in-labeling:** Current-host optional benchmark, not a general
  performance or production claim.
- **Risks:** Dataset availability, format drift, runtime cost.
- **Rollback:** Remove the optional driver/profile; keep CI fixture lanes.
- **Status:** Local-only, with CI-safe contract tests.

## PR26: Add One SAR Stage-Substitution Experiment

- **Purpose:** Test GraphX as a replacement for one selected baseline stage.
- **Scope:** Consolidate the existing substitution experiment around the PR20
  boundary; exchange files/artifacts only; compare downstream output with PR22.
- **Files likely to touch:**
  `examples/SAR/tools/sar_baseline_substitution_experiment.py`, selected adapter,
  schemas/tests/docs.
- **Files likely to delete:** Alternate substitution boundaries and duplicate
  experiment modes.
- **Tests to add:** Disabled skip, invocation/artifact contract, stage identity,
  downstream comparison, deterministic failure reasons.
- **Tests to update/delete:** Consolidate existing substitution tests around one
  package and one boundary.
- **Acceptance criteria:** One opt-in experiment; GraphX replaces exactly one
  named stage; no external package enters GraphX runtime; no second SAR GPU
  path.
- **Truth-in-labeling:** Local validation experiment, not supported package
  integration or production substitution.
- **Risks:** Baseline internals may not expose a stable substitution seam.
- **Rollback:** Remove experiment/adapter only; runner and comparison harness
  remain independently usable.
- **Status:** Local-only, with CI-safe contract tests.
