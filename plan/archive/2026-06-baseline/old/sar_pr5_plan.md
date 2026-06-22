**1. Executive Recommendation**

Best next PR: **SAR PR5 should add physically meaningful range-compression correctness and image-quality validation, while keeping `GraphExecutorBuilder` + JSON as the required runtime path.** Evidence: **Observed in code + External SAR best practice**. PR4 added a scalar point-target/reference foundation in [SarCpuReference.hpp](/Users/rklinkhammer/workspace/GraphX/examples/SAR/include/sar/SarCpuReference.hpp:1) and CPU-vs-native-Metal adapter parity in [test_sar_cpu_reference.cpp](/Users/rklinkhammer/workspace/GraphX/examples/SAR/test/test_sar_cpu_reference.cpp:1). The next weakest math stage is [RangeCompressionNode.cpp](/Users/rklinkhammer/workspace/GraphX/examples/SAR/src/RangeCompressionNode.cpp:1), which performs FFT magnitude extraction, not chirp matched filtering.

Why highest leverage: **Observed in code**. Graph execution, JSON loading, accel-token enforcement, trace schema, and native Metal evidence are now stronger than SAR fidelity. The best improvement is therefore algorithm credibility, not another GPU kernel.

What not to do next: **External SAR best practice + Observed in code**. Do not add new Metal kernels until range compression/backprojection have CPU reference parity, image metrics, and JSON/GraphExecutor integration tests.

**2. Current-State Findings**

Accuracy/fidelity: **Observed in code**. Synthetic source samples are deterministic formulas plus optional moving-target perturbation, not physical phase history with chirp, carrier phase, platform trajectory, range delay, or calibrated scene geometry.

Performance: **Observed in code**. [sar_benchmark.cpp](/Users/rklinkhammer/workspace/GraphX/examples/SAR/src/sar_benchmark.cpp:860) separates graph build, graph run, lifecycle total, baseline execution, transfer payload bytes, token-edge copies, and PR4 cost buckets.

Architecture/token contract: **Observed in code**. PR3 JSON presets use `execution_backend`, `backend_fallback_policy`, `resolver_diagnostics`, and `edge_contract: "accel-token"`. Parser validation rejects legacy SAR payload contracts under accel-token mode in [GraphConfigParser.cpp](/Users/rklinkhammer/workspace/GraphX/libgraph/src/graph/GraphConfigParser.cpp:565).

Validation/display: **Observed in code**. There are JSON runtime tests, graph-vs-direct diagnostics parity tests, trace schema tests, Gotcha normalized fixture tests, CPU reference tests, and PGM/CSV visualization support. Missing: true image-quality metrics, matched-filter known-vector tests, and graph/direct image-output parity.

**3. Mathematical Correctness Audit**

Source: **Symbolic/approximation**. Units exist in some configs, but runtime synthetic IQ is not generated from full SAR signal equations.

Range window: **Physically meaningful approximation**. Hann window + gain are valid DSP operations, but no PSLR/ISLR or sidelobe fixture validates impact.

Range compression: **Approximation/high-risk**. FFT magnitude output discards phase and does not apply a chirp replica/matched filter. Missing chirp bandwidth, carrier/sample frequency axis, FFT normalization audit, and complex output preservation.

Backprojection CPU reference: **Approximation but meaningful**. PR4 scalar reference uses slant range, wavelength, phase correction, nearest-range binning, and peak-location test. Missing interpolation beyond nearest bin, multi-target fixtures, radiometric scaling, and motion/calibration terms.

Backprojection Metal adapter: **Algorithm adapter parity, not full SAR correctness**. Native Metal matches the adapter CPU reference, but that adapter is a 1D tap/delay/phasor kernel, not full 2D SAR backprojection.

Merge/diagnostics: **Architecturally meaningful, physically symbolic**. It validates tile completeness and counters, not image accumulation correctness.

Highest-risk gaps: matched filtering, phase preservation, range-bin interpolation, 2D geometry model, dynamic-range/log scaling, PSLR/ISLR metrics.

**4. External Validation Data Matrix**

AFRL Gotcha: [SDMS Gotcha](https://www.sdms.afrl.af.mil/index.php?collection=gotcha). Type: phase-history validation. License/use: requires SDMS terms; immediate download per SDMS. Confidence: **Known/requires download**. Verified fields: X-band, 640 MHz bandwidth, MATLAB `.mat`, k-space, frequencies, antenna x/y/z, range-to-scene-center, azimuth/elevation. Best first local validation source.

AFRL Wide Angle SAR: [challenge PDF](https://www.sdms.afrl.af.mil/content/challenge_areas/documents/Wide_angle_SAR_data_for_target_discrimination_research.pdf). Type: wide-angle/circular SAR stress validation. Confidence: **Known/requires download**. Verified: 31 orbits, target spotlights, vehicles/reflectors/open area. Not CI suitable; good local PR6+ benchmark.

MSTAR: [SDMS MSTAR](https://www.sdms.afrl.af.mil/index.php?collection=mstar). Type: image-domain ATR sanity. Confidence: **Known/requires download**. Verified: X-band SAR imagery collections from 1995/1996. Not primary image-formation validation.

UAVSAR: [JPL format docs](https://uavsar.jpl.nasa.gov/science/documents.html). Type: SLC/geospatial/display validation. Confidence: **Known/requires experiment**. Verified: SLC, lat/lon/height, look vector, Doppler, metadata files; JPL asks for data courtesy acknowledgment.

Sentinel-1 via ASF: [ASF Sentinel-1](https://asf.alaska.edu/data-sets/sar-data-sets/sentinel-1/). Type: SLC/product/geospatial validation. Confidence: **Known/requires account/download**. Good for display/geospatial workflows, not raw custom backprojection first.

Sandia imagery: [Sandia SAR Imagery](https://www.sandia.gov/radar/pathfinder-radar-isr-and-synthetic-aperture-radar-sar-systems/archive-imagery-clone-2-clone-2/). Type: documentation/display reference. Verified: public reproduction with credit. Not raw algorithm validation.

OpenSARShip: [publication page](https://research.monash.edu/en/publications/opensarship-a-dataset-dedicated-to-sentinel-1-ship-interpretation/). Type: classification/display benchmark. Verified: 11,346 Sentinel-1 ship chips with AIS. Not image-formation correctness.

**5. Reference Parity And Numerical Stability Plan**

CPU references required next: matched-filter generation, range compression preserving complex phase, backprojection with interpolation, image metric utilities.

GPU parity tolerances: keep PR4 adapter tolerances as precedent: L∞ around `2e-5`, RMS/relative L2 around `1e-5` for tiny float fixtures, then loosen only with documented reason.

Numerical risks: **Needs review/high risk**. FFT scaling, magnitude-only compression, float accumulation, dynamic-range/log conversion, nearest-bin interpolation, and phase loss. Smallest exposing tests: one chirp echo matched-filter vector, one off-grid point target, one two-target sidelobe fixture.

**6. Recommended Roadmap**

PR5: matched-filter range-compression reference and metrics. Touch [SarCpuReference.hpp](/Users/rklinkhammer/workspace/GraphX/examples/SAR/include/sar/SarCpuReference.hpp:1), [RangeCompressionNode.cpp](/Users/rklinkhammer/workspace/GraphX/examples/SAR/src/RangeCompressionNode.cpp:1), tests, JSON presets, benchmark trace.

PR6: image-quality metrics and display artifacts: peak error, impulse width, PSLR, ISLR, contrast, entropy, log-magnitude PGM/PNG or CSV.

PR7: Gotcha normalized local fixture spike: do not ingest raw `.mat` into core GraphX yet; build a converter into normalized JSON fixtures first.

PR8: only after PR5-PR7, consider native Metal range compression or fuller backprojection kernels.

**7. Display And Artifact Plan**

CI: PGM magnitude, JSON metrics, trace JSON, metadata hash.

Local: PNG/log-magnitude writer, image comparison heatmap, HTML report with image, histogram, tile grid, benchmark table.

Future: trace/DAG viewer, phase image, range/azimuth cuts, tile boundary overlay, peak-vs-ground-truth overlay.

Current gap: [SarVisualizationSinkNode.cpp](/Users/rklinkhammer/workspace/GraphX/examples/SAR/src/SarVisualizationSinkNode.cpp:1) writes synthetic index ramps from token metadata, not actual image samples.

**8. Accel-Token Enforcement Checklist**

Existing: parser rejects legacy payload contracts; PR3 JSON metadata tests enforce generic intents; trace reports token lifecycle and `token_edge_payload_copies = 0`; sidecar preservation is tested.

Missing: per-edge token type schema in JSON, stronger tests for all sidecar fields, and clearer separation between CI shim token encoding and real sidecar metadata.

Parser gap: graph-level `edge_contract` exists, but per-edge input/output token types are not declared.

Resolver gap: generic H2D/D2H/DeviceKernel intents have token diagnostics; SAR-specific `SarBackprojectionTransformNode` remains a direct SAR adapter, though trace reports its concrete behavior.

**9. Measurement Plan**

Accuracy metrics: peak location error, L∞, RMS, relative L2, metadata hash.

Fidelity metrics: PSLR, ISLR, impulse response width, contrast, entropy, dynamic range.

GPU/DSP metrics: FFT time, matched-filter time, H2D/D2H bytes/sec, kernel elapsed time, dispatch count, queue id, overlap evidence.

Graph overhead metrics: build, run, lifecycle, join, baseline direct execution, provider/plugin lookup, token-edge copies.

Baseline method: continue graph-vs-direct, but add image/vector parity, not only diagnostics parity.

**10. Deterministic Validation And CI Tiers**

Tier 1 CI: token/view validity, sidecar preservation, parser/resolver contract.

Tier 2 CI: point target, matched-filter known vector, CPU backprojection metrics.

Tier 3 CI: JSON GraphExecutor pipeline parity vs direct baseline.

Tier 4 local/Metal CI: CPU vs Metal parity.

Tier 5 local: Gotcha/UAVSAR/Sentinel validation.

Tier 6 local: performance benchmark with trace and display artifacts.

All fixtures need seed, tolerances, expected metrics, and metadata hash.

**11. Concrete Next PR Proposal**

Title: `SAR PR5: matched-filter range-compression reference and image-quality metrics`.

Scope: add CPU matched-filter reference, preserve complex phase in reference path, add known-vector tests, add PSLR/ISLR/peak metrics, extend benchmark trace with image-quality fields, and validate through JSON/GraphExecutor configs.

Files to touch: [SarCpuReference.hpp](/Users/rklinkhammer/workspace/GraphX/examples/SAR/include/sar/SarCpuReference.hpp:1), [RangeCompressionNode.cpp](/Users/rklinkhammer/workspace/GraphX/examples/SAR/src/RangeCompressionNode.cpp:1), `examples/SAR/test/*`, `examples/SAR/config/*.json`, [sar_benchmark.cpp](/Users/rklinkhammer/workspace/GraphX/examples/SAR/src/sar_benchmark.cpp:1), and `plan/SAR_PR5_CHECKLIST.md`.

Acceptance: GraphExecutor JSON path passes; direct baseline parity passes; matched-filter known vector passes; PR4 CPU/Metal adapter parity remains green; trace separates algorithm/DSP/transfer/kernel/graph/diagnostics costs.

I did not implement changes or run tests; this was an inspection-only analysis pass.