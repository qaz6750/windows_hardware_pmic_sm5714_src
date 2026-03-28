#include "..\inc\SM5714Battery.h"
#include "..\inc\Spb.h"
#include "..\inc\SM5714Battery_regs.h"
#include "..\inc\sm5714_fuelgauge.h"
#include "sm5714_fuelgauge.tmh"

NTSTATUS
sm5714_Get_CycleCount(
	PSM5714_BATTERY_FDO_DATA DevExt,
	PULONG CycleCount
)
{
	NTSTATUS Status;
	int			   Cycle = 0;
	unsigned short rawCycle = 0;

	Status = SpbWriteRead(&DevExt->I2CContext, (PVOID)write_cycle, sizeof(write_cycle), &readCmd, sizeof(readCmd), &rawCycle, sizeof(rawCycle), 0);
	if (!NT_SUCCESS(Status))
	{
		Trace(TRACE_LEVEL_ERROR, SM5714_BATTERY_TRACE, "Failed to SPB write/read raw cycle count. Status=0x%08lX\n", Status);
		goto Exit;
	}

	if (rawCycle < 0) {
		Cycle = 0;
	}
	else {
		Cycle = rawCycle & 0x00FF;
	}

	*CycleCount = Cycle;

Exit:
	Trace(TRACE_LEVEL_INFORMATION, SM5714_BATTERY_TRACE, "Leaving %!FUNC!: Status = 0x%08lX\n", Status);
	return Status;
}

NTSTATUS
sm5714_Get_BatteryTemperature(
	PSM5714_BATTERY_FDO_DATA DevExt,
	PULONG Temperature
)
{
	NTSTATUS Status;
	unsigned short rawTemp = 0;
	int			   Temp = 0;

	Status = SpbWriteRead(&DevExt->I2CContext, (PVOID)write_temperature, sizeof(write_temperature), &readCmd, sizeof(readCmd), &rawTemp, sizeof(rawTemp), 0);
	if (!NT_SUCCESS(Status))
	{
		Trace(TRACE_LEVEL_ERROR, SM5714_BATTERY_TRACE, "Failed to SPB write/read raw battery temperature. Status=0x%08lX\n", Status);
		*Temperature = 2982; /* default 25.0C in 0.1 Kelvin */
		goto Exit;
	}

	/* temp = (((ret & 0x7FFF) * 10) * 2989) >> 11 >> 8; (result in 0.1 degC) */
	Temp = (int)(((__int64)(rawTemp & 0x7FFF) * 10 * 2989) >> 19);
	if (rawTemp & 0x8000)
		Temp *= -1;

	/* Windows BatteryTemperature unit : 0.1 Kelvin */
	*Temperature = (ULONG)(Temp + 2732);

Exit:
	Trace(TRACE_LEVEL_INFORMATION, SM5714_BATTERY_TRACE, "Leaving %!FUNC!: Temp=%d (0.1K), Status = 0x%08lX\n", *Temperature, Status);
	return Status;
}

NTSTATUS
sm5714_Get_BatterySoC(
	PSM5714_BATTERY_FDO_DATA DevExt,
	PULONG Capacity
)
{
	NTSTATUS Status;
	unsigned short rawCapacity = 0;

	Status = SpbWriteRead(&DevExt->I2CContext, (PVOID)write_capacity, sizeof(write_capacity), &readCmd, sizeof(readCmd), &rawCapacity, sizeof(rawCapacity), 0);
	if (!NT_SUCCESS(Status))
	{
		Trace(TRACE_LEVEL_ERROR, SM5714_BATTERY_TRACE, "Failed to SPB write/read raw State of Charge. Status=0x%08lX\n", Status);
	}

	*Capacity = FIXED_POINT_8_8_EXTEND_TO_INT((unsigned short)rawCapacity, 10);


Exit:
	Trace(TRACE_LEVEL_INFORMATION, SM5714_BATTERY_TRACE, "Leaving %!FUNC!: Status = 0x%08lX\n", Status);
	return Status;
}

NTSTATUS
sm5714_Get_BatteryVoltage(
	PSM5714_BATTERY_FDO_DATA DevExt,
	PULONG Voltage
)
{
	NTSTATUS Status;
	unsigned int   Volt = 0;
	unsigned short rawOcv = 0;

	Status = SpbWriteRead(&DevExt->I2CContext, (PVOID)write_ocv, sizeof(write_ocv), &readCmd, sizeof(readCmd), &rawOcv, sizeof(rawOcv), 0);
	if (!NT_SUCCESS(Status))
	{
		Trace(TRACE_LEVEL_ERROR, SM5714_BATTERY_TRACE, "Failed to SPB write/read raw voltage. Status=0x%08lX\n", Status);
	}

	if (rawOcv < 0) {
		Volt = 4000;
	}
	else {
		Volt = ((rawOcv & 0x3800) >> 11) * 1000;         //integer;
		Volt = Volt + (((rawOcv & 0x07ff) * 1000) / 2048); // integer + fractional
	}

	*Voltage = Volt;

Exit:
	Trace(TRACE_LEVEL_INFORMATION, SM5714_BATTERY_TRACE, "Leaving %!FUNC!: Status = 0x%08lX\n", Status);
	return Status;
}

NTSTATUS
sm5714_Get_BatteryCurrent(
	PSM5714_BATTERY_FDO_DATA DevExt,
	PULONG Current
)
{
	NTSTATUS Status;
	int   Curr = 0;
	unsigned short rawCurr = 0;

	Status = SpbWriteRead(&DevExt->I2CContext, (PVOID)write_current, sizeof(write_current), &readCmd, sizeof(readCmd), &rawCurr, sizeof(rawCurr), 0);
	if (!NT_SUCCESS(Status))
	{
		Trace(TRACE_LEVEL_ERROR, SM5714_BATTERY_TRACE, "Failed to SPB write/read raw current. Status=0x%08lX\n", Status);
	}
	Curr = ((rawCurr & 0x1800) >> 11) * 1000; //integer;
	Curr = Curr + (((rawCurr & 0x07ff) * 1000) / 2048); // integer + fractional
	if (rawCurr & 0x8000)
		Curr *= -1;

	*Current = Curr;


Exit:
	Trace(TRACE_LEVEL_INFORMATION, SM5714_BATTERY_TRACE, "Leaving %!FUNC!: Status = 0x%08lX\n", Status);
	return Status;
}
