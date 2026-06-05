# GraphX Architecture Simplification Roadmap

## Goal
After the C++26 cleanup pass, simplify architecture without reducing functionality by removing duplicated abstractions, reducing runtime type plumbing, and clarifying ownership boundaries.

## Priority Order
1. Typed descriptors as the single source of truth
2. Plugin facade and C ABI isolation
3. Factory and builder consolidation
4. Capability and policy unification
5. GPU backend genericization
6. CSV typed-descriptor migration
7. Large-header decomposition
8. Careful module expansion
9. Error-model standardization at orchestration edges
10. Move tutorial-heavy examples out of core headers

## Highest Impact Track

### 1) Typed Node and Port Descriptors as Source of Truth
#### Why
Metadata is currently distributed across runtime and template layers.

#### Scope
- Introduce constexpr descriptor model for node inputs, outputs, config fields, and capabilities.
- Generate schema, facade metadata, plugin metadata, and validation from this single model.
- Keep existing reflection-style helpers as compatibility adapters during migration.

#### Key Files
- libgraph/include/graph/Nodes.hpp
- libgraph/include/graph/NodeFacade.hpp
- libgraph/include/plugins/NodePluginTemplate.hpp
- libgraph/include/graph/Reflection.hpp
- libgraph/include/config/SchemaGenerator.hpp

#### Exit Criteria
- One descriptor definition drives all metadata surfaces.
- No behavior change in serialization, validation, plugin loading, or facade output.

### 2) Isolate C Plugin ABI behind Narrow Interop Layer
#### Why
void pointers, C callbacks, and cast-heavy probing spread complexity into core typed code.

#### Scope
- Centralize C ABI glue in one interop boundary.
- Keep internal graph/plugin pipeline strongly typed.
- Optionally generate repetitive ABI shims from descriptors.

#### Key Files
- libgraph/include/plugins/NodePluginTemplate.hpp
- libgraph/include/graph/NodeFacade.hpp

#### Exit Criteria
- Core graph code no longer depends on interop details.
- ABI behavior remains unchanged for existing plugins.

### 3) Collapse Factory, Registry, and Builder Overlap
#### Why
Adjacent layers overlap responsibilities and increase lifecycle branching.

#### Scope
- Move to a clear pipeline: descriptor registry -> graph construction -> execution plan/runtime.
- Keep native and plugin nodes on one registration and creation path.

#### Key Files
- libgraph/include/graph/NodeFactory.hpp
- libgraph/include/plugins/PluginRegistry.hpp
- libgraph/include/graph/FactoryManager.hpp
- libgraph/include/graph/GraphBuilder.hpp
- libgraph/include/graph/FluentGraphBuilder.hpp
- libgraph/include/graph/GraphExecutorBuilder.hpp

#### Exit Criteria
- Reduced creation pathways with equivalent coverage.
- Fewer node-type special cases in graph construction.

### 4) Unify Capability Discovery and Policy Access
#### Why
Capability checks and policy wiring still require probing and wrappers in some paths.

#### Scope
- Introduce typed CapabilityContext or GraphServices with get<T>() returning expected reference or error.
- Migrate policy retrieval and capability checks onto this typed surface.

#### Key Files
- libgraph/include/graph/CapabilityDiscovery.hpp
- libgraph/include/graph/CapabilityBus.hpp
- libgraph/include/graph/DefaultCapabilityBus.hpp

#### Exit Criteria
- Policies and nodes use typed service lookup rather than ad hoc probing.
- Capability-related failure paths become explicit and testable.

### 5) Genericize GPU Backend Architecture
#### Why
CUDA, SYCL, and Metal share repeatable node and capability patterns.

#### Scope
- Introduce backend traits and concepts for shared pipeline primitives.
- Keep backend-specific adapters thin and focused.
- Preserve first-class Metal path while reducing duplication.

#### Key Targets
- Shared GPU capability and node families across libgpu.

#### Exit Criteria
- Shared architecture handles common patterns for all backends.
- Backend-specific code is mostly adapter-level.

## Medium Impact Track

### 6) Replace CSV Runtime Type Plumbing with Typed Descriptors
#### Why
Generic CSV paths still rely on runtime any and map structures.

#### Scope
- Migrate CSV mapping to typed descriptor-driven extraction and conversion.
- Keep support for current formats while reducing runtime typing overhead.

#### Key Files
- libgraph/include/csv/CSVParser.hpp
- libgraph/src/csv/CSVParser.cpp

#### Exit Criteria
- Generic CSV parsing paths no longer require any for core flow.
- Existing CSV tests pass with identical outputs.

### 7) Split Large Headers into Focused Units
#### Why
Large headers hide ownership boundaries and increase compile churn.

#### Scope
- Decompose API, implementation details, adapters, and examples into focused headers.
- Keep public include compatibility with umbrella headers where needed.

#### Main Candidates
- libgraph/include/graph/ThreadPool.hpp
- libgraph/include/graph/GraphManager.hpp
- libgraph/include/graph/Nodes.hpp
- libgraph/include/graph/NodeFacade.hpp
- libgraph/include/plugins/NodePluginTemplate.hpp

#### Exit Criteria
- Cleaner dependency graph and measurable compile-time improvement.

### 8) Expand C++ Module Boundaries Carefully
#### Why
Modules can reduce compile overhead but should target stable seams first.

#### Scope
- Prioritize stable descriptor and config/schema types for module boundaries.
- Keep plugin ABI and platform GPU boundaries non-module for now.

#### Exit Criteria
- Incremental module adoption without disruption to plugin or platform integration.

### 9) Standardize One Error Model at Orchestration Boundaries
#### Why
Expected adoption is strong but mixed bool and optional control flow remains in high-level orchestration.

#### Scope
- Use expected for graph construction, plugin loading, schema generation, and capability lookup where errors carry context.
- Keep bool for trivial predicates only.

#### Exit Criteria
- Orchestration surfaces expose consistent error contracts.

### 10) Move Tutorial-Length Content Out of Core Headers
#### Why
Large inline examples reduce scanability of API contracts.

#### Scope
- Move long examples to docs and tests.
- Keep concise API intent comments in headers.

#### Exit Criteria
- Headers are reference-focused; tutorials live in docs.

## Recommended Execution Sequence
1. Typed descriptors
2. Plugin facade and ABI isolation
3. Registry and builder consolidation
4. Capability and policy context
5. GPU genericization
6. CSV and header decomposition
7. Modules and error-model finish pass

## Validation Gates for Every Phase
- Build gate: full build succeeds in active build tree.
- Unit gate: libgraph unit tests pass.
- Integration gate: libgraph integration tests pass.
- Performance gate: threadpool extended suite baseline comparison when enabled.
- Plugin gate: existing plugin binaries and smoke consumer still load and execute.

## Delivery Style
- One architectural objective per PR when possible.
- Land adapters and compatibility shims first, internal rewrites second.
- Avoid API removals until a stable deprecation window has elapsed.
