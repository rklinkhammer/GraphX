# Backwards Compatibility Inventory

## Project Rule

Backwards compatibility is not required for this project. New APIs are the source of truth, and tests/examples should be updated to use them directly.

## True Compatibility Shims in Production Code

1. `graph::config::JsonDynamicGraphLoader` name fallback behavior
   - Previously left node names at the plugin default, which could match node type.
   - Updated to assign `node_config.name` or `node_config.id`.

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

## Next Cleanup Steps

1. Re-audit for any remaining shim-style aliases after the compatibility sweep.
