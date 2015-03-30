#pragma once
#include "BaseObject.h"


extern "C"
DRIVER_INITIALIZE DriverEntry;

//
// PnP DDIs
//

VOID
DodUnload(VOID);

// If uncommenting ENABLE_DXGK_SAL in the sources file, all the below function prototypes should be updated to use
// the function typedef's from the header files. Additionally, annotations on the function definitions can be removed
// as they are inherited from the prototype definition here. As an example the entire 4-line prototype for BddDdiAddDevice
// is replaced by the single commented line below:
// DXGKDDI_ADD_DEVICE BddDdiAddDevice;
NTSTATUS
DodAddDevice(
    _In_ DEVICE_OBJECT* pPhysicalDeviceObject,
    _Outptr_ PVOID*  ppDeviceContext);

NTSTATUS
DodRemoveDevice(
    _In_  VOID* pDeviceContext);

NTSTATUS
DodStartDevice(
    _In_  VOID*              pDeviceContext,
    _In_  DXGK_START_INFO*   pDxgkStartInfo,
    _In_  DXGKRNL_INTERFACE* pDxgkInterface,
    _Out_ ULONG*             pNumberOfViews,
    _Out_ ULONG*             pNumberOfChildren);

NTSTATUS
DodStopDevice(
    _In_  VOID* pDeviceContext);

VOID
DodResetDevice(
    _In_  VOID* pDeviceContext);


NTSTATUS
DodDispatchIoRequest(
    _In_  VOID*                 pDeviceContext,
    _In_  ULONG                 VidPnSourceId,
    _In_  VIDEO_REQUEST_PACKET* pVideoRequestPacket);

NTSTATUS
DodSetPowerState(
    _In_  VOID*              pDeviceContext,
    _In_  ULONG              HardwareUid,
    _In_  DEVICE_POWER_STATE DevicePowerState,
    _In_  POWER_ACTION       ActionType);

NTSTATUS
DodQueryChildRelations(
    _In_                             VOID*                  pDeviceContext,
    _Out_writes_bytes_(ChildRelationsSize) DXGK_CHILD_DESCRIPTOR* pChildRelations,
    _In_                             ULONG                  ChildRelationsSize);

NTSTATUS
DodQueryChildStatus(
    _In_    VOID*              pDeviceContext,
    _Inout_ DXGK_CHILD_STATUS* pChildStatus,
    _In_    BOOLEAN            NonDestructiveOnly);

NTSTATUS
DodQueryDeviceDescriptor(
    _In_  VOID*                     pDeviceContext,
    _In_  ULONG                     ChildUid,
    _Inout_ DXGK_DEVICE_DESCRIPTOR* pDeviceDescriptor);

// Must be Non-Paged
BOOLEAN
DodInterruptRoutine(
    _In_  VOID* pDeviceContext,
    _In_  ULONG MessageNumber);

VOID
DodDpcRoutine(
    _In_  VOID* pDeviceContext);

//
// WDDM Display Only Driver DDIs
//

NTSTATUS
APIENTRY
DodQueryAdapterInfo(
    _In_ CONST HANDLE                         hAdapter,
    _In_ CONST DXGKARG_QUERYADAPTERINFO*      pQueryAdapterInfo);

NTSTATUS
APIENTRY
DodSetPointerPosition(
    _In_ CONST HANDLE                         hAdapter,
    _In_ CONST DXGKARG_SETPOINTERPOSITION*    pSetPointerPosition);

NTSTATUS
APIENTRY
DodSetPointerShape(
    _In_ CONST HANDLE                         hAdapter,
    _In_ CONST DXGKARG_SETPOINTERSHAPE*       pSetPointerShape);

NTSTATUS
APIENTRY
DodEscape(
    _In_ CONST HANDLE                         hAdapter,
    _In_ CONST DXGKARG_ESCAPE*                pEscape);

NTSTATUS
DodQueryInterface(
    _In_ CONST PVOID                          pDeviceContext,
    _In_ CONST PQUERY_INTERFACE               pQueryInterface);

NTSTATUS
APIENTRY
DodPresentDisplayOnly(
    _In_ CONST HANDLE                         hAdapter,
    _In_ CONST DXGKARG_PRESENT_DISPLAYONLY*   pPresentDisplayOnly);

NTSTATUS
APIENTRY
DodIsSupportedVidPn(
    _In_ CONST HANDLE                         hAdapter,
    _Inout_ DXGKARG_ISSUPPORTEDVIDPN*         pIsSupportedVidPn);

NTSTATUS
APIENTRY
DodRecommendFunctionalVidPn(
    _In_ CONST HANDLE                                   hAdapter,
    _In_ CONST DXGKARG_RECOMMENDFUNCTIONALVIDPN* CONST  pRecommendFunctionalVidPn);

NTSTATUS
APIENTRY
DodRecommendVidPnTopology(
    _In_ CONST HANDLE                                 hAdapter,
    _In_ CONST DXGKARG_RECOMMENDVIDPNTOPOLOGY* CONST  pRecommendVidPnTopology);

NTSTATUS
APIENTRY
DodRecommendMonitorModes(
    _In_ CONST HANDLE                                hAdapter,
    _In_ CONST DXGKARG_RECOMMENDMONITORMODES* CONST  pRecommendMonitorModes);

NTSTATUS
APIENTRY
DodEnumVidPnCofuncModality(
    _In_ CONST HANDLE                                  hAdapter,
    _In_ CONST DXGKARG_ENUMVIDPNCOFUNCMODALITY* CONST  pEnumCofuncModality);

NTSTATUS
APIENTRY
DodSetVidPnSourceVisibility(
    _In_ CONST HANDLE                             hAdapter,
    _In_ CONST DXGKARG_SETVIDPNSOURCEVISIBILITY*  pSetVidPnSourceVisibility);

NTSTATUS
APIENTRY
DodCommitVidPn(
    _In_ CONST HANDLE                         hAdapter,
    _In_ CONST DXGKARG_COMMITVIDPN* CONST     pCommitVidPn);

NTSTATUS
APIENTRY
DodUpdateActiveVidPnPresentPath(
    _In_ CONST HANDLE                                       hAdapter,
    _In_ CONST DXGKARG_UPDATEACTIVEVIDPNPRESENTPATH* CONST  pUpdateActiveVidPnPresentPath);

NTSTATUS
APIENTRY
DodQueryVidPnHWCapability(
    _In_ CONST HANDLE                         hAdapter,
    _Inout_ DXGKARG_QUERYVIDPNHWCAPABILITY*   pVidPnHWCaps);

NTSTATUS
APIENTRY
DodStopDeviceAndReleasePostDisplayOwnership(
    _In_  VOID*                          pDeviceContext,
    _In_  D3DDDI_VIDEO_PRESENT_TARGET_ID TargetId,
    _Out_ DXGK_DISPLAY_INFORMATION*      DisplayInfo);

// Must be Non-Paged
NTSTATUS
APIENTRY
DodSystemDisplayEnable(
    _In_  VOID* pDeviceContext,
    _In_  D3DDDI_VIDEO_PRESENT_TARGET_ID TargetId,
    _In_  PDXGKARG_SYSTEM_DISPLAY_ENABLE_FLAGS Flags,
    _Out_ UINT* Width,
    _Out_ UINT* Height,
    _Out_ D3DDDIFORMAT* ColorFormat);

// Must be Non-Paged
VOID
APIENTRY
DodSystemDisplayWrite(
    _In_  VOID* pDeviceContext,
    _In_  VOID* Source,
    _In_  UINT  SourceWidth,
    _In_  UINT  SourceHeight,
    _In_  UINT  SourceStride,
    _In_  UINT  PositionX,
    _In_  UINT  PositionY);

#define DBG 1

#if DBG

extern int nDebugLevel;
void DebugPrintFuncSerial(const char *format, ...);

void DebugPrintFunc(const char *format, ...);

#define DbgPrint(level, line) \
    if (level > nDebugLevel) {} \
    else DebugPrintFuncSerial line
#else
#define DbgPrint(level, line) 
#endif

//    else if (0) DebugPrintFuncSerial line \


#ifndef TRACE_LEVEL_INFORMATION
#define TRACE_LEVEL_NONE        0   // Tracing is not on
#define TRACE_LEVEL_FATAL       1   // Abnormal exit or termination
#define TRACE_LEVEL_ERROR       2   // Severe errors that need logging
#define TRACE_LEVEL_WARNING     3   // Warnings such as allocation failure
#define TRACE_LEVEL_INFORMATION 4   // Includes non-error cases(e.g.,Entry-Exit)
#define TRACE_LEVEL_VERBOSE     5   // Detailed traces from intermediate steps
#define TRACE_LEVEL_RESERVED6   6
#define TRACE_LEVEL_RESERVED7   7
#define TRACE_LEVEL_RESERVED8   8
#define TRACE_LEVEL_RESERVED9   9
#endif // TRACE_LEVEL_INFORMATION

#define QXL_LOG_ASSERTION0(Msg) NT_ASSERT(FALSE)
#define QXL_LOG_ASSERTION1(Msg,Param1) NT_ASSERT(FALSE)
#define QXL_LOG_ASSERTION2(Msg,Param1,Param2) NT_ASSERT(FALSE)
#define QXL_LOG_ASSERTION3(Msg,Param1,Param2,Param3) NT_ASSERT(FALSE)
#define QXL_LOG_ASSERTION4(Msg,Param1,Param2,Param3,Param4) NT_ASSERT(FALSE)
#define QXL_LOG_ASSERTION5(Msg,Param1,Param2,Param3,Param4,Param5) NT_ASSERT(FALSE)
#define QXL_ASSERT(exp) {if (!(exp)) {QXL_LOG_ASSERTION0(#exp);}}


#if DBG
#define QXL_ASSERT_CHK(exp) QXL_ASSERT(exp)
#else
#define QXL_ASSERT_CHK(exp) {}
#endif
