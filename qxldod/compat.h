/*
 * Copyright 2016 Red Hat, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 *
 * You may obtain a copy of the License at
 * http://www.apache.org/licenses/LICENSE-2.0
 */

#pragma once
#include "BaseObject.h"

typedef PVOID MapIoSpaceFunc(
    _In_ PHYSICAL_ADDRESS PhysicalAddress,
    _In_ SIZE_T           NumberOfBytes,
    _In_ MEMORY_CACHING_TYPE CacheType,
    _In_ ULONG            Protect
);
extern MapIoSpaceFunc *MapIoSpace;
