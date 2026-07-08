# AccelGraph Phase 2 Jetson Verification Summary

- Phase verified: 2
- Verifier host: jetson-cuda (mars)
- Branch: codex/gpu-clean-restart
- Commit: 43381c00d0c842a73484f015939ee1c43b14181d
- Imported results: none

## Result

Status: PASS

## Commands and Results

- `git pull --ff-only`: pass (already up to date)
- `cmake --preset ninja-debug-linux-host -DACCELGRAPH_ENABLE_METAL=OFF -DACCELGRAPH_ENABLE_CUDA=OFF`: pass
- `cmake --build --preset build-debug-linux-host --target test_libaccelgraph_smoke -- -j$(nproc)`: pass
- `ctest --test-dir build-ninja/ninja-debug-linux-host -R '^libaccelgraph_smoke$' --output-on-failure`: pass
- `cmake --preset ninja-debug-linux-host -DACCELGRAPH_ENABLE_METAL=OFF -DACCELGRAPH_ENABLE_CUDA=ON`: pass
- `cmake --build --preset build-debug-linux-host --target test_libaccelgraph_smoke -- -j$(nproc)`: pass
- `ctest --test-dir build-ninja/ninja-debug-linux-host -R '^libaccelgraph_smoke$' --output-on-failure`: pass
- `git diff --check`: pass
- `rg -n "IMetal.*Capability|ICuda.*Capability|Metal.*Capability|Cuda.*Capability|cuda|metal" libaccelgraph/src/TransferGraphNodes.cpp libaccelgraph/include/accelgraph/TransferGraphNodes.hpp libaccelgraph/include/accelgraph/TransferGraphTypes.hpp -i`: pass (no matches)
- `rg -n "CudaHostToDeviceNode|MetalHostToDeviceNode|CudaDeviceToHostNode|MetalDeviceToHostNode|Cuda.*Ingress|Metal.*Ingress|Cuda.*Egress|Metal.*Egress|Cuda.*Lease|Metal.*Lease" libaccelgraph`: pass (no matches)

## Phase 2 Evidence

- Executed binary includes phase-2 topology checks:
  - `AccelGraphPhase2TransferNodesTest.CpuTransferTopologyRoundTripAndRelease`
  - `AccelGraphPhase2TransferNodesTest.PluginDiscoveryAndRoundTripViaProvider`

## Host-Specific Status

- Jetson CPU lane: passed
- Jetson CUDA lane: passed
- macOS CPU lane: pending external verification
- macOS Metal lane: pending external verification

## Artifact

- jetson-cuda-20260708T230838Z.json
