#include "driver.h"
#include "qxldod.h"
#include "qxl_windows.h"

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
    DbgPrint(TRACE_LEVEL_INFORMATION, ("---> %s\n", __FUNCTION__));
    *((UINT*)&m_Flags) = 0;
    RtlZeroMemory(&m_DxgkInterface, sizeof(m_DxgkInterface));
    RtlZeroMemory(&m_DeviceInfo, sizeof(m_DeviceInfo));
    RtlZeroMemory(m_CurrentModes, sizeof(m_CurrentModes));
    RtlZeroMemory(&m_PointerShape, sizeof(m_PointerShape));
    m_pHWDevice = NULL;
    DbgPrint(TRACE_LEVEL_INFORMATION, ("<--- %s\n", __FUNCTION__));
}


QxlDod::~QxlDod(void)
{
    PAGED_CODE();
    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));
    CleanUp();
    delete m_pHWDevice;
    m_pHWDevice = NULL;
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
}

NTSTATUS QxlDod::CheckHardware()
{
    PAGED_CODE();

    NTSTATUS Status;
    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));


    // Get the Vendor & Device IDs on PCI system
    PCI_COMMON_HEADER Header = {0};
    ULONG BytesRead;

    Status = m_DxgkInterface.DxgkCbReadDeviceSpace(m_DxgkInterface.DeviceHandle,
                                                   DXGK_WHICHSPACE_CONFIG,
                                                   &Header,
                                                   0,
                                                   sizeof(Header),
                                                   &BytesRead);

    if (!NT_SUCCESS(Status))
    {
        DbgPrint(TRACE_LEVEL_ERROR, ("DxgkCbReadDeviceSpace failed with status 0x%X\n", Status));
        return Status;
    }

    Status = STATUS_GRAPHICS_DRIVER_MISMATCH;
    if (Header.VendorID == REDHAT_PCI_VENDOR_ID &&
        Header.DeviceID == 0x0100 &&
        Header.RevisionID == 4 &&
        m_VgaCompatible == 0)
    {
        Status = STATUS_SUCCESS;
    }

    DbgPrint(TRACE_LEVEL_INFORMATION, ("<--- %s returned with status 0x%X\n", __FUNCTION__, Status));
    return Status;
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
//CHECK ME!!!!!!!!!!!!!
    RtlCopyMemory(&m_DxgkInterface, pDxgkInterface, sizeof(m_DxgkInterface));
    RtlZeroMemory(m_CurrentModes, sizeof(m_CurrentModes));
//CHECK ME!!!!!!!!!!!!!
    m_CurrentModes[0].DispInfo.TargetId = D3DDDI_ID_UNINITIALIZED;
    // Get device information from OS.
    NTSTATUS Status = m_DxgkInterface.DxgkCbGetDeviceInformation(m_DxgkInterface.DeviceHandle, &m_DeviceInfo);
    if (!NT_SUCCESS(Status))
    {
        QXL_LOG_ASSERTION1("DxgkCbGetDeviceInformation failed with status 0x%X\n",
                           Status);
        return Status;
    }

    Status = CheckHardware();
    if (NT_SUCCESS(Status))
    {
        m_pHWDevice = new(PagedPool) QxlDevice(this);
    }
    else
    {
        m_pHWDevice = new(PagedPool) VgaDevice(this);
    }

    Status = m_pHWDevice->HWInit(m_DeviceInfo.TranslatedResourceList, &m_CurrentModes[0].DispInfo);
    if (!NT_SUCCESS(Status))
    {
        DbgPrint(TRACE_LEVEL_ERROR, ("HWInit failed with status 0x%X\n", Status));
        return Status;
    }

    Status = RegisterHWInfo(m_pHWDevice->GetId());
    if (!NT_SUCCESS(Status))
    {
        QXL_LOG_ASSERTION1("RegisterHWInfo failed with status 0x%X\n",
                           Status);
        return Status;
    }

    if (m_pHWDevice->GetId() == 0)
    {
         Status = m_DxgkInterface.DxgkCbAcquirePostDisplayOwnership(m_DxgkInterface.DeviceHandle, &(m_CurrentModes[0].DispInfo));
    }

    if (!NT_SUCCESS(Status) )
    {
        DbgPrint(TRACE_LEVEL_ERROR, ("DxgkCbAcquirePostDisplayOwnership failed with status 0x%X Width = %d\n",
                           Status, m_CurrentModes[0].DispInfo.Width));
        return STATUS_UNSUCCESSFUL;
    }

    if (m_CurrentModes[0].DispInfo.Width == 0)
    {
        m_CurrentModes[0].DispInfo.Width = MIN_WIDTH_SIZE; 
        m_CurrentModes[0].DispInfo.Height = MIN_HEIGHT_SIZE;
        m_CurrentModes[0].DispInfo.Pitch = BPPFromPixelFormat(D3DDDIFMT_R8G8B8) / 8;
        m_CurrentModes[0].DispInfo.ColorFormat = D3DDDIFMT_R8G8B8;
        m_CurrentModes[0].DispInfo.TargetId = 0;
    }

   *pNumberOfViews = MAX_VIEWS;
   *pNumberOfChildren = MAX_CHILDREN;
    m_Flags.DriverStarted = TRUE;
    DbgPrint(TRACE_LEVEL_INFORMATION, ("<--- %s\n", __FUNCTION__));
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

    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));
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
        return m_pHWDevice->SetPowerState(DevicePowerState, &(m_CurrentModes[0].DispInfo));
    }
    // TODO: This is where the specified monitor should be powered up/down
    
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
    ULONG DeviceId = m_pHWDevice->GetId();
    QXL_ASSERT(ChildRelationsCount <= MAX_CHILDREN);

    for (UINT ChildIndex = 0; ChildIndex < ChildRelationsCount; ++ChildIndex)
    {
        pChildRelations[ChildIndex].ChildDeviceType = TypeVideoOutput;
        pChildRelations[ChildIndex].ChildCapabilities.HpdAwareness = (DeviceId == 0) ? HpdAwarenessAlwaysConnected : HpdAwarenessInterruptible;
        pChildRelations[ChildIndex].ChildCapabilities.Type.VideoOutput.InterfaceTechnology = (DeviceId == 0) ? D3DKMDT_VOT_INTERNAL : D3DKMDT_VOT_HD15;
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
            if (!pQueryAdapterInfo->OutputDataSize/* < sizeof(DXGK_DRIVERCAPS)*/)
            {
                DbgPrint(TRACE_LEVEL_ERROR, ("pQueryAdapterInfo->OutputDataSize (0x%u) is smaller than sizeof(DXGK_DRIVERCAPS) (0x%u)\n", pQueryAdapterInfo->OutputDataSize, sizeof(DXGK_DRIVERCAPS)));
                return STATUS_BUFFER_TOO_SMALL;
            }

            DXGK_DRIVERCAPS* pDriverCaps = (DXGK_DRIVERCAPS*)pQueryAdapterInfo->pOutputData;

            RtlZeroMemory(pDriverCaps, pQueryAdapterInfo->OutputDataSize/*sizeof(DXGK_DRIVERCAPS)*/);

            pDriverCaps->WDDMVersion = DXGKDDI_WDDMv1_2;
            pDriverCaps->HighestAcceptableAddress.QuadPart = -1;
            pDriverCaps->SupportNonVGA = TRUE;

            if (m_pHWDevice->EnablePointer() && m_PointerCaps) {
                pDriverCaps->MaxPointerWidth  = POINTER_SIZE;
                pDriverCaps->MaxPointerHeight = POINTER_SIZE;
                pDriverCaps->PointerCaps.Monochrome = m_PointerCaps & 0x00000001;
                pDriverCaps->PointerCaps.Color = m_PointerCaps & 0x00000002;
                pDriverCaps->PointerCaps.MaskedColor = m_PointerCaps & 0x00000004;
            }
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
    return m_pHWDevice->SetPointerPosition(pSetPointerPosition);
}

// Basic Sample Display Driver does not support hardware cursors, and reports such
// in QueryAdapterInfo. Therefore this function should never be called.
NTSTATUS QxlDod::SetPointerShape(_In_ CONST DXGKARG_SETPOINTERSHAPE* pSetPointerShape)
{
    PAGED_CODE();
    QXL_ASSERT(pSetPointerShape != NULL);

    DbgPrint(TRACE_LEVEL_INFORMATION, ("<---> %s Height = %d, Width = %d, XHot= %d, YHot = %d SourceId = %d\n", 
        __FUNCTION__, pSetPointerShape->Height, pSetPointerShape->Width, pSetPointerShape->XHot, pSetPointerShape->YHot, pSetPointerShape->VidPnSourceId));
    return m_pHWDevice->SetPointerShape(pSetPointerShape);
}

NTSTATUS QxlDod::Escape(_In_ CONST DXGKARG_ESCAPE* pEscape)
{
    PAGED_CODE();
    NTSTATUS Status = STATUS_SUCCESS;
    QXL_ASSERT(pEscape != NULL);

    DbgPrint(TRACE_LEVEL_VERBOSE, ("<---> %s Flags = %d\n", __FUNCTION__, pEscape->Flags));

    Status = m_pHWDevice->Escape(pEscape);
    if (Status == STATUS_SUCCESS)
    {
        DXGK_CHILD_STATUS ChildStatus;
        ChildStatus.Type = StatusConnection;
        ChildStatus.ChildUid = 0;
        ChildStatus.HotPlug.Connected = FALSE;
        Status = m_DxgkInterface.DxgkCbIndicateChildStatus(m_DxgkInterface.DeviceHandle, &ChildStatus);
        if (Status == STATUS_SUCCESS)
        {
            ChildStatus.HotPlug.Connected = TRUE;
            Status = m_DxgkInterface.DxgkCbIndicateChildStatus(m_DxgkInterface.DeviceHandle, &ChildStatus);
        }
    }
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<---> %s Status = %x\n", __FUNCTION__, Status));
    return Status;
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
        Status = m_pHWDevice->ExecutePresentDisplayOnly(
                            pDst,
                            DstBitPerPixel,
                            (BYTE*)pPresentDisplayOnly->pSource,
                            pPresentDisplayOnly->BytesPerPixel,
                            pPresentDisplayOnly->Pitch,
                            pPresentDisplayOnly->NumMoves,
                            pPresentDisplayOnly->pMoves,
                            pPresentDisplayOnly->NumDirtyRects,
                            pPresentDisplayOnly->pDirtyRect,
                            RotationNeededByFb,
                            &m_CurrentModes[0]);
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
    m_pHWDevice->BlackOutScreen(&m_CurrentModes[SourceId]);

    *pDisplayInfo = m_CurrentModes[SourceId].DispInfo;

    return StopDevice();
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
    for (ULONG idx = 0; idx < m_pHWDevice->GetModeCount(); ++idx)
    {
        // Create new mode info that will be populated
        D3DKMDT_VIDPN_SOURCE_MODE* pVidPnSourceModeInfo = NULL;
        PVIDEO_MODE_INFORMATION pModeInfo = m_pHWDevice->GetModeInfo(idx);
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
        pVidPnSourceModeInfo->Format.Graphics.PrimSurfSize.cx = pModeInfo->VisScreenWidth;
        pVidPnSourceModeInfo->Format.Graphics.PrimSurfSize.cy = pModeInfo->VisScreenHeight;
        pVidPnSourceModeInfo->Format.Graphics.VisibleRegionSize = pVidPnSourceModeInfo->Format.Graphics.PrimSurfSize;
        pVidPnSourceModeInfo->Format.Graphics.Stride = pModeInfo->ScreenStride;
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
//FIXME !!!!!!
    for (UINT ModeIndex = 0; ModeIndex < m_pHWDevice->GetModeCount(); ++ModeIndex)
    {
        PVIDEO_MODE_INFORMATION pModeInfo = m_pHWDevice->GetModeInfo(SourceId);
        pVidPnTargetModeInfo = NULL;
        Status = pVidPnTargetModeSetInterface->pfnCreateNewModeInfo(hVidPnTargetModeSet, &pVidPnTargetModeInfo);
        if (!NT_SUCCESS(Status))
        {
            // If failed to create a new mode info, mode doesn't need to be released since it was never created
            DbgPrint(TRACE_LEVEL_ERROR, ("pfnCreateNewModeInfo failed with Status = 0x%I64x, hVidPnTargetModeSet = 0x%I64x", Status, hVidPnTargetModeSet));
            return Status;
        }
        pVidPnTargetModeInfo->VideoSignalInfo.VideoStandard = D3DKMDT_VSS_OTHER;
        pVidPnTargetModeInfo->VideoSignalInfo.TotalSize.cx = pModeInfo->VisScreenWidth;
        pVidPnTargetModeInfo->VideoSignalInfo.TotalSize.cy = pModeInfo->VisScreenHeight;
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
    PVIDEO_MODE_INFORMATION pVbeModeInfo = NULL;

    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));

    Status = pRecommendMonitorModes->pMonitorSourceModeSetInterface->pfnCreateNewModeInfo(pRecommendMonitorModes->hMonitorSourceModeSet, &pMonitorSourceMode);
    if (!NT_SUCCESS(Status))
    {
        // If failed to create a new mode info, mode doesn't need to be released since it was never created
        DbgPrint(TRACE_LEVEL_ERROR, ("pfnCreateNewModeInfo failed with Status = 0x%X, hMonitorSourceModeSet = 0x%I64x", Status, pRecommendMonitorModes->hMonitorSourceModeSet));
        return Status;
    }

    pVbeModeInfo = m_pHWDevice->GetModeInfo(m_pHWDevice->GetCurrentModeIndex());

    // Since we don't know the real monitor timing information, just use the current display mode (from the POST device) with unknown frequencies
    pMonitorSourceMode->VideoSignalInfo.VideoStandard = D3DKMDT_VSS_OTHER;
    pMonitorSourceMode->VideoSignalInfo.TotalSize.cx = pVbeModeInfo->VisScreenWidth;
    pMonitorSourceMode->VideoSignalInfo.TotalSize.cy = pVbeModeInfo->VisScreenHeight;
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
    for (UINT Idx = 0; Idx < m_pHWDevice->GetModeCount(); ++Idx)
    {
        // There is only one source format supported by display-only drivers, but more can be added in a 
        // full WDDM driver if the hardware supports them

        pVbeModeInfo = m_pHWDevice->GetModeInfo(Idx);
        // TODO: add routine for filling Monitor modepMonitorSourceMode = NULL;
        Status = pRecommendMonitorModes->pMonitorSourceModeSetInterface->pfnCreateNewModeInfo(pRecommendMonitorModes->hMonitorSourceModeSet, &pMonitorSourceMode);
        if (!NT_SUCCESS(Status))
        {
            // If failed to create a new mode info, mode doesn't need to be released since it was never created
            DbgPrint(TRACE_LEVEL_ERROR, ("pfnCreateNewModeInfo failed with Status = 0x%I64x, hMonitorSourceModeSet = 0x%I64x", Status, pRecommendMonitorModes->hMonitorSourceModeSet));
            return Status;
        }

        
        DbgPrint(TRACE_LEVEL_INFORMATION, ("%s: add pref mode, dimensions %ux%u, taken from DxgkCbAcquirePostDisplayOwnership at StartDevice\n", __FUNCTION__,
                   pVbeModeInfo->VisScreenWidth, pVbeModeInfo->VisScreenHeight));

        // Since we don't know the real monitor timing information, just use the current display mode (from the POST device) with unknown frequencies
        pMonitorSourceMode->VideoSignalInfo.VideoStandard = D3DKMDT_VSS_OTHER;
        pMonitorSourceMode->VideoSignalInfo.TotalSize.cx = pVbeModeInfo->VisScreenWidth;
        pMonitorSourceMode->VideoSignalInfo.TotalSize.cy = pVbeModeInfo->VisScreenHeight;
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
            m_pHWDevice->BlackOutScreen(&m_CurrentModes[SourceId]);
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
    DbgPrint(TRACE_LEVEL_INFORMATION, ("---> %s SourceId = %d\n", __FUNCTION__, pPath->VidPnSourceId));

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
        m_pHWDevice->BlackOutScreen(&m_CurrentModes[pPath->VidPnSourceId]);

        // Mark that the next present should be fullscreen so the screen doesn't go from black to actual pixels one dirty rect at a time.
        pCurrentBddMode->Flags.FullscreenPresent = TRUE;
        for (USHORT ModeIndex = 0; ModeIndex < m_pHWDevice->GetModeCount(); ++ModeIndex)
        {
             PVIDEO_MODE_INFORMATION pModeInfo = m_pHWDevice->GetModeInfo(ModeIndex);
             DbgPrint(TRACE_LEVEL_INFORMATION, ("%d\t%d x %d\t%d x %d\n", ModeIndex, pCurrentBddMode->DispInfo.Width, pCurrentBddMode->DispInfo.Height,
                                pModeInfo->VisScreenWidth, pModeInfo->VisScreenHeight));
             if (pCurrentBddMode->DispInfo.Width == pModeInfo->VisScreenWidth &&
                 pCurrentBddMode->DispInfo.Height == pModeInfo->VisScreenHeight )
             {
                 Status = m_pHWDevice->SetCurrentMode(m_pHWDevice->GetModeNumber(ModeIndex));
                 if (NT_SUCCESS(Status))
                 {
                     m_pHWDevice->SetCurrentModeIndex(ModeIndex);
                 }
                 break;
             }
        }
    }

    DbgPrint(TRACE_LEVEL_INFORMATION, ("<--- %s\n", __FUNCTION__));
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
    m_pHWDevice->DpcRoutine(&m_DxgkInterface);
    m_DxgkInterface.DxgkCbNotifyDpc((HANDLE)m_DxgkInterface.DeviceHandle);
    DbgPrint(TRACE_LEVEL_INFORMATION, ("<--- %s\n", __FUNCTION__));
}

BOOLEAN QxlDod::InterruptRoutine(_In_  ULONG MessageNumber)
{
    DbgPrint(TRACE_LEVEL_INFORMATION, ("<--> 0 %s\n", __FUNCTION__));
    return m_pHWDevice->InterruptRoutine(&m_DxgkInterface, MessageNumber);
}

VOID QxlDod::ResetDevice(VOID)
{
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<---> %s\n", __FUNCTION__));
    m_pHWDevice->ResetDevice();
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

NTSTATUS QxlDod::RegisterHWInfo(ULONG Id)
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

    UNICODE_STRING ValueQxlDeviceID;
    RtlInitUnicodeString(&ValueQxlDeviceID, L"QxlDeviceID");
    DWORD DeviceId = Id; // BDD has no access to video memory
    Status = ZwSetValueKey(DevInstRegKeyHandle,
                           &ValueQxlDeviceID,
                           0,
                           REG_BINARY,
                           &DeviceId,
                           sizeof(DeviceId));
    if (!NT_SUCCESS(Status))
    {
        DbgPrint(TRACE_LEVEL_ERROR, ("ZwSetValueKey for MemorySize failed with Status: 0x%X\n", Status));
        return Status;
    }

    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
    return Status;
}

NTSTATUS QxlDod::ReadConfiguration()
{
	PAGED_CODE();

	NTSTATUS Status;
	DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));

	HANDLE DevInstRegKeyHandle;
	Status = IoOpenDeviceRegistryKey(m_pPhysicalDevice, PLUGPLAY_REGKEY_DRIVER, KEY_SET_VALUE, &DevInstRegKeyHandle);
	if (!NT_SUCCESS(Status))
	{
		DbgPrint(TRACE_LEVEL_ERROR, ("IoOpenDeviceRegistryKey failed for PDO: 0x%I64x, Status: 0x%I64x", m_pPhysicalDevice, Status));
		return Status;
	}
	UNICODE_STRING ValueName;
	UCHAR Buffer[sizeof(KEY_VALUE_PARTIAL_INFORMATION)+sizeof(ULONG)];
	PKEY_VALUE_PARTIAL_INFORMATION Value = (PKEY_VALUE_PARTIAL_INFORMATION)Buffer;
	ULONG ValueLength = sizeof(Buffer);
	ULONG ResultLength;
	ULONG Length;

	RtlInitUnicodeString(&ValueName, L"VgaCompatible");

	Status = ZwQueryValueKey(DevInstRegKeyHandle,
		&ValueName,
		KeyValuePartialInformation,
		Value,
		ValueLength,
		&ResultLength);

	if (NT_SUCCESS(Status)) {
		m_VgaCompatible = *(PULONG)(Value->Data);
	}

	RtlInitUnicodeString(&ValueName, L"PointerCaps");

	Status = ZwQueryValueKey(DevInstRegKeyHandle,
		&ValueName,
		KeyValuePartialInformation,
		Value,
		ValueLength,
		&ResultLength);

	if (NT_SUCCESS(Status)) {
		m_PointerCaps = *(PULONG)(Value->Data);
	}

	DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
	return Status;
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




// HW specific code

VOID GetPitches(_In_ CONST BLT_INFO* pBltInfo, _Out_ LONG* pPixelPitch, _Out_ LONG* pRowPitch)
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

BYTE* GetRowStart(_In_ CONST BLT_INFO* pBltInfo, CONST RECT* pRect)
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

VOID CopyBitsGeneric(
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


VOID CopyBits32_32(
    BLT_INFO* pDst,
    CONST BLT_INFO* pSrc,
    UINT  NumRects,
    _In_reads_(NumRects) CONST RECT *pRects)
{
    NT_ASSERT((pDst->BitsPerPel == 32) &&
              (pSrc->BitsPerPel == 32));
    NT_ASSERT((pDst->Rotation == D3DKMDT_VPPR_IDENTITY) &&
              (pSrc->Rotation == D3DKMDT_VPPR_IDENTITY));

    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));

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


VOID BltBits (
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

VgaDevice::VgaDevice(_In_ QxlDod* pQxlDod)
{
    m_pQxlDod = pQxlDod;
    m_ModeInfo = NULL;
    m_ModeCount = 0;
    m_ModeNumbers = NULL;
    m_CurrentMode = 0;
    m_Id = 0;
}

VgaDevice::~VgaDevice(void)
{
    delete [] reinterpret_cast<BYTE*>(m_ModeInfo);
    delete [] reinterpret_cast<BYTE*>(m_ModeNumbers);
    m_ModeInfo = NULL;
    m_ModeNumbers = NULL;
    m_CurrentMode = 0;
    m_ModeCount = 0;
    m_Id = 0;
}

BOOL VgaDevice::SetVideoModeInfo(UINT Idx, PVBE_MODEINFO pModeInfo)
{
    PVIDEO_MODE_INFORMATION pMode = NULL;
    PAGED_CODE();

    pMode = &m_ModeInfo[Idx];
    pMode->Length = sizeof(VIDEO_MODE_INFORMATION);
    pMode->ModeIndex = Idx;//m_ModeNumbers[Idx];
    pMode->VisScreenWidth = pModeInfo->XResolution;
    pMode->VisScreenHeight = pModeInfo->YResolution;
    pMode->ScreenStride = pModeInfo->LinBytesPerScanLine;
    pMode->NumberOfPlanes = pModeInfo->NumberOfPlanes;
    pMode->BitsPerPlane = pModeInfo->BitsPerPixel / pModeInfo->NumberOfPlanes;
    pMode->Frequency = 60;
    pMode->XMillimeter = pModeInfo->XResolution * 254 / 720;
    pMode->YMillimeter = pModeInfo->YResolution * 254 / 720;

    if (pModeInfo->BitsPerPixel == 15 && pModeInfo->NumberOfPlanes == 1)
    {
        pMode->BitsPerPlane = 16;
    }

    pMode->NumberRedBits = pModeInfo->LinRedMaskSize;
    pMode->NumberGreenBits = pModeInfo->LinGreenMaskSize;
    pMode->NumberBlueBits = pModeInfo->LinBlueMaskSize;
    pMode->RedMask = ((1 << pModeInfo->LinRedMaskSize) - 1) << pModeInfo->LinRedFieldPosition;
    pMode->GreenMask = ((1 << pModeInfo->LinGreenMaskSize) - 1) << pModeInfo->LinGreenFieldPosition;
    pMode->BlueMask = ((1 << pModeInfo->LinBlueMaskSize) - 1) << pModeInfo->LinBlueFieldPosition;

    pMode->AttributeFlags = VIDEO_MODE_COLOR | VIDEO_MODE_GRAPHICS | VIDEO_MODE_NO_OFF_SCREEN;
    pMode->VideoMemoryBitmapWidth = pModeInfo->XResolution;
    pMode->VideoMemoryBitmapHeight = pModeInfo->YResolution;
    pMode->DriverSpecificAttributeFlags = 0;

    return TRUE;
}

NTSTATUS VgaDevice::GetModeList(DXGK_DISPLAY_INFORMATION* pDispInfo)
{
    PAGED_CODE();
    USHORT m_Segment;
    USHORT m_Offset;
    USHORT ModeCount;
    ULONG SuitableModeCount;
    USHORT ModeTemp;
    USHORT CurrentMode;
    VBE_INFO VbeInfo = {0};
    ULONG Length;
    VBE_MODEINFO tmpModeInfo;
    UINT Height = pDispInfo->Height;
    UINT Width = pDispInfo->Width;
    UINT BitsPerPixel = BPPFromPixelFormat(pDispInfo->ColorFormat);
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
    DbgPrint(TRACE_LEVEL_INFORMATION, ("x86BiosAllocateBuffer 0x%x (%x.%x)\n", VbeInfo.VideoModePtr, m_Segment, m_Offset));

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

    DbgPrint(TRACE_LEVEL_INFORMATION, ("VBE BIOS Present (%d.%d, %8ld Kb)\n", VbeInfo.Version / 0x100, VbeInfo.Version & 0xFF, VbeInfo.TotalMemory * 64));
    DbgPrint(TRACE_LEVEL_INFORMATION, ("Capabilities = 0x%x\n", VbeInfo.Capabilities));
    DbgPrint(TRACE_LEVEL_INFORMATION, ("VideoModePtr = 0x%x (0x%x.0x%x)\n", VbeInfo.VideoModePtr, HIWORD( VbeInfo.VideoModePtr), LOWORD( VbeInfo.VideoModePtr)));
    DbgPrint(TRACE_LEVEL_INFORMATION, ("pDispInfo = %p %dx%d@%d\n", pDispInfo, Width, Height, BitsPerPixel));

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

    DbgPrint(TRACE_LEVEL_INFORMATION, ("ModeCount %d\n", ModeCount));

    m_ModeInfo = reinterpret_cast<PVIDEO_MODE_INFORMATION> (new (PagedPool) BYTE[sizeof (VIDEO_MODE_INFORMATION) * ModeCount]);
    m_ModeNumbers = reinterpret_cast<PUSHORT> (new (PagedPool)  BYTE [sizeof (USHORT) * ModeCount]);
    m_CurrentMode = 0;
    DbgPrint(TRACE_LEVEL_INFORMATION, ("m_ModeInfo = 0x%p, m_ModeNumbers = 0x%p\n", m_ModeInfo, m_ModeNumbers));
    if (Width == 0 || Height == 0 || BitsPerPixel != VGA_BPP)
    {
        Width = MIN_WIDTH_SIZE;
        Height = MIN_HEIGHT_SIZE;
        BitsPerPixel = VGA_BPP;
    }
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
                    &tmpModeInfo,
                    sizeof(VBE_MODEINFO));

        DbgPrint(TRACE_LEVEL_INFORMATION, ("ModeTemp = 0x%X %dx%d@%d\n", ModeTemp, tmpModeInfo.XResolution, tmpModeInfo.YResolution, tmpModeInfo.BitsPerPixel));

        if (tmpModeInfo.XResolution >= Width &&
            tmpModeInfo.YResolution >= Height &&
            tmpModeInfo.BitsPerPixel == BitsPerPixel &&
            tmpModeInfo.PhysBasePtr != 0)
        {
            m_ModeNumbers[SuitableModeCount] = ModeTemp;
            SetVideoModeInfo(SuitableModeCount, &tmpModeInfo);
            if (tmpModeInfo.XResolution == MIN_WIDTH_SIZE &&
                tmpModeInfo.YResolution == MIN_HEIGHT_SIZE)
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
    DbgPrint(TRACE_LEVEL_INFORMATION, ("ModeCount filtered %d\n", m_ModeCount));
    for (ULONG idx = 0; idx < m_ModeCount; idx++)
    {
        DbgPrint(TRACE_LEVEL_INFORMATION, ("type %x, XRes = %d, YRes = %d, BPP = %d\n",
                                    m_ModeNumbers[idx],
                                    m_ModeInfo[idx].VisScreenWidth,
                                    m_ModeInfo[idx].VisScreenHeight,
                                    m_ModeInfo[idx].BitsPerPlane));
    }

    if (m_Segment != 0)
    {
        x86BiosFreeBuffer (m_Segment, m_Offset);
    }
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
    return Status;
}

NTSTATUS VgaDevice::QueryCurrentMode(PVIDEO_MODE RequestedMode)
{
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
    NTSTATUS Status = STATUS_SUCCESS;
    UNREFERENCED_PARAMETER(RequestedMode);

//    PVBE_MODEINFO VBEMode = &m_ModeInfo[m_CurrentMode];
    return Status;
}

NTSTATUS VgaDevice::SetCurrentMode(ULONG Mode)
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
    DbgPrint(TRACE_LEVEL_INFORMATION, ("<--- %s\n", __FUNCTION__));
    return Status;
}

NTSTATUS VgaDevice::GetCurrentMode(ULONG* pMode)
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

NTSTATUS VgaDevice::HWInit(PCM_RESOURCE_LIST pResList, DXGK_DISPLAY_INFORMATION* pDispInfo)
{
    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));
    UNREFERENCED_PARAMETER(pResList);
    UNREFERENCED_PARAMETER(pDispInfo);
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
    return GetModeList(pDispInfo);
}

NTSTATUS VgaDevice::HWClose(void)
{
    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
    return STATUS_SUCCESS;
}

NTSTATUS VgaDevice::SetPowerState(_In_  DEVICE_POWER_STATE DevicePowerState, DXGK_DISPLAY_INFORMATION* pDispInfo)
{
    DbgPrint(TRACE_LEVEL_INFORMATION, ("---> %s\n", __FUNCTION__));

    X86BIOS_REGISTERS regs = {0};
    regs.Eax = 0x4F10;
    regs.Ebx = 0;
    switch (DevicePowerState)
    {
        case PowerDeviceUnspecified: 
        case PowerDeviceD0: regs.Ebx |= 0x1; break;
        case PowerDeviceD1:
        case PowerDeviceD2: 
        case PowerDeviceD3: regs.Ebx |= 0x400; break;
    }
    if (!x86BiosCall (0x10, &regs))
    {
        DbgPrint(TRACE_LEVEL_ERROR, ("x86BiosCall failed\n"));
        return STATUS_UNSUCCESSFUL;
    }
    DbgPrint(TRACE_LEVEL_INFORMATION, ("<--- %s\n", __FUNCTION__));
    return STATUS_SUCCESS;
}


NTSTATUS
VgaDevice::ExecutePresentDisplayOnly(
    _In_ BYTE*             DstAddr,
    _In_ UINT              DstBitPerPixel,
    _In_ BYTE*             SrcAddr,
    _In_ UINT              SrcBytesPerPixel,
    _In_ LONG              SrcPitch,
    _In_ ULONG             NumMoves,
    _In_ D3DKMT_MOVE_RECT* Moves,
    _In_ ULONG             NumDirtyRects,
    _In_ RECT*             DirtyRect,
    _In_ D3DKMDT_VIDPN_PRESENT_PATH_ROTATION Rotation,
    _In_ const CURRENT_BDD_MODE* pModeCur)
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

//    const CURRENT_BDD_MODE* pModeCur = &m_CurrentModes[0];
    ctx->DstAddr          = DstAddr;
    ctx->DstBitPerPixel   = DstBitPerPixel;
    ctx->DstStride        = pModeCur->DispInfo.Pitch;
    ctx->SrcWidth         = pModeCur->SrcModeWidth;
    ctx->SrcHeight        = pModeCur->SrcModeHeight;
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
//        POINT*   pSourcePoint = &ctx->Moves[i].SourcePoint;
        RECT*    pDestRect = &ctx->Moves[i].DestRect;

//        DbgPrint(TRACE_LEVEL_FATAL, ("--- %d SourcePoint.x = %ld, SourcePoint.y = %ld, DestRect.bottom = %ld, DestRect.left = %ld, DestRect.right = %ld, DestRect.top = %ld\n", 
//            i , pSourcePoint->x, pSourcePoint->y, pDestRect->bottom, pDestRect->left, pDestRect->right, pDestRect->top));

        BltBits(&DstBltInfo,
        &SrcBltInfo,
        1, // NumRects
        pDestRect);
    }

    // Copy all the dirty rects from source image to video frame buffer.
    for (UINT i = 0; i < ctx->NumDirtyRects; i++)
    {
        RECT*    pDirtyRect = &ctx->DirtyRect[i];
//        DbgPrint(TRACE_LEVEL_FATAL, ("--- %d pDirtyRect->bottom = %ld, pDirtyRect->left = %ld, pDirtyRect->right = %ld, pDirtyRect->top = %ld\n", 
//            i, pDirtyRect->bottom, pDirtyRect->left, pDirtyRect->right, pDirtyRect->top));

        BltBits(&DstBltInfo,
        &SrcBltInfo,
        1, // NumRects
        pDirtyRect);
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

VOID VgaDevice::BlackOutScreen(CURRENT_BDD_MODE* pCurrentBddMod)
{
    PAGED_CODE();

    UINT ScreenHeight = pCurrentBddMod->DispInfo.Height;
    UINT ScreenPitch = pCurrentBddMod->DispInfo.Pitch;

    PHYSICAL_ADDRESS NewPhysAddrStart = pCurrentBddMod->DispInfo.PhysicAddress;
    PHYSICAL_ADDRESS NewPhysAddrEnd;
    NewPhysAddrEnd.QuadPart = NewPhysAddrStart.QuadPart + (ScreenHeight * ScreenPitch);

    if (pCurrentBddMod->Flags.FrameBufferIsActive)
    {
        BYTE* MappedAddr = reinterpret_cast<BYTE*>(pCurrentBddMod->FrameBuffer.Ptr);

        // Zero any memory at the start that hasn't been zeroed recently
        if (NewPhysAddrStart.QuadPart < pCurrentBddMod->ZeroedOutStart.QuadPart)
        {
            if (NewPhysAddrEnd.QuadPart < pCurrentBddMod->ZeroedOutStart.QuadPart)
            {
                // No overlap
                RtlZeroMemory(MappedAddr, ScreenHeight * ScreenPitch);
            }
            else
            {
                RtlZeroMemory(MappedAddr, (UINT)(pCurrentBddMod->ZeroedOutStart.QuadPart - NewPhysAddrStart.QuadPart));
            }
        }

        // Zero any memory at the end that hasn't been zeroed recently
        if (NewPhysAddrEnd.QuadPart > pCurrentBddMod->ZeroedOutEnd.QuadPart)
        {
            if (NewPhysAddrStart.QuadPart > pCurrentBddMod->ZeroedOutEnd.QuadPart)
            {
                // No overlap
                // NOTE: When actual pixels were the most recent thing drawn, ZeroedOutStart & ZeroedOutEnd will both be 0
                // and this is the path that will be used to black out the current screen.
                RtlZeroMemory(MappedAddr, ScreenHeight * ScreenPitch);
            }
            else
            {
                RtlZeroMemory(MappedAddr, (UINT)(NewPhysAddrEnd.QuadPart - pCurrentBddMod->ZeroedOutEnd.QuadPart));
            }
        }
    }

    pCurrentBddMod->ZeroedOutStart.QuadPart = NewPhysAddrStart.QuadPart;
    pCurrentBddMod->ZeroedOutEnd.QuadPart = NewPhysAddrEnd.QuadPart;
}

BOOLEAN VgaDevice::InterruptRoutine(_In_ PDXGKRNL_INTERFACE pDxgkInterface, _In_  ULONG MessageNumber)
{
    UNREFERENCED_PARAMETER(pDxgkInterface);
    UNREFERENCED_PARAMETER(MessageNumber);
    return FALSE;
}

VOID VgaDevice::DpcRoutine(PVOID)
{
}

VOID VgaDevice::ResetDevice(VOID)
{
}

NTSTATUS  VgaDevice::SetPointerShape(_In_ CONST DXGKARG_SETPOINTERSHAPE* pSetPointerShape)
{
    UNREFERENCED_PARAMETER(pSetPointerShape);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS VgaDevice::SetPointerPosition(_In_ CONST DXGKARG_SETPOINTERPOSITION* pSetPointerPosition)
{
    UNREFERENCED_PARAMETER(pSetPointerPosition);
    return STATUS_SUCCESS;
}

NTSTATUS VgaDevice::Escape(_In_ CONST DXGKARG_ESCAPE* pEscap)
{
    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
    return STATUS_NOT_IMPLEMENTED;
}

QxlDevice::QxlDevice(_In_ QxlDod* pQxlDod)
{
    m_pQxlDod = pQxlDod;
    m_ModeInfo = NULL;
    m_ModeCount = 0;
    m_ModeNumbers = NULL;
    m_CurrentMode = 0;
    m_CustomMode = 0;
    m_FreeOutputs = 0;
    m_Pending = 0;
}

QxlDevice::~QxlDevice(void)
{
    delete [] reinterpret_cast<BYTE*>(m_ModeInfo);
    delete [] reinterpret_cast<BYTE*>(m_ModeNumbers);
    m_ModeInfo = NULL;
    m_ModeNumbers = NULL;
    m_CurrentMode = 0;
    m_ModeCount = 0;
}

BOOL QxlDevice::SetVideoModeInfo(UINT Idx, QXLMode* pModeInfo)
{
    PVIDEO_MODE_INFORMATION pMode = NULL;
    ULONG color_bits;
    PAGED_CODE();

    pMode = &m_ModeInfo[Idx];
    pMode->Length = sizeof(VIDEO_MODE_INFORMATION);
    pMode->ModeIndex = Idx;//m_ModeNumbers[Idx];
    pMode->VisScreenWidth = pModeInfo->x_res;
    pMode->VisScreenHeight = pModeInfo->y_res;
    pMode->ScreenStride = pModeInfo->stride;
    pMode->NumberOfPlanes = 1;
    pMode->BitsPerPlane = pModeInfo->bits;
    pMode->Frequency = 100;
    pMode->XMillimeter = pModeInfo->x_mili;
    pMode->YMillimeter = pModeInfo->y_mili;
    color_bits = (pModeInfo->bits == 16) ? 5 : 8;
    pMode->NumberRedBits = color_bits;
    pMode->NumberGreenBits = color_bits;
    pMode->NumberBlueBits = color_bits;

    pMode->BlueMask = (1 << color_bits) - 1;
    pMode->GreenMask = pMode->BlueMask << color_bits;
    pMode->RedMask = pMode->GreenMask << color_bits;

    pMode->AttributeFlags = VIDEO_MODE_COLOR | VIDEO_MODE_GRAPHICS;
    pMode->VideoMemoryBitmapWidth = pModeInfo->x_res;
    pMode->VideoMemoryBitmapHeight = pModeInfo->y_res;
    pMode->DriverSpecificAttributeFlags = pModeInfo->orientation;
    return TRUE;
}

void QxlDevice::UpdateVideoModeInfo(UINT Idx, UINT xres, UINT yres, UINT bpp)
{
    PVIDEO_MODE_INFORMATION pMode = NULL;
    UINT bytes_pp = (bpp + 7) / 8;
    ULONG color_bits;
    PAGED_CODE();

    pMode = &m_ModeInfo[Idx];
    pMode->VisScreenWidth = xres;
    pMode->VisScreenHeight = yres;
    pMode->ScreenStride = (xres * bytes_pp + 3) & ~0x3;
    pMode->BitsPerPlane = bpp;
    color_bits = (bpp == 16) ? 5 : 8;
    pMode->NumberRedBits = color_bits;
    pMode->NumberGreenBits = color_bits;
    pMode->NumberBlueBits = color_bits;

    pMode->BlueMask = (1 << color_bits) - 1;
    pMode->GreenMask = pMode->BlueMask << color_bits;
    pMode->RedMask = pMode->GreenMask << color_bits;
}

NTSTATUS QxlDevice::GetModeList(DXGK_DISPLAY_INFORMATION* pDispInfo)
{
    PAGED_CODE();
    NTSTATUS Status = STATUS_SUCCESS;
    QXLModes *modes;
    ULONG ModeCount;
    USHORT SuitableModeCount;
    USHORT CurrentMode;
    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));

    modes = (QXLModes *)((UCHAR*)m_RomHdr + m_RomHdr->modes_offset);
    if (m_RomSize < m_RomHdr->modes_offset + sizeof(QXLModes) ||
        (ModeCount = modes->n_modes) == 0 || m_RomSize <
        m_RomHdr->modes_offset + sizeof(QXLModes) + ModeCount * sizeof(QXLMode)) {
        DbgPrint(TRACE_LEVEL_ERROR, ("%s: bad rom size\n", __FUNCTION__));
        return STATUS_UNSUCCESSFUL;
    }

    DbgPrint(TRACE_LEVEL_INFORMATION, ("%s: ModeCount = %d\n", __FUNCTION__, ModeCount));

    ModeCount += 2;
    m_ModeInfo = reinterpret_cast<PVIDEO_MODE_INFORMATION> (new (PagedPool) BYTE[sizeof (VIDEO_MODE_INFORMATION) * ModeCount]);
    m_ModeNumbers = reinterpret_cast<PUSHORT> (new (PagedPool)  BYTE [sizeof (USHORT) * ModeCount]);
    m_CurrentMode = 0;

    UINT Height = pDispInfo->Height;
    UINT Width = pDispInfo->Width;
    UINT BitsPerPixel = BPPFromPixelFormat(pDispInfo->ColorFormat);
    if (Width == 0 || Height == 0 || BitsPerPixel != QXL_BPP)
    {
        Width = MIN_WIDTH_SIZE;
        Height = MIN_HEIGHT_SIZE;
        BitsPerPixel = QXL_BPP;
    }

    for (CurrentMode = 0, SuitableModeCount = 0;
         CurrentMode < modes->n_modes;
         CurrentMode++)
    {

        QXLMode* tmpModeInfo = &modes->modes[CurrentMode];

        DbgPrint(TRACE_LEVEL_INFORMATION, ("%s: modes[%d] x_res = %d, y_res = %d, bits = %d BitsPerPixel = %d\n", __FUNCTION__, CurrentMode, tmpModeInfo->x_res, tmpModeInfo->y_res, tmpModeInfo->bits, BitsPerPixel));

        if (tmpModeInfo->x_res >= Width &&
            tmpModeInfo->y_res >= Height &&
            tmpModeInfo->bits == QXL_BPP)
        {
            m_ModeNumbers[SuitableModeCount] = SuitableModeCount;//CurrentMode;
            SetVideoModeInfo(SuitableModeCount, tmpModeInfo);
            if (tmpModeInfo->x_res == MIN_WIDTH_SIZE &&
                tmpModeInfo->y_res == MIN_HEIGHT_SIZE)
            {
                m_CurrentMode = SuitableModeCount;
            }
            SuitableModeCount++;
        }
    }

    if (SuitableModeCount == 0)
    {
        DbgPrint(TRACE_LEVEL_ERROR, ("No video modes supported\n"));
        Status = STATUS_UNSUCCESSFUL;
    }

    m_CustomMode = SuitableModeCount;
    for (CurrentMode = SuitableModeCount;
         CurrentMode < SuitableModeCount + 2;
         CurrentMode++)
    {
        m_ModeNumbers[CurrentMode] = CurrentMode;
        memcpy(&m_ModeInfo[CurrentMode], &m_ModeInfo[m_CurrentMode], sizeof(VIDEO_MODE_INFORMATION));
    }
    m_ModeCount = SuitableModeCount + 2;
    DbgPrint(TRACE_LEVEL_INFORMATION, ("ModeCount filtered %d\n", m_ModeCount));
    for (ULONG idx = 0; idx < GetModeCount(); idx++)
    {
        DbgPrint(TRACE_LEVEL_INFORMATION, ("type %x, XRes = %d, YRes = %d, BPP = %d\n",
                                    m_ModeNumbers[idx],
                                    m_ModeInfo[idx].VisScreenWidth,
                                    m_ModeInfo[idx].VisScreenHeight,
                                    m_ModeInfo[idx].BitsPerPlane));
    }

    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
    return Status;
}

NTSTATUS QxlDevice::QueryCurrentMode(PVIDEO_MODE RequestedMode)
{
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
    NTSTATUS Status = STATUS_SUCCESS;
    UNREFERENCED_PARAMETER(RequestedMode);
    return Status;
}

NTSTATUS QxlDevice::SetCurrentMode(ULONG Mode)
{
    DbgPrint(TRACE_LEVEL_INFORMATION, ("---> %s Mode = %x\n", __FUNCTION__, Mode));
    for (ULONG idx = 0; idx < GetModeCount(); idx++)
    {
        if (Mode == m_ModeNumbers[idx])
        {
            DestroyPrimarySurface();
            CreatePrimarySurface(&m_ModeInfo[idx]);
            return STATUS_SUCCESS;
        }
    }
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
    return STATUS_UNSUCCESSFUL;
}

NTSTATUS QxlDevice::GetCurrentMode(ULONG* pMode)
{
    NTSTATUS Status = STATUS_SUCCESS;
    DbgPrint(TRACE_LEVEL_INFORMATION, ("---> %s\n", __FUNCTION__));
    UNREFERENCED_PARAMETER(pMode);
    DbgPrint(TRACE_LEVEL_INFORMATION, ("<--- %s\n", __FUNCTION__));
    return Status;
}

NTSTATUS QxlDevice::SetPowerState(_In_ DEVICE_POWER_STATE DevicePowerState, DXGK_DISPLAY_INFORMATION* pDispInfo)
{
    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));
    switch (DevicePowerState)
    {
        case PowerDeviceUnspecified: 
        case PowerDeviceD0: QxlInit(pDispInfo); break;
        case PowerDeviceD1:
        case PowerDeviceD2: 
        case PowerDeviceD3: QxlClose(); break;
    }
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
    return STATUS_SUCCESS;
}

NTSTATUS QxlDevice::HWInit(PCM_RESOURCE_LIST pResList, DXGK_DISPLAY_INFORMATION* pDispInfo)
{
    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));
//    NTSTATUS Status = STATUS_SUCCESS;
    PDXGKRNL_INTERFACE pDxgkInterface = m_pQxlDod->GetDxgkInterrface();
    UINT pci_range = QXL_RAM_RANGE_INDEX;
    for (ULONG i = 0; i < pResList->Count; ++i)
    {
        PCM_FULL_RESOURCE_DESCRIPTOR pFullResDescriptor = &pResList->List[i];
        for (ULONG j = 0; j < pFullResDescriptor->PartialResourceList.Count; ++j)
        {
           PCM_PARTIAL_RESOURCE_DESCRIPTOR pResDescriptor = &pFullResDescriptor->PartialResourceList.PartialDescriptors[j];
           switch (pResDescriptor->Type)
           {
              case CmResourceTypePort:
              {
                   PVOID IoBase = NULL;
                   ULONG IoLength = pResDescriptor->u.Port.Length;
                   NTSTATUS Status = STATUS_SUCCESS;
                   DbgPrint(TRACE_LEVEL_INFORMATION, ("IO Port Info  [%08I64X-%08I64X]\n",
                                 pResDescriptor->u.Port.Start.QuadPart,
                                 pResDescriptor->u.Port.Start.QuadPart +
                                 pResDescriptor->u.Port.Length));
                   m_IoMapped = (pResDescriptor->Flags & CM_RESOURCE_PORT_IO) ? FALSE : TRUE;
                   if(m_IoMapped)
                   {
                         Status = pDxgkInterface->DxgkCbMapMemory(pDxgkInterface->DeviceHandle,
                                 pResDescriptor->u.Port.Start,
                                 IoLength,
                                 TRUE, /* IN BOOLEAN InIoSpace */
                                 FALSE, /* IN BOOLEAN MapToUserMode */
                                 MmNonCached, /* IN MEMORY_CACHING_TYPE CacheType */
                                 &IoBase /*OUT PVOID *VirtualAddress*/
                                 );
                         if (Status == STATUS_SUCCESS)
                         {
                             m_IoBase = (PUCHAR)IoBase;
                             m_IoSize = IoLength;
                         }
                         else
                         {
                               DbgPrint(TRACE_LEVEL_INFORMATION, ("DxgkCbMapMemor failed with status 0x%X\n", Status));
                         }
                   }
                   else
                   {
                       m_IoBase = (PUCHAR)(ULONG_PTR)pResDescriptor->u.Port.Start.QuadPart;
                       m_IoSize = pResDescriptor->u.Port.Length;
                   }
                   DbgPrint(TRACE_LEVEL_INFORMATION, ("io_base  [%X-%X]\n",
                                 m_IoBase,
                                 m_IoBase +
                                 m_IoSize));
              }
                   break;
              case CmResourceTypeInterrupt:
                   DbgPrint(TRACE_LEVEL_VERBOSE, ("Interrupt level: 0x%0x, Vector: 0x%0x\n",
                                 pResDescriptor->u.Interrupt.Level,
                                 pResDescriptor->u.Interrupt.Vector));
                   break;
              case CmResourceTypeMemory:
              {
                    PVOID MemBase = NULL;
                    ULONG MemLength = pResDescriptor->u.Memory.Length;
                    NTSTATUS Status = STATUS_SUCCESS;
                    DbgPrint( TRACE_LEVEL_INFORMATION, ("Memory mapped: (%x:%x) Length:(%x)\n",
                                 pResDescriptor->u.Memory.Start.LowPart,
                                 pResDescriptor->u.Memory.Start.HighPart,
                                 pResDescriptor->u.Memory.Length));
                    Status = pDxgkInterface->DxgkCbMapMemory(pDxgkInterface->DeviceHandle,
                                 pResDescriptor->u.Memory.Start,
                                 MemLength,
                                 FALSE, /* IN BOOLEAN InIoSpace */
                                 FALSE, /* IN BOOLEAN MapToUserMode */
                                 MmNonCached, /* IN MEMORY_CACHING_TYPE CacheType */
                                 &MemBase /*OUT PVOID *VirtualAddress*/
                                 );
                    if (Status == STATUS_SUCCESS)
                    {
                        switch (pci_range)
                        {
                        case QXL_RAM_RANGE_INDEX:
                            m_RamPA = pResDescriptor->u.Memory.Start;
                            m_RamStart = (UINT8*)MemBase;
                            m_RamSize = MemLength;
                            if (pDispInfo->PhysicAddress.QuadPart == 0L) {
                                pDispInfo->PhysicAddress.QuadPart = m_RamPA.QuadPart;
                            }
                            pci_range = QXL_VRAM_RANGE_INDEX;
                            break;
                        case QXL_VRAM_RANGE_INDEX:
                            m_VRamPA = pResDescriptor->u.Memory.Start;
                            m_VRamStart = (UINT8*)MemBase;
                            m_VRamSize = MemLength;
                            pci_range = QXL_ROM_RANGE_INDEX;
                            break;
                        case QXL_ROM_RANGE_INDEX:
                            m_RomHdr = (QXLRom*)MemBase;
                            m_RomSize = MemLength;
                            pci_range = QXL_PCI_RANGES;
                            break;
                        default:
                            break;
                        }
                    }
              }
                   break;
              case CmResourceTypeDma:
                   DbgPrint( TRACE_LEVEL_INFORMATION, ("Dma\n"));
                   break;
              case CmResourceTypeDeviceSpecific:
                   DbgPrint( TRACE_LEVEL_INFORMATION, ("Device Specific\n"));
                   break;
              case CmResourceTypeBusNumber:
                   DbgPrint( TRACE_LEVEL_INFORMATION, ("Bus number\n"));
                   break;
              default:
                   break;
           }
        }
    }
    if (m_IoBase == NULL || m_IoSize == 0 ||
        m_RomHdr == NULL || m_RomSize == 0 ||
        m_RomHdr->magic != QXL_ROM_MAGIC ||
        m_RamStart == NULL || m_RamSize == 0 ||
        m_VRamStart == NULL || m_VRamSize == 0 ||
        (m_RamHdr = (QXLRam *)(m_RamStart + m_RomHdr->ram_header_offset)) == NULL ||
        m_RamHdr->magic != QXL_RAM_MAGIC) 
    {
        UnmapMemory();
        return STATUS_UNSUCCESSFUL;
    }

    m_LogBuf = m_RamHdr->log_buf;
    m_LogPort = m_IoBase + QXL_IO_LOG;
    m_Id = m_RomHdr->id;

    CreateEvents();

    return QxlInit(pDispInfo);
}

NTSTATUS QxlDevice::QxlInit(DXGK_DISPLAY_INFORMATION* pDispInfo)
{
    NTSTATUS Status = STATUS_SUCCESS;

    if (!InitMemSlots()) {
        DestroyMemSlots();
        return STATUS_UNSUCCESSFUL;
    }

    Status = GetModeList(pDispInfo);
    if (!NT_SUCCESS(Status))
    {
        DbgPrint(TRACE_LEVEL_ERROR, ("GetModeList failed with status 0x%X\n",
                           Status));
        return Status;
    }

    WRITE_PORT_UCHAR((PUCHAR)(m_IoBase + QXL_IO_RESET), 0);
    CreateRings();
    m_RamHdr->int_mask = QXL_INTERRUPT_MASK;
    CreateMemSlots();
    InitDeviceMemoryResources();
    return Status;
}

void QxlDevice::QxlClose()
{
    DestroyMemSlots();
}

void QxlDevice::UnmapMemory(void)
{
    PDXGKRNL_INTERFACE pDxgkInterface = m_pQxlDod->GetDxgkInterrface();
    if (m_IoMapped && m_IoBase)
    {
        pDxgkInterface->DxgkCbUnmapMemory( pDxgkInterface->DeviceHandle, &m_IoBase);
    }
    m_IoBase = NULL;
    if (m_RomHdr)
    {
        pDxgkInterface->DxgkCbUnmapMemory( pDxgkInterface->DeviceHandle, &m_RomHdr);
        m_RomHdr = NULL;
    }

    if (m_RamStart)
    {
        pDxgkInterface->DxgkCbUnmapMemory( pDxgkInterface->DeviceHandle, &m_RamStart);
        m_RamStart = NULL;
    }

    if (m_VRamStart)
    {
        pDxgkInterface->DxgkCbUnmapMemory( pDxgkInterface->DeviceHandle, &m_VRamStart);
        m_VRamStart = NULL;
    }
}

BOOL QxlDevice::InitMemSlots(void)
{
    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));
    m_NumMemSlots = m_RomHdr->slots_end;
    m_SlotGenBits = m_RomHdr->slot_gen_bits;
    m_SlotIdBits = m_RomHdr->slot_id_bits;
    m_VaSlotMask = (~(uint64_t)0) >> (m_SlotIdBits + m_SlotGenBits);
    size_t size = m_NumMemSlots * sizeof(MemSlot);
    m_MemSlots = reinterpret_cast<MemSlot*>
                                (new (PagedPool) BYTE[size]);
    if (m_MemSlots)
    {
        RtlZeroMemory(m_MemSlots, size);
        return TRUE;
    }
    DbgPrint(TRACE_LEVEL_ERROR, ("---> %s Failed to init mem slot\n", __FUNCTION__));
    return FALSE;
}

void QxlDevice::DestroyMemSlots(void)
{
    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));
    delete [] reinterpret_cast<BYTE*>(m_MemSlots);
    m_MemSlots = NULL;
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
}

void QxlDevice::CreatePrimarySurface(PVIDEO_MODE_INFORMATION pModeInfo)
{
    QXLSurfaceCreate *primary_surface_create;
    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));
    primary_surface_create = &m_RamHdr->create_surface;
    primary_surface_create->format = pModeInfo->BitsPerPlane;
    primary_surface_create->width = pModeInfo->VisScreenWidth;
    primary_surface_create->height = pModeInfo->VisScreenHeight;
    primary_surface_create->stride = pModeInfo->ScreenStride;

    primary_surface_create->mem = PA( m_RamStart, m_MainMemSlot);

    primary_surface_create->flags = QXL_SURF_FLAG_KEEP_DATA;
    primary_surface_create->type = QXL_SURF_TYPE_PRIMARY;
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--> %s format = %d, width = %d, height = %d, stride = %d\n", __FUNCTION__, pModeInfo->BitsPerPlane, pModeInfo->VisScreenWidth, pModeInfo->VisScreenHeight,
                                     pModeInfo->ScreenStride));
//    AsyncIo(QXL_IO_CREATE_PRIMARY_ASYNC, 0);
    SyncIo(QXL_IO_CREATE_PRIMARY, 0);
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
}

void QxlDevice::DestroyPrimarySurface(void)
{
    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));
//    AsyncIo(QXL_IO_DESTROY_PRIMARY_ASYNC, 0);
    SyncIo(QXL_IO_DESTROY_PRIMARY, 0);
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
}

_inline QXLPHYSICAL QxlDevice::PA(PVOID virt, UINT8 slot_id)
{
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--> %s\n", __FUNCTION__));
    MemSlot *pSlot = &m_MemSlots[slot_id];;
    return pSlot->high_bits | ((UINT64)virt - pSlot->start_virt_addr);
}

_inline UINT64 QxlDevice::VA(QXLPHYSICAL paddr, UINT8 slot_id)
{
    UINT64 virt;
    MemSlot *pSlot = &m_MemSlots[slot_id];;

    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));
    virt = paddr & m_VaSlotMask;
    virt += pSlot->start_virt_addr;;

    return virt;
}

void QxlDevice::SetupHWSlot(UINT8 Idx, MemSlot *pSlot)
{
    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));
    m_RamHdr->mem_slot.mem_start = pSlot->start_phys_addr;
    m_RamHdr->mem_slot.mem_end = pSlot->end_phys_addr;
    WRITE_PORT_UCHAR((PUCHAR)(m_IoBase + QXL_IO_MEMSLOT_ADD), Idx);
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<---> %s\n", __FUNCTION__));
}

BOOL QxlDevice::CreateEvents()
{
    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));
    KeInitializeEvent(&m_DisplayEvent,
                      SynchronizationEvent,
                      FALSE);
    KeInitializeEvent(&m_CursorEvent,
                      SynchronizationEvent,
                      FALSE);
    KeInitializeEvent(&m_IoCmdEvent,
                      SynchronizationEvent,
                      FALSE);
    KeInitializeMutex(&m_MemLock,1);
    KeInitializeMutex(&m_CmdLock,1);
    KeInitializeMutex(&m_IoLock,1);
    KeInitializeMutex(&m_CrsLock,1);

    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
    return TRUE;
}

BOOL QxlDevice::CreateRings()
{
    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));
    m_CommandRing = &(m_RamHdr->cmd_ring);
    m_CursorRing = &(m_RamHdr->cursor_ring);
    m_ReleaseRing = &(m_RamHdr->release_ring);
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
    return TRUE;
}

void QxlDevice::AsyncIo(UCHAR  Port, UCHAR Value)
{
    LARGE_INTEGER timeout;
    KeWaitForSingleObject
    (
        &m_IoLock,
        Executive,
        KernelMode,
        FALSE,
        NULL
    );
    WRITE_PORT_UCHAR(m_IoBase + Port, Value);
    timeout.QuadPart = -60000L * 1000 * 10;
    WAIT_FOR_EVENT(m_IoCmdEvent, &timeout);
    KeReleaseMutex(&m_IoLock,FALSE);
}

void QxlDevice::SyncIo(UCHAR  Port, UCHAR Value)
{
    KeWaitForSingleObject
    (
        &m_IoLock,
        Executive,
        KernelMode,
        FALSE,
        NULL
    );
    WRITE_PORT_UCHAR(m_IoBase + Port, Value);
    KeReleaseMutex(&m_IoLock,FALSE);
}

UINT8 QxlDevice::SetupMemSlot(UINT8 Idx, UINT64 pastart, UINT64 paend, UINT64 vastart, UINT64 vaend)
{
    UINT64 high_bits;
    MemSlot *pSlot;
    UINT8 slot_index;

    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));
    slot_index = m_RomHdr->slots_start + Idx;
    pSlot = &m_MemSlots[slot_index];
    pSlot->start_phys_addr = pastart;
    pSlot->end_phys_addr = paend;
    pSlot->start_virt_addr = vastart;
    pSlot->end_virt_addr = vaend;

    SetupHWSlot(Idx + 1, pSlot);

    pSlot->generation = m_RomHdr->slot_generation;
    high_bits = slot_index << m_SlotGenBits;
    high_bits |= pSlot->generation;
    high_bits <<= (64 - (m_SlotGenBits + m_SlotIdBits));
    pSlot->high_bits = high_bits;
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
    return slot_index;
}

BOOL QxlDevice::CreateMemSlots(void)
{
    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s 3\n", __FUNCTION__));
    UINT64 len = m_RomHdr->surface0_area_size + m_RomHdr->num_pages * PAGE_SIZE;
    m_MainMemSlot = SetupMemSlot(0,
                                (UINT64)m_RamPA.QuadPart, 
                                (UINT64)(m_RamPA.QuadPart + len),
                                (UINT64)m_RamStart,
                                (UINT64)(m_RamStart + len));
    len = m_VRamSize;
    m_SurfaceMemSlot = SetupMemSlot(1,
                                (UINT64)m_VRamPA.QuadPart,
                                (UINT64)(m_VRamPA.QuadPart + len),
                                (UINT64)m_VRamStart,
                                (UINT64)(m_VRamStart + len));
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
    return TRUE;
}

void QxlDevice::InitDeviceMemoryResources(void)
{
    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s num_pages = %d\n", __FUNCTION__, m_RomHdr->num_pages));
    InitMspace(MSPACE_TYPE_DEVRAM, (m_RamStart + m_RomHdr->surface0_area_size), (size_t)(m_RomHdr->num_pages * PAGE_SIZE));
    InitMspace(MSPACE_TYPE_VRAM, m_VRamStart, m_VRamSize);
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
}

void QxlDevice::InitMspace(UINT32 mspace_type, UINT8 *start, size_t capacity)
{
    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s type = %d, start = %p, capacity = %d\n", __FUNCTION__, mspace_type, start, capacity));
    m_MSInfo[mspace_type]._mspace = create_mspace_with_base(start, capacity, 0, this);
    m_MSInfo[mspace_type].mspace_start = start;
    m_MSInfo[mspace_type].mspace_end = start + capacity;
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s _mspace = %p\n", __FUNCTION__, m_MSInfo[mspace_type]._mspace));
}

void QxlDevice::ResetDevice(void)
{
    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));
    m_RamHdr->int_mask = ~0;
    WRITE_PORT_UCHAR(m_IoBase + QXL_IO_MEMSLOT_ADD, 0);
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
}

NTSTATUS
QxlDevice::ExecutePresentDisplayOnly(
    _In_ BYTE*             DstAddr,
    _In_ UINT              DstBitPerPixel,
    _In_ BYTE*             SrcAddr,
    _In_ UINT              SrcBytesPerPixel,
    _In_ LONG              SrcPitch,
    _In_ ULONG             NumMoves,
    _In_ D3DKMT_MOVE_RECT* Moves,
    _In_ ULONG             NumDirtyRects,
    _In_ RECT*             DirtyRect,
    _In_ D3DKMDT_VIDPN_PRESENT_PATH_ROTATION Rotation,
    _In_ const CURRENT_BDD_MODE* pModeCur)
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

//    const CURRENT_BDD_MODE* pModeCur = &m_CurrentModes[0];
    ctx->DstAddr          = DstAddr;
    ctx->DstBitPerPixel   = DstBitPerPixel;
    ctx->DstStride        = pModeCur->DispInfo.Pitch;
    ctx->SrcWidth         = pModeCur->SrcModeWidth;
    ctx->SrcHeight        = pModeCur->SrcModeHeight;
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
        POINT*   pSourcePoint = &ctx->Moves[i].SourcePoint;
        RECT*    pDestRect = &ctx->Moves[i].DestRect;

        DbgPrint(TRACE_LEVEL_INFORMATION, ("--- %d SourcePoint.x = %ld, SourcePoint.y = %ld, DestRect.bottom = %ld, DestRect.left = %ld, DestRect.right = %ld, DestRect.top = %ld\n", 
            i , pSourcePoint->x, pSourcePoint->y, pDestRect->bottom, pDestRect->left, pDestRect->right, pDestRect->top));

        BltBits(&DstBltInfo,
        &SrcBltInfo,
        1, // NumRects
        pDestRect);
    }

    // Copy all the dirty rects from source image to video frame buffer.
    for (UINT i = 0; i < ctx->NumDirtyRects; i++)
    {
        RECT*    pDirtyRect = &ctx->DirtyRect[i];
        DbgPrint(TRACE_LEVEL_INFORMATION, ("--- %d pDirtyRect->bottom = %ld, pDirtyRect->left = %ld, pDirtyRect->right = %ld, pDirtyRect->top = %ld\n", 
            i, pDirtyRect->bottom, pDirtyRect->left, pDirtyRect->right, pDirtyRect->top));

        BltBits(&DstBltInfo,
        &SrcBltInfo,
        1, // NumRects
        pDirtyRect);
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

void QxlDevice::WaitForReleaseRing(void)
{
    int wait;

    DbgPrint(TRACE_LEVEL_VERBOSE, ("--->%s\n", __FUNCTION__));

    for (;;) {
        LARGE_INTEGER timeout;

        if (SPICE_RING_IS_EMPTY(m_ReleaseRing)) {
            QXL_SLEEP(10);
            if (!SPICE_RING_IS_EMPTY(m_ReleaseRing)) {
                break;
            }
            SyncIo(QXL_IO_NOTIFY_OOM, 0);
        }
        SPICE_RING_CONS_WAIT(m_ReleaseRing, wait);

        if (!wait) {
            break;
        }

        timeout.QuadPart = -30 * 1000 * 10; //30ms
        WAIT_FOR_EVENT(m_DisplayEvent, &timeout);

        if (SPICE_RING_IS_EMPTY(m_ReleaseRing)) {
            SyncIo(QXL_IO_NOTIFY_OOM, 0);
        }
    }
    DbgPrint(TRACE_LEVEL_VERBOSE, ("%s: <---\n", __FUNCTION__));
}

void QxlDevice::FlushReleaseRing()
{
    UINT64 output;
    int notify;
    int num_to_release = 50;

    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));

    output = m_FreeOutputs;

    while (1) {
        while (output != 0) {
            output = ReleaseOutput(output);
            if (--num_to_release == 0) {
                break;
            }
        }

        if (output != 0 ||
            SPICE_RING_IS_EMPTY(m_ReleaseRing)) {
            break;
        }

        output = *SPICE_RING_CONS_ITEM(m_ReleaseRing);
        SPICE_RING_POP(m_ReleaseRing, notify);
    }

    m_FreeOutputs = output;
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
}

void QxlDevice::EmptyReleaseRing()
{
    KeWaitForSingleObject
    (
        &m_MemLock,
        Executive,
        KernelMode,
        FALSE,
        NULL
    );
    while (m_FreeOutputs || !SPICE_RING_IS_EMPTY(m_ReleaseRing)) {
        FlushReleaseRing();
    }
    KeReleaseMutex(&m_MemLock,FALSE);
}

UINT64 QxlDevice::ReleaseOutput(UINT64 output_id)
{
    QXLOutput *output = (QXLOutput *)output_id;
    Resource **now;
    Resource **end;
    UINT64 next;

    ASSERT(output_id);
    DbgPrint(TRACE_LEVEL_VERBOSE, ("--->%s 0x%x\n", __FUNCTION__, output));

    for (now = output->resources, end = now + output->num_res; now < end; now++) {
        RELEASE_RES(*now);
    }
    next = *(UINT64*)output->data;
    FreeMem(MSPACE_TYPE_DEVRAM, output);
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<---%s\n", __FUNCTION__));
    return next;
}

void *QxlDevice::AllocMem(UINT32 mspace_type, size_t size, BOOL force)
{
    PVOID ptr;

    ASSERT(m_MSInfo[mspace_type]._mspace);
    DbgPrint(TRACE_LEVEL_VERBOSE, ("--->%s: %p(%d) size %u\n", __FUNCTION__,
        m_MSInfo[mspace_type]._mspace,
        mspace_footprint(m_MSInfo[mspace_type]._mspace),
        size));
#ifdef DBG
     mspace_malloc_stats(m_MSInfo[mspace_type]._mspace);
#endif

    KeWaitForSingleObject
    (
        &m_MemLock,
        Executive,
        KernelMode,
        FALSE,
        NULL
    );

    while (1) {
        /* Release lots of queued resources, before allocating, as we
           want to release early to minimize fragmentation risks. */
        FlushReleaseRing();

        ptr = mspace_malloc(m_MSInfo[mspace_type]._mspace, size);
        if (ptr) {
            break;
        }

        if (m_FreeOutputs != 0 ||
            !SPICE_RING_IS_EMPTY(m_ReleaseRing)) {
            /* We have more things to free, try that */
            continue;
        }

        if (force) {
            /* Ask spice to free some stuff */
            WaitForReleaseRing();
        } else {
            /* Fail */
            break;
        }
    }
    KeReleaseMutex(&m_MemLock,FALSE);

    ASSERT((!ptr && !force) || (ptr >= m_MSInfo[mspace_type].mspace_start &&
                                      ptr < m_MSInfo[mspace_type].mspace_end));
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<---%s: ptr 0x%x\n", __FUNCTION__, ptr));
    return ptr;
}

void QxlDevice::FreeMem(UINT32 mspace_type, void *ptr)
{
    ASSERT(m_MSInfo[mspace_type]._mspace);
    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));

#ifdef DBG
    if (!((UINT8 *)ptr >= m_MSInfo[mspace_type].mspace_start &&
                 (UINT8 *)ptr < m_MSInfo[mspace_type].mspace_end)) {
        DbgPrint(TRACE_LEVEL_ERROR, ("ASSERT failed @ %s, %p not in [%p, %p) (%d)\n", __FUNCTION__,
            ptr, m_MSInfo[mspace_type].mspace_start,
            m_MSInfo[mspace_type].mspace_end, mspace_type));
    }
#endif
    KeWaitForSingleObject
    (
        &m_MemLock,
        Executive,
        KernelMode,
        FALSE,
        NULL
    );
    mspace_free(m_MSInfo[mspace_type]._mspace, ptr);
    KeReleaseMutex(&m_MemLock,FALSE);
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
}

QXLDrawable *QxlDevice::GetDrawable()
{
    QXLOutput *output;

    output = (QXLOutput *)AllocMem(MSPACE_TYPE_DEVRAM, sizeof(QXLOutput) + sizeof(QXLDrawable), TRUE);
    output->num_res = 0;
    RESOURCE_TYPE(output, RESOURCE_TYPE_DRAWABLE);
    ((QXLDrawable *)output->data)->release_info.id = (UINT64)output;
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--> %s 0x%x\n", __FUNCTION__, output));
    return(QXLDrawable *)output->data;
}

QXLCursorCmd *QxlDevice::CursorCmd()
{
    QXLCursorCmd *cursor_cmd;
    QXLOutput *output;

    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));
    output = (QXLOutput *)AllocMem(MSPACE_TYPE_DEVRAM, sizeof(QXLOutput) + sizeof(QXLCursorCmd), TRUE);
    output->num_res = 0;
    RESOURCE_TYPE(output, RESOURCE_TYPE_CURSOR);
    cursor_cmd = (QXLCursorCmd *)output->data;
    cursor_cmd->release_info.id = (UINT64)output;
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
    return cursor_cmd;
}

BOOL QxlDevice::SetClip(const RECT *clip, QXLDrawable *drawable)
{
    Resource *rects_res;

    if (clip == NULL) {
        drawable->clip.type = SPICE_CLIP_TYPE_NONE;
        DbgPrint(TRACE_LEVEL_INFORMATION, ("%s QXL_CLIP_TYPE_NONE\n", __FUNCTION__));
        return TRUE;
    }

    QXLClipRects *rects;
    rects_res = (Resource *)AllocMem(MSPACE_TYPE_DEVRAM, sizeof(Resource) + sizeof(QXLClipRects) +
                                        sizeof(QXLRect), TRUE);
    rects_res->refs = 1;
    rects_res->free = FreeClipRectsEx;
    rects_res->ptr = this;
    rects = (QXLClipRects *)rects_res->res;
    rects->num_rects = 1;
    rects->chunk.data_size = sizeof(QXLRect);
    rects->chunk.prev_chunk = 0;
    rects->chunk.next_chunk = 0;
    CopyRect((QXLRect *)rects->chunk.data, clip);

    DrawableAddRes(drawable, rects_res);
    drawable->clip.type = SPICE_CLIP_TYPE_RECTS;
    drawable->clip.data = PA(rects_res->res, m_MainMemSlot);
    return TRUE;
}

void QxlDevice::AddRes(QXLOutput *output, Resource *res)
{
    res->refs++;
    output->resources[output->num_res++] = res;
}

void QxlDevice::DrawableAddRes(QXLDrawable *drawable, Resource *res)
{
    QXLOutput *output;

    output = (QXLOutput *)((UINT8 *)drawable - sizeof(QXLOutput));
    AddRes(output, res);
}

void QxlDevice::CursorCmdAddRes(QXLCursorCmd *cmd, Resource *res)
{
    QXLOutput *output;

    output = (QXLOutput *)((UINT8 *)cmd - sizeof(QXLOutput));
    AddRes(output, res);
}

void QxlDevice::FreeClipRectsEx(Resource *res)
{
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--> %s\n", __FUNCTION__));
    QxlDevice* pqxl = (QxlDevice*)res->ptr;
    pqxl->FreeClipRects(res);
}

void QxlDevice::FreeClipRects(Resource *res)
{
    QXLPHYSICAL chunk_phys;
    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));

    chunk_phys = ((QXLClipRects *)res->res)->chunk.next_chunk;
    while (chunk_phys) {
        QXLDataChunk *chunk = (QXLDataChunk *)VA(chunk_phys, m_MainMemSlot);
        chunk_phys = chunk->next_chunk;
        FreeMem(MSPACE_TYPE_DEVRAM, chunk);
    }
    FreeMem(MSPACE_TYPE_DEVRAM, res);
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
}

void QxlDevice::FreeBitmapImageEx(Resource *res)
{
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--> %s\n", __FUNCTION__));
    QxlDevice* pqxl = (QxlDevice*)res->ptr;
    pqxl->FreeBitmapImage(res);
}

void QxlDevice::FreeBitmapImage(Resource *res)
{
    InternalImage *internal;
    QXLPHYSICAL chunk_phys;
    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));

    internal = (InternalImage *)res->res;

    chunk_phys = ((QXLDataChunk *)(&internal->image.bitmap + 1))->next_chunk;
    while (chunk_phys) {
        QXLDataChunk *chunk = (QXLDataChunk *)VA(chunk_phys, m_MainMemSlot);
        chunk_phys = chunk->next_chunk;
        FreeMem(MSPACE_TYPE_DEVRAM, chunk);
    }

    FreeMem(MSPACE_TYPE_DEVRAM, res);
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
}

void QxlDevice::FreeCursorEx(Resource *res)
{
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--> %s\n", __FUNCTION__));
    QxlDevice* pqxl = (QxlDevice*)res->ptr;
    pqxl->FreeCursor(res);
}

void QxlDevice::FreeCursor(Resource *res)
{
    QXLPHYSICAL chunk_phys;

    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));
    chunk_phys = ((InternalCursor *)res->res)->cursor.chunk.next_chunk;
    while (chunk_phys) {
        QXLDataChunk *chunk = (QXLDataChunk *)VA(chunk_phys, m_MainMemSlot);
        chunk_phys = chunk->next_chunk;
        FreeMem(MSPACE_TYPE_DEVRAM, chunk);
    }

    FreeMem(MSPACE_TYPE_DEVRAM, res);
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
}

QXLDrawable *QxlDevice::Drawable(UINT8 type, CONST RECT *area, CONST RECT *clip, UINT32 surface_id)
{
    QXLDrawable *drawable;

    ASSERT(area);
    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));

    drawable = GetDrawable();
    drawable->surface_id = surface_id;
    drawable->type = type;
    drawable->effect = QXL_EFFECT_OPAQUE;
    drawable->self_bitmap = 0;
    drawable->mm_time = m_RomHdr->mm_clock;
    drawable->surfaces_dest[0] = -1;
    drawable->surfaces_dest[1] = - 1;
    drawable->surfaces_dest[2] = -1;
    CopyRect(&drawable->bbox, area);

    if (!SetClip(clip, drawable)) {
        DbgPrint(TRACE_LEVEL_VERBOSE, ("%s: set clip failed\n", __FUNCTION__));
        ReleaseOutput(drawable->release_info.id);
        drawable = NULL;
    }
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
    return drawable;
}

void QxlDevice::PushDrawable(QXLDrawable *drawable) {
    QXLCommand *cmd;
    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));

    KeWaitForSingleObject
    (
        &m_CmdLock,
        Executive,
        KernelMode,
        FALSE,
        NULL
    );
    WaitForCmdRing();
    cmd = SPICE_RING_PROD_ITEM(m_CommandRing);
    cmd->type = QXL_CMD_DRAW;
    cmd->data = PA(drawable, m_MainMemSlot);
    PushCmd();
    KeReleaseMutex(&m_CmdLock,FALSE);
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
}

void QxlDevice::PushCursorCmd(QXLCursorCmd *cursor_cmd)
{
    QXLCommand *cmd;

    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));

    KeWaitForSingleObject
    (
        &m_CrsLock,
        Executive,
        KernelMode,
        FALSE,
        NULL
    );
    WaitForCursorRing();
    cmd = SPICE_RING_PROD_ITEM(m_CursorRing);
    cmd->type = QXL_CMD_CURSOR;
    cmd->data = PA(cursor_cmd, m_MainMemSlot);
    PushCursor();
    KeReleaseMutex(&m_CrsLock,FALSE);
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
}

VOID QxlDevice::SetImageId(InternalImage *internal,
    BOOL cache_me,
    LONG width,
    LONG height,
    UINT8 format, UINT32 key)
{
    UINT32 image_info = IMAGE_HASH_INIT_VAL(width, height, format);

    if (cache_me) {
        QXL_SET_IMAGE_ID(&internal->image, ((UINT32)QXL_IMAGE_GROUP_DRIVER << 30) |
                         image_info, key);
        internal->image.descriptor.flags = QXL_IMAGE_CACHE;
    } else {
        QXL_SET_IMAGE_ID(&internal->image, ((UINT32)QXL_IMAGE_GROUP_DRIVER_DONT_CACHE  << 30) |
                         image_info, key);
        internal->image.descriptor.flags = 0;
    }
}

VOID QxlDevice::BltBits (
    BLT_INFO* pDst,
    CONST BLT_INFO* pSrc,
    UINT  NumRects,
    _In_reads_(NumRects) CONST RECT *pRects)
{
    QXLDrawable *drawable;
    Resource *image_res;
    InternalImage *internal;
    size_t alloc_size;
    QXLDataChunk *chunk;
    UINT32 line_size;
    LONG width;
    LONG height;

    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));
    UNREFERENCED_PARAMETER(NumRects);
    UNREFERENCED_PARAMETER(pDst);

    if (!(drawable = Drawable(QXL_DRAW_COPY, pRects, NULL, 0))) {
        DbgPrint(TRACE_LEVEL_ERROR, ("Cannot get Drawable.\n"));
    }

    CONST RECT* pRect = &pRects[0];
//    UpdateArea(pRect, 0);
    drawable->u.copy.scale_mode = SPICE_IMAGE_SCALE_MODE_NEAREST;
    drawable->u.copy.mask.bitmap = 0;
    drawable->u.copy.rop_descriptor = SPICE_ROPD_OP_PUT;

    drawable->surfaces_dest[0] = 0;
    CopyRect(&drawable->surfaces_rects[0], pRect);

    drawable->self_bitmap = TRUE;
    CopyRect(&drawable->self_bitmap_area, pRect);

    height = pRect->bottom - pRect->top;
    width = pRect->right - pRect->left;
    line_size = width * 4;

    drawable->u.copy.src_area.bottom = height;
    drawable->u.copy.src_area.left = 0;
    drawable->u.copy.src_area.top = 0;
    drawable->u.copy.src_area.right = width;

    alloc_size = BITMAP_ALLOC_BASE + BITS_BUF_MAX - BITS_BUF_MAX % line_size;
    alloc_size = MIN(BITMAP_ALLOC_BASE + height * line_size, alloc_size);
    image_res = (Resource*)AllocMem(MSPACE_TYPE_DEVRAM, alloc_size, TRUE);

    image_res->refs = 1;
    image_res->free = FreeBitmapImageEx;
    image_res->ptr = this;

    internal = (InternalImage *)image_res->res;
    SetImageId(internal, FALSE, width, height, SPICE_BITMAP_FMT_32BIT, 0);
    internal->image.descriptor.flags = 0;
    internal->image.descriptor.type = SPICE_IMAGE_TYPE_BITMAP;
    chunk = (QXLDataChunk *)(&internal->image.bitmap + 1);
    chunk->data_size = 0;
    chunk->prev_chunk = 0;
    chunk->next_chunk = 0;
    internal->image.bitmap.data = PA(chunk, m_MainMemSlot);
    internal->image.bitmap.flags = 0;
    internal->image.descriptor.width = internal->image.bitmap.x = width;
    internal->image.descriptor.height = internal->image.bitmap.y = height;
    internal->image.bitmap.format = SPICE_BITMAP_FMT_RGBA;
    internal->image.bitmap.stride = line_size;

    UINT8* src = (UINT8*)pSrc->pBits+
                                (pRect->top) * pSrc->Pitch +
                                (pRect->left * 4);
    UINT8* src_end = src - pSrc->Pitch;
    src += pSrc->Pitch * (height - 1);
    UINT8* dest = chunk->data;
    UINT8* dest_end = (UINT8 *)image_res + alloc_size;
    alloc_size = height * line_size;

    for (; src != src_end; src -= pSrc->Pitch, alloc_size -= line_size) {
        PutBytesAlign(&chunk, &dest, &dest_end, src,
                      line_size, alloc_size, line_size);
    }

    internal->image.bitmap.palette = 0;

    drawable->u.copy.src_bitmap = PA(&internal->image, m_MainMemSlot);

    CopyRect(&drawable->surfaces_rects[1], pRect);
    DrawableAddRes(drawable, image_res);
    RELEASE_RES(image_res);

    DbgPrint(TRACE_LEVEL_INFORMATION, ("%s drawable= %p type = %d, effect = %d Dest right(%d) left(%d) top(%d) bottom(%d) src_bitmap= %p.\n", __FUNCTION__,
         drawable, drawable->type, drawable->effect, drawable->surfaces_rects[0].right, drawable->surfaces_rects[0].left,
         drawable->surfaces_rects[0].top, drawable->surfaces_rects[0].bottom,
         drawable->u.copy.src_bitmap));

    PushDrawable(drawable);

    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
}

VOID QxlDevice::PutBytesAlign(QXLDataChunk **chunk_ptr, UINT8 **now_ptr,
                            UINT8 **end_ptr, UINT8 *src, int size,
                            size_t alloc_size, uint32_t alignment)
{
    QXLDataChunk *chunk = *chunk_ptr;
    UINT8 *now = *now_ptr;
    UINT8 *end = *end_ptr;
//    int offset;
    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));

    while (size) {
        int cp_size = (int)MIN(end - now, size);
        if (!cp_size) {
            size_t aligned_size;
            aligned_size = (int)MIN(alloc_size + alignment - 1, BITS_BUF_MAX);
            aligned_size -=  aligned_size % alignment;

            void *ptr = AllocMem(MSPACE_TYPE_DEVRAM, size + sizeof(QXLDataChunk), TRUE);
            chunk->next_chunk = PA(ptr, m_MainMemSlot);
            ((QXLDataChunk *)ptr)->prev_chunk = PA(chunk, m_MainMemSlot);
            chunk = (QXLDataChunk *)ptr;
            chunk->data_size = 0;
            chunk->next_chunk = 0;
            now = chunk->data;
            end = now + size;

            cp_size = (int)MIN(end - now, size);
        }
        RtlCopyMemory(now, src, cp_size);
        src += cp_size;
        now += cp_size;
        chunk->data_size += cp_size;
        size -= cp_size;
    }
    *chunk_ptr = chunk;
    *now_ptr = now;
    *end_ptr = end;
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
}

VOID QxlDevice::BlackOutScreen(CURRENT_BDD_MODE* pCurrentBddMod)
{
    QXLDrawable *drawable;
    RECT Rect;
    PAGED_CODE();

    DbgPrint(TRACE_LEVEL_FATAL, ("---> %s\n", __FUNCTION__));
    if (pCurrentBddMod->Flags.FrameBufferIsActive)
    {
        Rect.bottom = pCurrentBddMod->SrcModeHeight;
        Rect.top = 0;
        Rect.left = 0;
        Rect.right = pCurrentBddMod->SrcModeWidth;
        if (!(drawable = Drawable(QXL_DRAW_FILL, &Rect, NULL, 0)))
        {
            DbgPrint(TRACE_LEVEL_ERROR, ("Cannot get Drawable.\n"));
            return;
        }
        drawable->u.fill.brush.type = SPICE_BRUSH_TYPE_SOLID;
        drawable->u.fill.brush.u.color = 0;
        drawable->u.fill.rop_descriptor = SPICE_ROPD_OP_PUT;
        drawable->u.fill.mask.flags = 0;
        drawable->u.fill.mask.pos.x = 0;
        drawable->u.fill.mask.pos.y = 0;
        drawable->u.fill.mask.bitmap = 0;
        PushDrawable(drawable);
    }
    DbgPrint(TRACE_LEVEL_FATAL, ("<--- %s\n", __FUNCTION__));
}

NTSTATUS QxlDevice::HWClose(void)
{
    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));
    QxlClose();
    UnmapMemory();
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
    return STATUS_SUCCESS;
}

NTSTATUS  QxlDevice::SetPointerShape(_In_ CONST DXGKARG_SETPOINTERSHAPE* pSetPointerShape)
{
    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s flag = %x\n", __FUNCTION__, pSetPointerShape->Flags.Value));
    DbgPrint(TRACE_LEVEL_INFORMATION, ("<--> %s flag = %d pitch = %d, pixels = %p, id = %d, w = %d, h = %d, x = %d, y = %d\n", __FUNCTION__,
                                 pSetPointerShape->Flags.Value,
                                 pSetPointerShape->Pitch,
                                 pSetPointerShape->pPixels,
                                 pSetPointerShape->VidPnSourceId,
                                 pSetPointerShape->Width,
                                 pSetPointerShape->Height,
                                 pSetPointerShape->XHot,
                                 pSetPointerShape->YHot));
    if (!EnablePointer() || (!pSetPointerShape->Flags.Monochrome && !pSetPointerShape->Flags.Color))
        return STATUS_UNSUCCESSFUL;

    QXLCursorCmd *cursor_cmd;
    InternalCursor *internal;
    QXLCursor *cursor;
    Resource *res;
    QXLDataChunk *chunk;
//    ULONG unique;
    UINT8 *src;
    UINT8 *src_end;
    UINT8 *now;
    UINT8 *end;
    int line_size;

    cursor_cmd = CursorCmd();
    cursor_cmd->type = QXL_CURSOR_SET;

    cursor_cmd->u.set.visible = TRUE;
    cursor_cmd->u.set.position.x = 0;
    cursor_cmd->u.set.position.y = 0;

    res = (Resource *)AllocMem(MSPACE_TYPE_DEVRAM, CURSOR_ALLOC_SIZE, TRUE);
    res->refs = 1;
    res->free = FreeCursorEx;
    res->ptr = this;
    RESOURCE_TYPE(res, RESOURCE_TYPE_CURSOR);

    internal = (InternalCursor *)res->res;

    cursor = &internal->cursor;
    cursor->header.type = pSetPointerShape->Flags.Monochrome ? SPICE_CURSOR_TYPE_MONO : SPICE_CURSOR_TYPE_ALPHA;
    cursor->header.unique = 0;
    cursor->header.width = (UINT16)pSetPointerShape->Width;
    cursor->header.height = (UINT16)pSetPointerShape->Height;
    if (cursor->header.type == SPICE_CURSOR_TYPE_MONO) {
        cursor->header.height >>= 1;
        line_size = ALIGN(cursor->header.width, 8) >> 3;
    } else {
        line_size = cursor->header.width << 2;
    }

    cursor->header.hot_spot_x = (UINT16)pSetPointerShape->XHot;
    cursor->header.hot_spot_y = (UINT16)pSetPointerShape->YHot;

    cursor->data_size = line_size * pSetPointerShape->Height;
    DbgPrint(TRACE_LEVEL_INFORMATION, ("<--> %s %d::%d::%d::%d::%d\n", __FUNCTION__, cursor->header.width, cursor->header.height, cursor->header.hot_spot_x, cursor->header.hot_spot_y, cursor->data_size));

    chunk = &cursor->chunk;
    chunk->data_size = 0;
    chunk->prev_chunk = 0;
    chunk->next_chunk = 0;

    src = (UINT8*)pSetPointerShape->pPixels;
    now = chunk->data;
    end = (UINT8 *)res + CURSOR_ALLOC_SIZE;

    src_end = src + (pSetPointerShape->Pitch * pSetPointerShape->Height);
    for (; src != src_end; src += pSetPointerShape->Pitch) {
        PutBytesAlign(&chunk, &now, &end, src, line_size,
                 PAGE_SIZE, 1);
    }
    CursorCmdAddRes(cursor_cmd, res);
    RELEASE_RES(res);
    cursor_cmd->u.set.shape = PA(&internal->cursor, m_MainMemSlot);
    PushCursorCmd(cursor_cmd);
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));

    return STATUS_SUCCESS;
}

NTSTATUS QxlDevice::SetPointerPosition(_In_ CONST DXGKARG_SETPOINTERPOSITION* pSetPointerPosition)
{
    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));
    DbgPrint(TRACE_LEVEL_INFORMATION, ("<--> %s flag = %d id = %d, x = %d, y = %d\n", __FUNCTION__,
                                 pSetPointerPosition->Flags.Value,
                                 pSetPointerPosition->VidPnSourceId,
                                 pSetPointerPosition->X,
                                 pSetPointerPosition->Y));
    if (EnablePointer()) {
        QXLCursorCmd *cursor_cmd = CursorCmd();
        if (pSetPointerPosition->X < 0) {
           cursor_cmd->type = QXL_CURSOR_HIDE;
    } else {
           cursor_cmd->type = QXL_CURSOR_MOVE;
           cursor_cmd->u.position.x = (INT16)pSetPointerPosition->X;
           cursor_cmd->u.position.y = (INT16)pSetPointerPosition->Y;
        }
        PushCursorCmd(cursor_cmd);
    }
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
    return STATUS_SUCCESS;
}

NTSTATUS QxlDevice::Escape(_In_ CONST DXGKARG_ESCAPE* pEscap)
{
    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));
    QXLEscapeSetCustomDisplay *custom_display;
    UINT xres;
    UINT yres;
    UINT bpp;

    if (pEscap->PrivateDriverDataSize != sizeof(QXLEscapeSetCustomDisplay)) {
        DbgPrint(TRACE_LEVEL_FATAL, ("<--> %s Incorrect buffer size %d instead of %d\n", __FUNCTION__, pEscap->PrivateDriverDataSize, sizeof(QXLEscapeSetCustomDisplay)));
        return STATUS_INVALID_BUFFER_SIZE;
    }
    custom_display = (QXLEscapeSetCustomDisplay*)pEscap->pPrivateDriverData;
    xres = custom_display->xres;
    yres = custom_display->yres;
    bpp = custom_display->bpp;
    if (bpp != QXL_BPP)
    {
        bpp = QXL_BPP;
    }
    if (xres < MIN_WIDTH_SIZE || yres < MIN_HEIGHT_SIZE)
    {
        DbgPrint(TRACE_LEVEL_FATAL, ("%s: xres = %d, yres = %d\n", __FUNCTION__, xres, yres));
        return ERROR_INVALID_DATA;
    }

    if (m_CustomMode == (m_ModeCount - 1))
        m_CustomMode = (USHORT)(m_ModeCount - 2);
    else
        m_CustomMode = (USHORT)(m_ModeCount - 1);

    if ((xres * yres * bpp / 8) > m_RomHdr->surface0_area_size) {
        DbgPrint(TRACE_LEVEL_FATAL, ("%s: Mode (%dx%d#%d) doesn't fit in memory (%d)\n",
                    __FUNCTION__, xres, yres, bpp, m_RomHdr->surface0_area_size));
        return ERROR_NOT_ENOUGH_MEMORY;
    }
    UpdateVideoModeInfo(m_CustomMode, xres, yres, bpp);
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
    return STATUS_SUCCESS;
}

VOID QxlDevice::WaitForCmdRing()
{
    int wait;
    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));

    for (;;) {
        SPICE_RING_PROD_WAIT(m_CommandRing, wait);

        if (!wait) {
            break;
        }
        WAIT_FOR_EVENT(m_DisplayEvent, NULL);
    }
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
}

VOID QxlDevice::PushCmd()
{
    int notify;
    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));
    SPICE_RING_PUSH(m_CommandRing, notify);
    if (notify) {
        SyncIo(QXL_IO_NOTIFY_CMD, 0);
    }
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s notify = %d\n", __FUNCTION__, notify));
}

VOID QxlDevice::WaitForCursorRing(VOID)
{
    int wait;
    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));

    for (;;) {
        SPICE_RING_PROD_WAIT(m_CursorRing, wait);

        if (!wait) {
            break;
        }

        LARGE_INTEGER timeout; // 1 => 100 nanoseconds
        timeout.QuadPart = -1 * (1000 * 1000 * 10); //negative  => relative // 1s
        WAIT_FOR_EVENT(m_CursorEvent, &timeout);

        if (SPICE_RING_IS_FULL(m_CursorRing)) {
            DbgPrint(TRACE_LEVEL_ERROR, ("%s: timeout\n", __FUNCTION__));
        }
    }
}

VOID QxlDevice::PushCursor(VOID)
{
    int notify;
    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));
    SPICE_RING_PUSH(m_CursorRing, notify);
    if (notify) {
        SyncIo(QXL_IO_NOTIFY_CURSOR, 0);
    }
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s notify = %d\n", __FUNCTION__, notify));
}

BOOLEAN QxlDevice::InterruptRoutine(_In_ PDXGKRNL_INTERFACE pDxgkInterface, _In_  ULONG MessageNumber)
{
    UNREFERENCED_PARAMETER(MessageNumber);
    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));
    if (!(m_RamHdr->int_pending & m_RamHdr->int_mask)) {
        return FALSE;
    }
    m_RamHdr->int_mask = 0;
    WRITE_PORT_UCHAR((PUCHAR)(m_IoBase + QXL_IO_UPDATE_IRQ), 0);
    m_Pending |= m_RamHdr->int_pending;
    m_RamHdr->int_pending = 0;

    DXGKARGCB_NOTIFY_INTERRUPT_DATA  notifyInt;// = {0};
    notifyInt.InterruptType = DXGK_INTERRUPT_DISPLAYONLY_PRESENT_PROGRESS;
    notifyInt.DisplayOnlyPresentProgress.VidPnSourceId = 0;//FIXME pPath->VidPnSourceId;

    pDxgkInterface->DxgkCbNotifyInterrupt(pDxgkInterface->DeviceHandle,&notifyInt);
    if (!pDxgkInterface->DxgkCbQueueDpc(pDxgkInterface->DeviceHandle)) {
        m_RamHdr->int_mask = QXL_INTERRUPT_MASK;
        WRITE_PORT_UCHAR((PUCHAR)(m_IoBase + QXL_IO_UPDATE_IRQ), 0);
        DbgPrint(TRACE_LEVEL_FATAL, ("---> %s DxgkCbQueueDpc failed\n", __FUNCTION__));
    }
    return TRUE;
}

VOID QxlDevice::DpcRoutine(PVOID ptr)
{
    PDXGKRNL_INTERFACE pDxgkInterface = (PDXGKRNL_INTERFACE)ptr;

    DbgPrint(TRACE_LEVEL_INFORMATION, ("---> %s\n", __FUNCTION__));
    DPC_CB_CONTEXT ctx;
    BOOLEAN dummy;
    ctx.ptr = this;
    NTSTATUS Status = pDxgkInterface->DxgkCbSynchronizeExecution(
            pDxgkInterface->DeviceHandle,
            DpcCallbackEx,
            &ctx,
            0,
            &dummy);
    ASSERT(Status == STATUS_SUCCESS);

    if (ctx.data & QXL_INTERRUPT_DISPLAY) {
        DbgPrint(TRACE_LEVEL_INFORMATION, ("---> %s m_DisplayEvent\n", __FUNCTION__));
        KeSetEvent (&m_DisplayEvent, IO_NO_INCREMENT, FALSE);
    }
    if (ctx.data & QXL_INTERRUPT_CURSOR) {
        DbgPrint(TRACE_LEVEL_INFORMATION, ("---> %s m_CursorEvent\n", __FUNCTION__));
        KeSetEvent (&m_CursorEvent, IO_NO_INCREMENT, FALSE);
    }
    if (ctx.data & QXL_INTERRUPT_IO_CMD) {
        DbgPrint(TRACE_LEVEL_INFORMATION, ("---> %s m_IoCmdEvent\n", __FUNCTION__));
        KeSetEvent (&m_IoCmdEvent, IO_NO_INCREMENT, FALSE);
    }
    m_RamHdr->int_mask = QXL_INTERRUPT_MASK;
    WRITE_PORT_UCHAR((PUCHAR)(m_IoBase + QXL_IO_UPDATE_IRQ), 0);

    DbgPrint(TRACE_LEVEL_INFORMATION, ("<--- %s\n", __FUNCTION__));
}

VOID QxlDevice::UpdateArea(CONST RECT* area, UINT32 surface_id)
{
    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));
    CopyRect(&m_RamHdr->update_area, area);
    m_RamHdr->update_surface = surface_id;
//    AsyncIo(QXL_IO_UPDATE_AREA_ASYNC, 0);
    SyncIo(QXL_IO_UPDATE_AREA, 0);
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
}

BOOLEAN QxlDevice:: DpcCallbackEx(PVOID ptr)
{
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--> %s\n", __FUNCTION__));
    PDPC_CB_CONTEXT ctx = (PDPC_CB_CONTEXT) ptr;
    QxlDevice* pqxl = (QxlDevice*)ctx->ptr;
    pqxl->DpcCallback(ctx);
    return TRUE;
}

VOID QxlDevice::DpcCallback(PDPC_CB_CONTEXT ctx)
{
    ctx->data = m_Pending;
    m_Pending = 0;

}

UINT BPPFromPixelFormat(D3DDDIFORMAT Format)
{
    switch (Format)
    {
        case D3DDDIFMT_UNKNOWN: return 0;
        case D3DDDIFMT_P8: return 8;
        case D3DDDIFMT_R5G6B5: return 16;
        case D3DDDIFMT_R8G8B8: return 24;
        case D3DDDIFMT_X8R8G8B8: // fall through
        case D3DDDIFMT_A8R8G8B8: return 32;
        default: QXL_LOG_ASSERTION1("Unknown D3DDDIFORMAT 0x%I64x", Format); return 0;
    }
}

// Given bits per pixel, return the pixel format at the same bpp
D3DDDIFORMAT PixelFormatFromBPP(UINT BPP)
{
    switch (BPP)
    {
        case  8: return D3DDDIFMT_P8;
        case 16: return D3DDDIFMT_R5G6B5;
        case 24: return D3DDDIFMT_R8G8B8;
        case 32: return D3DDDIFMT_X8R8G8B8;
        default: QXL_LOG_ASSERTION1("A bit per pixel of 0x%I64x is not supported.", BPP); return D3DDDIFMT_UNKNOWN;
    }
}

UINT SpiceFromPixelFormat(D3DDDIFORMAT Format)
{
    switch (Format)
    {
        case D3DDDIFMT_UNKNOWN:
        case D3DDDIFMT_P8: QXL_LOG_ASSERTION1("Bad format type 0x%I64x", Format); return 0;
        case D3DDDIFMT_R5G6B5: return SPICE_SURFACE_FMT_16_555;
        case D3DDDIFMT_R8G8B8:
        case D3DDDIFMT_X8R8G8B8:
        case D3DDDIFMT_A8R8G8B8: return SPICE_SURFACE_FMT_32_xRGB;
        default: QXL_LOG_ASSERTION1("Unknown D3DDDIFORMAT 0x%I64x", Format); return 0;
    }
}
