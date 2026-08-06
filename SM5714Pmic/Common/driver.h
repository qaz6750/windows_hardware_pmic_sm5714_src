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

typedef struct _DEVICE_CONTEXT
{

	WDFDEVICE FxDevice;
	WDFINTERRUPT ChgInterrupt;
	WDFINTERRUPT PdInterrupt;
	WDFQUEUE ReportQueue;
	SPB_CONTEXT     SpbContexts[3];
	ULONG           SpbContextCount;
	BOOLEAN DevicePoweredOn;
	BOOLEAN TypecInitialized;
	WDFWAITLOCK DataLock;

	BOOLEAN                         Autostop;            // 0 = off, 1 = on
	ULONG                           InputCurrentLimit;   // mA
	ULONG                           ChargingCurrent;     // mA
	ULONG                           TopoffCurrent;       // mA
	ULONG                           FloatVoltage;        // mV (float charge voltage)
	ULONG                           OtgCurrent;          // mA (900, 1200, 1500)
	ULONG                           TrickleCurrent;      // mA (trickle charge current)
	UCHAR                           WdtTimer;            // WDT timeout index
	UCHAR                           LxSlope;             // LX slope 0-3
	BOOLEAN                         AiclEnabled;         // Auto input current limit
	UCHAR                           DischgLimit;         // Discharge OCP limit index
	UCHAR                           TopoffTimer;         // Topoff timer index

	// Runtime charging state
	BOOLEAN                         IsChargingEnabled;
	UCHAR                           ChgStatus;           // CHG_STATUS1 snapshot
	ULONG                           RpCurrentAdvertised; // mA from CC advertisement

	// Type-C state
	BOOLEAN                         IsAttached;
	UCHAR                           AttachType;          // SM5714_ATTACH_xxx
	UCHAR                           CcOrientation;       // 0=CC1, 1=CC2, 2=Open
	BOOLEAN                         VbusPresent;
	UCHAR                           Bc12Type;            // BC1.2 detected type
	BOOLEAN                         IsAudioAccessory;
	BOOLEAN                         IsDebugAccessory;

	// DisplayPort Alt Mode state
	BOOLEAN                         DpAltModeActive;
	BOOLEAN                         DpConfigured;
	BOOLEAN                         DpHpdState;
	BOOLEAN                         DpIrqHpd;
	UCHAR                           DpState;
	UCHAR                           DpSelectedPin;
	UCHAR                           DpPartnerPinAssignments;
	UCHAR                           DpMuxMode;
	UCHAR                           PdMsgId;
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
		(DebugCategories & dbgcatagory))                    \
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
