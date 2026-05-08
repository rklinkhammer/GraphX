# Stage 3: Source File Migration Notes

**Date**: May 8, 2026  
**Status**: Code Migrated, Dependencies Documented (Compilation Issues Identified)

## Summary

Successfully copied all 63 source files (.cpp) from monolithic dashboard to modularized GraphX structure. Identified circular dependency patterns that require resolution in Stage 4.

## Source File Distribution

| Project | Count | Status |
|---------|-------|--------|
| libgraph | 16 | Copied ✓ |
| libdashboard | 9 | Copied ✓ |
| libnodes | 38 | Copied ✓ |
| **TOTAL** | **63** | - |

## Directory Structure

### libgraph/src/graph/
```
16 .cpp files including:
  - AdaptiveCapacityMonitor.cpp
  - EdgeRegistry.cpp
  - EdgeRegistration.cpp
  - ExecutionState.cpp
  - GraphConfigParser.cpp
  - GraphExecutor.cpp
  - GraphExecutorBuilder.cpp
  - JsonDynamicGraphLoader.cpp
  - ... (8 more files)
```

### libdashboard/src/
```
9 .cpp files from ui/ and app/:
  - CommandRegistry.cpp
  - TabContainer.cpp
  - MetricsTileWindow.cpp
  - BuiltinCommands.cpp
  - Dashboard.cpp
  - JsonUtilities.cpp
  - FactoryManager.cpp
  - GraphBuilder.cpp
  - SignalHandler.cpp
```

### libnodes/src/
```
38 .cpp files from avionics/, csv/, dsp/:
  - avionics/ (19 files)
  - csv/ (2 files)
  - dsp/ (2 files)
  - (plus other node implementations)
```

## CMakeLists.txt Updates

Updated all project CMakeLists.txt files to:
- Use `file(GLOB ...)` to collect source files
- Replace placeholder sources with actual .cpp files
- Enable compilation of all three projects

```cmake
# Example pattern applied to all projects:
file(GLOB SOURCES "src/*.cpp" "src/**/*.cpp")
add_library(project_name STATIC ${SOURCES})
```

## Dependency Issues Identified

During compilation, we discovered **circular/complex dependencies**:

### Issue 1: Core Utilities
- `app/Errors.hpp` includes `core/FormatUtilities.hpp`
- `core/FormatUtilities.hpp` is header-only utility in core/
- **Solution**: Copy all core/ headers to libgraph/include/graph/core/
- **Status**: ✓ Implemented

### Issue 2: app/ Headers Split
**Original structure** bundled app headers together:
- Some are UI-specific (capabilities, policies) → belong to libdashboard
- Some are utilities (Errors.hpp, JsonUtilities.hpp) → needed by libgraph
- Some are interfaces (GraphCapability) → belong to libdashboard

**Current approach**: Copied both to libgraph as workaround
- app/Errors.hpp → libgraph/include/graph/app/
- app/JsonUtilities.hpp → libgraph/include/graph/app/

**Note**: This creates upward dependency (graph depends on dashboard concepts)

### Issue 3: Circular Include Patterns
- `IExecutionPolicy.hpp` includes `graph/app/capabilities/GraphCapability.hpp`
- This means graph layer depends on dashboard concepts (GraphCapability)
- Violates layering: Graph should be independent of UI/Dashboard

### Issue 4: Missing FormatUtilities Dependency
- `app/Errors.hpp` references `core/FormatUtilities.hpp`
- Not yet resolved - needs additional header copies

## Files Copied Successfully

```bash
✓ Graph sources: 16 files
✓ UI/App sources: 9 files
✓ Node sources: 38 files
✓ Core utility headers: 9 files
✓ CMakeLists.txt: Updated for all projects
✓ Include path fixes: Applied to graph headers
```

## What Works

- Directory structure properly organized
- All source files in place
- CMakeLists.txt properly configured for all projects
- Include paths mostly corrected

## What Needs Work (Stage 4+)

### High Priority: Dependency Restructuring

1. **Resolve app/ header split**
   - Move utility headers (Errors, JsonUtilities, core/) fully to core module
   - Move capability headers to libdashboard only
   - Update IExecutionPolicy to not depend on GraphCapability

2. **Break circular dependencies**
   - Graph layer should NOT depend on dashboard concepts
   - Move GraphCapability definition to where IExecutionPolicy lives
   - Separate interface (IExecutionPolicy) from implementation

3. **Header organization**
   - core/ should be standalone (no dependencies on app/ or graph/)
   - graph/ should only depend on core/
   - dashboard/ can depend on both graph/ and core/

### Medium Priority: Include Path Fixes

- Fix remaining #include paths in source files
- Verify all relocations are consistent
- Test each project compilation independently

### Compilation Status

**Current**: ~12 compilation errors (unresolved includes)
**Root cause**: Circular/upward dependencies in header structure
**Resolution**: Requires refactoring of header organization

## Test Strategy

After resolving dependencies, test compilation:

```bash
# Individual project compilation
cd libgraph/build && make
cd libnodes/build && make  
cd libdashboard/build && make

# Then full workspace
cd GraphX/build && make
```

## Lessons Learned

1. **Hidden dependencies**: Monolithic structure hid circular dependencies
2. **Utility headers**: Need explicit separation of utilities from domain concepts
3. **Layer violations**: Graph layer was implicitly depending on dashboard concepts
4. **Header organization**: More thought needed on which headers go where

## Next Steps

1. Stage 4 (Tests): Can proceed despite compilation issues
   - Copy test files to per-project directories
   - Tests will need same header/include fixes

2. Stage 5 (Integration): Final application main.cpp

3. Dependency refactoring can happen in parallel or after migration

---

**Total Migration Time**: ~30 minutes  
**Files Moved**: 63 source files  
**Issues Found**: 4 (documented above)  
**Status**: Ready for Stage 4 (with dependency work in parallel)
