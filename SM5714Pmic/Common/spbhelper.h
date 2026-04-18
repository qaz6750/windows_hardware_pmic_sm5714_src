#ifndef SM5714_H
#define SM5714_H

#include <ntddk.h>
#include "driver.h"

NTSTATUS
write_reg(
	PDEVICE_CONTEXT pDevice,
	unsigned long spbIndex,
	unsigned char reg,
	unsigned short data
);

NTSTATUS
read_reg(
	PDEVICE_CONTEXT pDevice,
	unsigned long spbIndex,
	unsigned char reg,
	unsigned short* data
);

NTSTATUS
update_reg(
	PDEVICE_CONTEXT pDevice,
	unsigned long spbIndex,
	unsigned char reg,
	unsigned short mask,
	unsigned short val
);

//
// 8-bit register helpers for USBPD sub-device
//

NTSTATUS
write_reg8(
	PDEVICE_CONTEXT pDevice,
	unsigned long spbIndex,
	unsigned char reg,
	unsigned char data
);

NTSTATUS
read_reg8(
	PDEVICE_CONTEXT pDevice,
	unsigned long spbIndex,
	unsigned char reg,
	unsigned char *data
);

NTSTATUS
update_reg8(
	PDEVICE_CONTEXT pDevice,
	unsigned long spbIndex,
	unsigned char reg,
	unsigned char mask,
	unsigned char val
);

#endif // SM5714_H
