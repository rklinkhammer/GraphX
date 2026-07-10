# AccelGraph Synthetic Topology Testing

This directory contains the synthetic topology regression suite for accelgraph.

## Topology JSON Location

Checked-in topology configs live in:
- `libaccelgraph/test/config/topologies/*.json`

These files are the authoritative source of node initialization for topology tests.

## Required Node Configuration Contract

Each topology node must define a `node_config` object.

Accepted keys in `node_config` must be declared by node descriptor metadata (`Fields()`) on the corresponding accelgraph node type. Unknown keys must fail loader validation.

When adding new configurable node behavior:
1. Add/adjust descriptor field metadata in node `Fields()`.
2. Use those keys only through topology JSON `node_config`.
3. Validate with topology contract tests in `test_accelgraph_phase2_topology_contract.cpp`.

## How Topology Tests Must Execute

Topology tests should load and execute through:
- `GraphExecutorBuilder().WithJsonConfig(...)`
- plugin descriptors from the configured plugin output directory
- loader-driven `IConfigurable` application

The shared helper for topology mechanics is:
- `libaccelgraph/test/unit/AccelGraphTopologyTestUtils.hpp`

Use this helper for common build/path/diagnostic mechanics while keeping suite assertions local and explicit.

## Forbidden Test Patterns

Do not add direct/manual configuration bypasses in topology tests:
- `Configure(...)`
- `ConfigureNode(...)`
- `ConfigureTransferNode(...)`
- ad hoc `JsonView(...)` setup in tests

Topology tests are graph-level tests; direct node configuration belongs only in direct node unit tests.

## Backend Diagnostics and Fallback

Keep backend diagnostics actionable and explicit:
- Metal unavailable diagnostics should map to known Metal runtime/build diagnostics.
- CUDA unavailable diagnostics should map to known CUDA runtime/build diagnostics.
- strict-vs-allow fallback behavior must remain asserted by topology suites.

## Standard Test Inclusion

Synthetic topology tests are compiled into:
- `test_libaccelgraph_smoke`

CTest registration should include:
- `libaccelgraph_smoke`

CUDA-only topology suites are expected to compile out or skip cleanly on non-CUDA builds.
