# Multi-Plugin Directory Support Analysis

**Date:** May 29, 2026  
**Status:** Analysis Phase  
**Objective:** Enable NodeFactory and plugin system to support loading plugins from multiple directories

---

## 1. Current Architecture Overview

### 1.1 Single Directory Model

The current plugin loading architecture is designed for a **single plugin directory**:

```
NodeFactory (singleton pattern expected)
    ├── PluginRegistry (1:1 relationship)
    │   └── Stores registered node types: Map<string, PluginNodeInfo>
    └── PluginLoader (1:1 relationship)
        └── plugin_directory_: string (single path)
            └── Scans and loads all .so/.dylib files
```

**Key Single-Directory Constraints:**

| Component | Single Directory Design |
|-----------|------------------------|
| PluginLoader Constructor | `PluginLoader(const std::string& plugin_directory, ...)` |
| Directory Storage | `std::string plugin_directory_` (member variable) |
| Scan Method | `fs::directory_iterator(plugin_directory_)` in LoadAllPlugins() |
| LoadPlugin() | Constructs full path: `plugin_directory_ / filename` |
| Configuration | Hard-coded or environment variable at initialization time |

### 1.2 Plugin Loading Flow

```
1. PluginLoader constructed with single directory path
2. LoadAllPlugins() called:
   - Checks fs::exists(plugin_directory_)
   - Iterates fs::directory_iterator(plugin_directory_)
   - For each .so/.dylib file:
     - Calls LoadPlugin(filename)
     - Constructs full_path = plugin_directory_ / filename
     - dlopen() from full_path
3. Each plugin registers with PluginRegistry
4. NodeFactory queries PluginRegistry for available types
```

### 1.3 Key Code Locations

| File | Purpose | Current Design |
|------|---------|-----------------|
| `include/graph/NodeFactory.hpp` | Unified node factory | Single `plugin_registry_` |
| `src/graph/NodeFactory.cpp` | Factory implementation | Uses one `PluginRegistry` |
| `include/plugins/PluginLoader.hpp` | Plugin discovery/loading | Takes single directory in constructor |
| `src/plugins/PluginLoader.cpp` | Load implementation | Hard-coded `fs::directory_iterator(plugin_directory_)` |
| `include/plugins/PluginRegistry.hpp` | Plugin metadata storage | Type-agnostic map, location-independent |
| `src/plugins/PluginRegistry.cpp` | Registration logic | Stores full paths, but loads from one loader |

---

## 2. Design Analysis: Multi-Directory Support

### 2.1 Architectural Layers

```
Layer 3: NodeFactory
    ↓ (depends on)
Layer 2: PluginLoader + PluginRegistry
    ↓ (scans)
Layer 1: Filesystem (plugin .so/.dylib files)
```

**Multi-directory support must exist at Layer 2 (PluginLoader)**

### 2.2 PluginRegistry is Already Location-Agnostic

**Good news:** `PluginRegistry` doesn't care where plugins come from!

```cpp
struct PluginNodeInfo {
    std::string type_name;           // e.g., "SensorNode"
    std::string plugin_path;         // FULL PATH, can come from anywhere
    void* plugin_handle;             // From dlopen()
    CreateNodeFunc create_func;      // From dlsym()
    // ... other metadata
};

// Registry doesn't know about directories, only handles
std::map<std::string, PluginNodeInfo> registered_types_;
```

**Impact:** Multiple PluginLoaders can safely register plugins with the same PluginRegistry.

### 2.3 PluginLoader Couples Directory Path to Loading Logic

**Problem locations:**

```cpp
// PluginLoader.hpp
class PluginLoader {
private:
    std::string plugin_directory_;  // ← SINGLE directory
    
    void LoadPlugin(const std::string& plugin_filename);
    void LoadAllPlugins();
};

// PluginLoader.cpp - LoadAllPlugins()
void PluginLoader::LoadAllPlugins() {
    for (const auto& entry : fs::directory_iterator(plugin_directory_)) {
        // ↑ Hard-coded to iterate ONE directory
        if (entry.path().extension() == ".so" || entry.path().extension() == ".dylib") {
            LoadPlugin(entry.path().filename().string());
        }
    }
}

// PluginLoader.cpp - LoadPlugin()
void PluginLoader::LoadPlugin(const std::string& plugin_filename) {
    fs::path full_path = fs::path(plugin_directory_) / plugin_filename;
    // ↑ Constructs path using single directory
    void* handle = dlopen(full_path_str.c_str(), RTLD_LAZY | RTLD_GLOBAL);
    // ... rest of loading
}
```

---

## 3. Options for Multi-Directory Support

### Option A: Multiple PluginLoader Instances (Recommended)

**Architecture:**
```
NodeFactory
    ├── PluginRegistry (shared)
    ├── PluginLoader #1 → directory_1/
    ├── PluginLoader #2 → directory_2/
    └── PluginLoader #3 → directory_3/
```

**Implementation:**
```cpp
class NodeFactory {
private:
    std::shared_ptr<PluginRegistry> plugin_registry_;
    std::vector<std::shared_ptr<PluginLoader>> loaders_;  // ← Multiple
    std::shared_ptr<PluginLoader> primary_loader_;
};

void NodeFactory::AddPluginDirectory(const std::string& directory) {
    auto loader = std::make_shared<PluginLoader>(directory, plugin_registry_);
    loader->LoadAllPlugins();
    loaders_.push_back(loader);
}

void NodeFactory::LoadPluginFromAllDirectories(const std::string& filename) {
    for (auto& loader : loaders_) {
        if (loader->TryLoadPlugin(filename)) {
            return;  // Found in first directory
        }
    }
    throw std::runtime_error("Plugin not found in any directory");
}
```

**Advantages:**
- Minimal changes to PluginLoader (none needed!)
- PluginRegistry unchanged
- Clear separation of concerns
- Each loader owns one directory
- Search order is controllable (priority directories first)

**Disadvantages:**
- Multiple PluginLoader instances consume memory
- Must track multiple handles for cleanup

### Option B: Modify PluginLoader for Multiple Directories

**Architecture:**
```
PluginLoader
    ├── std::vector<std::string> plugin_directories_
    ├── LoadPlugin(filename) searches all directories
    └── LoadAllPlugins() scans all directories
```

**Implementation:**
```cpp
class PluginLoader {
private:
    std::vector<std::string> plugin_directories_;  // ← Multiple
    
    std::string FindPlugin(const std::string& filename);
    void LoadAllPlugins();  // Scans all directories
};

std::string PluginLoader::FindPlugin(const std::string& filename) {
    for (const auto& dir : plugin_directories_) {
        fs::path full_path = fs::path(dir) / filename;
        if (fs::exists(full_path)) {
            return full_path.string();
        }
    }
    return "";  // Not found
}

void PluginLoader::LoadAllPlugins() {
    for (const auto& directory : plugin_directories_) {
        for (const auto& entry : fs::directory_iterator(directory)) {
            if (entry.path().extension() == ".so" || 
                entry.path().extension() == ".dylib") {
                try {
                    LoadPluginFromPath(entry.path().string());  // Full path version
                } catch (...) {
                    // Log and continue
                }
            }
        }
    }
}
```

**Advantages:**
- Single PluginLoader instance
- Single set of handles to manage
- Simple API: `loader->AddDirectory(path)`
- Unified plugin search logic

**Disadvantages:**
- More extensive changes to PluginLoader
- Must refactor LoadPlugin to accept full path OR implement FindPlugin
- Directory order for duplicates must be carefully specified

### Option C: Hybrid - Config File with Multiple Paths

**Architecture:**
```
NodeFactory reads:
    plugin_directories = [
        "/usr/local/lib/graphx/plugins",
        "./plugins",
        "/opt/graphx/plugins"
    ]

Then creates Option A or B implementation
```

**Advantages:**
- Flexible configuration
- Can be environment-dependent
- No code changes needed

**Disadvantages:**
- Adds configuration layer
- Introduces file I/O dependency

---

## 4. Recommended Approach: Option A (Multiple Loaders)

### 4.1 Why Option A?

1. **Minimal Risk:** PluginLoader and PluginRegistry unchanged
2. **Clear Intent:** Each loader explicitly owns one directory
3. **Testable:** Can mock individual loaders
4. **Extensible:** Can add priority/ordering later
5. **Backward Compatible:** Existing code patterns still work

### 4.2 Implementation Outline

**NodeFactory.hpp changes:**
```cpp
class NodeFactory {
private:
    std::vector<std::shared_ptr<PluginLoader>> loaders_;  // NEW
    std::shared_ptr<PluginRegistry> plugin_registry_;
    bool initialized_;

public:
    // NEW: Add a directory to plugin search path
    void AddPluginDirectory(const std::string& directory_path);
    
    // NEW: Load all plugins from all registered directories
    void LoadAllPluginsFromDirectories();
    
    // Existing methods remain unchanged
    virtual NodeFacadeAdapter CreateDynamicNode(const std::string& node_type_name);
    virtual NodeFacadeAdapter CreateNode(const std::string& node_type_name);
    void Initialize();
};
```

**NodeFactory.cpp implementation:**
```cpp
void NodeFactory::AddPluginDirectory(const std::string& directory_path) {
    LOG4CXX_TRACE(logger_, "Adding plugin directory: " << directory_path);
    
    if (loaders_.empty()) {
        // First directory - create primary loader
        auto loader = std::make_shared<PluginLoader>(directory_path, plugin_registry_);
        loaders_.push_back(loader);
        LOG4CXX_TRACE(logger_, "Created primary PluginLoader");
    } else {
        // Additional directory - add another loader
        auto loader = std::make_shared<PluginLoader>(directory_path, plugin_registry_);
        loaders_.push_back(loader);
        LOG4CXX_TRACE(logger_, "Added additional PluginLoader (total: " 
                     << loaders_.size() << ")");
    }
}

void NodeFactory::LoadAllPluginsFromDirectories() {
    LOG4CXX_TRACE(logger_, "Loading plugins from " << loaders_.size() << " directories");
    
    size_t total_loaded = 0;
    for (auto& loader : loaders_) {
        try {
            loader->LoadAllPlugins();
            auto types = loader->GetRegistry()->GetRegisteredNodeTypes();
            total_loaded += types.size();
            LOG4CXX_TRACE(logger_, "Loader loaded " << types.size() << " types");
        } catch (const std::exception& e) {
            LOG4CXX_WARN(logger_, "Failed to load plugins from directory: " << e.what());
            // Continue with next directory
        }
    }
    
    LOG4CXX_TRACE(logger_, "Total plugins loaded: " << total_loaded);
}
```

### 4.3 Usage Patterns

```cpp
// Pattern 1: Traditional single directory (backward compatible)
auto factory = std::make_shared<NodeFactory>(plugin_registry);
factory->Initialize();  // Uses original SetPluginLoader path

// Pattern 2: Multiple directories (new)
auto factory = std::make_shared<NodeFactory>();
factory->AddPluginDirectory("/usr/local/lib/graphx/plugins");
factory->AddPluginDirectory("./plugins");
factory->AddPluginDirectory("./test_plugins");
factory->LoadAllPluginsFromDirectories();

// Pattern 3: Environment-based configuration
std::string dirs_env = std::getenv("GRAPHX_PLUGIN_PATH") ?: "./plugins";
for (const auto& dir : split(dirs_env, ':')) {
    factory->AddPluginDirectory(dir);
}
factory->LoadAllPluginsFromDirectories();
```

---

## 5. Breaking Changes and Backward Compatibility

### 5.1 Potential Breaking Changes

| Current API | Impact | Mitigation |
|------------|--------|-----------|
| `NodeFactory(plugin_registry)` | Constructor still works | Keep as-is |
| `factory->Initialize()` | Works with original loader | Check if loaders_ empty before using primary_loader_ |
| `factory->SetPluginLoader(loader)` | Becomes "set primary loader" | Document clearly |

### 5.2 Backward Compatibility Strategy

```cpp
void NodeFactory::Initialize() {
    if (loaders_.empty() && plugin_loader_) {
        // Legacy path: convert single loader to vector
        loaders_.push_back(plugin_loader_);
        LOG4CXX_TRACE(logger_, "Using legacy plugin loader");
    }
    
    if (loaders_.empty()) {
        LOG4CXX_WARN(logger_, "No plugin loaders configured");
        // Fall back to pure static node registry
        RegisterStaticNodes();
        initialized_ = true;
        return;
    }
    
    // New path: load from all directories
    LoadAllPluginsFromDirectories();
    RegisterStaticNodes();
    initialized_ = true;
}
```

---

## 6. Implementation Phases

### Phase 1: Infrastructure (non-breaking)
- [ ] Add `std::vector<std::shared_ptr<PluginLoader>> loaders_` to NodeFactory
- [ ] Implement `AddPluginDirectory(path)`
- [ ] Implement `LoadAllPluginsFromDirectories()`
- [ ] Add unit tests for multi-directory loading
- [ ] **No breaking changes**

### Phase 2: Transition (optional)
- [ ] Deprecate `SetPluginLoader()` in favor of `AddPluginDirectory()`
- [ ] Update documentation with multi-directory examples
- [ ] Add integration tests with multiple real directories

### Phase 3: Future Enhancements (if needed)
- [ ] Priority/order-based directory search
- [ ] Hot-reloading plugins from new directories
- [ ] Plugin namespace isolation per directory
- [ ] Directory-specific configuration

---

## 7. Testing Strategy

### 7.1 Unit Tests

```cpp
TEST(NodeFactoryMultiDirectory, AddSingleDirectory) {
    auto factory = std::make_shared<NodeFactory>();
    factory->AddPluginDirectory("./plugins");
    // Verify loader added
}

TEST(NodeFactoryMultiDirectory, AddMultipleDirectories) {
    auto factory = std::make_shared<NodeFactory>();
    factory->AddPluginDirectory("./plugins");
    factory->AddPluginDirectory("./test_plugins");
    factory->AddPluginDirectory("./custom_plugins");
    // Verify all loaders added
}

TEST(NodeFactoryMultiDirectory, LoadFromMultipleDirectories) {
    auto factory = std::make_shared<NodeFactory>();
    factory->AddPluginDirectory(PLUGIN_DIR1);
    factory->AddPluginDirectory(PLUGIN_DIR2);
    factory->LoadAllPluginsFromDirectories();
    
    // Verify plugins from both directories loaded
    EXPECT_TRUE(factory->IsNodeTypeAvailable("PluginFromDir1"));
    EXPECT_TRUE(factory->IsNodeTypeAvailable("PluginFromDir2"));
}

TEST(NodeFactoryMultiDirectory, DuplicatePluginNameResolution) {
    // If both directories have "MyPlugin", first one wins
    auto factory = std::make_shared<NodeFactory>();
    factory->AddPluginDirectory(PLUGIN_DIR1);  // Has MyPlugin
    factory->AddPluginDirectory(PLUGIN_DIR2);  // Also has MyPlugin
    factory->LoadAllPluginsFromDirectories();
    
    // Verify MyPlugin from DIR1 is registered (first match wins)
    auto info = factory->GetPluginRegistry()->GetNodeTypeInfo("MyPlugin");
    EXPECT_EQ(info->plugin_path, /* DIR1 path */);
}

TEST(NodeFactoryMultiDirectory, BackwardCompatibility) {
    // Old code should still work
    auto plugin_registry = std::make_shared<PluginRegistry>();
    auto loader = std::make_shared<PluginLoader>("./plugins", plugin_registry);
    auto factory = std::make_shared<NodeFactory>(plugin_registry);
    factory->SetPluginLoader(loader);
    factory->Initialize();  // Should work
}
```

### 7.2 Integration Tests

- [ ] Test with 3+ real plugin directories
- [ ] Test with missing/invalid directories
- [ ] Test with overlapping plugin names
- [ ] Test with deeply nested directory structures
- [ ] Test concurrent loading from multiple directories

---

## 8. Configuration File Example

Optional `graphx_plugins.conf`:
```yaml
plugin_directories:
  - path: "/usr/local/lib/graphx/plugins"
    priority: 100
  - path: "./plugins"
    priority: 50
  - path: "./test_plugins"
    priority: 10

loading_options:
  fail_on_missing_directory: false
  skip_invalid_plugins: true
  log_level: TRACE
```

---

## 9. Summary and Recommendations

| Aspect | Recommendation |
|--------|-----------------|
| **Approach** | Option A: Multiple PluginLoader instances |
| **Scope** | Phase 1 (non-breaking additions) |
| **Timeline** | 1-2 days for core implementation + tests |
| **Risk Level** | Low (additive changes) |
| **Test Coverage** | Unit tests for all new methods |
| **Documentation** | Update README with multi-directory examples |
| **Backward Compat** | 100% maintained |

**Key Principle:** Support multiple plugin directories without breaking existing single-directory code.

---

## 10. Code Review Checklist

- [ ] PluginLoader and PluginRegistry unchanged (Option A)
- [ ] No breaking changes to public API
- [ ] All new methods have comprehensive unit tests
- [ ] Documentation updated with examples
- [ ] Backward compatibility verified
- [ ] Error handling for missing/invalid directories
- [ ] Logging at appropriate levels (TRACE for details, WARN for errors)
- [ ] Memory management (shared_ptr cleanup)
- [ ] Thread safety (if applicable)

