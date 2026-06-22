**Executive Recommendation**

Best next PR: add a deterministic CPU reference SAR correctness harness before further GPU expansion. Evidence: **Observed in code**. The current PR3 graph and accel-token path are much stronger than the SAR math path: graph config rejects legacy SAR payload contracts under `edge_contract: "accel-token"` in [GraphConfigParser.cpp](/Users/rklinkhammer/workspace/GraphX/libgraph/src/graph/GraphConfigParser.cpp:565), benchmarks separate graph run/lifecycle/baseline timing in [sar_benchmark.cpp](/Users/rklinkhammer/workspace/GraphX/examples/SAR/src/sar_benchmark.cpp:914), and sidecar survival is tested in [test_sar_accel_nodes.cpp](/Users/rklinkhammer/workspace/GraphX/examples/SAR/test/test_sar_accel_nodes.cpp:243). The weakest foundation is physical SAR correctness and CPU-vs-GPU parity.

What not to do next: do not add more Metal kernels until there is a scalar CPU reference, point-target fixture, and numeric tolerance gate. Evidence: **External SAR best practice + Observed in code**.

**Current-State Findings**

Accuracy/fidelity: **Observed in code**. `SyntheticApertureIqSourceNode` generates deterministic synthetic IQ using simple sequence/sample formulas plus optional moving-target perturbation, not physical phase history with carrier, chirp, platform geometry, or range delay equations [SyntheticApertureIqSourceNode.cpp](/Users/rklinkhammer/workspace/GraphX/examples/SAR/src/SyntheticApertureIqSourceNode.cpp:220).

Performance: **Observed in code**. Benchmark reporting now separates graph run, lifecycle teardown, baseline execution, transfer/kernel proxy timings, and token-vs-transfer-payload attribution [sar_benchmark.cpp](/Users/rklinkhammer/workspace/GraphX/examples/SAR/src/sar_benchmark.cpp:904). That is good enough to prevent the earlier “graph execute includes join teardown” confusion.

Architecture/token contract: **Observed in code**. PR3 JSON uses graph-level `edge_contract: "accel-token"` and generic node intents; parser validation rejects legacy SAR payload contracts when that edge contract is active. However, per-edge token types are not explicitly declared in the JSON, so enforcement is graph-level plus resolver convention, not full edge-local schema proof.

Test/display/validation: **Observed in code**. Tests cover deterministic diagnostics, dynamic plugin loading, token/view validity, sidecar preservation, JSON execution, and trace schema. They do not yet prove SAR image formation correctness with known targets, image metrics, or CPU-vs-GPU numerical parity.

**Mathematical Correctness Audit**

Source: **Symbolic/Approximation**. Deterministic samples, optional range/velocity-inspired perturbation; missing carrier phase model, chirp bandwidth use, sample frequency axis, antenna coordinates, and scene coordinate frame.

Range window: **Approximation but mathematically meaningful**. Hann window and gain are real DSP operations in [RangeWindowNode.cpp](/Users/rklinkhammer/workspace/GraphX/examples/SAR/src/RangeWindowNode.cpp:18), but there is no sidelobe/PSLR/ISLR validation.

Range compression: **Approximation**. Uses `dsp::FFTManager` with Hann window and writes magnitude-only real samples [RangeCompressionNode.cpp](/Users/rklinkhammer/workspace/GraphX/examples/SAR/src/RangeCompressionNode.cpp:29). Missing matched-filter generation, chirp replica, complex phase preservation, FFT scaling audit, and known-vector test.

Tile split/fanout: **Symbolic for SAR semantics, real for DAG semantics**. Token identity encodes marker/tile/sequence/bytes/stream into `host_ptr` [AzimuthTileSplitNode.cpp](/Users/rklinkhammer/workspace/GraphX/examples/SAR/src/AzimuthTileSplitNode.cpp:19). `ResolveTileId` still supports sequence-modulo tiling [AzimuthTileSplitNode.cpp](/Users/rklinkhammer/workspace/GraphX/examples/SAR/src/AzimuthTileSplitNode.cpp:183).

Backprojection: **Placeholder/Approximation**. The inline Metal kernel is a delay/tap/window/phasor accumulation over a 1D range tile [SarBackprojectionTransformAccelNode.cpp](/Users/rklinkhammer/workspace/GraphX/examples/SAR/src/SarBackprojectionTransformAccelNode.cpp:30). Missing physical geometry, slant range, carrier phase correction, interpolation tied to range bins, 2D image coordinates, and accumulation precision policy.

Merge/diagnostics: **Physically symbolic but architecturally meaningful**. Merge validates tile completeness and diagnostics, not image accumulation correctness.

**External Validation Data Matrix**

- [AFRL Gotcha](https://www.sdms.afrl.af.mil/index.php?collection=gotcha): **Known/Requires download**. Phase-history validation source. AFRL page says X-band, 640 MHz bandwidth, MATLAB `.mat`, k-space, frequencies, antenna x/y/z, range-to-scene-center, azimuth/elevation. Best first real phase-history target.
- [AFRL Wide Angle SAR](https://www.sdms.afrl.af.mil/index.php?collection=wide_angle_sar): **Known/Requires download**. Phase-history/classification stress source; circular SAR, 31 orbits, target extracts and reflectors.
- MSTAR via [AFRL SDMS](https://www.sdms.afrl.af.mil): **Known/Requires download/Requires experiment**. Best for image-domain target sanity, not primary backprojection unless phase history is available.
- [NASA/JPL UAVSAR](https://uavsar.jpl.nasa.gov/): **Known/Requires experiment**. Good image/geospatial metadata validation; not first choice for custom backprojection.
- [Sentinel-1 via ASF/Earthdata](https://www.earthdata.nasa.gov/data/platforms/space-based-platforms/sentinel-1): **Known/Requires download**. Image-product/SLC/geospatial workflow validation; ASF provides Vertex/API access.
- [Sandia SAR imagery](https://www.sandia.gov/radar/pathfinder-radar-isr-and-synthetic-aperture-radar-sar-systems/archive-imagery-clone-2-clone-2/): **Known/Unknown raw data**. Documentation/display reference unless raw phase history is confirmed.
- [OpenSARShip](https://research.monash.edu/en/publications/opensarship-a-dataset-dedicated-to-sentinel-1-ship-interpretation/): **Known/Requires download**. Classification/display validation; not image-formation correctness.

**Reference Parity And Numerical Stability**

Required next: **Observed gap + External SAR best practice**. Add CPU scalar reference for point-target generation, range compression, and backprojection. Report L-infinity, RMS, relative error, peak-location error, and simple PSLR/ISLR where feasible.

Stability risks: **Needs review**. Current math uses `float`, magnitude-only compression, float accumulation, and no scaling audit. High-risk areas are FFT scaling, dynamic range/log display, phase loss in range compression, and backprojection accumulation precision.

**Roadmap**

PR-A, highest leverage: deterministic point-target CPU reference harness under `examples/SAR/test` plus tiny fixture and image metric utilities. Acceptance: CPU scalar path produces known peak location and stable metrics.

PR-B: make RangeCompressionNode physically meaningful enough for a known chirp/matched-filter vector. Acceptance: fixed known-vector test with tolerance, FFT scaling documented.

PR-C: add CPU-vs-Metal parity for the current `DeviceKernelNodeMetal` SAR adapter. Acceptance: native path compared against CPU scalar reference, not another GPU path.

PR-D: external Gotcha normalized fixture path. The existing Gotcha adapter validates a normalized JSON schema [GotchaReplaySourceNode.cpp](/Users/rklinkhammer/workspace/GraphX/examples/SAR/src/GotchaReplaySourceNode.cpp:102), but it is not yet raw AFRL `.mat` ingestion.

**Display And Artifact Plan**

CI: PGM/PPM magnitude image, JSON metrics, trace JSON, and golden metadata hash.

Local: PNG/log-magnitude writer, image comparison heatmap, HTML report with image, histogram, tile grid, and benchmark table.

Future: simple trace/DAG viewer and SAR-specific views: phase image, range/azimuth cuts, tile boundaries, peak/ground-truth overlay.

**Accel-Token Checklist**

Good: graph-level accel-token metadata exists in PR3 JSON; parser rejects legacy SAR payload contracts; resolver knows H2D/D2H/DeviceKernel token types; traces report token lifecycle and `token_edge_payload_copies = 0` [sar_benchmark.cpp](/Users/rklinkhammer/workspace/GraphX/examples/SAR/src/sar_benchmark.cpp:1126).

Gaps: sidecar identity is currently packed into pointer tokens in CI shims, which couples metadata and token identity; JSON does not declare per-edge input/output token types; stale unbuilt legacy tests still reference `SarRangeTileMessage`, while [test/CMakeLists.txt](/Users/rklinkhammer/workspace/GraphX/examples/SAR/test/CMakeLists.txt:10) excludes them.

**Measurement Plan**

Keep current graph build/run/lifecycle/baseline separation. Add algorithm timing buckets for source, range compression, backprojection CPU reference, H2D, kernel, D2H, merge, diagnostics. For GPU metrics, add bytes/sec, kernel dispatch count, kernel elapsed time, queue overlap/utilization, and CPU-vs-GPU parity error.

**Deterministic Validation Tiers**

Tier 1 CI: token/view/sidecar/unit validation.  
Tier 2 CI: point-target CPU algorithm tests.  
Tier 3 CI: graph integration parity vs direct baseline.  
Tier 4 local/CI-stub: CPU vs GPU parity, with Metal local where available.  
Tier 5 local: Gotcha/Sentinel/UAVSAR dataset validation.  
Tier 6 local: performance benchmark with graph-overhead attribution.

**Concrete Next PR**

Title: `SAR CPU reference correctness harness and point-target validation`.

Scope: add deterministic point-target fixture, CPU scalar backprojection/reference metrics, update benchmark trace with algorithm-cost buckets, and remove or convert stale legacy `SarRangeTileMessage` tests.

Tests: point peak location, RMS/L-infinity metrics, range-compression known vector if small enough, graph-vs-direct output metric parity, sidecar hash preservation.

I did not run the test suite, per the analysis-only request.