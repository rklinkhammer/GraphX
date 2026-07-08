# AccelGraph Phase 1 Jetson Verification Summary

- Phase verified: 1
- Verifier host: jetson-cuda (mars)
- Branch: codex/gpu-clean-restart
- Commit: 00d25c92e9ca620bccad7bd4f48fbe8a7f7275ea
- Imported results: none

## Result

Status: PASS

## Commands and Results

- `git pull --ff-only`: pass (already up to date)
- `cmake --preset ninja-debug-linux-host -DACCELGRAPH_ENABLE_METAL=OFF -DACCELGRAPH_ENABLE_CUDA=OFF`: pass
- `cmake --build --preset build-debug-linux-host --target test_libaccelgraph_smoke -- -j$(nproc)`: pass
- `ctest --test-dir build-ninja/ninja-debug-linux-host -R '^libaccelgraph_smoke$' --output-on-failure`: pass
- `ctest --preset test-libgraph-unit-linux-host --output-on-failure`: pass
- `cmake --preset ninja-debug-linux-host -DACCELGRAPH_ENABLE_METAL=OFF -DACCELGRAPH_ENABLE_CUDA=ON`: pass
- `cmake --build --preset build-debug-linux-host --target test_libaccelgraph_smoke -- -j$(nproc)`: pass
- `ctest --test-dir build-ninja/ninja-debug-linux-host -R '^libaccelgraph_smoke$' --output-on-failure`: pass
- `git diff --check`: pass
- `rg -n "void\\s*\\*|CU(stream|event|deviceptr)|MTL[A-Za-z]*|uint(32|64)_t\\s+(queue|event)|int\\s+(queue|event)" libaccelgraph/include/accelgraph -i`: pass (no matches)

## Host-Specific Status

- Jetson CPU lane: passed
- Jetson CUDA lane: passed
- macOS CPU lane: pending external verification
- macOS Metal lane: pending external verification

## Artifact

- jetson-cuda-20260708T223707Z.json
