# AccelGraph Phase 7 Benchmarking and Replacement Decision

## Scope

Phase 7 was executed after:
- Phase 6 CPU + Metal correctness passed on macOS.
- Phase 6B CUDA correctness was available via imported Jetson verification artifact.

This phase adds performance benchmarking for the greenfield spectrum graph and records replacement readiness signals.

## Artifacts

- Benchmark schema: `verification/accelgraph/phase-7/benchmark_result.schema.json`
- Benchmark run: `verification/accelgraph/phase-7/macos-jetson-matrix-20260709T030416Z.json`
- Imported correctness artifact (Jetson CUDA): `verification/accelgraph/phase-6b/jetson-cuda-20260709T024817Z.json`
- Jetson import runbook: `verification/accelgraph/phase-7/JETSON_IMPORT_RUNBOOK.md`

## Benchmark Configurations Added

- `libaccelgraph/test/config/benchmarks/accelgraph_phase7_spectrum_cpu_macos.json`
- `libaccelgraph/test/config/benchmarks/accelgraph_phase7_spectrum_metal_macos.json`
- `libaccelgraph/test/config/benchmarks/accelgraph_phase7_spectrum_cuda_jetson.json`

## Benchmark Runner Added

- `libaccelgraph/test/bench/accelgraph_phase7_benchmark.cpp`
- CMake target: `accelgraph_phase7_benchmark`

The runner emits required fields for backend/mode/config/packet/frame/warmup timing, throughput, latency, graph overhead, fallback metadata, parity status, host class, and local-vs-imported origin.

Importer behavior (updated):
- If `imported_artifact` points to a valid phase-7 schema artifact with a matching CUDA row, the row is validated and merged directly (timing/throughput/allocation fields included as available).
- If phase-7 artifact is missing or lacks a matching CUDA row, output remains pending for CUDA lane.
- If a phase-6b verifier artifact is provided instead, correctness-only import fallback is used.

## Results (Current Invocation)

From `macos-jetson-matrix-20260709T030416Z.json`:

1. CPU-only (local, backend=cpu)
- packet_size: 256
- frame_count: 3 (warmup: 1)
- total_elapsed_time_ms: 80497.840542
- steady_state_elapsed_time_ms: 60202.824458
- frames_per_second: 0.0498315491
- samples_per_second: 12.7568765571
- latency_ms: 20067.608153
- cold_frame_ms: 20069.648875
- warm_frame_ms: 20066.587792
- graph_overhead_ms: 20064.102292
- legacy_reference_baseline_ms: 1006.339014
- correctness_parity_status: pass

2. macOS Metal (local, backend=metal)
- packet_size: 256
- frame_count: 3 (warmup: 1)
- total_elapsed_time_ms: 80314.017459
- steady_state_elapsed_time_ms: 60198.961792
- frames_per_second: 0.0498347465
- samples_per_second: 12.7576951020
- transfer_inclusive_gpu_time_ms: 20066.320597
- compute_only_gpu_time_ms: not available
- latency_ms: 20066.320597
- cold_frame_ms: 20064.742875
- warm_frame_ms: 20067.109459
- graph_overhead_ms: 20063.507764
- cpu_gpu_speed_ratio: 1.0000641650
- legacy_reference_baseline_ms: 1007.478917
- correctness_parity_status: pass

3. Jetson CUDA (imported)
- host_class: jetson-cuda
- measurement_origin.imported: true
- correctness_parity_status: pass:imported-correctness
- transfer_inclusive_gpu_time_ms: pending benchmark import
- compute_only_gpu_time_ms: pending benchmark import
- allocation_count/bytes: pending benchmark import

## Replacement Recommendation

Decision: NOT READY to replace libgpu pieces yet.

Rationale:
- Correctness and parity are passing for local CPU/Metal and imported Jetson CUDA correctness.
- Current measured throughput/latency for greenfield graph execution is dominated by lifecycle/graph overhead (~20s per frame in this invocation), with CPU and Metal effectively equal (cpu_gpu_speed_ratio ~1.0), so no demonstrated acceleration benefit yet.
- Jetson CUDA benchmark performance metrics are not yet present in phase-7 schema output (only correctness import exists).
- Allocation telemetry and compute-only GPU telemetry are currently unavailable in these outputs.

Required before replacement approval:
- Collect native Jetson CUDA phase-7 benchmark using this phase-7 schema.
- Add compute-only and allocation telemetry plumbing where supported.
- Reduce graph/lifecycle overhead to avoid timeout-scale per-frame cost.
