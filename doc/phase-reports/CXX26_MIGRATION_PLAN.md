# C++26 Migration Plan

**Date:** May 31, 2026  
**Scope:** Move GraphX from C++20/23-compatible patterns to a C++26-first codebase, accepting intentional breaking changes where they reduce complexity, improve correctness, or unlock compile-time generation.

---

## Executive Summary

GraphX already uses several modern features such as `std::expected`, `std::optional`, and reflection-oriented design patterns. The next step is not to add more compatibility layers. It is to make C++26 the source of truth and remove legacy structures that exist only to preserve older language support.

The best migration strategy is a **phased, subsystem-first conversion** with explicit breakpoints:

1. **Freeze compatibility scope** - stop adding C++20/23 fallback paths.
2. **Move metadata and schema generation to reflection** - eliminate hand-maintained duplication.
3. **Adopt compile-time node descriptors** - reduce runtime registration and helper code.
4. **Modernize test generation and validation** - use reflection/concepts to reduce boilerplate.
5. **Introduce modules selectively** - once APIs are stable.
6. **Remove compatibility shims** - after coverage proves the C++26 path is complete.

---

## Migration Goals

### Primary Goals
- Make C++26 the baseline for all core GraphX libraries.
- Remove duplicate metadata, schema, and port-description logic.
- Prefer compile-time generation over runtime introspection where practical.
- Reduce boilerplate in node, plugin, and schema code.
- Improve compile times and maintainability by removing older abstraction layers.

### Secondary Goals
- Simplify test suites by generating validation coverage from reflection metadata.
- Improve API clarity by collapsing legacy wrappers.
- Prepare the codebase for module-based builds in later phases.

---

## What Can Break

These changes are intentionally allowed to break C++20/23 consumers:

- Remove pre-C++26 compatibility fallbacks.
- Replace manual node/port metadata with reflection-driven descriptors.
- Remove legacy adapter layers whose only purpose is backwards compatibility.
- Require C++26-capable compilers and standard libraries.
- Change build structure to support modules and stricter compile-time contracts.
- Replace runtime discovery paths with compile-time validated equivalents where feasible.

---

## Recommended Migration Order

### Phase 1 - Baseline and Inventory
**Goal:** Identify all places where the codebase still carries compatibility overhead.

Tasks:
- Inventory all reflection, schema, and metadata helpers.
- Identify duplicate port and node descriptions.
- Catalog runtime-only registration paths.
- Identify tests that should become generated or parameterized from descriptors.
- Document APIs that exist solely to support older compilers or fallback flows.

Exit criteria:
- A complete list of compatibility shims and metadata duplication points.
- A clear decision on which public APIs are staying and which are being removed.

---

### Phase 2 - Reflection as the Source of Truth
**Goal:** Replace hand-written metadata with reflection-generated metadata.

Targets:
- Node port descriptions
- Type/schema descriptors
- Plugin capability metadata
- JSON/schema generation paths
- Port validation and introspection helpers

Likely affected areas:
- `graph/Reflection.hpp`
- `config/SchemaGenerator.hpp`
- `core/ReflectionHelper.hpp`
- `PluginReflection`-style helpers

Expected changes:
- Port definitions become compile-time discoverable.
- Schema generation is derived from reflected fields rather than duplicate declarations.
- Runtime metadata containers become thinner or disappear entirely.

Exit criteria:
- Reflection produces the canonical metadata for nodes and schemas.
- Hand-maintained metadata tables are removed or reduced to a compatibility layer.

---

### Phase 3 - Compile-Time Node Contracts
**Goal:** Move node validation and descriptors into compile-time constraints.

Tasks:
- Replace runtime shape checks with concepts or compile-time predicates.
- Encode input/output port counts and types in a single descriptor path.
- Simplify NodeFactory validation by relying on compile-time contracts for internal nodes.
- Use stronger compile-time checks in test nodes and built-in adapters.

Potential benefits:
- Fewer runtime checks.
- Better error messages at compile time.
- Less duplicated validation logic.

Exit criteria:
- Core node types are validated by compile-time constraints instead of duplicated runtime rules.
- Runtime validation only remains where dynamic plugins require it.

---

### Phase 4 - Test Modernization
**Goal:** Use C++26 capabilities to reduce repetitive test code and increase coverage.

Tasks:
- Generate node/port conformance tests from reflection metadata.
- Convert repeated topology tests into parameterized suites where possible.
- Split slow integration/chaos tests from fast unit tests.
- Add C++26-only diagnostics tests for reflection, schema generation, and plugin loading.

Relevant areas already present in the repo:
- `doc/architecture/NODEFACTORY_CPP26_TEST_PLAN.md`
- `doc/tests/CLASS_TEST_COVERAGE_REPORT.md`
- `libgraph/test/unit/test_port_metadata_reflection.cpp`

Exit criteria:
- New tests derive expectations from compile-time descriptors.
- Boilerplate test coverage is reduced without losing behavioral coverage.

---

### Phase 5 - Selective Module Adoption
**Goal:** Transition stable subsystems to modules where the build benefits are clear.

Best candidates:
- Core graph types
- Reflection/schema utilities
- Message and capability abstractions
- Plugin interface headers once stable

Rules:
- Do not module-ize unstable or frequently changing code first.
- Keep plugin ABI boundaries explicit.
- Introduce modules per library, not all at once.

Exit criteria:
- At least one core library builds cleanly as a module.
- Module boundaries improve build speed or clarity without breaking plugin loading.

---

### Phase 6 - Remove Compatibility Shims
**Goal:** Delete the old code paths once C++26 paths are complete.

Tasks:
- Remove dual-path APIs.
- Remove adapter classes used only for legacy support.
- Drop old test scaffolding tied to deprecated metadata structures.
- Remove language-version workarounds.

Exit criteria:
- Only the C++26 path remains.
- No fallback APIs are required for the core libraries.

---

## Subsystem Priorities

### Highest Priority
1. **Reflection and metadata**
2. **Schema generation**
3. **Node descriptors and factory validation**
4. **Test generation from descriptors**

### Medium Priority
1. **Module adoption**
2. **Plugin loading modernization**
3. **Compile-time diagnostics cleanup**

### Lower Priority
1. **Binary size optimization**
2. **Build graph refinements**
3. **Further runtime simplification after reflection adoption**

---

## Recommended Breaking Changes

If the goal is to fully embrace C++26, these are worth breaking compatibility for:

- Require `CMAKE_CXX_STANDARD 26` everywhere.
- Remove support for compilers without the needed reflection support.
- Replace duplicated port metadata with reflected descriptors.
- Replace runtime schema builders with compile-time schema generation.
- Remove fallback APIs that mirror newer methods.
- Refactor tests to use generated fixtures and parameterized cases.
- Prepare core APIs for modules instead of raw include-only distribution.

---

## Risks

### Risk: Reflection APIs May Still Evolve
**Mitigation:** Isolate reflection usage behind a small set of internal helpers.

### Risk: Plugin ABI Instability
**Mitigation:** Keep plugin boundaries narrow and validate ABI at load time.

### Risk: Modules Could Complicate Incremental Adoption
**Mitigation:** Adopt modules only after core APIs stabilize.

### Risk: Over-optimizing for C++26 Too Early
**Mitigation:** Migrate subsystem-by-subsystem with passing tests at each step.

---

## Success Criteria

The migration is complete when:
- The codebase builds cleanly with a C++26-only toolchain.
- Compatibility shims are removed from core libraries.
- Reflection owns metadata and schema generation.
- Core node and plugin contracts are compile-time validated.
- Test coverage remains at 100% or better after refactoring.
- At least one core subsystem is module-ready or module-based.

---

## Suggested Execution Sequence

1. Freeze new compatibility work.
2. Convert reflection and schema generation first.
3. Tighten node contracts and factory validation.
4. Refactor tests to derive from descriptors.
5. Introduce modules in one stable subsystem.
6. Remove leftover compatibility code.

---

## Implementation Checklist

Use this as the working checklist for the migration. Complete items in order unless a dependency forces a different sequence.

### 1. Freeze Compatibility Scope
- [x] Set C++26 as the only supported standard in all top-level and subproject CMake files.
- [x] Remove any new C++20/23 fallback work from the backlog.
- [x] Document the APIs that will remain stable and the ones that will be removed.

Compatibility policy:
- Stable: C++26-first config/schema generation, `std::expected`-based error handling, plugin loading, and the current graph/node public façades.
- Removed: new C++20/23 fallback branches, compatibility adapters whose only purpose is older compiler support, and any future dual-path metadata generation.

### 2. Inventory Legacy Metadata
- [x] Enumerate all node, port, schema, and plugin metadata that is still handwritten.
- [x] Identify duplicate descriptions across runtime code, tests, and documentation.
- [x] Mark runtime-only registration paths that can be replaced by compile-time descriptors.

Inventory notes:
- [libgraph/src/graph/StaticNodeAdapter.cpp](/Users/rklinkhammer/workspace/GraphX/libgraph/src/graph/StaticNodeAdapter.cpp#L186) still hand-builds input and output port metadata from runtime `InputPorts()` / `OutputPorts()` results.
- [libgraph/src/graph/GraphConfigParser.cpp](/Users/rklinkhammer/workspace/GraphX/libgraph/src/graph/GraphConfigParser.cpp#L64) still parses top-level graph metadata fields manually from JSON.
- [libgraph/src/graph/NodeFacade.cpp](/Users/rklinkhammer/workspace/GraphX/libgraph/src/graph/NodeFacade.cpp#L532) still synthesizes façade metadata and placeholder port type strings at runtime.
- [libgraph/include/plugins/NodePluginTemplate.hpp](/Users/rklinkhammer/workspace/GraphX/libgraph/include/plugins/NodePluginTemplate.hpp#L1039) still wires metadata callbacks explicitly in the plugin façade.
- [libgraph/include/core/ReflectionHelper.hpp](/Users/rklinkhammer/workspace/GraphX/libgraph/include/core/ReflectionHelper.hpp#L147) still derives type metadata from current type traits rather than a finalized reflection source.
- [libgraph/include/core/PluginReflection.hpp](/Users/rklinkhammer/workspace/GraphX/libgraph/include/core/PluginReflection.hpp#L141) still exposes plugin metadata as a reflection-ready compatibility layer.
- Duplicate descriptions still appear in [libgraph/test/plugins/CMakeLists.txt](/Users/rklinkhammer/workspace/GraphX/libgraph/test/plugins/CMakeLists.txt#L16) and the plugin sources themselves, for example [int_producer_plugin.cpp](/Users/rklinkhammer/workspace/GraphX/libgraph/test/plugins/int_producer_plugin.cpp#L42) and [double_producer_plugin.cpp](/Users/rklinkhammer/workspace/GraphX/libgraph/test/plugins/double_producer_plugin.cpp#L42), where the target descriptions and `plugin_get_info()` strings repeat the same human-readable node text.
- Runtime registration still flows through [FactoryManager.cpp](/Users/rklinkhammer/workspace/GraphX/libgraph/src/graph/FactoryManager.cpp#L40) and [NodeFactory.cpp](/Users/rklinkhammer/workspace/GraphX/libgraph/src/graph/NodeFactory.cpp#L217), then into [PluginLoader.cpp](/Users/rklinkhammer/workspace/GraphX/libgraph/src/plugins/PluginLoader.cpp#L218) and `PluginRegistry::RegisterNodeType()`, which is the remaining path to replace with compile-time descriptors for built-in nodes.

### 3. Make Reflection Canonical
- [x] Move port and node metadata generation into reflection-based helpers.
- [x] Replace manual schema builders with reflection-derived schema generation.
- [x] Ensure reflection output is the single source of truth for tests and runtime validation.

Progress note:
- Port metadata assembly now flows through a shared `MakePortMetadataC()` helper in [NodeFacade.hpp](/Users/rklinkhammer/workspace/GraphX/libgraph/include/graph/NodeFacade.hpp#L47), and [NodeFacade.cpp](/Users/rklinkhammer/workspace/GraphX/libgraph/src/graph/NodeFacade.cpp#L532) now derives node metadata from the existing port metadata arrays instead of synthesizing placeholder port entries.
- Named node and merge/split node classes now route their `GetInputPortMetadata()` / `GetOutputPortMetadata()` implementations through the shared [MakePortMetadataForDirection()](/Users/rklinkhammer/workspace/GraphX/libgraph/include/graph/PortTypes.hpp#L197) helper instead of repeating the same `std::apply` filtering logic in each class.

### 4. Tighten Compile-Time Contracts
- [x] Replace runtime shape checks with concepts or compile-time predicates where possible.
- [x] Encode input/output port counts in compile-time descriptors.
- [x] Simplify NodeFactory validation for built-in node types.

Progress note:
- [PortTypes.hpp](/Users/rklinkhammer/workspace/GraphX/libgraph/include/graph/PortTypes.hpp#L207) now exposes a `NodePortDescriptor` that derives input/output counts from either a node's `Ports` tuple or its `NInputs`/`NOutputs` constants.
- [NodeFactory.hpp](/Users/rklinkhammer/workspace/GraphX/libgraph/include/graph/NodeFactory.hpp#L101) now constrains template-based node creation on that compile-time port descriptor, which removes the last implicit shape assumption from the built-in creation path.

### 5. Modernize Tests
- [x] Generate conformance tests from reflection metadata.
- [x] Convert repeated topology tests into parameterized or table-driven suites.
- [x] Add focused schema and deserialization tests for the new config contract.

### 6. Introduce Modules Selectively
- [x] Pick one stable core library for the first module conversion.
- [x] Define explicit boundaries for graph, schema, and plugin-facing APIs.
- [x] Keep plugin ABI boundaries outside the first module adoption pass.

Progress note:
- The first module pilot is the stable `libgraph` port-metadata layer, centered on [PortTypes.hpp](/Users/rklinkhammer/workspace/GraphX/libgraph/include/graph/PortTypes.hpp) and the module interface unit [graph.port_metadata.ixx](/Users/rklinkhammer/workspace/GraphX/libgraph/modules/graph.port_metadata.ixx).
- The pilot is opt-in through `GRAPHX_ENABLE_MODULE_PILOT` and builds in a separate Ninja/LLVM tree, which keeps the default Unix Makefiles build unchanged.
- Plugin-facing code such as [NodeFacade.hpp](/Users/rklinkhammer/workspace/GraphX/libgraph/include/graph/NodeFacade.hpp), [FactoryManager.cpp](/Users/rklinkhammer/workspace/GraphX/libgraph/src/graph/FactoryManager.cpp), and [PluginLoader.cpp](/Users/rklinkhammer/workspace/GraphX/libgraph/src/plugins/PluginLoader.cpp) stays out of the first module pass so the ABI boundary remains unchanged.

### 7. Remove Compatibility Shims
- [x] Delete dual-path APIs after the C++26 path is verified.
- [x] Remove adapter layers that exist only for older compiler support.
- [x] Eliminate language-version workarounds and legacy test scaffolding.

Progress note:
- Removed the dead backward-compatibility CSV integration tests from [libgraph/test/integration/test_csv_parser.cpp](/Users/rklinkhammer/workspace/GraphX/libgraph/test/integration/test_csv_parser.cpp).
- Removed the legacy `SetPluginLoader()`-only test path from [libgraph/test/unit/test_node_factory_multi_directory.cpp](/Users/rklinkhammer/workspace/GraphX/libgraph/test/unit/test_node_factory_multi_directory.cpp), leaving the suite focused on the directory-based loading API.
- Removed the `NodeFactory`-level `SetPluginLoader()` shim and its legacy fallback path in [libgraph/src/graph/NodeFactory.cpp](/Users/rklinkhammer/workspace/GraphX/libgraph/src/graph/NodeFactory.cpp), so node creation now flows through the registry and directory-based loader list only.
- Removed the remaining loader bridge from [libgraph/include/capabilities/GraphCapability.hpp](/Users/rklinkhammer/workspace/GraphX/libgraph/include/capabilities/GraphCapability.hpp) and [libgraph/src/graph/GraphExecutorBuilder.cpp](/Users/rklinkhammer/workspace/GraphX/libgraph/src/graph/GraphExecutorBuilder.cpp), completing the dual-path cleanup.
- Removed the legacy sensor CSV compatibility adapter layer by deleting [libsensor/include/sensor/CSVParserCompat.hpp](/Users/rklinkhammer/workspace/GraphX/libsensor/include/sensor/CSVParserCompat.hpp) and [libsensor/src/csv/CSVParserCompat.cpp](/Users/rklinkhammer/workspace/GraphX/libsensor/src/csv/CSVParserCompat.cpp); the generic CSV parser API in [libgraph/include/csv/CSVParser.hpp](/Users/rklinkhammer/workspace/GraphX/libgraph/include/csv/CSVParser.hpp) is now the canonical path.

### 8. Verify Completion
- [x] Build the full project with the C++26-only toolchain.
- [x] Run the full test suite and confirm 100% pass rate.
- [x] Confirm metadata, schema generation, and node contracts are reflection-driven.
- [x] Confirm at least one core subsystem is module-ready or module-based.

Progress note:
- Default C++26 toolchain build and tests pass in [build](build): full `make -j4` succeeds and `ctest --verbose` reports 100% pass rate.
- Module-enabled build and tests pass in [build-modules-llvm](build-modules-llvm): `GRAPHX_ENABLE_MODULE_PILOT=ON` build completes and `ctest -C Release --output-on-failure` reports 100% pass rate.
- Reflection-driven metadata/contracts are enforced through [libgraph/include/graph/PortTypes.hpp](/Users/rklinkhammer/workspace/GraphX/libgraph/include/graph/PortTypes.hpp) (`NodePortDescriptor`, `MakePortMetadataForDirection`) and [libgraph/include/graph/NodeFactory.hpp](/Users/rklinkhammer/workspace/GraphX/libgraph/include/graph/NodeFactory.hpp) (`requires HasCompileTimePortCounts<NodeType>`).
- Reflection/schema generation remains descriptor-driven via [libgraph/include/config/SchemaGenerator.hpp](/Users/rklinkhammer/workspace/GraphX/libgraph/include/config/SchemaGenerator.hpp) (`GenerateSchemaFromType`, `SchemaValidator`), with runtime/compile-time conformance verified by [libgraph/test/unit/test_port_metadata_reflection.cpp](/Users/rklinkhammer/workspace/GraphX/libgraph/test/unit/test_port_metadata_reflection.cpp).

## Immediate Next Step

Start with the **reflection and schema layer** because that is where C++26 gives the largest payoff and where the existing codebase already shows the strongest direction.

That means:
- replace hand-maintained metadata tables,
- make generated descriptors canonical,
- and use those descriptors to drive both runtime validation and tests.
