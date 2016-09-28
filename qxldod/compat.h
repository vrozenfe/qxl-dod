#pragma once
#include "BaseObject.h"

typedef PVOID MapIoSpaceFunc(
    _In_ PHYSICAL_ADDRESS PhysicalAddress,
    _In_ SIZE_T           NumberOfBytes,
    _In_ MEMORY_CACHING_TYPE CacheType,
    _In_ ULONG            Protect
);
extern MapIoSpaceFunc *MapIoSpace;
