#ifndef _CHARGER_H_
#define _CHARGER_H_

#include "..\Common\driver.h"

//
// Basic charging parameters
//
int ChargerSetAutostop(_In_ PDEVICE_CONTEXT pDevice, BOOLEAN enable);
int ChargerSetInputCurrent(_In_ PDEVICE_CONTEXT pDevice, unsigned int mA);
int ChargerSetChargeCurrent(_In_ PDEVICE_CONTEXT pDevice, unsigned int mA);
int ChargerSetTopoffCurrent(_In_ PDEVICE_CONTEXT pDevice, unsigned int mA);
int ChargerProbe(_In_ PDEVICE_CONTEXT pDevice);
int ChargerEnable(_In_ PDEVICE_CONTEXT pDevice, BOOLEAN enable);

//
// Advanced charging parameters (ported from Android kernel driver)
//

// Float voltage (BATREG): sets target battery regulation voltage (3700mV~4620mV)
int ChargerSetFloatVoltage(_In_ PDEVICE_CONTEXT pDevice, unsigned int mV);
int ChargerGetFloatVoltage(_In_ PDEVICE_CONTEXT pDevice);

// Watchdog timer: prevents charger lockup by resetting periodically
int ChargerSetWatchdogEnable(_In_ PDEVICE_CONTEXT pDevice, BOOLEAN enable);
int ChargerSetWatchdogTimer(_In_ PDEVICE_CONTEXT pDevice, UCHAR timer_idx);
int ChargerKickWatchdog(_In_ PDEVICE_CONTEXT pDevice);
int ChargerClearWatchdog(_In_ PDEVICE_CONTEXT pDevice);

// AICL (Automatic Input Current Limit): auto-adapts to adapter capability
int ChargerSetAiclEnable(_In_ PDEVICE_CONTEXT pDevice, BOOLEAN enable);
int ChargerSetAiclThreshold(_In_ PDEVICE_CONTEXT pDevice, UCHAR threshold_idx);

// ENQ4FET: charging soft-start/stop with current ramp
int ChargerSetSoftStart(_In_ PDEVICE_CONTEXT pDevice, BOOLEAN enable);

// Discharge current limit (battery-side OCP)
int ChargerSetDischargeLimit(_In_ PDEVICE_CONTEXT pDevice, UCHAR limit_idx);

// Topoff timer limit
int ChargerSetTopoffTimer(_In_ PDEVICE_CONTEXT pDevice, UCHAR tmr_idx);

// LX slope: switching regulator slew rate control
int ChargerSetLxSlope(_In_ PDEVICE_CONTEXT pDevice, UCHAR slope);

// Trickle charging current
int ChargerSetTrickleCurrent(_In_ PDEVICE_CONTEXT pDevice, unsigned int mA);

//
// Runtime charging status and control
//
NTSTATUS ChargerReadStatus(_In_ PDEVICE_CONTEXT pDevice);
int ChargerAdaptRpCurrent(_In_ PDEVICE_CONTEXT pDevice);
int ChargerApplyInputCurrentPolicy(_In_ PDEVICE_CONTEXT pDevice);
int ChargerGetState(_In_ PDEVICE_CONTEXT pDevice);
int ChargerSetBypass(_In_ PDEVICE_CONTEXT pDevice, BOOLEAN enable);
int ChargerSetShipMode(_In_ PDEVICE_CONTEXT pDevice, BOOLEAN forced, UCHAR auto_vref, UCHAR auto_time);

//
// Charger interrupt processing
//
NTSTATUS ChargerProcessInterrupts(_In_ PDEVICE_CONTEXT pDevice);

//
// BC1.2 charger type detection
//
NTSTATUS ChargerDetectBc12(_In_ PDEVICE_CONTEXT pDevice, _Out_ UCHAR *type);

//
// VBUS voltage reading (via MUIC)
//
NTSTATUS ChargerReadVbusVoltage(_In_ PDEVICE_CONTEXT pDevice, _Out_ ULONG *mV);

//
// Quick Charge 2.0 / AFC voltage control
//
int ChargerSetQc20Voltage(_In_ PDEVICE_CONTEXT pDevice, UCHAR voltage);
int ChargerDisableQc20(_In_ PDEVICE_CONTEXT pDevice);

#endif // _CHARGER_H_
