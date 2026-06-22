# GraphX Cleanup PR Roadmap

Date: 2026-06-22

Inputs:

- `plan/reviews/GRAPHX_INSPECTOR_REPORT.md`
- `plan/reviews/GRAPHX_SIMPLIFIER_REPORT.m`
- `plan/agents/GRAPHX_AGENT_ROLES.md`

Rules:

- Every PR must compile independently.
- Every PR must add, update, or delete tests with the change.
- Backward compatibility shims are not allowed.
- Complexity is a defect.
- Prefer deletion over compatibility.
- Do not maintain dual canonical paths.

## Planner Addendum: SAR Baseline Sequence

After GraphX SAR token architecture and basic performance instrumentation are stable, prefer this order:

1. External SAR baseline survey.
2. Select one baseline package.
3. Add local-only baseline runner script.
4. Add GraphX-vs-baseline output comparison harness.
5. Add tiny deterministic fixture comparison.
6. Add CI-safe derived fixture if licensing permits.
7. Add optional local GOTCHA/OpenSAR benchmark.
8. Add substitution experiment where GraphX replaces the baseline SAR stage in a selected test.

Maintain exactly one canonical SAR GPU path. Other SAR GPU paths must be removed, marked local-only, or marked experimental.

---

## PR1: Baseline Architecture Guardrails

Purpose:

- Convert the simplifier invariants into tests before deleting code.

Scope:

- Add structural guardrails for canonical docs, real GraphX node naming, token-wrapped accelerator edges, FHSS channelizer output count, and truth-in-labeling phrases.
- Keep guardrails focused on active paths, not archived history.

Files likely to touch:

- `libgraph/test/`
- `libdsp/test/`
- `examples/SAR/test/`
- `README.md`
- `plan/BASELINE.md`

Files likely to delete:

- None.

Tests to add:

- Active-doc guardrail test.
- Public `...Node` real-GraphX-node guardrail.
- FHSS canonical graph guardrail.
- SAR canonical GPU-path label guardrail.

Tests to update or delete:

- Update brittle string checks only where they conflict with the new canonical language.

Acceptance criteria:

- Tests fail if active docs name deleted or noncanonical graph paths as current.
- Tests fail if public pseudo-node names return in active headers.
- Tests fail if FHSS channelizer is not represented as one output port per configured frequency.

Truth-in-labeling requirements:

- CPU-only, Metal experimental, reference-only, local-only, fixture-only, and unsupported states remain explicit.

Risks:

- String guardrails can be brittle.

Rollback plan:

- Revert only the new guardrail tests and doc wording updates.

CI-safe or local-only status:

- CI-safe.

---

## PR2: Remove FHSS Pulse Merge Duplicate Node

Purpose:

- Keep one canonical FHSS pulse merge implementation.

Scope:

- Delete public `FHSSPulseMergeInteriorNode`.
- Keep `FHSSPulseMergeNode` as the canonical routed/fixed fan-in implementation.
- Update tests to exercise only `FHSSPulseMergeNode` through GraphX APIs.

Files likely to touch:

- `libdsp/include/dsp/fhss/`
- `libdsp/src/dsp/`
- `libdsp/test/`
- `libdsp/plugins/`

Files likely to delete:

- `FHSSPulseMergeInteriorNode` header/source.
- Direct tests of `FHSSPulseMergeInteriorNode`.

Tests to add:

- Compile-time port/type contract test for `FHSSPulseMergeNode`.
- Runtime merge behavior test through GraphX node API.

Tests to update or delete:

- Delete duplicate interior-node tests.

Acceptance criteria:

- Only one public FHSS pulse merge node remains.
- Merge behavior, duplicate rejection, collision reporting, and global-time ordering remain covered.

Truth-in-labeling requirements:

- No compatibility alias claims the deleted node is supported.

Risks:

- Plugin registration may still reference the deleted class.

Rollback plan:

- Restore deleted files and registrations only if the canonical node cannot cover existing behavior.

CI-safe or local-only status:

- CI-safe.

---

## PR3: Remove FHSS Correlator-Bank Canonical Surface

Purpose:

- Eliminate the noncanonical FHSS receiver topology from active support.

Scope:

- Remove the correlator-bank graph from canonical configs.
- Delete `FHSSCorrelatorBankDetectorNode` if no current CI-safe test requires it.
- If retained for one transition PR, mark it reference-only and local-only in code, config, docs, and tests.

Files likely to touch:

- `libdsp/include/dsp/fhss/`
- `libdsp/src/dsp/`
- `libdsp/config/`
- `libdsp/plugins/`
- `libdsp/test/`
- `examples/DSP/`
- `README.md`
- `plan/BASELINE.md`

Files likely to delete:

- `FHSSCorrelatorBankDetectorNode` header/source.
- Correlator-bank plugin target.
- Correlator-bank config if not retained as reference-only.
- Correlator-bank-only tests.

Tests to add:

- Guardrail proving the channelized graph is the only canonical FHSS graph.

Tests to update or delete:

- Delete tests that require correlator-bank behavior as active support.

Acceptance criteria:

- Channelized graph remains green.
- No active doc/config labels correlator-bank as canonical or production-like.

Truth-in-labeling requirements:

- FHSS remains deterministic CPU fixture, not production RF.

Risks:

- Examples may still default to the older config.

Rollback plan:

- Restore correlator-bank files as reference-only only, not canonical.

CI-safe or local-only status:

- CI-safe.

---

## PR4: Normalize Repeated-Port GraphX Helpers

Purpose:

- Reduce repeated-port boilerplate without changing domain behavior.

Scope:

- Move reusable routed input/output/transfer support into stable GraphX core headers.
- Add or refine fixed fan-in/fan-out helper coverage for large fixed-port nodes.
- Do not rewrite unrelated nodes in this PR.

Files likely to touch:

- `libgraph/include/graph/core/`
- `libgraph/src/`
- `libgraph/test/`
- `libdsp/test/`

Files likely to delete:

- Any now-unused duplicated routed helper declarations.

Tests to add:

- Compile-time routed input/output/transfer tests.
- Fixed fan-in/fan-out node smoke test.
- Transfer-with-no-output test documenting that no queued output is valid behavior.

Tests to update or delete:

- Update tests that duplicate helper behavior locally.

Acceptance criteria:

- Existing FHSS pulse merge behavior still compiles through shared helpers.
- Helper docs/tests capture repeated-port semantics.

Truth-in-labeling requirements:

- Helpers are generic GraphX infrastructure, not FHSS-specific runtime behavior.

Risks:

- Template diagnostics may become harder to read.

Rollback plan:

- Revert helper changes and keep existing specialized implementation.

CI-safe or local-only status:

- CI-safe.

---

## PR5: Simplify Channelizer Port Implementation

Purpose:

- Keep the 64-port invariant while reducing specialized channelizer type-list code.

Scope:

- Either keep the current generated 64-port type list and document it as the canonical implementation, or replace it with the generalized fixed fan-out helper from PR4.
- Do not change channelizer DSP behavior.

Files likely to touch:

- `libdsp/include/dsp/fhss/ChannelizerNode.hpp`
- `libdsp/src/dsp/ChannelizerNode.cpp`
- `libdsp/test/`
- `libgraph/test/` if new helper coverage is needed

Files likely to delete:

- Any channelizer-specific repeated-port boilerplate made redundant.

Tests to add:

- Compile-time tests for exactly 64 output ports.
- Representative port type tests for ports 0, 1, 62, and 63.
- Runtime mapping test: output port `N` emits channel id `N` and frequency index `N`.

Tests to update or delete:

- Delete any test accepting aggregate channelized stream output.

Acceptance criteria:

- `ChannelizerNode` exposes exactly 64 GraphX output ports.
- Every output remains `ControlToken<FHSSChannelizedIqPacket>`.

Truth-in-labeling requirements:

- No production channelizer separation claim is added.

Risks:

- Rewriting the base class may affect plugin metadata or JSON port names.

Rollback plan:

- Restore the previous generated type-list implementation while keeping guardrails.

CI-safe or local-only status:

- CI-safe.

---

## PR6: Remove Aggregate Channelizer Contracts And Guards

Purpose:

- Ensure the one-channel-per-frequency invariant cannot regress.

Scope:

- Delete any aggregate channelized stream packet, token alias, fanout sidecar, or single-edge channelizer contract.
- Add negative guardrails preventing aggregate channelizer outputs from returning as canonical port types.

Files likely to touch:

- `libdsp/include/dsp/fhss/FHSSGraphXPackets.hpp`
- `libdsp/include/dsp/fhss/FHSSGraphXNodeUtils.hpp`
- `libdsp/test/`
- `plan/BASELINE.md`

Files likely to delete:

- Aggregate channelized stream packet aliases or tests, if present.

Tests to add:

- Compile-time negative/absence guardrail where repository conventions allow.
- Config guardrail proving channelizer has 64 distinct outputs.

Tests to update or delete:

- Delete aggregate-output tests.

Acceptance criteria:

- No canonical channelizer output type carries a vector/list of all channels.

Truth-in-labeling requirements:

- Receiver channel count equals configured frequency count.

Risks:

- Absence tests can be fragile if implemented as text scans.

Rollback plan:

- Revert only the negative guardrail if it blocks unrelated refactors.

CI-safe or local-only status:

- CI-safe.

---

## PR7: SAR Config Set Consolidation

Purpose:

- Reduce SAR graph/config sprawl to a small named active set.

Scope:

- Identify and keep the canonical CI-safe SAR CPU config.
- Identify exactly one canonical SAR GPU path, or mark the GPU path experimental if not stable.
- Keep one local-only GOTCHA/CRSD validation path.
- Keep one benchmark config if still useful.
- Delete stale duplicate configs and update references.

Files likely to touch:

- `examples/SAR/config/`
- `examples/SAR/test/`
- `examples/SAR/src/`
- `README.md`
- `plan/BASELINE.md`

Files likely to delete:

- Redundant SAR JSON configs.
- Tests that only preserve deleted duplicate configs.

Tests to add:

- Config catalog guardrail.
- Test proving exactly one canonical SAR GPU path is named.
- Local-only marker test for GOTCHA/OpenSAR style configs.

Tests to update or delete:

- Update SAR executor tests to use selected configs.
- Delete tests tied to removed configs.

Acceptance criteria:

- Active SAR config list is small, named, and documented.
- Local-only configs cannot be mistaken for CI-required configs.

Truth-in-labeling requirements:

- Experimental Metal SAR behavior remains explicitly labeled until complete.

Risks:

- Deleted config names may be used by scripts.

Rollback plan:

- Restore only the config required by a still-supported script, then label it local-only.

CI-safe or local-only status:

- CI-safe for config catalog; local-only configs remain local-only.

---

## PR8: Documentation Surface Reduction

Purpose:

- Make the active documentation set match the consolidated baseline.

Scope:

- Keep active user documentation in `README.md`.
- Keep active architecture in `plan/BASELINE.md`.
- Keep active roles in `plan/agents/GRAPHX_AGENT_ROLES.md`.
- Keep current reports and roadmaps in `plan/reviews` and `plan/roadmap`.
- Archive or delete duplicate active docs.

Files likely to touch:

- `README.md`
- `plan/BASELINE.md`
- `doc/`
- `docs/archive/`
- `plan/archive/`
- `plan/reviews/`
- `plan/roadmap/`

Files likely to delete:

- Active duplicate docs outside the selected active documentation set.

Tests to add:

- Documentation index guardrail.
- No-active-reference-to-archived-roadmap guardrail.

Tests to update or delete:

- Update doc string guardrails to point at the new active docs.

Acceptance criteria:

- Active docs are findable from top-level README.
- Historical docs are clearly archived or removed.
- No active doc claims deleted paths are current.

Truth-in-labeling requirements:

- Docs describe current behavior, not intended future behavior.

Risks:

- Some historical notes may still be useful during planning.

Rollback plan:

- Restore historical notes only under archive, not active docs.

CI-safe or local-only status:

- CI-safe.

---

## PR9: Placeholder And Editor Artifact Cleanup

Purpose:

- Delete obvious dead surface area and local artifacts.

Scope:

- Remove editor artifacts.
- Classify placeholder runtime/plugin surfaces as either supported extension points or dead code.
- Delete dead placeholder-only code.
- Keep supported extension points only if tests document their behavior.

Files likely to touch:

- `libgraph/src/`
- `libgraph/include/`
- `libgpu/include/`
- `libgpu/src/`
- `libgraph/test/`
- `libgpu/test/`

Files likely to delete:

- `.swp` artifact under vendored or third-party include tree.
- Placeholder-only functions with no supported behavior.

Tests to add:

- Tests for any retained extension point behavior.
- Repository hygiene guardrail for editor artifacts.

Tests to update or delete:

- Delete tests that only exercise placeholder no-op behavior.

Acceptance criteria:

- No editor artifacts remain in source tree.
- Placeholder surfaces either have tested behavior or are gone.

Truth-in-labeling requirements:

- Unsupported plugin/runtime features report unsupported status or are absent.

Risks:

- Some placeholder functions may be part of public headers.

Rollback plan:

- Restore only the minimum public declaration needed to compile, without compatibility shim behavior.

CI-safe or local-only status:

- CI-safe.

---

## PR10: Accelerator Token Contract Hardening

Purpose:

- Make token sidecar rules structural instead of convention-only.

Scope:

- Add C++26 concepts/traits for token-wrapped GraphX edge contracts.
- Add compile-time tests for DSP, FHSS, SAR, and GPU node port contracts.
- Keep GraphX core domain-neutral.

Files likely to touch:

- `libgpu/include/gpu/accel/types/`
- `libgraph/include/graph/`
- `libdsp/test/`
- `examples/SAR/test/`
- `libgpu/test/`

Files likely to delete:

- Duplicate local static assertions replaced by shared traits, if any.

Tests to add:

- Token sidecar concept tests.
- Port contract tests for representative DSP, FHSS, SAR, and GPU nodes.
- Transport-opacity tests for `host_ptr` and `ready_event`.

Tests to update or delete:

- Replace duplicated ad hoc type checks where the shared trait covers them.

Acceptance criteria:

- Accelerator-ready ports prove `ControlToken<PacketT>` use at compile time.
- Domain identity fields remain in packet sidecars.

Truth-in-labeling requirements:

- Token-ready does not imply GPU execution.

Risks:

- Concepts may increase compile-time diagnostics noise.

Rollback plan:

- Revert traits and keep existing explicit static assertions.

CI-safe or local-only status:

- CI-safe.

---

## PR11: Deterministic Diagnostics And Metrics Baseline

Purpose:

- Stabilize metrics before performance work or external comparisons.

Scope:

- Define minimum deterministic diagnostics for GraphX executor, DSP spectrum, FHSS, SAR, and GPU lanes.
- Keep metrics lightweight and CI-safe.
- Do not optimize performance in this PR.

Files likely to touch:

- `libgraph/`
- `libdsp/`
- `examples/SAR/`
- `libgpu/`
- `README.md`
- `plan/BASELINE.md`

Files likely to delete:

- Duplicate ad hoc diagnostics fields that conflict with the baseline schema.

Tests to add:

- Diagnostics schema tests.
- Executor metrics smoke tests.
- FHSS demo metrics JSON test.
- SAR fixture metrics JSON test.

Tests to update or delete:

- Update examples that emit metrics with unstable field names.

Acceptance criteria:

- Core lanes emit deterministic diagnostic keys.
- CI tests validate presence and meaning of baseline fields.

Truth-in-labeling requirements:

- Metrics are instrumentation, not performance claims.

Risks:

- Existing example output may change.

Rollback plan:

- Revert field renames while keeping schema tests disabled only if necessary.

CI-safe or local-only status:

- CI-safe.

---

## PR12: SAR Token Architecture Stability Pass

Purpose:

- Ensure SAR is ready for external baseline comparison without changing algorithms.

Scope:

- Audit SAR node edges for `SarAccelControlToken` preservation.
- Confirm `host_ptr` and `ready_event` are transport-only.
- Confirm CRSD/GOTCHA metadata stays in SAR-domain packets.
- Confirm exactly one canonical SAR GPU path is named.

Files likely to touch:

- `examples/SAR/include/`
- `examples/SAR/src/`
- `examples/SAR/test/`
- `examples/SAR/config/`
- `README.md`
- `plan/BASELINE.md`

Files likely to delete:

- Duplicate SAR token helpers if shared helpers cover them.

Tests to add:

- SAR sidecar preservation tests.
- SAR transport-opacity tests.
- SAR canonical GPU-path guardrail.

Tests to update or delete:

- Update tests that use transport fields as identity.

Acceptance criteria:

- SAR identity is fully sidecar-based.
- SAR GPU path status is unambiguous.

Truth-in-labeling requirements:

- Experimental or incomplete Metal SAR behavior remains labeled.

Risks:

- Some tests may implicitly depend on transport fields.

Rollback plan:

- Revert only identity-field test changes; do not reclassify transport as identity.

CI-safe or local-only status:

- CI-safe.

---

## PR13: External SAR Baseline Survey

Purpose:

- Document candidate SAR baseline packages before integrating any external tool.

Scope:

- Survey candidate baseline packages and compare license, install complexity, data support, output format, determinism, and local/CI feasibility.
- No dependency is added in this PR.

Files likely to touch:

- `plan/reviews/`
- `plan/BASELINE.md`

Files likely to delete:

- None.

Tests to add:

- None required unless adding doc guardrails for local-only claims.

Tests to update or delete:

- None.

Acceptance criteria:

- One recommendation is selected or a clear deferral is recorded.
- CI-safe versus local-only implications are explicit.

Truth-in-labeling requirements:

- Survey does not imply package support exists.

Risks:

- External package status may change over time.

Rollback plan:

- Replace survey report with updated findings.

CI-safe or local-only status:

- Planning/doc-only; CI-safe.

---

## PR14: Local-Only SAR Baseline Runner

Purpose:

- Add a local-only runner for the selected external SAR baseline.

Scope:

- Add a script that invokes the selected package when locally installed.
- Gate it behind explicit local-only environment/config flags.
- Do not add external packages to normal CI.

Files likely to touch:

- `examples/SAR/tools/`
- `examples/SAR/test/`
- `README.md`
- `plan/BASELINE.md`

Files likely to delete:

- Older local baseline scripts for unselected packages, if any.

Tests to add:

- CI-safe skip test when dependency/data is absent.
- Local-only smoke test path when explicitly enabled.

Tests to update or delete:

- Remove unselected baseline runner tests.

Acceptance criteria:

- CI passes without external dependency.
- Local runner gives clear missing-dependency diagnostics.

Truth-in-labeling requirements:

- Runner is local-only and not a GraphX runtime dependency.

Risks:

- External install instructions may drift.

Rollback plan:

- Delete runner and retain survey only.

CI-safe or local-only status:

- Runner is local-only; skip behavior is CI-safe.

---

## PR15: GraphX-Vs-Baseline SAR Comparison Harness

Purpose:

- Compare GraphX SAR output to one selected local baseline output.

Scope:

- Add artifact comparison harness for image/metadata metrics.
- Support tiny deterministic fixture first.
- Do not substitute GraphX into the external baseline pipeline yet.

Files likely to touch:

- `examples/SAR/tools/`
- `examples/SAR/test/`
- `README.md`
- `plan/BASELINE.md`

Files likely to delete:

- Duplicate comparison scripts for unselected packages.

Tests to add:

- Tiny deterministic fixture comparison.
- CI-safe derived fixture only if licensing permits.
- Local-only GOTCHA/OpenSAR comparison if enabled.

Tests to update or delete:

- Delete older comparison tests tied to unselected baselines.

Acceptance criteria:

- Harness reports deterministic comparison metrics.
- CI-safe fixture does not require restricted datasets.

Truth-in-labeling requirements:

- Comparison metrics are validation aids, not production SAR claims.

Risks:

- Baseline numerical output may differ by package version.

Rollback plan:

- Keep fixture outputs and delete only baseline invocation.

CI-safe or local-only status:

- Tiny fixture CI-safe; GOTCHA/OpenSAR local-only.

---

## PR16: Optional SAR Baseline Substitution Experiment

Purpose:

- Test one controlled substitution where GraphX replaces a stage in the selected SAR baseline flow.

Scope:

- Use the previously selected package and comparison harness.
- Keep the experiment local-only unless all data and dependencies are CI-safe.
- Do not create a second canonical SAR GPU path.

Files likely to touch:

- `examples/SAR/tools/`
- `examples/SAR/test/`
- `README.md`
- `plan/BASELINE.md`

Files likely to delete:

- Experimental substitution paths that are not the selected one.

Tests to add:

- Local-only substitution smoke test.
- Comparison harness regression test.

Tests to update or delete:

- None unless older experiments exist.

Acceptance criteria:

- One substitution experiment runs locally and reports comparison metrics.
- Canonical SAR GPU path remains singular and clearly labeled.

Truth-in-labeling requirements:

- The experiment is not a production SAR claim.

Risks:

- External baseline internals may not expose a clean substitution boundary.

Rollback plan:

- Delete substitution experiment and retain comparison harness.

CI-safe or local-only status:

- Local-only by default.

---

## PR17: Cleanup Roadmap Closure And Baseline Refresh

Purpose:

- Fold completed cleanup decisions into the active baseline.

Scope:

- Update `plan/BASELINE.md` and `README.md` with the final canonical paths.
- Remove stale roadmap statements that were completed or rejected.
- Keep historical reports archived or current according to the selected docs policy.

Files likely to touch:

- `README.md`
- `plan/BASELINE.md`
- `plan/roadmap/`
- `plan/reviews/`
- `docs/archive/`

Files likely to delete:

- Superseded cleanup planning notes once archived.

Tests to add:

- Baseline consistency guardrail.
- Active roadmap index guardrail if roadmap files remain active.

Tests to update or delete:

- Update doc guardrails with final canonical names.

Acceptance criteria:

- New contributors can identify active architecture, active configs, active demos, and local-only workflows from top-level docs.

Truth-in-labeling requirements:

- No future work is described as implemented.

Risks:

- Prematurely archiving useful planning detail.

Rollback plan:

- Restore archived notes under archive only.

CI-safe or local-only status:

- CI-safe.
