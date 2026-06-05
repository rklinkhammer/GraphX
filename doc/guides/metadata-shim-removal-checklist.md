# Metadata Shim Removal Checklist

## Purpose
Track safe removal of descriptor-provider compatibility shims now that metadata-service-first APIs are in place.

## Shim Scope
1. `NodeFacadeAdapter` descriptor-provider constructor shim.
2. `NodeFacadeAdapter::SetDescriptorProvider(...)` shim.
3. `BuildDescriptorFromPluginInstance(..., const INodeDescriptorProvider*)` shim.

## Status (2026-06-05)
1. Production code paths using compatibility shims: none found.
2. Constructor shim declaration for `NodeFacadeAdapter(..., const INodeDescriptorProvider*)`: removed.
3. `SetDescriptorProvider(...)` shim: removed.
4. Plugin helper descriptor-provider overload: removed.
5. Targeted shim signature sweep for `NodeFacadeAdapter(...INodeDescriptorProvider...)`, `SetDescriptorProvider(...)`, and `BuildDescriptorFromPluginInstance(...INodeDescriptorProvider...)`: no matches in `libgraph`.
6. Final validation gates completed:
	- Unit gate: `test_libgraph_unit` passed (`1004` passed, `2` skipped, `1` disabled).
	- Integration gate: `test_libgraph_integration` passed (`37/37`).

## Remaining Work
1. None. Checklist exit criteria are satisfied.

## Current Metadata-Service Coverage
1. `CapabilityContext` injected metadata service path.
2. `JsonDynamicGraphLoader` injected metadata service path.
3. `PluginInspector` injected metadata service path.
4. `NodeFacadeAdapter` metadata-service constructor path.
5. Plugin descriptor helper metadata-service path.

## Validation Gates (Final Pass)
1. Build gate: full build in active build tree.
2. Unit gate: `libgraph/test/test_libgraph_unit` broad pass.
3. Integration gate: `libgraph/test/test_libgraph_integration` pass.
4. Plugin gate: plugin inspection and metadata schema smoke checks.

## Exit Criteria
1. Metadata injection surfaces accept one dependency object: `INodeMetadataService`.
2. No internal tests or production code reference descriptor-provider-only shim APIs.
3. Descriptor/schema behavior remains unchanged for loader, inspector, capability context, facade, and plugin helper paths.
