/*++

Copyright (c) Microsoft Corporation. All rights reserved.

Module Name:

	wdf.c

Abstract:
	This module implements WDF and WDM functionality required to register as a
	device driver, instantiate devices, and register those devices with the
	battery class driver.

	N.B. This code is provided "AS IS" without any expressed or implied warranty.

--*/

//--------------------------------------------------------------------- Includes

#include "..\inc\SM5714Battery.h"
#include "..\inc\SM5714BatteryInterface.h"
#include "wdf.tmh"
#include <initguid.h>
#include <acpiioct.h>
#include <wdm.h>

DEFINE_GUID(GUID_DEVINTERFACE_SM5714_BATTERY,
	0x08e8729c, 0x2c8f, 0x49e2, 0xa4, 0xc2, 0xf3, 0xbb, 0x81, 0x47, 0x4b, 0xd0);

//------------------------------------------------------------------- Prototypes

DRIVER_INITIALIZE DriverEntry;
EVT_WDF_DRIVER_DEVICE_ADD SM5714BatteryDriverDeviceAdd;
EVT_WDF_DEVICE_SELF_MANAGED_IO_INIT  SM5714BatterySelfManagedIoInit;
EVT_WDF_DEVICE_SELF_MANAGED_IO_CLEANUP  SM5714BatterySelfManagedIoCleanup;
EVT_WDF_DEVICE_QUERY_STOP SM5714BatteryQueryStop;
EVT_WDF_DEVICE_PREPARE_HARDWARE SM5714BatteryDevicePrepareHardware;
EVT_WDFDEVICE_WDM_IRP_PREPROCESS SM5714BatteryWdmIrpPreprocessDeviceControl;
EVT_WDFDEVICE_WDM_IRP_PREPROCESS SM5714BatteryWdmIrpPreprocessSystemControl;
WMI_QUERY_REGINFO_CALLBACK SM5714BatteryQueryWmiRegInfo;
WMI_QUERY_DATABLOCK_CALLBACK SM5714BatteryQueryWmiDataBlock;
EVT_WDF_DRIVER_UNLOAD SM5714BatteryEvtDriverUnload;
EVT_WDF_OBJECT_CONTEXT_CLEANUP SM5714BatteryEvtDriverContextCleanup;

//---------------------------------------------------------------------- Pragmas

#pragma alloc_text(INIT, DriverEntry)
#pragma alloc_text(PAGE, SM5714BatterySelfManagedIoInit)
#pragma alloc_text(PAGE, SM5714BatterySelfManagedIoCleanup)
#pragma alloc_text(PAGE, SM5714BatteryQueryStop)
#pragma alloc_text(PAGE, SM5714BatteryDriverDeviceAdd)
#pragma alloc_text(PAGE, SM5714BatteryDevicePrepareHardware)
#pragma alloc_text(PAGE, SM5714BatteryWdmIrpPreprocessDeviceControl)
#pragma alloc_text(PAGE, SM5714BatteryWdmIrpPreprocessSystemControl)
#pragma alloc_text(PAGE, SM5714BatteryQueryWmiRegInfo)
#pragma alloc_text(PAGE, SM5714BatteryQueryWmiDataBlock)
#pragma alloc_text(PAGE, SM5714BatteryEvtDriverUnload)
#pragma alloc_text(PAGE, SM5714BatteryEvtDriverContextCleanup)

//-------------------------------------------------------------------- Functions

#define GET_INTEGER(_arg_)  (*(PULONG UNALIGNED) ((_arg_)->Data))

NTSTATUS
Sm5714FetchCapacities(
	_In_  WDFDEVICE  Device,
	_Out_ PULONG     DesignedCap_mWh,
	_Out_ PULONG     FullCap_mWh,
	_Out_opt_ PUCHAR BatteryTech,
	_Out_opt_ PULONG DesignVoltage_mV
)
{
	// Build the ACPI request
	ACPI_EVAL_INPUT_BUFFER input = { 0 };
	input.Signature = ACPI_EVAL_INPUT_BUFFER_SIGNATURE;
	RtlCopyMemory(input.MethodName, "BATT", 4);

	ULONG const outLen = sizeof(ACPI_EVAL_OUTPUT_BUFFER) +
		8 * sizeof(ACPI_METHOD_ARGUMENT);
	PACPI_EVAL_OUTPUT_BUFFER output =
		(PACPI_EVAL_OUTPUT_BUFFER)ExAllocatePoolZero(
			NonPagedPoolNx, outLen, 'ttaB');
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
		&bytesReturned
	);

	// Parse four integers
	enum { DESIGN = 0, FULL = 1, TECH = 2, VOLT = 3 };

	if (NT_SUCCESS(status) &&
		output->Signature == ACPI_EVAL_OUTPUT_BUFFER_SIGNATURE &&
		output->Count >= 2 &&
		output->Argument[DESIGN].Type == ACPI_METHOD_ARGUMENT_INTEGER &&
		output->Argument[FULL].Type == ACPI_METHOD_ARGUMENT_INTEGER)
	{
		*DesignedCap_mWh = GET_INTEGER(&output->Argument[DESIGN]);
		*FullCap_mWh = GET_INTEGER(&output->Argument[FULL]);

		if (BatteryTech &&
			output->Count > TECH &&
			output->Argument[TECH].Type == ACPI_METHOD_ARGUMENT_INTEGER)
		{
			*BatteryTech = (UCHAR)GET_INTEGER(&output->Argument[TECH]);
		}

		if (DesignVoltage_mV &&
			output->Count > VOLT &&
			output->Argument[VOLT].Type == ACPI_METHOD_ARGUMENT_INTEGER)
		{
			*DesignVoltage_mV = GET_INTEGER(&output->Argument[VOLT]);
		}
	}
	else
	{
		status = STATUS_ACPI_INVALID_DATA;
	}

	ExFreePool(output);
	return status;
}

_Use_decl_annotations_
NTSTATUS
DriverEntry(
	PDRIVER_OBJECT DriverObject,
	PUNICODE_STRING RegistryPath
)

/*++

Routine Description:

	DriverEntry initializes the driver and is the first routine called by the
	system after the driver is loaded. DriverEntry configures and creates a WDF
	driver object.

Parameters Description:

	DriverObject - Supplies a pointer to the driver object.

	RegistryPath - Supplies a pointer to a unicode string representing the path
		to the driver-specific key in the registry.

Return Value:

	NTSTATUS.

--*/

{

	WDF_OBJECT_ATTRIBUTES DriverAttributes;
	WDF_DRIVER_CONFIG DriverConfig;
	PSM5714_BATTERY_GLOBAL_DATA GlobalData;
	NTSTATUS Status;

	//
	// Initialize WPP Tracing
	//
	WPP_INIT_TRACING(DriverObject, RegistryPath);

	Trace(TRACE_LEVEL_INFORMATION, SM5714_BATTERY_TRACE, "Entering %!FUNC!\n");

	WDF_DRIVER_CONFIG_INIT(&DriverConfig, SM5714BatteryDriverDeviceAdd);
	DriverConfig.EvtDriverUnload = SM5714BatteryEvtDriverUnload;
	DriverConfig.DriverPoolTag = SM5714_BATTERY_TAG;

	//
	// Initialize attributes and a context area for the driver object.
	//
	// N.B. ExecutionLevel is set to Passive because this driver expect callback
	//      functions to be called at PASSIVE_LEVEL.
	//
	// N.B. SynchronizationScope is not specified and therefore it is set to
	//      None. This means that the WDF framework does not synchronize the
	//      callbacks, you may want to set this to a different value based on
	//      how the callbacks are required to be synchronized in your driver.
	//

	WDF_OBJECT_ATTRIBUTES_INIT(&DriverAttributes);
	WDF_OBJECT_ATTRIBUTES_SET_CONTEXT_TYPE(&DriverAttributes,
		SM5714_BATTERY_GLOBAL_DATA);

	DriverAttributes.ExecutionLevel = WdfExecutionLevelPassive;
	DriverAttributes.EvtCleanupCallback = SM5714BatteryEvtDriverContextCleanup;

	//
	// Create the driver object
	//

	Status = WdfDriverCreate(DriverObject, RegistryPath, &DriverAttributes, &DriverConfig, WDF_NO_HANDLE);

	if (!NT_SUCCESS(Status)) {
		Trace(TRACE_LEVEL_INFORMATION, SM5714_BATTERY_ERROR, "WdfDriverCreate() Failed. Status 0x%x\n", Status);
		goto DriverEntryEnd;
	}

	GlobalData = GetGlobalData(WdfGetDriver());
	GlobalData->RegistryPath.MaximumLength = RegistryPath->Length + sizeof(UNICODE_NULL);

	GlobalData->RegistryPath.Length = RegistryPath->Length;
	GlobalData->RegistryPath.Buffer = WdfDriverGetRegistryPath(WdfGetDriver());

DriverEntryEnd:
	Trace(TRACE_LEVEL_INFORMATION, SM5714_BATTERY_TRACE, "Leaving %!FUNC!: Status = 0x%08lX\n", Status);
	return Status;
}

_Use_decl_annotations_
NTSTATUS
SM5714BatteryDriverDeviceAdd(
	WDFDRIVER Driver,
	PWDFDEVICE_INIT DeviceInit
)

/*++
Routine Description:

	EvtDriverDeviceAdd is called by the framework in response to AddDevice
	call from the PnP manager. A WDF device object is created and initialized to
	represent a new instance of the battery device.

Arguments:

	Driver - Supplies a handle to the WDF Driver object.

	DeviceInit - Supplies a pointer to a framework-allocated WDFDEVICE_INIT
		structure.

Return Value:

	NTSTATUS

--*/

{

	WDF_OBJECT_ATTRIBUTES DeviceAttributes;
	PSM5714_BATTERY_FDO_DATA DevExt;
	WDFDEVICE DeviceHandle;
	WDF_OBJECT_ATTRIBUTES LockAttributes;
	WDF_PNPPOWER_EVENT_CALLBACKS PnpPowerCallbacks;
	NTSTATUS Status;

	UNREFERENCED_PARAMETER(Driver);

	Trace(TRACE_LEVEL_INFORMATION, SM5714_BATTERY_TRACE, "Entering %!FUNC!\n");
	PAGED_CODE();

	//
	// Initialize the PnpPowerCallbacks structure.  Callback events for PNP
	// and Power are specified here.
	//

	WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&PnpPowerCallbacks);
	PnpPowerCallbacks.EvtDevicePrepareHardware = SM5714BatteryDevicePrepareHardware;
	PnpPowerCallbacks.EvtDeviceSelfManagedIoInit = SM5714BatterySelfManagedIoInit;
	PnpPowerCallbacks.EvtDeviceSelfManagedIoCleanup = SM5714BatterySelfManagedIoCleanup;
	PnpPowerCallbacks.EvtDeviceQueryStop = SM5714BatteryQueryStop;
	WdfDeviceInitSetPnpPowerEventCallbacks(DeviceInit, &PnpPowerCallbacks);

	//
	// Register WDM preprocess callbacks for IRP_MJ_DEVICE_CONTROL and
	// IRP_MJ_SYSTEM_CONTROL. The battery class driver needs to handle these IO
	// requests directly.
	//

	Status = WdfDeviceInitAssignWdmIrpPreprocessCallback(
		DeviceInit,
		SM5714BatteryWdmIrpPreprocessDeviceControl,
		IRP_MJ_DEVICE_CONTROL,
		NULL,
		0);

	if (!NT_SUCCESS(Status)) {
		Trace(TRACE_LEVEL_INFORMATION, SM5714_BATTERY_ERROR, "WdfDeviceInitAssignWdmIrpPreprocessCallback" "(IRP_MJ_DEVICE_CONTROL) Failed. 0x%x\n", Status);
		goto DriverDeviceAddEnd;
	}

	Status = WdfDeviceInitAssignWdmIrpPreprocessCallback(
		DeviceInit,
		SM5714BatteryWdmIrpPreprocessSystemControl,
		IRP_MJ_SYSTEM_CONTROL,
		NULL,
		0);

	if (!NT_SUCCESS(Status)) {
		Trace(TRACE_LEVEL_INFORMATION, SM5714_BATTERY_ERROR, "WdfDeviceInitAssignWdmIrpPreprocessCallback" "(IRP_MJ_SYSTEM_CONTROL) Failed. 0x%x\n", Status);
		goto DriverDeviceAddEnd;
	}

	//
	// Initialize attributes and a context area for the device object.
	//

	WDF_OBJECT_ATTRIBUTES_INIT(&DeviceAttributes);
	WDF_OBJECT_ATTRIBUTES_SET_CONTEXT_TYPE(&DeviceAttributes, SM5714_BATTERY_FDO_DATA);

	//
	// Create a framework device object.  This call will in turn create
	// a WDM device object, attach to the lower stack, and set the
	// appropriate flags and attributes.
	//

	Status = WdfDeviceCreate(&DeviceInit, &DeviceAttributes, &DeviceHandle);
	if (!NT_SUCCESS(Status)) {
		Trace(TRACE_LEVEL_INFORMATION, SM5714_BATTERY_ERROR, "WdfDeviceCreate() Failed. 0x%x\n", Status);
		goto DriverDeviceAddEnd;
	}

	//
	// Finish initializing the device context area.
	//

	DevExt = GetDeviceExtension(DeviceHandle);
	DevExt->Device = DeviceHandle;
	DevExt->BatteryTag = BATTERY_TAG_INVALID;
	DevExt->ClassHandle = NULL;
	DevExt->ExternalPowerOnline = FALSE;

	Status = WdfDeviceCreateDeviceInterface(
		DeviceHandle,
		&GUID_DEVINTERFACE_SM5714_BATTERY,
		NULL);
	if (!NT_SUCCESS(Status)) {
		Trace(TRACE_LEVEL_INFORMATION, SM5714_BATTERY_ERROR,
			"WdfDeviceCreateDeviceInterface() Failed. 0x%x\n", Status);
		goto DriverDeviceAddEnd;
	}

	WDF_OBJECT_ATTRIBUTES_INIT(&LockAttributes);
	LockAttributes.ParentObject = DeviceHandle;
	Status = WdfWaitLockCreate(&LockAttributes, &DevExt->ClassInitLock);

	if (!NT_SUCCESS(Status)) {
		Trace(TRACE_LEVEL_INFORMATION, SM5714_BATTERY_ERROR, "WdfWaitLockCreate(ClassInitLock) Failed. Status 0x%x\n", Status);
		goto DriverDeviceAddEnd;
	}

	WDF_OBJECT_ATTRIBUTES_INIT(&LockAttributes);
	LockAttributes.ParentObject = DeviceHandle;
	Status = WdfWaitLockCreate(&LockAttributes, &DevExt->StateLock);

	if (!NT_SUCCESS(Status)) {
		Trace(TRACE_LEVEL_INFORMATION, SM5714_BATTERY_ERROR, "WdfWaitLockCreate(StateLock) Failed. Status 0x%x\n", Status);
		goto DriverDeviceAddEnd;
	}

DriverDeviceAddEnd:
	Trace(TRACE_LEVEL_INFORMATION, SM5714_BATTERY_TRACE, "Leaving %!FUNC!: Status = 0x%08lX\n", Status);
	return Status;
}

_Use_decl_annotations_
NTSTATUS
SM5714BatterySelfManagedIoInit(
	WDFDEVICE Device
)

/*++

Routine Description:

	The framework calls this function once per device after EvtDeviceD0Entry
	callback has been called for the first time. This function is not called
	again unless device is remove and reconnected or the drivers are reloaded.

Arguments:

	Device - Supplies a handle to a framework device object.

Return Value:

	NTSTATUS

--*/

{

	BATTERY_MINIPORT_INFO_V1_1 BattInit;
	PSM5714_BATTERY_FDO_DATA DevExt;
	PDEVICE_OBJECT DeviceObject;
	NTSTATUS Status;

	Trace(TRACE_LEVEL_INFORMATION, SM5714_BATTERY_TRACE, "Entering %!FUNC!\n");
	PAGED_CODE();

	DevExt = GetDeviceExtension(Device);

	//
	// Attach to the battery class driver.
	//

	RtlZeroMemory(&BattInit, sizeof(BattInit));
	BattInit.MajorVersion = BATTERY_CLASS_MAJOR_VERSION;
	BattInit.MinorVersion = BATTERY_CLASS_MINOR_VERSION_1;
	BattInit.Context = DevExt;
	BattInit.QueryTag = SM5714BatteryQueryTag;
	BattInit.QueryInformation = SM5714BatteryQueryInformation;
	BattInit.SetInformation = SM5714BatterySetInformation;
	BattInit.QueryStatus = SM5714BatteryQueryStatus;
	BattInit.SetStatusNotify = SM5714BatterySetStatusNotify;
	BattInit.DisableStatusNotify = SM5714BatteryDisableStatusNotify;
	BattInit.Pdo = WdfDeviceWdmGetPhysicalDevice(Device);
	BattInit.DeviceName = NULL;
	BattInit.Fdo = WdfDeviceWdmGetDeviceObject(Device);
	WdfWaitLockAcquire(DevExt->ClassInitLock, NULL);
	Status = BatteryClassInitializeDevice((PBATTERY_MINIPORT_INFO)&BattInit, &DevExt->ClassHandle);

	WdfWaitLockRelease(DevExt->ClassInitLock);
	if (!NT_SUCCESS(Status)) {
		goto DevicePrepareHardwareEnd;
	}

	//
	// Register the device as a WMI data provider. This is done using WDM
	// methods because the battery class driver uses WDM methods to complete
	// WMI requests.
	//

	DevExt->WmiLibContext.GuidCount = 0;
	DevExt->WmiLibContext.GuidList = NULL;
	DevExt->WmiLibContext.QueryWmiRegInfo = SM5714BatteryQueryWmiRegInfo;
	DevExt->WmiLibContext.QueryWmiDataBlock = SM5714BatteryQueryWmiDataBlock;
	DevExt->WmiLibContext.SetWmiDataBlock = NULL;
	DevExt->WmiLibContext.SetWmiDataItem = NULL;
	DevExt->WmiLibContext.ExecuteWmiMethod = NULL;
	DevExt->WmiLibContext.WmiFunctionControl = NULL;
	DeviceObject = WdfDeviceWdmGetDeviceObject(Device);
	Status = IoWMIRegistrationControl(DeviceObject, WMIREG_ACTION_REGISTER);

	//
	// Failure to register with WMI is nonfatal.
	//

	if (!NT_SUCCESS(Status)) {
		Trace(TRACE_LEVEL_INFORMATION, SM5714_BATTERY_WARN, "IoWMIRegistrationControl() Failed. Status 0x%x\n", Status);
		Status = STATUS_SUCCESS;
	}

DevicePrepareHardwareEnd:
	Trace(TRACE_LEVEL_INFORMATION, SM5714_BATTERY_TRACE, "Leaving %!FUNC!: Status = 0x%08lX\n", Status);
	return Status;
}

_Use_decl_annotations_
VOID
SM5714BatterySelfManagedIoCleanup(
	WDFDEVICE Device
)

/*++

Routine Description:

	This function is called after EvtDeviceSelfManagedIoSuspend callback. This
	function must release any sel-managed I/O operation data.

Arguments:

	Device - Supplies a handle to a framework device object.

Return Value:

	NTSTATUS - Failures will be logged, but not acted on.

--*/

{

	PSM5714_BATTERY_FDO_DATA DevExt;
	PDEVICE_OBJECT DeviceObject;
	NTSTATUS Status;

	Trace(TRACE_LEVEL_INFORMATION, SM5714_BATTERY_TRACE, "Entering %!FUNC!\n");
	PAGED_CODE();

	DeviceObject = WdfDeviceWdmGetDeviceObject(Device);
	Status = IoWMIRegistrationControl(DeviceObject, WMIREG_ACTION_DEREGISTER);
	if (!NT_SUCCESS(Status)) {
		Trace(TRACE_LEVEL_INFORMATION, SM5714_BATTERY_WARN, "IoWMIRegistrationControl() Failed. Status 0x%x\n", Status);
		Status = STATUS_SUCCESS;
	}

	DevExt = GetDeviceExtension(Device);

	WdfWaitLockAcquire(DevExt->ClassInitLock, NULL);
	if (DevExt->ClassHandle != NULL) {
		Status = BatteryClassUnload(DevExt->ClassHandle);
		DevExt->ClassHandle = NULL;
	}

	WdfWaitLockRelease(DevExt->ClassInitLock);
	Trace(TRACE_LEVEL_INFORMATION, SM5714_BATTERY_TRACE, "Leaving %!FUNC!: Status = 0x%08lX\n", Status);
	return;
}

_Use_decl_annotations_
NTSTATUS
SM5714BatteryQueryStop(
	_In_ WDFDEVICE Device
)

/*++

Routine Description:

	EvtDeviceQueryStop event callback function determines whether a specified
	device can be stopped so that the PnP manager can redistribute system
	hardware resources.

	SM5714Battery is designed to fail a rebalance operation, for reasons described
	below. Note however that this approach must *not* be adopted by an actual
	battery driver.

	SM5714Battery unregisters itself as a Battery driver by calling
	BatteryClassUnload() when IRP_MN_STOP_DEVICE arrives at the driver. It
	re-establishes itself as a Battery driver on arrival of IRP_MN_START_DEVICE.
	This results in any IOCTLs normally handeled by the Battery Class driver to
	be delivered to SM5714Battery. The IO Queue employed by SM5714Battery is power managed,
	it causes these IOCTLs to be pended when SM5714Battery is not in D0. Now if the
	device attempts to do a shutdown while an IOCTL is pended in SM5714Battery, it
	would result in a 0x9F bugcheck. By opting out of PNP rebalance operation
	SM5714Battery circumvents this issue.

Arguments:

	Device - Supplies a handle to a framework device object.

Return Value:

	NTSTATUS

--*/

{

	UNREFERENCED_PARAMETER(Device);

	return STATUS_UNSUCCESSFUL;
}



_Use_decl_annotations_
NTSTATUS
SM5714BatteryDevicePrepareHardware(
	WDFDEVICE Device,
	WDFCMRESLIST ResourcesRaw,
	WDFCMRESLIST ResourcesTranslated
)

/*++

Routine Description:

	EvtDevicePrepareHardware event callback performs operations that are
	necessary to make the driver's device operational. The framework calls the
	driver's EvtDevicePrepareHardware callback when the PnP manager sends an
	IRP_MN_START_DEVICE request to the driver stack.

Arguments:

	Device - Supplies a handle to a framework device object.

	ResourcesRaw - Supplies a handle to a collection of framework resource
		objects. This collection identifies the raw (bus-relative) hardware
		resources that have been assigned to the device.

	ResourcesTranslated - Supplies a handle to a collection of framework
		resource objects. This collection identifies the translated
		(system-physical) hardware resources that have been assigned to the
		device. The resources appear from the CPU's point of view. Use this list
		of resources to map I/O space and device-accessible memory into virtual
		address space

Return Value:

	NTSTATUS

--*/

{
	NTSTATUS status = STATUS_INSUFFICIENT_RESOURCES;
	PCM_PARTIAL_RESOURCE_DESCRIPTOR res, resRaw;
	ULONG resourceCount;
	ULONG i;

	Trace(TRACE_LEVEL_INFORMATION, SM5714_BATTERY_TRACE, "Entering %!FUNC!\n");
	PAGED_CODE();

	PSM5714_BATTERY_FDO_DATA devContext = GetDeviceExtension(Device);

	devContext->Device = Device;

	//
	// Get the resouce hub connection ID for our I2C driver
	//
	resourceCount = WdfCmResourceListGetCount(ResourcesTranslated);

	for (i = 0; i < resourceCount; i++)
	{
		res = WdfCmResourceListGetDescriptor(ResourcesTranslated, i);
		resRaw = WdfCmResourceListGetDescriptor(ResourcesRaw, i);

		if (res->Type == CmResourceTypeConnection &&
			res->u.Connection.Class == CM_RESOURCE_CONNECTION_CLASS_SERIAL &&
			res->u.Connection.Type == CM_RESOURCE_CONNECTION_TYPE_SERIAL_I2C)
		{
			devContext->I2CContext.I2cResHubId.LowPart =
				res->u.Connection.IdLowPart;
			devContext->I2CContext.I2cResHubId.HighPart =
				res->u.Connection.IdHighPart;

			status = STATUS_SUCCESS;
		}
	}

	if (!NT_SUCCESS(status))
	{
		Trace(TRACE_LEVEL_ERROR, SM5714_BATTERY_ERROR, "Error finding CmResourceTypeConnection resource - %!STATUS!", status);
		goto exit;
	}

	//
	// Initialize Spb so the driver can issue reads/writes
	//
	status = SpbTargetInitialize(Device, &devContext->I2CContext);

	if (!NT_SUCCESS(status))
	{
		Trace(TRACE_LEVEL_ERROR, SM5714_BATTERY_ERROR, "Error in Spb initialization - %!STATUS!", status);
		goto exit;
	}

	status = Sm5714FetchCapacities(Device,
		&devContext->DesignedCapacity_mWh,
		&devContext->FullChargedCapacity_mWh,
		&devContext->BatteryTechnology,
		&devContext->DesignVoltage_mV);

	if (!NT_SUCCESS(status)) {
		Trace(TRACE_LEVEL_ERROR, SM5714_BATTERY_ERROR,
			"Sm5714FetchCapacities failed (0x%08X)\n",
			status);

		return status;
	}

	SM5714BatteryPrepareHardware(Device);

exit:
	Trace(TRACE_LEVEL_INFORMATION, SM5714_BATTERY_TRACE, "Leaving %!FUNC!: Status = 0x%08lX\n", status);
	return status;
}

_Use_decl_annotations_
NTSTATUS
SM5714BatteryWdmIrpPreprocessDeviceControl(
	WDFDEVICE Device,
	PIRP Irp
)

/*++

Routine Description:

	This event is called when the framework receives IRP_MJ_DEVICE_CONTROL
	requests from the system.

	N.B. Battery stack requires the device IOCTLs be sent to it at
		 PASSIVE_LEVEL only, any IOCTL comming from user mode is therefore
		 fine, kernel components, such as filter drivers sitting on top of
		 the battery drivers should be careful to not voilate this
		 requirement.

Arguments:

	Device - Supplies a handle to a framework device object.

	Irp - Supplies the IO request being processed.

Return Value:

	NTSTATUS

--*/

{

	PSM5714_BATTERY_FDO_DATA DevExt;
	NTSTATUS Status;

	PAGED_CODE();
	Trace(TRACE_LEVEL_INFORMATION, SM5714_BATTERY_TRACE, "Entering %!FUNC!\n");

	ASSERTMSG("Must be called at IRQL = PASSIVE_LEVEL", (KeGetCurrentIrql() == PASSIVE_LEVEL));

	DevExt = GetDeviceExtension(Device);
	Status = STATUS_NOT_SUPPORTED;

	{
		PIO_STACK_LOCATION IrpStack = IoGetCurrentIrpStackLocation(Irp);

		if (IrpStack->Parameters.DeviceIoControl.IoControlCode ==
			IOCTL_SM5714_BATTERY_SET_EXTERNAL_POWER)
		{
			PSM5714_BATTERY_POWER_REQUEST PowerRequest =
				(PSM5714_BATTERY_POWER_REQUEST)Irp->AssociatedIrp.SystemBuffer;
			BOOLEAN StateChanged;

			if (IrpStack->Parameters.DeviceIoControl.InputBufferLength <
				sizeof(*PowerRequest) || PowerRequest == NULL)
			{
				Status = STATUS_BUFFER_TOO_SMALL;
			}
			else if (PowerRequest->ExternalPowerOnline > 1)
			{
				Status = STATUS_INVALID_PARAMETER;
			}
			else
			{
				WdfWaitLockAcquire(DevExt->StateLock, NULL);
				StateChanged = DevExt->ExternalPowerOnline !=
					(BOOLEAN)PowerRequest->ExternalPowerOnline;
				DevExt->ExternalPowerOnline =
					(BOOLEAN)PowerRequest->ExternalPowerOnline;
				WdfWaitLockRelease(DevExt->StateLock);

				if (StateChanged)
				{
					WdfWaitLockAcquire(DevExt->ClassInitLock, NULL);
					if (DevExt->ClassHandle != NULL)
						BatteryClassStatusNotify(DevExt->ClassHandle);
					WdfWaitLockRelease(DevExt->ClassInitLock);
				}

				Status = STATUS_SUCCESS;
			}

			Irp->IoStatus.Status = Status;
			Irp->IoStatus.Information = 0;
			IoCompleteRequest(Irp, IO_NO_INCREMENT);
			return Status;
		}
	}

	//
	// Suppress 28118:Irq Exceeds Caller, see Routine Description for
	// explaination.
	//

#pragma warning(suppress: 28118)
	WdfWaitLockAcquire(DevExt->ClassInitLock, NULL);

	//
	// N.B. An attempt to queue the IRP with the port driver should happen
	//      before WDF assumes ownership of this IRP, i.e. before
	//      WdfDeviceWdmDispatchPreprocessedIrp is called, this is so that the
	//      Battery port driver, which is a WDM driver, may complete the IRP if
	//      it does endup procesing it.
	//

	if (DevExt->ClassHandle != NULL) {

		//
		// Suppress 28118:Irq Exceeds Caller, see above N.B.
		//

#pragma warning(suppress: 28118)
		Status = BatteryClassIoctl(DevExt->ClassHandle, Irp);
	}

	WdfWaitLockRelease(DevExt->ClassInitLock);
	if (Status == STATUS_NOT_SUPPORTED) {
		IoSkipCurrentIrpStackLocation(Irp);
		Status = WdfDeviceWdmDispatchPreprocessedIrp(Device, Irp);
	}

	Trace(TRACE_LEVEL_INFORMATION, SM5714_BATTERY_TRACE, "Leaving %!FUNC!: Status = 0x%08lX\n", Status);
	return Status;
}

_Use_decl_annotations_
NTSTATUS
SM5714BatteryWdmIrpPreprocessSystemControl(
	WDFDEVICE Device,
	PIRP Irp
)

/*++

Routine Description:

	This event is called when the framework receives IRP_MJ_SYSTEM_CONTROL
	requests from the system.

	N.B. Battery stack requires the device IOCTLs be sent to it at
		 PASSIVE_LEVEL only, any IOCTL comming from user mode is therefore
		 fine, kernel components, such as filter drivers sitting on top of
		 the battery drivers should be careful to not voilate this
		 requirement.

Arguments:

	Device - Supplies a handle to a framework device object.

	Irp - Supplies the IO request being processed.

Return Value:

	NTSTATUS

--*/

{

	PSM5714_BATTERY_FDO_DATA DevExt;
	PDEVICE_OBJECT DeviceObject;
	SYSCTL_IRP_DISPOSITION Disposition;
	NTSTATUS Status;

	Trace(TRACE_LEVEL_INFORMATION, SM5714_BATTERY_TRACE, "Entering %!FUNC!\n");
	PAGED_CODE();

	ASSERTMSG("Must be called at IRQL = PASSIVE_LEVEL", (KeGetCurrentIrql() == PASSIVE_LEVEL));

	Status = STATUS_NOT_IMPLEMENTED;
	DevExt = GetDeviceExtension(Device);
	Disposition = IrpForward;

	//
	// Acquire the class initialization lock and attempt to queue the IRP with
	// the class driver.
	//
	// Suppress 28118:Irq Exceeds Caller, see Routine Description for
	// explaination.
	//

#pragma warning(suppress: 28118)
	WdfWaitLockAcquire(DevExt->ClassInitLock, NULL);
	if (DevExt->ClassHandle != NULL) {
		DeviceObject = WdfDeviceWdmGetDeviceObject(Device);
		Status = BatteryClassSystemControl(DevExt->ClassHandle, &DevExt->WmiLibContext, DeviceObject, Irp, &Disposition);
	}

	WdfWaitLockRelease(DevExt->ClassInitLock);
	switch (Disposition) {
	case IrpProcessed:
		break;

	case IrpNotCompleted:
		IoCompleteRequest(Irp, IO_NO_INCREMENT);
		break;

	case IrpForward:
	case IrpNotWmi:
	default:
		IoSkipCurrentIrpStackLocation(Irp);
		Status = WdfDeviceWdmDispatchPreprocessedIrp(Device, Irp);
		break;
	}

	Trace(TRACE_LEVEL_INFORMATION, SM5714_BATTERY_TRACE, "Leaving %!FUNC!: Status = 0x%08lX\n", Status);
	return Status;
}

_Use_decl_annotations_
NTSTATUS
SM5714BatteryQueryWmiRegInfo(
	PDEVICE_OBJECT DeviceObject,
	PULONG RegFlags,
	PUNICODE_STRING InstanceName,
	PUNICODE_STRING* RegistryPath,
	PUNICODE_STRING MofResourceName,
	PDEVICE_OBJECT* Pdo
)

/*++

Routine Description:

	This routine is a callback into the driver to retrieve the list of
	guids or data blocks that the driver wants to register with WMI. This
	routine may not pend or block. Driver should NOT call
	WmiCompleteRequest.

Arguments:

	DeviceObject - Supplies the device whose data block is being queried.

	RegFlags - Supplies a pointer to return a set of flags that describe the
		guids being registered for this device. If the device wants enable and
		disable collection callbacks before receiving queries for the registered
		guids then it should return the WMIREG_FLAG_EXPENSIVE flag. Also the
		returned flags may specify WMIREG_FLAG_INSTANCE_PDO in which case
		the instance name is determined from the PDO associated with the
		device object. Note that the PDO must have an associated devnode. If
		WMIREG_FLAG_INSTANCE_PDO is not set then Name must return a unique
		name for the device.

	InstanceName - Supplies a pointer to return the instance name for the guids
		if WMIREG_FLAG_INSTANCE_PDO is not set in the returned *RegFlags. The
		caller will call ExFreePool with the buffer returned.

	RegistryPath - Supplies a pointer to return the registry path of the driver.

	MofResourceName - Supplies a pointer to return the name of the MOF resource
		attached to the binary file. If the driver does not have a mof resource
		attached then this can be returned as NULL.

	Pdo - Supplies a pointer to return the device object for the PDO associated
		with this device if the WMIREG_FLAG_INSTANCE_PDO flag is returned in
		*RegFlags.

Return Value:

	NTSTATUS

--*/

{

	WDFDEVICE Device;
	PSM5714_BATTERY_GLOBAL_DATA GlobalData;
	NTSTATUS Status;

	UNREFERENCED_PARAMETER(MofResourceName);
	UNREFERENCED_PARAMETER(InstanceName);

	Trace(TRACE_LEVEL_INFORMATION, SM5714_BATTERY_TRACE, "Entering %!FUNC!\n");
	PAGED_CODE();

	Device = WdfWdmDeviceGetWdfDeviceHandle(DeviceObject);
	GlobalData = GetGlobalData(WdfGetDriver());
	*RegFlags = WMIREG_FLAG_INSTANCE_PDO;
	*RegistryPath = &GlobalData->RegistryPath;
	*Pdo = WdfDeviceWdmGetPhysicalDevice(Device);
	Status = STATUS_SUCCESS;
	Trace(TRACE_LEVEL_INFORMATION, SM5714_BATTERY_TRACE, "Leaving %!FUNC!: Status = 0x%08lX\n", Status);
	return Status;
}

_Use_decl_annotations_
NTSTATUS
SM5714BatteryQueryWmiDataBlock(
	PDEVICE_OBJECT DeviceObject,
	PIRP Irp,
	ULONG GuidIndex,
	ULONG InstanceIndex,
	ULONG InstanceCount,
	PULONG InstanceLengthArray,
	ULONG BufferAvail,
	PUCHAR Buffer
)

/*++

Routine Description:

	This routine is a callback into the driver to query for the contents of
	a data block. When the driver has finished filling the data block it
	must call WmiCompleteRequest to complete the irp. The driver can
	return STATUS_PENDING if the irp cannot be completed immediately.

Arguments:

	DeviceObject - Supplies the device whose data block is being queried.

	Irp - Supplies the Irp that makes this request.

	GuidIndex - Supplies the index into the list of guids provided when the
		device registered.

	InstanceIndex - Supplies the index that denotes which instance of the data
		block is being queried.

	InstanceCount - Supplies the number of instances expected to be returned for
		the data block.

	InstanceLengthArray - Supplies a pointer to an array of ULONG that returns
		the lengths of each instance of the data block. If this is NULL then
		there was not enough space in the output buffer to fulfill the request
		so the irp should be completed with the buffer needed.

	BufferAvail - Supplies the maximum size available to write the data
		block.

	Buffer - Supplies a pointer to a buffer to return the data block.


Return Value:

	NTSTATUS

--*/

{

	PSM5714_BATTERY_FDO_DATA DevExt;
	WDFDEVICE Device;
	NTSTATUS Status;

	UNREFERENCED_PARAMETER(InstanceIndex);
	UNREFERENCED_PARAMETER(InstanceCount);

	Trace(TRACE_LEVEL_INFORMATION, SM5714_BATTERY_TRACE, "Entering %!FUNC!\n");
	PAGED_CODE();

	ASSERT((InstanceIndex == 0) && (InstanceCount == 1));

	if (InstanceLengthArray == NULL) {
		Status = STATUS_BUFFER_TOO_SMALL;
		goto SM5714BatteryQueryWmiDataBlockEnd;
	}

	Device = WdfWdmDeviceGetWdfDeviceHandle(DeviceObject);
	DevExt = GetDeviceExtension(Device);

	//
	// The class driver guarantees that all outstanding IO requests will be
	// completed before it finishes unregistering. As a result, the class
	// initialization lock does not need to be acquired in this callback, since
	// it is called during class driver processing of a WMI IRP.
	//

	Status = BatteryClassQueryWmiDataBlock(DevExt->ClassHandle,
		DeviceObject,
		Irp,
		GuidIndex,
		InstanceLengthArray,
		BufferAvail,
		Buffer);

	if (Status == STATUS_WMI_GUID_NOT_FOUND) {
		Status = WmiCompleteRequest(DeviceObject, Irp, STATUS_WMI_GUID_NOT_FOUND, 0, IO_NO_INCREMENT);
	}

SM5714BatteryQueryWmiDataBlockEnd:
	Trace(TRACE_LEVEL_INFORMATION, SM5714_BATTERY_TRACE, "Leaving %!FUNC!: Status = 0x%08lX\n", Status);
	return Status;
}


VOID
SM5714BatteryEvtDriverContextCleanup(
	_In_ WDFOBJECT DriverObject
)
/*++
Routine Description:

	Free all the resources allocated in DriverEntry.

Arguments:

	DriverObject - handle to a WDF Driver object.

Return Value:

	VOID.

--*/
{
	UNREFERENCED_PARAMETER(DriverObject);

	PAGED_CODE();

	Trace(TRACE_LEVEL_INFORMATION, SM5714_BATTERY_INFO, "%!FUNC! Entry");

	//
	// Stop WPP Tracing
	//
	WPP_CLEANUP(NULL);
}

VOID
SM5714BatteryEvtDriverUnload(
	IN WDFDRIVER Driver
)
/*++
Routine Description:

	Free all the resources allocated in DriverEntry.

Arguments:

	Driver - handle to a WDF Driver object.

Return Value:

	VOID.

--*/
{
	PAGED_CODE();

	//
	// Stop WPP Tracing
	//
	WPP_CLEANUP(WdfDriverWdmGetDriverObject(Driver));

	return;
}