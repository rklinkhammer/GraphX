# Topology5_SplitSimple Crash Debugging Guide

## Issue
Test crashes after ~10 messages consumed (after 3 SinkTestNode outputs).  
Segmentation fault occurs in Topology5_SplitSimple (dual-sink scenario) when SinkTestNode metrics re-enabled.

## Previous Analysis
- **Root cause identified:** Heap-use-after-free (callback freed while edge threads still running)
- **Attempted fix:** MetricsPolicy now stores callbacks in shared_ptr map
- **Current status:** Still crashes despite lifetime fix

## Hypothesis
Even though MetricsPolicy keeps callbacks alive via shared_ptr in `metrics_node_callbacks_` map:
1. The policy might be destroyed before the map is cleaned
2. OR: Raw pointer stored in SinkTestNode becomes invalid during shutdown
3. OR: There's a race between node destruction and callback access

## Instrumentation Strategy

### 1. Add Lifetime Logging to MetricsCapabilityCallback

In `libgraph/include/policies/MetricsPolicy.hpp`, modify:
```cpp
struct MetricsCapabilityCallback : public graph::IMetricsCallback {
    MetricsCapabilityCallback() {
        std::cerr << "[DEBUG] MetricsCapabilityCallback constructed at " << this << "\n";
    }
    ~MetricsCapabilityCallback() {
        std::cerr << "[DEBUG] MetricsCapabilityCallback destroyed at " << this << "\n";
    }
    
    bool PublishAsync(const app::metrics::MetricsEvent& event) noexcept override {
        std::cerr << "[DEBUG] PublishAsync called on " << this << "\n";
        std::lock_guard<std::mutex> lock(publish_mutex_);
        if (on_publish_async_) {
            return on_publish_async_(event);
        }
        return false;
    }
    // ... rest of struct
};
```

### 2. Add Lifetime Logging to SinkTestNode Metrics

In `libgraph/test/include/test/AdvancedTestNodes.hpp`, SinkTestNode::Consume():
```cpp
if (metrics_callback_) {
    std::cerr << "[DEBUG] SinkTestNode::Consume() accessing metrics_callback_ at " 
              << metrics_callback_ << "\n";
    std::lock_guard<std::mutex> lock(metrics_mutex_);
    // ... rest of metrics code
} else {
    std::cerr << "[DEBUG] SinkTestNode::Consume() - metrics_callback_ is nullptr!\n";
}
```

### 3. Add Logging to MetricsPolicy

In `libgraph/src/policies/MetricsPolicy.cpp`, InitMetricsSources():
```cpp
auto metrics_callback = std::make_shared<MetricsCapabilityCallback>();
std::cerr << "[DEBUG] MetricsPolicy created callback at " << metrics_callback.get() 
          << " for node " << node_name << "\n";
metrics_node->SetMetricsCallback(metrics_callback.get());
std::cerr << "[DEBUG] MetricsPolicy set raw pointer " << metrics_callback.get() 
          << " on node\n";
AddNodeMetrics(node_name, metrics_callback, metrics_node->GetNodeMetricsSchema());
std::cerr << "[DEBUG] MetricsPolicy stored shared_ptr for " << node_name 
          << " (ref count = " << metrics_callback.use_count() << ")\n";
```

### 4. Add Logging to MetricsPolicy Lifecycle

```cpp
void OnJoin(capabilities::GraphCapability &) override {
    std::cerr << "[DEBUG] MetricsPolicy::OnJoin() starting\n";
    if (metrics_thread_.joinable()) {
        metrics_thread_.join();
        std::cerr << "[DEBUG] MetricsPolicy metrics_thread_ joined\n";
    }
    
    // Drain queue
    app::metrics::MetricsEvent event;
    size_t drained_count = 0;
    while (metrics_event_queue_.DequeueNonBlocking(event)) {
        metrics_capability_->InvokeSubscribers(event);
        ++drained_count;
    }
    std::cerr << "[DEBUG] MetricsPolicy::OnJoin() drained " << drained_count 
              << " events, callback map size = " << metrics_node_callbacks_.size() << "\n";
}
// Destructor
~MetricsPolicy() {
    std::cerr << "[DEBUG] MetricsPolicy destructor called, clearing " 
              << metrics_node_callbacks_.size() << " callbacks\n";
    metrics_node_callbacks_.clear();
    std::cerr << "[DEBUG] MetricsPolicy destructor done - callbacks destroyed\n";
}
```

## Debug Run Steps

1. Apply instrumentation changes above
2. Rebuild: `cd /Users/rklinkhammer/workspace/GraphX/build && make test_libgraph_unit -j4`
3. Run only Topology5: `./libgraph/test/test_libgraph_unit --gtest_filter="TopologiesSimple.Topology5_SplitSimple" 2>&1 | tee /tmp/debug.log`
4. Analyze output for:
   - When callbacks are created
   - When callbacks are destroyed
   - When SinkTestNode tries to access them
   - Order of lifecycle events

## Expected Debug Output Pattern (if working)
```
[DEBUG] MetricsPolicy created callback at 0x7f1234567890 for node SinkTestNode
[DEBUG] MetricsPolicy set raw pointer 0x7f1234567890 on node
[DEBUG] MetricsPolicy stored shared_ptr for SinkTestNode (ref count = 2)
[DEBUG] MetricsCapabilityCallback constructed at 0x7f1234567890
[DEBUG] SinkTestNode::Consume() accessing metrics_callback_ at 0x7f1234567890
[DEBUG] PublishAsync called on 0x7f1234567890
...more messages...
[DEBUG] MetricsPolicy::OnJoin() starting
[DEBUG] MetricsPolicy metrics_thread_ joined
[DEBUG] MetricsPolicy::OnJoin() drained N events, callback map size = 7
[DEBUG] MetricsPolicy destructor called, clearing 7 callbacks
[DEBUG] MetricsCapabilityCallback destroyed at 0x7f1234567890
[DEBUG] MetricsPolicy destructor done - callbacks destroyed
```

## Problem Indicators to Look For

1. **Nullptr access:** `metrics_callback_ is nullptr!` before crash
2. **Freed pointer access:** `SinkTestNode::Consume() accessing metrics_callback_` followed immediately by crash (before PublishAsync message)
3. **Early destruction:** Destructor message before OnJoin message
4. **Wrong pointer:** Different addresses in creation vs. consumption

## Additional Investigation

If instrumentation doesn't reveal the issue:
1. Check if nodes are being destroyed while edge threads still run
2. Look for policies being cleared/destroyed in wrong order
3. Verify ExecutionPolicyChain lifecycle matches node lifecycle
4. Check if test itself is destroying nodes before executor cleanup

---

**Next step:** Apply instrumentation and run debug test to capture the exact sequence of events.
