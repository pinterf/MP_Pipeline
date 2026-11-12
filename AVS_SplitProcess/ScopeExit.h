#pragma once

// Runs a function (lambda) when the guard leaves its scope: at a return, at the end of the
// block, or when an exception (e.g. env->ThrowError) passes through.
// Portable replacement of MSVC's __try / __finally (not available with GCC / MinGW Clang).
//
//   auto cleanup = make_scope_exit([&] { CloseHandle(h); });

#include <utility>

template <typename F>
class ScopeExit
{
public:
    explicit ScopeExit(F&& f) : _f(std::move(f)), _active(true) {}
    ScopeExit(ScopeExit&& o) : _f(std::move(o._f)), _active(o._active) { o._active = false; }
    ~ScopeExit() { if (_active) { _f(); } }
private:
    ScopeExit(const ScopeExit&);
    ScopeExit& operator=(const ScopeExit&);
    F _f;
    bool _active;
};

template <typename F>
ScopeExit<F> make_scope_exit(F&& f)
{
    return ScopeExit<F>(std::forward<F>(f));
}