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
	unsigned short rawCycle = 0;

	*CycleCount = 0;

	Status = SpbWriteRead(&DevExt->I2CContext, (PVOID)write_cycle, sizeof(write_cycle), &readCmd, sizeof(readCmd), &rawCycle, sizeof(rawCycle), 0);
	if (!NT_SUCCESS(Status))
	{
		Trace(TRACE_LEVEL_ERROR, SM5714_BATTERY_TRACE, "Failed to SPB write/read raw cycle count. Status=0x%08lX\n", Status);
		goto Exit;
	}

	*CycleCount = rawCycle & 0x00FF;

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

	*Temperature = 0;

	Status = SpbWriteRead(&DevExt->I2CContext, (PVOID)write_temperature, sizeof(write_temperature), &readCmd, sizeof(readCmd), &rawTemp, sizeof(rawTemp), 0);
	if (!NT_SUCCESS(Status))
	{
		Trace(TRACE_LEVEL_ERROR, SM5714_BATTERY_TRACE, "Failed to SPB write/read raw battery temperature. Status=0x%08lX\n", Status);
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

	*Capacity = 0;

	Status = SpbWriteRead(&DevExt->I2CContext, (PVOID)write_capacity, sizeof(write_capacity), &readCmd, sizeof(readCmd), &rawCapacity, sizeof(rawCapacity), 0);
	if (!NT_SUCCESS(Status))
	{
		Trace(TRACE_LEVEL_ERROR, SM5714_BATTERY_TRACE, "Failed to SPB write/read raw State of Charge. Status=0x%08lX\n", Status);
		goto Exit;
	}

	*Capacity = FIXED_POINT_8_8_EXTEND_TO_INT((unsigned short)rawCapacity, 10);
	if (*Capacity > 1000) {
		*Capacity = 1000;
	}


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
	NTSTATUS       Status;
	unsigned short rawVbat = 0;
	unsigned int   Volt = 0;

	*Voltage = 0;

	/*
	 * Read the averaged battery terminal voltage from SRAM_VBAT_AVG (0x08).
	 * Register encoding (same as sm5714_get_vbat in Android driver):
	 *   vbat_mV = ((raw & 0x7FFF) * 10) / 109 + 2700
	 * Bit 15 indicates a negative offset below 2700 mV (should never occur
	 * on a healthy battery, but guard it anyway).
	 */
	Status = SpbWriteRead(&DevExt->I2CContext,
	                      (PVOID)write_vbat_avg, sizeof(write_vbat_avg),
	                      &readCmd, sizeof(readCmd),
	                      &rawVbat, sizeof(rawVbat), 0);
	if (!NT_SUCCESS(Status)) {
		Trace(TRACE_LEVEL_ERROR, SM5714_BATTERY_TRACE,
		      "Failed to read VBAT_AVG. Status=0x%08lX\n", Status);
		goto Exit;
	}

	if (rawVbat & 0x8000) {
		Volt = 2700 - (((rawVbat & 0x7FFF) * 10) / 109);
	} else {
		Volt = ((rawVbat * 10) / 109) + 2700;
	}

	*Voltage = Volt;

Exit:
	Trace(TRACE_LEVEL_INFORMATION, SM5714_BATTERY_TRACE,
	      "Leaving %!FUNC!: Voltage=%u mV, Status=0x%08lX\n", *Voltage, Status);
	return Status;
}

NTSTATUS
sm5714_Get_BatteryCurrent(
	PSM5714_BATTERY_FDO_DATA DevExt,
	PLONG Current
)
{
	NTSTATUS       Status;
	unsigned short rawCurr = 0;
	int            Curr = 0;

	*Current = 0;

	/*
	 * Read the averaged current from SRAM_CURRENT_AVG (0x09).
	 * Register encoding (matches sm5714_get_curr in Android driver):
	 *   curr_mA = (raw & 0x7FFF) * 1000 / 2044
	 * Bit 15: 0 = charging (positive), 1 = discharging (negative).
	 * Using the averaged register gives smoother Windows power-meter values.
	 */
	Status = SpbWriteRead(&DevExt->I2CContext,
	                      (PVOID)write_current_avg, sizeof(write_current_avg),
	                      &readCmd, sizeof(readCmd),
	                      &rawCurr, sizeof(rawCurr), 0);
	if (!NT_SUCCESS(Status)) {
		Trace(TRACE_LEVEL_ERROR, SM5714_BATTERY_TRACE,
		      "Failed to read CURRENT_AVG. Status=0x%08lX\n", Status);
		goto Exit;
	}

	Curr = (int)(((unsigned int)(rawCurr & 0x7FFF) * 1000) / 2044);
	if (rawCurr & 0x8000) {
		Curr = -Curr;
	}

	*Current = (LONG)Curr;

Exit:
	Trace(TRACE_LEVEL_INFORMATION, SM5714_BATTERY_TRACE,
	      "Leaving %!FUNC!: Status=0x%08lX\n", Status);
	return Status;
}

/*
 * sm5714_Get_BatteryHealth - Read the battery State-of-Health from the FG.
 *
 * The SM5714 FG stores a running SOH estimate in two SRAM registers:
 *   AGING_RATE_FILT (0x46): raw IC-calculated aging rate (Q11.0 fixed-point)
 *   USER_RESERV_2   (0x8B): monotonically-decremented SOH snapshot (bits[6:0])
 *                           bit 7 is an internal hysteresis flag (ignored here)
 *
 * Android driver logic (sm5714_get_asoc):
 *   ic_soh  = aging_rate_filt * 100 / 2048   (convert to %)
 *   stored  = user_reserv_2 & 0x7F           (persisted 0-100 value)
 *   returned soh = stored (only decremented, never increased beyond ic_soh)
 *
 * We return the stored SOH (0-100 %).  On a new battery this will be 100.
 * Returns STATUS_SUCCESS; on read error *Soh is left at 100 (optimistic).
 */
NTSTATUS
sm5714_Get_BatteryHealth(
	PSM5714_BATTERY_FDO_DATA DevExt,
	PULONG                   Soh
)
{
	NTSTATUS       Status;
	unsigned short rawAgingRate = 0;
	unsigned short rawReserv    = 0;
	int            icSoh;
	int            storedSoh;

	*Soh = 100;

	Status = SpbWriteRead(&DevExt->I2CContext,
	                      (PVOID)write_aging_rate, sizeof(write_aging_rate),
	                      &readCmd, sizeof(readCmd),
	                      &rawAgingRate, sizeof(rawAgingRate), 0);
	if (!NT_SUCCESS(Status)) {
		Trace(TRACE_LEVEL_WARNING, SM5714_BATTERY_TRACE,
		      "Failed to read AGING_RATE_FILT. Status=0x%08lX\n", Status);
		goto Exit;
	}

	Status = SpbWriteRead(&DevExt->I2CContext,
	                      (PVOID)write_user_reserv2, sizeof(write_user_reserv2),
	                      &readCmd, sizeof(readCmd),
	                      &rawReserv, sizeof(rawReserv), 0);
	if (!NT_SUCCESS(Status)) {
		Trace(TRACE_LEVEL_WARNING, SM5714_BATTERY_TRACE,
		      "Failed to read USER_RESERV_2. Status=0x%08lX\n", Status);
		goto Exit;
	}

	/* Convert raw aging rate to percentage (Q11.0 to %) */
	icSoh     = (int)((unsigned int)rawAgingRate * 100 / 2048);
	storedSoh = (int)(rawReserv & 0x7F);

	/*
	 * If the FG has never written a stored SOH (fresh battery or first boot),
	 * USER_RESERV_2 reads 0.  Fall back to the IC-computed value in that case.
	 */
	if (storedSoh == 0) {
		storedSoh = (icSoh > 0) ? icSoh : 100;
	}

	if (storedSoh > 100) {
		storedSoh = 100;
	}

	*Soh = (ULONG)storedSoh;

Exit:
	Trace(TRACE_LEVEL_INFORMATION, SM5714_BATTERY_TRACE,
	      "Leaving %!FUNC!: SOH=%lu%%, Status=0x%08lX\n", *Soh, Status);
	return Status;
}
