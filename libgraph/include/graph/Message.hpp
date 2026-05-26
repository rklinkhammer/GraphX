// MIT License
//
// Copyright (c) 2025 Robert Klinkhammer
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

/**
 * @file nodes/Message.hpp
 * @brief Type-erased message container with small object optimization, constexpr-capable
 * @author Robert Klinkhammer
 * @date December 13, 2025
 */

#pragma once

#include <cstddef>
#include <type_traits>
#include <atomic>
#include <new>
#include <cstdlib>
#include <string_view>
#include "core/TypeInfo.hpp"
#include "PooledMessage.hpp"

namespace graph::message {

// Policy struct for configuring Message storage
template <size_t Size = 32, size_t Align = 16>
struct MessageStoragePolicy {
    static constexpr size_t SSO_SIZE = Size;
    static constexpr size_t SSO_ALIGN = Align;
};

// Default policy: 32-byte buffer with 16-byte alignment
using DefaultMessagePolicy = MessageStoragePolicy<32, 16>;

// Common pre-configured policies for different use cases
using CompactMessagePolicy = MessageStoragePolicy<16, 8>;   // Smaller, less aligned
using LargeMessagePolicy = MessageStoragePolicy<64, 32>;    // Larger, highly aligned
using AVXMessagePolicy = MessageStoragePolicy<32, 32>;      // For AVX operations
using SSEMessagePolicy = MessageStoragePolicy<32, 16>;      // For SSE operations

/**
 * @example Using Message with Different Policies
 * 
 * EXAMPLE 1: Standard Message (default policy)
 * ==============================================
 * @code
 *   // Uses DefaultMessagePolicy (32-byte buffer, 16-byte alignment)
 *   graph::Message msg(42);
 *   msg.get<int>();  // Retrieve the value
 *   
 *   // Check policy configuration
 *   static_assert(graph::Message::SSO_SIZE == 32);
 *   static_assert(graph::Message::SSO_ALIGN == 16);
 * @endcode
 * 
 * EXAMPLE 2: Memory-Efficient Messages
 * =====================================
 * @code
 *   // Create a storage with compact policy (16-byte buffer, 8-byte alignment)
 *   using CompactStorage = graph::MessageStorage<graph::CompactMessagePolicy>;
 *   
 *   // This storage takes less memory, good for embedded systems
 *   CompactStorage compact_msg;
 *   compact_msg.emplace<double>(3.14159);
 * @endcode
 * 
 * EXAMPLE 3: Large Payload Messages
 * ==================================
 * @code
 *   // Create a storage with large policy (64-byte buffer, 32-byte alignment)
 *   using LargeStorage = graph::MessageStorage<graph::LargeMessagePolicy>;
 *   
 *   // This storage can hold larger objects without heap allocation
 *   struct LargeData { double arr[8]; };
 *   LargeStorage large_msg;
 *   large_msg.emplace<LargeData>();
 * @endcode
 * 
 * EXAMPLE 4: SIMD-Optimized Messages
 * ===================================
 * @code
 *   // For AVX operations: 32-byte buffer, 32-byte alignment
 *   using AVXStorage = graph::MessageStorage<graph::AVXMessagePolicy>;
 *   
 *   struct AVXData {
 *       float values[8];  // Fits in 32-byte AVX register
 *   };
 *   AVXStorage avx_msg;
 *   avx_msg.emplace<AVXData>();
 * @endcode
 * 
 * EXAMPLE 5: Using Multiple Policies Simultaneously
 * ==================================================
 * @code
 *   // High-frequency critical path: use compact storage for speed
 *   using FastStorage = graph::MessageStorage<graph::CompactMessagePolicy>;
 *   
 *   // Batch processing: use large storage for efficiency
 *   using BatchStorage = graph::MessageStorage<graph::LargeMessagePolicy>;
 *   
 *   // Standard processing: use default storage
 *   using StandardStorage = graph::MessageStorage<graph::DefaultMessagePolicy>;
 *   
 *   class MessageRouter {
 *   private:
 *       FastStorage critical_fast_path_;
 *       BatchStorage batch_processor_;
 *       StandardStorage standard_processor_;
 *       graph::Message default_msg_;  // Also uses DefaultMessagePolicy
 *   
 *   public:
 *       void route_critical(int value) {
 *           critical_fast_path_.emplace<int>(value);
 *           // Minimal memory footprint (16 bytes)
 *       }
 *       
 *       void route_batch(std::array<double, 8> data) {
 *           batch_processor_.emplace<std::array<double, 8>>(data);
 *           // Plenty of space (64 bytes), highly aligned
 *       }
 *       
 *       void route_standard(std::string text) {
 *           default_msg_.emplace<std::string>(text);
 *           // Standard configuration for normal use
 *       }
 *   };
 * @endcode
 * 
 * EXAMPLE 6: Custom Policies
 * ===========================
 * @code
 *   // Define a custom policy for specific hardware requirements
 *   struct CustomHardwarePolicy : graph::MessageStoragePolicy<48, 64> {
 *       // 48-byte buffer, 64-byte alignment for specific hardware requirements
 *   };
 *   
 *   // Use the custom policy
 *   using CustomStorage = graph::MessageStorage<CustomHardwarePolicy>;
 *   
 *   CustomStorage hw_msg;
 *   hw_msg.emplace<uint64_t>(0xDEADBEEF);
 * @endcode
 * 
 * EXAMPLE 7: Policy Traits and Metaprogramming
 * =============================================
 * @code
 *   // Check and adapt to policy at compile time
 *   template<typename Policy>
 *   class MessageProcessor {
 *       static constexpr size_t buffer_size = Policy::SSO_SIZE;
 *       static constexpr size_t alignment = Policy::SSO_ALIGN;
 *       
 *       // Compile-time decisions based on policy
 *       static constexpr bool is_compact = (buffer_size <= 16);
 *       static constexpr bool is_highly_aligned = (alignment >= 32);
 *       
 *   public:
 *       void process(const graph::MessageStorage<Policy>& msg) {
 *           if constexpr (is_compact) {
 *               // Optimize for speed in tight loops
 *           }
 *           if constexpr (is_highly_aligned) {
 *               // Use SIMD operations safely
 *           }
 *       }
 *   };
 * @endcode
 */

// Forward declarations for MessageTraits
template <typename T>
struct MessageTraits;

class Message;

template <typename Policy = DefaultMessagePolicy>
class MessageStorage {
public:
    friend class Message;
    
    // Expose policy configuration
    static constexpr size_t SSO_SIZE = Policy::SSO_SIZE;
    static constexpr size_t SSO_ALIGN = Policy::SSO_ALIGN;

    // Phase 2: Operational metrics for debugging and observability
    // Note: Counts are only available at runtime; compile-time evaluation returns 0
    // Note: The runtime check using std::is_constant_evaluated() ensures that
    //       metric methods work correctly when called from constexpr contexts
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconstant-evaluated"
    static constexpr size_t message_creation_count() noexcept {
        if (!std::is_constant_evaluated()) {
            return s_creation_count.load(std::memory_order_relaxed);
        }
        return 0;
    }

    static constexpr size_t message_destruction_count() noexcept {
        if (!std::is_constant_evaluated()) {
            return s_destruction_count.load(std::memory_order_relaxed);
        }
        return 0;
    }

    static constexpr size_t message_copy_count() noexcept {
        if (!std::is_constant_evaluated()) {
            return s_copy_count.load(std::memory_order_relaxed);
        }
        return 0;
    }

    static constexpr size_t message_move_count() noexcept {
        if (!std::is_constant_evaluated()) {
            return s_move_count.load(std::memory_order_relaxed);
        }
        return 0;
    }
#pragma GCC diagnostic pop

    /// @brief Create empty message storage
    /// @note This constructor supports constexpr context.
    ///       At compile-time, ensure types fit SSO buffer.
    constexpr MessageStorage() noexcept : sso_(), heap_ptr_(nullptr), ops_(nullptr), active_(false) {}

    /// @brief Destructor with automatic cleanup
    /// @note This destructor supports constexpr context.
    ///       At compile-time, only trivial types may be stored.
    constexpr ~MessageStorage() { clear(); }

    inline static std::atomic<size_t> s_alloc_count{0};
    inline static std::atomic<size_t> s_alloc_bytes{0};
    inline static std::atomic<size_t> s_creation_count{0};
    inline static std::atomic<size_t> s_destruction_count{0};
    inline static std::atomic<size_t> s_copy_count{0};
    inline static std::atomic<size_t> s_move_count{0};

    template<typename T>
    constexpr void emplace(const T& value) {
        // Type constraint: Message only accepts types that can be moved without throwing.
        // This ensures exception-safe move operations and simplifies error handling.
        static_assert(std::is_nothrow_move_constructible_v<T>,
                      "Message requires nothrow-move-constructible types. "
                      "Add 'noexcept' to your type's move constructor.");

        clear();
        active_ = true;

        // Small Object Optimization (SSO): Types that fit criteria are stored inline
        // to eliminate heap allocation. This significantly improves cache locality.
        // Criteria: sizeof(T) ≤ 32 bytes, alignof(T) ≤ 16 bytes, trivially destructible
        if constexpr (sizeof(T) <= SSO_SIZE && alignof(T) <= SSO_ALIGN &&
                      std::is_trivially_destructible_v<T>) {
            // SSO path: Store directly in buffer (no allocation)
            // cppcheck-suppress objectIndex
            new (&sso_) T(value);
            heap_ptr_ = nullptr;
            ops_ = &OpsTable<T>::ops;
        } else {
            // Heap path: Allocate on heap for large or non-trivial types
            // First try to allocate from pool if size warrants it
            if constexpr (sizeof(T) > 32) {  // Only pool larger messages
                auto* pool = graph::MessagePoolRegistry::GetInstance().GetPoolForSize(sizeof(T));
                if (pool) {
                    void* pooled_buffer = pool->AcquireBuffer();
                    if (pooled_buffer) {
                        heap_ptr_ = new (pooled_buffer) T(value);
                        // Mark as pooled by storing special marker
                        // (in future version, could store pool pointer for proper return)
                    } else {
                        // Fallback to direct malloc + placement new if pool fails
                        void* buffer = std::malloc(sizeof(T));
                        if (!buffer) throw std::bad_alloc();
                        heap_ptr_ = new (buffer) T(value);
                    }
                } else {
                    // No pool yet (initialization not complete), use malloc + placement new
                    void* buffer = std::malloc(sizeof(T));
                    if (!buffer) throw std::bad_alloc();
                    heap_ptr_ = new (buffer) T(value);
                }
            } else {
                // Small heap messages use malloc + placement new for consistency
                void* buffer = std::malloc(sizeof(T));
                if (!buffer) throw std::bad_alloc();
                heap_ptr_ = new (buffer) T(value);
            }
            ops_ = &OpsTable<T>::ops;
            if (!std::is_constant_evaluated()) {
                s_alloc_count.fetch_add(1, std::memory_order_relaxed);
                s_alloc_bytes.fetch_add(sizeof(T), std::memory_order_relaxed);
            }
        }
    }

    constexpr const void* data() const noexcept {
        return active_ ? (heap_ptr_ ? heap_ptr_ : static_cast<const void*>(&sso_)) : nullptr;
    }

private:
    struct Ops {
        void (*destroy)(MessageStorage&) noexcept;
        void (*copy)(MessageStorage&, const MessageStorage&) noexcept;
        void (*move)(MessageStorage&, MessageStorage&) noexcept;
    };

    template<typename T>
    struct OpsTable {
        static constexpr void destroy(MessageStorage& s) noexcept {
            if (s.heap_ptr_) {
                T* ptr = static_cast<T*>(s.heap_ptr_);
                ptr->~T();  // Call destructor explicitly

                // Try to return to pool for large types
                if constexpr (sizeof(T) > 32) {
                    if (!std::is_constant_evaluated()) {
                        auto* pool = graph::MessagePoolRegistry::GetInstance().GetPoolForSize(sizeof(T));
                        if (pool) {
                            pool->ReleaseBuffer(s.heap_ptr_);
                            s.heap_ptr_ = nullptr;
                            s_alloc_count.fetch_sub(1, std::memory_order_relaxed);
                            s_alloc_bytes.fetch_sub(sizeof(T), std::memory_order_relaxed);
                            return;
                        }
                    }
                }

                // Fallback: use std::free for all non-pooled allocations (malloc + placement new)
                std::free(ptr);
                if (!std::is_constant_evaluated()) {
                    s_alloc_count.fetch_sub(1, std::memory_order_relaxed);
                    s_alloc_bytes.fetch_sub(sizeof(T), std::memory_order_relaxed);
                }
            } else {
                // cppcheck-suppress unionAccess
                // cppcheck-suppress objectIndex
                reinterpret_cast<T*>(&s.sso_)->~T();
           }
        }

        static constexpr void copy(MessageStorage& dst, const MessageStorage& src) noexcept {
            dst.active_ = true;
            dst.ops_ = src.ops_;
            if (src.heap_ptr_) {
                const T* src_ptr = static_cast<const T*>(src.heap_ptr_);

                // Try pooled allocation for large types
                if constexpr (sizeof(T) > 32) {
                    if (!std::is_constant_evaluated()) {
                        auto* pool = graph::MessagePoolRegistry::GetInstance().GetPoolForSize(sizeof(T));
                        if (pool) {
                            void* pooled_buffer = pool->AcquireBuffer();
                            if (pooled_buffer) {
                                dst.heap_ptr_ = new (pooled_buffer) T(*src_ptr);
                                s_alloc_count.fetch_add(1, std::memory_order_relaxed);
                                s_alloc_bytes.fetch_add(sizeof(T), std::memory_order_relaxed);
                                return;
                            }
                        }
                    }
                }

                // Fallback to direct allocation
                dst.heap_ptr_ = new T(*src_ptr);
                if (!std::is_constant_evaluated()) {
                    s_alloc_count.fetch_add(1, std::memory_order_relaxed);
                    s_alloc_bytes.fetch_add(sizeof(T), std::memory_order_relaxed);
                }
            } else {
                // cppcheck-suppress unionAccess
                // cppcheck-suppress objectIndex
                new (&dst.sso_) T(*reinterpret_cast<const T*>(&src.sso_));
                dst.heap_ptr_ = nullptr;
            }
        }

        static constexpr void move(MessageStorage& dst, MessageStorage& src) noexcept {
            dst.active_ = true;
            dst.ops_ = src.ops_;
            if (src.heap_ptr_) {
                dst.heap_ptr_ = src.heap_ptr_;
                src.heap_ptr_ = nullptr;
            } else {
                // cppcheck-suppress unionAccess
                // cppcheck-suppress objectIndex
                new (&dst.sso_) T(std::move(*reinterpret_cast<T*>(&src.sso_)));
                // cppcheck-suppress unionAccess
                // cppcheck-suppress objectIndex
                std::launder(reinterpret_cast<T*>(&src.sso_))->~T();
                dst.heap_ptr_ = nullptr;
            }
            src.ops_ = nullptr;
            src.active_ = false;
        }

        // cppcheck-suppress unusedStructMember
        static constexpr Ops ops{
            &destroy,
            &copy,
            &move
        };
    };

    // cppcheck-suppress uninitMemberVar
    union {
        // cppcheck-suppress uninitMemberVar
        alignas(Policy::SSO_ALIGN) unsigned char sso_[Policy::SSO_SIZE];
    };

    void*       heap_ptr_;
    const Ops*  ops_;
    bool        active_;

    constexpr void clear() noexcept {
        if (!active_ || !ops_) return;
        ops_->destroy(*this);
        heap_ptr_ = nullptr;
        ops_ = nullptr;
        active_ = false;
    }
};

class Message {
public:
    // Policy configuration
    using StoragePolicy = DefaultMessagePolicy;
    static constexpr size_t SSO_SIZE = StoragePolicy::SSO_SIZE;
    static constexpr size_t SSO_ALIGN = StoragePolicy::SSO_ALIGN;

    // cppcheck-suppress unusedFunction
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconstant-evaluated"
    static constexpr size_t heap_allocation_count() { // cppcheck-suppress unusedFunction
        if (!std::is_constant_evaluated()) {
            return MessageStorage<DefaultMessagePolicy>::s_alloc_count.load(std::memory_order_relaxed);
        }
        return 0;
    }

    // cppcheck-suppress unusedFunction
    static constexpr size_t heap_allocation_bytes() { // cppcheck-suppress unusedFunction
        if (!std::is_constant_evaluated()) {
            return MessageStorage<DefaultMessagePolicy>::s_alloc_bytes.load(std::memory_order_relaxed);
        }
        return 0;
    }
#pragma GCC diagnostic pop

    constexpr Message() noexcept = default;
    
    constexpr ~Message() {
        if (!std::is_constant_evaluated()) {
            MessageStorage<DefaultMessagePolicy>::s_destruction_count.fetch_add(1, std::memory_order_relaxed);
        }
    }

    constexpr Message(const Message& o) : type_(o.type_) {
        if (o.storage_.ops_) o.storage_.ops_->copy(storage_, o.storage_);
        if (!std::is_constant_evaluated()) {
            MessageStorage<DefaultMessagePolicy>::s_copy_count.fetch_add(1, std::memory_order_relaxed);
        }
    }

    constexpr Message(Message&& o) noexcept : storage_(), type_(o.type_) {
        if (o.storage_.ops_) o.storage_.ops_->move(storage_, o.storage_);
        o.type_ = {};
        if (!std::is_constant_evaluated()) {
            MessageStorage<DefaultMessagePolicy>::s_move_count.fetch_add(1, std::memory_order_relaxed);
        }
    }

    /// @brief Copy assignment operator (strong exception safety)
    /// @param o Source message to copy
    /// @return Reference to this message
    /// @throws std::bad_alloc if copy() requires allocation and fails
    /// @note If allocation fails during copy(), this object is cleared.
    ///       For strict exception-safety guarantee, use: *this = Message(o)
    constexpr Message& operator=(const Message& o) {
        if (this != &o) {
            storage_.clear();
            type_ = o.type_;
            if (o.storage_.ops_) o.storage_.ops_->copy(storage_, o.storage_);
            if (!std::is_constant_evaluated()) {
                MessageStorage<DefaultMessagePolicy>::s_copy_count.fetch_add(1, std::memory_order_relaxed);
            }
        }
        return *this;
    }

    constexpr Message& operator=(Message&& o) noexcept {
        if (this != &o) {
            storage_.clear();
            type_ = o.type_;
            if (o.storage_.ops_) o.storage_.ops_->move(storage_, o.storage_);
            o.type_ = {};
            if (!std::is_constant_evaluated()) {
                MessageStorage<DefaultMessagePolicy>::s_move_count.fetch_add(1, std::memory_order_relaxed);
            }
        }
        return *this;
    }

    template<typename T>
    // cppcheck-suppress noExplicitConstructor
    constexpr Message(const T& value) : type_(TypeInfo::create<T>()) {
        storage_.emplace<T>(value);
        if (!std::is_constant_evaluated()) {
            MessageStorage<DefaultMessagePolicy>::s_creation_count.fetch_add(1, std::memory_order_relaxed);
        }
    }

    // cppcheck-suppress unusedFunction
    constexpr const TypeInfo& type() const noexcept { return type_; } // cppcheck-suppress unusedFunction
    constexpr bool valid() const noexcept { return type_.is_valid(); }

    template<typename T>
    constexpr const T& get() const {
        if (!valid() || !storage_.data() || type_.hash != TypeInfo::create<T>().hash) {
            throw std::bad_cast();
        }
        return *reinterpret_cast<const T*>(storage_.data());
    }

    template<typename Base>
    // cppcheck-suppress unusedFunction
    constexpr const Base* try_get() const { // cppcheck-suppress unusedFunction
        if (!valid()) return nullptr;
        try {
            const Base& exact = get<Base>();
            return &exact;
        } catch (const std::bad_cast&) {}
        return try_cast_to_base_ptr<Base>();
    }

private:
    template<typename Base>
    constexpr const Base* try_cast_to_base_ptr() const { return nullptr; }

private:
    MessageStorage<DefaultMessagePolicy> storage_;
    TypeInfo       type_;
};

} // namespace graph::message

