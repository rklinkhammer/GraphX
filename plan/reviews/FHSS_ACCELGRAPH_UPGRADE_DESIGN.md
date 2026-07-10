# FHSS AccelGraph Upgrade Design (Phase 0)

## Scope and Baseline

This record captures Phase 0 audit and design for upgrading FHSS into an accelerator-aware libaccelgraph workload.

Constraints retained:
- SAR remains out of scope.
- Topology-style tests remain JSON-owned and loader-driven.
- Graph construction remains through GraphExecutorBuilder + plugin descriptors.
- Node initialization remains through node_config + IConfigurable.
- No manual post-build Configure/ConfigureNode/ConfigureTransferNode/JsonView setup in topology tests.

## Audit Summary

### Existing FHSS graph and plugin topology

Current canonical FHSS topology:
- `libdsp/config/fhss_cpsm_channelized_fixture_500msps.json`

Observed shape:
- Source: `FHSSSyntheticIqSourceNode`
- Downconverter: `FHSSDownconverterNode`
- Channelizer: `FHSSFixtureFrequencyChannelizerNode`
- 64 detector fan-out: `PerChannelPulseDetectorNode` x64
- Merge and decode chain: `FHSSPulseMergeNode` -> `FHSSPulseCandidateNode` -> `CPSMBranchMetricNode` -> `CPSMViterbiDecoderNode` -> `FHSSPulseWordDecoderNode` -> `FHSSPreambleDetectorNode` -> `FHSSMessageAssemblerNode` -> `FHSSMessageSinkNode`

This is already a deterministic, end-to-end fixture graph with explicit JSON node_config per node.

Plugin ownership:
- FHSS runtime nodes are currently published from `libdsp/plugins`.
- libaccelgraph plugins currently expose transfer and spectrum scaffolding only.

### FHSS config fields and IConfigurable usage

FHSS configuration is already JSON-first via helper parsing in:
- `libdsp/include/dsp/fhss/FHSSGraphXConfig.hpp`

Several FHSS nodes implement IConfigurable + IParameterized (not all):
- Configurable now: source, downconverter, channelizer, per-channel detector, preamble detector, message assembler, message sink
- Non-configurable/compute-only now: branch metric, viterbi, pulse candidate/pass-through, pulse word decoder (current shape)

This supports preserving graph_rev2-style JSON-owned initialization for future accel FHSS topologies.

### Existing FHSS token and packet model

Critical finding:
- FHSS edge types are already accelerator-ready wrappers:
  - `FHSSGraphXToken<T> = graph::gpu::accel::ControlToken<T>`
  - Defined in `libdsp/include/dsp/fhss/FHSSPorts.hpp`

Canonical packet contracts already exist in:
- `libdsp/include/dsp/fhss/FHSSPackets.hpp`

They include explicit host/sidecar metadata and future-accelerator residency markers (`FHSSGraphXPayloadResidency`), so a second transport model should not be introduced.

### accelgraph conventions and backend behavior

Current accelgraph conventions are established by transfer and spectrum lanes:
- Descriptor metadata via static `Fields()` in node classes
- JSON config enforcement through loader + descriptors
- Backend/fallback keys and behavior:
  - `backend`
  - `strict_fallback`
  - `fallback_policy`
  - `cuda_device_ordinal`

Provider diagnostics and selection are explicit:
- CPU provider: always available reference lane
- Metal provider: runtime-supported only on valid Apple/Metal host conditions
- CUDA provider: gated by compile/runtime/toolkit availability with actionable diagnostics

Topology test conventions are codified in:
- `libaccelgraph/test/TOPOLOGY_TESTING.md`
- `libaccelgraph/test/unit/test_accelgraph_phase2_topology_contract.cpp`
- `libaccelgraph/test/unit/AccelGraphTopologyTestUtils.hpp`

### What stays in libdsp vs what moves to libaccelgraph

Keep in libdsp (initially):
- Canonical FHSS protocol semantics and deterministic fixture contracts
- Existing validated CPU reference decode stages
- Existing fixture graph for regression oracle

Move into libaccelgraph (incrementally):
- Accelerator-aware FHSS graph nodes for numerically heavy RF/DSP stages
- Backend selection/fallback/session policy handling at node level
- JSON topology lane expansion for accelgraph FHSS synthetic graphs

Do not migrate stateful protocol assembly/decode early unless profiling proves benefit.

## First libaccelgraph FHSS Nodes to Implement

Initial node sequence (recommended):
1. `AccelFhssDownconverterNode`
2. `AccelFhssChannelizerNode`
3. `AccelFhssPerChannelPulseDetectorNode`
4. `AccelFhssBranchMetricNode`

CPU-first parity for each stage is mandatory before claiming native Metal/CUDA execution.

## Shared Packet/Token Ownership Strategy

Recommendation:
- Reuse existing FHSS packet/token contracts as canonical semantic edge model.
- Avoid creating duplicate FHSS packet structures under libaccelgraph.

Dependency caveat:
- `libaccelgraph` currently links `graph` only (not `dsp`).

Practical approach for Phase 1:
- Introduce a small shared FHSS edge-contract surface in a dependency-neutral location (or refactor minimal shared headers) to avoid libaccelgraph -> libdsp hard coupling.
- Keep one canonical definition source for FHSS packet semantics to prevent drift.

## CPU Reference Parity Strategy

For each new accel FHSS stage:
- Execute CPU reference path from JSON topology through GraphExecutorBuilder.
- Compare stage outputs against existing libdsp behavior using deterministic fixture inputs.
- Gate GPU selection through strict/allow fallback semantics identical to existing spectrum patterns.

Parity scope per stage:
- Downconverter: frequency translation metadata, center/reference frequency behavior, sample-time map integrity.
- Channelizer: one output port per configured frequency, channel metadata parity.
- Detector: pulse timing/frequency/confidence metadata parity.
- Branch metrics: candidate ordering, metric shape/tolerances, status diagnostics.

## Backend Selection and Fallback Policy

Use existing accelgraph vocabulary exactly:
- `backend`: `cpu|metal|cuda`
- `strict_fallback`: bool
- `fallback_policy`: `strict|allow`
- `cuda_device_ordinal`: non-negative integer
- optionally `provider_id` and `session_key` where transfer/session ownership is explicit

Behavior rules:
- Strict mode: fail with actionable backend diagnostic when requested backend unavailable.
- Allow mode: run CPU fallback, surface `used_fallback=true` and diagnostic reason.
- Never claim native execution when running CPU reference fallback.

## Topology JSON Naming Conventions

Adopt existing accelgraph naming style:
- `accelgraph_fhss_<stage>_cpu_topology.json`
- `accelgraph_fhss_<stage>_metal_topology.json`
- `accelgraph_fhss_<stage>_metal_allow_fallback_topology.json`
- `accelgraph_fhss_<stage>_cuda_topology.json`
- `accelgraph_fhss_<stage>_cuda_allow_fallback_topology.json`

Examples for first stage:
- `accelgraph_fhss_downconverter_cpu_topology.json`
- `accelgraph_fhss_downconverter_metal_topology.json`
- `accelgraph_fhss_downconverter_metal_allow_fallback_topology.json`
- `accelgraph_fhss_downconverter_cuda_topology.json`
- `accelgraph_fhss_downconverter_cuda_allow_fallback_topology.json`

## Verification Lanes

### macOS lane (implementor)
- Build `test_libaccelgraph_smoke`
- Run focused FHSS accel suite filters
- Run `--gtest_list_tests`
- Run accelgraph discovery CTest lane
- Validate topology JSON (`jq`) and guardrail bypass scan (`rg`)

### Jetson lane (verifier)
- Build CUDA-enabled `test_libaccelgraph_smoke`
- Run focused FHSS accel filters and discovery CTest
- Validate strict CUDA and allow-fallback behavior
- Confirm diagnostics are actionable and truthful

## Key Risks

1. Ownership layering risk
- libaccelgraph currently does not depend on libdsp. Reusing FHSS packet contracts requires careful shared-header placement to avoid circular dependencies.

2. Contract drift risk
- Duplicating FHSS packet/config schemas in two libraries will drift quickly; single-source contract ownership is required.

3. Port topology complexity risk
- FHSS channelizer requires 64-output mapping with deterministic per-channel wiring. Any aggregate shortcut would violate existing contract assumptions.

4. Diagnostic truthfulness risk
- GPU backends must not report success when CPU fallback actually ran; strict/allow semantics must remain explicit.

5. Performance vs correctness risk
- Early focus must be parity and deterministic correctness; optimization and deeper GPU kernels come later.

## Recommended Phase Order (Implementation)

1. Phase 1
- Establish shared FHSS accel contract headers and config schema alignment.
- Add focused metadata/config parsing tests only.

2. Phase 2
- Implement accel FHSS downconverter with CPU parity baseline.
- Add CPU/Metal/CUDA strict+allow topology set and tests.

3. Phase 3
- Implement accel FHSS channelizer with one-port-per-frequency contract preservation.

4. Phase 4
- Implement accel per-channel detector with metadata parity checks.

5. Phase 5
- Implement accel branch metrics; keep viterbi/word decode CPU until profiling justifies migration.

## Recommended Phase 1 Scope

Minimal, low-risk Phase 1:
- Create shared FHSS accel config/backend contract surface.
- Define descriptor-metadata field list for FHSS accel nodes.
- Add no-op or CPU-reference shell node contract tests (no algorithm migration yet).
- Keep existing libdsp FHSS runtime and canonical fixture behavior unchanged.
