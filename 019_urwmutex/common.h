#pragma once

#include <assert.h>
#include <vector>
#include <intrin.h>
#include <Windows.h>

#define CACHE_LINE_SIZE 64

#if defined(_M_IX86)
inline void* InlineTlsGetValue(DWORD dwTlsIndex)
{
    assert(dwTlsIndex < 0x1040);

    if (dwTlsIndex < 0x40)
    {
        return (void*)__readfsdword(0x0e10 + dwTlsIndex * 4);
    }
    else // TlsExpansionSlots
    {
        void** pp = (void**)__readfsdword(0x0F94);
        return pp ? pp[dwTlsIndex * 4 - 0x40 * 4] : NULL;
    }
}
#elif defined(_M_X64)
inline void* InlineTlsGetValue(DWORD dwTlsIndex)
{
    assert(dwTlsIndex < 0x1040);

    if (dwTlsIndex < 0x40)
    {
        return (void*)__readgsqword(dwTlsIndex * 8 + 0x1480);
    }
    else // TlsExpansionSlots
    {
        void** pp = (void**)__readgsqword(0x1780);
        return pp ? pp[dwTlsIndex * 8 - 0x40 * 8] : NULL;
    }
}
#else
inline void* InlineTlsGetValue(DWORD dwTlsIndex)
{
    return TlsGetValue(dwTlsIndex);
}
#endif
