# CRSD To Focused Image Repository Discovery

## Scope

This document records repository discovery for CRSD source-node and focused-image output patterns.
It is discovery only: no code changes, no tests, no runtime config changes, and no dependency changes.

MATLAB is not used by this workflow and must not become a build-time, runtime, or test-time dependency.

## Executive Findings

- Current SAR graph construction is plugin-driven and JSON-configured under examples/SAR.
- Current SAR source-node conventions are node-local configuration in node_config (not GraphExecutorBuilder CSV injection).
- Existing CRSD writing and validation hooks are present and already separated from core runtime contracts.
- SarPy tooling is explicitly local-only and gated; CI-safe lanes avoid requiring SarPy and real GOTCHA data.
- Generated CRSD products are one ordered aperture set intended to form one final focused SAR image, not one final image per CRSD file.

## SAR Node, Plugin, and Config Conventions

### Source-node conventions

Observed repository patterns:

- Synthetic source node is a NamedSourceNode over SarAccelControlToken:
  - examples/SAR/include/sar/SyntheticApertureIqSourceNode.hpp
- GOTCHA replay source node is also a NamedSourceNode over SarAccelControlToken:
  - examples/SAR/include/sar/GotchaReplaySourceNode.hpp
- Source nodes consume node_config via Configure and expose parameter metadata via GetParameters/GetParameterDescription/GetParameterNames:
  - examples/SAR/src/SyntheticApertureIqSourceNode.cpp
  - examples/SAR/src/GotchaReplaySourceNode.cpp

JSON topology files consistently configure nodes with type plus node_config:

- examples/SAR/config/sar_stripmap_simulated.json
- examples/SAR/config/sar_stripmap_definitive.json
- examples/SAR/config/sar_gotcha_external_manual.json

### Plugin registration conventions

Observed plugin conventions:

- SAR plugins use NodePluginTemplate/PluginGlue wrappers:
  - examples/SAR/plugins/synthetic_aperture_iq_source_node_plugin.cpp
  - examples/SAR/plugins/gotcha_replay_source_node_plugin.cpp
- Plugin inventory and wiring are centralized in:
  - examples/SAR/plugins/CMakeLists.txt
- Plugin runtime loading is configured through plugin directories in main/benchmark:
  - examples/SAR/src/main.cpp
  - examples/SAR/src/sar_benchmark.cpp

Implication for PR2+:

- OrderedCrsdSetInputSourceNode should follow this same pattern:
  - source node class in examples/SAR/include and examples/SAR/src
  - plugin wrapper in examples/SAR/plugins
  - plugin target added in examples/SAR/plugins/CMakeLists.txt
  - JSON graph type + node_config usage

## CSV Injection Patterns Versus SAR Source Patterns

### CSV injection (libgraph, policy/capability path)

Observed CSV path is GraphExecutorBuilder-policy driven:

- GraphExecutorBuilder CSV entry points:
  - libgraph/include/graph/GraphExecutorBuilder.hpp
  - libgraph/src/graph/GraphExecutorBuilder.cpp
- CSV policy and manager:
  - libgraph/include/policies/CSVInjectionPolicy.hpp
  - libgraph/include/csv/CSVDataInjectionManager.hpp
  - libgraph/src/csv/CSVDataInjectionManager.cpp
- Capability/interface basis:
  - libgraph/include/graph/IDataInjectionSource.hpp
  - libgraph/include/policies/DataInjectionPolicy.hpp

This path is designed for IDataInjectionSource capability discovery and row injection policy execution.

### SAR source-node path (examples/SAR, node-local JSON)

Observed SAR path is plugin + node_config driven and node-local:

- Source nodes are explicit SAR node classes that emit SarAccelControlToken.
- Graph topology selects source type and parameters directly in node_config.

### Discovery conclusion

CSV injection and SAR source-node initialization are different architectural paths.
For CRSD-focused ingestion, the repository conventions favor a JSON-configured OrderedCrsdSetInputSourceNode-first approach rather than adapting CSV injection policy machinery.

## Justification: OrderedCrsdSetInputSourceNode-First

The repository evidence supports OrderedCrsdSetInputSourceNode as the first implementation step because:

- Existing SAR source nodes already define the expected source contract shape (NamedSourceNode, token emission, node_config, plugin wrapper).
- Existing SAR JSON config files and plugin loader behavior are stable and already tested.
- CSV injection is oriented to generic IDataInjectionSource policy behavior, not SAR tokenized aperture-source semantics.
- CRSD ingestion needs ordered-set semantics and diagnostics (ordering, missing segments, duplicated segments, per-segment accounting) that align with a dedicated SAR source node contract.

Planned CRSD source config modes should follow current SAR config style:

- explicit ordered crsd_paths list
- crsd_directory discovery mode
- manifest-driven order mode

## Existing SAR Fixture and Test Conventions

Observed SAR test conventions:

- Unified SAR test binary:
  - examples/SAR/test/CMakeLists.txt (test_sar_example_unit)
- Lanes split by CTest filters and labels:
  - sar_example_unit
  - sar_example_ci_lane
  - sar_example_sarpy_probe_lane (labeled local-only and gated)
  - sar_example_sarpy_integration_lane (labeled local-only and gated)
  - sar_real_gotcha_local_validation (disabled by default)
- Fixture/scenario paths are injected by compile definitions in test CMake.

Implication for PR1 planning:

- CRSD source-node tests should be integrated into existing SAR GTest/CTest lane conventions.
- CI-safe deterministic fixtures should remain the default path.
- Real-data and SarPy-required checks should remain local-only and opt-in.

## CRSD Writer, Validator, and Reference Tool Hooks

### CRSD writer hooks

Observed writer contract:

- C++ CRSD writer interface and artifact constants:
  - examples/SAR/include/sar/io/CrsdIO.hpp
- Output contract includes:
  - product.crsd
  - metadata.json
  - pvp.json
  - provenance.json
  - chunk_index.json
- Writer invokes Python writer script via anchored or override path:
  - tools/sarpy/write_crsd_from_graphx_product.py

### CRSD validation hooks

Observed validation path:

- CRSD conversion CLI performs optional environment probe and validation command checks through:
  - examples/SAR/src/graphx_gotcha_to_crsd.cpp
- Validation tool path resolves to:
  - tools/sarpy/validate_crsd.py

### Reference/comparison hooks

Observed local tooling:

- CRSD validation probe/validate:
  - tools/sarpy/validate_crsd.py
- Reference extraction from CRSD:
  - tools/sarpy/reference_image_from_crsd.py
- Image comparison reporting:
  - tools/sarpy/compare_images.py

## Local-Only SarPy Boundaries

Observed local-only boundary evidence:

- README labels SarPy CRSD checks as optional local-only and gated:
  - README.md
- SarPy harness tests explicitly assert local_only and ci_safe=false behavior:
  - examples/SAR/test/test_sarpy_crsd_validation_harness.cpp
- Real GOTCHA validation tests skip unless dataset environment variables are set:
  - examples/SAR/test/test_gotcha_real_full_aperture_validation.cpp

Boundary conclusion:

- SarPy remains an optional local validation/reference toolchain.
- SarPy is not a GraphX core runtime dependency and must remain outside mandatory CI requirements.

## Ordered Aperture-Set Semantics (Explicit)

The generated CRSD outputs represent one ordered aperture set for one final focused SAR image.
They are not one-final-image-per-CRSD-file products.

This aligns with the current planner intent and with the output structure where segment CRSD products are ordered parts of one logical aperture.

## MATLAB Boundary (Explicit)

MATLAB is not used and must not become a dependency in this workflow.

GOTCHA MAT files are inputs to C++/tooling conversion paths, but MATLAB itself is out of scope for build/runtime/test dependency graphs.

## PR1 Outcome for Follow-On Work

Repository discovery confirms the implementation baseline for PR2+:

- Use OrderedCrsdSetInputSourceNode in SAR plugin + JSON config conventions.
- Keep CRSD source semantics ordered and deterministic at source-node boundary.
- Preserve local-only SarPy boundaries.
- Preserve no-MATLAB dependency policy.
- Treat ordered CRSD segment sets as one aperture feeding one focused-image result.
