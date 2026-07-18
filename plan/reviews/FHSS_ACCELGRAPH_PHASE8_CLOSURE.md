# FHSS AccelGraph Phase 8 Closure

## Scope

This note closes the FHSS AccelGraph work spanning Phases 1 through 7 and records the remaining documentation and verification boundaries.

Completed outcomes:

- checked-in JSON topology coverage for the FHSS accel slices
- CPU parity and backend diagnostics for downconverter, channelizer, detector, and branch-metric paths
- hybrid end-to-end evidence generation with a reproducible JSON artifact
- honest fallback and skip handling for unavailable Metal/CUDA lanes

## Final State

The current FHSS accel implementation is intentionally split between topology-driven integration tests and narrow parity/unit tests.

Topology-driven tests:

- use checked-in JSON topology files
- construct graphs through `GraphExecutorBuilder` and plugin descriptors
- rely on `node_config` plus `IConfigurable`
- do not manually wire `Configure(...)`, `ConfigureNode`, `ConfigureTransferNode`, or ad hoc `JsonView(...)` setup

Narrow parity/unit tests:

- directly configure local test fixtures with `Configure(...)`
- compare accelerator node behavior against libdsp reference nodes
- are not topology-style tests and are permitted to stay local to the parity surface

The narrow direct-configuration tests are:

- `libaccelgraph/test/unit/test_accelgraph_fhss_downconverter.cpp`
- `libaccelgraph/test/unit/test_accelgraph_fhss_channelizer.cpp`
- `libaccelgraph/test/unit/test_accelgraph_fhss_detector.cpp`
- `libaccelgraph/test/unit/test_accelgraph_fhss_branch_metric.cpp`

## Verification Matrix

Validated on macOS:

- `libaccelgraph_smoke`
- `libaccelgraph_smoke_discovery`
- FHSS evidence generation under `libaccelgraph/test/unit/test_accelgraph_fhss_phase7_evidence.cpp`

Observed result class coverage in the evidence artifact and local lane:

- `cpu_reference`
- `cpu_fallback`
- `strict_skipped`
- `build_or_backend_unavailable`
- `host_specific_pending` / `host_specific_cuda_pending` as contract placeholders when native verification is deferred to another host

## Evidence Artifacts

Primary artifact:

- `build/fhss_accelgraph_evidence.json`

Contract files:

- `verification/accelgraph/phase-7/benchmark_result.schema.json`

## Remaining Host-Specific Work

Jetson CUDA verification is still required for any claim that native CUDA execution is present on that host. The macOS lane only proves the local build, discovery, and evidence workflow.

The remaining Jetson check is straightforward:

- build the CUDA-enabled tree
- run the focused FHSS evidence and smoke filters
- confirm the artifact records native, fallback, or skipped states truthfully

## Closure Notes

The cleanup phase does not require a topology rewrite. The main invariant is already enforced where it matters: topology tests stay JSON-owned, while the local parity tests remain intentionally direct and narrowly scoped.