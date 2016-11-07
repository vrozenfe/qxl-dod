/*
 * Copyright 2016 Red Hat, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 *
 * You may obtain a copy of the License at
 * http://www.apache.org/licenses/LICENSE-2.0
 */

#include "driver.h"
#include "compat.h"

static MapIoSpaceFunc DetectMapIoSpace;
MapIoSpaceFunc *MapIoSpace = DetectMapIoSpace;

typedef NTKERNELAPI PVOID MmMapIoSpaceExFunc(
    _In_ PHYSICAL_ADDRESS PhysicalAddress,
    _In_ SIZE_T           NumberOfBytes,
    _In_ ULONG            Protect
);
static MmMapIoSpaceExFunc *pMmMapIoSpaceEx;

// all functions in this module are paged
#pragma code_seg("PAGE")

// we call MmMapIoSpace only if MmMapIoSpaceEx is not present
// so disable the warning
#pragma warning(push)
#pragma warning(disable:30029)
static PVOID OldMapIoSpace(
    _In_ PHYSICAL_ADDRESS PhysicalAddress,
    _In_ SIZE_T           NumberOfBytes,
    _In_ MEMORY_CACHING_TYPE CacheType,
    _In_ ULONG            Protect
)
{
    PAGED_CODE();
    return MmMapIoSpace(PhysicalAddress, NumberOfBytes, CacheType);
}
#pragma warning(pop)

static PVOID NewMapIoSpace(
    _In_ PHYSICAL_ADDRESS PhysicalAddress,
    _In_ SIZE_T           NumberOfBytes,
    _In_ MEMORY_CACHING_TYPE CacheType,
    _In_ ULONG            Protect
)
{
    PAGED_CODE();
    return pMmMapIoSpaceEx(PhysicalAddress, NumberOfBytes, Protect);
}

static PVOID DetectMapIoSpace(
    _In_ PHYSICAL_ADDRESS PhysicalAddress,
    _In_ SIZE_T           NumberOfBytes,
    _In_ MEMORY_CACHING_TYPE CacheType,
    _In_ ULONG            Protect
)
{
    PAGED_CODE();
    UNICODE_STRING name;
    RtlInitUnicodeString(&name, L"MmMapIoSpaceEx");

    pMmMapIoSpaceEx = (MmMapIoSpaceExFunc*)MmGetSystemRoutineAddress(&name);
    if (pMmMapIoSpaceEx) {
        MapIoSpace = NewMapIoSpace;
    } else {
        MapIoSpace = OldMapIoSpace;
    }
    return MapIoSpace(PhysicalAddress, NumberOfBytes, CacheType, Protect);
}
