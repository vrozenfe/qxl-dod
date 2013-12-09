#include "driver.h"
#include "qxldod.h"

QxlDod::QxlDod(_In_ DEVICE_OBJECT* pPhysicalDeviceObject) : m_pPhysicalDevice(pPhysicalDeviceObject),
                                                            m_MonitorPowerState(PowerDeviceD0),
                                                            m_AdapterPowerState(PowerDeviceD0)
{
    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));
    *((UINT*)&m_Flags) = 0;
    RtlZeroMemory(&m_DxgkInterface, sizeof(m_DxgkInterface));
    RtlZeroMemory(&m_DeviceInfo, sizeof(m_DeviceInfo));

    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
}


QxlDod::~QxlDod(void)
{
    PAGED_CODE();
    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));

//    CleanUp();
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

    DXGK_DISPLAY_INFORMATION DisplayInfo;
    RtlCopyMemory(&m_DxgkInterface, pDxgkInterface, sizeof(m_DxgkInterface));
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
    Status = m_DxgkInterface.DxgkCbAcquirePostDisplayOwnership(m_DxgkInterface.DeviceHandle, &DisplayInfo);
    if (!NT_SUCCESS(Status) || DisplayInfo.Width == 0)
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

    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
    m_Flags.DriverStarted = FALSE;
    return STATUS_SUCCESS;
}

VOID QxlDod::CleanUp(VOID)
{
    PAGED_CODE();
    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
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

NTSTATUS QxlDod::SetPowerState(_In_  ULONG HardwareUid,
                               _In_  DEVICE_POWER_STATE DevicePowerState,
                               _In_  POWER_ACTION       ActionType)
{
    NTSTATUS Status = STATUS_SUCCESS;
    PAGED_CODE();
    DbgPrint(TRACE_LEVEL_INFORMATION, ("---> %s\n", __FUNCTION__));

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
    }

    if ((ActionType == PowerActionHibernate) &&
        (DevicePowerState > PowerDeviceD0))
    {
        ActionType = PowerActionHibernate;
    }
    else if ((ActionType >= PowerActionShutdown) &&
             (ActionType < PowerActionWarmEject) &&
             (DevicePowerState > PowerDeviceD0))
    {
        ActionType = PowerActionShutdown;
    }

    Status = VbeSetPowerState(ActionType);
    DbgPrint(TRACE_LEVEL_INFORMATION, ("<--- %s\n", __FUNCTION__));

    return Status;
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
    DbgPrint(TRACE_LEVEL_INFORMATION, ("<--- %s cursor position X = %d, Y = %d\n", __FUNCTION__, pSetPointerPosition->X, pSetPointerPosition->Y));
    return STATUS_UNSUCCESSFUL;
}

// Basic Sample Display Driver does not support hardware cursors, and reports such
// in QueryAdapterInfo. Therefore this function should never be called.
NTSTATUS QxlDod::SetPointerShape(_In_ CONST DXGKARG_SETPOINTERSHAPE* pSetPointerShape)
{
    PAGED_CODE();
    QXL_ASSERT(pSetPointerShape != NULL);

    DbgPrint(TRACE_LEVEL_VERBOSE, ("<---> %s Height = %d, Width = %d, XHot= %d, YHot = %d\n", __FUNCTION__, pSetPointerShape->Height, pSetPointerShape->Width, pSetPointerShape->XHot, pSetPointerShape->YHot));

    if (pSetPointerShape->Flags.Color)
    {
    }
    else if (pSetPointerShape->Flags.Monochrome)
    {
    }
    else
    {
        return STATUS_UNSUCCESSFUL;
    }

    return STATUS_NOT_IMPLEMENTED;
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

    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));
    QXL_ASSERT(pPresentDisplayOnly != NULL);
    QXL_ASSERT(pPresentDisplayOnly->VidPnSourceId < MAX_VIEWS);

    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
    return STATUS_SUCCESS;
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

    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));
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
    NTSTATUS Status = STATUS_SUCCESS;
    D3DKMDT_MONITOR_SOURCE_MODE* pMonitorSourceMode = NULL;
    PVBE_MODEINFO pVbeModeInfo = NULL;
    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));

    Status = pRecommendMonitorModes->pMonitorSourceModeSetInterface->pfnCreateNewModeInfo(pRecommendMonitorModes->hMonitorSourceModeSet, &pMonitorSourceMode);
    if (!NT_SUCCESS(Status))
    {
        // If failed to create a new mode info, mode doesn't need to be released since it was never created
        DbgPrint(TRACE_LEVEL_ERROR, ("pfnCreateNewModeInfo failed with Status = 0x%I64x, hMonitorSourceModeSet = 0x%I64x", Status, pRecommendMonitorModes->hMonitorSourceModeSet));
        return Status;
    }

    pVbeModeInfo = &m_ModeInfo[m_CurrentMode];
    pMonitorSourceMode->VideoSignalInfo.VideoStandard = D3DKMDT_VSS_OTHER;
    pMonitorSourceMode->VideoSignalInfo.TotalSize.cx = pVbeModeInfo->XResolution;//m_CurrentModes[CorrespondingSourceId].DispInfo.Width;
    pMonitorSourceMode->VideoSignalInfo.TotalSize.cy = pVbeModeInfo->YResolution;//m_CurrentModes[CorrespondingSourceId].DispInfo.Height;
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
            DbgPrint(TRACE_LEVEL_ERROR, ("pfnAddMode failed with Status = 0x%I64x, hMonitorSourceModeSet = 0x%I64x, pMonitorSourceMode = 0x%I64x",
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
    return STATUS_SUCCESS;
}

// Tell DMM about all the modes, etc. that are supported
NTSTATUS QxlDod::EnumVidPnCofuncModality(_In_ CONST DXGKARG_ENUMVIDPNCOFUNCMODALITY* CONST pEnumCofuncModality)
{
    PAGED_CODE();

    QXL_ASSERT(pEnumCofuncModality != NULL);
    DbgPrint(TRACE_LEVEL_FATAL, ("---> %s\n", __FUNCTION__));

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

//FIXME
		
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

    DbgPrint(TRACE_LEVEL_FATAL, ("<--- %s\n", __FUNCTION__));
    return Status;
}

NTSTATUS QxlDod::SetVidPnSourceVisibility(_In_ CONST DXGKARG_SETVIDPNSOURCEVISIBILITY* pSetVidPnSourceVisibility)
{
    PAGED_CODE();
    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));
    QXL_ASSERT(pSetVidPnSourceVisibility != NULL);
    QXL_ASSERT((pSetVidPnSourceVisibility->VidPnSourceId < MAX_VIEWS) ||
               (pSetVidPnSourceVisibility->VidPnSourceId == D3DDDI_ID_ALL));

    UINT StartVidPnSourceId = (pSetVidPnSourceVisibility->VidPnSourceId == D3DDDI_ID_ALL) ? 0 : pSetVidPnSourceVisibility->VidPnSourceId;
    UINT MaxVidPnSourceId = (pSetVidPnSourceVisibility->VidPnSourceId == D3DDDI_ID_ALL) ? MAX_VIEWS : pSetVidPnSourceVisibility->VidPnSourceId + 1;

    for (UINT SourceId = StartVidPnSourceId; SourceId < MaxVidPnSourceId; ++SourceId)
    {
        if (pSetVidPnSourceVisibility->Visible)
        {
//            m_CurrentModes[SourceId].Flags.FullscreenPresent = TRUE;
        }
        else
        {
//            BlackOutScreen(SourceId);
        }

        // Store current visibility so it can be dealt with during Present call
//        m_CurrentModes[SourceId].Flags.SourceNotVisible = !(pSetVidPnSourceVisibility->Visible);
    }

    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
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

    NTSTATUS                                 Status = STATUS_SUCCESS;


//FIXME


    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
    return Status;
}

NTSTATUS QxlDod::UpdateActiveVidPnPresentPath(_In_ CONST DXGKARG_UPDATEACTIVEVIDPNPRESENTPATH* CONST pUpdateActiveVidPnPresentPath)
{
    PAGED_CODE();
    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));
    QXL_ASSERT(pUpdateActiveVidPnPresentPath != NULL);

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
    UNREFERENCED_PARAMETER(pColorFormat);
    UNREFERENCED_PARAMETER(pWidth);
    UNREFERENCED_PARAMETER(pHeight);
    QXL_ASSERT((TargetId < MAX_CHILDREN) || (TargetId == D3DDDI_ID_UNINITIALIZED));

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
            VbeModeInfo->BitsPerPixel == 32 &&
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
    PVBE_MODEINFO VBEMode = &m_ModeInfo[m_CurrentMode];

    return Status;
}

NTSTATUS QxlDod::VbeSetCurrentMode(PVIDEO_MODE RequestedMode)
{
    NTSTATUS Status = STATUS_SUCCESS;
    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));
    X86BIOS_REGISTERS regs = {0};
    regs.Eax = 0x4F02;
    regs.Edx = m_ModeNumbers[RequestedMode->RequestedMode];
    if (!x86BiosCall (0x10, &regs))
    {
        DbgPrint(TRACE_LEVEL_ERROR, ("x86BiosCall failed\n"));
        return STATUS_UNSUCCESSFUL;
    }
    m_CurrentMode = (USHORT)RequestedMode->RequestedMode;
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
    return Status;
}

NTSTATUS QxlDod::VbeSetPowerState(POWER_ACTION ActionType)
{
    NTSTATUS Status = STATUS_SUCCESS;
    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));

    X86BIOS_REGISTERS regs = {0};
    regs.Eax = 0x4F10;
    regs.Ebx = 1;
    switch (ActionType)
    {
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
    return Status;
}
