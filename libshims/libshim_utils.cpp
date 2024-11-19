#include <utils/String8.h>
#include <string.h>

using namespace android;

extern "C" void _ZN7android7String8C2EPKDs(void *thisPtr, const char16_t* o);

extern "C" void _ZN7android7String8C1EPKt(void *thisPtr, const unsigned short* input) {
    _ZN7android7String8C2EPKDs(thisPtr, reinterpret_cast<const char16_t*>(input));
}
