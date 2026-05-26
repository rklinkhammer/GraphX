// MIT License
//
// Copyright (c) 2025 graphlib contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.


/*
 The MIT License (MIT)
 
 Copyright (c) 2019 rklinkhammer
 
 Permission is hereby granted, free of charge, to any person obtaining a copy
 of this software and associated documentation files (the "Software"), to deal
 in the Software without restriction, including without limitation the rights
 to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 copies of the Software, and to permit persons to whom the Software is
 furnished to do so, subject to the following conditions:
 
 The above copyright notice and this permission notice shall be included in all
 copies or substantial portions of the Software.
 
 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 SOFTWARE.
 */

#pragma once

#include <deque>
#include <queue>
#include <mutex>
#include <thread>
#include <atomic>
#include <condition_variable>
#include <functional>
#include <chrono>
#include <cstdint>

namespace core {

/**
 * @brief Metrics for queue operations
 * 
 * Captures performance and usage statistics for an ActiveQueue.
 * All counters and timings are atomic for thread-safe reads.
 * 
 * Related to DECISION-013: Metrics collection at queue level
 */
struct QueueMetrics {
    // Counters (atomic for thread-safe access)
    std::atomic<uint64_t> enqueued_count{0};        /*!< Total items successfully enqueued */
    std::atomic<uint64_t> dequeued_count{0};        /*!< Total items successfully dequeued */
    std::atomic<uint64_t> enqueue_rejections{0};    /*!< Items rejected during enqueue */
    std::atomic<uint64_t> dequeue_empty{0};         /*!< Dequeue attempts on empty queue */
    
    // Timing (nanoseconds)
    std::atomic<uint64_t> total_enqueue_time_ns{0}; /*!< Cumulative enqueue operation time */
    std::atomic<uint64_t> total_dequeue_time_ns{0}; /*!< Cumulative dequeue operation time */
    std::atomic<uint64_t> total_wait_time_ns{0};    /*!< Cumulative wait time on condition variable */
    
    // Capacity tracking
    std::atomic<uint64_t> max_size_observed{0};     /*!< Maximum queue depth observed */
    std::atomic<uint64_t> current_size{0};          /*!< Current queue size snapshot */
    
    // Helpers for computing averages
    double GetAverageEnqueueTimeUs() const {
        uint64_t total_ns = total_enqueue_time_ns.load(std::memory_order_relaxed);
        uint64_t count = enqueued_count.load(std::memory_order_relaxed);
        if (count == 0) return 0.0;
        return static_cast<double>(total_ns) / (1000.0 * count);
    }
    
    double GetAverageDequeueTimeUs() const {
        uint64_t total_ns = total_dequeue_time_ns.load(std::memory_order_relaxed);
        uint64_t count = dequeued_count.load(std::memory_order_relaxed);
        if (count == 0) return 0.0;
        return static_cast<double>(total_ns) / (1000.0 * count);
    }
    
    double GetAverageWaitTimeUs() const {
        uint64_t total_ns = total_wait_time_ns.load(std::memory_order_relaxed);
        uint64_t count = dequeued_count.load(std::memory_order_relaxed);
        if (count == 0) return 0.0;
        return static_cast<double>(total_ns) / (1000.0 * count);
    }
};

/**
 * @brief Thread-safe deque with blocking, signalling, and cleanup.
 *
 * ActiveQueue is a template class that manages enqueuing and dequeuing of elements,
 * blocking on the queue when empty. It uses condition variables to manage element
 * availability and an exit flag. Now backed by std::deque for efficient operations
 * at both ends and random access.
 *
 * Threads use the Enqueue() and Dequeue() commands under normal circumstances.
 * When the Queue is to be shutdown, it must first be disabled.  This can be
 * done by calling the DisableQueue() method.  This will set the exit flag and
 * wakeup any blocked threads.  The setting of the exit flag will also prevent
 * any thread from retrieving or blocking on the queue subsequently.  In order
 * to empty the queue itself, there is a method called  DequeueNonBlocking().
 * The queue can be re-enabled using the EnableQueue() method.
 *
 * The following is example code of how to handle disabling a queue and
 * emptying the elements.
 *
 * @code
 *        the_queue.DisableQueue();
 *        Request request;
 *        while(the_queue.DequeueNonBlocking(request)) {
 *           Free(request);
 *        }
 * @endcode
 */
template<class Element> class ActiveQueue {
    
public:
    ActiveQueue(const std::size_t capacity = 0, bool block_on_full = false) noexcept
        : capacity_(capacity), block_on_full_(block_on_full), metrics_enabled_(false) {
    }
    
    ~ActiveQueue() {
        Disable();
        Clear();
    }
    
    // Explicit deletion of copy operations (C++26: non-copyable by design)
    ActiveQueue(const ActiveQueue&) = delete;
    ActiveQueue& operator=(const ActiveQueue&) = delete;
    
    // Move operations are default-deleted due to mutex; provide explicit deletions
    ActiveQueue(ActiveQueue&&) = delete;
    ActiveQueue& operator=(ActiveQueue&&) = delete;
    
    /**
     * @brief Enable metrics collection for this queue
     * 
     * Once enabled, all enqueue/dequeue operations will track timing and statistics.
     * Can be toggled at runtime without side effects.
     */
    void EnableMetrics(bool enabled = true) noexcept {
        metrics_enabled_.store(enabled, std::memory_order_release);
    }
    
    /**
     * @brief Get reference to metrics for reading
     * 
     * Metrics are updated continuously if EnableMetrics(true) has been called.
     * Safe to read at any time from any thread.
     */
    [[nodiscard]] const core::QueueMetrics& GetMetrics() const {
        return metrics_;
    }
    
    /**
     * @brief Reset all metrics counters
     * 
     * Clears all counters and timing statistics. Safe to call during operation.
     */
    void ResetMetrics() noexcept {
        metrics_.enqueued_count.store(0, std::memory_order_release);
        metrics_.dequeued_count.store(0, std::memory_order_release);
        metrics_.enqueue_rejections.store(0, std::memory_order_release);
        metrics_.dequeue_empty.store(0, std::memory_order_release);
        metrics_.total_enqueue_time_ns.store(0, std::memory_order_release);
        metrics_.total_dequeue_time_ns.store(0, std::memory_order_release);
        metrics_.total_wait_time_ns.store(0, std::memory_order_release);
        metrics_.max_size_observed.store(0, std::memory_order_release);
        metrics_.current_size.store(0, std::memory_order_release);
    }
    
    /**
     * @brief Enqueue an element
     *
     * The caller will insert the element into the queue and notify
     * any waiters who may be blocked in Dequeue.
     *
     * If a comparator has been set via SetComparator(), the element will be
     * inserted in sorted order. Otherwise, it appends to the back (FIFO).
     *
     * @param element - reference to item to enqueue.
     *
     * @return The function will return <em>true</em> if element has been
     *         successfully enqueued; otherwise, the function will return
     *         <em>false</em> if disabled.
     */
    [[nodiscard]] bool Enqueue(Element element) {
        auto start_time = metrics_enabled_.load(std::memory_order_acquire) 
            ? std::chrono::high_resolution_clock::now() 
            : std::chrono::high_resolution_clock::time_point{};
        
        std::unique_lock<std::mutex> lock(mutex_);
        
        if (exit_) {
            if (metrics_enabled_.load(std::memory_order_acquire)) {
                metrics_.enqueue_rejections.fetch_add(1, std::memory_order_acq_rel);
            }
            return false;
        }
        
        // bounded queue: block or drop
        if (capacity_ > 0 && deque_.size() >= capacity_) {
            if (block_on_full_) {
                // Block until space is available or queue is disabled
                auto wait_start = metrics_enabled_.load(std::memory_order_acquire) 
                    ? std::chrono::high_resolution_clock::now() 
                    : std::chrono::high_resolution_clock::time_point{};
                
                not_full_condition_.wait(lock, [&] {
                    return exit_ || deque_.size() < capacity_;
                });
                
                if (metrics_enabled_.load(std::memory_order_acquire) && wait_start != std::chrono::high_resolution_clock::time_point{}) {
                    auto wait_end = std::chrono::high_resolution_clock::now();
                    auto wait_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(wait_end - wait_start).count();
                    metrics_.total_wait_time_ns.fetch_add(wait_ns, std::memory_order_acq_rel);
                }
                
                if (exit_) {
                    if (metrics_enabled_.load(std::memory_order_acquire)) {
                        metrics_.enqueue_rejections.fetch_add(1, std::memory_order_acq_rel);
                    }
                    return false;
                }
            } else {
                // Drop gracefully when not blocking
                if (metrics_enabled_.load(std::memory_order_acquire)) {
                    metrics_.enqueue_rejections.fetch_add(1, std::memory_order_acq_rel);
                }
                return false;
            }
        }
        
        // If a comparator is set, insert in sorted order
        if (comparator_) {
            // Find insertion point using the comparator
            auto it = deque_.begin();
            while (it != deque_.end() && !comparator_(element, *it)) {
                ++it;
            }
            deque_.insert(it, std::move(element));
        } else {
            // Default FIFO: append to back
            deque_.push_back(std::move(element));
        }
        
        // Update metrics
        if (metrics_enabled_.load(std::memory_order_acquire)) {
            metrics_.enqueued_count.fetch_add(1, std::memory_order_acq_rel);
            metrics_.current_size.store(deque_.size(), std::memory_order_release);
            
            // Track max size observed
            auto current = deque_.size();
            auto max = metrics_.max_size_observed.load(std::memory_order_acquire);
            while (current > max && !metrics_.max_size_observed.compare_exchange_weak(max, current, std::memory_order_release)) {
                // Retry if CAS failed
            }
            
            // Update enqueue time
            if (start_time != std::chrono::high_resolution_clock::time_point{}) {
                auto end_time = std::chrono::high_resolution_clock::now();
                auto duration_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end_time - start_time).count();
                metrics_.total_enqueue_time_ns.fetch_add(duration_ns, std::memory_order_acq_rel);
            }
        }
        
        condition_.notify_one();
        return true;
    }
    
    /**
     * @brief Set a comparator for sorted insertion
     *
     * If set, Enqueue() will insert elements in sorted order using this comparator.
     * The comparator should return true if the first argument should come before
     * the second argument in the sorted order.
     *
     * @param comparator Function for comparison, or nullptr to disable sorted insertion
     */
    void SetComparator(std::function<bool(const Element&, const Element&)> comparator) {
        std::unique_lock<std::mutex> lock(mutex_);
        comparator_ = comparator;
    }
    
    /**
     * @brief Nonblocking Dequeue from queue
     *
     * This method is intended to be used to empty the queue.  Especially
     * during cleanup.
     *
     * @param element - reference to store dequeued item.
     *
     * @return The function will return <em>true</em> if element has been
     *         successfully dequeued; otherwise, the function will return
     *         <em>false</em> if there is nothing left to dequeue.
     */
    [[nodiscard]] bool DequeueNonBlocking(Element& element) {
        std::unique_lock<std::mutex> lock(mutex_);
        if (deque_.empty()) {
            if (metrics_enabled_.load(std::memory_order_acquire)) {
                metrics_.dequeue_empty.fetch_add(1, std::memory_order_acq_rel);
            }
            return false;
        }

        element = std::move(deque_.front());
        deque_.pop_front();

        // Update metrics
        if (metrics_enabled_.load(std::memory_order_acquire)) {
            metrics_.dequeued_count.fetch_add(1, std::memory_order_acq_rel);
            metrics_.current_size.store(deque_.size(), std::memory_order_release);
        }

        // Notify blocked enqueuers that space is available
        if (block_on_full_ && capacity_ > 0) {
            not_full_condition_.notify_one();
        }

        return true;
    }
    
    /**
     * @brief Block until element available.
     *
     * The caller will block until an element has been enqueued or
     * until the queue has been signaled via DisableQueue().  If a queue
     * has been signaled, this takes priority over any queued elements.
     *
     * @param element - reference to store dequeued item.
     *
     * @return The function will return <em>true</em> if element has been
     *         successfully dequeued; otherwise, the function will return
     *         <em>false</em> if disabled.
     
     */
    [[nodiscard]] bool Dequeue(Element& element) {
        auto start_time = metrics_enabled_.load(std::memory_order_acquire) 
            ? std::chrono::high_resolution_clock::now() 
            : std::chrono::high_resolution_clock::time_point{};
        
        std::unique_lock<std::mutex> lock(mutex_);
        
        auto wait_start = metrics_enabled_.load(std::memory_order_acquire)
            ? std::chrono::high_resolution_clock::now()
            : std::chrono::high_resolution_clock::time_point{};
        
        condition_.wait(lock, [&] {
            return exit_ || !deque_.empty();
        });
        
        if (metrics_enabled_.load(std::memory_order_acquire) && wait_start != std::chrono::high_resolution_clock::time_point{}) {
            auto wait_end = std::chrono::high_resolution_clock::now();
            auto wait_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(wait_end - wait_start).count();
            metrics_.total_wait_time_ns.fetch_add(wait_ns, std::memory_order_acq_rel);
        }

        if (exit_) return false;

        element = std::move(deque_.front());
        deque_.pop_front();
        
        // Update metrics
        if (metrics_enabled_.load(std::memory_order_acquire)) {
            metrics_.dequeued_count.fetch_add(1, std::memory_order_acq_rel);
            metrics_.current_size.store(deque_.size(), std::memory_order_release);
            
            // Update dequeue time
            if (start_time != std::chrono::high_resolution_clock::time_point{}) {
                auto end_time = std::chrono::high_resolution_clock::now();
                auto duration_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end_time - start_time).count();
                metrics_.total_dequeue_time_ns.fetch_add(duration_ns, std::memory_order_acq_rel);
            }
        }
        
        // Notify blocked enqueuers that space is available
        if (block_on_full_ && capacity_ > 0) {
            not_full_condition_.notify_one();
        }
        
        return true;
    }
    
    template<class... Args>
    [[nodiscard]] bool Emplace(Args&&... args) {
        std::unique_lock<std::mutex> lock(mutex_);
    
        if (exit_) return false;
    
        // bounded queue: block or drop
        if (capacity_ > 0 && deque_.size() >= capacity_) {
            if (block_on_full_) {
                // Block until space is available or queue is disabled
                not_full_condition_.wait(lock, [&] {
                    return exit_ || deque_.size() < capacity_;
                });
                
                if (exit_) return false;
            } else {
                // Drop gracefully when not blocking
                return false;
            }
        }
    
        deque_.emplace_back(std::forward<Args>(args)...);
        condition_.notify_one();
        return true;
    }
    
    /**
     * @brief Disable ability to enqueue and dequeue elements.
     *
     * This method will set the exit flag and notify a single
     * waiter.  This will not effect any pending queue elements.
     */
    void Disable() noexcept {
        {
            std::unique_lock<std::mutex> lock(mutex_);
            exit_ = true;
        }
        condition_.notify_all();
        not_full_condition_.notify_all();
    }
    
    /**
     * @brief Restore  ability to enqueue and dequeue elements.
     *
     * This method will clear the exit flag and allow waiters
     * to retrieve elements.
     */
    void Enable() noexcept {
        // set condition variable
        std::unique_lock<std::mutex> lock(mutex_);
        exit_ = false;
    }
    
    // -------------------------------------------------------------
    // Utility
    // -------------------------------------------------------------
    [[nodiscard]] std::size_t Size() const {
        std::unique_lock<std::mutex> lock(mutex_);
        return deque_.size();
    }
    
    [[nodiscard]] std::size_t Capacity() const {
        return capacity_;
    }
    
    /**
     * @brief Set the capacity (max capacity) of the queue
     * 
     * Sets the maximum number of elements the queue can hold.
     * A value of 0 means unbounded capacity.
     * Note: This does not affect existing elements in the queue.
     * 
     * @param new_capacity New capacity limit (0 for unbounded)
     */
    void SetCapacity(std::size_t new_capacity) noexcept {
        std::unique_lock<std::mutex> lock(mutex_);
        capacity_ = new_capacity;
    }
    
    /**
     * @brief Set the block-on-full behavior at runtime
     * 
     * Changes whether the queue blocks or drops elements when full.
     * This method will fail if there is any data currently in the queue
     * to prevent inconsistent behavior for existing elements.
     * 
     * @param block_on_full New block-on-full setting
     * @return true if the setting was changed, false if queue contains data
     */
    [[nodiscard]] bool SetBlockOnFull(bool block_on_full) {
        std::unique_lock<std::mutex> lock(mutex_);
        
        // Cannot change blocking behavior if there's data in the queue
        if (!deque_.empty()) {
            return false;
        }
        
        block_on_full_ = block_on_full;
        return true;
    }
    
    /**
     * @brief Get the current block-on-full setting
     * 
     * @return true if queue blocks when full, false if it drops elements
     */
    [[nodiscard]] bool GetBlockOnFull() const {
        std::unique_lock<std::mutex> lock(mutex_);
        return block_on_full_;
    }
    
    [[nodiscard]] bool Empty() const {
        std::unique_lock<std::mutex> lock(mutex_);
        return deque_.empty();
    }
    
    [[nodiscard]] bool Enabled() const {
        std::unique_lock<std::mutex> lock(mutex_);
        return !exit_;
    }
    
    // Remove all items without blocking
    void Clear() noexcept {
        std::unique_lock<std::mutex> lock(mutex_);
        deque_.clear();
    }
    
    // -------------------------------------------------------------
    // Deque-style operations
    // -------------------------------------------------------------
    
    /**
     * @brief Add element to the front of the queue
     */
    [[nodiscard]] bool PushFront(Element element) {
        std::unique_lock<std::mutex> lock(mutex_);
        
        if (exit_) return false;
        
        // bounded queue: block or drop
        if (capacity_ > 0 && deque_.size() >= capacity_) {
            if (block_on_full_) {
                // Block until space is available or queue is disabled
                not_full_condition_.wait(lock, [&] {
                    return exit_ || deque_.size() < capacity_;
                });
                
                if (exit_) return false;
            } else {
                // Drop gracefully when not blocking
                return false;
            }
        }
        
        deque_.push_front(std::move(element));
        condition_.notify_one();
        return true;
    }
    
    /**
     * @brief Add element to the back of the queue (alias for Enqueue)
     */
    [[nodiscard]] bool PushBack(Element element) {
        return Enqueue(std::move(element));
    }
    
    /**
     * @brief Remove element from the back of the queue (non-blocking)
     */
    [[nodiscard]] bool PopBack(Element& element) {
        std::unique_lock<std::mutex> lock(mutex_);
        if (deque_.empty()) return false;
        
        element = std::move(deque_.back());
        deque_.pop_back();
        
        // Notify blocked enqueuers that space is available
        if (block_on_full_ && capacity_ > 0) {
            not_full_condition_.notify_one();
        }
        
        return true;
    }
    
    /**
     * @brief Remove element from the front (alias for DequeueNonBlocking)
     */
    [[nodiscard]] bool PopFront(Element& element) {
        return DequeueNonBlocking(element);
    }
    
    /**
     * @brief Access front element without removing (non-blocking)
     */
    [[nodiscard]] bool Front(Element& element) const {
        std::unique_lock<std::mutex> lock(mutex_);
        if (deque_.empty()) return false;
        
        element = deque_.front();
        return true;
    }
    
    /**
     * @brief Access back element without removing (non-blocking)
     */
    [[nodiscard]] bool Back(Element& element) const {
        std::unique_lock<std::mutex> lock(mutex_);
        if (deque_.empty()) return false;
        
        element = deque_.back();
        return true;
    }
    
    /**
     * @brief Access element at index (non-blocking, bounds-checked)
     */
    [[nodiscard]] bool At(std::size_t index, Element& element) const {
        std::unique_lock<std::mutex> lock(mutex_);
        if (index >= deque_.size()) return false;
        
        element = deque_.at(index);
        return true;
    }
    
private:
    // C++26: Thread-synchronization primitives (mutex, condition_variable) are non-movable,
    // making the entire queue non-movable by design for thread-safety guarantees.
    // Explicit deletion clarifies intent.
    
    std::condition_variable condition_; /*!< condition variable for dequeue */
    std::condition_variable not_full_condition_; /*!< condition variable for blocking enqueue */
    mutable std::mutex mutex_; /*!< mutex variable */
    std::size_t capacity_ {0}; /*!< max queue capacity */
    std::deque<Element> deque_; /*!< deque container */
    bool exit_ {false}; /*!< exit flag */
    bool block_on_full_ {false}; /*!< block on full flag */
    std::function<bool(const Element&, const Element&)> comparator_; /*!< optional comparator for sorted insertion */
    std::atomic<bool> metrics_enabled_{false}; /*!< enables/disables metrics collection */
    mutable core::QueueMetrics metrics_; /*!< queue performance metrics */
};

} // namespace core

