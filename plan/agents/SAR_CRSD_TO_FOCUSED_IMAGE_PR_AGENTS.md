# SAR CRSD To Focused Image PR Agents

Use these prompts with `plan/agents/GRAPHX_SAR_AGENT_ROLES.md` and
`plan/reviews/SAR_CRSD_TO_FOCUSED_IMAGE_PLANNER_REPORT.md`.

Global constraints for every PR:

- Implement or verify exactly the named PR.
- Preserve the corrected CRSD model: generated CRSD files are one logical GOTCHA image split across an ordered set of CRSD segment products, not independent final images.
- The focused-image lane must form one focused SAR image from the ordered CRSD set.
- `product.crsd` is authoritative for CRSD metadata, signal, and PVP. JSON sidecars are optional preflight, provenance, checksum, and report evidence only.
- Do not add MATLAB as a build, runtime, or test dependency.
- Do not require real GOTCHA data or SarPy in CI.
- Do not alter GraphX runtime contracts unless the PR explicitly requires a local SAR node/message addition.
- Keep local-only workflows opt-in and gated.
- Stop after the requested implementer or verifier report.

---

## PR1 Implementer

```text
Act as IMPLEMENTER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Implement exactly PR1 from plan/reviews/SAR_CRSD_TO_FOCUSED_IMAGE_PLANNER_REPORT.md: Repository discovery for CRSD source-node and focused-image output patterns.

Scope:
- Create docs/sar/crsd_focused_image_repo_discovery.md.
- Inspect the current repository and document SAR node/plugin/config/test conventions.
- Map CSV injection patterns versus SAR source-node patterns.
- Justify the JSON-configured OrderedCrsdSetInputSourceNode-first approach.
- Document existing SAR plugin registration and fixture/test conventions.
- Document CRSD writer/validator/reference tool hooks and local-only SarPy boundaries.
- Explicitly state that the generated CRSD products are one ordered aperture set for one final focused image, not one image per CRSD file.
- Explicitly state that MATLAB is not used and must not become a dependency.

Do not implement code.
Do not add tests.
Do not modify runtime configs except a narrow planner link/reference update if needed.

Output the standard IMPLEMENTER summary.
Save the report to plan/reviews/SAR_CRSD_TO_FOCUSED_IMAGE_IMPL_PR1.md.
```

## PR1 Verifier

```text
Act as VERIFIER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Verify exactly PR1 from plan/reviews/SAR_CRSD_TO_FOCUSED_IMAGE_PLANNER_REPORT.md.

Required checks:
- docs/sar/crsd_focused_image_repo_discovery.md exists.
- Discovery maps CSV injection versus SAR source-node patterns.
- Discovery justifies JSON-configured OrderedCrsdSetInputSourceNode-first approach.
- Discovery enumerates SAR plugin registration and fixture/test conventions.
- Discovery records CRSD writer/validator/reference hooks and local-only SarPy boundaries.
- Discovery states generated CRSD products are one ordered aperture set for one final focused image.
- Discovery states MATLAB is not used.
- No implementation code, tests, dependencies, or runtime behavior were added.

Stop after verifier report.
Save the report to plan/reviews/SAR_CRSD_TO_FOCUSED_IMAGE_VERIFY_PR1.md.
```

---

## PR2 Implementer

```text
Act as IMPLEMENTER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Implement exactly PR2 from plan/reviews/SAR_CRSD_TO_FOCUSED_IMAGE_PLANNER_REPORT.md: Ordered CRSD set reader/source-node interface and tiny fixture strategy.

Scope:
- Add OrderedCrsdSetInputSourceNode and a narrow C++ CrsdReader interface/implementation in the planned locations.
- Add plugin registration and CMake wiring for the source node.
- Add config examples for ordered CRSD paths, CRSD directory, and manifest modes.
- Add deterministic tiny multi-segment CRSD fixture strategy and tests.
- Node must read CRSD metadata, signal, and required PVP subset from each product.crsd through C++ reader interfaces.
- Node must emit one ordered aperture-set stream intended to produce one focused image.
- Add focused CRSD input-node contract tests for segment order, per-segment shape, total vector count, payload checksum/hash, first/last vector PVP, first/last geometry metadata, duplicate/missing/out-of-order diagnostics, and unsupported CRSD diagnostics.
- Treat metadata.json, pvp.json, chunk_index.json, provenance.json, and SarPy validation JSON as optional sanity/provenance inputs only.

Do not implement aperture assembly, focused image formation, Metal execution, sinks, SarPy reference generation, or local real-data workflow.
Do not require real GOTCHA data or SarPy in CI.
Do not add MATLAB.

Output the standard IMPLEMENTER summary.
Save the report to plan/reviews/SAR_CRSD_TO_FOCUSED_IMAGE_IMPL_PR2.md.
```

## PR2 Verifier

```text
Act as VERIFIER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Verify exactly PR2 from plan/reviews/SAR_CRSD_TO_FOCUSED_IMAGE_PLANNER_REPORT.md.

Required checks:
- OrderedCrsdSetInputSourceNode accepts ordered CRSD paths, CRSD directory, and manifest via node_config.
- CrsdReader reads CRSD metadata, signal, and required PVP subset from product.crsd.
- Tests prove deterministic segment order, per-segment shape, total vector count, payload checksums, first/last PVP, and first/last geometry metadata.
- Tests prove sidecar JSON files are optional and not authoritative for signal/PVP.
- Duplicate, missing, out-of-order, and unsupported-field cases produce deterministic diagnostics.
- Node emits one ordered aperture-set stream, not one focused image per segment.
- Plugin load and JSON topology smoke tests exist.
- No aperture assembly, focused image transform, Metal, sink, SarPy reference lane, real-data workflow, MATLAB, or dependency work was added.

Stop after verifier report.
Save the report to plan/reviews/SAR_CRSD_TO_FOCUSED_IMAGE_VERIFY_PR2.md.
```

---

## PR2b Implementer

```text
Act as IMPLEMENTER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Implement corrective PR2b for plan/reviews/SAR_CRSD_TO_FOCUSED_IMAGE_PLANNER_REPORT.md: Binary CRSD product reader support for OrderedCrsdSetInputSourceNode.

Context:
- PR2 currently passes tiny JSON fixture-mode tests, but fails on real generated CRSD data.
- OrderedCrsdSetInputSourceNode directory mode pointed at data/crsd fails with a JSON parse error because CrsdReader.cpp expects graphx.sar.crsd.tiny.v1 JSON fixtures.
- tools/sarpy/validate_crsd.py opens at least one data/crsd product.crsd successfully with status=ok, so the failure is reader support, not dataset corruption.
- The principal test fixture for this corrective PR must be generated binary CRSD product files, not JSON pretending to be CRSD.

Goal:
Make the PR2 CRSD input reader/source-node implementation read binary CRSD product files, including generated GOTCHA-derived product.crsd files, through the C++ CRSD reader path used by OrderedCrsdSetInputSourceNode.

Scope:
- Update examples/SAR/src/io/CrsdReader.cpp and related interfaces/tests so CrsdReader detects and reads binary CRSD product files instead of trying to parse every .crsd as JSON.
- Keep any tiny JSON fixture support only if it remains explicitly named as a test helper format and cannot be confused with binary CRSD.
- Add or generate deterministic binary CRSD fixture products for CI tests. Prefer small generated CRSD product files built from the repository's CRSD writer/generator path so the fixture exercises the real binary container contract.
- Add tests proving CrsdReader can open binary product.crsd, read metadata, signal dimensions, required PVP subset, first/last vectors, geometry metadata, and stable signal checksums.
- Add OrderedCrsdSetInputSourceNode tests proving directory mode discovers real binary product.crsd files under product directories and emits one ordered aperture-set stream.
- Add manifest/path mode tests using binary product.crsd files.
- Add deterministic diagnostics for unsupported/non-CRSD files, malformed CRSD files, missing required PVP/metadata, missing product.crsd, duplicate segments, and unexpected ordering.
- Add a local-only optional smoke path for data/crsd that can be enabled when that directory exists, but do not require it in CI.
- Ensure product.crsd remains the authoritative source for metadata, signal, and PVP. Sidecar JSON files may be used only as optional preflight/provenance/checksum evidence.
- Update docs or reports only where needed to clarify that PR2 support is binary CRSD support, not JSON fixture support.

Requirements:
- The supported fixture for PR2b must include actual binary CRSD product files.
- The implementation must not pass by reading metadata.json, pvp.json, chunk_index.json, provenance.json, or SarPy validation JSON as substitutes for CRSD signal/PVP.
- Directory mode must be constrained to discover product.crsd files deterministically, for example under */product.crsd, and must ignore sidecars unless explicitly used for optional diagnostics.
- If full binary CRSD support cannot be completed, fail clearly and leave PR2b incomplete rather than silently treating JSON as CRSD.

Do not implement aperture assembly, focused image formation, Metal execution, sinks, SarPy reference generation, image comparison, or local real-data workflow beyond the optional reader smoke.
Do not add MATLAB.
Do not require SarPy in CI.
Do not check in real GOTCHA data or generated large CRSD files from data/crsd.
Do not change GraphX runtime contracts.

Suggested validation:
```bash
cmake --build build-ninja/ninja-debug-metal-native --target test_sar_example_unit graphx_sar_example

./build-ninja/ninja-debug-metal-native/examples/SAR/test/test_sar_example_unit \
  '--gtest_filter=*CrsdReader*:*OrderedCrsdSetInputSourceNode*'
```

Optional local validation when data/crsd exists:
```bash
./build-ninja/ninja-debug-metal-native/examples/SAR/graphx_sar_example \
  --config examples/SAR/config/sar_crsd_real_directory_input_smoke.json
```

Output the standard IMPLEMENTER summary.
Save the report to plan/reviews/SAR_CRSD_TO_FOCUSED_IMAGE_IMPL_PR2B.md.
```

## PR2b Verifier

```text
Act as VERIFIER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Verify corrective PR2b for plan/reviews/SAR_CRSD_TO_FOCUSED_IMAGE_PLANNER_REPORT.md: Binary CRSD product reader support for OrderedCrsdSetInputSourceNode.

Required checks:
- CrsdReader detects binary CRSD product files and no longer tries to parse product.crsd as JSON.
- CI tests use actual generated binary CRSD product files as the principal CRSD reader fixture.
- Any retained JSON tiny fixture support is explicitly labeled as a test helper and is not described as CRSD binary support.
- CrsdReader tests prove binary product.crsd metadata, signal dimensions, required PVP subset, first/last vectors, geometry metadata, and stable signal checksums are read from product.crsd.
- OrderedCrsdSetInputSourceNode path, manifest, and directory modes work with binary product.crsd files.
- Directory mode discovers product.crsd files deterministically and ignores sidecars as authoritative signal/PVP sources.
- Node emits one ordered aperture-set stream for all selected CRSD segments, not one image or independent product per segment.
- Tests prove duplicate, missing, malformed, unsupported, missing required metadata/PVP, and unexpected ordering cases produce deterministic diagnostics.
- Sidecar JSON files are optional evidence only; tests fail if metadata.json, pvp.json, chunk_index.json, provenance.json, or SarPy validation JSON are used as substitutes for CRSD signal/PVP.
- Optional local data/crsd smoke is gated and documented; CI does not require real GOTCHA data or SarPy.
- No aperture assembly, focused image transform, Metal lane, sink, SarPy reference generation, image comparison, MATLAB dependency, real GOTCHA data check-in, large generated CRSD check-in, or GraphX runtime contract change was added.

Suggested verification commands:
```bash
cmake --build build-ninja/ninja-debug-metal-native --target test_sar_example_unit graphx_sar_example

./build-ninja/ninja-debug-metal-native/examples/SAR/test/test_sar_example_unit \
  '--gtest_filter=*CrsdReader*:*OrderedCrsdSetInputSourceNode*'
```

Optional local verification when data/crsd exists:
```bash
./build-ninja/ninja-debug-metal-native/examples/SAR/graphx_sar_example \
  --config examples/SAR/config/sar_crsd_real_directory_input_smoke.json
```

Stop after verifier report.
Save the report to plan/reviews/SAR_CRSD_TO_FOCUSED_IMAGE_VERIFY_PR2B.md.
```

---

## PR3 Implementer

```text
Act as IMPLEMENTER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Implement exactly PR3 from plan/reviews/SAR_CRSD_TO_FOCUSED_IMAGE_PLANNER_REPORT.md: CRSD aperture assembly to SAR phase-history adapter/model.

Scope:
- Add CrsdApertureAssemblyAdapterNode, plugin registration, and planned SAR phase-history message/model additions.
- Bridge ordered CRSD segment ingest to a full-aperture SAR phase-history model for downstream focused-image transform.
- Define token/message contracts for CRSD samples, PVP fields, geometry, EOS/control markers, ownership, buffer layout, checksums, sidecar boundaries, vector index/channel/sample ordering, and SarAccelControlToken preservation.
- Add tests for segment ordering, full-aperture accounting, metadata/PVP mapping, EOS/control-marker propagation, payload ownership/layout, split/merge boundary checksums, and deterministic diagnostics for segment gaps/duplicates/unexpected ordering.
- Ensure total output vector count equals the sum of segment vectors.
- Ensure sample/channel/frequency consistency is enforced across segments.
- Ensure sidecar pulse ranges are optional cross-checks only.

Do not implement focused-image transform, Metal lane, sinks, SarPy reference generation, local real-data workflow, or MATLAB dependency.
Do not treat each CRSD segment as a separate final image.

Output the standard IMPLEMENTER summary.
Save the report to plan/reviews/SAR_CRSD_TO_FOCUSED_IMAGE_IMPL_PR3.md.
```

## PR3 Verifier

```text
Act as VERIFIER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Verify exactly PR3 from plan/reviews/SAR_CRSD_TO_FOCUSED_IMAGE_PLANNER_REPORT.md.

Required checks:
- CrsdApertureAssemblyAdapterNode emits assembled full-aperture SAR phase-history messages/tokens.
- Required geometry and sampling fields are present and validated.
- Total output vector count equals sum of segment vectors.
- Sample/channel/frequency consistency is enforced across segments.
- Tests cover segment ordering, full-aperture accounting, metadata/PVP mapping, EOS/control-marker propagation, ownership/layout, checksums, vector/channel/sample ordering, and SarAccelControlToken preservation.
- Tests fail if payload data is dropped, sidecars are used as physics inputs, or each segment is treated as a separate final image.
- Split/merge partition metadata contract is defined for PR4.
- No focused-image transform, Metal lane, sink, SarPy reference, local real-data workflow, or MATLAB dependency was added.

Stop after verifier report.
Save the report to plan/reviews/SAR_CRSD_TO_FOCUSED_IMAGE_VERIFY_PR3.md.
```

---

## PR3b Implementer

```text
Act as IMPLEMENTER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Implement corrective PR3b for plan/reviews/SAR_CRSD_TO_FOCUSED_IMAGE_PLANNER_REPORT.md: Complete CRSD aperture assembly adapter contract and negative-proof coverage.

Authoritative failure input:
- plan/reviews/SAR_CRSD_TO_FOCUSED_IMAGE_VERIFY_PR3.md

Context:
- PR3 introduced CrsdApertureAssemblyAdapterNode/model scaffolding and its focused unit suite passes.
- Verification failed because the adapter contract is incomplete and several negative-proof tests are missing.
- PR3b must fix PR3 verification failures without advancing into PR4 focused-image formation.

Goal:
Make PR3 satisfy the verifier requirements for a complete ordered-CRSD-set to full-aperture SAR phase-history adapter contract.

Scope:
- Update examples/SAR/include/sar/SarPhaseHistoryModel.hpp, examples/SAR/include/sar/io/CrsdReader.hpp, examples/SAR/src/io/CrsdReader.cpp, examples/SAR/include/sar/CrsdApertureAssemblyAdapterNode.hpp, examples/SAR/src/CrsdApertureAssemblyAdapterNode.cpp, and tests as needed.
- Add explicit geometry validation for required per-vector fields, including receive time, platform position, platform velocity, sample count, carrier frequency, sample rate, and any existing/mapped geometry fields needed by PR4.
- Add explicit full-aperture accounting validation that checks adapter output total_vector_count equals the computed sum of segment vector counts and equals the emitted vector payload count.
- Add channel consistency contract and fields where missing, such as channel identifier/count/order metadata in CrsdReader result and SarPhaseHistory model.
- Enforce sample, channel, and frequency consistency across segments with deterministic diagnostics.
- Add or complete split/merge partition metadata contract needed by PR4, including partition id/count, vector range start/count, stable ordering keys, input/output boundary hashes, and deterministic merge ordering semantics.
- Deepen metadata/PVP mapping tests to assert timing, geometry, sampling, frequency, channel, and vector index fields field-by-field at adapter output.
- Add ownership/layout tests proving phase-history payload ownership mode, sample format, rank/shape/stride or equivalent buffer layout metadata, and checksum invariants are explicit and validated.
- Add vector/channel/sample ordering tests proving per-vector sample order and channel order survive the adapter boundary.
- Add SarAccelControlToken preservation tests that assert sidecar/token identity, control marker, diagnostics, and relevant edge metadata survive adapter output, not just one marker field.
- Add negative-proof tests that fail when:
  - vector payload is empty, truncated, corrupted, or dropped
  - sidecar JSON/routing metadata is used as physics input instead of typed phase-history payload
  - per-segment finalization semantics are attempted instead of one EOS-driven full-aperture assembly
  - geometry fields are missing, NaN/Inf, non-finite, or inconsistent with vector count
  - channel metadata is missing, duplicated unexpectedly, or inconsistent across segments
  - split/merge partition metadata is absent, overlapping, gapped, or nondeterministically ordered
- Preserve sidecar pulse ranges as optional cross-checks only.

Required verifier failures to close:
1. Geometry-field presence and continuity validation.
2. Explicit invariant that total output vector count equals sum of segment vectors.
3. Channel consistency contract and validation.
4. Complete test coverage for metadata/PVP mapping, ownership/layout, vector/channel/sample ordering, and SarAccelControlToken preservation.
5. Negative tests for payload drop, sidecar-as-physics misuse, and per-segment final-image behavior.
6. Explicit split/merge partition metadata contract for PR4.

Do not:
- Do not implement focused-image transform, image formation, image sinks, Metal execution, SarPy/reference generation, image comparison, local real-data workflow, or MATLAB dependency.
- Do not make JSON sidecars authoritative for signal, PVP, geometry, or physics fields.
- Do not treat CRSD segments as independent final images.
- Do not alter GraphX runtime contracts globally.
- Do not require real GOTCHA data or SarPy in CI.

Suggested validation:
```bash
cmake --build build-ninja/ninja-debug-metal-native --target test_sar_example_unit

./build-ninja/ninja-debug-metal-native/examples/SAR/test/test_sar_example_unit \
  '--gtest_filter=CrsdApertureAssemblyAdapterNodeTest.*'
```

Output the standard IMPLEMENTER summary.
Save the report to plan/reviews/SAR_CRSD_TO_FOCUSED_IMAGE_IMPL_PR3B.md.
```

---

## PR4 Implementer

```text
Act as IMPLEMENTER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Implement exactly PR4 from plan/reviews/SAR_CRSD_TO_FOCUSED_IMAGE_PLANNER_REPORT.md: Focused image formation path in GraphX.

Scope:
- Add CrsdFocusedImageTransformNode, plugin registration, CMake wiring, focused-image config, and tests.
- Implement a first true focused-image transform from CRSD-derived full-aperture phase history.
- Use a deterministic backprojection-based path unless repository inspection reveals an existing equivalent SAR CPU reference path to reuse.
- Produce one focused SAR image from the ordered CRSD set.
- Make image dimensions, coordinate/grid assumptions, dtype, layout, and geometry assumptions explicit.
- Add tests for deterministic output hash, finite/nonzero/stable peak, all-zero input, tiny coherent multi-segment peak, one-sample perturbation, PVP/geometry perturbation, segment drop/reorder behavior, quick-look rejection, diagnostics-only failure, payload-ignored failure, and split/merge determinism.
- Preserve SarAccelControlToken on SAR edges.

Do not implement Metal execution, focused-image sink artifacts beyond what the transform test needs, SarPy reference generation, local real-data workflow, or MATLAB dependency.
Do not write one focused image per CRSD segment.

Output the standard IMPLEMENTER summary.
Save the report to plan/reviews/SAR_CRSD_TO_FOCUSED_IMAGE_IMPL_PR4.md.
```

## PR4 Verifier

```text
Act as VERIFIER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Verify exactly PR4 from plan/reviews/SAR_CRSD_TO_FOCUSED_IMAGE_PLANNER_REPORT.md.

Required checks:
- GraphX computes one real focused SAR image from ordered CRSD-derived full-aperture phase history.
- Output is not a CRSD signal magnitude quick-look.
- Output shape, geometry assumptions, dtype, and layout are explicit.
- Tests prove data dependence on CRSD signal and PVP/geometry fields.
- Tests fail for diagnostics-only forwarding, payload-ignored implementation, quick-look output, and one-image-per-segment behavior.
- Split/merge path is deterministic and preserves SarAccelControlToken.
- Proof matrix covers all-zero, coherent peak, one-sample perturbation, PVP/geometry perturbation, deterministic repeatability, and segment drop/reorder.
- No Metal lane, sink artifact contract, SarPy reference generation, local real-data workflow, or MATLAB dependency was added.

Stop after verifier report.
Save the report to plan/reviews/SAR_CRSD_TO_FOCUSED_IMAGE_VERIFY_PR4.md.
```

---

## PR5 Implementer

```text
Act as IMPLEMENTER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Implement exactly PR5 from plan/reviews/SAR_CRSD_TO_FOCUSED_IMAGE_PLANNER_REPORT.md: Metal focused-image execution proof.

Scope:
- Add Metal focused-image execution lane in the planned files or equivalent repository-consistent locations.
- Reuse the same CRSD-derived phase-history payload contract as the CPU lane.
- Add CPU and Metal focused-image configs for the tiny multi-segment CRSD fixture.
- Add resolver-selection tests proving Metal-capable H2D/kernel/D2H nodes are selected.
- Add diagnostics requiring bytes_h2d > 0, bytes_d2h > 0, and kernel_dispatches > 0.
- Add CPU-vs-Metal focused-image parity tests with documented deterministic tolerances.
- Add guardrail test failing if Metal only forwards tokens without kernel execution.
- Preserve SarAccelControlToken through split/merge fan-out/fan-in and GPU backfill diagnostics.
- Gate native Metal execution where unavailable without weakening CPU CI coverage.

Do not change CRSD reader semantics, CPU focused-image algorithm, SarPy lane, local real-data workflow, or MATLAB dependency.

Output the standard IMPLEMENTER summary.
Save the report to plan/reviews/SAR_CRSD_TO_FOCUSED_IMAGE_IMPL_PR5.md.
```

## PR5 Verifier

```text
Act as VERIFIER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Verify exactly PR5 from plan/reviews/SAR_CRSD_TO_FOCUSED_IMAGE_PLANNER_REPORT.md.

Required checks:
- Metal lane uses the same CRSD-derived phase-history payload contract as CPU.
- Resolver-selection tests prove Metal-capable transfer/kernel nodes are selected.
- Diagnostics report nonzero H2D bytes, D2H bytes, and kernel dispatches when native Metal lane runs.
- CPU-vs-Metal parity test passes within documented tolerances on tiny multi-segment fixture.
- Guardrail fails if Metal lane only forwards tokens.
- SarAccelControlToken is preserved across split/merge fan-out/fan-in.
- Native Metal unavailability is gated without weakening CPU CI coverage.
- No SarPy reference lane, local real-data workflow, MATLAB dependency, or unrelated CRSD reader redesign was added.

Stop after verifier report.
Save the report to plan/reviews/SAR_CRSD_TO_FOCUSED_IMAGE_VERIFY_PR5.md.
```

---

## PR5b Implementer

```text
Act as IMPLEMENTER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Implement corrective PR5b for plan/reviews/SAR_CRSD_TO_FOCUSED_IMAGE_PLANNER_REPORT.md: Real Metal focused-image execution for CRSD-derived phase history.

Failure context:
- PR5 created CrsdFocusedImageTransformMetalNode, but it does not implement real Metal focused-image execution.
- The current Metal node computes pixels by calling CrsdFocusedImageTransformNode / CPU transform and then backfills Metal-looking diagnostics.
- Current bytes_h2d, bytes_d2h, kernel_dispatches, tickets, and timing fields are synthetic values assigned in C++ after CPU computation.
- Current IsNativeMetalAvailable only checks __APPLE__, not a real Metal device/capability.
- Current tests explicitly accept exact parity because the Metal lane reuses the CPU math path.
- Resolver tests prove generic SAR accel-node selection, but do not prove CrsdFocusedImageTransformMetalNode dispatches a Metal focused-image kernel.

Goal:
Replace the PR5 Metal-labeled CPU wrapper with a real Metal execution lane for CRSD-derived focused-image computation, or fail clearly when native Metal execution is unavailable. The PR is incomplete if focused-image pixels are produced by the CPU transform inside CrsdFocusedImageTransformMetalNode while claiming Metal execution.

Scope:
- Update examples/SAR/include/sar/CrsdFocusedImageTransformMetal.hpp and examples/SAR/src/CrsdFocusedImageTransformMetal.cpp so the native Metal path uses actual GraphX Metal capabilities or existing native Metal infrastructure for:
  - device availability/capability discovery
  - device/shared buffer allocation for phase-history input and focused-image output
  - host-to-device transfer of CRSD-derived phase-history payload
  - Metal kernel dispatch for focused-image/backprojection computation
  - device-to-host transfer of focused-image output
  - measured/real telemetry or capability-reported transfer/kernel diagnostics
- Reuse existing GraphX Metal abstractions and capability interfaces where available; inspect libgpu/gpu/metal and existing SAR accel nodes before adding anything new.
- If an existing Metal backprojection kernel node can be reused, route the CRSD phase-history payload into that node contract rather than inventing a parallel Metal stack.
- If a small new Metal kernel is required, add the minimum repository-consistent kernel/source/build wiring and keep it scoped to the CRSD focused-image path.
- Preserve the CPU focused-image node as a separate CPU baseline, not as the implementation body of the Metal node.
- Keep CPU fallback only when explicitly configured and clearly reported as fallback. Fallback output must not be labeled as native Metal execution and must not satisfy native Metal tests.
- Replace synthetic diagnostics with diagnostics derived from actual Metal transfers/kernel dispatches/capability telemetry.
- Add tests that fail if the Metal node calls or depends on CrsdFocusedImageTransformNode for native Metal output.
- Add tests that fail if bytes_h2d, bytes_d2h, kernel_dispatches, transfer tickets, kernel tickets, or timing fields are fabricated without actual Metal capability activity.
- Add CPU-vs-Metal parity tests using tolerance appropriate for real Metal math, not exact equality caused by CPU reuse.
- Add native Metal tests that require:
  - real Metal device/capability available
  - nonzero real H2D transfer
  - nonzero real kernel dispatch
  - nonzero real D2H transfer
  - focused-image output produced from Metal result buffer
  - output changes when CRSD samples or geometry/PVP inputs change
- Keep platform gating honest:
  - On non-Metal platforms, native Metal tests may skip.
  - On Apple/native Metal builds, strict native Metal mode must fail if no actual Metal device/capability/kernel dispatch occurs.
- Update PR5 report and tests to remove claims that CPU delegation is acceptable Metal proof.

Required negative-proof tests:
- Native Metal mode fails if the Metal capability/kernel is unavailable and fallback is disabled.
- Native Metal mode fails if kernel_dispatches remains zero.
- Native Metal mode fails if output is produced only by CPU transform delegation.
- Native Metal mode fails if diagnostics are manually backfilled without capability telemetry or actual command execution evidence.
- Native Metal mode fails if phase-history payload is ignored or replaced with synthetic data.
- Fallback mode, if retained, reports CPU fallback explicitly and is not counted as Metal execution.

Do not:
- Do not change CRSD reader semantics.
- Do not change the CPU focused-image algorithm except for small shared utility extraction if genuinely necessary.
- Do not implement SarPy reference generation, comparison lane, sink artifact contract, local real-data workflow, or MATLAB dependency.
- Do not add real GOTCHA data or generated large CRSD files.
- Do not loosen PR4 focused-image correctness tests.
- Do not use JSON sidecars as physics inputs.
- Do not silently pass by resolver-selection tests alone.

Suggested validation:
```bash
cmake --build build-ninja/ninja-debug-metal-native --target test_sar_example_unit

./build-ninja/ninja-debug-metal-native/examples/SAR/test/test_sar_example_unit \
  '--gtest_filter=CrsdFocusedImageMetalTest.*'

./build-ninja/ninja-debug-metal-native/examples/SAR/test/test_sar_example_unit \
  '--gtest_filter=CrsdFocusedImageTransformNodeTest.*:CrsdFocusedImageMetalTest.*'
```

Output the standard IMPLEMENTER summary.
Save the report to plan/reviews/SAR_CRSD_TO_FOCUSED_IMAGE_IMPL_PR5B.md.
```

## PR5b Verifier

```text
Act as VERIFIER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Verify corrective PR5b for plan/reviews/SAR_CRSD_TO_FOCUSED_IMAGE_PLANNER_REPORT.md: Real Metal focused-image execution for CRSD-derived phase history.

Required checks:
- CrsdFocusedImageTransformMetalNode no longer produces native Metal output by calling CrsdFocusedImageTransformNode / CPU transform internally.
- Native Metal path uses actual GraphX Metal capabilities or existing native Metal infrastructure for device/buffer allocation, H2D transfer, kernel dispatch, and D2H transfer.
- Focused-image pixels in native Metal mode are read back from a Metal result buffer.
- bytes_h2d, bytes_d2h, kernel_dispatches, transfer tickets, kernel tickets, and timing/telemetry are derived from real Metal capability activity or command execution evidence, not hardcoded/synthetic backfill.
- Device availability checks prove real Metal capability/device availability, not only __APPLE__.
- Strict native Metal mode fails clearly if real Metal capability/kernel execution is unavailable.
- CPU fallback, if retained, is explicit and is not counted as native Metal execution.
- CPU-vs-Metal parity uses documented numeric tolerance appropriate for real Metal math.
- Tests prove Metal output changes when CRSD signal or geometry/PVP inputs change.
- Tests fail if payload is ignored, CPU delegation is used for native Metal output, diagnostics are fabricated, or only resolver-selection evidence is present.
- Existing resolver-selection tests may remain, but they are not the only proof of PR5b.
- No CRSD reader redesign, SarPy reference lane, comparison lane, sink artifact contract, local real-data workflow, MATLAB dependency, checked-in real GOTCHA data, or large generated CRSD artifacts were added.

Suggested verification commands:
```bash
cmake --build build-ninja/ninja-debug-metal-native --target test_sar_example_unit

./build-ninja/ninja-debug-metal-native/examples/SAR/test/test_sar_example_unit \
  '--gtest_filter=CrsdFocusedImageMetalTest.*'
```

Stop after verifier report.
Save the report to plan/reviews/SAR_CRSD_TO_FOCUSED_IMAGE_VERIFY_PR5B.md.
```

---

## PR6 Implementer

```text
Act as IMPLEMENTER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Implement exactly PR6 from plan/reviews/SAR_CRSD_TO_FOCUSED_IMAGE_PLANNER_REPORT.md: Deterministic focused-image output sink and artifact contract.

Scope:
- Add CrsdFocusedImageSinkNode, plugin registration, schema, config, and tests.
- Persist deterministic focused-image binary/JSON artifact pair and convenience PNG/PGM image.
- Preserve shape, spacing, geometry assumptions, hashes, provenance, execution lane, ordered CRSD segment list, per-segment input hashes, ordered-set hash, output hash, and lineage proving one image came from the complete aperture set.
- Add artifact schema/contract tests and deterministic binary/JSON/PNG/PGM tests.

Do not implement SarPy reference generation, comparison lane, local real-data workflow, or MATLAB dependency.
Do not alter focused-image math except as needed to connect the sink.

Output the standard IMPLEMENTER summary.
Save the report to plan/reviews/SAR_CRSD_TO_FOCUSED_IMAGE_IMPL_PR6.md.
```

## PR6 Verifier

```text
Act as VERIFIER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Verify exactly PR6 from plan/reviews/SAR_CRSD_TO_FOCUSED_IMAGE_PLANNER_REPORT.md.

Required checks:
- Sink writes deterministic binary and JSON focused-image artifacts.
- Sink writes convenience PNG or PGM output.
- Artifact schema preserves shape, spacing, assumptions, hashes, provenance, CPU/Metal lane, ordered CRSD segment list, per-segment input hashes, ordered-set hash, output hash, and complete-aperture lineage.
- Tests prove deterministic serialization and JSON/binary consistency.
- No SarPy reference generation, comparison lane, local real-data workflow, MATLAB dependency, or unrelated algorithm redesign was added.

Stop after verifier report.
Save the report to plan/reviews/SAR_CRSD_TO_FOCUSED_IMAGE_VERIFY_PR6.md.
```

---

## PR7 Implementer

```text
Act as IMPLEMENTER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Implement exactly PR7 from plan/reviews/SAR_CRSD_TO_FOCUSED_IMAGE_PLANNER_REPORT.md: SarPy/reference focused-image generation harness from CRSD.

Scope:
- Update tools/sarpy/reference_image_from_crsd.py and docs/tests as planned.
- Reference workflow must accept the same ordered CRSD input set used by GraphX.
- Generate one focused reference image artifact and metadata when a true focused-image reference path is available.
- Keep SarPy/reference workflow local-only and gated; it must not become a runtime or CI dependency.
- Add probe-only tests documenting local_only=true and ci_safe=false.
- Add optional local smoke test.
- Add guardrail rejecting CRSD signal block magnitude extraction as a focused reference.
- If installed SarPy cannot form a true focused image from CRSD, document the limitation and select or stub an independent local reference image-formation harness path without pretending magnitude quick-look is focused output.

Do not change GraphX focused-image implementation, sink contract, local real-data workflow, or MATLAB dependency.

Output the standard IMPLEMENTER summary.
Save the report to plan/reviews/SAR_CRSD_TO_FOCUSED_IMAGE_IMPL_PR7.md.
```

## PR7 Verifier

```text
Act as VERIFIER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Verify exactly PR7 from plan/reviews/SAR_CRSD_TO_FOCUSED_IMAGE_PLANNER_REPORT.md.

Required checks:
- Reference script accepts an ordered CRSD input set.
- Reference path produces one focused reference image artifact when a true reference image-formation path is available.
- SarPy/reference workflow remains local-only/gated and not a runtime or CI dependency.
- Tests/documentation reject CRSD signal block magnitude extraction as a focused reference.
- If SarPy cannot generate a true focused image, the limitation is documented clearly and no misleading focused-output claim is made.
- No GraphX focused-image algorithm rewrite, comparison lane, real-data workflow, MATLAB dependency, or CI SarPy requirement was added.

Stop after verifier report.
Save the report to plan/reviews/SAR_CRSD_TO_FOCUSED_IMAGE_VERIFY_PR7.md.
```

---

## PR8 Implementer

```text
Act as IMPLEMENTER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Implement exactly PR8 from plan/reviews/SAR_CRSD_TO_FOCUSED_IMAGE_PLANNER_REPORT.md: GraphX-vs-SarPy focused-image comparison lane.

Scope:
- Add/update image comparison tools, schemas, docs, and tests as planned.
- Compare GraphX focused-image output against the reference lane using the same ordered CRSD input set.
- Emit comparison_report.json, difference_magnitude.png, and phase_difference.png.
- Include RMSE, phase RMSE, peak error, correlation, and optional SSIM.
- Record per-segment CRSD input checksums, ordered-set checksum, GraphX output hash, reference output hash, algorithm, and geometry assumptions.
- Add deterministic tiny-fixture CI-safe comparison tests.
- Keep extended SarPy/reference runs optional and local-only.

Do not require real GOTCHA data or SarPy in CI.
Do not change core GraphX runtime contracts or focused-image math except for report plumbing.
Do not add MATLAB.

Output the standard IMPLEMENTER summary.
Save the report to plan/reviews/SAR_CRSD_TO_FOCUSED_IMAGE_IMPL_PR8.md.
```

## PR8 Verifier

```text
Act as VERIFIER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Verify exactly PR8 from plan/reviews/SAR_CRSD_TO_FOCUSED_IMAGE_PLANNER_REPORT.md.

Required checks:
- Comparison lane uses the same ordered CRSD input set for GraphX and reference paths.
- Outputs include comparison_report.json, difference_magnitude.png, and phase_difference.png.
- Metrics include RMSE, phase RMSE, peak error, correlation, and optional SSIM where available.
- Reports record per-segment checksums, ordered-set checksum, GraphX output hash, reference output hash, algorithm, and geometry assumptions.
- Tiny deterministic fixture lane is CI-safe.
- Extended SarPy/reference run remains gated/local-only.
- No real GOTCHA data, CI SarPy dependency, MATLAB dependency, core runtime redesign, or unrelated algorithm change was added.

Stop after verifier report.
Save the report to plan/reviews/SAR_CRSD_TO_FOCUSED_IMAGE_VERIFY_PR8.md.
```

---

## PR9 Implementer

```text
Act as IMPLEMENTER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Implement exactly PR9 from plan/reviews/SAR_CRSD_TO_FOCUSED_IMAGE_PLANNER_REPORT.md: Local-only GOTCHA-derived CRSD validation workflow.

Scope:
- Add/update the local-only workflow, docs, config, and disabled-by-default tests as planned.
- Workflow must operate on generated CRSD layout such as data/crsd/subData01.crsd_output/.../product.crsd through subData10.../product.crsd.
- Treat all selected CRSD products as one ordered aperture set.
- Produce one final focused-image artifact set.
- Verify processing with nonzero metrics, per-segment input checksums, ordered-set checksum, and output checksum.
- Verify dropping or reordering one segment fails or changes output deterministically.
- Treat metadata.json, pvp.json, chunk_index.json, provenance.json, and SarPy validation JSON as optional sidecar evidence only.
- Keep workflow explicitly opt-in/local-only and CI independent of real GOTCHA data and SarPy.

Do not download datasets.
Do not check in GOTCHA data, generated real-data sidecars, generated CRSD, or focused-image outputs.
Do not add MATLAB.
Do not change CI requirements.

Output the standard IMPLEMENTER summary.
Save the report to plan/reviews/SAR_CRSD_TO_FOCUSED_IMAGE_IMPL_PR9.md.
```

## PR9 Verifier

```text
Act as VERIFIER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Verify exactly PR9 from plan/reviews/SAR_CRSD_TO_FOCUSED_IMAGE_PLANNER_REPORT.md.

Required checks:
- Real-data workflow is explicitly local-only and opt-in.
- CI remains independent of real GOTCHA data and SarPy.
- Workflow accepts the generated CRSD layout with subData01 through subData10 product.crsd files as one ordered aperture set.
- Workflow emits one final focused-image artifact set, not one image per CRSD segment.
- Workflow records per-segment input checksums, ordered-set checksum, output checksum, and nonzero image metrics.
- Dropping or reordering one segment fails or changes output deterministically.
- Sidecar JSON and SarPy validation JSON are optional evidence only, not authoritative signal/PVP inputs.
- No downloads, checked-in GOTCHA data, checked-in generated outputs, MATLAB dependency, or CI real-data requirement was added.

Stop after verifier report.
Save the report to plan/reviews/SAR_CRSD_TO_FOCUSED_IMAGE_VERIFY_PR9.md.
```

---

## PR10 Implementer

```text
Act as IMPLEMENTER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Implement exactly PR10 from plan/reviews/SAR_CRSD_TO_FOCUSED_IMAGE_PLANNER_REPORT.md: Documentation finalization and guardrail verification.

Scope:
- Update README/docs/config examples/tests as planned.
- Add docs/sar/crsd_to_focused_image.md.
- Document CRSD signal quick-look versus focused SAR image.
- Document ordered CRSD set ingestion, token-based phase-history flow, CPU focused-image path, Metal focused-image path, split/merge topology, SarAccelControlToken requirements, and processing evidence.
- Add/maintain tiny CI fixture and local GOTCHA-derived config examples.
- Add guardrails for no MATLAB dependency, no SarPy runtime dependency, local-only real-data lane, no quick-look misuse, no diagnostic-only focused-image execution, payload/output hash recording, SarAccelControlToken edge preservation, deterministic split/merge, and GPU backfill evidence.
- Delete obsolete tests tied only to superseded pre-CRSD focused-image assumptions if implementation confirms they are obsolete.

Do not add new product behavior beyond documentation/config/guardrails.
Do not require real GOTCHA data or SarPy in CI.
Do not add MATLAB.

Output the standard IMPLEMENTER summary.
Save the report to plan/reviews/SAR_CRSD_TO_FOCUSED_IMAGE_IMPL_PR10.md.
```

## PR10 Verifier

```text
Act as VERIFIER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Verify exactly PR10 from plan/reviews/SAR_CRSD_TO_FOCUSED_IMAGE_PLANNER_REPORT.md.

Required checks:
- Documentation distinguishes CRSD signal quick-look from focused SAR image.
- Documentation explains ordered CRSD set ingestion, token-based phase-history flow, CPU focused-image path, Metal path, split/merge topology, SarAccelControlToken requirements, and processing evidence.
- Tiny CI fixture and local GOTCHA-derived config examples exist and validate.
- Guardrails reject quick-look-only misuse, diagnostic-only execution, missing payload/output hashes, SarAccelControlToken edge drift, nondeterministic split/merge, MATLAB dependency, SarPy runtime dependency, and CI real-data dependency.
- Local-only real-data lane remains opt-in.
- Obsolete tests, if deleted, are genuinely superseded and final capability coverage remains.
- No new product behavior, real GOTCHA data, generated outputs, CI SarPy requirement, or MATLAB dependency was added.

Stop after verifier report.
Save the report to plan/reviews/SAR_CRSD_TO_FOCUSED_IMAGE_VERIFY_PR10.md.
```
