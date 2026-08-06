/*++

Copyright (c) Microsoft Corporation. All rights reserved.

Module Name:

	miniclass.c

Abstract:

	This module implements battery miniclass functionality specific to the
	SM5714 PMIC battery driver.

	N.B. This code is provided "AS IS" without any expressed or implied warranty.

--*/

//--------------------------------------------------------------------- Includes

#include "..\inc\SM5714Battery.h"
#include "usbfnbase.h"
#include "miniclass.tmh"
#include "..\inc\sm5714_fuelgauge.h"
#include <acpiioct.h>

//------------------------------------------------------------------- Prototypes

_IRQL_requires_same_
VOID
SM5714BatteryUpdateTag(
	_Inout_ PSM5714_BATTERY_FDO_DATA DevExt
);

BCLASS_QUERY_TAG_CALLBACK SM5714BatteryQueryTag;
BCLASS_QUERY_INFORMATION_CALLBACK SM5714BatteryQueryInformation;
BCLASS_SET_INFORMATION_CALLBACK SM5714BatterySetInformation;
BCLASS_QUERY_STATUS_CALLBACK SM5714BatteryQueryStatus;
BCLASS_SET_STATUS_NOTIFY_CALLBACK SM5714BatterySetStatusNotify;
BCLASS_DISABLE_STATUS_NOTIFY_CALLBACK SM5714BatteryDisableStatusNotify;

//---------------------------------------------------------------------- Pragmas

#pragma alloc_text(PAGE, SM5714BatteryPrepareHardware)
#pragma alloc_text(PAGE, SM5714BatteryUpdateTag)
#pragma alloc_text(PAGE, SM5714BatteryQueryTag)
#pragma alloc_text(PAGE, SM5714BatteryQueryInformation)
#pragma alloc_text(PAGE, SM5714BatteryQueryStatus)
#pragma alloc_text(PAGE, SM5714BatterySetStatusNotify)
#pragma alloc_text(PAGE, SM5714BatteryDisableStatusNotify)
#pragma alloc_text(PAGE, SM5714BatterySetInformation)
//------------------------------------------------------------ Battery Interface

_Use_decl_annotations_
VOID
SM5714BatteryPrepareHardware(
	WDFDEVICE Device
)

/*++

Routine Description:

	This routine is called to initialize battery data to sane values.

Arguments:

	Device - Supplies the device to initialize.

Return Value:

	NTSTATUS

--*/

{

	PSM5714_BATTERY_FDO_DATA DevExt;
	NTSTATUS Status = STATUS_SUCCESS;

	PAGED_CODE();
	Trace(TRACE_LEVEL_INFORMATION, SM5714_BATTERY_TRACE, "Entering %!FUNC!\n");

	DevExt = GetDeviceExtension(Device);


	WdfWaitLockAcquire(DevExt->StateLock, NULL);
	SM5714BatteryUpdateTag(DevExt);
	WdfWaitLockRelease(DevExt->StateLock);

	Trace(TRACE_LEVEL_INFORMATION, SM5714_BATTERY_TRACE, "Leaving %!FUNC!: Status = 0x%08lX\n", Status);
	return;
}

_Use_decl_annotations_
VOID
SM5714BatteryUpdateTag(PSM5714_BATTERY_FDO_DATA DevExt)

/*++

Routine Description:

	This routine is called when static battery properties have changed to
	update the battery tag.

Arguments:

	DevExt - Supplies a pointer to the device extension  of the battery to
		update.

Return Value:

	None

--*/

{

	PAGED_CODE();

	DevExt->BatteryTag += 1;
	if (DevExt->BatteryTag == BATTERY_TAG_INVALID) {
		DevExt->BatteryTag += 1;
	}

	return;
}

_Use_decl_annotations_
NTSTATUS
SM5714BatteryQueryTag(
	PVOID Context,
	PULONG BatteryTag
)

/*++

Routine Description:

	This routine is called to get the value of the current battery tag.

Arguments:

	Context - Supplies the miniport context value for battery

	BatteryTag - Supplies a pointer to a ULONG to receive the battery tag.

Return Value:

	NTSTATUS

--*/

{
	PSM5714_BATTERY_FDO_DATA DevExt;
	NTSTATUS Status;

	PAGED_CODE();
	Trace(TRACE_LEVEL_INFORMATION, SM5714_BATTERY_TRACE, "Entering %!FUNC!\n");

	DevExt = (PSM5714_BATTERY_FDO_DATA)Context;
	WdfWaitLockAcquire(DevExt->StateLock, NULL);
	*BatteryTag = DevExt->BatteryTag;
	WdfWaitLockRelease(DevExt->StateLock);
	if (*BatteryTag == BATTERY_TAG_INVALID) {
		Status = STATUS_NO_SUCH_DEVICE;
	}
	else {
		Status = STATUS_SUCCESS;
	}

	Trace(TRACE_LEVEL_INFORMATION, SM5714_BATTERY_TRACE, "Leaving %!FUNC!: Status = 0x%08lX\n", Status);
	return Status;
}

NTSTATUS
SM5714BatteryQueryBatteryInformation(PSM5714_BATTERY_FDO_DATA DevExt, PBATTERY_INFORMATION BatteryInformationResult)
{
	ULONG    CycleCount = 0;
	ULONG    Soh;
	NTSTATUS Status;

	Trace(TRACE_LEVEL_INFORMATION, SM5714_BATTERY_TRACE, "Entering %!FUNC!\n");

	// Set up battery info (chemistry, capacity in mWh, etc.)
	BatteryInformationResult->Capabilities = BATTERY_SYSTEM_BATTERY;
	BatteryInformationResult->Technology = DevExt->BatteryTechnology;

	BYTE LION[4] = {'L','I','O','N'};
	RtlCopyMemory(BatteryInformationResult->Chemistry, LION, 4);

	BatteryInformationResult->DesignedCapacity = DevExt->DesignedCapacity_mWh;

	/*
	 * Update FullChargedCapacity using the FG's State-of-Health.
	 * SOH represents battery aging: 100 % = brand-new, lower = degraded.
	 * This gives Windows an accurate picture of current capacity so that
	 * the charge percentage and time-to-empty estimates stay correct as the
	 * battery ages.
	 */
	Status = sm5714_Get_BatteryHealth(DevExt, &Soh);
	if (NT_SUCCESS(Status) && Soh > 0 && Soh <= 100) {
		DevExt->BatterySoh = Soh;
		DevExt->FullChargedCapacity_mWh =
		    DevExt->DesignedCapacity_mWh * Soh / 100;
	}

	BatteryInformationResult->FullChargedCapacity = DevExt->FullChargedCapacity_mWh;

	BatteryInformationResult->DefaultAlert1 = BatteryInformationResult->FullChargedCapacity * 7 / 100; // 7% of total capacity for error
	BatteryInformationResult->DefaultAlert2 = BatteryInformationResult->FullChargedCapacity * 9 / 100; // 9% of total capacity for warning
	BatteryInformationResult->CriticalBias = 0;

	sm5714_Get_CycleCount(DevExt, &CycleCount);

	BatteryInformationResult->CycleCount = CycleCount;

	Trace(
		TRACE_LEVEL_INFORMATION,
		SM5714_BATTERY_TRACE,
		"BATTERY_INFORMATION: \n"
		"Capabilities: %d \n"
		"Technology: %d \n"
		"DesignedCapacity: %d \n"
		"FullChargedCapacity: %d \n"
		"DefaultAlert1: %d \n"
		"DefaultAlert2: %d \n"
		"CriticalBias: %d \n"
		"CycleCount: %d\n",
		BatteryInformationResult->Capabilities,
		BatteryInformationResult->Technology,
		BatteryInformationResult->DesignedCapacity,
		BatteryInformationResult->FullChargedCapacity,
		BatteryInformationResult->DefaultAlert1,
		BatteryInformationResult->DefaultAlert2,
		BatteryInformationResult->CriticalBias,
		BatteryInformationResult->CycleCount);

	Status = STATUS_SUCCESS;
	Trace(TRACE_LEVEL_INFORMATION, SM5714_BATTERY_TRACE, "Leaving %!FUNC!: Status = 0x%08lX\n", Status);
	return Status;
}

NTSTATUS
SM5714BatteryQueryBatteryEstimatedTime(
	PSM5714_BATTERY_FDO_DATA DevExt,
	LONG AtRate,
	PULONG ResultValue
)
{
	NTSTATUS Status = STATUS_SUCCESS;
	ULONG Capacity = 0;
	ULONG Voltage = 0;
	LONG Current = 0;
	LONG Rate_mW = 0;
	ULONG AbsRate_mW = 0;
	ULONG RemainingCapacity_mWh = 0;
	ULONGLONG RemainingCapacitySeconds = 0;

	Trace(TRACE_LEVEL_INFORMATION, SM5714_BATTERY_TRACE, "Entering %!FUNC!\n");

	/* Get current SOC (0-1000, in 0.1% units) */
	Status = sm5714_Get_BatterySoC(DevExt, &Capacity);
	if (!NT_SUCCESS(Status))
	{
		*ResultValue = BATTERY_UNKNOWN_TIME;
		goto Exit;
	}

	if (AtRate > 0)
	{
		/* AtRate specifies drain rate in mW (positive = drain) */
		AbsRate_mW = (ULONG)AtRate;
	}
	else
	{
		/* Use present rate of drain */
		Status = sm5714_Get_BatteryVoltage(DevExt, &Voltage);
		if (!NT_SUCCESS(Status))
		{
			*ResultValue = BATTERY_UNKNOWN_TIME;
			goto Exit;
		}

		Status = sm5714_Get_BatteryCurrent(DevExt, &Current);
		if (!NT_SUCCESS(Status))
		{
			*ResultValue = BATTERY_UNKNOWN_TIME;
			goto Exit;
		}

		/* Rate_mW: positive = charging, negative = discharging */
		Rate_mW = ((LONG)Current * (LONG)Voltage) / 1000;

		if (Rate_mW >= 0)
		{
			/* Not discharging - cannot estimate time to empty */
			*ResultValue = BATTERY_UNKNOWN_TIME;

			Trace(TRACE_LEVEL_INFORMATION, SM5714_BATTERY_TRACE,
				"BatteryEstimatedTime: BATTERY_UNKNOWN_TIME (charging/idle, Rate=%d mW)\n",
				Rate_mW);
			goto Exit;
		}

		AbsRate_mW = (ULONG)(-Rate_mW);
	}

	if (AbsRate_mW == 0)
	{
		*ResultValue = BATTERY_UNKNOWN_TIME;
		goto Exit;
	}

	/* remaining_mWh = SOC(0.1%) * FullChargedCapacity_mWh / 1000 */
	RemainingCapacity_mWh = (ULONG)(((ULONGLONG)Capacity *
		DevExt->FullChargedCapacity_mWh) / 1000);

	/* time_seconds = remaining_mWh * 3600 / rate_mW */
	RemainingCapacitySeconds = (ULONGLONG)RemainingCapacity_mWh * 3600;
	*ResultValue = (ULONG)(RemainingCapacitySeconds / AbsRate_mW);

	Trace(TRACE_LEVEL_INFORMATION, SM5714_BATTERY_TRACE,
		"BatteryEstimatedTime: Remaining=%d mWh, DrainRate=%d mW, Time=%d seconds\n",
		RemainingCapacity_mWh, AbsRate_mW, *ResultValue);

Exit:
	Trace(TRACE_LEVEL_INFORMATION, SM5714_BATTERY_TRACE,
		"Leaving %!FUNC!: Status = 0x%08lX\n",
		Status);
	return Status;
}

_Use_decl_annotations_
NTSTATUS
SM5714BatteryQueryInformation(
	PVOID Context,
	ULONG BatteryTag,
	BATTERY_QUERY_INFORMATION_LEVEL Level,
	LONG AtRate,
	PVOID Buffer,
	ULONG BufferLength,
	PULONG ReturnedLength
)

/*++

Routine Description:

	Called by the class driver to retrieve battery information

	The battery class driver will serialize all requests it issues to
	the miniport for a given battery.

	Return invalid parameter when a request for a specific level of information
	can't be handled. This is defined in the battery class spec.

Arguments:

	Context - Supplies the miniport context value for battery

	BatteryTag - Supplies the tag of current battery

	Level - Supplies the type of information required

	AtRate - Supplies the rate of drain for the BatteryEstimatedTime level

	Buffer - Supplies a pointer to a buffer to place the information

	BufferLength - Supplies the length in bytes of the buffer

	ReturnedLength - Supplies the length in bytes of the returned data

Return Value:

	Success if there is a battery currently installed, else no such device.

--*/

{
	PSM5714_BATTERY_FDO_DATA DevExt;
	ULONG ResultValue;
	PVOID ReturnBuffer;
	size_t ReturnBufferLength;
	NTSTATUS Status;

	BATTERY_REPORTING_SCALE ReportingScale = { 0 };
	BATTERY_INFORMATION BatteryInformationResult = { 0 };
	WCHAR StringResult[MAX_BATTERY_STRING_SIZE] = { 0 };
	BATTERY_MANUFACTURE_DATE ManufactureDate = { 0 };

	unsigned short rawTemp = 0;
	ULONG Temperature = 0;
	USHORT DateData = 0;

	Trace(TRACE_LEVEL_INFORMATION, SM5714_BATTERY_TRACE, "Entering %!FUNC!\n");
	PAGED_CODE();

	DevExt = (PSM5714_BATTERY_FDO_DATA)Context;
	WdfWaitLockAcquire(DevExt->StateLock, NULL);
	if (BatteryTag != DevExt->BatteryTag) {
		Status = STATUS_NO_SUCH_DEVICE;
		goto QueryInformationEnd;
	}

	//
	// Determine the value of the information being queried for and return it.
	//

	ReturnBuffer = NULL;
	ReturnBufferLength = 0;
	Trace(TRACE_LEVEL_INFORMATION, SM5714_BATTERY_INFO, "Query for information level 0x%x\n", Level);
	Status = STATUS_INVALID_DEVICE_REQUEST;
	switch (Level) {
	case BatteryInformation:
		Status = SM5714BatteryQueryBatteryInformation(DevExt, &BatteryInformationResult);
		if (!NT_SUCCESS(Status))
		{
			Trace(TRACE_LEVEL_ERROR, SM5714_BATTERY_TRACE, "SM5714BatteryQueryBatteryInformation failed with Status = 0x%08lX\n", Status);
			goto Exit;
		}

		ReturnBuffer = &BatteryInformationResult;
		ReturnBufferLength = sizeof(BATTERY_INFORMATION);
		Status = STATUS_SUCCESS;
		break;

	case BatteryEstimatedTime:
		Status = SM5714BatteryQueryBatteryEstimatedTime(DevExt, AtRate, &ResultValue);
		if (!NT_SUCCESS(Status))
		{
			Trace(TRACE_LEVEL_ERROR, SM5714_BATTERY_TRACE, "SM5714BatteryQueryBatteryEstimatedTime failed with Status = 0x%08lX\n", Status);
			goto Exit;
		}

		ReturnBuffer = &ResultValue;
		ReturnBufferLength = sizeof(ResultValue);
		Status = STATUS_SUCCESS;
		break;

	case BatteryUniqueID:

		swprintf_s(StringResult, sizeof(StringResult) / sizeof(WCHAR), L"%c%c%c%c%c%c%c%c",
			'S',
			'M',
			'5',
			'7',
			'1',
			'4',
			'F',
			'G');

		Trace(TRACE_LEVEL_INFORMATION, SM5714_BATTERY_TRACE, "BatteryUniqueID: %S\n", StringResult);

		Status = RtlStringCbLengthW(StringResult, sizeof(StringResult), &ReturnBufferLength);
		if (!NT_SUCCESS(Status))
		{
			Trace(TRACE_LEVEL_ERROR, SM5714_BATTERY_TRACE, "RtlStringCbLengthW failed with Status = 0x%08lX\n", Status);
			goto Exit;
		}

		ReturnBuffer = StringResult;
		ReturnBufferLength += sizeof(WCHAR);
		Status = STATUS_SUCCESS;
		break;


	case BatteryManufactureName:

		swprintf_s(StringResult, sizeof(StringResult) / sizeof(WCHAR), L"%c%c",
			0x53,  // S
			0x53   // S
		);

		Trace(TRACE_LEVEL_INFORMATION, SM5714_BATTERY_TRACE, "BatteryManufactureName: %S\n", StringResult);

		Status = RtlStringCbLengthW(StringResult, sizeof(StringResult), &ReturnBufferLength);
		if (!NT_SUCCESS(Status))
		{
			Trace(TRACE_LEVEL_ERROR, SM5714_BATTERY_TRACE, "RtlStringCbLengthW failed with Status = 0x%08lX\n", Status);
			goto Exit;
		}

		ReturnBuffer = StringResult;
		ReturnBufferLength += sizeof(WCHAR);
		Status = STATUS_SUCCESS;
		break;

	case BatteryDeviceName:

		swprintf_s(StringResult, sizeof(StringResult) / sizeof(WCHAR), L"%c%c%c%c%c%c",
			0x53,  // S
			0x4D,  // M
			0x35,  // 5
			0x37,  // 7
			0x31,  // 1
			0x34   // 4
		);

		Trace(TRACE_LEVEL_INFORMATION, SM5714_BATTERY_TRACE, "BatteryDeviceName: %S\n", StringResult);

		Status = RtlStringCbLengthW(StringResult, sizeof(StringResult), &ReturnBufferLength);
		if (!NT_SUCCESS(Status))
		{
			Trace(TRACE_LEVEL_ERROR, SM5714_BATTERY_TRACE, "RtlStringCbLengthW failed with Status = 0x%08lX\n", Status);
			goto Exit;
		}

		ReturnBuffer = StringResult;
		ReturnBufferLength += sizeof(WCHAR);
		Status = STATUS_SUCCESS;
		break;

	case BatterySerialNumber:

		swprintf_s(StringResult, sizeof(StringResult) / sizeof(WCHAR), L"%u", (UINT32)5714);

		Trace(TRACE_LEVEL_INFORMATION, SM5714_BATTERY_TRACE, "BatterySerialNumber: %S\n", StringResult);

		Status = RtlStringCbLengthW(StringResult, sizeof(StringResult), &ReturnBufferLength);
		if (!NT_SUCCESS(Status))
		{
			Trace(TRACE_LEVEL_ERROR, SM5714_BATTERY_TRACE, "RtlStringCbLengthW failed with Status = 0x%08lX\n", Status);
			goto Exit;
		}

		ReturnBuffer = StringResult;
		ReturnBufferLength += sizeof(WCHAR);
		Status = STATUS_SUCCESS;
		break;

	case BatteryManufactureDate:

		ManufactureDate.Day = 1;
		ManufactureDate.Month = 9;
		ManufactureDate.Year = 2021;

		ReturnBuffer = &ManufactureDate;
		ReturnBufferLength = sizeof(BATTERY_MANUFACTURE_DATE);
		Status = STATUS_SUCCESS;
		break;

	case BatteryGranularityInformation:

		ReportingScale.Capacity = DevExt->DesignedCapacity_mWh;
		ReportingScale.Granularity = 1;

		Trace(TRACE_LEVEL_INFORMATION, SM5714_BATTERY_TRACE, "BATTERY_REPORTING_SCALE: Capacity: %d, Granularity: %d\n", ReportingScale.Capacity, ReportingScale.Granularity);

		ReturnBuffer = &ReportingScale;
		ReturnBufferLength = sizeof(BATTERY_REPORTING_SCALE);
		Status = STATUS_SUCCESS;
		break;

	case BatteryTemperature:

		Status = sm5714_Get_BatteryTemperature(DevExt, &Temperature);
		if (!NT_SUCCESS(Status))
		{
			Trace(TRACE_LEVEL_ERROR, SM5714_BATTERY_TRACE, "sm5714_Get_BatteryTemperature failed with Status = 0x%08lX\n", Status);
			goto Exit;
		}

		Trace(TRACE_LEVEL_INFORMATION, SM5714_BATTERY_TRACE, "Battery temperature: %d (0.1 K)\n", Temperature);

		ReturnBuffer = &Temperature;
		ReturnBufferLength = sizeof(ULONG);
		Status = STATUS_SUCCESS;
		break;
	}

Exit:
	NT_ASSERT(((ReturnBufferLength == 0) && (ReturnBuffer == NULL)) ||
		((ReturnBufferLength > 0) && (ReturnBuffer != NULL)));

	if (NT_SUCCESS(Status)) {
		*ReturnedLength = (ULONG)ReturnBufferLength;
		if (ReturnBuffer != NULL) {
			if ((Buffer == NULL) || (BufferLength < ReturnBufferLength)) {
				Status = STATUS_BUFFER_TOO_SMALL;

			}
			else {
				memcpy(Buffer, ReturnBuffer, ReturnBufferLength);
			}
		}

	}
	else {
		*ReturnedLength = 0;
	}

QueryInformationEnd:
	WdfWaitLockRelease(DevExt->StateLock);
	Trace(TRACE_LEVEL_INFORMATION, SM5714_BATTERY_TRACE, "Leaving %!FUNC!: Status = 0x%08lX\n", Status);
	return Status;
}

//
// Query the ACPI GBST method to get charging state from TypeC detection.
// Returns TRUE if charging (charger cable attached), FALSE if discharging.
//
static BOOLEAN Sm5714QueryChargingState(_In_ PSM5714_BATTERY_FDO_DATA DevExt)
{
	ACPI_EVAL_INPUT_BUFFER input = { 0 };
	input.Signature = ACPI_EVAL_INPUT_BUFFER_SIGNATURE;
	RtlCopyMemory(input.MethodName, "GBST", 4);

	ULONG const outLen = sizeof(ACPI_EVAL_OUTPUT_BUFFER) +
		sizeof(ACPI_METHOD_ARGUMENT);
	PACPI_EVAL_OUTPUT_BUFFER output =
		(PACPI_EVAL_OUTPUT_BUFFER)ExAllocatePoolZero(
			NonPagedPoolNx, outLen, 'tsbG');
	if (!output)
		return FALSE;

	WDF_MEMORY_DESCRIPTOR inDesc, outDesc;
	WDF_MEMORY_DESCRIPTOR_INIT_BUFFER(&inDesc, &input, sizeof(input));
	WDF_MEMORY_DESCRIPTOR_INIT_BUFFER(&outDesc, output, outLen);

	NTSTATUS status = WdfIoTargetSendIoctlSynchronously(
		WdfDeviceGetIoTarget(DevExt->Device),
		NULL,
		IOCTL_ACPI_EVAL_METHOD,
		&inDesc,
		&outDesc,
		NULL,
		NULL);

	BOOLEAN charging = FALSE;
	if (NT_SUCCESS(status) &&
		output->Signature == ACPI_EVAL_OUTPUT_BUFFER_SIGNATURE &&
		output->Count >= 1 &&
		output->Argument[0].Type == ACPI_METHOD_ARGUMENT_INTEGER)
	{
		charging = (output->Argument[0].Argument != 0);
	}

	ExFreePool(output);
	return charging;
}

_Use_decl_annotations_
NTSTATUS
SM5714BatteryQueryStatus(
	PVOID Context,
	ULONG BatteryTag,
	PBATTERY_STATUS BatteryStatus
)

/*++

Routine Description:

	Called by the class driver to retrieve the batteries current status

	The battery class driver will serialize all requests it issues to
	the miniport for a given battery.

Arguments:

	Context - Supplies the miniport context value for battery

	BatteryTag - Supplies the tag of current battery

	BatteryStatus - Supplies a pointer to the structure to return the current
		battery status in

Return Value:

	Success if there is a battery currently installed, else no such device.

--*/

{
	PSM5714_BATTERY_FDO_DATA DevExt;
	NTSTATUS Status;
	Trace(TRACE_LEVEL_INFORMATION, SM5714_BATTERY_TRACE, "Entering %!FUNC!\n");
	PAGED_CODE();

	DevExt = (PSM5714_BATTERY_FDO_DATA)Context;
	WdfWaitLockAcquire(DevExt->StateLock, NULL);
	if (BatteryTag != DevExt->BatteryTag) {
		Status = STATUS_NO_SUCH_DEVICE;
		goto QueryStatusEnd;
	}

	// Fetch State of Charge
	unsigned int     Capacity = 0;
	Status = sm5714_Get_BatterySoC(DevExt, &Capacity);
	if (!NT_SUCCESS(Status)) {
		goto QueryStatusEnd;
	}

	// Fetch Voltage(mV)
	unsigned int     Voltage = 0;
	Status = sm5714_Get_BatteryVoltage(DevExt, &Voltage);
	if (!NT_SUCCESS(Status)) {
		goto QueryStatusEnd;
	}

	// Fetch Current (mA) over I2C
	LONG Current = 0;
	Status = sm5714_Get_BatteryCurrent(DevExt, &Current);
	if (!NT_SUCCESS(Status)) {
		goto QueryStatusEnd;
	}
	DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "CURRENT: %d mA\n", Current);

	//
	// Determine power state from TypeC detection (via ACPI GBST method).
	// GBST returns 1 when a charger is attached, 0 when discharging/OTG.
	//
	if (Sm5714QueryChargingState(DevExt)) {
		Trace(TRACE_LEVEL_INFORMATION, SM5714_BATTERY_TRACE, "BATTERY_POWER_ON_LINE\n");
		BatteryStatus->PowerState = BATTERY_POWER_ON_LINE;
	}
	else {
		Trace(TRACE_LEVEL_INFORMATION, SM5714_BATTERY_TRACE, "BATTERY_DISCHARGING\n");
		BatteryStatus->PowerState = BATTERY_DISCHARGING;
	}

	/*
	 * BatteryStatus expects:
	 * - Capacity in mWh
	 * - Voltage in mV
	 * - Rate in mW (signed)
	 */

	// mW
	BatteryStatus->Capacity = (ULONG)(((ULONGLONG)Capacity *
		DevExt->FullChargedCapacity_mWh) / 1000);
	// mV
	BatteryStatus->Voltage = (ULONG)Voltage;
	// mW (Signed)
	BatteryStatus->Rate = (LONG)(((LONGLONG)Current * Voltage) / 1000);

	// Debug: Print final BatteryStatus
	Trace(
		TRACE_LEVEL_INFORMATION,
		SM5714_BATTERY_TRACE,
		"BATTERY_STATUS: \n"
		"PowerState: %d \n"
		"Capacity: %d \n"
		"Voltage: %d \n"
		"Rate: %d\n",
		BatteryStatus->PowerState,
		BatteryStatus->Capacity,
		BatteryStatus->Voltage,
		BatteryStatus->Rate);

	Status = STATUS_SUCCESS;

QueryStatusEnd:
	WdfWaitLockRelease(DevExt->StateLock);
	Trace(TRACE_LEVEL_INFORMATION, SM5714_BATTERY_TRACE, "Leaving %!FUNC!: Status = 0x%08lX\n", Status);
	return Status;
}

_Use_decl_annotations_
NTSTATUS
SM5714BatterySetStatusNotify(
	PVOID Context,
	ULONG BatteryTag,
	PBATTERY_NOTIFY BatteryNotify
)

/*++

Routine Description:

	Called by the class driver to set the capacity and power state levels
	at which the class driver requires notification.

	The battery class driver will serialize all requests it issues to
	the miniport for a given battery.

Arguments:

	Context - Supplies the miniport context value for battery

	BatteryTag - Supplies the tag of current battery

	BatteryNotify - Supplies a pointer to a structure containing the
		notification critera.

Return Value:

	Success if there is a battery currently installed, else no such device.

--*/

{
	PSM5714_BATTERY_FDO_DATA DevExt;
	NTSTATUS Status;

	UNREFERENCED_PARAMETER(BatteryNotify);

	Trace(TRACE_LEVEL_INFORMATION, SM5714_BATTERY_TRACE, "Entering %!FUNC!\n");
	PAGED_CODE();

	DevExt = (PSM5714_BATTERY_FDO_DATA)Context;
	WdfWaitLockAcquire(DevExt->StateLock, NULL);
	if (BatteryTag != DevExt->BatteryTag) {
		Status = STATUS_NO_SUCH_DEVICE;
		goto SetStatusNotifyEnd;
	}

	Status = STATUS_NOT_SUPPORTED;

SetStatusNotifyEnd:
	WdfWaitLockRelease(DevExt->StateLock);
	Trace(TRACE_LEVEL_INFORMATION, SM5714_BATTERY_TRACE, "Leaving %!FUNC!: Status = 0x%08lX\n", Status);
	return Status;
}

_Use_decl_annotations_
NTSTATUS
SM5714BatteryDisableStatusNotify(
	PVOID Context
)

/*++

Routine Description:

	Called by the class driver to disable notification.

	The battery class driver will serialize all requests it issues to
	the miniport for a given battery.

Arguments:

	Context - Supplies the miniport context value for battery

Return Value:

	Success if there is a battery currently installed, else no such device.

--*/

{
	NTSTATUS Status;

	UNREFERENCED_PARAMETER(Context);

	Trace(TRACE_LEVEL_INFORMATION, SM5714_BATTERY_TRACE, "Entering %!FUNC!\n");
	PAGED_CODE();

	Status = STATUS_NOT_SUPPORTED;
	Trace(TRACE_LEVEL_INFORMATION, SM5714_BATTERY_TRACE, "Leaving %!FUNC!: Status = 0x%08lX\n", Status);
	return Status;
}

_Use_decl_annotations_
NTSTATUS
SM5714BatterySetInformation(
	PVOID Context,
	ULONG BatteryTag,
	BATTERY_SET_INFORMATION_LEVEL Level,
	PVOID Buffer
)

/*
 Routine Description:

	Called by the class driver to set the battery's charge/discharge state,
	critical bias, or charge current.

Arguments:

	Context - Supplies the miniport context value for battery

	BatteryTag - Supplies the tag of current battery

	Level - Supplies action requested

	Buffer - Supplies a critical bias value if level is BatteryCriticalBias.

Return Value:

	NTSTATUS

--*/

{
	PBATTERY_CHARGING_SOURCE ChargingSource;
	PULONG CriticalBias;
	PBATTERY_CHARGER_ID ChargerId;
	PBATTERY_CHARGER_STATUS ChargerStatus;
	PBATTERY_USB_CHARGER_STATUS UsbChargerStatus;
	USBFN_PORT_TYPE UsbFnPortType;
	PSM5714_BATTERY_FDO_DATA DevExt;
	NTSTATUS Status;

	Trace(TRACE_LEVEL_INFORMATION, SM5714_BATTERY_TRACE, "Entering %!FUNC!\n");
	PAGED_CODE();

	DevExt = (PSM5714_BATTERY_FDO_DATA)Context;
	WdfWaitLockAcquire(DevExt->StateLock, NULL);
	if (BatteryTag != DevExt->BatteryTag) {
		Status = STATUS_NO_SUCH_DEVICE;
		goto SetInformationEnd;
	}

	if (Level == BatteryCharge)
	{
		Trace(TRACE_LEVEL_INFORMATION, SM5714_BATTERY_INFO, "SM5714Battery : BatteryCharge\n");

		Status = STATUS_SUCCESS;
	}
	else if (Level == BatteryDischarge)
	{
		Trace(TRACE_LEVEL_INFORMATION, SM5714_BATTERY_INFO, "SM5714Battery : BatteryDischarge\n");

		Status = STATUS_SUCCESS;
	}
	else if (Buffer == NULL)
	{
		Status = STATUS_INVALID_PARAMETER_4;
	}
	else if (Level == BatteryChargingSource)
	{
		ChargingSource = (PBATTERY_CHARGING_SOURCE)Buffer;

		Trace(TRACE_LEVEL_INFORMATION, SM5714_BATTERY_INFO, "SM5714Battery : BatteryChargingSource Type = %d\n", ChargingSource->Type);

		Trace(TRACE_LEVEL_INFORMATION, SM5714_BATTERY_INFO, "SM5714Battery : Set MaxCurrentDraw = %u mA\n", ChargingSource->MaxCurrent);

		Status = STATUS_SUCCESS;
	}
	else if (Level == BatteryCriticalBias)
	{
		CriticalBias = (PULONG)Buffer;
		Trace(TRACE_LEVEL_INFORMATION, SM5714_BATTERY_INFO, "SM5714Battery : Set CriticalBias = %u mW\n", *CriticalBias);

		Status = STATUS_SUCCESS;
	}
	else if (Level == BatteryChargerId)
	{
		ChargerId = (PBATTERY_CHARGER_ID)Buffer;
		Trace(TRACE_LEVEL_INFORMATION, SM5714_BATTERY_INFO, "SM5714Battery : BatteryChargerId = %!GUID!\n", ChargerId);

		Status = STATUS_SUCCESS;
	}
	else if (Level == BatteryChargerStatus)
	{
		ChargerStatus = (PBATTERY_CHARGER_STATUS)Buffer;

		Trace(TRACE_LEVEL_INFORMATION, SM5714_BATTERY_INFO, "SM5714Battery : BatteryChargingSource Type = %d\n", ChargerStatus->Type);

		if (ChargerStatus->Type == BatteryChargingSourceType_USB)
		{
			UsbChargerStatus = (PBATTERY_USB_CHARGER_STATUS)Buffer;

			Trace(TRACE_LEVEL_INFORMATION, SM5714_BATTERY_INFO,
				"SM5714Battery : BatteryChargingSourceType_USB: Flags = %d, MaxCurrent = %d, Voltage = %d, PortType = %d, PortId = %llu, OemCharger = %!GUID!\n",
				UsbChargerStatus->Flags, UsbChargerStatus->MaxCurrent, UsbChargerStatus->Voltage, UsbChargerStatus->PortType, UsbChargerStatus->PortId, &UsbChargerStatus->OemCharger);

			UsbFnPortType = (USBFN_PORT_TYPE)(UINT64)UsbChargerStatus->PowerSourceInformation;

			Trace(TRACE_LEVEL_INFORMATION, SM5714_BATTERY_INFO, "SM5714Battery : UsbFnPortType = %d\n", UsbFnPortType);
		}

		Status = STATUS_SUCCESS;
	}
	else
	{
		Status = STATUS_NOT_SUPPORTED;
	}

SetInformationEnd:
	WdfWaitLockRelease(DevExt->StateLock);
	Trace(TRACE_LEVEL_INFORMATION, SM5714_BATTERY_TRACE, "Leaving %!FUNC!: Status = 0x%08lX\n", Status);
	return Status;
}