# Implementation Plan: Priority 1 - Metrics Policy Gaps
## Focused, Non-Duplicative Test Suite

**Created**: May 29, 2026  
**File**: `libgraph/test/unit/test_metrics_policy_gaps.cpp`  
**Scope**: 95 focused tests addressing ONLY actual gaps  
**Execution Time**: ~8 seconds  
**Coverage**: Production-critical issues NOT covered by existing tests

---

## Executive Summary

The existing test suite (47 tests across 5 files) thoroughly covers:
- ✅ Metrics event publishing (all 5 types across 10 topologies)
- ✅ MetricsCapability registration during Init()
- ✅ Metrics callback installation
- ✅ Completion callback tracking
- ✅ Policy lifecycle (OnInit/Start/Stop/Join)
- ✅ All topology scenarios with metrics verification

**This plan focuses ONLY on the 8 critical gaps not covered elsewhere**, eliminating ~40% duplication from the original plan.

---

## Gap #1: Event Queue Mechanics & Capacity (15 tests)

### Purpose
MetricsEventQueue internal behavior and boundary conditions. Session notes indicate a potential capacity=0 issue causing silent event dropping.

### Test Scenarios

#### A. Queue Initialization (3 tests)
1. **QueueInitializedWithCorrectCapacity**
   - Create MetricsCapabilityCallback
   - Verify MetricsEventQueue capacity > 0 (not 0)
   - Assertion: `queue.capacity() > 0`

2. **QueueInitiallyEmpty**
   - New callback has size 0
   - DequeueNonBlocking() returns nullptr

3. **CapacityNotExceeded**
   - Enqueue 1000 events
   - Verify queue size <= capacity (unbounded or bounded correctly)

#### B. FIFO Ordering & Dequeue (4 tests)
4. **EnqueueDequeuePreservesOrder**
   - Enqueue: event1(produced), event2(consumed), event3(transfer)
   - DequeueNonBlocking() sequence
   - Assert: events in same order as enqueued

5. **DequeueBlockingWaitsForEvent**
   - Call Dequeue() on empty queue from thread 1
   - From thread 2, enqueue event after 50ms
   - Assert: Dequeue() unblocks and returns event

6. **DequeueNonBlockingReturnsNullImmediately**
   - Call DequeueNonBlocking() on empty queue
   - Assert: Returns nullptr immediately (no blocking)

7. **MultipleDequeuesExhaustQueue**
   - Enqueue 5 events
   - DequeueNonBlocking() 5 times
   - Assert: 6th dequeue returns nullptr

#### C. Queue Overflow & Unbounded Behavior (5 tests)
8. **UnboundedQueueAccepts10kEvents**
   - Enqueue 10,000 events rapidly
   - Assert: All events accepted (no throws, no drops)
   - Assert: Size == 10,000 post-enqueue

9. **QueueDoesNotSilentlyDropEvents**
   - Enqueue 1000 events
   - Track enqueue count
   - DequeueNonBlocking() all
   - Assert: dequeue_count == 1000 (no silent drops)

10. **EnqueueReturnsImmediately**
    - Measure enqueue time for 1000 events
    - Assert: Total time < 10ms (no blocking)

11. **DequeueUnderHighThroughput**
    - 10 producer threads enqueue 100 events each
    - 1 consumer thread dequeues all
    - Assert: Dequeued == 1000, no loss

12. **QueueMemoryCleanupOnDestruction**
    - Create queue, enqueue 1000 events
    - Destroy queue
    - ASAN validation: no leaks

#### D. Queue State Consistency (3 tests)
13. **QueueSizeMatchesEnqueueMinusDequeue**
    - Enqueue 50, dequeue 20, enqueue 30
    - Assert: size() == 60

14. **QueueNotificationMechanismUnblocks**
    - Thread A: Dequeue() blocking on empty
    - Thread B: Enqueue() event
    - Assert: Thread A unblocks with event

15. **QueueStateAfterSequentialOps**
    - Enqueue(a) → Dequeue → Enqueue(b,c) → DequeueAll
    - Assert: Events in order [b, c]

---

## Gap #2: Thread Initialization Race Conditions (12 tests)

### Purpose
Validate MetricsPolicy thread creation, initialization ordering, and synchronization barriers. Session notes show metrics thread must be properly initialized before events publish.

### Test Scenarios

#### A. OnInit vs OnStart Timing (4 tests)
16. **MetricsDiscoveryCompletesBeforeThreadStart**
    - Override MetricsPolicy::OnInit() with instrumentation
    - Track: callback_count set in OnInit()
    - Verify: callback_count > 0 before OnStart() returns
    - Assert: No race between discovery and start

17. **OnStartBlocksUntilThreadRunning**
    - Mock thread creation with 10ms delay
    - Measure OnStart() return time
    - Assert: Returns after thread started (not before)

18. **OnInitThenOnStartSequence**
    - Call OnInit(), verify metrics_thread not running
    - Call OnStart(), verify metrics_thread running
    - Assert: State transitions correct

19. **OnStartFailsBeforeOnInitCompletes**
    - Call Start() without Init()
    - Assert: Returns false (not crashing or hanging)

#### B. Queue Initialization Barrier (3 tests)
20. **QueueInitializedBeforeFirstPublish**
    - OnInit() sets up queue
    - Verify queue exists before node publishes
    - Publish event
    - Assert: Event queued (not dropped)

21. **ThreadStartupCompletesBeforeNodeExecution**
    - Instrument MetricsPolicy for startup completion notification
    - Execute graph with short timeout
    - Assert: At least 1 metric event captured (thread was ready)

22. **InitializationBarrierPreventsRaceConditions**
    - Call OnInit() + OnStart() from 2 threads simultaneously
    - Assert: No TSAN warnings, no corrupted state

#### C. Callback Registration Synchronization (5 tests)
23. **CallbacksRegisteredBeforePublishStarts**
    - In graph execution, capture first publish timestamp
    - Verify all callbacks registered before that timestamp
    - Assert: SetMetricsCallback() called before first Consume()

24. **MetricsThreadReadyBeforeGraphExecution**
    - Hook into executor: OnStart() → Verify thread running → Run()
    - Assert: No events lost at startup

25. **MultipleCallbackRegistrationsThreadSafe**
    - Register 10 callbacks simultaneously from different threads
    - Assert: All registered, no TSAN warnings

26. **CallbackPointerStabilityAfterRegistration**
    - Get callback pointer after registration
    - Access from different thread
    - Assert: Pointer valid, no seg faults

27. **UnblockThreadStartupOnDestruction**
    - Destroy MetricsPolicy while OnStart() mid-execution
    - Assert: Destructor completes cleanly (doesn't hang)

---

## Gap #3: Concurrent Publishing Stress Test (20 tests)

### Purpose
Validate metrics system under high-load concurrent scenarios. Existing tests use moderate topology-scale publishing; gap is high-throughput stress.

### Test Scenarios

#### A. High-Throughput Publishing (5 tests)
28. **PublishingThroughput1kEventsPerSecond**
    - Spawn 5 producer nodes
    - Each publishes 200 events/sec (1000 total/sec)
    - Duration: 5 seconds = 5000 events
    - Assert: All events queued (no loss)
    - Measure: CPU overhead < 10%

29. **PublishingThroughput10kEventsPerSecond**
    - 10 producer nodes × 1000 events/sec = 10k/sec
    - Duration: 2 seconds = 20k events
    - Assert: No data corruption, no ASAN warnings
    - Measure: Thread pool doesn't saturate

30. **SustainedPublishingWithManyProducers**
    - 50 SourceTestNode instances (all with metrics)
    - Each publishes 100 events over 10 seconds
    - Assert: Event count == 50 × 100 = 5000

31. **PublisherThreadPoolIntegrationUnderLoad**
    - 10 worker threads in ThreadPool
    - Each publishes metrics while processing messages
    - Assert: No deadlocks, metrics keep up

32. **PublishingRateStableAcrossRuntime**
    - Measure publish latency at: 0s, 2s, 4s, 6s, 8s, 10s
    - Assert: Latency doesn't degrade over time (< 5% variance)

#### B. Event Ordering Under Concurrency (5 tests)
33. **EventOrderingWithConcurrentProducers**
    - 5 threads each enqueue 100 ordered events
    - Verify: Events from each thread maintain order
    - Assert: No interleaving corruption

34. **TimestampOrderingWithConcurrentPublish**
    - Collect events from 5 concurrent sources
    - Verify: timestamp[i] <= timestamp[i+1] globally
    - Assert: Monotonic increase (or equal for same-ms events)

35. **SourceIdentificationAccuracyUnderLoad**
    - 10 nodes publish concurrently
    - Verify: Each event.source matches publishing node
    - Assert: No source field corruption

36. **EventCorrelationProducerToSubscriber**
    - Producer publishes event_id=123
    - Subscriber receives it
    - Assert: All fields intact (no data loss)

37. **OrderingGuaranteePerProducer**
    - Producer 1 enqueues [A, B, C]
    - Producer 2 enqueues [X, Y, Z] concurrently
    - Consumer receives (may interleave)
    - Assert: A before B before C (producer 1 order preserved)

#### C. Lock Contention & Performance (5 tests)
38. **MutexContentionUnder100Threads**
    - 100 threads competing for publish mutex
    - Measure: Lock acquisition time distribution
    - Assert: p99 < 5ms (no excessive contention)

39. **PublishLatencyDoesMNotIncreaseLinearlyWithThreadCount**
    - Measure publish latency with 1, 5, 10, 50, 100 threads
    - Assert: Latency grows sub-linearly (not O(n))

40. **QueueEnqueueNeverBlocks**
    - Verify enqueue never waits for dequeue
    - Measure: 100% completion of 10k enqueue in < 50ms
    - Assert: O(1) enqueue performance

41. **ThreadPoolContextSwitchingUnderLoad**
    - Monitor context switches during high publication
    - Assert: Switches < 100k (reasonable overhead)

42. **NoLivelock OrDeadlockUnderConcurrency**
    - Run test with timeout=10s
    - 20 concurrent producers + metrics thread
    - Assert: Test completes (no deadlock/livelock)

#### D. Subscriber Performance (5 tests)
43. **SubscriberInvocationKeepsUp**
    - 5 publishers × 1000 events/sec = 5000 events/sec
    - Subscriber counts events
    - Assert: All 5000 events received in 1 second

44. **MultipleSubscribersPerformance**
    - 5 concurrent publishers + 5 concurrent subscribers
    - Assert: Each subscriber receives all events
    - Measure: Latency from publish to subscriber < 100ms

45. **SubscriberExceptionDoesNotBlockMetrics**
    - Subscriber 1 throws on every 10th event
    - Subscriber 2 is normal
    - Assert: Subscriber 2 receives all events (not blocked by subscriber 1)

46. **SubscriberUnregistrationUnderLoad**
    - Register/unregister subscribers while publishing
    - Assert: No crashes, latency stable

47. **MemoryUsageDoesNotGrowUnbounded**
    - Publish 100k events
    - Measure memory before/after dequeue all
    - Assert: Memory released (no queue buildup)

---

## Gap #4: Event Content Validation (12 tests)

### Purpose
Validate that event fields (timestamp, source, data) are accurate and preserved. Existing tests only verify event *types* exist, not content.

### Test Scenarios

#### A. Timestamp Validation (4 tests)
48. **EventTimestampIsSet**
    - Publish event, subscribe
    - Verify: event.timestamp != 0
    - Assert: Valid timestamp

49. **EventTimestampsAreMonotonicallyIncreasing**
    - Publish 100 events
    - Verify: timestamp[i] <= timestamp[i+1]
    - Assert: No backward time jumps

50. **EventTimestampAccuracy**
    - Publish at T=100ms
    - Verify: event.timestamp within ±10ms of T
    - Assert: Timestamp within expected range

51. **TimestampPrecisionSufficientForOrdering**
    - Publish 1000 events as fast as possible
    - Count events with same timestamp
    - Assert: < 10% have duplicated timestamp (precise enough)

#### B. Event Source Identification (3 tests)
52. **EventSourceFieldMatchesPublishingNode**
    - SourceTestNode_1 publishes
    - Subscribe and verify: event.source == "SourceTestNode_1"
    - Assert: Correct source identified

53. **EventSourceWithDuplicateNodeTypes**
    - 2 SourceTestNodes (with unique names)
    - Each publishes
    - Assert: event.source correctly identifies which node

54. **EventSourceConsistencyAcrossPublishes**
    - Node publishes 100 events
    - Assert: All have same source field
    - Assert: Source == node's registered name

#### C. Event Data Preservation (3 tests)
55. **MessageDataSurvivesEventPublishing**
    - Create message with data="TestPayload_12345"
    - Node publishes event with this message
    - Subscriber receives event
    - Assert: event.data includes original payload

56. **LargeMessageDataPreservation**
    - Message with 1MB payload
    - Publish as event
    - Verify: All data present in event (not truncated)
    - Assert: Data integrity preserved

57. **SpecialCharactersInEventData**
    - Message with special chars: \0, \n, UTF-8, emojis
    - Publish and verify
    - Assert: No corruption or loss

#### D. Event Type Field Correctness (2 tests)
58. **EventTypeMatchesNodeType**
    - SourceTestNode publishes: type should be "message_produced"
    - SinkTestNode publishes: type should be "message_consumed"
    - Assert: Each node type publishes correct event type

59. **EventTypeConsistentAcrossInstances**
    - 5 SourceTestNode instances
    - All publish with type="message_produced"
    - Assert: Type field consistent across instances

---

## Gap #5: MetricsPolicy + CSVInjectionPolicy Interaction (10 tests)

### Purpose
Validate two policies work together without resource contention or initialization ordering issues.

### Test Scenarios

#### A. Initialization Ordering (3 tests)
60. **MetricsFirst_CSVSecondInitialization**
    - Builder adds MetricsPolicy then CSVInjectionPolicy
    - Execute: Init → Start → Run → Stop → Join
    - Assert: Both policies initialize successfully

61. **CSVFirst_MetricsSecondInitialization**
    - Builder adds CSVInjectionPolicy then MetricsPolicy
    - Execute full lifecycle
    - Assert: No resource conflicts, both functional

62. **OnInitCalledOnAllPoliciesRegardlessOfOrder**
    - Instrument both policies
    - Verify: Both OnInit() called, both register their capabilities
    - Assert: MetricsCapability and DataInjectionCapability both present

#### B. Concurrent Execution (3 tests)
63. **BothPoliciesStartThreadsIndependently**
    - OnStart() spawns both thread and CSV reader thread
    - Assert: Both threads running independently
    - Assert: No TSAN warnings for shared state

64. **CSVInjectionDoesNotBlockMetricsPublishing**
    - CSV thread reading slowly (1 event/100ms)
    - Metrics publishing at normal rate
    - Assert: Metrics throughput unaffected by CSV speed

65. **MetricsPublishingDoesNotBlockCSVInjection**
    - Many metrics events
    - CSV reader continues at normal pace
    - Assert: CSV events processed in order

#### C. Resource Contention (2 tests)
66. **NoResourceContentionForQueues**
    - Both policies have independent queues
    - Verify: CSVEventQueue capacity > 0
    - Verify: MetricsEventQueue capacity > 0
    - Assert: No shared queue infrastructure

67. **ThreadPoolResourcesSufficient**
    - Graph with CSV source + metrics enabled
    - 50 messages processed
    - Assert: CPU usage reasonable (not saturated)

#### D. Combined Failure Scenarios (2 tests)
68. **CSVErrorDoesNotDisableMetrics**
    - CSV reader encounters invalid file
    - OnStart() returns false for CSV policy
    - Verify: MetricsPolicy still running (continued after CSV failure)
    - Assert: Metrics events still published

69. **MetricsErrorDoesNotDisableCSV**
    - Mock metrics callback failure in OnInit()
    - Verify: CSV policy continues
    - Assert: CSV injection still functional

---

## Gap #6: Metrics Callback Lifecycle Details (10 tests)

### Purpose
Validate SetMetricsCallback() timing, weak_ptr vs raw_ptr safety, and schema registration.

### Test Scenarios

#### A. Callback Pointer Lifetime (4 tests)
70. **CallbackPointerValidAfterSetMetricsCallback**
    - Node calls SetMetricsCallback(ptr)
    - Store pointer in test variable
    - Later, attempt to call it: ptr->PublishAsync()
    - Assert: No seg fault, pointer valid

71. **CallbackSurvivesNodeDestruction**
    - Create node, set callback
    - Destroy node
    - Try to publish through saved callback
    - Assert: Either callback destroyed cleanly OR shared_ptr keeps alive

72. **SharedPtrManagementPreventsPrematureDestruction**
    - MetricsPolicy holds shared_ptr
    - Node holds raw_ptr
    - Node destroyed
    - OnJoin() drains queue using shared_ptr
    - Assert: Callback survives for drain operation

73. **MultipleNodesShareingSameCallbackIsUnsafe**
    - Two nodes get same callback pointer (intentional bug)
    - Verify: This causes problems (documents expected behavior)
    - Assert: Test captures issue (for future fix validation)

#### B. SetMetricsCallback Timing (3 tests)
74. **SetMetricsCallbackBeforeNodeStart**
    - Call SetMetricsCallback() after OnInit(), before OnStart()
    - Start node
    - Publish metric
    - Assert: Callback invoked

75. **SetMetricsCallbackAfterNodeStart**
    - Start node without callback
    - Call SetMetricsCallback() mid-execution
    - Publish metric
    - Assert: Callback invoked (late registration works)

76. **SetMetricsCallbackMultipleTimes**
    - Call SetMetricsCallback(ptr1) then SetMetricsCallback(ptr2)
    - Publish
    - Assert: ptr2 receives event (second overwrites first)

#### C. Schema Registration (3 tests)
77. **GetNodeMetricsSchemaReturnsValidJSON**
    - Call GetNodeMetricsSchema() on node
    - Parse as JSON
    - Assert: Valid JSON structure

78. **SchemaIncludesAllExpectedFields**
    - Get schema from SourceTestNode
    - Verify schema contains:
      - event_type: "message_produced"
      - fields: [timestamp, source, ...]
    - Assert: Complete schema

79. **SchemaConsistentAcrossNodeInstances**
    - Get schema from SourceTestNode_1 and SourceTestNode_2
    - Assert: Schemas are identical

---

## Gap #7: Completion Callback Ordering & Priority (10 tests)

### Purpose
Validate completion callback invocation order with multiple sinks, and timing relative to event delivery.

### Test Scenarios

#### A. Multi-Sink Callback Sequence (4 tests)
80. **CompletionCallbacksInvokedForAllSinks**
    - Topology with 3 sinks
    - Track: which sinks signal completion
    - Assert: All 3 sinks signal (count == 3)

81. **CompletionCallbackInvocationOrder**
    - 3 sinks: Sink_1, Sink_2, Sink_3
    - Track callback invocation sequence
    - Assert: All invoked (order may vary, all called)

82. **CompletionSignalingWhenAllSinksDone**
    - Wait for all sinks to signal
    - Then graph signals IsCompletionSignaled()
    - Assert: Graph completion == True

83. **PartialCompletionDoesNotSignalGraph**
    - 3 sinks, only 2 complete
    - Assert: IsCompletionSignaled() == False (waiting for 3rd)

#### B. Completion vs Last Message Timing (3 tests)
84. **CompletionSignaledAfterLastMessageConsumed**
    - Track: Last message consumed time
    - Track: Completion signal time
    - Assert: Completion signal >= Last message time

85. **CompletionCallbackInvokedImmediatelyAfterLastMessage**
    - Measure time from last message to completion callback
    - Assert: < 100ms latency

86. **NoMessagesAfterCompletionSignaled**
    - Signal completion
    - Attempt to publish new message
    - Assert: Message rejected or queued for earlier processing

#### C. Late Subscriber Behavior (3 tests)
87. **SubscriberRegisteredAfterCompletionMissesEvents**
    - Topology completes and signals
    - Register new subscriber
    - Assert: Subscriber gets 0 events (too late)

88. **SubscriberRegisteredBeforeCompletionSeesAllEvents**
    - Register subscriber before Run()
    - Run until completion
    - Assert: Subscriber sees all events

89. **CompletionStatusQueryable**
    - Register subscriber
    - Query: metrics_capability->IsCompletionSignaled()
    - Before completion: False
    - After completion: True
    - Assert: Status queryable at any time

---

## Gap #8: Metrics Capability State Machine (6 tests)

### Purpose
Validate MetricsCapability registration, discovery, and subscriber timing relative to event publishing.

### Test Scenarios

#### A. Capability Registration & Discovery (3 tests)
90. **MetricsCapabilityRegisteredDuringInit**
    - Before Init: GetCapability<MetricsCapability>() == nullptr
    - After Init: GetCapability<MetricsCapability>() != nullptr
    - Assert: Capability becomes available at Init

91. **CapabilityDiscoveryTypeSpecific**
    - GetCapability<MetricsCapability>() returns MetricsCapability
    - GetCapability<CSVCapability>() returns different capability
    - Assert: Type-safe discovery works

92. **CapabilityIdempotency**
    - Call GetCapability<MetricsCapability>() multiple times
    - Assert: Returns same instance each time (not cloned)

#### B. Subscriber Registration Timing (3 tests)
93. **SubscriberRegistrationBeforeStartReceivesAllEvents**
    - Register subscriber in OnInit()-to-OnStart() window
    - Run graph
    - Assert: Subscriber receives all events

94. **SubscriberRegistrationAfterStartReceivesSubsequentEvents**
    - Start graph
    - Wait for 10 events published
    - Register subscriber
    - Run for 10 more events
    - Assert: Subscriber receives only the 10 (not the initial 10)

95. **MultipleSubscriberRegistrations**
    - Register 3 subscribers at different times
    - Assert: No interference, each receives appropriate subset

---

## Test File Structure

```cpp
// libgraph/test/unit/test_metrics_policy_gaps.cpp

#include <gtest/gtest.h>
#include <memory>
#include <thread>
#include <chrono>
#include <atomic>
#include "libgraph/include/policies/MetricsPolicy.hpp"
#include "libgraph/test/include/test/AdvancedTestNodes.hpp"
#include "libgraph/test/include/test/TestGraphTopologies.hpp"

namespace MetricsPolicyGapsTests {

class MetricsPolicyGapsFixture : public ::testing::Test {
protected:
  void SetUp() override {
    graph_manager_ = std::make_shared<GraphManager>();
    metrics_policy_ = std::make_unique<MetricsPolicy>(graph_manager_);
    factory_ = PluginInfrastructure::GetFactory();
  }
  
  void TearDown() override {
    if (metrics_policy_) {
      metrics_policy_->OnStop();
      metrics_policy_->OnJoin();
    }
  }
  
  std::shared_ptr<GraphManager> graph_manager_;
  std::unique_ptr<MetricsPolicy> metrics_policy_;
  std::shared_ptr<INodeFactory> factory_;
};

// Gap #1: Event Queue Mechanics (15 tests)
TEST_F(MetricsPolicyGapsFixture, QueueInitializedWithCorrectCapacity) { ... }
// ... 14 more tests

// Gap #2: Thread Initialization Races (12 tests)
TEST_F(MetricsPolicyGapsFixture, MetricsDiscoveryCompletesBeforeThreadStart) { ... }
// ... 11 more tests

// Gap #3: Concurrent Publishing Stress (20 tests)
TEST_F(MetricsPolicyGapsFixture, PublishingThroughput1kEventsPerSecond) { ... }
// ... 19 more tests

// Gap #4: Event Content Validation (12 tests)
TEST_F(MetricsPolicyGapsFixture, EventTimestampIsSet) { ... }
// ... 11 more tests

// Gap #5: Policy Interaction (10 tests)
TEST_F(MetricsPolicyGapsFixture, MetricsFirst_CSVSecondInitialization) { ... }
// ... 9 more tests

// Gap #6: Callback Lifecycle (10 tests)
TEST_F(MetricsPolicyGapsFixture, CallbackPointerValidAfterSetMetricsCallback) { ... }
// ... 9 more tests

// Gap #7: Completion Ordering (10 tests)
TEST_F(MetricsPolicyGapsFixture, CompletionCallbacksInvokedForAllSinks) { ... }
// ... 9 more tests

// Gap #8: Capability FSM (6 tests)
TEST_F(MetricsPolicyGapsFixture, MetricsCapabilityRegisteredDuringInit) { ... }
// ... 5 more tests

} // namespace MetricsPolicyGapsTests
```

---

## Build Integration

### CMakeLists.txt Addition

```cmake
add_test_executable(test_metrics_policy_gaps
  test_metrics_policy_gaps.cpp
  LINK_LIBRARIES
    test_lib
    libgraph
    gtest
    gtest_main
)

target_compile_options(test_metrics_policy_gaps PRIVATE
  -fsanitize=thread,address
  -fno-sanitize=vptr
)
```

### Build & Run Commands

```bash
# Build
cd /Users/rklinkhammer/workspace/GraphX/build
make test_metrics_policy_gaps -j4

# Run all
./libgraph/test/test_metrics_policy_gaps

# Run by gap
./libgraph/test/test_metrics_policy_gaps --gtest_filter="*QueueMechanics*"
./libgraph/test/test_metrics_policy_gaps --gtest_filter="*ThreadInitialization*"

# With ThreadSanitizer
TSAN_OPTIONS=halt_on_error=1 ./libgraph/test/test_metrics_policy_gaps

# With AddressSanitizer  
ASAN_OPTIONS=halt_on_error=1 ./libgraph/test/test_metrics_policy_gaps
```

---

## Implementation Roadmap

### Phase 1: Foundation (1-2 days)
- Create file structure and base fixture
- Implement Gap #1 (Event Queue Mechanics) - 15 tests
- Implement Gap #4 (Event Content Validation) - 12 tests
- Build and verify basic infrastructure

### Phase 2: Concurrency Testing (2 days)
- Implement Gap #2 (Thread Initialization) - 12 tests
- Implement Gap #3 (Concurrent Publishing Stress) - 20 tests
- Add ThreadSanitizer configuration
- Verify with TSAN

### Phase 3: Advanced Scenarios (1-2 days)
- Implement Gap #5 (Policy Interaction) - 10 tests
- Implement Gap #6 (Callback Lifecycle) - 10 tests
- Implement Gap #7 (Completion Ordering) - 10 tests
- Implement Gap #8 (Capability FSM) - 6 tests

### Phase 4: Validation & Polish (1 day)
- Fix any failures from TSAN/ASAN
- Optimize slow tests
- Code review
- Commit with detailed message

**Total Estimated Effort**: 5-7 days (vs. 10+ for original plan)

---

## Success Criteria

| Criterion | Target | Validation |
|-----------|--------|-----------|
| **All Tests Pass** | 100% (95 tests) | CTest reports 95/95 ✅ |
| **Thread Safety** | 0 TSAN warnings | ThreadSanitizer clean |
| **Memory Safety** | 0 ASAN warnings | AddressSanitizer clean |
| **No Duplication** | <5% overlap with existing tests | Code review validates |
| **Execution Time** | <8 seconds | CTest timing < 8s |
| **Documentation** | 100% test function comments | Clear test purpose, gap addressed |

---

## Key Differences from Original Plan

| Aspect | Original | Revised |
|--------|----------|---------|
| **Total Tests** | 150 | 95 |
| **Focus** | Broad policy testing | Gap-specific only |
| **Duplication** | ~40% duplicate existing tests | <5% duplication |
| **Implementation Time** | 10+ days | 5-7 days |
| **Value Add** | Moderate (many tested elsewhere) | High (addresses only real gaps) |
| **Test Quality** | Good | Better (focused scope) |

---

## Risk Mitigation

| Risk | Mitigation |
|-----|-----------|
| **Queue tests too low-level** | Verify capacity issue exists first; adjust if bug not present |
| **Thread race tests hard to reproduce** | Use stress loops (1000 iterations) and TSAN |
| **Stress tests too aggressive** | Start with 1k events/sec; scale down if infrastructure can't handle |
| **Callback lifecycle subtle** | Use gdb for lifecycle tracking; manual verification if tests insufficient |
| **CSVPolicy interaction untested** | Mock CSV if real CSV not available; focus on policy composition |

---

## Next Steps

1. **Review Gap Analysis**: Validate that 8 gaps are indeed critical
2. **Approve Scope**: 95 tests vs. original 150
3. **Create File**: Begin implementation in `test_metrics_policy_gaps.cpp`
4. **Phase 1 Tests**: Queue mechanics + event content (27 tests)
5. **Build & Validate**: Ensure infrastructure works

Ready to proceed with Phase 1 implementation?
