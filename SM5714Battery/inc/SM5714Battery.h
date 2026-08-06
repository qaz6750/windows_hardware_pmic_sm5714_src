/*++

Copyright (c) Microsoft Corporation. All rights reserved.

Module Name:

    SM5714Battery.h

Abstract:

    This is the header file for the SM5714 PMIC fuel gauge driver.

    N.B. This code is provided "AS IS" without any expressed or implied warranty.

--*/

//---------------------------------------------------------------------- Pragmas

#pragma once

//--------------------------------------------------------------------- Includes

#include <wdm.h>
#include <wdf.h>
#include <batclass.h>
#include <wmistr.h>
#include <wmilib.h>
#include <ntstrsafe.h>
#include "trace.h"
#define RESHUB_USE_HELPER_ROUTINES
#include <reshub.h>
#include "spb.h"

//--------------------------------------------------------------------- Literals

#define SM5714_BATTERY_TAG                 'StaB'

/*
* Rob Green, a member of the NTDEV list, provides the
* following set of macros that'll keep you from having
* to scratch your head and count zeros ever again.
* Using these defintions, all you'll have to do is write:
*
* interval.QuadPart = RELATIVE(SECONDS(5));
*/

#ifndef ABSOLUTE
#define ABSOLUTE(wait) (wait)
#endif

#ifndef RELATIVE
#define RELATIVE(wait) (-(wait))
#endif

#ifndef NANOSECONDS
#define NANOSECONDS(nanos) \
	(((signed __int64)(nanos)) / 100L)
#endif

#ifndef MICROSECONDS
#define MICROSECONDS(micros) \
	(((signed __int64)(micros)) * NANOSECONDS(1000L))
#endif

#ifndef MILLISECONDS
#define MILLISECONDS(milli) \
	(((signed __int64)(milli)) * MICROSECONDS(1000L))
#endif

#ifndef SECONDS
#define SECONDS(seconds) \
	(((signed __int64)(seconds)) * MILLISECONDS(1000L))
#endif

//------------------------------------------------------------------ Definitions

#define MFG_NAME_SIZE  0x3
#define DEVICE_NAME_SIZE 0x8
#define CHEM_SIZE 0x4

typedef struct {
    UNICODE_STRING                  RegistryPath;
} SM5714_BATTERY_GLOBAL_DATA, *PSM5714_BATTERY_GLOBAL_DATA;

typedef struct {
    //
    // Device handle
    //
    WDFDEVICE Device;

    //
    // Battery class registration
    //

    PVOID                           ClassHandle;
    WDFWAITLOCK                     ClassInitLock;
    WMILIB_CONTEXT                  WmiLibContext;

    //
    // Spb (I2C) related members used for the lifetime of the device
    //
    SPB_CONTEXT I2CContext;

    //
    // Battery state
    //

    WDFWAITLOCK                     StateLock;
    ULONG                           BatteryTag;

    ULONG                           DesignedCapacity_mWh;
    ULONG                           FullChargedCapacity_mWh;
    UCHAR                           BatteryTechnology;
    ULONG                           DesignVoltage_mV;
    ULONG                           BatterySoh;         /* State-of-Health, 0-100 % */
} SM5714_BATTERY_FDO_DATA, *PSM5714_BATTERY_FDO_DATA;

//------------------------------------------------------ WDF Context Declaration

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(SM5714_BATTERY_GLOBAL_DATA, GetGlobalData);
WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(SM5714_BATTERY_FDO_DATA, GetDeviceExtension);

//----------------------------------------------------- Prototypes (miniclass.c)

_IRQL_requires_same_
VOID
SM5714BatteryPrepareHardware(
    _In_ WDFDEVICE Device
);

BCLASS_QUERY_TAG_CALLBACK SM5714BatteryQueryTag;
BCLASS_QUERY_INFORMATION_CALLBACK SM5714BatteryQueryInformation;
BCLASS_SET_INFORMATION_CALLBACK SM5714BatterySetInformation;
BCLASS_QUERY_STATUS_CALLBACK SM5714BatteryQueryStatus;
BCLASS_SET_STATUS_NOTIFY_CALLBACK SM5714BatterySetStatusNotify;
BCLASS_DISABLE_STATUS_NOTIFY_CALLBACK SM5714BatteryDisableStatusNotify;