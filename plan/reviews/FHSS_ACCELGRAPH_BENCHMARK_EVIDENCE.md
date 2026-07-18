# FHSS AccelGraph Benchmark Evidence

## Scope

This workflow records reproducible FHSS benchmark/evidence artifacts for the accelerator-aware libaccelgraph pipeline without changing algorithm behavior.

Covered phases:

1. Phase 2 downconverter
2. Phase 3 channelizer
3. Phase 4 per-channel pulse detector
4. Phase 5 branch metric
5. Phase 6 end-to-end hybrid topology

The evidence runner is implemented as smoke-side tests in `libaccelgraph/test/unit/test_accelgraph_fhss_phase7_evidence.cpp` and writes a JSON artifact under `build/`.

## Artifact

Default output path:

- `build/fhss_accelgraph_evidence.json`

Artifact schema:

- `graphx.accelgraph.phase7.fhss.evidence.v1`

Each result row records:

- git revision
- host backend / feature flags
- topology file
- stage name
- explicit outcome classification for native, fallback, strict-skip, and unavailable paths
- strict vs allow-fallback mode
- requested backend
- selected backend
- whether native GPU execution occurred
- fallback stages and diagnostics
- pass / skip status
- timing summary
- key output validation summary

## macOS Run

Build the smoke binary first, then run the focused FHSS evidence filter:

```bash
./build/libaccelgraph/test/test_libaccelgraph_smoke \
  --gtest_filter='*Fhss*:*FHSS*:*Benchmark*:*Evidence*:*E2E*:*Hybrid*' \
  --gtest_brief=1
```

Then run the smoke and discovery CTest targets:

```bash
ctest --test-dir build -R "libaccelgraph_smoke|libaccelgraph_smoke_discovery" --output-on-failure
```

If the workspace uses a different build directory, substitute the active build tree path.

## Jetson CUDA Run

Use the same evidence test filter in the Jetson CUDA-enabled build tree:

```bash
<jetson-build-dir>/libaccelgraph/test/test_libaccelgraph_smoke \
  --gtest_filter='*Fhss*:*FHSS*:*Benchmark*:*Evidence*:*E2E*:*Hybrid*' \
  --gtest_brief=1
```

The report must remain honest about backend status:

- strict CUDA rows should skip or fail clearly when native implementation is unavailable
- allow-fallback rows must record the actual fallback stage(s)
- native GPU execution must not be claimed when fallback occurred

## Expected Current CUDA Posture

The latest Jetson phase reports show:

- Phase 2 transfer nodes: CPU-native, CUDA backend-neutral / not required in the earlier phase report
- Phase 3 Metal provider lane: implemented on macOS, CUDA still not native in the Jetson report set
- Phase 4 CUDA shell diagnostics: pass, but not a native device kernel path
- Phase 5 CUDA graph executor: native CUDA path passed in Jetson verification
- Phase 6 end-to-end hybrid CUDA: intentionally not claimed as native e2e support yet

This workflow preserves that posture by recording skip/fallback/native honestly in the evidence rows.

## Suggested Verification Prompt

For Jetson Phase 7 validation, run the FHSS evidence filter and inspect the generated JSON artifact for:

1. CPU baseline rows for phases 2-6
2. Metal rows showing either native Metal execution or explicit fallback diagnostics
3. CUDA rows showing either native CUDA execution or explicit fallback / skip diagnostics
4. Phase 6 hybrid rows preserving the unsupported native-CUDA-e2e truthfulness

Report the exact topology file, requested backend, selected backend, fallback stages, and the artifact path.