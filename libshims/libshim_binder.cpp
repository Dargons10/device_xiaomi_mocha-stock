#include <stdint.h>

extern "C" void _ZN7android12MemoryDealerC2EjPKcj(void* thisPtr, size_t size, const char* name, uint32_t flags);

extern "C" void _ZN7android12MemoryDealerC1EjPKc(void* thisPtr, size_t size, const char* name) {
    _ZN7android12MemoryDealerC2EjPKcj(thisPtr, size, name, 0);
}
