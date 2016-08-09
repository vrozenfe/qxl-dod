#include "BaseObject.h"

#pragma code_seg("PAGE")

#define QXLTAG 'LXQd'

//
// New and delete operators
//
_When_((PoolType & NonPagedPoolMustSucceed) != 0,
    __drv_reportError("Must succeed pool allocations are forbidden. "
            "Allocation failures cause a system crash"))
void* __cdecl operator new(size_t Size, POOL_TYPE PoolType)
{
    PAGED_CODE();

    Size = (Size != 0) ? Size : 1;
    
    void* pObject = ExAllocatePoolWithTag(PoolType, Size, QXLTAG);

    if (pObject != NULL)
    {
#if DBG
        RtlFillMemory(pObject, Size, 0xCD);
#else
        RtlZeroMemory(pObject, Size);
#endif // DBG
    }
    return pObject;
}

_When_((PoolType & NonPagedPoolMustSucceed) != 0,
    __drv_reportError("Must succeed pool allocations are forbidden. "
            "Allocation failures cause a system crash"))
void* __cdecl operator new[](size_t Size, POOL_TYPE PoolType)
{
    PAGED_CODE();

    Size = (Size != 0) ? Size : 1;
    
    void* pObject = ExAllocatePoolWithTag(PoolType, Size, QXLTAG);

    if (pObject != NULL)
    {
#if DBG
        RtlFillMemory(pObject, Size, 0xCD);
#else
        RtlZeroMemory(pObject, Size);
#endif // DBG
    }
    return pObject;
}

void __cdecl operator delete(void* pObject)
{
    PAGED_CODE();

    if (pObject != NULL)
    {
        ExFreePool(pObject);
    }
}

void __cdecl operator delete[](void* pObject)
{
    PAGED_CODE();

    if (pObject != NULL)
    {
        ExFreePool(pObject);
    }
}

void __cdecl operator delete(void *pObject, size_t s)
{
    PAGED_CODE();

    UNREFERENCED_PARAMETER(s);

    if (pObject != NULL) {
        ExFreePool(pObject);
    }
}
