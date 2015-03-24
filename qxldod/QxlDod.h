#pragma once
#include "baseobject.h"
#include "qxl_dev.h"
#include "mspace.h"

#define MAX_CHILDREN               1
#define MAX_VIEWS                  1
#define BITS_PER_BYTE              8

#define POINTER_SIZE               64
#define MIN_WIDTH_SIZE             1024
#define MIN_HEIGHT_SIZE            768
#define QXL_BPP                    32
#define VGA_BPP                    24

typedef struct _QXL_FLAGS
{
    UINT DriverStarted           : 1; // ( 1) 1 after StartDevice and 0 after StopDevice
    UINT Unused                  : 31;
} QXL_FLAGS;

// For the following macros, c must be a UCHAR.
#define UPPER_6_BITS(c)   (((c) & rMaskTable[6 - 1]) >> 2)
#define UPPER_5_BITS(c)   (((c) & rMaskTable[5 - 1]) >> 3)
#define LOWER_6_BITS(c)   (((BYTE)(c)) & lMaskTable[BITS_PER_BYTE - 6])
#define LOWER_5_BITS(c)   (((BYTE)(c)) & lMaskTable[BITS_PER_BYTE - 5])


#define SHIFT_FOR_UPPER_5_IN_565   (6 + 5)
#define SHIFT_FOR_MIDDLE_6_IN_565  (5)
#define SHIFT_UPPER_5_IN_565_BACK  ((BITS_PER_BYTE * 2) + (BITS_PER_BYTE - 5))
#define SHIFT_MIDDLE_6_IN_565_BACK ((BITS_PER_BYTE * 1) + (BITS_PER_BYTE - 6))
#define SHIFT_LOWER_5_IN_565_BACK  ((BITS_PER_BYTE * 0) + (BITS_PER_BYTE - 5))

#pragma pack(push)
#pragma pack(1)

typedef struct
{
    CHAR Signature[4];
    USHORT Version;
    ULONG OemStringPtr;
    LONG Capabilities;
    ULONG VideoModePtr;
    USHORT TotalMemory;
    USHORT OemSoftwareRevision;
    ULONG OemVendorNamePtr;
    ULONG OemProductNamePtr;
    ULONG OemProductRevPtr;
    CHAR Reserved[222];
} VBE_INFO, *PVBE_INFO;

typedef struct
{
/* Mandatory information for all VBE revisions */
    USHORT ModeAttributes;
    UCHAR WinAAttributes;
    UCHAR WinBAttributes;
    USHORT WinGranularity;
    USHORT WinSize;
    USHORT WinASegment;
    USHORT WinBSegment;
    ULONG WinFuncPtr;
    USHORT BytesPerScanLine;
/* Mandatory information for VBE 1.2 and above */
    USHORT XResolution;
    USHORT YResolution;
    UCHAR XCharSize;
    UCHAR YCharSize;
    UCHAR NumberOfPlanes;
    UCHAR BitsPerPixel;
    UCHAR NumberOfBanks;
    UCHAR MemoryModel;
    UCHAR BankSize;
    UCHAR NumberOfImagePages;
    UCHAR Reserved1;
/* Direct Color fields (required for Direct/6 and YUV/7 memory models) */
    UCHAR RedMaskSize;
    UCHAR RedFieldPosition;
    UCHAR GreenMaskSize;
    UCHAR GreenFieldPosition;
    UCHAR BlueMaskSize;
    UCHAR BlueFieldPosition;
    UCHAR ReservedMaskSize;
    UCHAR ReservedFieldPosition;
    UCHAR DirectColorModeInfo;
/* Mandatory information for VBE 2.0 and above */
    ULONG PhysBasePtr;
    ULONG Reserved2;
    USHORT Reserved3;
    /* Mandatory information for VBE 3.0 and above */
    USHORT LinBytesPerScanLine;
    UCHAR BnkNumberOfImagePages;
    UCHAR LinNumberOfImagePages;
    UCHAR LinRedMaskSize;
    UCHAR LinRedFieldPosition;
    UCHAR LinGreenMaskSize;
    UCHAR LinGreenFieldPosition;
    UCHAR LinBlueMaskSize;
    UCHAR LinBlueFieldPosition;
    UCHAR LinReservedMaskSize;
    UCHAR LinReservedFieldPosition;
    ULONG MaxPixelClock;
    CHAR Reserved4[189];
} VBE_MODEINFO, *PVBE_MODEINFO;

#pragma pack(pop)

typedef struct _X86BIOS_REGISTERS	// invented names
{
    ULONG Eax;
    ULONG Ecx;
    ULONG Edx;
    ULONG Ebx;
    ULONG Ebp;
    ULONG Esi;
    ULONG Edi;
    USHORT SegDs;
    USHORT SegEs;
} X86BIOS_REGISTERS, *PX86BIOS_REGISTERS;

/*  Undocumented imports from the HAL  */

#ifdef __cplusplus
extern "C" {
#endif

NTHALAPI BOOLEAN x86BiosCall (ULONG, PX86BIOS_REGISTERS);

NTHALAPI NTSTATUS x86BiosAllocateBuffer (ULONG *, USHORT *, USHORT *);
NTHALAPI NTSTATUS x86BiosFreeBuffer (USHORT, USHORT);

NTHALAPI NTSTATUS x86BiosReadMemory (USHORT, USHORT, PVOID, ULONG);
NTHALAPI NTSTATUS x86BiosWriteMemory (USHORT, USHORT, PVOID, ULONG);

#ifdef __cplusplus
}
#endif

struct DoPresentMemory
{
    PVOID                     DstAddr;
    UINT                      DstStride;
    ULONG                     DstBitPerPixel;
    UINT                      SrcWidth;
    UINT                      SrcHeight;
    BYTE*                     SrcAddr;
    LONG                      SrcPitch;
    ULONG                     NumMoves;             // in:  Number of screen to screen moves
    D3DKMT_MOVE_RECT*         Moves;               // in:  Point to the list of moves
    ULONG                     NumDirtyRects;        // in:  Number of direct rects
    RECT*                     DirtyRect;           // in:  Point to the list of dirty rects
    D3DKMDT_VIDPN_PRESENT_PATH_ROTATION Rotation;
    D3DDDI_VIDEO_PRESENT_SOURCE_ID  SourceID;
    HANDLE                    hAdapter;
    PMDL                      Mdl;
    PVOID                     DisplaySource;
};

typedef struct _BLT_INFO
{
    PVOID pBits;
    UINT Pitch;
    UINT BitsPerPel;
    POINT Offset; // To unrotated top-left of dirty rects
    D3DKMDT_VIDPN_PRESENT_PATH_ROTATION Rotation;
    UINT Width; // For the unrotated image
    UINT Height; // For the unrotated image
} BLT_INFO;

// Represents the current mode, may not always be set (i.e. frame buffer mapped) if representing the mode passed in on single mode setups.
typedef struct _CURRENT_BDD_MODE
{
    // The source mode currently set for HW Framebuffer
    // For sample driver this info filled in StartDevice by the OS and never changed.
    DXGK_DISPLAY_INFORMATION             DispInfo;

    // The rotation of the current mode. Rotation is performed in software during Present call
    D3DKMDT_VIDPN_PRESENT_PATH_ROTATION  Rotation;

    D3DKMDT_VIDPN_PRESENT_PATH_SCALING Scaling;
    // This mode might be different from one which are supported for HW frame buffer
    // Scaling/displasment might be needed (if supported)
    UINT SrcModeWidth;
    UINT SrcModeHeight;

    // Various boolean flags the struct uses
    struct _CURRENT_BDD_MODE_FLAGS
    {
        UINT SourceNotVisible     : 1; // 0 if source is visible
        UINT FullscreenPresent    : 1; // 0 if should use dirty rects for present
        UINT FrameBufferIsActive  : 1; // 0 if not currently active (i.e. target not connected to source)
        UINT DoNotMapOrUnmap      : 1; // 1 if the FrameBuffer should not be (un)mapped during normal execution
        UINT IsInternal           : 1; // 1 if it was determined (i.e. through ACPI) that an internal panel is being driven
        UINT Unused               : 27;
    } Flags;

    // The start and end of physical memory known to be all zeroes. Used to optimize the BlackOutScreen function to not write
    // zeroes to memory already known to be zero. (Physical address is located in DispInfo)
    PHYSICAL_ADDRESS ZeroedOutStart;
    PHYSICAL_ADDRESS ZeroedOutEnd;

    // Linear frame buffer pointer
    // A union with a ULONG64 is used here to ensure this struct looks the same on 32bit and 64bit builds
    // since the size of a VOID* changes depending on the build.
    union
    {
        VOID*                            Ptr;
        ULONG64                          Force8Bytes;
    } FrameBuffer;
} CURRENT_BDD_MODE;

class QxlDod;

class HwDeviceIntrface {
public:
    virtual NTSTATUS QueryCurrentMode(PVIDEO_MODE RequestedMode) = 0;
    virtual NTSTATUS SetCurrentMode(ULONG Mode) = 0;
    virtual NTSTATUS GetCurrentMode(ULONG* Mode) = 0;
    virtual NTSTATUS SetPowerState(DEVICE_POWER_STATE DevicePowerState, DXGK_DISPLAY_INFORMATION* pDispInfo) = 0;
    virtual NTSTATUS HWInit(PCM_RESOURCE_LIST pResList, DXGK_DISPLAY_INFORMATION* pDispInfo) = 0;
    virtual NTSTATUS HWClose(void) = 0;
    virtual BOOLEAN InterruptRoutine(_In_ PDXGKRNL_INTERFACE pDxgkInterface, _In_  ULONG MessageNumber) = 0;
    virtual VOID DpcRoutine(PVOID) = 0;
    virtual VOID ResetDevice(void) = 0;

    virtual ULONG GetModeCount(void) = 0;
    PVIDEO_MODE_INFORMATION GetModeInfo(UINT idx) {return &m_ModeInfo[idx];}
    USHORT GetModeNumber(USHORT idx) {return m_ModeNumbers[idx];}
    USHORT GetCurrentModeIndex(void) {return m_CurrentMode;}
    VOID SetCurrentModeIndex(USHORT idx) {m_CurrentMode = idx;}
    virtual BOOLEAN EnablePointer(void) = 0;
    virtual NTSTATUS ExecutePresentDisplayOnly(_In_ BYTE*             DstAddr,
                                 _In_ UINT              DstBitPerPixel,
                                 _In_ BYTE*             SrcAddr,
                                 _In_ UINT              SrcBytesPerPixel,
                                 _In_ LONG              SrcPitch,
                                 _In_ ULONG             NumMoves,
                                 _In_ D3DKMT_MOVE_RECT* pMoves,
                                 _In_ ULONG             NumDirtyRects,
                                 _In_ RECT*             pDirtyRect,
                                 _In_ D3DKMDT_VIDPN_PRESENT_PATH_ROTATION Rotation,
                                 _In_ const CURRENT_BDD_MODE* pModeCur) = 0;

    virtual VOID BlackOutScreen(CURRENT_BDD_MODE* pCurrentBddMod) = 0;
    virtual NTSTATUS SetPointerShape(_In_ CONST DXGKARG_SETPOINTERSHAPE* pSetPointerShape) = 0;
    virtual NTSTATUS SetPointerPosition(_In_ CONST DXGKARG_SETPOINTERPOSITION* pSetPointerPosition) = 0;
    virtual NTSTATUS Escape(_In_ CONST DXGKARG_ESCAPE* pEscap) = 0;
    ULONG GetId(void) { return m_Id; }
protected:
    virtual NTSTATUS GetModeList(DXGK_DISPLAY_INFORMATION* pDispInfo) = 0;
protected:
    QxlDod* m_pQxlDod;
    PVIDEO_MODE_INFORMATION m_ModeInfo;
    ULONG m_ModeCount;
    PUSHORT m_ModeNumbers;
    USHORT m_CurrentMode;
    USHORT m_CustomMode;
    ULONG  m_Id;
};

class VgaDevice  :
    public HwDeviceIntrface
{
public:
    VgaDevice(_In_ QxlDod* pQxlDod);
    virtual ~VgaDevice(void);
    NTSTATUS QueryCurrentMode(PVIDEO_MODE RequestedMode);
    NTSTATUS SetCurrentMode(ULONG Mode);
    NTSTATUS GetCurrentMode(ULONG* Mode);
    ULONG GetModeCount(void) {return m_ModeCount;}
    NTSTATUS SetPowerState(DEVICE_POWER_STATE DevicePowerState, DXGK_DISPLAY_INFORMATION* pDispInfo);
    NTSTATUS HWInit(PCM_RESOURCE_LIST pResList, DXGK_DISPLAY_INFORMATION* pDispInfo);
    NTSTATUS HWClose(void);
    BOOLEAN EnablePointer(void) { return FALSE; }
    NTSTATUS ExecutePresentDisplayOnly(_In_ BYTE*             DstAddr,
                                 _In_ UINT              DstBitPerPixel,
                                 _In_ BYTE*             SrcAddr,
                                 _In_ UINT              SrcBytesPerPixel,
                                 _In_ LONG              SrcPitch,
                                 _In_ ULONG             NumMoves,
                                 _In_ D3DKMT_MOVE_RECT* pMoves,
                                 _In_ ULONG             NumDirtyRects,
                                 _In_ RECT*             pDirtyRect,
                                 _In_ D3DKMDT_VIDPN_PRESENT_PATH_ROTATION Rotation,
                                 _In_ const CURRENT_BDD_MODE* pModeCur);
    VOID BlackOutScreen(CURRENT_BDD_MODE* pCurrentBddMod);
    BOOLEAN InterruptRoutine(_In_ PDXGKRNL_INTERFACE pDxgkInterface, _In_  ULONG MessageNumber);
    VOID DpcRoutine(PVOID);
    VOID ResetDevice(VOID);
    NTSTATUS SetPointerShape(_In_ CONST DXGKARG_SETPOINTERSHAPE* pSetPointerShape);
    NTSTATUS SetPointerPosition(_In_ CONST DXGKARG_SETPOINTERPOSITION* pSetPointerPosition);
    NTSTATUS Escape(_In_ CONST DXGKARG_ESCAPE* pEscap);
protected:
    NTSTATUS GetModeList(DXGK_DISPLAY_INFORMATION* pDispInfo);
private:
    BOOL SetVideoModeInfo(UINT Idx, PVBE_MODEINFO pModeInfo);
};

typedef struct _MemSlot {
    UINT8 generation;
    UINT64 start_phys_addr;
    UINT64 end_phys_addr;
    UINT64 start_virt_addr;
    UINT64 end_virt_addr;
    QXLPHYSICAL high_bits;
} MemSlot;

typedef struct MspaceInfo {
    mspace _mspace;
    UINT8 *mspace_start;
    UINT8 *mspace_end;
} MspaceInfo;

enum {
    MSPACE_TYPE_DEVRAM,
    MSPACE_TYPE_VRAM,
    NUM_MSPACES,
};

#define RELEASE_RES(res) if (!--(res)->refs) (res)->free(res);
#define GET_RES(res) (++(res)->refs)

/* Debug helpers - tag each resource with this enum */
enum {
    RESOURCE_TYPE_DRAWABLE = 1,
    RESOURCE_TYPE_SURFACE,
    RESOURCE_TYPE_PATH,
    RESOURCE_TYPE_CLIP_RECTS,
    RESOURCE_TYPE_QUIC_IMAGE,
    RESOURCE_TYPE_BITMAP_IMAGE,
    RESOURCE_TYPE_SURFACE_IMAGE,
    RESOURCE_TYPE_SRING,
    RESOURCE_TYPE_CURSOR,
    RESOURCE_TYPE_BUF,
    RESOURCE_TYPE_UPDATE,
};

#ifdef DBG
#define RESOURCE_TYPE(res, val) do { res->type = val; } while (0)
#else
#define RESOURCE_TYPE(res, val)
#endif

typedef struct Resource Resource;
struct Resource {
    UINT32 refs;
    void* ptr;
#ifdef DBG
    UINT32 type;
#endif
    void (*free)(Resource *res);
    UINT8 res[0];
};

#define TIMEOUT_TO_MS               ((LONGLONG) 1 * 10 * 1000)

BOOLEAN
FORCEINLINE
WaitForObject(
    PVOID Object,
    PLARGE_INTEGER Timeout)
{
    NTSTATUS status;
    status = KeWaitForSingleObject (
            Object,
            Executive,
            KernelMode,
            FALSE,
            Timeout);
    ASSERT(NT_SUCCESS(status));
    return (status == STATUS_SUCCESS);
}

VOID
FORCEINLINE
ReleaseMutex(
    PKMUTEX Mutex,
    BOOLEAN locked)
{
    if (locked)
    {
        KeReleaseMutex(Mutex, FALSE);
    }
}

#define QXL_SLEEP(msec) do {                             \
    LARGE_INTEGER timeout;                               \
    timeout.QuadPart = -msec * TIMEOUT_TO_MS;            \
    KeDelayExecutionThread (KernelMode, FALSE, &timeout);\
} while (0);

#define IMAGE_HASH_INIT_VAL(width, height, format) \
    ((UINT32)((width) & 0x1FFF) | ((UINT32)((height) & 0x1FFF) << 13) |\
     ((UINT32)(format) << 26))

#define MAX_OUTPUT_RES 6

typedef struct QXLOutput {
    UINT32 num_res;
#ifdef DBG
    UINT32 type;
#endif
    Resource *resources[MAX_OUTPUT_RES];
    UINT8 data[0];
} QXLOutput;

typedef struct Ring RingItem;
typedef struct Ring {
    RingItem *prev;
    RingItem *next;
} Ring;

typedef struct InternalImage {
    QXLImage image;
} InternalImage;

typedef struct InternalCursor {
    QXLCursor cursor;
} InternalCursor;

#define CURSOR_ALLOC_SIZE (PAGE_SIZE << 1)

typedef struct DpcCbContext {
    void* ptr;
    UINT32 data;
} DPC_CB_CONTEXT,* PDPC_CB_CONTEXT;

#define BITMAP_ALLOC_BASE (sizeof(Resource) + sizeof(InternalImage) + sizeof(QXLDataChunk))
#define BITS_BUF_MAX (64 * 1024)
#define MIN(x, y) (((x) <= (y)) ? (x) : (y))
#define MAX(x, y) (((x) >= (y)) ? (x) : (y))
#define ALIGN(a, b) (((a) + ((b) - 1)) & ~((b) - 1))

class QxlDevice  :
    public HwDeviceIntrface
{
public:
    QxlDevice(_In_ QxlDod* pQxlDod);
    virtual ~QxlDevice(void);
    NTSTATUS QueryCurrentMode(PVIDEO_MODE RequestedMode);
    NTSTATUS SetCurrentMode(ULONG Mode);
    NTSTATUS GetCurrentMode(ULONG* Mode);
    ULONG GetModeCount(void) {return m_ModeCount;}
    NTSTATUS SetPowerState(DEVICE_POWER_STATE DevicePowerState, DXGK_DISPLAY_INFORMATION* pDispInfo);
    NTSTATUS HWInit(PCM_RESOURCE_LIST pResList, DXGK_DISPLAY_INFORMATION* pDispInfo);
    NTSTATUS HWClose(void);
    BOOLEAN EnablePointer(void) { return FALSE; }
    NTSTATUS ExecutePresentDisplayOnly(_In_ BYTE*             DstAddr,
                    _In_ UINT              DstBitPerPixel,
                    _In_ BYTE*             SrcAddr,
                    _In_ UINT              SrcBytesPerPixel,
                    _In_ LONG              SrcPitch,
                    _In_ ULONG             NumMoves,
                    _In_ D3DKMT_MOVE_RECT* pMoves,
                    _In_ ULONG             NumDirtyRects,
                    _In_ RECT*             pDirtyRect,
                    _In_ D3DKMDT_VIDPN_PRESENT_PATH_ROTATION Rotation,
                    _In_ const CURRENT_BDD_MODE* pModeCur);
    VOID BlackOutScreen(CURRENT_BDD_MODE* pCurrentBddMod);
    BOOLEAN InterruptRoutine(_In_ PDXGKRNL_INTERFACE pDxgkInterface, _In_  ULONG MessageNumber);
    VOID DpcRoutine(PVOID);
    VOID ResetDevice(VOID);
    NTSTATUS SetPointerShape(_In_ CONST DXGKARG_SETPOINTERSHAPE* pSetPointerShape);
    NTSTATUS SetPointerPosition(_In_ CONST DXGKARG_SETPOINTERPOSITION* pSetPointerPosition);
    NTSTATUS Escape(_In_ CONST DXGKARG_ESCAPE* pEscap);
protected:
    NTSTATUS GetModeList(DXGK_DISPLAY_INFORMATION* pDispInfo);
    VOID BltBits (BLT_INFO* pDst,
                    CONST BLT_INFO* pSrc,
                    UINT  NumRects,
                    _In_reads_(NumRects) CONST RECT *pRects);
    QXLDrawable *Drawable(UINT8 type,
                    CONST RECT *area,
                    CONST RECT *clip,
                    UINT32 surface_id);
    void PushDrawable(QXLDrawable *drawable);
    void PushCursorCmd(QXLCursorCmd *cursor_cmd);
    QXLDrawable *GetDrawable();
    QXLCursorCmd *CursorCmd();
    void *AllocMem(UINT32 mspace_type, size_t size, BOOL force);
    VOID UpdateArea(CONST RECT* area, UINT32 surface_id);
    VOID SetImageId(InternalImage *internal,
                    BOOL cache_me,
                    LONG width,
                    LONG height,
                    UINT8 format, UINT32 key);
private:
    NTSTATUS QxlInit(DXGK_DISPLAY_INFORMATION* pDispInfo);
    void QxlClose(void);
    void UnmapMemory(void);
    BOOL SetVideoModeInfo(UINT Idx, QXLMode* pModeInfo);
    void UpdateVideoModeInfo(UINT Idx, UINT xres, UINT yres, UINT bpp);
    BOOL InitMemSlots(void);
    BOOL CreateMemSlots(void);
    void DestroyMemSlots(void);
    void CreatePrimarySurface(PVIDEO_MODE_INFORMATION pModeInfo);
    void DestroyPrimarySurface(void);
    void SetupHWSlot(UINT8 Idx, MemSlot *pSlot);
    UINT8 SetupMemSlot(UINT8 Idx, UINT64 pastart, UINT64 paend, UINT64 vastart, UINT64 vaend);
    BOOL CreateEvents(void);
    BOOL CreateRings(void);
    UINT64 VA(QXLPHYSICAL paddr, UINT8 slot_id);
    QXLPHYSICAL PA(PVOID virt, UINT8 slot_id);
    void InitDeviceMemoryResources(void);
    void InitMspace(UINT32 mspace_type, UINT8 *start, size_t capacity);
    void FlushReleaseRing();
    void FreeMem(UINT32 mspace_type, void *ptr);
    UINT64 ReleaseOutput(UINT64 output_id);
    void WaitForReleaseRing(void);
    void EmptyReleaseRing(void);
    BOOL SetClip(const RECT *clip, QXLDrawable *drawable);
    void AddRes(QXLOutput *output, Resource *res);
    void DrawableAddRes(QXLDrawable *drawable, Resource *res);
    void CursorCmdAddRes(QXLCursorCmd *cmd, Resource *res);
    void FreeClipRects(Resource *res);
    void static FreeClipRectsEx(Resource *res);
    void FreeBitmapImage(Resource *res);
    void static FreeBitmapImageEx(Resource *res);
    void static FreeCursorEx(Resource *res);
    void FreeCursor(Resource *res);
    void WaitForCmdRing(void);
    void PushCmd(void);
    void WaitForCursorRing(void);
    void PushCursor(void);
    void PutBytesAlign(QXLDataChunk **chunk_ptr, UINT8 **now_ptr,
                            UINT8 **end_ptr, UINT8 *src, int size,
                            size_t alloc_size, uint32_t alignment);
    BOOLEAN static DpcCallbackEx(PVOID);
    void DpcCallback(PDPC_CB_CONTEXT);
    void AsyncIo(UCHAR  Port, UCHAR Value);
    void SyncIo(UCHAR  Port, UCHAR Value);
private:
    PUCHAR m_IoBase;
    BOOLEAN m_IoMapped;
    ULONG m_IoSize;

    PHYSICAL_ADDRESS m_RamPA;
    UINT8 *m_RamStart;
    QXLRam *m_RamHdr;
    ULONG m_RamSize;

    PHYSICAL_ADDRESS m_VRamPA;
    UINT8 *m_VRamStart;
    ULONG m_VRamSize;

    QXLRom *m_RomHdr;
    ULONG m_RomSize;

    MemSlot *m_MemSlots;
    UINT8 m_NumMemSlots;
    UINT8 m_MainMemSlot;
    UINT8 m_SurfaceMemSlot;
    UINT8 m_SlotIdBits;
    UINT8 m_SlotGenBits;
    QXLPHYSICAL m_VaSlotMask;

    QXLCommandRing *m_CommandRing;
    QXLCursorRing *m_CursorRing;
    QXLReleaseRing *m_ReleaseRing;

    KEVENT m_DisplayEvent;
    KEVENT m_CursorEvent;
    KEVENT m_IoCmdEvent;

    PUCHAR m_LogPort;
    PUCHAR m_LogBuf;

    KMUTEX m_MemLock;
    KMUTEX m_CmdLock;
    KMUTEX m_IoLock;
    KMUTEX m_CrsLock;
    MspaceInfo m_MSInfo[NUM_MSPACES];

    UINT64 m_FreeOutputs;
    UINT32 m_Pending;
};

class QxlDod {
private:
    DEVICE_OBJECT* m_pPhysicalDevice;
    DXGKRNL_INTERFACE m_DxgkInterface;
    DXGK_DEVICE_INFO m_DeviceInfo;

    DEVICE_POWER_STATE m_MonitorPowerState;
    DEVICE_POWER_STATE m_AdapterPowerState;
    QXL_FLAGS m_Flags;

    CURRENT_BDD_MODE m_CurrentModes[MAX_VIEWS];

    D3DDDI_VIDEO_PRESENT_SOURCE_ID m_SystemDisplaySourceId;
    DXGKARG_SETPOINTERSHAPE m_PointerShape;
    HwDeviceIntrface* m_pHWDevice;
public:
    QxlDod(_In_ DEVICE_OBJECT* pPhysicalDeviceObject);
    ~QxlDod(void);
#pragma code_seg(push)
#pragma code_seg()
    BOOLEAN IsDriverActive() const
    {
        return m_Flags.DriverStarted;
    }
#pragma code_seg(pop)

    NTSTATUS StartDevice(_In_  DXGK_START_INFO*   pDxgkStartInfo,
                         _In_  DXGKRNL_INTERFACE* pDxgkInterface,
                         _Out_ ULONG*             pNumberOfViews,
                         _Out_ ULONG*             pNumberOfChildren);
    NTSTATUS StopDevice(VOID);
    // Must be Non-Paged
    VOID ResetDevice(VOID);

    NTSTATUS DispatchIoRequest(_In_  ULONG VidPnSourceId,
                               _In_  VIDEO_REQUEST_PACKET* pVideoRequestPacket);
    NTSTATUS SetPowerState(_In_  ULONG HardwareUid,
                               _In_  DEVICE_POWER_STATE DevicePowerState,
                               _In_  POWER_ACTION       ActionType);
    // Report back child capabilities
    NTSTATUS QueryChildRelations(_Out_writes_bytes_(ChildRelationsSize) DXGK_CHILD_DESCRIPTOR* pChildRelations,
                                 _In_                             ULONG                  ChildRelationsSize);

    NTSTATUS QueryChildStatus(_Inout_ DXGK_CHILD_STATUS* pChildStatus,
                              _In_    BOOLEAN            NonDestructiveOnly);

    // Return EDID if previously retrieved
    NTSTATUS QueryDeviceDescriptor(_In_    ULONG                   ChildUid,
                                   _Inout_ DXGK_DEVICE_DESCRIPTOR* pDeviceDescriptor);

    // Must be Non-Paged
    // BDD doesn't have interrupts, so just returns false
    BOOLEAN InterruptRoutine(_In_  ULONG MessageNumber);

    VOID DpcRoutine(VOID);

    // Return DriverCaps, doesn't support other queries though
    NTSTATUS QueryAdapterInfo(_In_ CONST DXGKARG_QUERYADAPTERINFO* pQueryAdapterInfo);

    NTSTATUS SetPointerPosition(_In_ CONST DXGKARG_SETPOINTERPOSITION* pSetPointerPosition);

    NTSTATUS SetPointerShape(_In_ CONST DXGKARG_SETPOINTERSHAPE* pSetPointerShape);

    NTSTATUS Escape(_In_ CONST DXGKARG_ESCAPE* pEscape);

    NTSTATUS PresentDisplayOnly(_In_ CONST DXGKARG_PRESENT_DISPLAYONLY* pPresentDisplayOnly);

    NTSTATUS QueryInterface(_In_ CONST PQUERY_INTERFACE     QueryInterface);

    NTSTATUS IsSupportedVidPn(_Inout_ DXGKARG_ISSUPPORTEDVIDPN* pIsSupportedVidPn);

    NTSTATUS RecommendFunctionalVidPn(_In_ CONST DXGKARG_RECOMMENDFUNCTIONALVIDPN* CONST pRecommendFunctionalVidPn);

    NTSTATUS RecommendVidPnTopology(_In_ CONST DXGKARG_RECOMMENDVIDPNTOPOLOGY* CONST pRecommendVidPnTopology);

    NTSTATUS RecommendMonitorModes(_In_ CONST DXGKARG_RECOMMENDMONITORMODES* CONST pRecommendMonitorModes);

    NTSTATUS EnumVidPnCofuncModality(_In_ CONST DXGKARG_ENUMVIDPNCOFUNCMODALITY* CONST pEnumCofuncModality);

    NTSTATUS SetVidPnSourceVisibility(_In_ CONST DXGKARG_SETVIDPNSOURCEVISIBILITY* pSetVidPnSourceVisibility);

    NTSTATUS CommitVidPn(_In_ CONST DXGKARG_COMMITVIDPN* CONST pCommitVidPn);

    NTSTATUS UpdateActiveVidPnPresentPath(_In_ CONST DXGKARG_UPDATEACTIVEVIDPNPRESENTPATH* CONST pUpdateActiveVidPnPresentPath);

    NTSTATUS QueryVidPnHWCapability(_Inout_ DXGKARG_QUERYVIDPNHWCAPABILITY* pVidPnHWCaps);

    // Part of PnPStop (PnP instance only), returns current mode information (which will be passed to fallback instance by dxgkrnl)
    NTSTATUS StopDeviceAndReleasePostDisplayOwnership(_In_  D3DDDI_VIDEO_PRESENT_TARGET_ID TargetId,
                                                      _Out_ DXGK_DISPLAY_INFORMATION*      pDisplayInfo);

    // Must be Non-Paged
    // Call to initialize as part of bugcheck
    NTSTATUS SystemDisplayEnable(_In_  D3DDDI_VIDEO_PRESENT_TARGET_ID       TargetId,
                                 _In_  PDXGKARG_SYSTEM_DISPLAY_ENABLE_FLAGS Flags,
                                 _Out_ UINT*                                pWidth,
                                 _Out_ UINT*                                pHeight,
                                 _Out_ D3DDDIFORMAT*                        pColorFormat);

    // Must be Non-Paged
    // Write out pixels as part of bugcheck
    VOID SystemDisplayWrite(_In_reads_bytes_(SourceHeight * SourceStride) VOID* pSource,
                                 _In_                                     UINT  SourceWidth,
                                 _In_                                     UINT  SourceHeight,
                                 _In_                                     UINT  SourceStride,
                                 _In_                                     INT   PositionX,
                                 _In_                                     INT   PositionY);
    PDXGKRNL_INTERFACE GetDxgkInterrface(void) { return &m_DxgkInterface;}
private:
    VOID CleanUp(VOID);
    NTSTATUS CheckHardware();
    NTSTATUS WriteHWInfoStr(_In_ HANDLE DevInstRegKeyHandle, _In_ PCWSTR pszwValueName, _In_ PCSTR pszValue);
    // Set the given source mode on the given path
    NTSTATUS SetSourceModeAndPath(CONST D3DKMDT_VIDPN_SOURCE_MODE* pSourceMode,
                                  CONST D3DKMDT_VIDPN_PRESENT_PATH* pPath);

    // Add the current mode to the given monitor source mode set
    NTSTATUS AddSingleMonitorMode(_In_ CONST DXGKARG_RECOMMENDMONITORMODES* CONST pRecommendMonitorModes);

    // Add the current mode to the given VidPn source mode set
    NTSTATUS AddSingleSourceMode(_In_ CONST DXGK_VIDPNSOURCEMODESET_INTERFACE* pVidPnSourceModeSetInterface,
                                 D3DKMDT_HVIDPNSOURCEMODESET hVidPnSourceModeSet,
                                 D3DDDI_VIDEO_PRESENT_SOURCE_ID SourceId);

    // Add the current mode (or the matching to pinned source mode) to the give VidPn target mode set
    NTSTATUS AddSingleTargetMode(_In_ CONST DXGK_VIDPNTARGETMODESET_INTERFACE* pVidPnTargetModeSetInterface,
                                 D3DKMDT_HVIDPNTARGETMODESET hVidPnTargetModeSet,
                                 _In_opt_ CONST D3DKMDT_VIDPN_SOURCE_MODE* pVidPnPinnedSourceModeInfo,
                                 D3DDDI_VIDEO_PRESENT_SOURCE_ID SourceId);
    D3DDDI_VIDEO_PRESENT_SOURCE_ID FindSourceForTarget(D3DDDI_VIDEO_PRESENT_TARGET_ID TargetId, BOOLEAN DefaultToZero);
    NTSTATUS IsVidPnSourceModeFieldsValid(CONST D3DKMDT_VIDPN_SOURCE_MODE* pSourceMode) const;
    NTSTATUS IsVidPnPathFieldsValid(CONST D3DKMDT_VIDPN_PRESENT_PATH* pPath) const;
    NTSTATUS RegisterHWInfo(_In_ ULONG Id);
};

NTSTATUS
MapFrameBuffer(
    _In_                PHYSICAL_ADDRESS    PhysicalAddress,
    _In_                ULONG               Length,
    _Outptr_result_bytebuffer_(Length) VOID**              VirtualAddress);

NTSTATUS
UnmapFrameBuffer(
    _In_reads_bytes_(Length) VOID* VirtualAddress,
    _In_                ULONG Length);

UINT BPPFromPixelFormat(D3DDDIFORMAT Format);
D3DDDIFORMAT PixelFormatFromBPP(UINT BPP);
UINT SpiceFromPixelFormat(D3DDDIFORMAT Format);

VOID CopyBitsGeneric(
                        BLT_INFO* pDst,
                        CONST BLT_INFO* pSrc,
                        UINT  NumRects,
                        _In_reads_(NumRects) CONST RECT *pRects);

VOID CopyBits32_32(
                        BLT_INFO* pDst,
                        CONST BLT_INFO* pSrc,
                        UINT  NumRects,
                        _In_reads_(NumRects) CONST RECT *pRects);
VOID BltBits (
                        BLT_INFO* pDst,
                        CONST BLT_INFO* pSrc,
                        UINT  NumRects,
                        _In_reads_(NumRects) CONST RECT *pRects);

BYTE* GetRowStart(_In_ CONST BLT_INFO* pBltInfo, CONST RECT* pRect);
VOID GetPitches(_In_ CONST BLT_INFO* pBltInfo, _Out_ LONG* pPixelPitch, _Out_ LONG* pRowPitch);
