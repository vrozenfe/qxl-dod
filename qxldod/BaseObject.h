#pragma once
extern "C"
{
    #define __CPLUSPLUS

    // Standard C-runtime headers
    #include <stddef.h>
    #include <string.h>
    #include <stdarg.h>
    #include <stdio.h>
    #include <stdlib.h>

    #include <initguid.h>

    // NTOS headers
    #include <ntddk.h>

    #ifndef FAR
    #define FAR
    #endif

    // Windows headers
    #include <windef.h>
    #include <winerror.h>

    // Windows GDI headers
    #include <wingdi.h>

    // Windows DDI headers
    #include <winddi.h>
    #include <ntddvdeo.h>

    #include <d3dkmddi.h>
    #include <d3dkmthk.h>

    #include <ntstrsafe.h>
    #include <ntintsafe.h>

    #include <dispmprt.h>
};

//
// Memory handling
//

// Defaulting the value of PoolType means that any call to new Foo()
// will raise a compiler error for being ambiguous. This is to help keep
// any calls to allocate memory from accidentally NOT going through
// these functions.
_When_((PoolType & NonPagedPoolMustSucceed) != 0,
    __drv_reportError("Must succeed pool allocations are forbidden. "
            "Allocation failures cause a system crash"))
void* __cdecl operator new(size_t Size, POOL_TYPE PoolType = PagedPool);
_When_((PoolType & NonPagedPoolMustSucceed) != 0,
    __drv_reportError("Must succeed pool allocations are forbidden. "
            "Allocation failures cause a system crash"))
void* __cdecl operator new[](size_t Size, POOL_TYPE PoolType = PagedPool);
void  __cdecl operator delete(void* pObject);
void  __cdecl operator delete[](void* pObject);
