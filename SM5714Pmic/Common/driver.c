#include "driver.h"
#include "registers.h"
#include "spbhelper.h"
#include "..\Charger\charger.h"
#include "..\TypeC\typec.h"

static ULONG DebugLevel = 100;
static ULONG DebugCategories = DBG_INIT | DBG_PNP | DBG_IOCTL;

#define GET_INTEGER(_arg_) (*(PULONG UNALIGNED)((_arg_)->Data))

//
// ISR for charger interrupt (GPIO 54)
// Runs at PASSIVE_LEVEL due to PassiveHandling = TRUE.
// Reads CHG INT1-5 registers and logs charger state changes.
//
BOOLEAN
EvtChgInterruptIsr(
    _In_ WDFINTERRUPT Interrupt,
    _In_ ULONG MessageID
)
{
    UNREFERENCED_PARAMETER(MessageID);

    PDEVICE_CONTEXT pDevice = GetDeviceContext(
        WdfInterruptGetDevice(Interrupt));

    if (pDevice->SpbContextCount < 1) {
        return FALSE;
    }

    if (!pDevice->DevicePoweredOn) {
        //
        // Must still read INT registers to deassert the line.
        //
        UCHAR dummy;
        read_reg8(pDevice, SPB_CHARGER_INDEX, SM5714_CHG_REG_INT1, &dummy);
        read_reg8(pDevice, SPB_CHARGER_INDEX, SM5714_CHG_REG_INT2, &dummy);
        read_reg8(pDevice, SPB_CHARGER_INDEX, SM5714_CHG_REG_INT3, &dummy);
        read_reg8(pDevice, SPB_CHARGER_INDEX, SM5714_CHG_REG_INT4, &dummy);
        read_reg8(pDevice, SPB_CHARGER_INDEX, SM5714_CHG_REG_INT5, &dummy);
        return TRUE;
    }

    WdfWaitLockAcquire(pDevice->DataLock, NULL);
    ChargerProcessInterrupts(pDevice);
    WdfWaitLockRelease(pDevice->DataLock);

    return TRUE;
}

//
// ISR for USBPD interrupt (GPIO 142)
// Runs at PASSIVE_LEVEL (PassiveHandling = TRUE), so we can
// safely perform I2C transactions directly.
//
// CRITICAL: Must ALWAYS read INT registers to deassert the INT line.
// SM5714 holds INT low until registers are read. With Level trigger,
// failing to clear will cause an interrupt storm.
//
BOOLEAN
EvtPdInterruptIsr(
    _In_ WDFINTERRUPT Interrupt,
    _In_ ULONG MessageID
)
{
    UNREFERENCED_PARAMETER(MessageID);

    PDEVICE_CONTEXT pDevice = GetDeviceContext(
        WdfInterruptGetDevice(Interrupt));

    // If USBPD I2C bus is not available, we cannot service this interrupt
    if (pDevice->SpbContextCount < 2)
        return FALSE;

    if (!pDevice->DevicePoweredOn || !pDevice->TypecInitialized)
    {
        // Not ready to process events yet, but must clear interrupt
        // registers to deassert the INT line and prevent storm/deadlock.
        UCHAR dummy;
        read_reg8(pDevice, SPB_USBPD_INDEX, SM5714_REG_INT1, &dummy);
        read_reg8(pDevice, SPB_USBPD_INDEX, SM5714_REG_INT2, &dummy);
        read_reg8(pDevice, SPB_USBPD_INDEX, SM5714_REG_INT3, &dummy);
        read_reg8(pDevice, SPB_USBPD_INDEX, SM5714_REG_INT4, &dummy);
        read_reg8(pDevice, SPB_USBPD_INDEX, SM5714_REG_INT5, &dummy);
        return TRUE;
    }

    // PassiveHandling = TRUE means we are already at PASSIVE_LEVEL.
    // Process the interrupt inline - I2C access is safe here.
    WdfWaitLockAcquire(pDevice->DataLock, NULL);
    typec_process_interrupt(pDevice);
    WdfWaitLockRelease(pDevice->DataLock);

    return TRUE;
}

static
NTSTATUS
FetchPmicConfig(
    _In_  WDFDEVICE        Device,
    _Out_ PDEVICE_CONTEXT  DevCtx
)
{
    ACPI_EVAL_INPUT_BUFFER input = { 0 };
    input.Signature = ACPI_EVAL_INPUT_BUFFER_SIGNATURE;
    memcpy(input.MethodName, "PMIC", 4);

    // Support up to 8 ACPI arguments
    const ULONG outLen =
        sizeof(ACPI_EVAL_OUTPUT_BUFFER) + 8 * sizeof(ACPI_METHOD_ARGUMENT);

    PACPI_EVAL_OUTPUT_BUFFER output =
        (PACPI_EVAL_OUTPUT_BUFFER)ExAllocatePoolZero(
            NonPagedPoolNx, outLen, 'cimP');
    if (!output)
        return STATUS_INSUFFICIENT_RESOURCES;

    WDF_MEMORY_DESCRIPTOR inDesc, outDesc;
    WDF_MEMORY_DESCRIPTOR_INIT_BUFFER(&inDesc, &input, sizeof(input));
    WDF_MEMORY_DESCRIPTOR_INIT_BUFFER(&outDesc, output, outLen);

    ULONG_PTR bytesReturned = 0;
    NTSTATUS status = WdfIoTargetSendIoctlSynchronously(
        WdfDeviceGetIoTarget(Device),
        NULL,
        IOCTL_ACPI_EVAL_METHOD,
        &inDesc,
        &outDesc,
        NULL,
        &bytesReturned);

    if (NT_SUCCESS(status) &&
        output->Signature == ACPI_EVAL_OUTPUT_BUFFER_SIGNATURE &&
        output->Count >= 4 &&
        output->Argument[0].Type == ACPI_METHOD_ARGUMENT_INTEGER &&
        output->Argument[1].Type == ACPI_METHOD_ARGUMENT_INTEGER &&
        output->Argument[2].Type == ACPI_METHOD_ARGUMENT_INTEGER &&
        output->Argument[3].Type == ACPI_METHOD_ARGUMENT_INTEGER)
    {
        // Arguments 0-3: basic charging (required)
        DevCtx->Autostop           = GET_INTEGER(&output->Argument[0]) ? TRUE : FALSE;
        DevCtx->InputCurrentLimit  = GET_INTEGER(&output->Argument[1]);
        DevCtx->ChargingCurrent    = GET_INTEGER(&output->Argument[2]);
        DevCtx->TopoffCurrent      = GET_INTEGER(&output->Argument[3]);

        // Arguments 4-7: advanced charging (optional, use defaults)
        DevCtx->FloatVoltage       = 4380;  // GTS7FE battery float voltage
        DevCtx->WdtTimer           = 2;     // 90s
        DevCtx->AiclEnabled        = FALSE;
        DevCtx->DischgLimit        = 7;     // Disabled by GTS7FE battery data
        DevCtx->TopoffTimer        = 3;     // 45min
        DevCtx->LxSlope            = 1;     // SM5714 default
        DevCtx->TrickleCurrent     = 450;   // Default: 450mA

        if (output->Count >= 5 &&
            output->Argument[4].Type == ACPI_METHOD_ARGUMENT_INTEGER)
            DevCtx->FloatVoltage = GET_INTEGER(&output->Argument[4]);

        if (output->Count >= 6 &&
            output->Argument[5].Type == ACPI_METHOD_ARGUMENT_INTEGER)
            DevCtx->WdtTimer = (UCHAR)GET_INTEGER(&output->Argument[5]);

        if (output->Count >= 7 &&
            output->Argument[6].Type == ACPI_METHOD_ARGUMENT_INTEGER)
            DevCtx->AiclEnabled = GET_INTEGER(&output->Argument[6]) ? TRUE : FALSE;

        if (output->Count >= 8 &&
            output->Argument[7].Type == ACPI_METHOD_ARGUMENT_INTEGER)
            DevCtx->DischgLimit = (UCHAR)GET_INTEGER(&output->Argument[7]);
    }
    else
    {
        status = STATUS_ACPI_INVALID_DATA;
    }

    ExFreePool(output);
    return status;
}

NTSTATUS
DriverEntry(
    __in PDRIVER_OBJECT  DriverObject,
    __in PUNICODE_STRING RegistryPath
)
{
    NTSTATUS               status = STATUS_SUCCESS;
    WDF_DRIVER_CONFIG      config;
    WDF_OBJECT_ATTRIBUTES  attributes;

    Print(DEBUG_LEVEL_INFO, DBG_INIT, "Driver Entry\n");

    WDF_DRIVER_CONFIG_INIT(&config, EvtDeviceAdd);

    WDF_OBJECT_ATTRIBUTES_INIT(&attributes);

    //
    // Create a framework driver object to represent our driver.
    //
    status = WdfDriverCreate(DriverObject, RegistryPath, &attributes, &config, WDF_NO_HANDLE);
    if (!NT_SUCCESS(status))
    {
        Print(DEBUG_LEVEL_ERROR, DBG_INIT, "WdfDriverCreate failed with status 0x%x\n", status);
    }
    return status;
}

NTSTATUS
OnPrepareHardware(
    _In_  WDFDEVICE     FxDevice,
    _In_  WDFCMRESLIST  FxResourcesRaw,
    _In_  WDFCMRESLIST  FxResourcesTranslated
)
/*++

Routine Description:

This routine caches the SPB resource connection ID.

Arguments:

FxDevice - a handle to the framework device object
FxResourcesRaw - list of translated hardware resources that
the PnP manager has assigned to the device
FxResourcesTranslated - list of raw hardware resources that
the PnP manager has assigned to the device

Return Value:

Status

--*/
{
    PDEVICE_CONTEXT pDevice = GetDeviceContext(FxDevice);
    NTSTATUS status = STATUS_INSUFFICIENT_RESOURCES;
    pDevice->SpbContextCount = 0;

    UNREFERENCED_PARAMETER(FxResourcesRaw);

    //
    // Parse the peripheral's resources.
    //
    ULONG resourceCount = WdfCmResourceListGetCount(FxResourcesTranslated);

    for (ULONG i = 0; i < resourceCount; i++)
    {
        PCM_PARTIAL_RESOURCE_DESCRIPTOR pDescriptor =
            WdfCmResourceListGetDescriptor(FxResourcesTranslated, i);

        if (pDescriptor == NULL)
        {
            continue;
        }

        if (pDescriptor->Type == CmResourceTypeConnection &&
            pDescriptor->u.Connection.Class == CM_RESOURCE_CONNECTION_CLASS_SERIAL &&
            pDescriptor->u.Connection.Type == CM_RESOURCE_CONNECTION_TYPE_SERIAL_I2C)
        {
            if (pDevice->SpbContextCount >= ARRAYSIZE(pDevice->SpbContexts))
            {
                break;
            }

            SPB_CONTEXT* spbCtx = &pDevice->SpbContexts[pDevice->SpbContextCount];

            spbCtx->I2cResHubId.LowPart = pDescriptor->u.Connection.IdLowPart;
            spbCtx->I2cResHubId.HighPart = pDescriptor->u.Connection.IdHighPart;

            status = SpbTargetInitialize(FxDevice, spbCtx);
            if (!NT_SUCCESS(status))
            {
                if (pDevice->SpbContextCount == 0)
                {
                    // First I2C (charger) is required - fail hard
                    Print(DEBUG_LEVEL_ERROR, DBG_PNP,
                          "Charger I2C target open failed - 0x%x\n", status);
                    return status;
                }

                // Second I2C (USBPD) or third I2C (MUIC) failure is non-fatal
                Print(DEBUG_LEVEL_INFO, DBG_PNP,
                      "I2C target #%lu open failed - 0x%x, some features disabled\n",
                      pDevice->SpbContextCount, status);
                break;
            }

            pDevice->SpbContextCount++;
        }
    }

    if (pDevice->SpbContextCount == 0)
    {
        return STATUS_NOT_FOUND;
    }

    return STATUS_SUCCESS;
}

NTSTATUS
OnReleaseHardware(
    _In_  WDFDEVICE     FxDevice,
    _In_  WDFCMRESLIST  FxResourcesTranslated
)
/*++

Routine Description:

Arguments:

FxDevice - a handle to the framework device object
FxResourcesTranslated - list of raw hardware resources that
the PnP manager has assigned to the device

Return Value:

Status

--*/
{
    PDEVICE_CONTEXT pDevice = GetDeviceContext(FxDevice);
    UNREFERENCED_PARAMETER(FxResourcesTranslated);

    // Deinitialize each SPB_CONTEXT in the array
    for (ULONG i = 0; i < pDevice->SpbContextCount; i++)
    {
        SpbTargetDeinitialize(FxDevice, &pDevice->SpbContexts[i]);
    }

    pDevice->SpbContextCount = 0;

    return STATUS_SUCCESS;
}

NTSTATUS
OnD0Entry(
    _In_  WDFDEVICE               FxDevice,
    _In_  WDF_POWER_DEVICE_STATE  FxPreviousState
)
/*++

Routine Description:

This routine allocates objects needed by the driver.

Arguments:

FxDevice - a handle to the framework device object
FxPreviousState - previous power state

Return Value:

Status

--*/
{
    UNREFERENCED_PARAMETER(FxPreviousState);

    PDEVICE_CONTEXT pDevice = GetDeviceContext(FxDevice);
    NTSTATUS status = STATUS_SUCCESS;
    Print(DEBUG_LEVEL_INFO, DBG_PNP, "OnD0Entry called\n");

    pDevice->DevicePoweredOn = FALSE;
    pDevice->TypecInitialized = FALSE;

    status = FetchPmicConfig(FxDevice, pDevice);
    if (!NT_SUCCESS(status))
    {
        Print(DEBUG_LEVEL_INFO, DBG_INIT, "PMIC ACPI method missing\n");
        return status;
    }

    Print(DEBUG_LEVEL_INFO, DBG_INIT,
        "PMIC cfg:\n"
        "  Autostop=%s  FloatV=%lu mV  ICL=%lu mA  ICHG=%lu mA\n"
        "  Topoff=%lu mA  Trickle=%lu mA  WDT=%u  AICL=%s\n"
        "  DischgOCP=%u  TopoffTmr=%u  LXslope=%u\n",
        pDevice->Autostop ? "ON" : "OFF",
        pDevice->FloatVoltage,
        pDevice->InputCurrentLimit,
        pDevice->ChargingCurrent,
        pDevice->TopoffCurrent,
        pDevice->TrickleCurrent,
        pDevice->WdtTimer,
        pDevice->AiclEnabled ? "ON" : "OFF",
        pDevice->DischgLimit,
        pDevice->TopoffTimer,
        pDevice->LxSlope);

    // Configure charging
    status = ChargerProbe(pDevice);
    if (!NT_SUCCESS(status))
    {
        Print(DEBUG_LEVEL_ERROR, DBG_IOCTL,
              "Charger probe failed - %!STATUS!", status);
        goto exit;
    }

    //
    // Set up charger interrupt masks.
    // 0 = enabled, 1 = masked (inverted logic).
    //
    status = write_reg8(pDevice, SPB_CHARGER_INDEX,
                        SM5714_CHG_REG_INTMSK1, CHG_INT1_MASK_VALUE);
    if (!NT_SUCCESS(status)) goto exit;
    status = write_reg8(pDevice, SPB_CHARGER_INDEX,
                        SM5714_CHG_REG_INTMSK2, CHG_INT2_MASK_VALUE);
    if (!NT_SUCCESS(status)) goto exit;
    status = write_reg8(pDevice, SPB_CHARGER_INDEX,
                        SM5714_CHG_REG_INTMSK3, CHG_INT3_MASK_VALUE);
    if (!NT_SUCCESS(status)) goto exit;
    status = write_reg8(pDevice, SPB_CHARGER_INDEX,
                        SM5714_CHG_REG_INTMSK4, CHG_INT4_MASK_VALUE);
    if (!NT_SUCCESS(status)) goto exit;
    status = write_reg8(pDevice, SPB_CHARGER_INDEX,
                        SM5714_CHG_REG_INTMSK5, CHG_INT5_MASK_VALUE);
    if (!NT_SUCCESS(status)) goto exit;

    // Clear any pending charger interrupts
    {
        UCHAR dummy = 0;
        status = read_reg8(pDevice, SPB_CHARGER_INDEX,
                   SM5714_CHG_REG_INT1, &dummy);
        if (!NT_SUCCESS(status)) goto exit;
        status = read_reg8(pDevice, SPB_CHARGER_INDEX,
                   SM5714_CHG_REG_INT2, &dummy);
        if (!NT_SUCCESS(status)) goto exit;
        status = read_reg8(pDevice, SPB_CHARGER_INDEX,
                   SM5714_CHG_REG_INT3, &dummy);
        if (!NT_SUCCESS(status)) goto exit;
        status = read_reg8(pDevice, SPB_CHARGER_INDEX,
                   SM5714_CHG_REG_INT4, &dummy);
        if (!NT_SUCCESS(status)) goto exit;
        status = read_reg8(pDevice, SPB_CHARGER_INDEX,
                   SM5714_CHG_REG_INT5, &dummy);
        if (!NT_SUCCESS(status)) goto exit;
    }

    // Enable charging
    status = ChargerEnable(pDevice, TRUE);
    if (!NT_SUCCESS(status))
    {
        Print(DEBUG_LEVEL_ERROR, DBG_IOCTL, "Error enabling charging - %!STATUS!", status);
        goto exit;
    }

    status = WdfWaitLockCreate(WDF_NO_OBJECT_ATTRIBUTES, &pDevice->DataLock);
	if (!NT_SUCCESS(status))
	{
		Print(DEBUG_LEVEL_ERROR, DBG_IOCTL, "Error creating Data Waitlock - %!STATUS!", status);
		goto exit;
	}

    pDevice->DevicePoweredOn = TRUE;

    // Initialize the USBPD Type-C controller if we have the second I2C bus
    if (pDevice->SpbContextCount >= 2)
    {
        status = typec_reg_init(pDevice);
        if (!NT_SUCCESS(status))
        {
            Print(DEBUG_LEVEL_ERROR, DBG_INIT,
                  "USBPD init failed - 0x%x, USB Type-C will not work\n", status);
            // Non-fatal: charging still works without TypeC detection
            status = STATUS_SUCCESS;
        }
        else
        {
            pDevice->TypecInitialized = TRUE;

            // Check if a cable is already plugged in
            status = typec_check_initial_state(pDevice);
            if (!NT_SUCCESS(status))
            {
                Print(DEBUG_LEVEL_ERROR, DBG_INIT,
                      "USBPD initial state read failed - 0x%x\n", status);
                status = STATUS_SUCCESS;
            }
        }
    }
    else
    {
        Print(DEBUG_LEVEL_INFO, DBG_INIT,
              "No USBPD I2C resource found, TypeC detection disabled\n");
    }

exit:
    if (!NT_SUCCESS(status))
    {
        pDevice->DevicePoweredOn = FALSE;
        pDevice->TypecInitialized = FALSE;
        if (pDevice->DataLock != NULL)
        {
            WdfObjectDelete(pDevice->DataLock);
            pDevice->DataLock = NULL;
        }
    }

    return status;
}

NTSTATUS
OnD0Exit(
    _In_  WDFDEVICE               FxDevice,
    _In_  WDF_POWER_DEVICE_STATE  FxPreviousState
)
/*++

Routine Description:

This routine destroys objects needed by the driver.

Arguments:

FxDevice - a handle to the framework device object
FxPreviousState - previous power state

Return Value:

Status

--*/
{

    PDEVICE_CONTEXT pDevice = GetDeviceContext(FxDevice);
    NTSTATUS status = STATUS_SUCCESS;

    pDevice->DevicePoweredOn = FALSE;
    pDevice->TypecInitialized = FALSE;

    // Only disable charging if transitioning to OFF state (S5)
    if (FxPreviousState == WdfPowerDeviceD3Final)
    {
        // Disable OTG if active before shutting down
        // SM5714_ATTACH_SINK = 0x02
        if (pDevice->AttachType == 0x02)
            typec_set_otg_mode(pDevice, FALSE);

        ChargerEnable(pDevice, FALSE);
    }

    if (pDevice->DataLock != NULL)
    {
        WdfObjectDelete(pDevice->DataLock);
        pDevice->DataLock = NULL;
    }
    return status;
}

NTSTATUS
EvtDeviceAdd(
    IN WDFDRIVER       Driver,
    IN PWDFDEVICE_INIT DeviceInit
)
{
    NTSTATUS                      status = STATUS_SUCCESS;
    WDF_IO_QUEUE_CONFIG           queueConfig;
    WDF_OBJECT_ATTRIBUTES         attributes;
    WDFDEVICE                     device;
    WDFQUEUE                      queue;
    PDEVICE_CONTEXT               devContext;

    UNREFERENCED_PARAMETER(Driver);

    PAGED_CODE();

    Print(DEBUG_LEVEL_INFO, DBG_PNP, "EvtDeviceAdd called\n");

    {
        WDF_PNPPOWER_EVENT_CALLBACKS pnpCallbacks;
        WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&pnpCallbacks);

        pnpCallbacks.EvtDevicePrepareHardware = OnPrepareHardware;
        pnpCallbacks.EvtDeviceReleaseHardware = OnReleaseHardware;
        pnpCallbacks.EvtDeviceD0Entry = OnD0Entry;
        pnpCallbacks.EvtDeviceD0Exit = OnD0Exit;

        WdfDeviceInitSetPnpPowerEventCallbacks(DeviceInit, &pnpCallbacks);
    }

    //
    // Setup the device context
    //
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, DEVICE_CONTEXT);

    //
    // Create a framework device object.This call will in turn create
    // a WDM device object, attach to the lower stack, and set the
    // appropriate flags and attributes.
    //
    status = WdfDeviceCreate(&DeviceInit, &attributes, &device);
    if (!NT_SUCCESS(status))
    {
        Print(DEBUG_LEVEL_ERROR, DBG_PNP, "WdfDeviceCreate failed with status code 0x%x\n", status);
        return status;
    }

    {
        WDF_DEVICE_STATE deviceState;
        WDF_DEVICE_STATE_INIT(&deviceState);

        deviceState.NotDisableable = WdfFalse;
        WdfDeviceSetDeviceState(device, &deviceState);
    }

    WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&queueConfig, WdfIoQueueDispatchParallel);

    queueConfig.EvtIoInternalDeviceControl = EvtInternalDeviceControl;

    status = WdfIoQueueCreate(device, &queueConfig, WDF_NO_OBJECT_ATTRIBUTES, &queue);
    if (!NT_SUCCESS(status))
    {
        Print(DEBUG_LEVEL_ERROR, DBG_PNP, "WdfIoQueueCreate failed 0x%x\n", status);
        return status;
    }

    //
    // Create manual I/O queue to take care of hid report read requests
    //
    devContext = GetDeviceContext(device);
    devContext->FxDevice = device;

    //
    // Create interrupt objects for the two GPIO interrupts in _CRS.
    // WDF maps them in order: first = charger (GPIO 54), second = USBPD (GPIO 142).
    //

    // Charger interrupt (index 0)
    {
        WDF_INTERRUPT_CONFIG intConfig;
        WDF_INTERRUPT_CONFIG_INIT(&intConfig, EvtChgInterruptIsr, NULL);
        intConfig.PassiveHandling = TRUE;

        status = WdfInterruptCreate(device, &intConfig,
                                    WDF_NO_OBJECT_ATTRIBUTES,
                                    &devContext->ChgInterrupt);
        if (!NT_SUCCESS(status))
        {
            Print(DEBUG_LEVEL_ERROR, DBG_PNP,
                  "Charger interrupt create failed 0x%x\n", status);
            return status;
        }
    }

    // USBPD interrupt (index 1)
    {
        WDF_INTERRUPT_CONFIG intConfig;
        WDF_INTERRUPT_CONFIG_INIT(&intConfig, EvtPdInterruptIsr, NULL);
        intConfig.PassiveHandling = TRUE;

        status = WdfInterruptCreate(device, &intConfig,
                                    WDF_NO_OBJECT_ATTRIBUTES,
                                    &devContext->PdInterrupt);
        if (!NT_SUCCESS(status))
        {
            Print(DEBUG_LEVEL_ERROR, DBG_PNP,
                  "USBPD interrupt create failed 0x%x\n", status);
            return status;
        }
    }

    WDF_IO_QUEUE_CONFIG_INIT(&queueConfig, WdfIoQueueDispatchManual);

    queueConfig.PowerManaged = WdfFalse;

    status = WdfIoQueueCreate(device, &queueConfig, WDF_NO_OBJECT_ATTRIBUTES, &devContext->ReportQueue);
    if (!NT_SUCCESS(status))
    {
        Print(DEBUG_LEVEL_ERROR, DBG_PNP, "WdfIoQueueCreate failed 0x%x\n", status);
        return status;
    }
    return status;
}

VOID
EvtInternalDeviceControl(
    IN WDFQUEUE     Queue,
    IN WDFREQUEST   Request,
    IN size_t       OutputBufferLength,
    IN size_t       InputBufferLength,
    IN ULONG        IoControlCode
)
{
    NTSTATUS            status = STATUS_SUCCESS;
    WDFDEVICE           device;
    PDEVICE_CONTEXT     devContext;

    UNREFERENCED_PARAMETER(OutputBufferLength);
    UNREFERENCED_PARAMETER(InputBufferLength);

    device = WdfIoQueueGetDevice(Queue);
    devContext = GetDeviceContext(device);

    switch (IoControlCode)
    {
    default:
        status = STATUS_NOT_SUPPORTED;
        break;
    }

    WdfRequestComplete(Request, status);

    return;
}