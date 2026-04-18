#ifndef _TYPEC_H_
#define _TYPEC_H_

#include "..\Common\driver.h"

// SPB index for I2C sub-devices (order matches _CRS resources)
#define SPB_CHARGER_INDEX  0
#define SPB_USBPD_INDEX    1
#define SPB_MUIC_INDEX     2

//
// SM5714 MUIC register definitions (I2C addr 0x25)
// The MUIC controls the D+/D- analog switch between USB connector and SoC.
//
#define SM5714_MUIC_REG_MANUAL_SW   0x06

// MANUAL_SW register bit fields:
//   [2:0] = DP switch (D+ routing)
//   [5:3] = DM switch (D- routing)
//   [7]   = Manual mode enable (0=auto, 1=manual)
#define MUIC_MANSW_OPEN             0x00   // D+/D- disconnected
#define MUIC_MANSW_USB              0x09   // D+/D- routed to USB (DM=1<<3 | DP=1<<0)
#define MUIC_MANSW_MANUAL_BIT       0x80   // Bit 7: manual mode enable

NTSTATUS typec_reg_init(_In_ PDEVICE_CONTEXT pDevice);
NTSTATUS typec_process_interrupt(_In_ PDEVICE_CONTEXT pDevice);
NTSTATUS typec_set_otg_mode(_In_ PDEVICE_CONTEXT pDevice, _In_ bool enable);
NTSTATUS typec_notify_usb_state(_In_ PDEVICE_CONTEXT pDevice);
NTSTATUS typec_check_initial_state(_In_ PDEVICE_CONTEXT pDevice);

#endif // _TYPEC_H_
