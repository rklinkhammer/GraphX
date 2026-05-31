# EdgeRegistry Unit Test Analysis

**Date**: May 10, 2026  
**Component**: `graph::config::EdgeRegistry`  
**Current Test Status**: ❌ No unit tests (0 tests)  
**Integration Test Status**: ❌ No integration tests  
**C++26 Compliance**: Partial (uses `std::type_index`, `std::mutex`, modern patterns)

---

## Executive Summary

**EdgeRegistry** is a critical but **completely untested** component responsible for runtime creation of template-based edges via a creator function registry. It's a key infrastructure piece for JSON-based graph loading and dynamic graph construction.

### Critical Gaps

| Aspect | Status | Impact |
|--------|--------|--------|
| **Unit Tests** | ❌ 0/0 | Unknown behavior of thread safety, registration, and edge creation |
| **Integration Tests** | ❌ 0/0 | No validation of GraphManager integration |
| **C++26 Features** | ⚠️ Partial | Not utilizing all modern C++ capabilities |
| **Thread Safety** | ⚠️ Untested | Mutex present but behavior unvalidated |
| **Error Handling** | ❌ Unknown | Exception behavior not verified |

---

## Architecture Overview

### Purpose
`EdgeRegistry` solves a fundamental problem: How to create `Edge<SrcNode, SrcPort, DstNode, DstPort>` when node types and port indices are only known at runtime?

### Solution Pattern
```cpp
// Compile-time: Register type-aware creator lambda
EdgeRegistry::Register<NodeA, 0, NodeB, 0>(
    "NodeA", "NodeB",
    [](GraphManager& g, size_t src, size_t dst) {
        return g.AddEdge<NodeA, 0, NodeB, 0>(...);
    }
);

// Runtime: Look up and dispatch via type name
EdgeRegistry::CreateEdge(
    graph, "NodeA", 0, "NodeB", 0, src_idx, dst_idx, buffer_size
);
```

### Key Design Decisions

#### 1. **Type-Index Based Lookup (O(1))**
```cpp
struct EdgeKey {
    std::type_index src_type;   // Optimized for hashing
    std::size_t src_port;
    std::type_index dst_type;   // Faster than string comparison
    std::size_t dst_port;
};
```
- **Benefit**: O(1) lookup vs O(log n) for string maps
- **Trade-off**: Type names not stored (optimization)
- **C++26 Opportunity**: Use `std::expected<T, E>` for better error handling

#### 2. **Singleton Pattern with Mutex**
```cpp
static std::unordered_map<EdgeKey, EdgeCreator, EdgeKeyHash>& GetRegistry();
static std::mutex mutex_;
```
- **Thread Safety**: Mutex guards all operations
- **C++26 Opportunity**: Use `std::atomic<std::shared_ptr<>>` or scope guards

#### 3. **Closure Capture with EdgeCreator**
```cpp
using EdgeCreator = std::function<bool(GraphManager&, std::size_t, std::size_t, std::size_t)>;
```
- **Flexibility**: Allows complex creation logic per edge type
- **C++26 Opportunity**: Use `std::move_only_function` for better performance

---

## Current Implementation Analysis

### Public API

| Method | Signature | Purpose | Status |
|--------|-----------|---------|--------|
| `Register<...>()` | Template method | Register edge creator lambda | ✅ Implemented |
| `CreateEdge()` | Static method | Create edge from type names | ✅ Implemented |
| `IsRegistered()` | Static method | Check if creator registered | ⚠️ Incomplete |
| `Clear()` | Static method | Clear all registrations | ✅ Implemented |
| `GetRegisteredCount()` | Static method | Get registry size | ✅ Implemented |
| `GetRegistered()` | Static method | Get debug info | ❌ Not implemented |

### Known Issues

#### Issue 1: **IsRegistered() Always Returns True**
```cpp
bool EdgeRegistry::IsRegistered(...) {
    std::lock_guard<std::mutex> lock(EdgeRegistry::mutex_);
    return !GetRegistry().empty();  // ❌ WRONG - returns true if ANY registered
}
```
- **Problem**: Doesn't actually validate the specific edge type combination
- **Impact**: Callers can't reliably check if an edge type is registered
- **Test Gap**: No tests catch this bug

#### Issue 2: **CreateEdge() Uses Trial-and-Error**
```cpp
for (const auto& pair : GetRegistry()) {
    try {
        return pair.second(graph, src_node_idx, dst_node_idx, buffer_size);
    } catch (const std::exception&) {
        continue;  // ❌ Try next creator
    }
}
```
- **Problem**: O(n) lookup instead of O(1), no error tracking
- **Impact**: Performance degrades with registry size, false successes possible
- **Test Gap**: Performance characteristics not validated

#### Issue 3: **GetRegistered() Returns Placeholder**
```cpp
std::vector<std::string> EdgeRegistry::GetRegistered() {
    std::lock_guard<std::mutex> lock(EdgeRegistry::mutex_);
    std::vector<std::string> result;
    result.push_back("(Unable to list - type names not stored for optimization)");
    return result;  // ❌ Not useful for debugging
}
```
- **Problem**: Debug method returns placeholder instead of useful information
- **Impact**: No way to list registered edge types during development
- **Test Gap**: Debug utilities not validated

---

## Required Unit Tests

### PART 1: Registration Tests (6 tests)

```cpp
TEST(EdgeRegistryTest, RegisterSingleEdgeCreator) {
    // Verify Register() stores creator in registry
    // Check: GetRegisteredCount() == 1 after Register()
}

TEST(EdgeRegistryTest, RegisterMultipleEdgeCreators) {
    // Register 5 different edge type combinations
    // Check: GetRegisteredCount() == 5
}

TEST(EdgeRegistryTest, RegisterDuplicateThrows) {
    // Register same edge type twice
    // Check: std::runtime_error thrown with "already registered"
}

TEST(EdgeRegistryTest, RegisterUsesTypeIndexNotName) {
    // Register edge with type_index-based lookup
    // Verify: Same SrcNode, SrcPort, DstNode, DstPort -> same key (O(1) capable)
}

TEST(EdgeRegistryTest, ClearRemovesAllCreators) {
    // Register 3 creators, Clear(), register 2 new ones
    // Check: GetRegisteredCount() == 2 (old ones removed)
}

TEST(EdgeRegistryTest, RegistrationIsSingleton) {
    // Register in one GetRegistry() call
    // Check: Same registry visible in second GetRegistry() call
}
```

### PART 2: Edge Creation Tests (8 tests)

```cpp
TEST(EdgeRegistryTest, CreateEdgeWithRegisteredType) {
    // Register creator for NodeA -> NodeB
    // Call CreateEdge() with matching types
    // Check: Returns true, edge created in GraphManager
}

TEST(EdgeRegistryTest, CreateEdgeWithUnregisteredTypeThrows) {
    // Call CreateEdge() without prior Register()
    // Check: std::runtime_error with "No edge creator registered"
}

TEST(EdgeRegistryTest, CreateEdgeInvokesClosure) {
    // Register creator that increments counter
    // Call CreateEdge()
    // Check: Counter incremented (closure was called)
}

TEST(EdgeRegistryTest, CreateEdgePropagatesException) {
    // Register creator that throws custom exception
    // Call CreateEdge()
    // Check: Exception re-thrown to caller
}

TEST(EdgeRegistryTest, CreateEdgeWithCorrectNodeIndices) {
    // Register creator that validates src_idx and dst_idx
    // Call CreateEdge() with specific indices
    // Check: Indices passed correctly to creator
}

TEST(EdgeRegistryTest, CreateEdgeWithBufferSize) {
    // Register creator that stores buffer_size
    // Call CreateEdge() with buffer_size=1024
    // Check: Creator received buffer_size=1024
}

TEST(EdgeRegistryTest, CreateEdgeMultipleSequential) {
    // Create same edge type 5 times sequentially
    // Check: All 5 succeed with different node indices
}

TEST(EdgeRegistryTest, CreateEdgePartialMatch) {
    // Register NodeA -> NodeB
    // Create NodeA -> NodeC (different dst port)
    // Check: Fails appropriately (no creator found)
}
```

### PART 3: Thread Safety Tests (5 tests)

```cpp
TEST(EdgeRegistryTest, RegisterFromMultipleThreads) {
    // 4 threads each Register() 3 edge types
    // Check: GetRegisteredCount() == 12 (no lost registrations)
}

TEST(EdgeRegistryTest, CreateEdgeWhileRegistering) {
    // Thread A: Register creators
    // Thread B: CreateEdge() in parallel
    // Check: Both succeed without deadlock or race conditions
}

TEST(EdgeRegistryTest, ClearWhileCreating) {
    // Thread A: CreateEdge() in loop
    // Thread B: Clear() 
    // Check: No segfaults, proper exception handling
}

TEST(EdgeRegistryTest, IsRegisteredFromMultipleThreads) {
    // 4 threads call IsRegistered() concurrently
    // Check: All receive consistent results
}

TEST(EdgeRegistryTest, GetRegisteredCountConsistent) {
    // 4 threads register and query count
    // Check: Count always >= previous count
}
```

### PART 4: Error Handling Tests (6 tests)

```cpp
TEST(EdgeRegistryTest, IsRegisteredReturnsTrueForExactMatch) {
    // Register NodeA:0 -> NodeB:0
    // Check: IsRegistered("NodeA", 0, "NodeB", 0) returns true
}

TEST(EdgeRegistryTest, IsRegisteredReturnsFalseForDifferentPort) {
    // Register NodeA:0 -> NodeB:0
    // Check: IsRegistered("NodeA", 1, "NodeB", 0) returns false
}

TEST(EdgeRegistryTest, IsRegisteredReturnsFalseForDifferentNode) {
    // Register NodeA -> NodeB
    // Check: IsRegistered("NodeC", 0, "NodeB", 0) returns false
}

TEST(EdgeRegistryTest, CreateEdgeErrorMessageIsDescriptive) {
    // Try to create unregistered edge
    // Check: Error includes src type, dst type, port indices
}

TEST(EdgeRegistryTest, RegistrationErrorMessageOnDuplicate) {
    // Register same type twice
    // Check: Error message includes "already registered" + debug key
}

TEST(EdgeRegistryTest, GetRegisteredCountAgreesWithSize) {
    // Register 7 creators, GetRegisteredCount()
    // Check: Count == 7
}
```

### PART 5: Type Safety Tests (4 tests)

```cpp
TEST(EdgeRegistryTest, TypeIndexHashingConsistent) {
    // Register with std::type_index
    // Verify: Same type_index always hashes same
}

TEST(EdgeRegistryTest, DifferentPortsSeparateCreators) {
    // Register NodeA:0 -> NodeB:0 and NodeA:1 -> NodeB:0
    // Check: GetRegisteredCount() == 2 (different ports = different keys)
}

TEST(EdgeRegistryTest, SameTypesDifferentInstancesSeparateCreators) {
    // Register same template types with different creator logic
    // Check: Can't register twice (existing check prevents this)
}

TEST(EdgeRegistryTest, EdgeKeyHashCollisionsRare) {
    // Register 50 different edge type combinations
    // Check: No false positives in lookup (creators don't interfere)
}
```

### PART 6: C++26 Feature Tests (4 tests)

```cpp
TEST(EdgeRegistryTest, MoveSemanticsMaintained) {
    // Register creator as rvalue
    // Verify: Creator moved, not copied
}

TEST(EdgeRegistryTest, MutexGuardLockScope) {
    // Call Register() and CreateEdge()
    // Verify: Locks are scoped correctly (no deadlocks)
}

TEST(EdgeRegistryTest, ConstexprTypeIndexIfSupported) {
    // Verify: type_index operations are efficient
}

TEST(EdgeRegistryTest, LockGuardRAII) {
    // Register with exception in creator
    // Verify: Mutex properly released (RAII guaranteed)
}
```

---

## C++26 Compliance Analysis

### Current Usage
```cpp
std::unordered_map<EdgeKey, EdgeCreator, EdgeKeyHash>
std::function<bool(GraphManager&, std::size_t, std::size_t, std::size_t)>
std::lock_guard<std::mutex>
std::type_index
std::type_info
```

### Missing C++26 Opportunities

#### 1. **Use `std::expected<T, E>` for Better Error Handling**

**Current**:
```cpp
static bool CreateEdge(...);  // Returns bool, loses error info
// Caller doesn't know WHY it failed
```

**C++26 Improvement**:
```cpp
static std::expected<EdgeHandle, EdgeRegistryError> CreateEdge(...);

enum class EdgeRegistryError {
    NotRegistered,
    InvalidNodeIndices,
    PortMismatch,
    CreationFailed
};
```

**Benefits**: Type-safe error handling, no exception overhead, better caller experience

#### 2. **Use `std::move_only_function` Instead of `std::function`**

**Current**:
```cpp
using EdgeCreator = std::function<bool(GraphManager&, std::size_t, std::size_t, std::size_t)>;
// Can be copied, adds unnecessary overhead
```

**C++26 Improvement**:
```cpp
using EdgeCreator = std::move_only_function<bool(GraphManager&, std::size_t, std::size_t, std::size_t)>;
// Move-only, no copy overhead, better semantics
```

**Benefits**: Better performance, clearer intent, prevents accidental copies

#### 3. **Utilize `std::source_location` for Better Debugging**

**Current**:
```cpp
void Register(...) {
    if (GetRegistry().count(key) > 0) {
        throw std::runtime_error("already registered");
        // No information where Register() was called
    }
}
```

**C++26 Improvement**:
```cpp
void Register(..., std::source_location loc = std::source_location::current()) {
    if (GetRegistry().count(key) > 0) {
        std::ostringstream oss;
        oss << "Already registered at " << loc.file_name()
            << ":" << loc.line();
        throw std::runtime_error(oss.str());
    }
}
```

**Benefits**: Runtime call site information, better debugging

#### 4. **Use Concepts for Creator Validation**

**Current**:
```cpp
using EdgeCreator = std::function<...>;
// No compile-time validation of creator signature
```

**C++26 Improvement**:
```cpp
template <typename F>
concept EdgeCreatorConcept = requires(F f, GraphManager& g, std::size_t s1, std::size_t s2, std::size_t s3) {
    { f(g, s1, s2, s3) } -> std::convertible_to<bool>;
};

template <typename SrcNode, std::size_t SrcPort, typename DstNode, std::size_t DstPort, EdgeCreatorConcept Creator>
static void Register(Creator&& creator) {
    // Compile-time validation!
}
```

**Benefits**: Compile-time safety, better error messages

#### 5. **Use `std::shared_ptr<std::atomic<bool>>` for Double-Check Locking**

**Current**:
```cpp
static std::mutex mutex_;
static std::unordered_map<EdgeKey, EdgeCreator, EdgeKeyHash>& GetRegistry();
// Every operation requires lock acquisition
```

**C++26 Improvement** (if registry rarely changes):
```cpp
// Check without lock first (compiler optimizations)
// Only acquire lock on write
if (auto it = GetRegistry().find(key); it != GetRegistry().end()) {
    return true;  // Fast path, no lock
}
```

**Benefits**: Performance for read-heavy workloads

#### 6. **Structured Bindings for EdgeKey**

**Current**:
```cpp
for (const auto& pair : GetRegistry()) {
    auto creator = pair.second;  // pair.first is EdgeKey, unused
}
```

**C++26 Improvement**:
```cpp
for (const auto& [key, creator] : GetRegistry()) {
    // Cleaner - key is EdgeKey, creator is EdgeCreator
}
```

**Benefits**: More readable, modern C++ idiom

---

## Comprehensive Test Gap Analysis

### Test Coverage by Category

| Category | Tests Needed | Implementation | Status |
|----------|--------------|-----------------|--------|
| **Registration** | 6 | Medium (fixture setup) | ❌ None |
| **Edge Creation** | 8 | Medium (needs GraphManager mock) | ❌ None |
| **Thread Safety** | 5 | Hard (timing, synchronization) | ❌ None |
| **Error Handling** | 6 | Medium (exception verification) | ❌ None |
| **Type Safety** | 4 | Medium (type_index validation) | ❌ None |
| **C++26 Features** | 4 | Medium (semantics validation) | ❌ None |
| **Total** | **33 tests** | **Medium-Hard** | **❌ All Missing** |

### Mock/Fixture Requirements

#### 1. **GraphManager Mock**
EdgeRegistry needs GraphManager to create edges. Mocking requirements:
- `GetNode(idx)` - return mock nodes
- `AddEdge<SrcNode, Port, DstNode, Port>()` - return success/failure
- State tracking for verification

#### 2. **Test Node Types**
Need simple test nodes for registration:
```cpp
struct TestNodeA : INode { /* minimal */ };
struct TestNodeB : INode { /* minimal */ };
struct TestNodeC : INode { /* minimal */ };
```

#### 3. **Thread Safety Testing**
- Use `std::thread` with barriers for synchronization
- `std::atomic<int>` counters to verify concurrent behavior
- Race condition detection via sanitizers

---

## Recommendations

### Priority 1: CRITICAL (Week 1)

**Implement Basic Registration & Creation Tests**
- 6 registration tests (verify Register stores and prevents duplicates)
- 4 creation tests (basic create success/failure)
- **Effort**: 3-4 hours
- **Impact**: Validates core functionality

**Fix Known Bugs**
1. **IsRegistered()** - Implement proper lookup by EdgeKey
2. **CreateEdge()** - Replace trial-and-error with O(1) lookup
3. **GetRegistered()** - Return useful debug information

### Priority 2: IMPORTANT (Week 2)

**Implement Thread Safety Tests** (5 tests)
- Multi-threaded registration
- Concurrent create while registering
- Clear during active use
- **Effort**: 2-3 hours
- **Impact**: Validates production readiness

**Implement Error Handling Tests** (6 tests)
- Exact type matching for IsRegistered()
- Descriptive error messages
- Exception propagation
- **Effort**: 2 hours
- **Impact**: Better debugging, reliability

### Priority 3: IMPORTANT (Week 2)

**Implement Type Safety Tests** (4 tests)
- Type index hashing consistency
- Port separation verification
- Hash collision testing
- **Effort**: 2 hours
- **Impact**: Validates type system correctness

### Priority 4: ENHANCEMENT (Week 3)

**Implement C++26 Feature Tests** (4 tests)
- Move semantics
- Lock scoping
- RAII guarantees
- **Effort**: 1-2 hours
- **Impact**: Validates modern C++ practices

**Refactor for C++26**
- Use `std::expected<T, E>` instead of bool return
- Use `std::move_only_function` instead of `std::function`
- Add `std::source_location` to Register()
- Create EdgeCreatorConcept for compile-time safety
- **Effort**: 4-6 hours
- **Impact**: Better performance, type safety, error handling

### Priority 5: NICE-TO-HAVE (Later)

**Performance Testing**
- Benchmark Registry lookup with N creators
- Verify O(1) behavior vs O(n)
- **Effort**: 1-2 hours

**Integration Testing**
- Test with real GraphManager
- JSON graph loading with EdgeRegistry
- **Effort**: 2-3 hours

---

## Test File Structure Recommendation

```cpp
// libgraph/test/unit/test_edge_registry.cpp

#include <gtest/gtest.h>
#include "graph/EdgeRegistry.hpp"
#include "graph/GraphManager.hpp"
#include "test/TestNode.hpp"
#include <thread>

namespace {

class EdgeRegistryTest : public ::testing::Test {
protected:
    void SetUp() override {
        EdgeRegistry::Clear();  // Clean state
    }
    
    void TearDown() override {
        EdgeRegistry::Clear();  // Cleanup
    }
};

// PART 1: Registration Tests (6)
TEST_F(EdgeRegistryTest, RegisterSingleEdgeCreator) { /*...*/ }
TEST_F(EdgeRegistryTest, RegisterMultipleEdgeCreators) { /*...*/ }
// ... 4 more registration tests

// PART 2: Edge Creation Tests (8)
TEST_F(EdgeRegistryTest, CreateEdgeWithRegisteredType) { /*...*/ }
TEST_F(EdgeRegistryTest, CreateEdgeWithUnregisteredTypeThrows) { /*...*/ }
// ... 6 more creation tests

// PART 3: Thread Safety Tests (5)
TEST_F(EdgeRegistryTest, RegisterFromMultipleThreads) { /*...*/ }
// ... 4 more thread safety tests

// PART 4: Error Handling Tests (6)
TEST_F(EdgeRegistryTest, IsRegisteredReturnsTrueForExactMatch) { /*...*/ }
// ... 5 more error handling tests

// PART 5: Type Safety Tests (4)
TEST_F(EdgeRegistryTest, TypeIndexHashingConsistent) { /*...*/ }
// ... 3 more type safety tests

// PART 6: C++26 Feature Tests (4)
TEST_F(EdgeRegistryTest, MoveSemanticsMaintained) { /*...*/ }
// ... 3 more C++26 tests

}  // namespace
```

---

## Summary Table

| Aspect | Current | Recommended | Impact |
|--------|---------|------------|--------|
| **Unit Tests** | 0 | 33 | Comprehensive coverage |
| **Thread Safety** | Untested | 5 tests | Production readiness |
| **Error Handling** | Buggy | Fixed + 6 tests | Reliability |
| **C++26 Features** | Partial | Full refactor | Performance + Safety |
| **Known Issues** | 3 bugs | All fixed | Correctness |
| **Test File** | None | test_edge_registry.cpp | Complete validation |

---

## Conclusion

**EdgeRegistry is a CRITICAL UNTESTED COMPONENT with:**
- ❌ Zero unit tests
- ⚠️ 3 known implementation bugs
- ⚠️ Untested thread safety
- ❌ Missing C++26 optimizations

**Recommended Action**: Implement 33 comprehensive unit tests in 1-2 weeks following the Priority 1-3 roadmap. This will validate a crucial piece of the graph loading infrastructure and catch important reliability issues.

---

**Report Generated**: May 10, 2026  
**Analysis Scope**: EdgeRegistry.hpp/cpp + EdgeKey + EdgeKeyHash  
**Recommendation**: Start with Priority 1 (Registration + Creation) tests in Week 1

