# Backwards Compatibility Inventory

## Project Rule

Backwards compatibility is not required for this project. New APIs are the source of truth, and tests/examples should be updated to use them directly.

## True Compatibility Shims in Production Code

1. None remaining.

## Test-Only Compatibility Aliases and Adapters

1. None remaining.

## Compatibility Wording That Is Not a Shim

1. ABI/version compatibility checks in plugin inspection.
   - These are runtime validation features, not backwards-compatibility shims.
2. Documentation references to compatibility during migration.
   - These should be rewritten as migration notes once removal is complete.

## Removal Status

1. `NodeFacadeAdapter` descriptor-provider compatibility shim: removed.
2. `NodeFacadeAdapter::SetDescriptorProvider(...)`: removed.
3. `BuildDescriptorFromPluginInstance(..., const INodeDescriptorProvider*)`: removed.
4. `GraphCapability::SetNodeFactory(...)`: removed.
5. `GraphCapability::GetNodeFactory()`: removed.
6. `TestMetricsSubscriber::GetEvents()`: removed.
7. `JsonViewAdapter` legacy test adapter: removed.
8. `NodeFactoryRegistry` public creation layer: removed; `NodeFactory` owns provider-backed creation directly.
9. `FactoryManager` legacy bootstrap path: removed; `NodeProviderBootstrap` is the only provider bootstrap boundary.
10. `NodeFactory` plugin-directory loading path: removed; plugin loading belongs to `NodeProviderBootstrap`.
11. `csv::ParseRowConsolidatedExpected(...)` alias: removed; `csv::ParseRowUnifiedExpected(...)` is the single row parser entry point.

## Current Non-Compatibility Behaviors

1. `JsonDynamicGraphLoader` assigns node instance names from `node_config.name` when present, otherwise `node_config.id`.
   - `NodeConfig::name` is optional.
   - The `id` fallback is the current naming rule, not a backwards-compatibility shim.
   - `node_config.type` remains only for dynamic loading and type-level behavior.

## Next Cleanup Steps

1. Continue the Track 3 `GraphBuilder`/policy setup audit.
