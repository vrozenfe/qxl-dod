#include "driver.h"
#include "QxlDod.h"

#pragma code_seg(push)
#pragma code_seg("INIT")
// BEGIN: Init Code

//
// Driver Entry point
//

int nDebugLevel = TRACE_LEVEL_ERROR;


extern "C"
NTSTATUS
DriverEntry(
    _In_  DRIVER_OBJECT*  pDriverObject,
    _In_  UNICODE_STRING* pRegistryPath)
{
    PAGED_CODE();

    DbgPrint(TRACE_LEVEL_FATAL, ("---> KMDOD build on on %s %s\n", __DATE__, __TIME__));

#ifdef DBG
//    KdBreakPoint();
#endif
    // Initialize DDI function pointers and dxgkrnl
    KMDDOD_INITIALIZATION_DATA InitialData = {0};

    InitialData.Version = DXGKDDI_INTERFACE_VERSION;

    InitialData.DxgkDdiAddDevice                    = DodAddDevice;
    InitialData.DxgkDdiStartDevice                  = DodStartDevice;
    InitialData.DxgkDdiStopDevice                   = DodStopDevice;
    InitialData.DxgkDdiResetDevice                  = DodResetDevice;
    InitialData.DxgkDdiRemoveDevice                 = DodRemoveDevice;
    InitialData.DxgkDdiDispatchIoRequest            = DodDispatchIoRequest;
    InitialData.DxgkDdiInterruptRoutine             = DodInterruptRoutine;
    InitialData.DxgkDdiDpcRoutine                   = DodDpcRoutine;
    InitialData.DxgkDdiQueryChildRelations          = DodQueryChildRelations;
    InitialData.DxgkDdiQueryChildStatus             = DodQueryChildStatus;
    InitialData.DxgkDdiQueryDeviceDescriptor        = DodQueryDeviceDescriptor;
    InitialData.DxgkDdiSetPowerState                = DodSetPowerState;
    InitialData.DxgkDdiUnload                       = DodUnload;
    InitialData.DxgkDdiQueryInterface               = DodQueryInterface;
    InitialData.DxgkDdiQueryAdapterInfo             = DodQueryAdapterInfo;
    InitialData.DxgkDdiSetPointerPosition           = DodSetPointerPosition;
    InitialData.DxgkDdiSetPointerShape              = DodSetPointerShape;
    InitialData.DxgkDdiEscape                       = DodEscape;
    InitialData.DxgkDdiIsSupportedVidPn             = DodIsSupportedVidPn;
    InitialData.DxgkDdiRecommendFunctionalVidPn     = DodRecommendFunctionalVidPn;
    InitialData.DxgkDdiEnumVidPnCofuncModality      = DodEnumVidPnCofuncModality;
    InitialData.DxgkDdiSetVidPnSourceVisibility     = DodSetVidPnSourceVisibility;
    InitialData.DxgkDdiCommitVidPn                  = DodCommitVidPn;
    InitialData.DxgkDdiUpdateActiveVidPnPresentPath = DodUpdateActiveVidPnPresentPath;
    InitialData.DxgkDdiRecommendMonitorModes        = DodRecommendMonitorModes;
    InitialData.DxgkDdiQueryVidPnHWCapability       = DodQueryVidPnHWCapability;
    InitialData.DxgkDdiPresentDisplayOnly           = DodPresentDisplayOnly;
    InitialData.DxgkDdiStopDeviceAndReleasePostDisplayOwnership = DodStopDeviceAndReleasePostDisplayOwnership;
    InitialData.DxgkDdiSystemDisplayEnable          = DodSystemDisplayEnable;
    InitialData.DxgkDdiSystemDisplayWrite           = DodSystemDisplayWrite;

    NTSTATUS Status = DxgkInitializeDisplayOnlyDriver(pDriverObject, pRegistryPath, &InitialData);
    if (!NT_SUCCESS(Status))
    {
        DbgPrint(TRACE_LEVEL_ERROR, ("DxgkInitializeDisplayOnlyDriver failed with Status: 0x%X\n", Status));
    }

    DbgPrint(TRACE_LEVEL_INFORMATION, ("<--- %s\n", __FUNCTION__));
    return Status;
}
// END: Init Code
#pragma code_seg(pop)

#pragma code_seg(push)
#pragma code_seg("PAGE")

//
// PnP DDIs
//

VOID
DodUnload(VOID)
{
    PAGED_CODE();
    DbgPrint(TRACE_LEVEL_INFORMATION, ("<--> %s\n", __FUNCTION__));
}

NTSTATUS
DodAddDevice(
    _In_ DEVICE_OBJECT* pPhysicalDeviceObject,
    _Outptr_ PVOID*  ppDeviceContext)
{
    PAGED_CODE();
    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));

    if ((pPhysicalDeviceObject == NULL) ||
        (ppDeviceContext == NULL))
    {
        DbgPrint(TRACE_LEVEL_ERROR, ("One of pPhysicalDeviceObject (0x%I64x), ppDeviceContext (0x%I64x) is NULL",
                        pPhysicalDeviceObject, ppDeviceContext));
        return STATUS_INVALID_PARAMETER;
    }
    *ppDeviceContext = NULL;

    QxlDod* pQxl = new(NonPagedPoolNx) QxlDod(pPhysicalDeviceObject);
    if (pQxl == NULL)
    {
        DbgPrint(TRACE_LEVEL_ERROR, ("pQxl failed to be allocated"));
        return STATUS_NO_MEMORY;
    }

    *ppDeviceContext = pQxl;

    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
    return STATUS_SUCCESS;
}

NTSTATUS
DodRemoveDevice(
    _In_  VOID* pDeviceContext)
{
    PAGED_CODE();
    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));

    QxlDod* pQxl = reinterpret_cast<QxlDod*>(pDeviceContext);

    if (pQxl)
    {
        delete pQxl;
        pQxl = NULL;
    }

    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
    return STATUS_SUCCESS;
}

NTSTATUS
DodStartDevice(
    _In_  VOID*              pDeviceContext,
    _In_  DXGK_START_INFO*   pDxgkStartInfo,
    _In_  DXGKRNL_INTERFACE* pDxgkInterface,
    _Out_ ULONG*             pNumberOfViews,
    _Out_ ULONG*             pNumberOfChildren)
{
    PAGED_CODE();
    QXL_ASSERT_CHK(pDeviceContext != NULL);
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<---> %s\n", __FUNCTION__));

    QxlDod* pQxl = reinterpret_cast<QxlDod*>(pDeviceContext);
    return pQxl->StartDevice(pDxgkStartInfo, pDxgkInterface, pNumberOfViews, pNumberOfChildren);
}

NTSTATUS
DodStopDevice(
    _In_  VOID* pDeviceContext)
{
    PAGED_CODE();
    QXL_ASSERT_CHK(pDeviceContext != NULL);
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<---> %s\n", __FUNCTION__));

    QxlDod* pQxl = reinterpret_cast<QxlDod*>(pDeviceContext);
    return pQxl->StopDevice();
}


NTSTATUS
DodDispatchIoRequest(
    _In_  VOID*                 pDeviceContext,
    _In_  ULONG                 VidPnSourceId,
    _In_  VIDEO_REQUEST_PACKET* pVideoRequestPacket)
{
    PAGED_CODE();
    QXL_ASSERT_CHK(pDeviceContext != NULL);
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<---> %s\n", __FUNCTION__));

    QxlDod* pQxl = reinterpret_cast<QxlDod*>(pDeviceContext);
    if (!pQxl->IsDriverActive())
    {
        QXL_LOG_ASSERTION1("QXL (0x%I64x) is being called when not active!", pQxl);
        return STATUS_UNSUCCESSFUL;
    }
    return pQxl->DispatchIoRequest(VidPnSourceId, pVideoRequestPacket);
}

NTSTATUS
DodSetPowerState(
    _In_  VOID*              pDeviceContext,
    _In_  ULONG              HardwareUid,
    _In_  DEVICE_POWER_STATE DevicePowerState,
    _In_  POWER_ACTION       ActionType)
{
    PAGED_CODE();
    QXL_ASSERT_CHK(pDeviceContext != NULL);
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<---> %s\n", __FUNCTION__));

    QxlDod* pQxl = reinterpret_cast<QxlDod*>(pDeviceContext);
    if (!pQxl->IsDriverActive())
    {
        // If the driver isn't active, SetPowerState can still be called, however in QXL's case
        // this shouldn't do anything, as it could for instance be called on QXL Fallback after
        // Fallback has been stopped and QXL PnP is being started. Fallback doesn't have control
        // of the hardware in this case.
        return STATUS_SUCCESS;
    }
    return pQxl->SetPowerState(HardwareUid, DevicePowerState, ActionType);
}

NTSTATUS
DodQueryChildRelations(
    _In_                             VOID*                  pDeviceContext,
    _Out_writes_bytes_(ChildRelationsSize) DXGK_CHILD_DESCRIPTOR* pChildRelations,
    _In_                             ULONG                  ChildRelationsSize)
{
    PAGED_CODE();
    QXL_ASSERT_CHK(pDeviceContext != NULL);
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<---> %s\n", __FUNCTION__));

    QxlDod* pQxl = reinterpret_cast<QxlDod*>(pDeviceContext);
    return pQxl->QueryChildRelations(pChildRelations, ChildRelationsSize);
}

NTSTATUS
DodQueryChildStatus(
    _In_    VOID*              pDeviceContext,
    _Inout_ DXGK_CHILD_STATUS* pChildStatus,
    _In_    BOOLEAN            NonDestructiveOnly)
{
    PAGED_CODE();
    QXL_ASSERT_CHK(pDeviceContext != NULL);
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<---> %s\n", __FUNCTION__));

    QxlDod* pQxl = reinterpret_cast<QxlDod*>(pDeviceContext);
    return pQxl->QueryChildStatus(pChildStatus, NonDestructiveOnly);
}

NTSTATUS
DodQueryDeviceDescriptor(
    _In_  VOID*                     pDeviceContext,
    _In_  ULONG                     ChildUid,
    _Inout_ DXGK_DEVICE_DESCRIPTOR* pDeviceDescriptor)
{
    PAGED_CODE();
    QXL_ASSERT_CHK(pDeviceContext != NULL);
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<---> %s\n", __FUNCTION__));

    QxlDod* pQxl = reinterpret_cast<QxlDod*>(pDeviceContext);
    if (!pQxl->IsDriverActive())
    {
        // During stress testing of PnPStop, it is possible for QXL Fallback to get called to start then stop in quick succession.
        // The first call queues a worker thread item indicating that it now has a child device, the second queues a worker thread
        // item that it no longer has any child device. This function gets called based on the first worker thread item, but after
        // the driver has been stopped. Therefore instead of asserting like other functions, we only warn.
        DbgPrint(TRACE_LEVEL_WARNING, ("QXL (0x%I64x) is being called when not active!", pQxl));
        return STATUS_UNSUCCESSFUL;
    }
    return pQxl->QueryDeviceDescriptor(ChildUid, pDeviceDescriptor);
}


//
// WDDM Display Only Driver DDIs
//

NTSTATUS
APIENTRY
DodQueryAdapterInfo(
    _In_ CONST HANDLE                    hAdapter,
    _In_ CONST DXGKARG_QUERYADAPTERINFO* pQueryAdapterInfo)
{
    PAGED_CODE();
    QXL_ASSERT_CHK(hAdapter != NULL);
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<---> %s\n", __FUNCTION__));

    QxlDod* pQxl = reinterpret_cast<QxlDod*>(hAdapter);
    return pQxl->QueryAdapterInfo(pQueryAdapterInfo);
}

NTSTATUS
APIENTRY
DodSetPointerPosition(
    _In_ CONST HANDLE                      hAdapter,
    _In_ CONST DXGKARG_SETPOINTERPOSITION* pSetPointerPosition)
{
    PAGED_CODE();
    QXL_ASSERT_CHK(hAdapter != NULL);
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<---> %s\n", __FUNCTION__));

    QxlDod* pQxl = reinterpret_cast<QxlDod*>(hAdapter);
    if (!pQxl->IsDriverActive())
    {
        QXL_LOG_ASSERTION1("QXL (0x%I64x) is being called when not active!", pQxl);
        return STATUS_UNSUCCESSFUL;
    }
    return pQxl->SetPointerPosition(pSetPointerPosition);
}

NTSTATUS
APIENTRY
DodSetPointerShape(
    _In_ CONST HANDLE                   hAdapter,
    _In_ CONST DXGKARG_SETPOINTERSHAPE* pSetPointerShape)
{
    PAGED_CODE();
    QXL_ASSERT_CHK(hAdapter != NULL);
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<---> %s\n", __FUNCTION__));

    QxlDod* pQxl = reinterpret_cast<QxlDod*>(hAdapter);
    if (!pQxl->IsDriverActive())
    {
        QXL_LOG_ASSERTION1("QXL (0x%I64x) is being called when not active!", pQxl);
        return STATUS_UNSUCCESSFUL;
    }
    return pQxl->SetPointerShape(pSetPointerShape);
}

NTSTATUS
APIENTRY
DodEscape(
    _In_ CONST HANDLE                 hAdapter,
    _In_ CONST DXGKARG_ESCAPE*        pEscape
    )
{
    PAGED_CODE();
    QXL_ASSERT_CHK(hAdapter != NULL);
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<---> %s\n", __FUNCTION__));

    QxlDod* pQxl = reinterpret_cast<QxlDod*>(hAdapter);
    return pQxl->Escape(pEscape);
}

NTSTATUS
DodQueryInterface(
    _In_ CONST PVOID          pDeviceContext,
    _In_ CONST PQUERY_INTERFACE     QueryInterface
    )
{
    PAGED_CODE();
    QXL_ASSERT_CHK(pDeviceContext != NULL);
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<---> %s\n", __FUNCTION__));

    QxlDod* pQxl = reinterpret_cast<QxlDod*>(pDeviceContext);
    return pQxl->QueryInterface(QueryInterface);
}

NTSTATUS
APIENTRY
DodPresentDisplayOnly(
    _In_ CONST HANDLE                       hAdapter,
    _In_ CONST DXGKARG_PRESENT_DISPLAYONLY* pPresentDisplayOnly)
{
    PAGED_CODE();
    QXL_ASSERT_CHK(hAdapter != NULL);
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<---> %s\n", __FUNCTION__));

    QxlDod* pQxl = reinterpret_cast<QxlDod*>(hAdapter);
    if (!pQxl->IsDriverActive())
    {
        QXL_LOG_ASSERTION1("QXL (0x%I64x) is being called when not active!", pQxl);
        return STATUS_UNSUCCESSFUL;
    }
    return pQxl->PresentDisplayOnly(pPresentDisplayOnly);
}

NTSTATUS
APIENTRY
DodStopDeviceAndReleasePostDisplayOwnership(
    _In_  VOID*                          pDeviceContext,
    _In_  D3DDDI_VIDEO_PRESENT_TARGET_ID TargetId,
    _Out_ DXGK_DISPLAY_INFORMATION*      DisplayInfo)
{
    PAGED_CODE();
    QXL_ASSERT_CHK(pDeviceContext != NULL);
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<---> %s\n", __FUNCTION__));

    QxlDod* pQxl = reinterpret_cast<QxlDod*>(pDeviceContext);
    return pQxl->StopDeviceAndReleasePostDisplayOwnership(TargetId, DisplayInfo);
}

NTSTATUS
APIENTRY
DodIsSupportedVidPn(
    _In_ CONST HANDLE                 hAdapter,
    _Inout_ DXGKARG_ISSUPPORTEDVIDPN* pIsSupportedVidPn)
{
    PAGED_CODE();
    QXL_ASSERT_CHK(hAdapter != NULL);
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<---> %s\n", __FUNCTION__));

    QxlDod* pQxl = reinterpret_cast<QxlDod*>(hAdapter);
    if (!pQxl->IsDriverActive())
    {
        // This path might hit because win32k/dxgport doesn't check that an adapter is active when taking the adapter lock.
        // The adapter lock is the main thing QXL Fallback relies on to not be called while it's inactive. It is still a rare
        // timing issue around PnpStart/Stop and isn't expected to have any effect on the stability of the system.
        DbgPrint(TRACE_LEVEL_WARNING, ("QXL (0x%I64x) is being called when not active!", pQxl));
        return STATUS_UNSUCCESSFUL;
    }
    return pQxl->IsSupportedVidPn(pIsSupportedVidPn);
}

NTSTATUS
APIENTRY
DodRecommendFunctionalVidPn(
    _In_ CONST HANDLE                                  hAdapter,
    _In_ CONST DXGKARG_RECOMMENDFUNCTIONALVIDPN* CONST pRecommendFunctionalVidPn)
{
    PAGED_CODE();
    QXL_ASSERT_CHK(hAdapter != NULL);
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<---> %s\n", __FUNCTION__));

    QxlDod* pQxl = reinterpret_cast<QxlDod*>(hAdapter);
    if (!pQxl->IsDriverActive())
    {
        QXL_LOG_ASSERTION1("QXL (0x%I64x) is being called when not active!", pQxl);
        return STATUS_UNSUCCESSFUL;
    }
    return pQxl->RecommendFunctionalVidPn(pRecommendFunctionalVidPn);
}

NTSTATUS
APIENTRY
DodRecommendVidPnTopology(
    _In_ CONST HANDLE                                 hAdapter,
    _In_ CONST DXGKARG_RECOMMENDVIDPNTOPOLOGY* CONST  pRecommendVidPnTopology)
{
    PAGED_CODE();
    QXL_ASSERT_CHK(hAdapter != NULL);
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<---> %s\n", __FUNCTION__));

    QxlDod* pQxl = reinterpret_cast<QxlDod*>(hAdapter);
    if (!pQxl->IsDriverActive())
    {
        QXL_LOG_ASSERTION1("QXL (0x%I64x) is being called when not active!", pQxl);
        return STATUS_UNSUCCESSFUL;
    }
    return pQxl->RecommendVidPnTopology(pRecommendVidPnTopology);
}

NTSTATUS
APIENTRY
DodRecommendMonitorModes(
    _In_ CONST HANDLE                                hAdapter,
    _In_ CONST DXGKARG_RECOMMENDMONITORMODES* CONST  pRecommendMonitorModes)
{
    PAGED_CODE();
    QXL_ASSERT_CHK(hAdapter != NULL);
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<---> %s\n", __FUNCTION__));

    QxlDod* pQxl = reinterpret_cast<QxlDod*>(hAdapter);
    if (!pQxl->IsDriverActive())
    {
        QXL_LOG_ASSERTION1("QXL (0x%I64x) is being called when not active!", pQxl);
        return STATUS_UNSUCCESSFUL;
    }
    return pQxl->RecommendMonitorModes(pRecommendMonitorModes);
}

NTSTATUS
APIENTRY
DodEnumVidPnCofuncModality(
    _In_ CONST HANDLE                                 hAdapter,
    _In_ CONST DXGKARG_ENUMVIDPNCOFUNCMODALITY* CONST pEnumCofuncModality)
{
    PAGED_CODE();
    QXL_ASSERT_CHK(hAdapter != NULL);
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<---> %s\n", __FUNCTION__));

    QxlDod* pQxl = reinterpret_cast<QxlDod*>(hAdapter);
    if (!pQxl->IsDriverActive())
    {
        QXL_LOG_ASSERTION1("QXL (0x%I64x) is being called when not active!", pQxl);
        return STATUS_UNSUCCESSFUL;
    }
    return pQxl->EnumVidPnCofuncModality(pEnumCofuncModality);
}

NTSTATUS
APIENTRY
DodSetVidPnSourceVisibility(
    _In_ CONST HANDLE                            hAdapter,
    _In_ CONST DXGKARG_SETVIDPNSOURCEVISIBILITY* pSetVidPnSourceVisibility)
{
    PAGED_CODE();
    QXL_ASSERT_CHK(hAdapter != NULL);
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<---> %s\n", __FUNCTION__));

    QxlDod* pQxl = reinterpret_cast<QxlDod*>(hAdapter);
    if (!pQxl->IsDriverActive())
    {
        QXL_LOG_ASSERTION1("QXL (0x%I64x) is being called when not active!", pQxl);
        return STATUS_UNSUCCESSFUL;
    }
    return pQxl->SetVidPnSourceVisibility(pSetVidPnSourceVisibility);
}

NTSTATUS
APIENTRY
DodCommitVidPn(
    _In_ CONST HANDLE                     hAdapter,
    _In_ CONST DXGKARG_COMMITVIDPN* CONST pCommitVidPn)
{
    PAGED_CODE();
    QXL_ASSERT_CHK(hAdapter != NULL);
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<---> %s\n", __FUNCTION__));

    QxlDod* pQxl = reinterpret_cast<QxlDod*>(hAdapter);
    if (!pQxl->IsDriverActive())
    {
        QXL_LOG_ASSERTION1("QXL (0x%I64x) is being called when not active!", pQxl);
        return STATUS_UNSUCCESSFUL;
    }
    return pQxl->CommitVidPn(pCommitVidPn);
}

NTSTATUS
APIENTRY
DodUpdateActiveVidPnPresentPath(
    _In_ CONST HANDLE                                      hAdapter,
    _In_ CONST DXGKARG_UPDATEACTIVEVIDPNPRESENTPATH* CONST pUpdateActiveVidPnPresentPath)
{
    PAGED_CODE();
    QXL_ASSERT_CHK(hAdapter != NULL);
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<---> %s\n", __FUNCTION__));

    QxlDod* pQxl = reinterpret_cast<QxlDod*>(hAdapter);
    if (!pQxl->IsDriverActive())
    {
        QXL_LOG_ASSERTION1("QXL (0x%I64x) is being called when not active!", pQxl);
        return STATUS_UNSUCCESSFUL;
    }
    return pQxl->UpdateActiveVidPnPresentPath(pUpdateActiveVidPnPresentPath);
}

NTSTATUS
APIENTRY
DodQueryVidPnHWCapability(
    _In_ CONST HANDLE                       hAdapter,
    _Inout_ DXGKARG_QUERYVIDPNHWCAPABILITY* pVidPnHWCaps)
{
    PAGED_CODE();
    QXL_ASSERT_CHK(hAdapter != NULL);
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<---> %s\n", __FUNCTION__));

    QxlDod* pQxl = reinterpret_cast<QxlDod*>(hAdapter);
    if (!pQxl->IsDriverActive())
    {
        QXL_LOG_ASSERTION1("QXL (0x%I64x) is being called when not active!", pQxl);
        return STATUS_UNSUCCESSFUL;
    }
    return pQxl->QueryVidPnHWCapability(pVidPnHWCaps);
}

//END: Paged Code
#pragma code_seg(pop)

#pragma code_seg(push)
#pragma code_seg()
// BEGIN: Non-Paged Code

VOID
DodDpcRoutine(
    _In_  VOID* pDeviceContext)
{
    QXL_ASSERT_CHK(pDeviceContext != NULL);
    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));

    QxlDod* pQxl = reinterpret_cast<QxlDod*>(pDeviceContext);
    if (!pQxl->IsDriverActive())
    {
        QXL_LOG_ASSERTION1("QXL (0x%I64x) is being called when not active!", pQxl);
        return;
    }
    pQxl->DpcRoutine();
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
}

BOOLEAN
DodInterruptRoutine(
    _In_  VOID* pDeviceContext,
    _In_  ULONG MessageNumber)
{
    QXL_ASSERT_CHK(pDeviceContext != NULL);
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<---> %s\n", __FUNCTION__));

    QxlDod* pQxl = reinterpret_cast<QxlDod*>(pDeviceContext);
    return pQxl->InterruptRoutine(MessageNumber);
}

VOID
DodResetDevice(
    _In_  VOID* pDeviceContext)
{
    QXL_ASSERT_CHK(pDeviceContext != NULL);
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<---> %s\n", __FUNCTION__));

    QxlDod* pQxl = reinterpret_cast<QxlDod*>(pDeviceContext);
    pQxl->ResetDevice();
}

NTSTATUS
APIENTRY
DodSystemDisplayEnable(
    _In_  VOID* pDeviceContext,
    _In_  D3DDDI_VIDEO_PRESENT_TARGET_ID TargetId,
    _In_  PDXGKARG_SYSTEM_DISPLAY_ENABLE_FLAGS Flags,
    _Out_ UINT* Width,
    _Out_ UINT* Height,
    _Out_ D3DDDIFORMAT* ColorFormat)
{
    QXL_ASSERT_CHK(pDeviceContext != NULL);
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<---> %s\n", __FUNCTION__));

    QxlDod* pQxl = reinterpret_cast<QxlDod*>(pDeviceContext);
    return pQxl->SystemDisplayEnable(TargetId, Flags, Width, Height, ColorFormat);
}

VOID
APIENTRY
DodSystemDisplayWrite(
    _In_  VOID* pDeviceContext,
    _In_  VOID* Source,
    _In_  UINT  SourceWidth,
    _In_  UINT  SourceHeight,
    _In_  UINT  SourceStride,
    _In_  UINT  PositionX,
    _In_  UINT  PositionY)
{
    QXL_ASSERT_CHK(pDeviceContext != NULL);
    DbgPrint(TRACE_LEVEL_INFORMATION, ("<---> %s\n", __FUNCTION__));

    QxlDod* pQxl = reinterpret_cast<QxlDod*>(pDeviceContext);
    pQxl->SystemDisplayWrite(Source, SourceWidth, SourceHeight, SourceStride, PositionX, PositionY);
}

#if defined(DBG)

#define RHEL_DEBUG_PORT     ((PUCHAR)0x3F8)
#define TEMP_BUFFER_SIZE	256

void DebugPrintFuncSerial(const char *format, ...)
{
    char buf[TEMP_BUFFER_SIZE];
    NTSTATUS status;
    size_t len;
    va_list list;
    va_start(list, format);
    status = RtlStringCbVPrintfA(buf, sizeof(buf), format, list);
    if (status == STATUS_SUCCESS)
    {
        len = strlen(buf);
    }
    else
    {
        len = 2;
        buf[0] = 'O';
        buf[1] = '\n';
    }
    if (len)
    {
        WRITE_PORT_BUFFER_UCHAR(RHEL_DEBUG_PORT, (PUCHAR)buf, (ULONG)len);
        WRITE_PORT_UCHAR(RHEL_DEBUG_PORT, '\r');
    }
}

void DebugPrintFunc(const char *format, ...)
{
    va_list list;
    va_start(list, format);
    vDbgPrintEx(DPFLTR_DEFAULT_ID, 9 | DPFLTR_MASK, format, list);
}
#endif

#pragma code_seg(pop) // End Non-Paged Code

