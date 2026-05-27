#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <new>

namespace arctic {

// bump allocator to keep things off the heap
// we don't want malloc hanging us during a race
// reset this thing after every path to zero out
class ArenaAllocator {
public:
    explicit ArenaAllocator(size_t capacity_bytes)
        : capacity_(capacity_bytes), offset_(0) {
        // Allocate the backing store with page-aligned allocation for TLB efficiency
        base_ = static_cast<char*>(::operator new(capacity_bytes, std::align_val_t{4096}));
        // Touch all pages to avoid page faults in the hot path (pre-fault)
        std::memset(base_, 0, capacity_bytes);
    }

    ~ArenaAllocator() {
        ::operator delete(base_, std::align_val_t{4096});
    }

    // Non-copyable, non-movable (owns raw memory)
    ArenaAllocator(const ArenaAllocator&) = delete;
    ArenaAllocator& operator=(const ArenaAllocator&) = delete;

    // grab aligned memory chunk. will blow up if full.
    template<typename T>
    T* allocate(size_t count) {
        size_t alignment = alignof(T);
        size_t aligned_offset = (offset_ + alignment - 1) & ~(alignment - 1);
        size_t total_bytes = count * sizeof(T);

        if (aligned_offset + total_bytes > capacity_) {
            throw std::bad_alloc();
        }

        T* ptr = reinterpret_cast<T*>(base_ + aligned_offset);
        offset_ = aligned_offset + total_bytes;
        return ptr;
    }

    // reset pointer. don't use this for stuff with destructors.
    void reset() noexcept {
        offset_ = 0;
    }

    size_t used() const noexcept { return offset_; }
    size_t capacity() const noexcept { return capacity_; }
    size_t remaining() const noexcept { return capacity_ - offset_; }

private:
    char* base_;
    size_t capacity_;
    size_t offset_;
};

// object pool for LOB orders. built a hacky intrusive free-list
// so we don't pay the piper on new/delete when order flow is crazy
template<typename T, size_t Capacity>
class PoolAllocator {
    static_assert(sizeof(T) >= sizeof(int32_t),
                  "T must be at least 4 bytes for intrusive free-list");
public:
    PoolAllocator() : free_head_(0), allocated_(0) {
        // Build intrusive free-list through the storage
        for (size_t i = 0; i < Capacity - 1; ++i) {
            *reinterpret_cast<int32_t*>(&storage_[i * sizeof(T)]) = static_cast<int32_t>(i + 1);
        }
        *reinterpret_cast<int32_t*>(&storage_[(Capacity - 1) * sizeof(T)]) = -1; // End sentinel
    }

    // pop slot from list. check for -1!
    int32_t allocate_index() {
        if (free_head_ == -1) return -1;
        int32_t idx = free_head_;
        free_head_ = *reinterpret_cast<int32_t*>(&storage_[idx * sizeof(T)]);
        allocated_++;
        return idx;
    }

    // push slot back
    void deallocate_index(int32_t idx) {
        *reinterpret_cast<int32_t*>(&storage_[idx * sizeof(T)]) = free_head_;
        free_head_ = idx;
        allocated_--;
    }

    T* get(int32_t idx) {
        return reinterpret_cast<T*>(&storage_[idx * sizeof(T)]);
    }

    const T* get(int32_t idx) const {
        return reinterpret_cast<const T*>(&storage_[idx * sizeof(T)]);
    }

    size_t allocated() const noexcept { return allocated_; }
    size_t capacity() const noexcept { return Capacity; }

    // nuke everything and rebuild list
    void reset() {
        free_head_ = 0;
        allocated_ = 0;
        for (size_t i = 0; i < Capacity - 1; ++i) {
            *reinterpret_cast<int32_t*>(&storage_[i * sizeof(T)]) = static_cast<int32_t>(i + 1);
        }
        *reinterpret_cast<int32_t*>(&storage_[(Capacity - 1) * sizeof(T)]) = -1;
    }

private:
    alignas(64) char storage_[sizeof(T) * Capacity];
    int32_t free_head_;
    size_t allocated_;
};

} // namespace arctic
