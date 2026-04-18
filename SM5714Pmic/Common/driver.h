#if !defined(_SM5714_H_)
#define _SM5714_H_

#pragma warning(disable:4200)  // suppress nameless struct/union warning
#pragma warning(disable:4201)  // suppress nameless struct/union warning
#pragma warning(disable:4214)  // suppress bit field types other than int warning
#include <initguid.h>
#include <wdm.h>

#pragma warning(default:4200)
#pragma warning(default:4201)
#pragma warning(default:4214)
#include <wdf.h>

#include <acpiioct.h>
#include <ntstrsafe.h>

#include "spb.h"

//
// String definitions
//

#define DRIVERNAME                 "SM5714Pmic.sys: "

#define SPB_POOL_TAG            (ULONG) '7495'

#define bool int

#define true 1
#define false 0

typedef struct _DEVICE_CONTEXT
{

	WDFDEVICE FxDevice;
	WDFINTERRUPT ChgInterrupt;
	WDFINTERRUPT PdInterrupt;
	WDFQUEUE ReportQueue;
	SPB_CONTEXT     SpbContexts[3];
	ULONG           SpbContextCount;
	BOOLEAN DevicePoweredOn;
	WDFWAITLOCK DataLock;

	BOOLEAN                         Autostop;            // 0 = off, 1 = on
	ULONG                           InputCurrentLimit;   // mA
	ULONG                           ChargingCurrent;     // mA
	ULONG                           TopoffCurrent;       // mA

	// Type-C state
	UCHAR                           AttachType;          // SM5714_ATTACH_xxx
	UCHAR                           CcOrientation;       // 0=CC1, 1=CC2, 2=Open
	BOOLEAN                         VbusPresent;
	BOOLEAN                         IsAttached;

} DEVICE_CONTEXT, *PDEVICE_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(DEVICE_CONTEXT, GetDeviceContext)

//
// Function definitions
//

DRIVER_INITIALIZE DriverEntry;

EVT_WDF_DRIVER_UNLOAD DriverUnload;

EVT_WDF_DRIVER_DEVICE_ADD EvtDeviceAdd;

EVT_WDFDEVICE_WDM_IRP_PREPROCESS EvtWdmPreprocessMnQueryId;

EVT_WDF_IO_QUEUE_IO_INTERNAL_DEVICE_CONTROL EvtInternalDeviceControl;

EVT_WDF_INTERRUPT_ISR EvtChgInterruptIsr;
EVT_WDF_INTERRUPT_ISR EvtPdInterruptIsr;

//
// Helper macros
//

#define DEBUG_LEVEL_ERROR   1
#define DEBUG_LEVEL_INFO    2
#define DEBUG_LEVEL_VERBOSE 3

#define DBG_INIT  1
#define DBG_PNP   2
#define DBG_IOCTL 4

#if 1
#define Print(dbglevel, dbgcatagory, fmt, ...) {          \
    if (DebugLevel >= dbglevel &&                         \
        (DebugCatagories && dbgcatagory))                 \
	    {                                                           \
        DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, DRIVERNAME);                                   \
		DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, fmt, __VA_ARGS__);                             \
	    }                                                           \
}
#else
#define Print(dbglevel, fmt, ...) {                       \
}
#endif

#endif
