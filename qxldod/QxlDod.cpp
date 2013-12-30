#include "driver.h"
#include "qxldod.h"

#pragma code_seg(push)
#pragma code_seg()
// BEGIN: Non-Paged Code

// Bit is 1 from Idx to end of byte, with bit count starting at high order
BYTE lMaskTable[BITS_PER_BYTE] = {0xff, 0x7f, 0x3f, 0x1f, 0x0f, 0x07, 0x03, 0x01};

// Bit is 1 from Idx to start of byte, with bit count starting at high order
BYTE rMaskTable[BITS_PER_BYTE] = {0x80, 0xc0, 0xe0, 0xf0, 0xf8, 0xfc, 0xfe, 0xff};

// Bit of Idx is 1, with bit count starting at high order
BYTE PixelMask[BITS_PER_BYTE]  = {0x80, 0x40, 0x20, 0x10, 0x08, 0x04, 0x02, 0x01};


// For the following macros, pPixel must be a BYTE* pointing to the start of a 32 bit pixel
#define CONVERT_32BPP_TO_16BPP(pPixel) ((UPPER_5_BITS(pPixel[2]) << SHIFT_FOR_UPPER_5_IN_565)  | \
                                        (UPPER_6_BITS(pPixel[1]) << SHIFT_FOR_MIDDLE_6_IN_565) | \
                                        (UPPER_5_BITS(pPixel[0])))

// 8bpp is done with 6 levels per color channel since this gives true grays, even if it leaves 40 empty palette entries
// The 6 levels per color is the reason for dividing below by 43 (43 * 6 == 258, closest multiple of 6 to 256)
// It is also the reason for multiplying the red channel by 36 (== 6*6) and the green channel by 6, as this is the
// equivalent to bit shifting in a 3:3:2 model. Changes to this must be reflected in vesasup.cxx with the Blues/Greens/Reds arrays
#define CONVERT_32BPP_TO_8BPP(pPixel) (((pPixel[2] / 43) * 36) + \
                                       ((pPixel[1] / 43) * 6) + \
                                       ((pPixel[0] / 43)))

// 4bpp is done with strict grayscale since this has been found to be usable
// 30% of the red value, 59% of the green value, and 11% of the blue value is the standard way to convert true color to grayscale
#define CONVERT_32BPP_TO_4BPP(pPixel) ((BYTE)(((pPixel[2] * 30) + \
                                               (pPixel[1] * 59) + \
                                               (pPixel[0] * 11)) / (100 * 16)))


// For the following macro, Pixel must be a WORD representing a 16 bit pixel
#define CONVERT_16BPP_TO_32BPP(Pixel) (((ULONG)LOWER_5_BITS((Pixel) >> SHIFT_FOR_UPPER_5_IN_565) << SHIFT_UPPER_5_IN_565_BACK) | \
                                       ((ULONG)LOWER_6_BITS((Pixel) >> SHIFT_FOR_MIDDLE_6_IN_565) << SHIFT_MIDDLE_6_IN_565_BACK) | \
                                       ((ULONG)LOWER_5_BITS((Pixel)) << SHIFT_LOWER_5_IN_565_BACK))


#pragma code_seg(pop)



QxlDod::QxlDod(_In_ DEVICE_OBJECT* pPhysicalDeviceObject) : m_pPhysicalDevice(pPhysicalDeviceObject),
                                                            m_MonitorPowerState(PowerDeviceD0),
                                                            m_AdapterPowerState(PowerDeviceD0)
{
    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));
    *((UINT*)&m_Flags) = 0;
    RtlZeroMemory(&m_DxgkInterface, sizeof(m_DxgkInterface));
    RtlZeroMemory(&m_DeviceInfo, sizeof(m_DeviceInfo));
    RtlZeroMemory(m_CurrentModes, sizeof(m_CurrentModes));
    RtlZeroMemory(&m_PointerShape, sizeof(m_PointerShape));
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
}


QxlDod::~QxlDod(void)
{
    PAGED_CODE();
    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));

    CleanUp();
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
}


NTSTATUS QxlDod::StartDevice(_In_  DXGK_START_INFO*   pDxgkStartInfo,
                         _In_  DXGKRNL_INTERFACE* pDxgkInterface,
                         _Out_ ULONG*             pNumberOfViews,
                         _Out_ ULONG*             pNumberOfChildren)
{
    PAGED_CODE();
    DbgPrint(TRACE_LEVEL_INFORMATION, ("---> %s\n", __FUNCTION__));
    QXL_ASSERT(pDxgkStartInfo != NULL);
    QXL_ASSERT(pDxgkInterface != NULL);
    QXL_ASSERT(pNumberOfViews != NULL);
    QXL_ASSERT(pNumberOfChildren != NULL);
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));

    RtlCopyMemory(&m_DxgkInterface, pDxgkInterface, sizeof(m_DxgkInterface));
    RtlZeroMemory(m_CurrentModes, sizeof(m_CurrentModes));
    m_CurrentModes[0].DispInfo.TargetId = D3DDDI_ID_UNINITIALIZED;
    // Get device information from OS.
    NTSTATUS Status = m_DxgkInterface.DxgkCbGetDeviceInformation(m_DxgkInterface.DeviceHandle, &m_DeviceInfo);
    if (!NT_SUCCESS(Status))
    {
        QXL_LOG_ASSERTION1("DxgkCbGetDeviceInformation failed with status 0x%X\n",
                           Status);
        return Status;
    }

    Status = RegisterHWInfo();
    if (!NT_SUCCESS(Status))
    {
        QXL_LOG_ASSERTION1("RegisterHWInfo failed with status 0x%X\n",
                           Status);
        return Status;
    }

// TODO: Uncomment the line below after updating the TODOs in the function CheckHardware
//    Status = CheckHardware();
//    if (!NT_SUCCESS(Status))
//    {
//        return Status;
//    }


    Status = VbeGetModeList();
    if (!NT_SUCCESS(Status))
    {
        QXL_LOG_ASSERTION1("RegisterHWInfo failed with status 0x%X\n",
                           Status);
        return Status;
    }

    // This sample driver only uses the frame buffer of the POST device. DxgkCbAcquirePostDisplayOwnership 
    // gives you the frame buffer address and ensures that no one else is drawing to it. Be sure to give it back!
    Status = m_DxgkInterface.DxgkCbAcquirePostDisplayOwnership(m_DxgkInterface.DeviceHandle, &(m_CurrentModes[0].DispInfo));
    if (!NT_SUCCESS(Status) || m_CurrentModes[0].DispInfo.Width == 0)
    {
        // The most likely cause of failure is that the driver is simply not running on a POST device, or we are running
        // after a pre-WDDM 1.2 driver. Since we can't draw anything, we should fail to start.
        return STATUS_UNSUCCESSFUL;
    }
   *pNumberOfViews = MAX_VIEWS;
   *pNumberOfChildren = MAX_CHILDREN;
    m_Flags.DriverStarted = TRUE;
    return STATUS_SUCCESS;
}

NTSTATUS QxlDod::StopDevice(VOID)
{
    PAGED_CODE();
    DbgPrint(TRACE_LEVEL_INFORMATION, ("---> %s\n", __FUNCTION__));
    m_Flags.DriverStarted = FALSE;
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
    return STATUS_SUCCESS;
}

VOID QxlDod::CleanUp(VOID)
{
    PAGED_CODE();
    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));
    for (UINT Source = 0; Source < MAX_VIEWS; ++Source)
    {
        if (m_CurrentModes[Source].FrameBuffer.Ptr)
        {
            UnmapFrameBuffer(m_CurrentModes[Source].FrameBuffer.Ptr, m_CurrentModes[Source].DispInfo.Height * m_CurrentModes[Source].DispInfo.Pitch);
            m_CurrentModes[Source].FrameBuffer.Ptr = NULL;
            m_CurrentModes[Source].Flags.FrameBufferIsActive = FALSE;
        }
    }
}


NTSTATUS QxlDod::DispatchIoRequest(_In_  ULONG VidPnSourceId,
                                   _In_  VIDEO_REQUEST_PACKET* pVideoRequestPacket)
{
    PAGED_CODE();
    UNREFERENCED_PARAMETER(VidPnSourceId);
    UNREFERENCED_PARAMETER(pVideoRequestPacket);

    DbgPrint(TRACE_LEVEL_INFORMATION, ("---> %s\n", __FUNCTION__));
    return STATUS_SUCCESS;
}

PCHAR
DbgDevicePowerString(
    __in DEVICE_POWER_STATE Type
    )
{
    switch (Type)
    {
    case PowerDeviceUnspecified:
        return "PowerDeviceUnspecified";
    case PowerDeviceD0:
        return "PowerDeviceD0";
    case PowerDeviceD1:
        return "PowerDeviceD1";
    case PowerDeviceD2:
        return "PowerDeviceD2";
    case PowerDeviceD3:
        return "PowerDeviceD3";
    case PowerDeviceMaximum:
        return "PowerDeviceMaximum";
    default:
        return "UnKnown Device Power State";
    }
}

PCHAR
DbgPowerActionString(
    __in POWER_ACTION Type
    )
{
    switch (Type)
    {
    case PowerActionNone:
        return "PowerActionNone";
    case PowerActionReserved:
        return "PowerActionReserved";
    case PowerActionSleep:
        return "PowerActionSleep";
    case PowerActionHibernate:
        return "PowerActionHibernate";
    case PowerActionShutdown:
        return "PowerActionShutdown";
    case PowerActionShutdownReset:
        return "PowerActionShutdownReset";
    case PowerActionShutdownOff:
        return "PowerActionShutdownOff";
    case PowerActionWarmEject:
        return "PowerActionWarmEject";
    default:
        return "UnKnown Device Power State";
    }
}

NTSTATUS QxlDod::SetPowerState(_In_  ULONG HardwareUid,
                               _In_  DEVICE_POWER_STATE DevicePowerState,
                               _In_  POWER_ACTION       ActionType)
{
    PAGED_CODE();
    DbgPrint(TRACE_LEVEL_INFORMATION, ("---> %s HardwareUid = 0x%x ActionType = %s DevicePowerState = %s AdapterPowerState = %s\n", __FUNCTION__, HardwareUid, DbgPowerActionString(ActionType), DbgDevicePowerString(DevicePowerState), DbgDevicePowerString(m_AdapterPowerState)));

    if (HardwareUid == DISPLAY_ADAPTER_HW_ID)
    {
        if (DevicePowerState == PowerDeviceD0)
        {

            // When returning from D3 the device visibility defined to be off for all targets
            if (m_AdapterPowerState == PowerDeviceD3)
            {
                DXGKARG_SETVIDPNSOURCEVISIBILITY Visibility;
                Visibility.VidPnSourceId = D3DDDI_ID_ALL;
                Visibility.Visible = FALSE;
                SetVidPnSourceVisibility(&Visibility);
            }
        }

        // Store new adapter power state
        m_AdapterPowerState = DevicePowerState;

        // There is nothing to do to specifically power up/down the display adapter
        return STATUS_SUCCESS;
    }
    // TODO: This is where the specified monitor should be powered up/down
    VbeSetPowerState(ActionType);
    return STATUS_SUCCESS;
}

NTSTATUS QxlDod::QueryChildRelations(_Out_writes_bytes_(ChildRelationsSize) DXGK_CHILD_DESCRIPTOR* pChildRelations,
                                     _In_  ULONG  ChildRelationsSize)
{
    PAGED_CODE();

    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));
    QXL_ASSERT(pChildRelations != NULL);

    // The last DXGK_CHILD_DESCRIPTOR in the array of pChildRelations must remain zeroed out, so we subtract this from the count
    ULONG ChildRelationsCount = (ChildRelationsSize / sizeof(DXGK_CHILD_DESCRIPTOR)) - 1;
    QXL_ASSERT(ChildRelationsCount <= MAX_CHILDREN);

    for (UINT ChildIndex = 0; ChildIndex < ChildRelationsCount; ++ChildIndex)
    {
        pChildRelations[ChildIndex].ChildDeviceType = TypeVideoOutput;
        pChildRelations[ChildIndex].ChildCapabilities.HpdAwareness = HpdAwarenessAlwaysConnected;
        pChildRelations[ChildIndex].ChildCapabilities.Type.VideoOutput.InterfaceTechnology = D3DKMDT_VOT_INTERNAL;
        pChildRelations[ChildIndex].ChildCapabilities.Type.VideoOutput.MonitorOrientationAwareness = D3DKMDT_MOA_NONE;
        pChildRelations[ChildIndex].ChildCapabilities.Type.VideoOutput.SupportsSdtvModes = FALSE;
        // TODO: Replace 0 with the actual ACPI ID of the child device, if available
        pChildRelations[ChildIndex].AcpiUid = 0;
        pChildRelations[ChildIndex].ChildUid = ChildIndex;
    }

    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
    return STATUS_SUCCESS;
}

NTSTATUS QxlDod::QueryChildStatus(_Inout_ DXGK_CHILD_STATUS* pChildStatus,
                                                _In_    BOOLEAN            NonDestructiveOnly)
{
    PAGED_CODE();
    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));
    UNREFERENCED_PARAMETER(NonDestructiveOnly);
    QXL_ASSERT(pChildStatus != NULL);
    QXL_ASSERT(pChildStatus->ChildUid < MAX_CHILDREN);

    switch (pChildStatus->Type)
    {
        case StatusConnection:
        {
            // HpdAwarenessInterruptible was reported since HpdAwarenessNone is deprecated.
            // However, BDD has no knowledge of HotPlug events, so just always return connected.
            pChildStatus->HotPlug.Connected = IsDriverActive();
            return STATUS_SUCCESS;
        }

        case StatusRotation:
        {
            // D3DKMDT_MOA_NONE was reported, so this should never be called
            DbgPrint(TRACE_LEVEL_ERROR, ("Child status being queried for StatusRotation even though D3DKMDT_MOA_NONE was reported"));
            return STATUS_INVALID_PARAMETER;
        }

        default:
        {
            DbgPrint(TRACE_LEVEL_WARNING, ("Unknown pChildStatus->Type (0x%I64x) requested.", pChildStatus->Type));
            return STATUS_NOT_SUPPORTED;
        }
    }
}

// EDID retrieval
NTSTATUS QxlDod::QueryDeviceDescriptor(_In_    ULONG                   ChildUid,
                                       _Inout_ DXGK_DEVICE_DESCRIPTOR* pDeviceDescriptor)
{
    PAGED_CODE();
    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));
    QXL_ASSERT(pDeviceDescriptor != NULL);
    QXL_ASSERT(ChildUid < MAX_CHILDREN);

    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
    return STATUS_MONITOR_NO_MORE_DESCRIPTOR_DATA;
}

NTSTATUS QxlDod::QueryAdapterInfo(_In_ CONST DXGKARG_QUERYADAPTERINFO* pQueryAdapterInfo)
{
    PAGED_CODE();

    QXL_ASSERT(pQueryAdapterInfo != NULL);
    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));

    switch (pQueryAdapterInfo->Type)
    {
        case DXGKQAITYPE_DRIVERCAPS:
        {
            if (pQueryAdapterInfo->OutputDataSize < sizeof(DXGK_DRIVERCAPS))
            {
                DbgPrint(TRACE_LEVEL_ERROR, ("pQueryAdapterInfo->OutputDataSize (0x%I64x) is smaller than sizeof(DXGK_DRIVERCAPS) (0x%I64x)", pQueryAdapterInfo->OutputDataSize, sizeof(DXGK_DRIVERCAPS)));
                return STATUS_BUFFER_TOO_SMALL;
            }

            DXGK_DRIVERCAPS* pDriverCaps = (DXGK_DRIVERCAPS*)pQueryAdapterInfo->pOutputData;

            RtlZeroMemory(pDriverCaps, sizeof(DXGK_DRIVERCAPS));

            pDriverCaps->WDDMVersion = DXGKDDI_WDDMv1_2;
            pDriverCaps->HighestAcceptableAddress.QuadPart = -1;
/*
            pDriverCaps->MaxPointerWidth  = 64;
            pDriverCaps->MaxPointerHeight = 64;
            pDriverCaps->PointerCaps.Monochrome = 1;
            pDriverCaps->PointerCaps.Color = 1;
*/
            DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s 1\n", __FUNCTION__));
            return STATUS_SUCCESS;
        }

        default:
        {
            // BDD does not need to support any other adapter information types
            DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
            return STATUS_NOT_SUPPORTED;
        }
    }
}

NTSTATUS QxlDod::SetPointerPosition(_In_ CONST DXGKARG_SETPOINTERPOSITION* pSetPointerPosition)
{
    PAGED_CODE();
    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));

    QXL_ASSERT(pSetPointerPosition != NULL);
    QXL_ASSERT(pSetPointerPosition->VidPnSourceId < MAX_VIEWS);

    if (!(pSetPointerPosition->Flags.Visible))
    {
        DbgPrint(TRACE_LEVEL_INFORMATION, ("<--- %s Cursor is not visible\n", __FUNCTION__));
        return STATUS_SUCCESS;
    }
    return STATUS_UNSUCCESSFUL;
}

// Basic Sample Display Driver does not support hardware cursors, and reports such
// in QueryAdapterInfo. Therefore this function should never be called.
NTSTATUS QxlDod::SetPointerShape(_In_ CONST DXGKARG_SETPOINTERSHAPE* pSetPointerShape)
{
    PAGED_CODE();
    QXL_ASSERT(pSetPointerShape != NULL);

    DbgPrint(TRACE_LEVEL_INFORMATION, ("<---> %s Height = %d, Width = %d, XHot= %d, YHot = %d SourceId = %d\n", 
        __FUNCTION__, pSetPointerShape->Height, pSetPointerShape->Width, pSetPointerShape->XHot, pSetPointerShape->YHot, pSetPointerShape->VidPnSourceId));
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS QxlDod::Escape(_In_ CONST DXGKARG_ESCAPE* pEscape)
{
    PAGED_CODE();
    QXL_ASSERT(pEscape != NULL);

    DbgPrint(TRACE_LEVEL_VERBOSE, ("<---> %s Flags = %d\n", __FUNCTION__, pEscape->Flags));

    return STATUS_NOT_IMPLEMENTED;
}


NTSTATUS QxlDod::PresentDisplayOnly(_In_ CONST DXGKARG_PRESENT_DISPLAYONLY* pPresentDisplayOnly)
{
    PAGED_CODE();
    NTSTATUS Status = STATUS_SUCCESS;
    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));
    QXL_ASSERT(pPresentDisplayOnly != NULL);
    QXL_ASSERT(pPresentDisplayOnly->VidPnSourceId < MAX_VIEWS);

    if (pPresentDisplayOnly->BytesPerPixel < 4)
    {
        // Only >=32bpp modes are reported, therefore this Present should never pass anything less than 4 bytes per pixel
        DbgPrint(TRACE_LEVEL_ERROR, ("pPresentDisplayOnly->BytesPerPixel is 0x%d, which is lower than the allowed.\n", pPresentDisplayOnly->BytesPerPixel));
        return STATUS_INVALID_PARAMETER;
    }

    // If it is in monitor off state or source is not supposed to be visible, don't present anything to the screen
    if ((m_MonitorPowerState > PowerDeviceD0) ||
        (m_CurrentModes[pPresentDisplayOnly->VidPnSourceId].Flags.SourceNotVisible))
    {
        DbgPrint(TRACE_LEVEL_ERROR, ("<--- %s\n", __FUNCTION__));
        return STATUS_SUCCESS;
    }

    if (m_CurrentModes[pPresentDisplayOnly->VidPnSourceId].Flags.FrameBufferIsActive)
    {

        // If actual pixels are coming through, will need to completely zero out physical address next time in BlackOutScreen
        m_CurrentModes[pPresentDisplayOnly->VidPnSourceId].ZeroedOutStart.QuadPart = 0;
        m_CurrentModes[pPresentDisplayOnly->VidPnSourceId].ZeroedOutEnd.QuadPart = 0;


        D3DKMDT_VIDPN_PRESENT_PATH_ROTATION RotationNeededByFb = pPresentDisplayOnly->Flags.Rotate ?
                                                                 m_CurrentModes[pPresentDisplayOnly->VidPnSourceId].Rotation :
                                                                 D3DKMDT_VPPR_IDENTITY;
        BYTE* pDst = (BYTE*)m_CurrentModes[pPresentDisplayOnly->VidPnSourceId].FrameBuffer.Ptr;
        UINT DstBitPerPixel = BPPFromPixelFormat(m_CurrentModes[pPresentDisplayOnly->VidPnSourceId].DispInfo.ColorFormat);
        if (m_CurrentModes[pPresentDisplayOnly->VidPnSourceId].Scaling == D3DKMDT_VPPS_CENTERED)
        {
            UINT CenterShift = (m_CurrentModes[pPresentDisplayOnly->VidPnSourceId].DispInfo.Height -
                m_CurrentModes[pPresentDisplayOnly->VidPnSourceId].SrcModeHeight)*m_CurrentModes[pPresentDisplayOnly->VidPnSourceId].DispInfo.Pitch;
            CenterShift += (m_CurrentModes[pPresentDisplayOnly->VidPnSourceId].DispInfo.Width -
                m_CurrentModes[pPresentDisplayOnly->VidPnSourceId].SrcModeWidth)*DstBitPerPixel/8;
            pDst += (int)CenterShift/2;
        }
        Status = ExecutePresentDisplayOnly(
                            pDst,
                            DstBitPerPixel,
                            (BYTE*)pPresentDisplayOnly->pSource,
                            pPresentDisplayOnly->BytesPerPixel,
                            pPresentDisplayOnly->Pitch,
                            pPresentDisplayOnly->NumMoves,
                            pPresentDisplayOnly->pMoves,
                            pPresentDisplayOnly->NumDirtyRects,
                            pPresentDisplayOnly->pDirtyRect,
                            RotationNeededByFb);
    }
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
    return Status;
}

NTSTATUS QxlDod::QueryInterface(_In_ CONST PQUERY_INTERFACE pQueryInterface)
{
    PAGED_CODE();
    QXL_ASSERT(pQueryInterface != NULL);

    DbgPrint(TRACE_LEVEL_VERBOSE, ("<---> %s Version = %d\n", __FUNCTION__, pQueryInterface->Version));

    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS QxlDod::StopDeviceAndReleasePostDisplayOwnership(_In_  D3DDDI_VIDEO_PRESENT_TARGET_ID TargetId,
                                                          _Out_ DXGK_DISPLAY_INFORMATION*      pDisplayInfo)
{
    PAGED_CODE();

    UNREFERENCED_PARAMETER(pDisplayInfo);
    QXL_ASSERT(TargetId < MAX_CHILDREN);
    D3DDDI_VIDEO_PRESENT_SOURCE_ID SourceId = FindSourceForTarget(TargetId, TRUE);

    // In case BDD is the next driver to run, the monitor should not be off, since
    // this could cause the BIOS to hang when the EDID is retrieved on Start.
    if (m_MonitorPowerState > PowerDeviceD0)
    {
        SetPowerState(TargetId, PowerDeviceD0, PowerActionNone);
    }

    // The driver has to black out the display and ensure it is visible when releasing ownership
    BlackOutScreen(SourceId);

    *pDisplayInfo = m_CurrentModes[SourceId].DispInfo;

    return StopDevice();
}

VOID QxlDod::BlackOutScreen(D3DDDI_VIDEO_PRESENT_SOURCE_ID SourceId)
{
    PAGED_CODE();

    UINT ScreenHeight = m_CurrentModes[SourceId].DispInfo.Height;
    UINT ScreenPitch = m_CurrentModes[SourceId].DispInfo.Pitch;

    PHYSICAL_ADDRESS NewPhysAddrStart = m_CurrentModes[SourceId].DispInfo.PhysicAddress;
    PHYSICAL_ADDRESS NewPhysAddrEnd;
    NewPhysAddrEnd.QuadPart = NewPhysAddrStart.QuadPart + (ScreenHeight * ScreenPitch);

    if (m_CurrentModes[SourceId].Flags.FrameBufferIsActive)
    {
        BYTE* MappedAddr = reinterpret_cast<BYTE*>(m_CurrentModes[SourceId].FrameBuffer.Ptr);

        // Zero any memory at the start that hasn't been zeroed recently
        if (NewPhysAddrStart.QuadPart < m_CurrentModes[SourceId].ZeroedOutStart.QuadPart)
        {
            if (NewPhysAddrEnd.QuadPart < m_CurrentModes[SourceId].ZeroedOutStart.QuadPart)
            {
                // No overlap
                RtlZeroMemory(MappedAddr, ScreenHeight * ScreenPitch);
            }
            else
            {
                RtlZeroMemory(MappedAddr, (UINT)(m_CurrentModes[SourceId].ZeroedOutStart.QuadPart - NewPhysAddrStart.QuadPart));
            }
        }

        // Zero any memory at the end that hasn't been zeroed recently
        if (NewPhysAddrEnd.QuadPart > m_CurrentModes[SourceId].ZeroedOutEnd.QuadPart)
        {
            if (NewPhysAddrStart.QuadPart > m_CurrentModes[SourceId].ZeroedOutEnd.QuadPart)
            {
                // No overlap
                // NOTE: When actual pixels were the most recent thing drawn, ZeroedOutStart & ZeroedOutEnd will both be 0
                // and this is the path that will be used to black out the current screen.
                RtlZeroMemory(MappedAddr, ScreenHeight * ScreenPitch);
            }
            else
            {
                RtlZeroMemory(MappedAddr, (UINT)(NewPhysAddrEnd.QuadPart - m_CurrentModes[SourceId].ZeroedOutEnd.QuadPart));
            }
        }
    }

    m_CurrentModes[SourceId].ZeroedOutStart.QuadPart = NewPhysAddrStart.QuadPart;
    m_CurrentModes[SourceId].ZeroedOutEnd.QuadPart = NewPhysAddrEnd.QuadPart;
}

NTSTATUS QxlDod::QueryVidPnHWCapability(_Inout_ DXGKARG_QUERYVIDPNHWCAPABILITY* pVidPnHWCaps)
{
    PAGED_CODE();
    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));

    QXL_ASSERT(pVidPnHWCaps != NULL);
    QXL_ASSERT(pVidPnHWCaps->SourceId < MAX_VIEWS);
    QXL_ASSERT(pVidPnHWCaps->TargetId < MAX_CHILDREN);

    pVidPnHWCaps->VidPnHWCaps.DriverRotation             = 1; // BDD does rotation in software
    pVidPnHWCaps->VidPnHWCaps.DriverScaling              = 0; // BDD does not support scaling
    pVidPnHWCaps->VidPnHWCaps.DriverCloning              = 0; // BDD does not support clone
    pVidPnHWCaps->VidPnHWCaps.DriverColorConvert         = 1; // BDD does color conversions in software
    pVidPnHWCaps->VidPnHWCaps.DriverLinkedAdapaterOutput = 0; // BDD does not support linked adapters
    pVidPnHWCaps->VidPnHWCaps.DriverRemoteDisplay        = 0; // BDD does not support remote displays

    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
    return STATUS_SUCCESS;
}


// TODO: Need to also check pinned modes and the path parameters, not just topology
NTSTATUS QxlDod::IsSupportedVidPn(_Inout_ DXGKARG_ISSUPPORTEDVIDPN* pIsSupportedVidPn)
{
    PAGED_CODE();
    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));

    QXL_ASSERT(pIsSupportedVidPn != NULL);

    if (pIsSupportedVidPn->hDesiredVidPn == 0)
    {
        // A null desired VidPn is supported
        pIsSupportedVidPn->IsVidPnSupported = TRUE;
        return STATUS_SUCCESS;
    }

    // Default to not supported, until shown it is supported
    pIsSupportedVidPn->IsVidPnSupported = FALSE;

    CONST DXGK_VIDPN_INTERFACE* pVidPnInterface;
    NTSTATUS Status = m_DxgkInterface.DxgkCbQueryVidPnInterface(pIsSupportedVidPn->hDesiredVidPn, DXGK_VIDPN_INTERFACE_VERSION_V1, &pVidPnInterface);
    if (!NT_SUCCESS(Status))
    {
        DbgPrint(TRACE_LEVEL_ERROR, ("DxgkCbQueryVidPnInterface failed with Status = 0x%X, hDesiredVidPn = 0x%I64x\n", Status, pIsSupportedVidPn->hDesiredVidPn));
        return Status;
    }

    D3DKMDT_HVIDPNTOPOLOGY hVidPnTopology;
    CONST DXGK_VIDPNTOPOLOGY_INTERFACE* pVidPnTopologyInterface;
    Status = pVidPnInterface->pfnGetTopology(pIsSupportedVidPn->hDesiredVidPn, &hVidPnTopology, &pVidPnTopologyInterface);
    if (!NT_SUCCESS(Status))
    {
        DbgPrint(TRACE_LEVEL_ERROR, ("pfnGetTopology failed with Status = 0x%X, hDesiredVidPn = 0x%I64x\n", Status, pIsSupportedVidPn->hDesiredVidPn));
        return Status;
    }

    // For every source in this topology, make sure they don't have more paths than there are targets
    for (D3DDDI_VIDEO_PRESENT_SOURCE_ID SourceId = 0; SourceId < MAX_VIEWS; ++SourceId)
    {
        SIZE_T NumPathsFromSource = 0;
        Status = pVidPnTopologyInterface->pfnGetNumPathsFromSource(hVidPnTopology, SourceId, &NumPathsFromSource);
        if (Status == STATUS_GRAPHICS_SOURCE_NOT_IN_TOPOLOGY)
        {
            continue;
        }
        else if (!NT_SUCCESS(Status))
        {
            DbgPrint(TRACE_LEVEL_ERROR, ("pfnGetNumPathsFromSource failed with Status = 0x%X hVidPnTopology = 0x%I64x, SourceId = 0x%I64x",
                           Status, hVidPnTopology, SourceId));
            return Status;
        }
        else if (NumPathsFromSource > MAX_CHILDREN)
        {
            // This VidPn is not supported, which has already been set as the default
            return STATUS_SUCCESS;
        }
    }

    // All sources succeeded so this VidPn is supported
    pIsSupportedVidPn->IsVidPnSupported = TRUE;
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
    return STATUS_SUCCESS;
}

NTSTATUS QxlDod::RecommendFunctionalVidPn(_In_ CONST DXGKARG_RECOMMENDFUNCTIONALVIDPN* CONST pRecommendFunctionalVidPn)
{
    PAGED_CODE();
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<---> %s\n", __FUNCTION__));

    QXL_ASSERT(pRecommendFunctionalVidPn == NULL);

    return STATUS_GRAPHICS_NO_RECOMMENDED_FUNCTIONAL_VIDPN;
}

NTSTATUS QxlDod::RecommendVidPnTopology(_In_ CONST DXGKARG_RECOMMENDVIDPNTOPOLOGY* CONST pRecommendVidPnTopology)
{
    PAGED_CODE();
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<---> %s\n", __FUNCTION__));

    QXL_ASSERT(pRecommendVidPnTopology == NULL);

    return STATUS_GRAPHICS_NO_RECOMMENDED_FUNCTIONAL_VIDPN;
}

NTSTATUS QxlDod::RecommendMonitorModes(_In_ CONST DXGKARG_RECOMMENDMONITORMODES* CONST pRecommendMonitorModes)
{
    PAGED_CODE();
    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));

    return AddSingleMonitorMode(pRecommendMonitorModes);
}


NTSTATUS QxlDod::AddSingleSourceMode(_In_ CONST DXGK_VIDPNSOURCEMODESET_INTERFACE* pVidPnSourceModeSetInterface,
                                                   D3DKMDT_HVIDPNSOURCEMODESET hVidPnSourceModeSet,
                                                   D3DDDI_VIDEO_PRESENT_SOURCE_ID SourceId)
{
    PAGED_CODE();
    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));
    UNREFERENCED_PARAMETER(SourceId);

    // There is only one source format supported by display-only drivers, but more can be added in a 
    // full WDDM driver if the hardware supports them
    for (ULONG idx = 0; idx < m_ModeCount; ++idx)
    {
        // Create new mode info that will be populated
        D3DKMDT_VIDPN_SOURCE_MODE* pVidPnSourceModeInfo = NULL;
        NTSTATUS Status = pVidPnSourceModeSetInterface->pfnCreateNewModeInfo(hVidPnSourceModeSet, &pVidPnSourceModeInfo);
        if (!NT_SUCCESS(Status))
        {
            // If failed to create a new mode info, mode doesn't need to be released since it was never created
            DbgPrint(TRACE_LEVEL_ERROR, ("pfnCreateNewModeInfo failed with Status = 0x%X, hVidPnSourceModeSet = 0x%I64x", Status, hVidPnSourceModeSet));
            return Status;
        }

        // Populate mode info with values from current mode and hard-coded values
        // Always report 32 bpp format, this will be color converted during the present if the mode is < 32bpp
        pVidPnSourceModeInfo->Type = D3DKMDT_RMT_GRAPHICS;
        pVidPnSourceModeInfo->Format.Graphics.PrimSurfSize.cx = m_ModeInfo[idx].XResolution;
        pVidPnSourceModeInfo->Format.Graphics.PrimSurfSize.cy = m_ModeInfo[idx].YResolution;
        pVidPnSourceModeInfo->Format.Graphics.VisibleRegionSize = pVidPnSourceModeInfo->Format.Graphics.PrimSurfSize;
        pVidPnSourceModeInfo->Format.Graphics.Stride = m_ModeInfo[idx].BytesPerScanLine / m_ModeInfo[idx].XResolution;
        pVidPnSourceModeInfo->Format.Graphics.PixelFormat = D3DDDIFMT_A8R8G8B8;
        pVidPnSourceModeInfo->Format.Graphics.ColorBasis = D3DKMDT_CB_SCRGB;
        pVidPnSourceModeInfo->Format.Graphics.PixelValueAccessMode = D3DKMDT_PVAM_DIRECT;

        // Add the mode to the source mode set
        Status = pVidPnSourceModeSetInterface->pfnAddMode(hVidPnSourceModeSet, pVidPnSourceModeInfo);
        if (!NT_SUCCESS(Status))
        {
            // If adding the mode failed, release the mode, if this doesn't work there is nothing that can be done, some memory will get leaked
            NTSTATUS TempStatus = pVidPnSourceModeSetInterface->pfnReleaseModeInfo(hVidPnSourceModeSet, pVidPnSourceModeInfo);
            UNREFERENCED_PARAMETER(TempStatus);
            NT_ASSERT(NT_SUCCESS(TempStatus));

            if (Status != STATUS_GRAPHICS_MODE_ALREADY_IN_MODESET)
            {
                DbgPrint(TRACE_LEVEL_ERROR, ("pfnAddMode failed with Status = 0x%X, hVidPnSourceModeSet = 0x%I64x, pVidPnSourceModeInfo = 0x%I64x", Status, hVidPnSourceModeSet, pVidPnSourceModeInfo));
                return Status;
            }
        }
    }

    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
    return STATUS_SUCCESS;
}

// Add the current mode information (acquired from the POST frame buffer) as the target mode.
NTSTATUS QxlDod::AddSingleTargetMode(_In_ CONST DXGK_VIDPNTARGETMODESET_INTERFACE* pVidPnTargetModeSetInterface,
                                                   D3DKMDT_HVIDPNTARGETMODESET hVidPnTargetModeSet,
                                                   _In_opt_ CONST D3DKMDT_VIDPN_SOURCE_MODE* pVidPnPinnedSourceModeInfo,
                                                   D3DDDI_VIDEO_PRESENT_SOURCE_ID SourceId)
{
    PAGED_CODE();
    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));
    UNREFERENCED_PARAMETER(pVidPnPinnedSourceModeInfo);

    D3DKMDT_VIDPN_TARGET_MODE* pVidPnTargetModeInfo = NULL;
    NTSTATUS Status  = STATUS_SUCCESS;

    for (UINT ModeIndex = 0; ModeIndex < m_ModeCount; ++ModeIndex)
    {
        pVidPnTargetModeInfo = NULL;
        Status = pVidPnTargetModeSetInterface->pfnCreateNewModeInfo(hVidPnTargetModeSet, &pVidPnTargetModeInfo);
        if (!NT_SUCCESS(Status))
        {
            // If failed to create a new mode info, mode doesn't need to be released since it was never created
            DbgPrint(TRACE_LEVEL_ERROR, ("pfnCreateNewModeInfo failed with Status = 0x%I64x, hVidPnTargetModeSet = 0x%I64x", Status, hVidPnTargetModeSet));
            return Status;
        }
        pVidPnTargetModeInfo->VideoSignalInfo.VideoStandard = D3DKMDT_VSS_OTHER;
        pVidPnTargetModeInfo->VideoSignalInfo.TotalSize.cx = m_ModeInfo[SourceId].XResolution;
        pVidPnTargetModeInfo->VideoSignalInfo.TotalSize.cy = m_ModeInfo[SourceId].YResolution;
        pVidPnTargetModeInfo->VideoSignalInfo.ActiveSize = pVidPnTargetModeInfo->VideoSignalInfo.TotalSize;
        pVidPnTargetModeInfo->VideoSignalInfo.VSyncFreq.Numerator = D3DKMDT_FREQUENCY_NOTSPECIFIED;
        pVidPnTargetModeInfo->VideoSignalInfo.VSyncFreq.Denominator = D3DKMDT_FREQUENCY_NOTSPECIFIED;
        pVidPnTargetModeInfo->VideoSignalInfo.HSyncFreq.Numerator = D3DKMDT_FREQUENCY_NOTSPECIFIED;
        pVidPnTargetModeInfo->VideoSignalInfo.HSyncFreq.Denominator = D3DKMDT_FREQUENCY_NOTSPECIFIED;
        pVidPnTargetModeInfo->VideoSignalInfo.PixelRate = D3DKMDT_FREQUENCY_NOTSPECIFIED;
        pVidPnTargetModeInfo->VideoSignalInfo.ScanLineOrdering = D3DDDI_VSSLO_PROGRESSIVE;
    // We add this as PREFERRED since it is the only supported target
        pVidPnTargetModeInfo->Preference = D3DKMDT_MP_NOTPREFERRED; // TODO: another logic for prefferred mode. Maybe the pinned source mode

        Status = pVidPnTargetModeSetInterface->pfnAddMode(hVidPnTargetModeSet, pVidPnTargetModeInfo);
        if (!NT_SUCCESS(Status))
        {
            if (Status != STATUS_GRAPHICS_MODE_ALREADY_IN_MODESET)
            {
                DbgPrint(TRACE_LEVEL_ERROR, ("pfnAddMode failed with Status = 0x%I64x, hVidPnTargetModeSet = 0x%I64x, pVidPnTargetModeInfo = 0x%I64x", Status, hVidPnTargetModeSet, pVidPnTargetModeInfo));
            }
            
            // If adding the mode failed, release the mode, if this doesn't work there is nothing that can be done, some memory will get leaked
            Status = pVidPnTargetModeSetInterface->pfnReleaseModeInfo(hVidPnTargetModeSet, pVidPnTargetModeInfo);
            NT_ASSERT(NT_SUCCESS(Status));
        }
    }
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
    return STATUS_SUCCESS;
}


NTSTATUS QxlDod::AddSingleMonitorMode(_In_ CONST DXGKARG_RECOMMENDMONITORMODES* CONST pRecommendMonitorModes)
{
    PAGED_CODE();
    NTSTATUS Status = STATUS_SUCCESS;
    D3DKMDT_MONITOR_SOURCE_MODE* pMonitorSourceMode = NULL;
    PVBE_MODEINFO pVbeModeInfo = NULL;

    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));

    Status = pRecommendMonitorModes->pMonitorSourceModeSetInterface->pfnCreateNewModeInfo(pRecommendMonitorModes->hMonitorSourceModeSet, &pMonitorSourceMode);
    if (!NT_SUCCESS(Status))
    {
        // If failed to create a new mode info, mode doesn't need to be released since it was never created
        DbgPrint(TRACE_LEVEL_ERROR, ("pfnCreateNewModeInfo failed with Status = 0x%X, hMonitorSourceModeSet = 0x%I64x", Status, pRecommendMonitorModes->hMonitorSourceModeSet));
        return Status;
    }

    pVbeModeInfo = &m_ModeInfo[m_CurrentMode];

    // Since we don't know the real monitor timing information, just use the current display mode (from the POST device) with unknown frequencies
    pMonitorSourceMode->VideoSignalInfo.VideoStandard = D3DKMDT_VSS_OTHER;
    pMonitorSourceMode->VideoSignalInfo.TotalSize.cx = pVbeModeInfo->XResolution;
    pMonitorSourceMode->VideoSignalInfo.TotalSize.cy = pVbeModeInfo->YResolution;
    pMonitorSourceMode->VideoSignalInfo.ActiveSize = pMonitorSourceMode->VideoSignalInfo.TotalSize;
    pMonitorSourceMode->VideoSignalInfo.VSyncFreq.Numerator = D3DKMDT_FREQUENCY_NOTSPECIFIED;
    pMonitorSourceMode->VideoSignalInfo.VSyncFreq.Denominator = D3DKMDT_FREQUENCY_NOTSPECIFIED;
    pMonitorSourceMode->VideoSignalInfo.HSyncFreq.Numerator = D3DKMDT_FREQUENCY_NOTSPECIFIED;
    pMonitorSourceMode->VideoSignalInfo.HSyncFreq.Denominator = D3DKMDT_FREQUENCY_NOTSPECIFIED;
    pMonitorSourceMode->VideoSignalInfo.PixelRate = D3DKMDT_FREQUENCY_NOTSPECIFIED;
    pMonitorSourceMode->VideoSignalInfo.ScanLineOrdering = D3DDDI_VSSLO_PROGRESSIVE;

    // We set the preference to PREFERRED since this is the only supported mode
    pMonitorSourceMode->Origin = D3DKMDT_MCO_DRIVER;
    pMonitorSourceMode->Preference = D3DKMDT_MP_PREFERRED;
    pMonitorSourceMode->ColorBasis = D3DKMDT_CB_SRGB;
    pMonitorSourceMode->ColorCoeffDynamicRanges.FirstChannel = 8;
    pMonitorSourceMode->ColorCoeffDynamicRanges.SecondChannel = 8;
    pMonitorSourceMode->ColorCoeffDynamicRanges.ThirdChannel = 8;
    pMonitorSourceMode->ColorCoeffDynamicRanges.FourthChannel = 8;

    Status = pRecommendMonitorModes->pMonitorSourceModeSetInterface->pfnAddMode(pRecommendMonitorModes->hMonitorSourceModeSet, pMonitorSourceMode);
    if (!NT_SUCCESS(Status))
    {
        if (Status != STATUS_GRAPHICS_MODE_ALREADY_IN_MODESET)
        {
            DbgPrint(TRACE_LEVEL_ERROR, ("pfnAddMode failed with Status = 0x%X, hMonitorSourceModeSet = 0x%I64x, pMonitorSourceMode = 0x%I64x",
                            Status, pRecommendMonitorModes->hMonitorSourceModeSet, pMonitorSourceMode));
        }
        else
        {
            Status = STATUS_SUCCESS;
        }

        // If adding the mode failed, release the mode, if this doesn't work there is nothing that can be done, some memory will get leaked
        NTSTATUS TempStatus = pRecommendMonitorModes->pMonitorSourceModeSetInterface->pfnReleaseModeInfo(pRecommendMonitorModes->hMonitorSourceModeSet, pMonitorSourceMode);
        UNREFERENCED_PARAMETER(TempStatus);
        NT_ASSERT(NT_SUCCESS(TempStatus));
        return Status;
    }
    // If AddMode succeeded with something other than STATUS_SUCCESS treat it as such anyway when propagating up
    for (UINT Idx = 0; Idx < m_ModeCount; ++Idx)
    {
        // There is only one source format supported by display-only drivers, but more can be added in a 
        // full WDDM driver if the hardware supports them

        pVbeModeInfo = &m_ModeInfo[Idx];
        // TODO: add routine for filling Monitor modepMonitorSourceMode = NULL;
        Status = pRecommendMonitorModes->pMonitorSourceModeSetInterface->pfnCreateNewModeInfo(pRecommendMonitorModes->hMonitorSourceModeSet, &pMonitorSourceMode);
        if (!NT_SUCCESS(Status))
        {
            // If failed to create a new mode info, mode doesn't need to be released since it was never created
            DbgPrint(TRACE_LEVEL_ERROR, ("pfnCreateNewModeInfo failed with Status = 0x%I64x, hMonitorSourceModeSet = 0x%I64x", Status, pRecommendMonitorModes->hMonitorSourceModeSet));
            return Status;
        }

        
        DbgPrint(TRACE_LEVEL_INFORMATION, ("%s: add pref mode, dimensions %ux%u, taken from DxgkCbAcquirePostDisplayOwnership at StartDevice\n", __FUNCTION__,
                   pVbeModeInfo->XResolution, pVbeModeInfo->YResolution));

        // Since we don't know the real monitor timing information, just use the current display mode (from the POST device) with unknown frequencies
        pMonitorSourceMode->VideoSignalInfo.VideoStandard = D3DKMDT_VSS_OTHER;
        pMonitorSourceMode->VideoSignalInfo.TotalSize.cx = pVbeModeInfo->XResolution;
        pMonitorSourceMode->VideoSignalInfo.TotalSize.cy = pVbeModeInfo->YResolution;
        pMonitorSourceMode->VideoSignalInfo.ActiveSize = pMonitorSourceMode->VideoSignalInfo.TotalSize;
        pMonitorSourceMode->VideoSignalInfo.VSyncFreq.Numerator = D3DKMDT_FREQUENCY_NOTSPECIFIED;
        pMonitorSourceMode->VideoSignalInfo.VSyncFreq.Denominator = D3DKMDT_FREQUENCY_NOTSPECIFIED;
        pMonitorSourceMode->VideoSignalInfo.HSyncFreq.Numerator = D3DKMDT_FREQUENCY_NOTSPECIFIED;
        pMonitorSourceMode->VideoSignalInfo.HSyncFreq.Denominator = D3DKMDT_FREQUENCY_NOTSPECIFIED;
        pMonitorSourceMode->VideoSignalInfo.PixelRate = D3DKMDT_FREQUENCY_NOTSPECIFIED;
        pMonitorSourceMode->VideoSignalInfo.ScanLineOrdering = D3DDDI_VSSLO_PROGRESSIVE; //???

        pMonitorSourceMode->Origin = D3DKMDT_MCO_DRIVER; // ????
        pMonitorSourceMode->Preference = D3DKMDT_MP_NOTPREFERRED; // TODO...
        pMonitorSourceMode->ColorBasis = D3DKMDT_CB_SRGB; // ????
        pMonitorSourceMode->ColorCoeffDynamicRanges.FirstChannel = 8;
        pMonitorSourceMode->ColorCoeffDynamicRanges.SecondChannel = 8;
        pMonitorSourceMode->ColorCoeffDynamicRanges.ThirdChannel = 8;
        pMonitorSourceMode->ColorCoeffDynamicRanges.FourthChannel = 8;

        Status = pRecommendMonitorModes->pMonitorSourceModeSetInterface->pfnAddMode(pRecommendMonitorModes->hMonitorSourceModeSet, pMonitorSourceMode);
        if (!NT_SUCCESS(Status))
        {
            if (Status != STATUS_GRAPHICS_MODE_ALREADY_IN_MODESET)
            {
                DbgPrint(TRACE_LEVEL_ERROR, ("pfnAddMode failed with Status = 0x%I64x, hMonitorSourceModeSet = 0x%I64x, pMonitorSourceMode = 0x%I64x",
                                Status, pRecommendMonitorModes->hMonitorSourceModeSet, pMonitorSourceMode));
            }
        
            // If adding the mode failed, release the mode, if this doesn't work there is nothing that can be done, some memory will get leaked
            Status = pRecommendMonitorModes->pMonitorSourceModeSetInterface->pfnReleaseModeInfo(pRecommendMonitorModes->hMonitorSourceModeSet, pMonitorSourceMode);
            NT_ASSERT(NT_SUCCESS(Status));
        }
    }

    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
    return Status;
}

// Tell DMM about all the modes, etc. that are supported
NTSTATUS QxlDod::EnumVidPnCofuncModality(_In_ CONST DXGKARG_ENUMVIDPNCOFUNCMODALITY* CONST pEnumCofuncModality)
{
    PAGED_CODE();

    QXL_ASSERT(pEnumCofuncModality != NULL);
    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));

    D3DKMDT_HVIDPNTOPOLOGY                   hVidPnTopology = 0;
    D3DKMDT_HVIDPNSOURCEMODESET              hVidPnSourceModeSet = 0;
    D3DKMDT_HVIDPNTARGETMODESET              hVidPnTargetModeSet = 0;
    CONST DXGK_VIDPN_INTERFACE*              pVidPnInterface = NULL;
    CONST DXGK_VIDPNTOPOLOGY_INTERFACE*      pVidPnTopologyInterface = NULL;
    CONST DXGK_VIDPNSOURCEMODESET_INTERFACE* pVidPnSourceModeSetInterface = NULL;
    CONST DXGK_VIDPNTARGETMODESET_INTERFACE* pVidPnTargetModeSetInterface = NULL;
    CONST D3DKMDT_VIDPN_PRESENT_PATH*        pVidPnPresentPath = NULL;
    CONST D3DKMDT_VIDPN_PRESENT_PATH*        pVidPnPresentPathTemp = NULL; // Used for AcquireNextPathInfo
    CONST D3DKMDT_VIDPN_SOURCE_MODE*         pVidPnPinnedSourceModeInfo = NULL;
    CONST D3DKMDT_VIDPN_TARGET_MODE*         pVidPnPinnedTargetModeInfo = NULL;

    // Get the VidPn Interface so we can get the 'Source Mode Set', 'Target Mode Set' and 'VidPn Topology' interfaces
    NTSTATUS Status = m_DxgkInterface.DxgkCbQueryVidPnInterface(pEnumCofuncModality->hConstrainingVidPn, DXGK_VIDPN_INTERFACE_VERSION_V1, &pVidPnInterface);
    if (!NT_SUCCESS(Status))
    {
        DbgPrint(TRACE_LEVEL_ERROR, ("DxgkCbQueryVidPnInterface failed with Status = 0x%X, hFunctionalVidPn = 0x%I64x", Status, pEnumCofuncModality->hConstrainingVidPn));
        return Status;
    }

    // Get the VidPn Topology interface so we can enumerate all paths
    Status = pVidPnInterface->pfnGetTopology(pEnumCofuncModality->hConstrainingVidPn, &hVidPnTopology, &pVidPnTopologyInterface);
    if (!NT_SUCCESS(Status))
    {
        DbgPrint(TRACE_LEVEL_ERROR, ("pfnGetTopology failed with Status = 0x%X, hFunctionalVidPn = 0x%I64x", Status, pEnumCofuncModality->hConstrainingVidPn));
        return Status;
    }

    // Get the first path before we start looping through them
    Status = pVidPnTopologyInterface->pfnAcquireFirstPathInfo(hVidPnTopology, &pVidPnPresentPath);
    if (!NT_SUCCESS(Status))
    {
        DbgPrint(TRACE_LEVEL_ERROR, ("pfnAcquireFirstPathInfo failed with Status =0x%X, hVidPnTopology = 0x%I64x", Status, hVidPnTopology));
        return Status;
    }

    // Loop through all available paths.
    while (Status != STATUS_GRAPHICS_NO_MORE_ELEMENTS_IN_DATASET)
    {
        // Get the Source Mode Set interface so the pinned mode can be retrieved
        Status = pVidPnInterface->pfnAcquireSourceModeSet(pEnumCofuncModality->hConstrainingVidPn,
                                                          pVidPnPresentPath->VidPnSourceId,
                                                          &hVidPnSourceModeSet,
                                                          &pVidPnSourceModeSetInterface);
        if (!NT_SUCCESS(Status))
        {
            DbgPrint(TRACE_LEVEL_ERROR, ("pfnAcquireSourceModeSet failed with Status = 0x%X, hConstrainingVidPn = 0x%I64x, SourceId = 0x%I64x",
                           Status, pEnumCofuncModality->hConstrainingVidPn, pVidPnPresentPath->VidPnSourceId));
            break;
        }

        // Get the pinned mode, needed when VidPnSource isn't pivot, and when VidPnTarget isn't pivot
        Status = pVidPnSourceModeSetInterface->pfnAcquirePinnedModeInfo(hVidPnSourceModeSet, &pVidPnPinnedSourceModeInfo);
        if (!NT_SUCCESS(Status))
        {
            DbgPrint(TRACE_LEVEL_ERROR, ("pfnAcquirePinnedModeInfo failed with Status = 0x%X, hVidPnSourceModeSet = 0x%I64x", Status, hVidPnSourceModeSet));
            break;
        }

        // SOURCE MODES: If this source mode isn't the pivot point, do work on the source mode set
        if (!((pEnumCofuncModality->EnumPivotType == D3DKMDT_EPT_VIDPNSOURCE) &&
              (pEnumCofuncModality->EnumPivot.VidPnSourceId == pVidPnPresentPath->VidPnSourceId)))
        {
            // If there's no pinned source add possible modes (otherwise they've already been added)
            if (pVidPnPinnedSourceModeInfo == NULL)
            {
                // Release the acquired source mode set, since going to create a new one to put all modes in
                Status = pVidPnInterface->pfnReleaseSourceModeSet(pEnumCofuncModality->hConstrainingVidPn, hVidPnSourceModeSet);
                if (!NT_SUCCESS(Status))
                {
                    DbgPrint(TRACE_LEVEL_ERROR, ("pfnReleaseSourceModeSet failed with Status = 0x%X, hConstrainingVidPn = 0x%I64x, hVidPnSourceModeSet = 0x%I64x",
                                   Status, pEnumCofuncModality->hConstrainingVidPn, hVidPnSourceModeSet));
                    break;
                }
                hVidPnSourceModeSet = 0; // Successfully released it

                // Create a new source mode set which will be added to the constraining VidPn with all the possible modes
                Status = pVidPnInterface->pfnCreateNewSourceModeSet(pEnumCofuncModality->hConstrainingVidPn,
                                                                    pVidPnPresentPath->VidPnSourceId,
                                                                    &hVidPnSourceModeSet,
                                                                    &pVidPnSourceModeSetInterface);
                if (!NT_SUCCESS(Status))
                {
                    DbgPrint(TRACE_LEVEL_ERROR, ("pfnCreateNewSourceModeSet failed with Status = 0x%X, hConstrainingVidPn = 0x%I64x, SourceId = 0x%I64x",
                                   Status, pEnumCofuncModality->hConstrainingVidPn, pVidPnPresentPath->VidPnSourceId));
                    break;
                }

                // Add the appropriate modes to the source mode set
                {
                    Status = AddSingleSourceMode(pVidPnSourceModeSetInterface, hVidPnSourceModeSet, pVidPnPresentPath->VidPnSourceId);
                }

                if (!NT_SUCCESS(Status))
                {
                    DbgPrint(TRACE_LEVEL_ERROR, ("AddSingleSourceMode failed with Status = 0x%X, hFunctionalVidPn = 0x%I64x", Status, pEnumCofuncModality->hConstrainingVidPn));
                    break;
                }

                // Give DMM back the source modes just populated
                Status = pVidPnInterface->pfnAssignSourceModeSet(pEnumCofuncModality->hConstrainingVidPn, pVidPnPresentPath->VidPnSourceId, hVidPnSourceModeSet);
                if (!NT_SUCCESS(Status))
                {
                    DbgPrint(TRACE_LEVEL_ERROR, ("pfnAssignSourceModeSet failed with Status = 0x%X, hConstrainingVidPn = 0x%I64x, SourceId = 0x%I64x, hVidPnSourceModeSet = 0x%I64x",
                                   Status, pEnumCofuncModality->hConstrainingVidPn, pVidPnPresentPath->VidPnSourceId, hVidPnSourceModeSet));
                    break;
                }
                hVidPnSourceModeSet = 0; // Successfully assigned it (equivalent to releasing it)
            }
        }// End: SOURCE MODES

        // TARGET MODES: If this target mode isn't the pivot point, do work on the target mode set
        if (!((pEnumCofuncModality->EnumPivotType == D3DKMDT_EPT_VIDPNTARGET) &&
              (pEnumCofuncModality->EnumPivot.VidPnTargetId == pVidPnPresentPath->VidPnTargetId)))
        {
            // Get the Target Mode Set interface so modes can be added if necessary
            Status = pVidPnInterface->pfnAcquireTargetModeSet(pEnumCofuncModality->hConstrainingVidPn,
                                                              pVidPnPresentPath->VidPnTargetId,
                                                              &hVidPnTargetModeSet,
                                                              &pVidPnTargetModeSetInterface);
            if (!NT_SUCCESS(Status))
            {
                DbgPrint(TRACE_LEVEL_ERROR, ("pfnAcquireTargetModeSet failed with Status = 0x%X, hConstrainingVidPn = 0x%I64x, TargetId = 0x%I64x",
                               Status, pEnumCofuncModality->hConstrainingVidPn, pVidPnPresentPath->VidPnTargetId));
                break;
            }

            Status = pVidPnTargetModeSetInterface->pfnAcquirePinnedModeInfo(hVidPnTargetModeSet, &pVidPnPinnedTargetModeInfo);
            if (!NT_SUCCESS(Status))
            {
                DbgPrint(TRACE_LEVEL_ERROR, ("pfnAcquirePinnedModeInfo failed with Status = 0x%X, hVidPnTargetModeSet = 0x%I64x", Status, hVidPnTargetModeSet));
                break;
            }

            // If there's no pinned target add possible modes (otherwise they've already been added)
            if (pVidPnPinnedTargetModeInfo == NULL)
            {
                // Release the acquired target mode set, since going to create a new one to put all modes in
                Status = pVidPnInterface->pfnReleaseTargetModeSet(pEnumCofuncModality->hConstrainingVidPn, hVidPnTargetModeSet);
                if (!NT_SUCCESS(Status))
                {
                    DbgPrint(TRACE_LEVEL_ERROR, ("pfnReleaseTargetModeSet failed with Status = 0x%X, hConstrainingVidPn = 0x%I64x, hVidPnTargetModeSet = 0x%I64x",
                                       Status, pEnumCofuncModality->hConstrainingVidPn, hVidPnTargetModeSet));
                    break;
                }
                hVidPnTargetModeSet = 0; // Successfully released it

                // Create a new target mode set which will be added to the constraining VidPn with all the possible modes
                Status = pVidPnInterface->pfnCreateNewTargetModeSet(pEnumCofuncModality->hConstrainingVidPn,
                                                                    pVidPnPresentPath->VidPnTargetId,
                                                                    &hVidPnTargetModeSet,
                                                                    &pVidPnTargetModeSetInterface);
                if (!NT_SUCCESS(Status))
                {
                    DbgPrint(TRACE_LEVEL_ERROR, ("pfnCreateNewTargetModeSet failed with Status = 0x%X, hConstrainingVidPn = 0x%I64x, TargetId = 0x%I64x",
                                   Status, pEnumCofuncModality->hConstrainingVidPn, pVidPnPresentPath->VidPnTargetId));
                    break;
                }

                Status = AddSingleTargetMode(pVidPnTargetModeSetInterface, hVidPnTargetModeSet, pVidPnPinnedSourceModeInfo, pVidPnPresentPath->VidPnSourceId);

                if (!NT_SUCCESS(Status))
                {
                    DbgPrint(TRACE_LEVEL_ERROR, ("AddSingleTargetMode failed with Status = 0x%X, hFunctionalVidPn = 0x%I64x", Status, pEnumCofuncModality->hConstrainingVidPn));
                    break;
                }

                // Give DMM back the source modes just populated
                Status = pVidPnInterface->pfnAssignTargetModeSet(pEnumCofuncModality->hConstrainingVidPn, pVidPnPresentPath->VidPnTargetId, hVidPnTargetModeSet);
                if (!NT_SUCCESS(Status))
                {
                    DbgPrint(TRACE_LEVEL_ERROR, ("pfnAssignTargetModeSet failed with Status = 0x%X, hConstrainingVidPn = 0x%I64x, TargetId = 0x%I64x, hVidPnTargetModeSet = 0x%I64x",
                                   Status, pEnumCofuncModality->hConstrainingVidPn, pVidPnPresentPath->VidPnTargetId, hVidPnTargetModeSet));
                    break;
                }
                hVidPnTargetModeSet = 0; // Successfully assigned it (equivalent to releasing it)
            }
            else
            {
                // Release the pinned target as there's no other work to do
                Status = pVidPnTargetModeSetInterface->pfnReleaseModeInfo(hVidPnTargetModeSet, pVidPnPinnedTargetModeInfo);
                if (!NT_SUCCESS(Status))
                {
                    DbgPrint(TRACE_LEVEL_ERROR, ("pfnReleaseModeInfo failed with Status = 0x%X, hVidPnTargetModeSet = 0x%I64x, pVidPnPinnedTargetModeInfo = 0x%I64x",
                                        Status, hVidPnTargetModeSet, pVidPnPinnedTargetModeInfo));
                    break;
                }
                pVidPnPinnedTargetModeInfo = NULL; // Successfully released it

                // Release the acquired target mode set, since it is no longer needed
                Status = pVidPnInterface->pfnReleaseTargetModeSet(pEnumCofuncModality->hConstrainingVidPn, hVidPnTargetModeSet);
                if (!NT_SUCCESS(Status))
                {
                    DbgPrint(TRACE_LEVEL_ERROR, ("pfnReleaseTargetModeSet failed with Status = 0x%X, hConstrainingVidPn = 0x%I64x, hVidPnTargetModeSet = 0x%I64x",
                                       Status, pEnumCofuncModality->hConstrainingVidPn, hVidPnTargetModeSet));
                    break;
                }
                hVidPnTargetModeSet = 0; // Successfully released it
            }
        }// End: TARGET MODES

        // Nothing else needs the pinned source mode so release it
        if (pVidPnPinnedSourceModeInfo != NULL)
        {
            Status = pVidPnSourceModeSetInterface->pfnReleaseModeInfo(hVidPnSourceModeSet, pVidPnPinnedSourceModeInfo);
            if (!NT_SUCCESS(Status))
            {
                DbgPrint(TRACE_LEVEL_ERROR, ("pfnReleaseModeInfo failed with Status = 0x%X, hVidPnSourceModeSet = 0x%I64x, pVidPnPinnedSourceModeInfo = 0x%I64x",
                                    Status, hVidPnSourceModeSet, pVidPnPinnedSourceModeInfo));
                break;
            }
            pVidPnPinnedSourceModeInfo = NULL; // Successfully released it
        }

        // With the pinned source mode now released, if the source mode set hasn't been released, release that as well
        if (hVidPnSourceModeSet != 0)
        {
            Status = pVidPnInterface->pfnReleaseSourceModeSet(pEnumCofuncModality->hConstrainingVidPn, hVidPnSourceModeSet);
            if (!NT_SUCCESS(Status))
            {
                DbgPrint(TRACE_LEVEL_ERROR, ("pfnReleaseSourceModeSet failed with Status = 0x%X, hConstrainingVidPn = 0x%I64x, hVidPnSourceModeSet = 0x%I64x",
                               Status, pEnumCofuncModality->hConstrainingVidPn, hVidPnSourceModeSet));
                break;
            }
            hVidPnSourceModeSet = 0; // Successfully released it
        }

        // If modifying support fields, need to modify a local version of a path structure since the retrieved one is const
        D3DKMDT_VIDPN_PRESENT_PATH LocalVidPnPresentPath = *pVidPnPresentPath;
        BOOLEAN SupportFieldsModified = FALSE;

        // SCALING: If this path's scaling isn't the pivot point, do work on the scaling support
        if (!((pEnumCofuncModality->EnumPivotType == D3DKMDT_EPT_SCALING) &&
              (pEnumCofuncModality->EnumPivot.VidPnSourceId == pVidPnPresentPath->VidPnSourceId) &&
              (pEnumCofuncModality->EnumPivot.VidPnTargetId == pVidPnPresentPath->VidPnTargetId)))
        {
            // If the scaling is unpinned, then modify the scaling support field
            if (pVidPnPresentPath->ContentTransformation.Scaling == D3DKMDT_VPPS_UNPINNED)
            {
                // Identity and centered scaling are supported, but not any stretch modes
                RtlZeroMemory(&(LocalVidPnPresentPath.ContentTransformation.ScalingSupport), sizeof(D3DKMDT_VIDPN_PRESENT_PATH_SCALING_SUPPORT));
                LocalVidPnPresentPath.ContentTransformation.ScalingSupport.Identity = 1;
                LocalVidPnPresentPath.ContentTransformation.ScalingSupport.Centered = 1;
                SupportFieldsModified = TRUE;
            }
        } // End: SCALING

        // ROTATION: If this path's rotation isn't the pivot point, do work on the rotation support
        if (!((pEnumCofuncModality->EnumPivotType != D3DKMDT_EPT_ROTATION) &&
              (pEnumCofuncModality->EnumPivot.VidPnSourceId == pVidPnPresentPath->VidPnSourceId) &&
              (pEnumCofuncModality->EnumPivot.VidPnTargetId == pVidPnPresentPath->VidPnTargetId)))
        {
            // If the rotation is unpinned, then modify the rotation support field
            if (pVidPnPresentPath->ContentTransformation.Rotation == D3DKMDT_VPPR_UNPINNED)
            {
                LocalVidPnPresentPath.ContentTransformation.RotationSupport.Identity = 1;
                // Sample supports only Rotate90
                LocalVidPnPresentPath.ContentTransformation.RotationSupport.Rotate90 = 1;
                LocalVidPnPresentPath.ContentTransformation.RotationSupport.Rotate180 = 0;
                LocalVidPnPresentPath.ContentTransformation.RotationSupport.Rotate270 = 0;
                SupportFieldsModified = TRUE;
            }
        } // End: ROTATION

        if (SupportFieldsModified)
        {
            // The correct path will be found by this function and the appropriate fields updated
            Status = pVidPnTopologyInterface->pfnUpdatePathSupportInfo(hVidPnTopology, &LocalVidPnPresentPath);
            if (!NT_SUCCESS(Status))
            {
                DbgPrint(TRACE_LEVEL_ERROR, ("pfnUpdatePathSupportInfo failed with Status = 0x%X, hVidPnTopology = 0x%I64x", Status, hVidPnTopology));
                break;
            }
        }

        // Get the next path...
        // (NOTE: This is the value of Status that will return STATUS_GRAPHICS_NO_MORE_ELEMENTS_IN_DATASET when it's time to quit the loop)
        pVidPnPresentPathTemp = pVidPnPresentPath;
        Status = pVidPnTopologyInterface->pfnAcquireNextPathInfo(hVidPnTopology, pVidPnPresentPathTemp, &pVidPnPresentPath);
        if (!NT_SUCCESS(Status))
        {
            DbgPrint(TRACE_LEVEL_ERROR, ("pfnAcquireNextPathInfo failed with Status = 0x%X, hVidPnTopology = 0x%I64x, pVidPnPresentPathTemp = 0x%I64x", Status, hVidPnTopology, pVidPnPresentPathTemp));
            break;
        }

        // ...and release the last path
        NTSTATUS TempStatus = pVidPnTopologyInterface->pfnReleasePathInfo(hVidPnTopology, pVidPnPresentPathTemp);
        if (!NT_SUCCESS(TempStatus))
        {
            DbgPrint(TRACE_LEVEL_ERROR, ("pfnReleasePathInfo failed with Status = 0x%X, hVidPnTopology = 0x%I64x, pVidPnPresentPathTemp = 0x%I64x", TempStatus, hVidPnTopology, pVidPnPresentPathTemp));
            Status = TempStatus;
            break;
        }
        pVidPnPresentPathTemp = NULL; // Successfully released it
    }// End: while loop for paths in topology

    // If quit the while loop normally, set the return value to success
    if (Status == STATUS_GRAPHICS_NO_MORE_ELEMENTS_IN_DATASET)
    {
        Status = STATUS_SUCCESS;
    }

    // Release any resources hanging around because the loop was quit early.
    // Since in normal execution everything should be released by this point, TempStatus is initialized to a bogus error to be used as an
    //  assertion that if anything had to be released now (TempStatus changing) Status isn't successful.
    NTSTATUS TempStatus = STATUS_NOT_FOUND;

    if ((pVidPnSourceModeSetInterface != NULL) &&
        (pVidPnPinnedSourceModeInfo != NULL))
    {
        TempStatus = pVidPnSourceModeSetInterface->pfnReleaseModeInfo(hVidPnSourceModeSet, pVidPnPinnedSourceModeInfo);
        QXL_ASSERT_CHK(NT_SUCCESS(TempStatus));
    }

    if ((pVidPnTargetModeSetInterface != NULL) &&
        (pVidPnPinnedTargetModeInfo != NULL))
    {
        TempStatus = pVidPnTargetModeSetInterface->pfnReleaseModeInfo(hVidPnTargetModeSet, pVidPnPinnedTargetModeInfo);
        QXL_ASSERT_CHK(NT_SUCCESS(TempStatus));
    }

    if (pVidPnPresentPath != NULL)
    {
        TempStatus = pVidPnTopologyInterface->pfnReleasePathInfo(hVidPnTopology, pVidPnPresentPath);
        QXL_ASSERT_CHK(NT_SUCCESS(TempStatus));
    }

    if (pVidPnPresentPathTemp != NULL)
    {
        TempStatus = pVidPnTopologyInterface->pfnReleasePathInfo(hVidPnTopology, pVidPnPresentPathTemp);
        QXL_ASSERT_CHK(NT_SUCCESS(TempStatus));
    }

    if (hVidPnSourceModeSet != 0)
    {
        TempStatus = pVidPnInterface->pfnReleaseSourceModeSet(pEnumCofuncModality->hConstrainingVidPn, hVidPnSourceModeSet);
        QXL_ASSERT_CHK(NT_SUCCESS(TempStatus));
    }

    if (hVidPnTargetModeSet != 0)
    {
        TempStatus = pVidPnInterface->pfnReleaseTargetModeSet(pEnumCofuncModality->hConstrainingVidPn, hVidPnTargetModeSet);
        QXL_ASSERT_CHK(NT_SUCCESS(TempStatus));
    }

    QXL_ASSERT_CHK(TempStatus == STATUS_NOT_FOUND || Status != STATUS_SUCCESS);

    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
    return Status;
}

NTSTATUS QxlDod::SetVidPnSourceVisibility(_In_ CONST DXGKARG_SETVIDPNSOURCEVISIBILITY* pSetVidPnSourceVisibility)
{
    PAGED_CODE();
//    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));
    QXL_ASSERT(pSetVidPnSourceVisibility != NULL);
    QXL_ASSERT((pSetVidPnSourceVisibility->VidPnSourceId < MAX_VIEWS) ||
               (pSetVidPnSourceVisibility->VidPnSourceId == D3DDDI_ID_ALL));

    UINT StartVidPnSourceId = (pSetVidPnSourceVisibility->VidPnSourceId == D3DDDI_ID_ALL) ? 0 : pSetVidPnSourceVisibility->VidPnSourceId;
    UINT MaxVidPnSourceId = (pSetVidPnSourceVisibility->VidPnSourceId == D3DDDI_ID_ALL) ? MAX_VIEWS : pSetVidPnSourceVisibility->VidPnSourceId + 1;

    for (UINT SourceId = StartVidPnSourceId; SourceId < MaxVidPnSourceId; ++SourceId)
    {
        if (pSetVidPnSourceVisibility->Visible)
        {
            m_CurrentModes[SourceId].Flags.FullscreenPresent = TRUE;
        }
        else
        {
            BlackOutScreen(SourceId);
        }

        // Store current visibility so it can be dealt with during Present call
        m_CurrentModes[SourceId].Flags.SourceNotVisible = !(pSetVidPnSourceVisibility->Visible);
    }

//    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
    return STATUS_SUCCESS;
}

// NOTE: The value of pCommitVidPn->MonitorConnectivityChecks is ignored, since BDD is unable to recognize whether a monitor is connected or not
// The value of pCommitVidPn->hPrimaryAllocation is also ignored, since BDD is a display only driver and does not deal with allocations
NTSTATUS QxlDod::CommitVidPn(_In_ CONST DXGKARG_COMMITVIDPN* CONST pCommitVidPn)
{
    PAGED_CODE();
    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));
    QXL_ASSERT(pCommitVidPn != NULL);
    QXL_ASSERT(pCommitVidPn->AffectedVidPnSourceId < MAX_VIEWS);

    NTSTATUS                                 Status;
    SIZE_T                                   NumPaths = 0;
    D3DKMDT_HVIDPNTOPOLOGY                   hVidPnTopology = 0;
    D3DKMDT_HVIDPNSOURCEMODESET              hVidPnSourceModeSet = 0;
    CONST DXGK_VIDPN_INTERFACE*              pVidPnInterface = NULL;
    CONST DXGK_VIDPNTOPOLOGY_INTERFACE*      pVidPnTopologyInterface = NULL;
    CONST DXGK_VIDPNSOURCEMODESET_INTERFACE* pVidPnSourceModeSetInterface = NULL;
    CONST D3DKMDT_VIDPN_PRESENT_PATH*        pVidPnPresentPath = NULL;
    CONST D3DKMDT_VIDPN_SOURCE_MODE*         pPinnedVidPnSourceModeInfo = NULL;

    // Check this CommitVidPn is for the mode change notification when monitor is in power off state.
    if (pCommitVidPn->Flags.PathPoweredOff)
    {
        // Ignore the commitVidPn call for the mode change notification when monitor is in power off state.
        Status = STATUS_SUCCESS;
        goto CommitVidPnExit;
    }

    // Get the VidPn Interface so we can get the 'Source Mode Set' and 'VidPn Topology' interfaces
    Status = m_DxgkInterface.DxgkCbQueryVidPnInterface(pCommitVidPn->hFunctionalVidPn, DXGK_VIDPN_INTERFACE_VERSION_V1, &pVidPnInterface);
    if (!NT_SUCCESS(Status))
    {
        DbgPrint(TRACE_LEVEL_ERROR, ("DxgkCbQueryVidPnInterface failed with Status = 0x%X, hFunctionalVidPn = 0x%I64x", Status, pCommitVidPn->hFunctionalVidPn));
        goto CommitVidPnExit;
    }

    // Get the VidPn Topology interface so can enumerate paths from source
    Status = pVidPnInterface->pfnGetTopology(pCommitVidPn->hFunctionalVidPn, &hVidPnTopology, &pVidPnTopologyInterface);
    if (!NT_SUCCESS(Status))
    {
        DbgPrint(TRACE_LEVEL_ERROR, ("pfnGetTopology failed with Status = 0x%X, hFunctionalVidPn = 0x%I64x", Status, pCommitVidPn->hFunctionalVidPn));
        goto CommitVidPnExit;
    }

    // Find out the number of paths now, if it's 0 don't bother with source mode set and pinned mode, just clear current and then quit
    Status = pVidPnTopologyInterface->pfnGetNumPaths(hVidPnTopology, &NumPaths);
    if (!NT_SUCCESS(Status))
    {
        DbgPrint(TRACE_LEVEL_ERROR, ("pfnGetNumPaths failed with Status = 0x%X, hVidPnTopology = 0x%I64x", Status, hVidPnTopology));
        goto CommitVidPnExit;
    }

    if (NumPaths != 0)
    {
        // Get the Source Mode Set interface so we can get the pinned mode
        Status = pVidPnInterface->pfnAcquireSourceModeSet(pCommitVidPn->hFunctionalVidPn,
                                                          pCommitVidPn->AffectedVidPnSourceId,
                                                          &hVidPnSourceModeSet,
                                                          &pVidPnSourceModeSetInterface);
        if (!NT_SUCCESS(Status))
        {
            DbgPrint(TRACE_LEVEL_ERROR, ("pfnAcquireSourceModeSet failed with Status = 0x%X, hFunctionalVidPn = 0x%I64x, SourceId = 0x%I64x", Status, pCommitVidPn->hFunctionalVidPn, pCommitVidPn->AffectedVidPnSourceId));
            goto CommitVidPnExit;
        }

        // Get the mode that is being pinned
        Status = pVidPnSourceModeSetInterface->pfnAcquirePinnedModeInfo(hVidPnSourceModeSet, &pPinnedVidPnSourceModeInfo);
        if (!NT_SUCCESS(Status))
        {
            DbgPrint(TRACE_LEVEL_ERROR, ("pfnAcquirePinnedModeInfo failed with Status = 0x%X, hFunctionalVidPn = 0x%I64x", Status, pCommitVidPn->hFunctionalVidPn));
            goto CommitVidPnExit;
        }
    }
    else
    {
        // This will cause the successful quit below
        pPinnedVidPnSourceModeInfo = NULL;
    }

    if (m_CurrentModes[pCommitVidPn->AffectedVidPnSourceId].FrameBuffer.Ptr &&
        !m_CurrentModes[pCommitVidPn->AffectedVidPnSourceId].Flags.DoNotMapOrUnmap)
    {
        Status = UnmapFrameBuffer(m_CurrentModes[pCommitVidPn->AffectedVidPnSourceId].FrameBuffer.Ptr,
                                  m_CurrentModes[pCommitVidPn->AffectedVidPnSourceId].DispInfo.Pitch * m_CurrentModes[pCommitVidPn->AffectedVidPnSourceId].DispInfo.Height);
        m_CurrentModes[pCommitVidPn->AffectedVidPnSourceId].FrameBuffer.Ptr = NULL;
        m_CurrentModes[pCommitVidPn->AffectedVidPnSourceId].Flags.FrameBufferIsActive = FALSE;

        if (!NT_SUCCESS(Status))
        {
            goto CommitVidPnExit;
        }
    }

    if (pPinnedVidPnSourceModeInfo == NULL)
    {
        // There is no mode to pin on this source, any old paths here have already been cleared
        Status = STATUS_SUCCESS;
        goto CommitVidPnExit;
    }

    Status = IsVidPnSourceModeFieldsValid(pPinnedVidPnSourceModeInfo);
    if (!NT_SUCCESS(Status))
    {
        goto CommitVidPnExit;
    }

    // Get the number of paths from this source so we can loop through all paths
    SIZE_T NumPathsFromSource = 0;
    Status = pVidPnTopologyInterface->pfnGetNumPathsFromSource(hVidPnTopology, pCommitVidPn->AffectedVidPnSourceId, &NumPathsFromSource);
    if (!NT_SUCCESS(Status))
    {
        DbgPrint(TRACE_LEVEL_ERROR, ("pfnGetNumPathsFromSource failed with Status = 0x%X, hVidPnTopology = 0x%I64x", Status, hVidPnTopology));
        goto CommitVidPnExit;
    }

    // Loop through all paths to set this mode
    for (SIZE_T PathIndex = 0; PathIndex < NumPathsFromSource; ++PathIndex)
    {
        // Get the target id for this path
        D3DDDI_VIDEO_PRESENT_TARGET_ID TargetId = D3DDDI_ID_UNINITIALIZED;
        Status = pVidPnTopologyInterface->pfnEnumPathTargetsFromSource(hVidPnTopology, pCommitVidPn->AffectedVidPnSourceId, PathIndex, &TargetId);
        if (!NT_SUCCESS(Status))
        {
            DbgPrint(TRACE_LEVEL_ERROR, ("pfnEnumPathTargetsFromSource failed with Status = 0x%X, hVidPnTopology = 0x%I64x, SourceId = 0x%I64x, PathIndex = 0x%I64x",
                            Status, hVidPnTopology, pCommitVidPn->AffectedVidPnSourceId, PathIndex));
            goto CommitVidPnExit;
        }

        // Get the actual path info
        Status = pVidPnTopologyInterface->pfnAcquirePathInfo(hVidPnTopology, pCommitVidPn->AffectedVidPnSourceId, TargetId, &pVidPnPresentPath);
        if (!NT_SUCCESS(Status))
        {
            DbgPrint(TRACE_LEVEL_ERROR, ("pfnAcquirePathInfo failed with Status = 0x%X, hVidPnTopology = 0x%I64x, SourceId = 0x%I64x, TargetId = 0x%I64x",
                            Status, hVidPnTopology, pCommitVidPn->AffectedVidPnSourceId, TargetId));
            goto CommitVidPnExit;
        }

        Status = IsVidPnPathFieldsValid(pVidPnPresentPath);
        if (!NT_SUCCESS(Status))
        {
            goto CommitVidPnExit;
        }

        Status = SetSourceModeAndPath(pPinnedVidPnSourceModeInfo, pVidPnPresentPath);
        if (!NT_SUCCESS(Status))
        {
            goto CommitVidPnExit;
        }

        Status = pVidPnTopologyInterface->pfnReleasePathInfo(hVidPnTopology, pVidPnPresentPath);
        if (!NT_SUCCESS(Status))
        {
            DbgPrint(TRACE_LEVEL_ERROR, ("pfnReleasePathInfo failed with Status = 0x%X, hVidPnTopoogy = 0x%I64x, pVidPnPresentPath = 0x%I64x",
                            Status, hVidPnTopology, pVidPnPresentPath));
            goto CommitVidPnExit;
        }
        pVidPnPresentPath = NULL; // Successfully released it
    }

CommitVidPnExit:

    NTSTATUS TempStatus;
    UNREFERENCED_PARAMETER(TempStatus);

    if ((pVidPnSourceModeSetInterface != NULL) &&
        (hVidPnSourceModeSet != 0) &&
        (pPinnedVidPnSourceModeInfo != NULL))
    {
        TempStatus = pVidPnSourceModeSetInterface->pfnReleaseModeInfo(hVidPnSourceModeSet, pPinnedVidPnSourceModeInfo);
        NT_ASSERT(NT_SUCCESS(TempStatus));
    }

    if ((pVidPnInterface != NULL) &&
        (pCommitVidPn->hFunctionalVidPn != 0) &&
        (hVidPnSourceModeSet != 0))
    {
        TempStatus = pVidPnInterface->pfnReleaseSourceModeSet(pCommitVidPn->hFunctionalVidPn, hVidPnSourceModeSet);
        NT_ASSERT(NT_SUCCESS(TempStatus));
    }

    if ((pVidPnTopologyInterface != NULL) &&
        (hVidPnTopology != 0) &&
        (pVidPnPresentPath != NULL))
    {
        TempStatus = pVidPnTopologyInterface->pfnReleasePathInfo(hVidPnTopology, pVidPnPresentPath);
        NT_ASSERT(NT_SUCCESS(TempStatus));
    }

    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
    return Status;
}

NTSTATUS QxlDod::SetSourceModeAndPath(CONST D3DKMDT_VIDPN_SOURCE_MODE* pSourceMode,
                                                    CONST D3DKMDT_VIDPN_PRESENT_PATH* pPath)
{
    PAGED_CODE();
    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));

    NTSTATUS Status = STATUS_SUCCESS;

    CURRENT_BDD_MODE* pCurrentBddMode = &m_CurrentModes[pPath->VidPnSourceId];

    pCurrentBddMode->Scaling = pPath->ContentTransformation.Scaling;
    pCurrentBddMode->SrcModeWidth = pSourceMode->Format.Graphics.VisibleRegionSize.cx;
    pCurrentBddMode->SrcModeHeight = pSourceMode->Format.Graphics.VisibleRegionSize.cy;
    pCurrentBddMode->Rotation = pPath->ContentTransformation.Rotation;

    pCurrentBddMode->DispInfo.Width = pSourceMode->Format.Graphics.PrimSurfSize.cx;
    pCurrentBddMode->DispInfo.Height = pSourceMode->Format.Graphics.PrimSurfSize.cy;
    pCurrentBddMode->DispInfo.Pitch = pSourceMode->Format.Graphics.PrimSurfSize.cx * BPPFromPixelFormat(pCurrentBddMode->DispInfo.ColorFormat) / BITS_PER_BYTE;


    if (!pCurrentBddMode->Flags.DoNotMapOrUnmap)
    {
        // Map the new frame buffer
        QXL_ASSERT(pCurrentBddMode->FrameBuffer.Ptr == NULL);
        Status = MapFrameBuffer(pCurrentBddMode->DispInfo.PhysicAddress,
                                pCurrentBddMode->DispInfo.Pitch * pCurrentBddMode->DispInfo.Height,
                                &(pCurrentBddMode->FrameBuffer.Ptr));
    }

    if (NT_SUCCESS(Status))
    {

        pCurrentBddMode->Flags.FrameBufferIsActive = TRUE;
        BlackOutScreen(pPath->VidPnSourceId);

        // Mark that the next present should be fullscreen so the screen doesn't go from black to actual pixels one dirty rect at a time.
        pCurrentBddMode->Flags.FullscreenPresent = TRUE;
        for (USHORT ModeIndex = 0; ModeIndex < m_ModeCount; ++ModeIndex)
        {
             if (pCurrentBddMode->DispInfo.Width == m_ModeInfo[ModeIndex].XResolution &&
                 pCurrentBddMode->DispInfo.Height == m_ModeInfo[ModeIndex].YResolution )
             {
                 Status = VbeSetCurrentMode(m_ModeNumbers[ModeIndex]);
                 if (NT_SUCCESS(Status))
                 {
                     m_CurrentMode = ModeIndex;
                 }
                 break;
             }
        }
    }

    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
    return Status;
}

NTSTATUS QxlDod::IsVidPnPathFieldsValid(CONST D3DKMDT_VIDPN_PRESENT_PATH* pPath) const
{
    PAGED_CODE();
    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));

    if (pPath->VidPnSourceId >= MAX_VIEWS)
    {
        DbgPrint(TRACE_LEVEL_ERROR, ("VidPnSourceId is 0x%I64x is too high (MAX_VIEWS is 0x%I64x)",
                        pPath->VidPnSourceId, MAX_VIEWS));
        return STATUS_GRAPHICS_INVALID_VIDEO_PRESENT_SOURCE;
    }
    else if (pPath->VidPnTargetId >= MAX_CHILDREN)
    {
        DbgPrint(TRACE_LEVEL_ERROR, ("VidPnTargetId is 0x%I64x is too high (MAX_CHILDREN is 0x%I64x)",
                        pPath->VidPnTargetId, MAX_CHILDREN));
        return STATUS_GRAPHICS_INVALID_VIDEO_PRESENT_TARGET;
    }
    else if (pPath->GammaRamp.Type != D3DDDI_GAMMARAMP_DEFAULT)
    {
        DbgPrint(TRACE_LEVEL_ERROR, ("pPath contains a gamma ramp (0x%I64x)", pPath->GammaRamp.Type));
        return STATUS_GRAPHICS_GAMMA_RAMP_NOT_SUPPORTED;
    }
    else if ((pPath->ContentTransformation.Scaling != D3DKMDT_VPPS_IDENTITY) &&
             (pPath->ContentTransformation.Scaling != D3DKMDT_VPPS_CENTERED) &&
             (pPath->ContentTransformation.Scaling != D3DKMDT_VPPS_NOTSPECIFIED) &&
             (pPath->ContentTransformation.Scaling != D3DKMDT_VPPS_UNINITIALIZED))
    {
        DbgPrint(TRACE_LEVEL_ERROR, ("pPath contains a non-identity scaling (0x%I64x)", pPath->ContentTransformation.Scaling));
        return STATUS_GRAPHICS_VIDPN_MODALITY_NOT_SUPPORTED;
    }
    else if ((pPath->ContentTransformation.Rotation != D3DKMDT_VPPR_IDENTITY) &&
             (pPath->ContentTransformation.Rotation != D3DKMDT_VPPR_ROTATE90) &&
             (pPath->ContentTransformation.Rotation != D3DKMDT_VPPR_NOTSPECIFIED) &&
             (pPath->ContentTransformation.Rotation != D3DKMDT_VPPR_UNINITIALIZED))
    {
        DbgPrint(TRACE_LEVEL_ERROR, ("pPath contains a not-supported rotation (0x%I64x)", pPath->ContentTransformation.Rotation));
        return STATUS_GRAPHICS_VIDPN_MODALITY_NOT_SUPPORTED;
    }
    else if ((pPath->VidPnTargetColorBasis != D3DKMDT_CB_SCRGB) &&
             (pPath->VidPnTargetColorBasis != D3DKMDT_CB_UNINITIALIZED))
    {
        DbgPrint(TRACE_LEVEL_ERROR, ("pPath has a non-linear RGB color basis (0x%I64x)", pPath->VidPnTargetColorBasis));
        return STATUS_GRAPHICS_INVALID_VIDEO_PRESENT_SOURCE_MODE;
    }
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
    return STATUS_SUCCESS;
}


NTSTATUS QxlDod::IsVidPnSourceModeFieldsValid(CONST D3DKMDT_VIDPN_SOURCE_MODE* pSourceMode) const
{
    PAGED_CODE();
    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));
    if (pSourceMode->Type != D3DKMDT_RMT_GRAPHICS)
    {
        DbgPrint(TRACE_LEVEL_ERROR, ("pSourceMode is a non-graphics mode (0x%I64x)", pSourceMode->Type));
        return STATUS_GRAPHICS_INVALID_VIDEO_PRESENT_SOURCE_MODE;
    }
    else if ((pSourceMode->Format.Graphics.ColorBasis != D3DKMDT_CB_SCRGB) &&
             (pSourceMode->Format.Graphics.ColorBasis != D3DKMDT_CB_UNINITIALIZED))
    {
        DbgPrint(TRACE_LEVEL_ERROR, ("pSourceMode has a non-linear RGB color basis (0x%I64x)", pSourceMode->Format.Graphics.ColorBasis));
        return STATUS_GRAPHICS_INVALID_VIDEO_PRESENT_SOURCE_MODE;
    }
    else if (pSourceMode->Format.Graphics.PixelValueAccessMode != D3DKMDT_PVAM_DIRECT)
    {
        DbgPrint(TRACE_LEVEL_ERROR, ("pSourceMode has a palettized access mode (0x%I64x)", pSourceMode->Format.Graphics.PixelValueAccessMode));
        return STATUS_GRAPHICS_INVALID_VIDEO_PRESENT_SOURCE_MODE;
    }
    else
    {
        if (pSourceMode->Format.Graphics.PixelFormat == D3DDDIFMT_A8R8G8B8)
        {
            return STATUS_SUCCESS;
        }
    }
    DbgPrint(TRACE_LEVEL_ERROR, ("pSourceMode has an unknown pixel format (0x%I64x)", pSourceMode->Format.Graphics.PixelFormat));
    return STATUS_GRAPHICS_INVALID_VIDEO_PRESENT_SOURCE_MODE;
}

NTSTATUS QxlDod::UpdateActiveVidPnPresentPath(_In_ CONST DXGKARG_UPDATEACTIVEVIDPNPRESENTPATH* CONST pUpdateActiveVidPnPresentPath)
{
    PAGED_CODE();
    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));
    QXL_ASSERT(pUpdateActiveVidPnPresentPath != NULL);
    NTSTATUS Status = IsVidPnPathFieldsValid(&(pUpdateActiveVidPnPresentPath->VidPnPresentPathInfo));
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    // Mark the next present as fullscreen to make sure the full rotation comes through
    m_CurrentModes[pUpdateActiveVidPnPresentPath->VidPnPresentPathInfo.VidPnSourceId].Flags.FullscreenPresent = TRUE;

    m_CurrentModes[pUpdateActiveVidPnPresentPath->VidPnPresentPathInfo.VidPnSourceId].Rotation = pUpdateActiveVidPnPresentPath->VidPnPresentPathInfo.ContentTransformation.Rotation;

    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
    return STATUS_SUCCESS;
}



//
// Non-Paged Code
//
#pragma code_seg(push)
#pragma code_seg()

VOID QxlDod::DpcRoutine(VOID)
{
    DbgPrint(TRACE_LEVEL_INFORMATION, ("---> %s\n", __FUNCTION__));
    m_DxgkInterface.DxgkCbNotifyDpc((HANDLE)m_DxgkInterface.DeviceHandle);
    DbgPrint(TRACE_LEVEL_INFORMATION, ("<--- %s\n", __FUNCTION__));
}

BOOLEAN QxlDod::InterruptRoutine(_In_  ULONG MessageNumber)
{
    UNREFERENCED_PARAMETER(MessageNumber);

    DbgPrint(TRACE_LEVEL_INFORMATION, ("---> 0 %s\n", __FUNCTION__));
    DbgPrint(TRACE_LEVEL_INFORMATION, ("<--- %s\n", __FUNCTION__));
    return TRUE;
}

VOID QxlDod::ResetDevice(VOID)
{
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<---> %s\n", __FUNCTION__));
}

// Must be Non-Paged, as it sets up the display for a bugcheck
NTSTATUS QxlDod::SystemDisplayEnable(_In_  D3DDDI_VIDEO_PRESENT_TARGET_ID TargetId,
                                                   _In_  PDXGKARG_SYSTEM_DISPLAY_ENABLE_FLAGS Flags,
                                                   _Out_ UINT* pWidth,
                                                   _Out_ UINT* pHeight,
                                                   _Out_ D3DDDIFORMAT* pColorFormat)
{
    UNREFERENCED_PARAMETER(Flags);

    DbgPrint(TRACE_LEVEL_INFORMATION, ("---> %s\n", __FUNCTION__));
    m_SystemDisplaySourceId = D3DDDI_ID_UNINITIALIZED;

    QXL_ASSERT((TargetId < MAX_CHILDREN) || (TargetId == D3DDDI_ID_UNINITIALIZED));

    // Find the frame buffer for displaying the bugcheck, if it was successfully mapped
    if (TargetId == D3DDDI_ID_UNINITIALIZED)
    {
        for (UINT SourceIdx = 0; SourceIdx < MAX_VIEWS; ++SourceIdx)
        {
            if (m_CurrentModes[SourceIdx].FrameBuffer.Ptr != NULL)
            {
                m_SystemDisplaySourceId = SourceIdx;
                break;
            }
        }
    }
    else
    {
        m_SystemDisplaySourceId = FindSourceForTarget(TargetId, FALSE);
    }

    if (m_SystemDisplaySourceId == D3DDDI_ID_UNINITIALIZED)
    {
        {
            return STATUS_UNSUCCESSFUL;
        }
    }

    if ((m_CurrentModes[m_SystemDisplaySourceId].Rotation == D3DKMDT_VPPR_ROTATE90) ||
        (m_CurrentModes[m_SystemDisplaySourceId].Rotation == D3DKMDT_VPPR_ROTATE270))
    {
        *pHeight = m_CurrentModes[m_SystemDisplaySourceId].DispInfo.Width;
        *pWidth = m_CurrentModes[m_SystemDisplaySourceId].DispInfo.Height;
    }
    else
    {
        *pWidth = m_CurrentModes[m_SystemDisplaySourceId].DispInfo.Width;
        *pHeight = m_CurrentModes[m_SystemDisplaySourceId].DispInfo.Height;
    }

    *pColorFormat = m_CurrentModes[m_SystemDisplaySourceId].DispInfo.ColorFormat;


    return STATUS_SUCCESS;
}

// Must be Non-Paged, as it is called to display the bugcheck screen
VOID QxlDod::SystemDisplayWrite(_In_reads_bytes_(SourceHeight * SourceStride) VOID* pSource,
                                              _In_ UINT SourceWidth,
                                              _In_ UINT SourceHeight,
                                              _In_ UINT SourceStride,
                                              _In_ INT PositionX,
                                              _In_ INT PositionY)
{
    UNREFERENCED_PARAMETER(pSource);
    UNREFERENCED_PARAMETER(SourceStride);
    // Rect will be Offset by PositionX/Y in the src to reset it back to 0
    RECT Rect;
    Rect.left = PositionX;
    Rect.top = PositionY;
    Rect.right =  Rect.left + SourceWidth;
    Rect.bottom = Rect.top + SourceHeight;

    // Set up destination blt info
    BLT_INFO DstBltInfo;
    DstBltInfo.pBits = m_CurrentModes[m_SystemDisplaySourceId].FrameBuffer.Ptr;
    DstBltInfo.Pitch = m_CurrentModes[m_SystemDisplaySourceId].DispInfo.Pitch;
    DstBltInfo.BitsPerPel = BPPFromPixelFormat(m_CurrentModes[m_SystemDisplaySourceId].DispInfo.ColorFormat);
    DstBltInfo.Offset.x = 0;
    DstBltInfo.Offset.y = 0;
    DstBltInfo.Rotation = m_CurrentModes[m_SystemDisplaySourceId].Rotation;
    DstBltInfo.Width = m_CurrentModes[m_SystemDisplaySourceId].DispInfo.Width;
    DstBltInfo.Height = m_CurrentModes[m_SystemDisplaySourceId].DispInfo.Height;

    // Set up source blt info
    BLT_INFO SrcBltInfo;
    SrcBltInfo.pBits = pSource;
    SrcBltInfo.Pitch = SourceStride;
    SrcBltInfo.BitsPerPel = 32;

    SrcBltInfo.Offset.x = -PositionX;
    SrcBltInfo.Offset.y = -PositionY;
    SrcBltInfo.Rotation = D3DKMDT_VPPR_IDENTITY;
    SrcBltInfo.Width = SourceWidth;
    SrcBltInfo.Height = SourceHeight;

    BltBits(&DstBltInfo,
            &SrcBltInfo,
            1, // NumRects
            &Rect);

}

#pragma code_seg(pop) // End Non-Paged Code

NTSTATUS QxlDod::WriteHWInfoStr(_In_ HANDLE DevInstRegKeyHandle, _In_ PCWSTR pszwValueName, _In_ PCSTR pszValue)
{
    PAGED_CODE();

    NTSTATUS Status;
    ANSI_STRING AnsiStrValue;
    UNICODE_STRING UnicodeStrValue;
    UNICODE_STRING UnicodeStrValueName;
    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));

    // ZwSetValueKey wants the ValueName as a UNICODE_STRING
    RtlInitUnicodeString(&UnicodeStrValueName, pszwValueName);

    // REG_SZ is for WCHARs, there is no equivalent for CHARs
    // Use the ansi/unicode conversion functions to get from PSTR to PWSTR
    RtlInitAnsiString(&AnsiStrValue, pszValue);
    Status = RtlAnsiStringToUnicodeString(&UnicodeStrValue, &AnsiStrValue, TRUE);
    if (!NT_SUCCESS(Status))
    {
        DbgPrint(TRACE_LEVEL_ERROR, ("RtlAnsiStringToUnicodeString failed with Status: 0x%X\n", Status));
        return Status;
    }

    // Write the value to the registry
    Status = ZwSetValueKey(DevInstRegKeyHandle,
                           &UnicodeStrValueName,
                           0,
                           REG_SZ,
                           UnicodeStrValue.Buffer,
                           UnicodeStrValue.MaximumLength);

    // Free the earlier allocated unicode string
    RtlFreeUnicodeString(&UnicodeStrValue);

    if (!NT_SUCCESS(Status))
    {
        DbgPrint(TRACE_LEVEL_ERROR, ("ZwSetValueKey failed with Status: 0x%X\n", Status));
    }

    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
    return Status;
}

NTSTATUS QxlDod::RegisterHWInfo()
{
    PAGED_CODE();

    NTSTATUS Status;
    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));

    // TODO: Replace these strings with proper information
    PCSTR StrHWInfoChipType = "QEMU QXL";
    PCSTR StrHWInfoDacType = "QXL 1B36";
    PCSTR StrHWInfoAdapterString = "QXL";
    PCSTR StrHWInfoBiosString = "SEABIOS QXL";

    HANDLE DevInstRegKeyHandle;
    Status = IoOpenDeviceRegistryKey(m_pPhysicalDevice, PLUGPLAY_REGKEY_DRIVER, KEY_SET_VALUE, &DevInstRegKeyHandle);
    if (!NT_SUCCESS(Status))
    {
        DbgPrint(TRACE_LEVEL_ERROR, ("IoOpenDeviceRegistryKey failed for PDO: 0x%I64x, Status: 0x%I64x", m_pPhysicalDevice, Status));
        return Status;
    }

    Status = WriteHWInfoStr(DevInstRegKeyHandle, L"HardwareInformation.ChipType", StrHWInfoChipType);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    Status = WriteHWInfoStr(DevInstRegKeyHandle, L"HardwareInformation.DacType", StrHWInfoDacType);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    Status = WriteHWInfoStr(DevInstRegKeyHandle, L"HardwareInformation.AdapterString", StrHWInfoAdapterString);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    Status = WriteHWInfoStr(DevInstRegKeyHandle, L"HardwareInformation.BiosString", StrHWInfoBiosString);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    // MemorySize is a ULONG, unlike the others which are all strings
    UNICODE_STRING ValueNameMemorySize;
    RtlInitUnicodeString(&ValueNameMemorySize, L"HardwareInformation.MemorySize");
    DWORD MemorySize = 0; // BDD has no access to video memory
    Status = ZwSetValueKey(DevInstRegKeyHandle,
                           &ValueNameMemorySize,
                           0,
                           REG_DWORD,
                           &MemorySize,
                           sizeof(MemorySize));
    if (!NT_SUCCESS(Status))
    {
        DbgPrint(TRACE_LEVEL_ERROR, ("ZwSetValueKey for MemorySize failed with Status: 0x%X\n", Status));
        return Status;
    }

    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
    return Status;
}


NTSTATUS
QxlDod::ExecutePresentDisplayOnly(
    _In_ BYTE*             DstAddr,
    _In_ UINT              DstBitPerPixel,
    _In_ BYTE*             SrcAddr,
    _In_ UINT              SrcBytesPerPixel,
    _In_ LONG              SrcPitch,
    _In_ ULONG             NumMoves,
    _In_ D3DKMT_MOVE_RECT* Moves,
    _In_ ULONG             NumDirtyRects,
    _In_ RECT*             DirtyRect,
    _In_ D3DKMDT_VIDPN_PRESENT_PATH_ROTATION Rotation)
/*++

  Routine Description:

    The method creates present worker thread and provides context
    for it filled with present commands

  Arguments:

    DstAddr - address of destination surface
    DstBitPerPixel - color depth of destination surface
    SrcAddr - address of source surface
    SrcBytesPerPixel - bytes per pixel of source surface
    SrcPitch - source surface pitch (bytes in a row)
    NumMoves - number of moves to be copied
    Moves - moves' data
    NumDirtyRects - number of rectangles to be copied
    DirtyRect - rectangles' data
    Rotation - roatation to be performed when executing copy
    CallBack - callback for present worker thread to report execution status

  Return Value:

    Status

--*/
{

    PAGED_CODE();
    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));

    NTSTATUS Status = STATUS_SUCCESS;

    SIZE_T sizeMoves = NumMoves*sizeof(D3DKMT_MOVE_RECT);
    SIZE_T sizeRects = NumDirtyRects*sizeof(RECT);
    SIZE_T size = sizeof(DoPresentMemory) + sizeMoves + sizeRects;

    DoPresentMemory* ctx = reinterpret_cast<DoPresentMemory*>
                                (new (NonPagedPoolNx) BYTE[size]);

    if (!ctx)
    {
        return STATUS_NO_MEMORY;
    }

    RtlZeroMemory(ctx,size);

//    const CURRENT_BDD_MODE* pModeCur = GetCurrentMode(m_SourceId);
    const CURRENT_BDD_MODE* pModeCur = &m_CurrentModes[0];
    ctx->DstAddr          = DstAddr;
    ctx->DstBitPerPixel   = DstBitPerPixel;
    ctx->DstStride        = pModeCur->DispInfo.Pitch; //m_ModeInfo[m_CurrentMode].BytesPerScanLine;//
    ctx->SrcWidth         = pModeCur->SrcModeWidth;//m_ModeInfo[m_CurrentMode].XResolution;//
    ctx->SrcHeight        = pModeCur->SrcModeHeight;//m_ModeInfo[m_CurrentMode].YResolution;//
    ctx->SrcAddr          = NULL;
    ctx->SrcPitch         = SrcPitch;
    ctx->Rotation         = Rotation;
    ctx->NumMoves         = NumMoves;
    ctx->Moves            = Moves;
    ctx->NumDirtyRects    = NumDirtyRects;
    ctx->DirtyRect        = DirtyRect;
//    ctx->SourceID         = m_SourceId;
//    ctx->hAdapter         = m_DevExt;
    ctx->Mdl              = NULL;
    ctx->DisplaySource    = this;

    // Alternate between synch and asynch execution, for demonstrating 
    // that a real hardware implementation can do either

    {
        // Map Source into kernel space, as Blt will be executed by system worker thread
        UINT sizeToMap = SrcBytesPerPixel * ctx->SrcWidth * ctx->SrcHeight;

        PMDL mdl = IoAllocateMdl((PVOID)SrcAddr, sizeToMap,  FALSE, FALSE, NULL);
        if(!mdl)
        {
            return STATUS_INSUFFICIENT_RESOURCES;
        }

        KPROCESSOR_MODE AccessMode = static_cast<KPROCESSOR_MODE>(( SrcAddr <=
                        (BYTE* const) MM_USER_PROBE_ADDRESS)?UserMode:KernelMode);
        __try
        {
            // Probe and lock the pages of this buffer in physical memory.
            // We need only IoReadAccess.
            MmProbeAndLockPages(mdl, AccessMode, IoReadAccess);
        }
        #pragma prefast(suppress: __WARNING_EXCEPTIONEXECUTEHANDLER, "try/except is only able to protect against user-mode errors and these are the only errors we try to catch here");
        __except(EXCEPTION_EXECUTE_HANDLER)
        {
            Status = GetExceptionCode();
            IoFreeMdl(mdl);
            return Status;
        }

        // Map the physical pages described by the MDL into system space.
        // Note: double mapping the buffer this way causes lot of system
        // overhead for large size buffers.
        ctx->SrcAddr = reinterpret_cast<BYTE*>
            (MmGetSystemAddressForMdlSafe(mdl, NormalPagePriority ));

        if(!ctx->SrcAddr) {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            MmUnlockPages(mdl);
            IoFreeMdl(mdl);
            return Status;
        }

        // Save Mdl to unmap and unlock the pages in worker thread
        ctx->Mdl = mdl;
    }

    BYTE* rects = reinterpret_cast<BYTE*>(ctx+1);

    // copy moves and update pointer
    if (Moves)
    {
        memcpy(rects,Moves,sizeMoves);
        ctx->Moves = reinterpret_cast<D3DKMT_MOVE_RECT*>(rects);
        rects += sizeMoves;
    }

    // copy dirty rects and update pointer
    if (DirtyRect)
    {
        memcpy(rects,DirtyRect,sizeRects);
        ctx->DirtyRect = reinterpret_cast<RECT*>(rects);
    }


//    HwExecutePresentDisplayOnly((PVOID)ctx);


    // Set up destination blt info
    BLT_INFO DstBltInfo;
    DstBltInfo.pBits = ctx->DstAddr;
    DstBltInfo.Pitch = ctx->DstStride;
    DstBltInfo.BitsPerPel = ctx->DstBitPerPixel;
    DstBltInfo.Offset.x = 0;
    DstBltInfo.Offset.y = 0;
    DstBltInfo.Rotation = ctx->Rotation;
    DstBltInfo.Width = ctx->SrcWidth;
    DstBltInfo.Height = ctx->SrcHeight;

    // Set up source blt info
    BLT_INFO SrcBltInfo;
    SrcBltInfo.pBits = ctx->SrcAddr;
    SrcBltInfo.Pitch = ctx->SrcPitch;
    SrcBltInfo.BitsPerPel = 32;
    SrcBltInfo.Offset.x = 0;
    SrcBltInfo.Offset.y = 0;
    SrcBltInfo.Rotation = D3DKMDT_VPPR_IDENTITY;
    if (ctx->Rotation == D3DKMDT_VPPR_ROTATE90 ||
        ctx->Rotation == D3DKMDT_VPPR_ROTATE270)
    {
        SrcBltInfo.Width = DstBltInfo.Height;
        SrcBltInfo.Height = DstBltInfo.Width;
    }
    else
    {
        SrcBltInfo.Width = DstBltInfo.Width;
        SrcBltInfo.Height = DstBltInfo.Height;
    }


    // Copy all the scroll rects from source image to video frame buffer.
    for (UINT i = 0; i < ctx->NumMoves; i++)
    {
        BltBits(&DstBltInfo,
        &SrcBltInfo,
        1, // NumRects
        &ctx->Moves[i].DestRect);
    }

    // Copy all the dirty rects from source image to video frame buffer.
    for (UINT i = 0; i < ctx->NumDirtyRects; i++)
    {

        BltBits(&DstBltInfo,
        &SrcBltInfo,
        1, // NumRects
        &ctx->DirtyRect[i]);
    }

    // Unmap unmap and unlock the pages.
    if (ctx->Mdl)
    {
        MmUnlockPages(ctx->Mdl);
        IoFreeMdl(ctx->Mdl);
    }
    delete [] reinterpret_cast<BYTE*>(ctx);

    return STATUS_SUCCESS;
}


VOID QxlDod::GetPitches(_In_ CONST BLT_INFO* pBltInfo, _Out_ LONG* pPixelPitch, _Out_ LONG* pRowPitch)
{
    switch (pBltInfo->Rotation)
    {
        case D3DKMDT_VPPR_IDENTITY:
        {
            *pPixelPitch = (pBltInfo->BitsPerPel / BITS_PER_BYTE);
            *pRowPitch = pBltInfo->Pitch;
            return;
        }
        case D3DKMDT_VPPR_ROTATE90:
        {
            *pPixelPitch = -((LONG)pBltInfo->Pitch);
            *pRowPitch = (pBltInfo->BitsPerPel / BITS_PER_BYTE);
            return;
        }
        case D3DKMDT_VPPR_ROTATE180:
        {
            *pPixelPitch = -((LONG)pBltInfo->BitsPerPel / BITS_PER_BYTE);
            *pRowPitch = -((LONG)pBltInfo->Pitch);
            return;
        }
        case D3DKMDT_VPPR_ROTATE270:
        {
            *pPixelPitch = pBltInfo->Pitch;
            *pRowPitch = -((LONG)pBltInfo->BitsPerPel / BITS_PER_BYTE);
            return;
        }
        default:
        {
            QXL_LOG_ASSERTION1("Invalid rotation (0x%I64x) specified", pBltInfo->Rotation);
            *pPixelPitch = 0;
            *pRowPitch = 0;
            return;
        }
    }
}

BYTE* QxlDod::GetRowStart(_In_ CONST BLT_INFO* pBltInfo, CONST RECT* pRect)
{
    BYTE* pRet = NULL;
    LONG OffLeft = pRect->left + pBltInfo->Offset.x;
    LONG OffTop = pRect->top + pBltInfo->Offset.y;
    LONG BytesPerPixel = (pBltInfo->BitsPerPel / BITS_PER_BYTE);
    switch (pBltInfo->Rotation)
    {
        case D3DKMDT_VPPR_IDENTITY:
        {
            pRet = ((BYTE*)pBltInfo->pBits +
                           OffTop * pBltInfo->Pitch +
                           OffLeft * BytesPerPixel);
            break;
        }
        case D3DKMDT_VPPR_ROTATE90:
        {
            pRet = ((BYTE*)pBltInfo->pBits +
                           (pBltInfo->Height - 1 - OffLeft) * pBltInfo->Pitch +
                           OffTop * BytesPerPixel);
            break;
        }
        case D3DKMDT_VPPR_ROTATE180:
        {
            pRet = ((BYTE*)pBltInfo->pBits +
                           (pBltInfo->Height - 1 - OffTop) * pBltInfo->Pitch +
                           (pBltInfo->Width - 1 - OffLeft) * BytesPerPixel);
            break;
        }
        case D3DKMDT_VPPR_ROTATE270:
        {
            pRet = ((BYTE*)pBltInfo->pBits +
                           OffLeft * pBltInfo->Pitch +
                           (pBltInfo->Width - 1 - OffTop) * BytesPerPixel);
            break;
        }
        default:
        {
            QXL_LOG_ASSERTION1("Invalid rotation (0x%I64x) specified", pBltInfo->Rotation);
            break;
        }
    }

    return pRet;
}

/****************************Internal*Routine******************************\
 * CopyBitsGeneric
 *
 *
 * Blt function which can handle a rotated dst/src, offset rects in dst/src
 * and bpp combinations of:
 *   dst | src
 *    32 | 32   // For identity rotation this is much faster in CopyBits32_32
 *    32 | 24
 *    32 | 16
 *    24 | 32
 *    16 | 32
 *     8 | 32
 *    24 | 24   // untested
 *
\**************************************************************************/

VOID QxlDod::CopyBitsGeneric(
    BLT_INFO* pDst,
    CONST BLT_INFO* pSrc,
    UINT  NumRects,
    _In_reads_(NumRects) CONST RECT *pRects)
{
    LONG DstPixelPitch = 0;
    LONG DstRowPitch = 0;
    LONG SrcPixelPitch = 0;
    LONG SrcRowPitch = 0;

    DbgPrint(TRACE_LEVEL_VERBOSE , ("---> %s NumRects = %d Dst = %p Src = %p\n", __FUNCTION__, NumRects, pDst->pBits, pSrc->pBits));

    GetPitches(pDst, &DstPixelPitch, &DstRowPitch);
    GetPitches(pSrc, &SrcPixelPitch, &SrcRowPitch);

    for (UINT iRect = 0; iRect < NumRects; iRect++)
    {
        CONST RECT* pRect = &pRects[iRect];

        NT_ASSERT(pRect->right >= pRect->left);
        NT_ASSERT(pRect->bottom >= pRect->top);

        UINT NumPixels = pRect->right - pRect->left;
        UINT NumRows = pRect->bottom - pRect->top;

        BYTE* pDstRow = GetRowStart(pDst, pRect);
        CONST BYTE* pSrcRow = GetRowStart(pSrc, pRect);

        for (UINT y=0; y < NumRows; y++)
        {
            BYTE* pDstPixel = pDstRow;
            CONST BYTE* pSrcPixel = pSrcRow;

            for (UINT x=0; x < NumPixels; x++)
            {
                if ((pDst->BitsPerPel == 24) ||
                    (pSrc->BitsPerPel == 24))
                {
                    pDstPixel[0] = pSrcPixel[0];
                    pDstPixel[1] = pSrcPixel[1];
                    pDstPixel[2] = pSrcPixel[2];
                    // pPixel[3] is the alpha channel and is ignored for whichever of Src/Dst is 32bpp
                }
                else if (pDst->BitsPerPel == 32)
                {
                    if (pSrc->BitsPerPel == 32)
                    {
                        UINT32* pDstPixelAs32 = (UINT32*)pDstPixel;
                        UINT32* pSrcPixelAs32 = (UINT32*)pSrcPixel;
                        *pDstPixelAs32 = *pSrcPixelAs32;
                    }
                    else if (pSrc->BitsPerPel == 16)
                    {
                        UINT32* pDstPixelAs32 = (UINT32*)pDstPixel;
                        UINT16* pSrcPixelAs16 = (UINT16*)pSrcPixel;

                        *pDstPixelAs32 = CONVERT_16BPP_TO_32BPP(*pSrcPixelAs16);
                    }
                    else
                    {
                        // Invalid pSrc->BitsPerPel on a pDst->BitsPerPel of 32
                        NT_ASSERT(FALSE);
                    }
                }
                else if (pDst->BitsPerPel == 16)
                {
                    NT_ASSERT(pSrc->BitsPerPel == 32);

                    UINT16* pDstPixelAs16 = (UINT16*)pDstPixel;
                    *pDstPixelAs16 = CONVERT_32BPP_TO_16BPP(pSrcPixel);
                }
                else if (pDst->BitsPerPel == 8)
                {
                    NT_ASSERT(pSrc->BitsPerPel == 32);

                    *pDstPixel = CONVERT_32BPP_TO_8BPP(pSrcPixel);
                }
                else
                {
                    // Invalid pDst->BitsPerPel
                    NT_ASSERT(FALSE);
                }
                pDstPixel += DstPixelPitch;
                pSrcPixel += SrcPixelPitch;
            }

            pDstRow += DstRowPitch;
            pSrcRow += SrcRowPitch;
        }
    }
}


VOID QxlDod::CopyBits32_32(
    BLT_INFO* pDst,
    CONST BLT_INFO* pSrc,
    UINT  NumRects,
    _In_reads_(NumRects) CONST RECT *pRects)
{
    NT_ASSERT((pDst->BitsPerPel == 32) &&
              (pSrc->BitsPerPel == 32));
    NT_ASSERT((pDst->Rotation == D3DKMDT_VPPR_IDENTITY) &&
              (pSrc->Rotation == D3DKMDT_VPPR_IDENTITY));

    DbgPrint(TRACE_LEVEL_INFORMATION, ("---> %s\n", __FUNCTION__));

    for (UINT iRect = 0; iRect < NumRects; iRect++)
    {
        CONST RECT* pRect = &pRects[iRect];

        NT_ASSERT(pRect->right >= pRect->left);
        NT_ASSERT(pRect->bottom >= pRect->top);

        UINT NumPixels = pRect->right - pRect->left;
        UINT NumRows = pRect->bottom - pRect->top;
        UINT BytesToCopy = NumPixels * 4;
        BYTE* pStartDst = ((BYTE*)pDst->pBits +
                          (pRect->top + pDst->Offset.y) * pDst->Pitch +
                          (pRect->left + pDst->Offset.x) * 4);
        CONST BYTE* pStartSrc = ((BYTE*)pSrc->pBits +
                                (pRect->top + pSrc->Offset.y) * pSrc->Pitch +
                                (pRect->left + pSrc->Offset.x) * 4);

        for (UINT i = 0; i < NumRows; ++i)
        {
            RtlCopyMemory(pStartDst, pStartSrc, BytesToCopy);
            pStartDst += pDst->Pitch;
            pStartSrc += pSrc->Pitch;
        }
    }
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
}


VOID QxlDod::BltBits (
    BLT_INFO* pDst,
    CONST BLT_INFO* pSrc,
    UINT  NumRects,
    _In_reads_(NumRects) CONST RECT *pRects)
{
    // pSrc->pBits might be coming from user-mode. User-mode addresses when accessed by kernel need to be protected by a __try/__except.
    // This usage is redundant in the sample driver since it is already being used for MmProbeAndLockPages. However, it is very important
    // to have this in place and to make sure developers don't miss it, it is in these two locations.
    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));
    __try
    {
        if (pDst->BitsPerPel == 32 &&
            pSrc->BitsPerPel == 32 &&
            pDst->Rotation == D3DKMDT_VPPR_IDENTITY &&
            pSrc->Rotation == D3DKMDT_VPPR_IDENTITY)
        {
            // This is by far the most common copy function being called
            CopyBits32_32(pDst, pSrc, NumRects, pRects);
        }
        else
        {
            CopyBitsGeneric(pDst, pSrc, NumRects, pRects);
        }
    }
    #pragma prefast(suppress: __WARNING_EXCEPTIONEXECUTEHANDLER, "try/except is only able to protect against user-mode errors and these are the only errors we try to catch here");
    __except(EXCEPTION_EXECUTE_HANDLER)
    {
        DbgPrint(TRACE_LEVEL_ERROR, ("Either dst (0x%I64x) or src (0x%I64x) bits encountered exception during access.\n", pDst->pBits, pSrc->pBits));
    }
}


NTSTATUS QxlDod::VbeGetModeList()
{
    PAGED_CODE();
    USHORT m_Segment;
    USHORT m_Offset;
    USHORT ModeCount;
    ULONG SuitableModeCount;
    USHORT ModeTemp;
    USHORT CurrentMode;
    PVBE_MODEINFO VbeModeInfo;
    VBE_INFO VbeInfo = {0};
    ULONG Length;

    NTSTATUS Status = STATUS_SUCCESS;
    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));
//Get VBE Mode List
    Length = 0x400;
    Status = x86BiosAllocateBuffer (&Length, &m_Segment, &m_Offset);
    if (!NT_SUCCESS (Status))
    {
        DbgPrint(TRACE_LEVEL_ERROR, ("x86BiosAllocateBuffer failed with Status: 0x%X\n", Status));
        return Status;
    }
    DbgPrint(TRACE_LEVEL_ERROR, ("x86BiosAllocateBuffer 0x%x (%x.%x)\n", VbeInfo.VideoModePtr, m_Segment, m_Offset));

    Status = x86BiosWriteMemory (m_Segment, m_Offset, "VBE2", 4);

    if (!NT_SUCCESS (Status))
    {
        DbgPrint(TRACE_LEVEL_ERROR, ("x86BiosWriteMemory failed with Status: 0x%X\n", Status));
        return Status;
    }

    X86BIOS_REGISTERS regs = {0};
    regs.SegEs = m_Segment;
    regs.Edi = m_Offset;
    regs.Eax = 0x4F00;
    if (!x86BiosCall (0x10, &regs) /* || (regs.Eax & 0xFF00) != 0x4F00 */)
    {
        DbgPrint(TRACE_LEVEL_ERROR, ("x86BiosCall failed\n"));
        return STATUS_UNSUCCESSFUL;
    }

    Status = x86BiosReadMemory (m_Segment, m_Offset, &VbeInfo, sizeof (VbeInfo));
    if (!NT_SUCCESS (Status))
    {
        DbgPrint(TRACE_LEVEL_ERROR, ("x86BiosReadMemory failed with Status: 0x%X\n", Status));
        return Status;
    }

    if (!RtlEqualMemory(VbeInfo.Signature, "VESA", 4))
    {
        DbgPrint(TRACE_LEVEL_ERROR, ("No VBE BIOS present\n"));
        return STATUS_UNSUCCESSFUL;
    }

    DbgPrint(TRACE_LEVEL_ERROR, ("VBE BIOS Present (%d.%d, %8ld Kb)\n", VbeInfo.Version / 0x100, VbeInfo.Version & 0xFF, VbeInfo.TotalMemory * 64));
    DbgPrint(TRACE_LEVEL_ERROR, ("Capabilities = 0x%x\n", VbeInfo.Capabilities));
    DbgPrint(TRACE_LEVEL_ERROR, ("VideoModePtr = 0x%x (0x%x.0x%x)\n", VbeInfo.VideoModePtr, HIWORD( VbeInfo.VideoModePtr), LOWORD( VbeInfo.VideoModePtr)));

   for (ModeCount = 0; ; ModeCount++)
   {
      /* Read the VBE mode number. */
        Status = x86BiosReadMemory (
                    HIWORD(VbeInfo.VideoModePtr),
                    LOWORD(VbeInfo.VideoModePtr) + (ModeCount << 1),
                    &ModeTemp,
                    sizeof(ModeTemp));

        if (!NT_SUCCESS (Status))
        {
            DbgPrint(TRACE_LEVEL_ERROR, ("x86BiosReadMemory failed with Status: 0x%X\n", Status));
            break;
        }
      /* End of list? */
        if (ModeTemp == 0xFFFF || ModeTemp == 0)
        {
            break;
        }
   }

    DbgPrint(TRACE_LEVEL_ERROR, ("ModeCount %d\n", ModeCount));

    m_ModeInfo = reinterpret_cast<PVBE_MODEINFO> (new (PagedPool) BYTE[sizeof (VBE_MODEINFO) * ModeCount]);
    m_ModeNumbers = reinterpret_cast<PUSHORT> (new (PagedPool)  BYTE [sizeof (USHORT) * ModeCount]);
    m_CurrentMode = 0;
    DbgPrint(TRACE_LEVEL_ERROR, ("m_ModeInfo = 0x%p, m_ModeNumbers = 0x%p\n", m_ModeInfo, m_ModeNumbers)); 
    for (CurrentMode = 0, SuitableModeCount = 0;
         CurrentMode < ModeCount;
         CurrentMode++)
    {
        Status = x86BiosReadMemory (
                    HIWORD(VbeInfo.VideoModePtr),
                    LOWORD(VbeInfo.VideoModePtr) + (CurrentMode << 1),
                    &ModeTemp,
                    sizeof(ModeTemp));

        if (!NT_SUCCESS (Status))
        {
            DbgPrint(TRACE_LEVEL_ERROR, ("x86BiosReadMemory failed with Status: 0x%X\n", Status));
            break;
        }

        DbgPrint(TRACE_LEVEL_ERROR, ("ModeTemp = 0x%X\n", ModeTemp));
        RtlZeroMemory(&regs, sizeof(regs));
        regs.Eax = 0x4F01;
        regs.Ecx = ModeTemp;
        regs.Edi = m_Offset + sizeof (VbeInfo);
        regs.SegEs = m_Segment;
        if (!x86BiosCall (0x10, &regs))
        {
           DbgPrint(TRACE_LEVEL_ERROR, ("x86BiosCall failed\n"));
           return STATUS_UNSUCCESSFUL;
        }
        Status = x86BiosReadMemory (
                    m_Segment,
                    m_Offset + sizeof (VbeInfo),
                    m_ModeInfo + SuitableModeCount,
                    sizeof(VBE_MODEINFO));

        VbeModeInfo = m_ModeInfo + SuitableModeCount;

        if (VbeModeInfo->XResolution >= 1024 &&
            VbeModeInfo->YResolution >= 768 &&
            VbeModeInfo->BitsPerPixel == 24 &&
            VbeModeInfo->PhysBasePtr != 0)
        {
            m_ModeNumbers[SuitableModeCount] = ModeTemp;
            if (VbeModeInfo->XResolution == 1024 &&
                VbeModeInfo->YResolution == 768)
            {
                m_CurrentMode = (USHORT)SuitableModeCount;
            }
            SuitableModeCount++;
        }
    }

    if (SuitableModeCount == 0)
    {
        DbgPrint(TRACE_LEVEL_ERROR, ("No video modes supported\n"));
        Status = STATUS_UNSUCCESSFUL;
    }

    m_ModeCount = SuitableModeCount;
    DbgPrint(TRACE_LEVEL_ERROR, ("ModeCount filtered %d\n", m_ModeCount));
    for (ULONG idx = 0; idx < m_ModeCount; idx++)
    {
        DbgPrint(TRACE_LEVEL_ERROR, ("type %x, XRes = %d, YRes = %d, BPP = %d\n", m_ModeNumbers[idx], m_ModeInfo[idx].XResolution,  m_ModeInfo[idx].YResolution,  m_ModeInfo[idx].BitsPerPixel));
    }

    if (m_Segment != 0)
    {
        x86BiosFreeBuffer (m_Segment, m_Offset);
    }
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
    return Status;
}

NTSTATUS QxlDod::VbeQueryCurrentMode(PVIDEO_MODE RequestedMode)
{
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
    NTSTATUS Status = STATUS_SUCCESS;
    UNREFERENCED_PARAMETER(RequestedMode);

//    PVBE_MODEINFO VBEMode = &m_ModeInfo[m_CurrentMode];
    return Status;
}

NTSTATUS QxlDod::VbeSetCurrentMode(ULONG Mode)
{
    NTSTATUS Status = STATUS_SUCCESS;
    DbgPrint(TRACE_LEVEL_INFORMATION, ("---> %s Mode = %x\n", __FUNCTION__, Mode));
    X86BIOS_REGISTERS regs = {0};
    regs.Eax = 0x4F02;
    regs.Ebx = Mode | 0x000;
    if (!x86BiosCall (0x10, &regs))
    {
        DbgPrint(TRACE_LEVEL_ERROR, ("x86BiosCall failed\n"));
        return STATUS_UNSUCCESSFUL;
    }
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
    return Status;
}

NTSTATUS QxlDod::VbeGetCurrentMode(ULONG* pMode)
{
    NTSTATUS Status = STATUS_SUCCESS;
    DbgPrint(TRACE_LEVEL_INFORMATION, ("---> %s\n", __FUNCTION__));
    X86BIOS_REGISTERS regs = {0};
    regs.Eax = 0x4F03;
    if (!x86BiosCall (0x10, &regs))
    {
        DbgPrint(TRACE_LEVEL_ERROR, ("x86BiosCall failed\n"));
        return STATUS_UNSUCCESSFUL;
    }
    *pMode = regs.Ebx;
    DbgPrint(TRACE_LEVEL_INFORMATION, ("<---> EAX = %x, EBX = %x Mode = %x\n", regs.Eax, regs.Ebx, *pMode));
    return Status;
}

NTSTATUS QxlDod::VbeSetPowerState(POWER_ACTION ActionType)
{
    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));

    X86BIOS_REGISTERS regs = {0};
    regs.Eax = 0x4F10;
    regs.Ebx = 1;
    switch (ActionType)
    {
        case PowerActionNone: break;
        case PowerActionSleep: regs.Ebx |= 0x100; break;
        case PowerActionHibernate: regs.Ebx |= 0x200; break;
        case PowerActionShutdown:
        case PowerActionShutdownReset:
        case PowerActionShutdownOff: regs.Ebx |= 0x400; break;
    }
    if (!x86BiosCall (0x10, &regs))
    {
        DbgPrint(TRACE_LEVEL_ERROR, ("x86BiosCall failed\n"));
        return STATUS_UNSUCCESSFUL;
    }
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
    return STATUS_SUCCESS;
}

//
// Non-Paged Code
//
#pragma code_seg(push)
#pragma code_seg()
D3DDDI_VIDEO_PRESENT_SOURCE_ID QxlDod::FindSourceForTarget(D3DDDI_VIDEO_PRESENT_TARGET_ID TargetId, BOOLEAN DefaultToZero)
{
    UNREFERENCED_PARAMETER(TargetId);
    for (UINT SourceId = 0; SourceId < MAX_VIEWS; ++SourceId)
    {
        if (m_CurrentModes[SourceId].FrameBuffer.Ptr != NULL)
        {
            return SourceId;
        }
    }

    return DefaultToZero ? 0 : D3DDDI_ID_UNINITIALIZED;
}

#pragma code_seg(pop) // End Non-Paged Code

//
// Frame buffer map/unmap
//

NTSTATUS
MapFrameBuffer(
    _In_                       PHYSICAL_ADDRESS    PhysicalAddress,
    _In_                       ULONG               Length,
    _Outptr_result_bytebuffer_(Length) VOID**              VirtualAddress)
{
    PAGED_CODE();

    //
    // Check for parameters
    //
    if ((PhysicalAddress.QuadPart == (ULONGLONG)0) ||
        (Length == 0) ||
        (VirtualAddress == NULL))
    {
        DbgPrint(TRACE_LEVEL_ERROR, ("One of PhysicalAddress.QuadPart (0x%I64x), Length (0x%I64x), VirtualAddress (0x%I64x) is NULL or 0",
                        PhysicalAddress.QuadPart, Length, VirtualAddress));
        return STATUS_INVALID_PARAMETER;
    }

    *VirtualAddress = MmMapIoSpace(PhysicalAddress,
                                   Length,
                                   MmWriteCombined);
    if (*VirtualAddress == NULL)
    {
        // The underlying call to MmMapIoSpace failed. This may be because, MmWriteCombined
        // isn't supported, so try again with MmNonCached

        *VirtualAddress = MmMapIoSpace(PhysicalAddress,
                                       Length,
                                       MmNonCached);
        if (*VirtualAddress == NULL)
        {
            DbgPrint(TRACE_LEVEL_ERROR, ("MmMapIoSpace returned a NULL buffer when trying to allocate 0x%I64x bytes", Length));
            return STATUS_NO_MEMORY;
        }
    }

    return STATUS_SUCCESS;
}

NTSTATUS
UnmapFrameBuffer(
    _In_reads_bytes_(Length) VOID* VirtualAddress,
    _In_                ULONG Length)
{
    PAGED_CODE();


    //
    // Check for parameters
    //
    if ((VirtualAddress == NULL) && (Length == 0))
    {
        // Allow this function to be called when there's no work to do, and treat as successful
        return STATUS_SUCCESS;
    }
    else if ((VirtualAddress == NULL) || (Length == 0))
    {
        DbgPrint(TRACE_LEVEL_ERROR, ("Only one of Length (0x%I64x), VirtualAddress (0x%I64x) is NULL or 0",
                        Length, VirtualAddress));
        return STATUS_INVALID_PARAMETER;
    }

    MmUnmapIoSpace(VirtualAddress,
                   Length);

    return STATUS_SUCCESS;
}

