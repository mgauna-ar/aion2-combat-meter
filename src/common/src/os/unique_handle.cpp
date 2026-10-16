#include "common/os/unique_handle.hpp"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace aion2::os {

void close_win32_handle(void* handle) noexcept {
    CloseHandle(static_cast<HANDLE>(handle));
}

void LocalMemoryTraits::close(pointer memory) noexcept {
    LocalFree(static_cast<HLOCAL>(memory));
}

} // namespace aion2::os

#else // !_WIN32

namespace aion2::os {

void close_win32_handle(void*) noexcept {}
void LocalMemoryTraits::close(pointer) noexcept {}

} // namespace aion2::os

#endif
