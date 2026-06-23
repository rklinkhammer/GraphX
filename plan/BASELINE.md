# GraphX SAR And FHSS Baseline Plan

This is the active planning baseline going forward. Historical roadmaps,
agent prompts, implementer reports, verifier reports, and exploratory notes
have been archived under `plan/archive/2026-06-baseline/`.

The goal of this baseline is to keep the active plan small: architecture,
next steps, known issues, and future work for SAR and FHSS.

## Planning Rules

- Keep active planning material in this file unless a future effort genuinely
  needs its own active plan.
- Treat archived plan files as historical references only.
- Keep user-facing build, run, and test documentation in the top-level
  `README.md`.
- Avoid reintroducing PR-by-PR prompt files as active documentation.
- Preserve truth-in-labeling: fixture, test, reference, local-only, GPU, and
  production-like claims must be named exactly.

Current active cleanup roadmap:

- `plan/roadmap/GRAPHX_PR_ROADMAP.md`

Current active cleanup implementer/verifier prompts:

- `plan/agents/GRAPHX_PR_AGENTS.md`

## Current Architecture

### Core GraphX Runtime

- GraphX is a C++26 graph runtime with typed nodes, ports, JSON graph loading,
  plugin/provider-based dynamic nodes, executor policies, and accelerator-ready
  token contracts.
- Runtime execution should use repository-native GraphX APIs, especially
  `GraphExecutorBuilder`, existing graph config parsing, existing plugin
  loading, and existing node/edge methods.
- New node families should avoid local pseudo-node APIs. Public `...Node`
  classes should be real GraphX nodes or private algorithm kernels with names
  that do not imply graph-node status.
- Large fixed fan-in/fan-out nodes should prefer reusable GraphX base helpers
  such as routed input/output/transfer functions and fixed fan-in/out bases
  instead of per-port boilerplate.

### SAR

The active SAR work is organized around CRSD ingest, deterministic fixture
coverage, focused-image formation, and local-only reference comparison.

Current SAR lanes:

- **Synthetic stripmap examples:** deterministic GraphX SAR example configs
  remain useful for node-level and graph-shape coverage.
- **GOTCHA to CRSD conversion:** local GOTCHA `.mat` inputs are converted to
  GraphX CRSD products through `graphx-gotcha-to-crsd` and helper scripts.
- **CRSD ordered-set ingest:** `OrderedCrsdSetInputSourceNode` reads ordered
  CRSD products and preserves input ordering/metadata.
- **CRSD to focused image:** CRSD aperture assembly feeds CPU and Metal
  focused-image transform paths, then a focused-image sink writes deterministic
  artifacts.
- **Local reference comparison:** SarPy/gotcha-back/reference tooling is kept
  local-only and outside the GraphX runtime. It is a validation aid, not a
  runtime dependency.

SAR truth-in-labeling:

- MATLAB is not a build-time, runtime, or test-time dependency.
- SarPy and gotcha-back are local-only reference/comparison tools.
- Quick-look CRSD signal inspection is not focused-image acceptance evidence.
- Metal SAR nodes must state whether they are transfer, memory, sync/control,
  generic kernel, or domain algorithm nodes.
- Experimental or incomplete Metal behavior must remain labeled as such.

PR7: Consolidated SAR config set (13 active configs):

**Canonical CPU configs (CI-safe):**
- `examples/SAR/config/sar_stripmap_simulated.json` - Basic synthetic stripmap graph
- `examples/SAR/config/sar_crsd_tiny_fixture_focused_image_cpu.json` - CRSD focused-image CPU lane

**Experimental canonical GPU path (1 only):**
- `examples/SAR/config/sar_crsd_tiny_fixture_focused_image_metal.json` - CRSD focused-image Metal lane (experimental/incomplete)

**Local-only validation config:**
- `examples/SAR/config/sar_crsd_gotcha_local_validation.json` - Local-only GOTCHA-derived CRSD validation

**Specialized variants for dedicated test scenarios (kept):**
- `sar_stripmap_definitive.json` - Definitive stripmap config
- `sar_stripmap_fanout.json` - Fanout variant
- `sar_stripmap_matched_filter.json` - Matched filter variant
- `sar_stripmap_materialized_image.json` - Materialized image variant
- `sar_projectile_approach.json` - Projectile scenario
- `sar_crsd_tiny_fixture_full_pipeline.json` - Full pipeline variant
- `sar_crsd_tiny_fixture_set_input.json` - Path-based input mode
- `sar_crsd_tiny_fixture_set_input_directory.json` - Directory-based input mode
- `sar_crsd_tiny_fixture_set_input_manifest.json` - Manifest-based input mode

**Deleted (PR7 cleanup):**
- Orphaned: `sar_gotcha_external_manual.json`
- Unused CMakeLists vars: `sar_crsd_focused_image_tiny_fixture.json`, `sar_crsd_tiny_fixture_with_sink.json`,
  `sar_crsd_real_directory_input_smoke.json`, `sar_crsd_real_paths_input_smoke.json`
- Unused Metal variants: `sar_stripmap_metal_window.json`, `sar_stripmap_metal_compression.json`,
  `sar_stripmap_metal_fanout.json`

Current canonical SAR GPU path is `sar_crsd_tiny_fixture_focused_image_metal.json` (experimental/incomplete).
It is the only active canonical SAR GPU path and remains experimental/incomplete until explicitly promoted.

PR13 external SAR baseline survey status:

- Planning-only survey completed; no external baseline package is integrated,
  required, or supported by GraphX in this PR.
- Candidate comparison dimensions are tracked as: license, install complexity,
  data support, output format compatibility, determinism, and CI/local
  feasibility.
- Recommendation status: clear deferral.
  - No package is selected for integration in PR13.
  - PR14 may select one candidate only after license and reproducibility checks
    are revalidated at implementation time.
- CI/local implications are explicit:
  - Any external baseline execution remains local-only and opt-in.
  - Default CI remains external-dependency-free.

PR14 local-only baseline runner status:

- Selected baseline package for local runner: SarPy.
- Runner entry point: `examples/SAR/tools/sar_local_baseline_runner.py`.
- Explicit gating:
  - `GRAPHX_SAR_BASELINE_RUNNER_ENABLE=1` is required to attempt local smoke.
  - `GRAPHX_SARPY_CRSD_FILE` must point to a local CRSD product for smoke execution.
- CI-safe behavior:
  - Without opt-in, missing package, or missing dataset path, the runner emits
    deterministic skip diagnostics.
  - Default CI remains external-dependency-free.
- Truth-in-labeling:
  - This runner is local-only and not a GraphX runtime dependency.

PR15 GraphX-vs-baseline comparison harness status:

- Harness entry point: `examples/SAR/tools/sar_graphx_vs_baseline_harness.py`.
- CI-safe path: `run-ci-tiny-fixture` compares deterministic tiny fixture
  contracts without restricted datasets.
- Local-only path: `run-local-comparison` is gated by
  `GRAPHX_SAR_BASELINE_RUNNER_ENABLE=1`.
- Output semantics:
  - Deterministic comparison metrics are emitted for image/metadata contract
    validation.
  - Metrics are validation aids only and are not production SAR claims.

### FHSS

The active FHSS implementation is a deterministic CPU fixture and decoder lane.
It is not a production RF receiver.

Canonical FHSS graph:

```text
FHSSSyntheticIqSourceNode
  -> FHSSDownconverterNode
  -> ChannelizerNode
  -> PerChannelPulseDetectorNode[64]
  -> FHSSPulseMergeNode
  -> FHSSPulseCandidateNode
  -> CPSMBranchMetricNode
  -> CPSMViterbiDecoderNode
  -> FHSSPulseWordDecoderNode
  -> FHSSPreambleDetectorNode
  -> FHSSMessageAssemblerNode
  -> FHSSMessageSinkNode
```

FHSS fixture constants:

- sample rate: `500 Msps`;
- bit rate: `5 Mbps`;
- `100` samples/symbol;
- `32` symbols/pulse;
- `3200` pulse samples;
- `3300` gap samples;
- `6500` samples per pulse period;
- 64 RF metadata frequencies;
- selectable transmit indices `[1, 62]`;
- reserved receiver guard/metadata indices `0` and `63`;
- exactly 16 preamble pulses;
- exactly four active transmit frequencies derived from the preamble;
- at most 256 pulses per message, including preamble.

FHSS node/edge requirements:

- FHSS graph edges use `graph::gpu::accel::ControlToken<...>` packet sidecars.
- `ChannelizerNode` exposes one GraphX output port per configured frequency;
  for the current table this means exactly 64 output ports.
- Output port `N` maps to frequency index `N` and channel id `N`.
- `PerChannelPulseDetectorNode` consumes one channel and does not scan across
  frequencies.
- `FHSSPulseMergeNode` normalizes detections into shared global sample time and
  preserves complex evidence for CPSM decoding.
- The canonical channelized graph is the only active FHSS receiver topology.
- The old correlator-bank graph, node, config, and plugin were removed from
  active support; do not describe them as retained, canonical, or
  production-like.

FHSS truth-in-labeling:

- 1 GHz RF values are metadata. Fixture IQ uses baseband/IF offsets.
- The 500 Msps fixture cannot represent the full 64-entry 1 GHz RF table as
  direct sampled RF, and it does not use or implement direct 1 GHz RF sampling.
- Guardrail wording: cannot represent the full 64-entry 1 GHz RF table as direct sampled RF.
- The full 64-entry RF table must not be described as one simultaneous
  alias-free 500 Msps complex-baseband RF capture.
- Magnitude-only DFT/FFT output is not the canonical decoder input.
- Complex IQ evidence, CPSM branch metrics, and Viterbi/MLSE decisions are the
  canonical decoder path.
- Guardrail wording: complex IQ evidence; CPSM branch metrics; Viterbi/MLSE.
- The deterministic CPU fixture channelizer is not a production channelizer.
  Do not claim production channelizer separation claims or production
  channelizer performance.
- Doppler, noise, multipath, overlap-aware separation, real RF capture, and
  production channelizer performance are not implemented.
- Overlap is unsupported.
- FHSS Metal/GPU execution is future work.
- Metal/GPU acceleration of the FHSS lane is future work.
- PDW diagnostics as canonical decoder output are not part of the active
  decoder contract.
- Occupied-bandwidth and channel-filter requirements remain unresolved in PR16,
  so the current plan must not claim production channelizer separation.
- Receiver configuration preserves one logical GraphX channel output port per
  configured frequency.
- Guardrail wording: channel output port per configured frequency.
- Guardrail wording: retuned sub-band windows.
- Guardrail wording: explicit alias/downconvert modeling.
- **PR6 Guardrail: No canonical channelizer output type carries a vector/list
  of all channels.** Each ChannelizerNode output port is a distinct GraphX edge.
  There is no single token type or packet that bundles all 64 frequencies.
  The channelizer exposes exactly 64 separate output ports, each carrying one
  FHSSChannelizedIqToken. Aggregate stream containers are not allowed.
- Guardrail wording: no aggregate channelizer output stream.
- Guardrail wording: one port per frequency; no vector of all frequencies.
- The canonical FHSS implementation uses real GraphX nodes and
  `graph::gpu::accel::ControlToken<...>` contracts. Deleted pre-GraphX
  pseudo-node scaffolding is not the current node model.
- Canonical impairment status values include `configured_rejected` and
  `unsupported_impairments_rejected`; PDW diagnostics remain optional and
  non-canonical.
- Guardrail wording: PDW diagnostics remain optional and non-canonical.
- Guardrail wording: deleted pre-GraphX pseudo-node scaffolding is not the current node model.

## Next Steps

### Immediate Cross-Cutting Work

1. Keep the documentation baseline stable: README for user instructions and
   this file for active planning.
2. Add guardrails that prevent active docs from drifting back into many
   PR-specific plan files.
3. Continue converting any large repeated-port GraphX nodes to shared routed
   helpers where doing so reduces boilerplate and preserves existing GraphX
   semantics.
4. Keep CI-safe tests focused on deterministic fixtures; keep real-data and
   external-reference runs opt-in/local-only.

### SAR Next Steps

1. Reconfirm the canonical SAR graph configs after documentation consolidation:
   tiny CRSD focused-image CPU lane, Metal focused-image lane, and local GOTCHA
   validation lane.
2. Tighten artifact lineage for focused-image outputs: input product hashes,
   ordered-set checksums, graph config identity, algorithm identity, and output
   hashes.
3. Strengthen comparison reporting for GraphX-vs-reference focused images
   without adding reference tools as runtime dependencies.
4. Keep improving Metal focused-image truth-in-labeling and diagnostics before
   making performance claims.
5. Review SAR config sprawl and decide which example configs are canonical,
   reference-only, or historical.

### FHSS Next Steps

1. Use `graphx-dsp-fhss-demo` as the primary investigation tool for configured
   message schedules and graph diagnostics.
2. Add better diagnostics around channelizer sample-time mapping, detector
   confidence, pulse merge decisions, and unsupported overlap/impairment
   rejection.
3. Decide the next receiver-acquisition boundary beyond the current known-slot
   fixture assumption.
4. Define a deterministic occupied-bandwidth/spectral-occupancy check for the
   5 Mbps CPSM fixture before making stronger channelizer claims.
5. Plan the future full selectable-frequency strategy: retuned sub-band
   windows, higher sample rate, sparse scheduling, or explicit alias/downconvert
   modeling.

## Known Issues

### Documentation And Process

- Historical plan material is large and sometimes contradictory; use the
  archive only for traceability.
- Some tests intentionally verify truth-in-labeling strings. When docs move,
  update those tests to target the new baseline, not archived files.
- Avoid creating a new active roadmap file unless the project has a genuinely
  new active initiative.

### SAR Issues

- Real GOTCHA validation remains local-only and depends on local datasets.
- Reference comparison tooling can validate and diagnose, but it must not
  define GraphX runtime architecture.
- Focused-image correctness criteria need clearer numerical acceptance bands
  for larger scenes and nontrivial geometry.
- Metal focused-image execution remains truth-in-labeling sensitive: transfer
  and sync nodes are valid Metal nodes, but they are not proof of domain
  compute acceleration by themselves.

### FHSS Issues

- The deterministic channelizer is fixture-grade and does not prove production
  adjacent-channel separation.
- The current fixture uses known timing/slot assumptions; acquisition is not
  yet a general receiver problem.
- Overlap-aware separation is unsupported.
- Doppler/noise/CFO/multipath behavior is unsupported or placeholder-only.
- FHSS confidence values are useful for deterministic tests but are not yet a
  calibrated RF estimator.

## Future Work

### GraphX

- Generalize repeated-port node patterns where they produce simpler real
  GraphX nodes.
- Improve plugin/provider diagnostics for large graphs.
- Keep accelerator-token semantics stable across CPU and future GPU sidecars.
- Improve graph metrics reporting in example programs and CI artifacts.

### SAR

- Broaden deterministic CRSD focused-image fixtures while keeping CI cost
  controlled.
- Add optional local reference lanes for larger GOTCHA-derived CRSD products.
- Improve focused-image comparison metrics and reports.
- Expand Metal execution only with explicit kernel-ticket diagnostics and
  truth-in-labeling guardrails.
- Review and reduce SAR example config sprawl.

### FHSS

- Implement a production-oriented channelizer only after RF feasibility,
  bandwidth, filter, and guard requirements are defined.
- Add receiver acquisition beyond `message_start_sample = 0`.
- Add impairment models in planned order: CFO/phase drift, noise, Doppler,
  multipath.
- Add overlap-aware separation only after pulse timing, association, and
  collision diagnostics are stable.
- Consider future Metal/GPU acceleration after CPU channelized correctness is
  stable and packet sidecar semantics are preserved.

## Archive Map

- Historical plan material: `plan/archive/2026-06-baseline/`
- Historical user documentation: `docs/archive/2026-06-baseline/`
- Active user guide: `README.md`
- Active planning baseline: `plan/BASELINE.md`
