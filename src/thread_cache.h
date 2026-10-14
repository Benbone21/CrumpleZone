#pragma once

// MinGW's emulated C++ TLS can free an object's storage before its destructor at
// pthread exit (https://github.com/msys2/MINGW-packages/issues/2519). Keep scratch
// objects in native Windows FLS instead. Each static cache owns a process-lifetime
// slot; Windows destroys the independently allocated object when its thread exits.
#ifdef __MINGW32__
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#undef near
#undef far
#include <memory>
#include <system_error>

template <class T> class ThreadCache {
    DWORD slot;
    static void WINAPI destroy(void* value) { delete static_cast<T*>(value); }
public:
    ThreadCache() : slot(FlsAlloc(destroy)) {
        if (slot == FLS_OUT_OF_INDEXES)
            throw std::system_error(GetLastError(), std::system_category(), "FlsAlloc");
    }
    ThreadCache(const ThreadCache&) = delete;
    ThreadCache& operator=(const ThreadCache&) = delete;
    T& get() const {
        auto* value = static_cast<T*>(FlsGetValue(slot));
        if (!value) {
            auto fresh = std::make_unique<T>();
            if (!FlsSetValue(slot, fresh.get()))
                throw std::system_error(GetLastError(), std::system_category(), "FlsSetValue");
            value = fresh.release();
        }
        return *value;
    }
};
#define CZ_THREAD_CACHE(type, name) \
    static const ThreadCache<type> name##Cache; type& name = name##Cache.get()
#else
#define CZ_THREAD_CACHE(type, name) static thread_local type name
#endif
