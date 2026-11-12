#pragma once

#include <Windows.h>
#include <intrin.h>
#include <assert.h>

#include "SpinLockImplementations.h"

void fill_processor_count();

extern int _processor_count;

static const int SPIN_LOCK_UNIT = 10;
static const int SLEEP_LOCK_UNIT = 3;

typedef
#if _DEBUG
    SpinLockImplementationDebug
#else
    SpinLockImplementationBasic<> 
#endif
    default_spin_lock_implementation;

template <typename implmentation_t = default_spin_lock_implementation>
class SpinLock
{
#if _DEBUG
    friend class CondVar;
    friend class CondVarLockAcquire;
#endif
public:

    SpinLock(volatile spin_lock_value_type_t* lock_ptr) : impl(lock_ptr)
    {
        assert(lock_ptr);
        if (_processor_count == 0)
        {
            fill_processor_count();
        }
    }
    
    int try_lock(int spin_count = 1)
    {
        return try_lock_impl<false>(spin_count);
    }
    int try_sleep_lock(int sleep_count = 1)
    {
        return try_lock_impl<true>(sleep_count);
    }
    void unlock()
    {
        impl.unlock();
    }
private:

    template<bool is_sleep_lock>
    int __inline try_lock_impl(int spin_count)
    {
        assert(spin_count >= 0);

        impl.on_before_locking();

        assert(_processor_count > 0);
        if (_processor_count == 1 && !is_sleep_lock)
        {
            // never busy-wait on single-processor systems
            int result = impl.try_lock_once();
            if (!result && spin_count > 0)
            {
                // just try once more
                Sleep(0);
                result = impl.try_lock_once();
            }
            return result;
        }
        while (spin_count >= 0)
        {
            int result = impl.try_lock_once();
            if (result)
            {
                return result;
            }
            spin_count--;
            if (is_sleep_lock && spin_count > 0)
            {
                Sleep(0);
            }
        }
        return false;
    }

    implmentation_t impl;
};

template <typename T = default_spin_lock_implementation>
class SpinLockContext
{
public:
    SpinLockContext(SpinLock<T>& lock) : _lock(lock) {}
    ~SpinLockContext() 
    {
        _lock.unlock();
    }
private:
    SpinLock<T>& _lock;
};