# AccelGraph Phase 8 Controlled Integration Plan (Gate Not Met)

## Phase Attempted

- Phase 8: Controlled integration back into GraphX.
- Host invocation: macOS.

## Prerequisite Gate Decision

Decision: prerequisite NOT SATISFIED in current repository state.

Required gate from prompt:
- "Only after the greenfield package proves replacement value" may Phase 8 delete legacy surfaces.

Evidence reviewed:
- `verification/accelgraph/phase-7/summary.md` states replacement recommendation: **NOT READY**.
- `verification/accelgraph/phase-7/macos-jetson-matrix-default-import-20260709T034459Z.json` includes imported CUDA benchmark row, but CPU/Metal/CUDA timings remain near-parity with high graph overhead and no demonstrated acceleration replacement benefit.
- `verification/accelgraph/phase-7/jetson-cuda-20260709T032648Z.json` confirms CUDA benchmark artifact exists, but does not change Phase 7 recommendation in-repo.

Result:
- No deletion of legacy `libgpu` surfaces is allowed in this Phase 8 invocation.

## Blockers

1. Replacement value gate is explicitly negative in current Phase 7 summary.
2. Throughput/latency data does not yet demonstrate greenfield acceleration advantage versus current baseline in the same benchmark family.
3. Graph/lifecycle overhead remains dominant in reported measurements, preventing a replacement-value claim.

## Candidate First Deletion Slice (When Gate Passes)

Chosen candidate (one slice only):
- Slice 1: old GPU capability bootstrap surfaces.

Why this slice first:
- Narrow blast radius compared with transfer or algorithm node deletions.
- Avoids premature removal of data-path nodes while performance/value proof is still being hardened.
- Aligns with "one deletion slice at a time" and no permanent dual registration state.

## Exact Readiness Gates Before Any Deletion

All gates below must be satisfied together before deleting legacy surfaces:

1. Phase 7 replacement decision updated to READY in `verification/accelgraph/phase-7/summary.md`, with explicit rationale tied to benchmark outputs.
2. Phase 7 schema artifacts contain complete lane evidence:
   - macOS CPU local benchmark row,
   - macOS Metal local benchmark row,
   - Jetson CUDA benchmark row (measured locally on Jetson or imported phase-7 schema artifact with matching branch/commit identity).
3. Correctness parity status remains pass across the benchmark family for all required lanes.
4. No compatibility adapters/shims/wrappers/aliases are introduced to bridge old/new APIs during deletion.
5. Deletion patch is restricted to one legacy surface slice and preserves unrelated repository changes.

## Verifier Commands To Run Next

Run from repository root unless noted.

### macOS verifier lane

1. Validate replacement-decision artifact still blocks deletion until READY:
- `rg -n "Replacement Recommendation|NOT READY|READY" verification/accelgraph/phase-7/summary.md`

2. Confirm Phase 7 matrix contains all lane rows and import metadata:
- `rg -n "\"backend\": \"(cpu|metal|cuda)\"|\"measurement_origin\"|\"imported\"" verification/accelgraph/phase-7/macos-jetson-matrix-default-import-20260709T034459Z.json`

3. Ensure no Phase 8 legacy deletions happened in this slice:
- `git diff -- libgpu CMakeLists.txt libaccelgraph verification/accelgraph/phase-8`

4. Hygiene check:
- `git diff --check`

### Jetson verifier lane (pending external verification)

1. Confirm imported CUDA artifact identity and lane completeness:
- `rg -n "\"phase\": \"7\"|\"backend\": \"cuda\"|\"execution_mode\": \"local\"|\"correctness_parity_status\"" verification/accelgraph/phase-7/jetson-cuda-20260709T032648Z.json`

2. Re-run Jetson phase-7 benchmark generation (local reproduction path):
- `cmake --build build-ninja/ninja-debug-linux-host --target accelgraph_phase7_benchmark -- -j$(nproc)`
- `build-ninja/ninja-debug-linux-host/libaccelgraph/test/accelgraph_phase7_benchmark --config=libaccelgraph/test/config/benchmarks/accelgraph_phase7_spectrum_cuda_jetson.json --frames=24 --warmup=4 --output=verification/accelgraph/phase-7/jetson-cuda-latest.json`

3. Verify no accidental compatibility layer appears in integration changes:
- `rg -n "adapter|shim|compat|wrapper|alias" libaccelgraph libgpu`

4. Hygiene check:
- `git diff --check`

## Planned Next Phase 8 Action Once Gates Pass

- Execute exactly one deletion slice: remove old GPU capability bootstrap surfaces.
- In same slice, remove corresponding stale registration paths and tests that target deleted bootstrap surfaces.
- Verify no dual registration remains and no compatibility layer is introduced.
- Stop immediately after this single slice and hand off for verifier pass.
