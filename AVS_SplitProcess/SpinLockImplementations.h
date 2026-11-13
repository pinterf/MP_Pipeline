#pragma once

#include <atomic>
#include <stdint.h>
#include <assert.h>

// The lock word lives in the shared memory, the processes on both sides use it (also
// win32 and win64 together): a lock-free 32 bit atomic, the same size with every compiler
// and data model (long would be 64 bit on LP64 systems).
typedef int32_t spin_lock_value_type_t;
typedef std::atomic<spin_lock_value_type_t> spin_lock_t;
static const spin_lock_value_type_t UNLOCKED = 0;

static_assert(spin_lock_t::is_always_lock_free, "std::atomic<int32_t> must be lock-free (shared between processes)");
static_assert(sizeof(spin_lock_t) == sizeof(spin_lock_value_type_t), "std::atomic<int32_t> must have the size of int32_t");

// Taking the lock is an acquire, releasing it a release operation: the data protected by
// the lock (written before unlock) is visible to the next owner, on any compiler and CPU.

template <spin_lock_value_type_t lock_value = 1>
class SpinLockImplementationBasic
{
    static_assert(lock_value != UNLOCKED, "lock_value must NOT be equal to UNLOCKED");

public:
    SpinLockImplementationBasic(spin_lock_t* lock_ptr) : _lock_ptr(lock_ptr) {}
    void on_before_locking() {}
    int try_lock_once()
    {
        spin_lock_value_type_t expected = UNLOCKED;
        return _lock_ptr->compare_exchange_strong(expected, lock_value, std::memory_order_acquire, std::memory_order_relaxed);
    }
    void unlock()
    {
        assert(_lock_ptr->load(std::memory_order_relaxed) == lock_value);
        _lock_ptr->store(UNLOCKED, std::memory_order_release);
    }
private:
    spin_lock_t* _lock_ptr;
};


class SpinLockImplementationDebug
{
#if _DEBUG
    friend class CondVar;
    friend class CondVarLockAcquire;
#endif

public:
    SpinLockImplementationDebug(spin_lock_t* lock_ptr) : _lock_ptr(lock_ptr) {}
    void on_before_locking()
    {
        assert(_lock_ptr->load(std::memory_order_relaxed) != get_lock_value());
    }
    int try_lock_once()
    {
        spin_lock_value_type_t expected = UNLOCKED;
        return _lock_ptr->compare_exchange_strong(expected, get_lock_value(), std::memory_order_acquire, std::memory_order_relaxed);
    }
    void unlock()
    {
        assert(_lock_ptr->load(std::memory_order_relaxed) == get_lock_value());
        _lock_ptr->store(UNLOCKED, std::memory_order_release);
    }
private:
    spin_lock_value_type_t get_lock_value()
    {
        return (spin_lock_value_type_t)GetCurrentThreadId();
    }
    spin_lock_t* _lock_ptr;
};
