#include "..\Common\registers.h"
#include "..\Common\spbhelper.h"
#include "..\Common\ps5169interface.h"
#include "..\Charger\charger.h"
#include "typec.h"

#include <acpiioct.h>

static ULONG DebugLevel = 100;
static ULONG DebugCategories = DBG_INIT | DBG_PNP | DBG_IOCTL;

#define PD_HEADER_MESSAGE_TYPE_MASK      0x001F
#define PD_HEADER_MESSAGE_ID_SHIFT       9
#define PD_HEADER_NUM_OBJECTS_SHIFT      12
#define PD_HEADER_FIELD_MASK             0x0007
#define PD_MAX_DATA_OBJECTS              7
#define PD_DATA_OBJECT_SIZE              4

#define DP_STATE_IDLE                    0
#define DP_STATE_DISCOVERED              1
#define DP_STATE_ENTERED                 2
#define DP_STATE_CONFIGURED              3

#define DP_MUX_4LANE                     0
#define DP_MUX_MULTI_FUNCTION            1
#define DP_MUX_USB_ONLY                  2

#define DP_STATUS_PORT_CONNECTED_MASK    0x00000003
#define DP_STATUS_MULTI_FUNCTION         (1UL << 4)
#define DP_STATUS_HPD_STATE              (1UL << 7)
#define DP_STATUS_IRQ_HPD                (1UL << 8)

#define DP_CONFIG_SELECT_MASK            0x03
#define DP_CONFIG_PIN_SHIFT              8
#define DP_CONFIG_PIN_MASK               0x3F

#define DP_HPD_ACPI_STATE                0x01
#define DP_HPD_ACPI_IRQ                  0x02

//
// Set SM5714 MUIC D+/D- analog switch to USB mode or OPEN.
//
// The MUIC sits between the USB connector and the SoC, controlling the
// physical D+/D- data lines. Without switching to USB mode, the data
// lines are disconnected and no USB communication can occur, even if
// the USB controller is in host mode with VBUS power supplied.
//
// Android equivalent: com_to_usb() / com_to_open() in sm5714-muic.c
//
static NTSTATUS muic_set_usb_path(_In_ PDEVICE_CONTEXT pDevice, _In_ BOOLEAN connect)
{
    if (pDevice->SpbContextCount < 3)
    {
        Print(DEBUG_LEVEL_INFO, DBG_IOCTL,
              "MUIC I2C not available (SpbCount=%lu), skipping D+/D- switch\n",
              pDevice->SpbContextCount);
        return STATUS_SUCCESS;
    }

    UCHAR mansw_val;
    if (connect)
    {
        // Route D+/D- to USB + enable manual mode
        mansw_val = MUIC_MANSW_USB | MUIC_MANSW_MANUAL_BIT;
    }
    else
    {
        // Disconnect D+/D- + enable manual mode
        mansw_val = MUIC_MANSW_OPEN | MUIC_MANSW_MANUAL_BIT;
    }

    NTSTATUS status = write_reg8(pDevice, SPB_MUIC_INDEX,
                                 SM5714_MUIC_REG_MANUAL_SW, mansw_val);
    if (!NT_SUCCESS(status))
    {
        Print(DEBUG_LEVEL_ERROR, DBG_IOCTL,
              "MUIC MANUAL_SW write failed (0x%02x) - 0x%x\n",
              mansw_val, status);
    }
    else
    {
        Print(DEBUG_LEVEL_INFO, DBG_IOCTL,
              "MUIC D+/D- %s (MANUAL_SW=0x%02x)\n",
              connect ? "connected" : "disconnected", mansw_val);
    }

    return status;
}

static BOOLEAN typec_is_audio_accessory(_In_ UCHAR attach_type)
{
    return attach_type == SM5714_ATTACH_AUDIO;
}

static BOOLEAN typec_is_debug_accessory(_In_ UCHAR attach_type)
{
    if (attach_type == SM5714_ATTACH_UN_ORI_DEBUG_SOURCE)
        return TRUE;

    if (attach_type == SM5714_ATTACH_ORI_DEBUG_SOURCE)
        return TRUE;

    return FALSE;
}

static BOOLEAN typec_is_source_like_attach(_In_ UCHAR attach_type)
{
    if (attach_type == SM5714_ATTACH_SOURCE)
        return TRUE;

    return typec_is_debug_accessory(attach_type);
}

static void typec_update_accessory_state(
    _In_ PDEVICE_CONTEXT pDevice,
    _In_ UCHAR attach_type
)
{
    pDevice->IsAudioAccessory = typec_is_audio_accessory(attach_type);
    pDevice->IsDebugAccessory = typec_is_debug_accessory(attach_type);
}

static void typec_reset_dp_state(_In_ PDEVICE_CONTEXT pDevice)
{
    pDevice->DpAltModeActive = FALSE;
    pDevice->DpConfigured = FALSE;
    pDevice->DpHpdState = FALSE;
    pDevice->DpIrqHpd = FALSE;
    pDevice->DpState = DP_STATE_IDLE;
    pDevice->DpSelectedPin = 0;
    pDevice->DpPartnerPinAssignments = 0;
    pDevice->DpMuxMode = DP_MUX_USB_ONLY;
    pDevice->PdMsgId = 0;
}

static void typec_set_disconnected_state(_In_ PDEVICE_CONTEXT pDevice)
{
    pDevice->IsAttached = FALSE;
    pDevice->AttachType = SM5714_ATTACH_NONE;
    pDevice->CcOrientation = 2;
    pDevice->VbusPresent = FALSE;
    pDevice->Bc12Type = 0;
    pDevice->RpCurrentAdvertised = 0;
    pDevice->IsAudioAccessory = FALSE;
    pDevice->IsDebugAccessory = FALSE;
    pDevice->SuperSpeedReady = FALSE;
    typec_reset_dp_state(pDevice);
}

static UCHAR typec_reported_usb_attach(_In_ UCHAR attach_type)
{
    if (typec_is_audio_accessory(attach_type))
        return SM5714_ATTACH_NONE;

    if (typec_is_debug_accessory(attach_type))
        return SM5714_ATTACH_SOURCE;

    return attach_type;
}

static NTSTATUS typec_read_block(
    _In_ PDEVICE_CONTEXT pDevice,
    _In_ UCHAR reg,
    _Out_writes_bytes_(length) PUCHAR buffer,
    _In_ USHORT length
)
{
    SPB_CONTEXT* spbCtx = &pDevice->SpbContexts[SPB_USBPD_INDEX];

    return SpbWriteRead(spbCtx, &reg, sizeof(reg), buffer, length, 0);
}

static USHORT typec_read_le16(_In_reads_(2) const UCHAR* value)
{
    return (USHORT)(value[0] | ((USHORT)value[1] << 8));
}

static ULONG typec_read_le32(_In_reads_(4) const UCHAR* value)
{
    return (ULONG)value[0] |
           ((ULONG)value[1] << 8) |
           ((ULONG)value[2] << 16) |
           ((ULONG)value[3] << 24);
}

static UCHAR typec_pd_header_message_type(_In_ USHORT header)
{
    return (UCHAR)(header & PD_HEADER_MESSAGE_TYPE_MASK);
}

static UCHAR typec_pd_header_message_id(_In_ USHORT header)
{
    return (UCHAR)((header >> PD_HEADER_MESSAGE_ID_SHIFT) & PD_HEADER_FIELD_MASK);
}

static UCHAR typec_pd_header_object_count(_In_ USHORT header)
{
    return (UCHAR)((header >> PD_HEADER_NUM_OBJECTS_SHIFT) & PD_HEADER_FIELD_MASK);
}

static UCHAR typec_dp_pin_to_number(_In_ UCHAR pin_mask)
{
    if (pin_mask & DP_PIN_ASSIGNMENT_A)
        return 1;

    if (pin_mask & DP_PIN_ASSIGNMENT_B)
        return 2;

    if (pin_mask & DP_PIN_ASSIGNMENT_C)
        return 3;

    if (pin_mask & DP_PIN_ASSIGNMENT_D)
        return 4;

    if (pin_mask & DP_PIN_ASSIGNMENT_E)
        return 5;

    if (pin_mask & DP_PIN_ASSIGNMENT_F)
        return 6;

    return 0;
}

static UCHAR typec_dp_select_pin(
    _In_ UCHAR pin_assignments,
    _In_ BOOLEAN prefer_multi_function
)
{
    if (prefer_multi_function)
    {
        if (pin_assignments & DP_PIN_ASSIGNMENT_D)
            return DP_PIN_ASSIGNMENT_D;

        if (pin_assignments & DP_PIN_ASSIGNMENT_B)
            return DP_PIN_ASSIGNMENT_B;

        if (pin_assignments & DP_PIN_ASSIGNMENT_F)
            return DP_PIN_ASSIGNMENT_F;
    }

    if (pin_assignments & DP_PIN_ASSIGNMENT_C)
        return DP_PIN_ASSIGNMENT_C;

    if (pin_assignments & DP_PIN_ASSIGNMENT_E)
        return DP_PIN_ASSIGNMENT_E;

    if (pin_assignments & DP_PIN_ASSIGNMENT_A)
        return DP_PIN_ASSIGNMENT_A;

    if (pin_assignments & DP_PIN_ASSIGNMENT_D)
        return DP_PIN_ASSIGNMENT_D;

    if (pin_assignments & DP_PIN_ASSIGNMENT_B)
        return DP_PIN_ASSIGNMENT_B;

    if (pin_assignments & DP_PIN_ASSIGNMENT_F)
        return DP_PIN_ASSIGNMENT_F;

    return 0;
}

static UCHAR typec_dp_mux_from_pin(_In_ UCHAR pin)
{
    if (pin == 2 || pin == 4 || pin == 6)
        return DP_MUX_MULTI_FUNCTION;

    if (pin == 1 || pin == 3 || pin == 5)
        return DP_MUX_4LANE;

    return DP_MUX_USB_ONLY;
}

static NTSTATUS typec_notify_dp_state(
    _In_ PDEVICE_CONTEXT pDevice,
    _In_ UCHAR mux,
    _In_ UCHAR pin,
    _In_ BOOLEAN hpd,
    _In_ BOOLEAN irq_hpd
)
{
    NTSTATUS status;
    UCHAR pin_arg = pin;
    UCHAR hpd_arg = 0;

    if (pin_arg != 0 && pDevice->CcOrientation == 1)
        pin_arg += 6;

    if (hpd)
        hpd_arg |= DP_HPD_ACPI_STATE;

    if (irq_hpd)
        hpd_arg |= DP_HPD_ACPI_IRQ;

    ULONG inputSize = sizeof(ACPI_EVAL_INPUT_BUFFER_COMPLEX) +
                      4 * sizeof(ACPI_METHOD_ARGUMENT);

    PACPI_EVAL_INPUT_BUFFER_COMPLEX pInput =
        (PACPI_EVAL_INPUT_BUFFER_COMPLEX)ExAllocatePoolZero(
            NonPagedPoolNx, inputSize, 'tspD');
    if (!pInput)
        return STATUS_INSUFFICIENT_RESOURCES;

    pInput->Signature = ACPI_EVAL_INPUT_BUFFER_COMPLEX_SIGNATURE;
    RtlCopyMemory(pInput->MethodName, "DPST", 4);
    pInput->Size = 4 * sizeof(ACPI_METHOD_ARGUMENT);
    pInput->ArgumentCount = 4;

    PACPI_METHOD_ARGUMENT pArg = &pInput->Argument[0];
    ACPI_METHOD_SET_ARGUMENT_INTEGER(pArg, (ULONG)mux);

    pArg = ACPI_METHOD_NEXT_ARGUMENT(pArg);
    ACPI_METHOD_SET_ARGUMENT_INTEGER(pArg, (ULONG)pin_arg);

    pArg = ACPI_METHOD_NEXT_ARGUMENT(pArg);
    ACPI_METHOD_SET_ARGUMENT_INTEGER(pArg, (ULONG)hpd_arg);

    pArg = ACPI_METHOD_NEXT_ARGUMENT(pArg);
    ACPI_METHOD_SET_ARGUMENT_INTEGER(pArg, (ULONG)pDevice->CcOrientation);

    WDF_MEMORY_DESCRIPTOR inDesc;
    WDF_MEMORY_DESCRIPTOR_INIT_BUFFER(&inDesc, pInput, inputSize);

    status = WdfIoTargetSendIoctlSynchronously(
        WdfDeviceGetIoTarget(pDevice->FxDevice),
        NULL,
        IOCTL_ACPI_EVAL_METHOD,
        &inDesc,
        NULL,
        NULL,
        NULL);

    if (!NT_SUCCESS(status))
    {
        Print(DEBUG_LEVEL_ERROR, DBG_IOCTL,
              "DPST ACPI method failed - 0x%x\n", status);
    }
    else
    {
        Print(DEBUG_LEVEL_INFO, DBG_IOCTL,
              "DP state notified: mux=%d pin=%d hpd=0x%02x cc=%d\n",
              mux, pin_arg, hpd_arg, pDevice->CcOrientation);
    }

    ExFreePool(pInput);
    return status;
}

static NTSTATUS typec_clear_dp_state(
    _In_ PDEVICE_CONTEXT pDevice,
    _In_ BOOLEAN notify
)
{
    typec_reset_dp_state(pDevice);

    if (!notify)
        return STATUS_SUCCESS;

    if (!pDevice->IsAttached)
        return STATUS_SUCCESS;

    if (pDevice->IsAudioAccessory)
        return STATUS_SUCCESS;

    return typec_notify_dp_state(pDevice, DP_MUX_USB_ONLY, 0, FALSE, FALSE);
}

static NTSTATUS typec_notify_current_dp_state(_In_ PDEVICE_CONTEXT pDevice)
{
    if (!pDevice->IsAttached)
        return STATUS_SUCCESS;

    if (pDevice->IsAudioAccessory)
        return STATUS_SUCCESS;

    return typec_notify_dp_state(
        pDevice,
        pDevice->DpMuxMode,
        pDevice->DpSelectedPin,
        pDevice->DpHpdState,
        pDevice->DpIrqHpd);
}

static NTSTATUS typec_process_dp_capabilities(
    _In_ PDEVICE_CONTEXT pDevice,
    _In_ ULONG capabilities_vdo
)
{
    UCHAR dfp_pins = (UCHAR)((capabilities_vdo >> 8) & DP_CONFIG_PIN_MASK);
    UCHAR ufp_pins = (UCHAR)((capabilities_vdo >> 16) & DP_CONFIG_PIN_MASK);

    pDevice->DpPartnerPinAssignments = ufp_pins;
    if (pDevice->DpPartnerPinAssignments == 0)
        pDevice->DpPartnerPinAssignments = dfp_pins;

    pDevice->DpState = DP_STATE_DISCOVERED;

    Print(DEBUG_LEVEL_INFO, DBG_IOCTL,
          "DP capabilities: dfp=0x%02x ufp=0x%02x selected-mask=0x%02x\n",
          dfp_pins, ufp_pins, pDevice->DpPartnerPinAssignments);

    return STATUS_SUCCESS;
}

static NTSTATUS typec_process_dp_status(
    _In_ PDEVICE_CONTEXT pDevice,
    _In_ ULONG status_vdo
)
{
    UCHAR selected_mask;
    UCHAR port_connected = (UCHAR)(status_vdo & DP_STATUS_PORT_CONNECTED_MASK);
    BOOLEAN prefer_multi_function = FALSE;

    if (status_vdo & DP_STATUS_MULTI_FUNCTION)
        prefer_multi_function = TRUE;

    pDevice->DpHpdState = FALSE;
    if (status_vdo & DP_STATUS_HPD_STATE)
        pDevice->DpHpdState = TRUE;

    pDevice->DpIrqHpd = FALSE;
    if (status_vdo & DP_STATUS_IRQ_HPD)
        pDevice->DpIrqHpd = TRUE;

    if (port_connected == DP_PORT_CONNECTED_NONE)
    {
        Print(DEBUG_LEVEL_INFO, DBG_IOCTL,
              "DP status: partner disconnected\n");
        return typec_clear_dp_state(pDevice, TRUE);
    }

    if (pDevice->DpSelectedPin == 0 && pDevice->DpPartnerPinAssignments != 0)
    {
        selected_mask = typec_dp_select_pin(
            pDevice->DpPartnerPinAssignments,
            prefer_multi_function);
        pDevice->DpSelectedPin = typec_dp_pin_to_number(selected_mask);
        pDevice->DpMuxMode = typec_dp_mux_from_pin(pDevice->DpSelectedPin);
    }

    pDevice->DpAltModeActive = TRUE;
    if (pDevice->DpState < DP_STATE_ENTERED)
        pDevice->DpState = DP_STATE_ENTERED;

    Print(DEBUG_LEVEL_INFO, DBG_IOCTL,
          "DP status: connected=%d hpd=%d irq=%d pin=%d mux=%d\n",
          port_connected,
          pDevice->DpHpdState,
          pDevice->DpIrqHpd,
          pDevice->DpSelectedPin,
          pDevice->DpMuxMode);

    if (pDevice->DpSelectedPin == 0)
        return STATUS_SUCCESS;

    return typec_notify_current_dp_state(pDevice);
}

static NTSTATUS typec_process_dp_configure(
    _In_ PDEVICE_CONTEXT pDevice,
    _In_ ULONG config_vdo
)
{
    UCHAR selected_pin;
    UCHAR selected_mask = (UCHAR)((config_vdo >> DP_CONFIG_PIN_SHIFT) &
                                  DP_CONFIG_PIN_MASK);
    UCHAR select_config = (UCHAR)(config_vdo & DP_CONFIG_SELECT_MASK);

    if (select_config == DP_CONFIG_USB)
        return typec_clear_dp_state(pDevice, TRUE);

    selected_pin = typec_dp_pin_to_number(selected_mask);
    if (selected_pin != 0)
        pDevice->DpSelectedPin = selected_pin;

    if (pDevice->DpSelectedPin == 0)
        return STATUS_SUCCESS;

    pDevice->DpAltModeActive = TRUE;
    pDevice->DpConfigured = TRUE;
    pDevice->DpState = DP_STATE_CONFIGURED;
    pDevice->DpMuxMode = typec_dp_mux_from_pin(pDevice->DpSelectedPin);

    Print(DEBUG_LEVEL_INFO, DBG_IOCTL,
          "DP configured: select=%d pin=%d mux=%d\n",
          select_config, pDevice->DpSelectedPin, pDevice->DpMuxMode);

    return typec_notify_current_dp_state(pDevice);
}

static NTSTATUS typec_process_vdm_message(
    _In_ PDEVICE_CONTEXT pDevice,
    _In_reads_(object_count) ULONG* objects,
    _In_ UCHAR object_count
)
{
    ULONG vdm_header;
    UCHAR command;
    UCHAR command_type;
    UCHAR vdm_type;
    USHORT svid;

    if (object_count == 0)
        return STATUS_SUCCESS;

    if (!pDevice->IsAttached)
        return STATUS_SUCCESS;

    if (pDevice->IsAudioAccessory)
        return STATUS_SUCCESS;

    vdm_header = objects[0];
    command = (UCHAR)(vdm_header & 0x1F);
    command_type = (UCHAR)((vdm_header >> 6) & 0x03);
    vdm_type = (UCHAR)((vdm_header >> 15) & 0x01);
    svid = (USHORT)(vdm_header >> 16);

    Print(DEBUG_LEVEL_VERBOSE, DBG_IOCTL,
          "VDM: svid=0x%04x type=%d cmd=%d cmdtype=%d objs=%d\n",
          svid, vdm_type, command, command_type, object_count);

    if (vdm_type != VDM_TYPE_STRUCTURED)
        return STATUS_SUCCESS;

    if (svid != PD_SID_DISPLAYPORT)
        return STATUS_SUCCESS;

    if (command == VDM_DISCOVER_MODES &&
        command_type == VDM_COMMAND_TYPE_RESPONDER_ACK &&
        object_count > 1)
    {
        return typec_process_dp_capabilities(pDevice, objects[1]);
    }

    if (command == VDM_ENTER_MODE &&
        command_type == VDM_COMMAND_TYPE_RESPONDER_ACK)
    {
        pDevice->DpAltModeActive = TRUE;
        pDevice->DpState = DP_STATE_ENTERED;
        Print(DEBUG_LEVEL_INFO, DBG_IOCTL, "DP Alt Mode entered\n");
        return STATUS_SUCCESS;
    }

    if (command == VDM_EXIT_MODE)
    {
        Print(DEBUG_LEVEL_INFO, DBG_IOCTL, "DP Alt Mode exited\n");
        return typec_clear_dp_state(pDevice, TRUE);
    }

    if (command == VDM_ATTENTION || command == VDM_DISPLAYPORT_STATUS_UPDATE)
    {
        if (object_count > 1)
            return typec_process_dp_status(pDevice, objects[1]);

        return STATUS_SUCCESS;
    }

    if (command == VDM_DISPLAYPORT_CONFIGURE)
    {
        if (object_count > 1)
            return typec_process_dp_configure(pDevice, objects[1]);

        if (command_type == VDM_COMMAND_TYPE_RESPONDER_ACK &&
            pDevice->DpSelectedPin != 0)
        {
            pDevice->DpConfigured = TRUE;
            pDevice->DpState = DP_STATE_CONFIGURED;
            return typec_notify_current_dp_state(pDevice);
        }
    }

    return STATUS_SUCCESS;
}

static NTSTATUS typec_process_pd_rx(_In_ PDEVICE_CONTEXT pDevice)
{
    NTSTATUS status;
    UCHAR header_bytes[2] = { 0 };
    UCHAR payload[PD_MAX_DATA_OBJECTS * PD_DATA_OBJECT_SIZE] = { 0 };
    ULONG objects[PD_MAX_DATA_OBJECTS] = { 0 };
    UCHAR object_count;
    UCHAR message_type;
    USHORT header;
    ULONG i;

    status = typec_read_block(
        pDevice,
        SM5714_REG_RX_HEADER_00,
        header_bytes,
        sizeof(header_bytes));
    if (!NT_SUCCESS(status))
        return status;

    header = typec_read_le16(header_bytes);
    object_count = typec_pd_header_object_count(header);
    if (object_count > PD_MAX_DATA_OBJECTS)
        object_count = PD_MAX_DATA_OBJECTS;

    if (object_count != 0)
    {
        status = typec_read_block(
            pDevice,
            SM5714_REG_RX_PAYLOAD,
            payload,
            (USHORT)(object_count * PD_DATA_OBJECT_SIZE));
        if (!NT_SUCCESS(status))
        {
            write_reg8(pDevice, SPB_USBPD_INDEX, SM5714_REG_RX_BUF, 0x80);
            return status;
        }

        for (i = 0; i < object_count; i++)
        {
            objects[i] = typec_read_le32(&payload[i * PD_DATA_OBJECT_SIZE]);
        }
    }

    status = write_reg8(pDevice, SPB_USBPD_INDEX, SM5714_REG_RX_BUF, 0x80);
    if (!NT_SUCCESS(status))
        return status;

    pDevice->PdMsgId = typec_pd_header_message_id(header);
    message_type = typec_pd_header_message_type(header);

    Print(DEBUG_LEVEL_VERBOSE, DBG_IOCTL,
          "PD RX: header=0x%04x msg=%d id=%d objs=%d\n",
          header, message_type, pDevice->PdMsgId, object_count);

    if (message_type != USBPD_VENDOR_DEFINED)
        return STATUS_SUCCESS;

    return typec_process_vdm_message(pDevice, objects, object_count);
}

static NTSTATUS typec_apply_attach_policy(_In_ PDEVICE_CONTEXT pDevice)
{
    NTSTATUS status = STATUS_SUCCESS;
    NTSTATUS battery_status;
    NTSTATUS redriver_status;
    ULONG idx = SPB_USBPD_INDEX;
    UCHAR attach_type = pDevice->AttachType;
    UCHAR reported_attach;

    typec_update_accessory_state(pDevice, attach_type);
    typec_reset_dp_state(pDevice);

    if (pDevice->IsAudioAccessory)
    {
        Print(DEBUG_LEVEL_INFO, DBG_IOCTL,
              "Role: Audio Accessory, USB data path disabled\n");
        status = muic_set_usb_path(pDevice, FALSE);
        if (!NT_SUCCESS(status))
            return status;

        pDevice->SuperSpeedReady = FALSE;
        redriver_status = Ps5169ConfigureRedriver(
            pDevice,
            PS5169_CONFIG_ATTACH_NONE,
            2);
        if (!NT_SUCCESS(redriver_status))
        {
            Print(DEBUG_LEVEL_ERROR, DBG_IOCTL,
                  "PS5169 clear failed - 0x%x\n", redriver_status);
        }

        battery_status = Sm5714BatterySetExternalPower(pDevice, FALSE);
        if (!NT_SUCCESS(battery_status))
        {
            Print(DEBUG_LEVEL_ERROR, DBG_IOCTL,
                  "Battery power state update failed - 0x%x\n",
                  battery_status);
        }

        return typec_notify_qualcomm_state(pDevice);
    }

    if (typec_is_source_like_attach(attach_type))
    {
        // We are Sink (charger cable connected). Debug Source is handled
        // as Source-like but kept distinguishable in IsDebugAccessory.
        status = update_reg8(pDevice, idx, SM5714_REG_PD_CNTL2, 0x03, 0x00);
        if (!NT_SUCCESS(status))
            return status;

        status = typec_set_otg_mode(pDevice, FALSE);
        if (!NT_SUCCESS(status))
            return status;

        // Adapt input current based on RP advertisement
        status = ChargerAdaptRpCurrent(pDevice);
        if (!NT_SUCCESS(status))
            return status;

        // Enable charging from VBUS
        status = ChargerEnable(pDevice, TRUE);
        if (!NT_SUCCESS(status))
            return status;

        // Read and log charger status
        ChargerReadStatus(pDevice);

        // Detect BC1.2 charger type (DCP / CDP / SDP)
        {
            UCHAR bc12;
            if (NT_SUCCESS(ChargerDetectBc12(pDevice, &bc12))) {
                pDevice->Bc12Type = bc12;
                /*
                 * Re-apply input current limit now that we know the charger
                 * type.  ChargerApplyInputCurrentPolicy combines BC1.2 type
                 * with RP advertisement and the ACPI-configured maximum.
                 */
                status = ChargerApplyInputCurrentPolicy(pDevice);
                if (!NT_SUCCESS(status))
                    return status;
            }
        }

        // Log the current PD state for debug. The full PD policy engine is
        // still handled by the SM5714 state machine.
        {
            UCHAR pd_state = 0;
            if (NT_SUCCESS(read_reg8(pDevice, idx,
                         SM5714_REG_PD_STATE0, &pd_state))) {
                Print(DEBUG_LEVEL_VERBOSE, DBG_IOCTL,
                      "PD state: 0x%02x\n", pd_state);
            }
        }

        if (pDevice->IsDebugAccessory)
        {
            Print(DEBUG_LEVEL_INFO, DBG_IOCTL,
                  "Role: Debug Accessory Source (UFP)\n");
        }
        else
        {
            Print(DEBUG_LEVEL_INFO, DBG_IOCTL, "Role: Sink (UFP)\n");
        }
    }
    else if (attach_type == SM5714_ATTACH_SINK)
    {
        // We are Source (OTG device connected)
        // Set data role DFP, power role Source
        status = update_reg8(pDevice, idx, SM5714_REG_PD_CNTL2, 0x03, 0x03);
        if (!NT_SUCCESS(status))
            return status;

        // Disable charging before entering OTG boost mode
        status = ChargerEnable(pDevice, FALSE);
        if (!NT_SUCCESS(status))
            return status;

        // Enable OTG VBUS output
        status = typec_set_otg_mode(pDevice, TRUE);
        if (!NT_SUCCESS(status))
            return status;

        Print(DEBUG_LEVEL_INFO, DBG_IOCTL, "Role: Source (DFP/OTG)\n");
    }
    else
    {
        Print(DEBUG_LEVEL_INFO, DBG_IOCTL,
              "Unknown attach type 0x%x, ignored\n", attach_type);
        return STATUS_SUCCESS;
    }

    reported_attach = typec_reported_usb_attach(attach_type);
    redriver_status = Ps5169ConfigureRedriver(
        pDevice,
        reported_attach,
        pDevice->CcOrientation);
    pDevice->SuperSpeedReady = NT_SUCCESS(redriver_status);
    if (!pDevice->SuperSpeedReady)
    {
        Print(DEBUG_LEVEL_ERROR, DBG_IOCTL,
              "PS5169 configuration failed - 0x%x, using USB2 fallback\n",
              redriver_status);
    }

    battery_status = Sm5714BatterySetExternalPower(
        pDevice,
        reported_attach == SM5714_ATTACH_SOURCE);
    if (!NT_SUCCESS(battery_status))
    {
        Print(DEBUG_LEVEL_ERROR, DBG_IOCTL,
              "Battery power state update failed - 0x%x\n", battery_status);
    }

    // Connect D+/D- only after the SuperSpeed redriver has been configured.
    status = muic_set_usb_path(pDevice, TRUE);
    if (!NT_SUCCESS(status))
    {
        typec_set_disconnected_state(pDevice);

        redriver_status = Ps5169ConfigureRedriver(
            pDevice,
            PS5169_CONFIG_ATTACH_NONE,
            2);
        if (!NT_SUCCESS(redriver_status))
        {
            Print(DEBUG_LEVEL_ERROR, DBG_IOCTL,
                  "PS5169 rollback after MUIC failure failed - 0x%x\n",
                  redriver_status);
        }

        battery_status = Sm5714BatterySetExternalPower(pDevice, FALSE);
        if (!NT_SUCCESS(battery_status))
        {
            Print(DEBUG_LEVEL_ERROR, DBG_IOCTL,
                  "Battery rollback after MUIC failure failed - 0x%x\n",
                  battery_status);
        }

        redriver_status = typec_notify_qualcomm_state(pDevice);
        if (!NT_SUCCESS(redriver_status))
        {
            Print(DEBUG_LEVEL_ERROR, DBG_IOCTL,
                  "USB state rollback after MUIC failure failed - 0x%x\n",
                  redriver_status);
        }
        return status;
    }

    // Publish the role only after both the SuperSpeed and D+/D- paths are
    // physically ready, matching the previously working enumeration order.
    status = typec_notify_qualcomm_state(pDevice);
    if (!NT_SUCCESS(status))
    {
        Print(DEBUG_LEVEL_ERROR, DBG_IOCTL,
              "Qualcomm USB state notification failed - 0x%x; preserving physical attach\n",
              status);
    }

    return STATUS_SUCCESS;
}

//
// Initialize SM5714 USBPD controller registers.
// Sets up interrupt masks, enables DRP mode, and clears pending interrupts.
//
NTSTATUS typec_reg_init(_In_ PDEVICE_CONTEXT pDevice)
{
    NTSTATUS status;
    LARGE_INTEGER delay;
    UCHAR dummy = 0;
    ULONG idx = SPB_USBPD_INDEX;

    // Soft-reset the USBPD block
    status = write_reg8(pDevice, idx, SM5714_REG_SYS_CNTL, 0x80);
    if (!NT_SUCCESS(status))
    {
        Print(DEBUG_LEVEL_ERROR, DBG_INIT,
              "USBPD soft reset failed - 0x%x\n", status);
        return status;
    }

    // Wait for reset to settle (2 ms)
    delay.QuadPart = -20000;  // 2 ms in 100ns units
    KeDelayExecutionThread(KernelMode, FALSE, &delay);

    // Clear pending interrupts by reading INT1-INT5
    status = read_reg8(pDevice, idx, SM5714_REG_INT1, &dummy);
    if (!NT_SUCCESS(status)) return status;
    status = read_reg8(pDevice, idx, SM5714_REG_INT2, &dummy);
    if (!NT_SUCCESS(status)) return status;
    status = read_reg8(pDevice, idx, SM5714_REG_INT3, &dummy);
    if (!NT_SUCCESS(status)) return status;
    status = read_reg8(pDevice, idx, SM5714_REG_INT4, &dummy);
    if (!NT_SUCCESS(status)) return status;
    status = read_reg8(pDevice, idx, SM5714_REG_INT5, &dummy);
    if (!NT_SUCCESS(status)) return status;

    // Enable only the interrupts we handle:
    // MASK register: 0 = enabled, 1 = masked (inverted logic)
    status = write_reg8(pDevice, idx, SM5714_REG_INT_MASK1,
                        (UCHAR)~USBPD_ENABLED_INT1);
    if (!NT_SUCCESS(status)) return status;
    status = write_reg8(pDevice, idx, SM5714_REG_INT_MASK2,
                        (UCHAR)~USBPD_ENABLED_INT2);
    if (!NT_SUCCESS(status)) return status;
    status = write_reg8(pDevice, idx, SM5714_REG_INT_MASK3,
                        (UCHAR)~USBPD_ENABLED_INT3);
    if (!NT_SUCCESS(status)) return status;
    status = write_reg8(pDevice, idx, SM5714_REG_INT_MASK4,
                        (UCHAR)~USBPD_ENABLED_INT4);
    if (!NT_SUCCESS(status)) return status;
    status = write_reg8(pDevice, idx, SM5714_REG_INT_MASK5,
                        (UCHAR)~USBPD_ENABLED_INT5);
    if (!NT_SUCCESS(status)) return status;

    // JIGON / COMP / CLK defaults (from kernel reference)
    status = write_reg8(pDevice, idx, SM5714_REG_JIGON_CONTROL, 0x03);
    if (!NT_SUCCESS(status)) return status;
    status = write_reg8(pDevice, idx, SM5714_REG_COMP_CNTL, 0x98);
    if (!NT_SUCCESS(status)) return status;
    status = write_reg8(pDevice, idx, SM5714_REG_CLK_CNTL, 0x08);
    if (!NT_SUCCESS(status)) return status;

    // Enable DRP toggling: CC_CNTL1 = 0x41 (DRP mode)
    status = write_reg8(pDevice, idx, SM5714_REG_CC_CNTL1, 0x41);
    if (!NT_SUCCESS(status)) return status;

    // Enable CC detection with pull-up/down
    status = write_reg8(pDevice, idx, SM5714_REG_CC_CNTL3, 0x80);
    if (!NT_SUCCESS(status)) return status;

    Print(DEBUG_LEVEL_INFO, DBG_INIT,
          "USBPD registers initialized, DRP mode enabled\n");

    return STATUS_SUCCESS;
}

//
// Read initial state after driver initialization.
// The cable might already be plugged in when the driver starts.
//
NTSTATUS typec_check_initial_state(_In_ PDEVICE_CONTEXT pDevice)
{
    NTSTATUS status;
    NTSTATUS battery_status;
    UCHAR cc_status = 0;
    UCHAR status1 = 0;
    ULONG idx = SPB_USBPD_INDEX;
    UCHAR attach_type;

    status = read_reg8(pDevice, idx, SM5714_REG_STATUS1, &status1);
    if (!NT_SUCCESS(status))
        return status;

    status = read_reg8(pDevice, idx, SM5714_REG_CC_STATUS, &cc_status);
    if (!NT_SUCCESS(status))
        return status;

    attach_type = cc_status & SM5714_CC_ATTACH_TYPE;

    if ((status1 & SM5714_INT_STATUS1_ATTACH) && attach_type != SM5714_ATTACH_NONE)
    {
        pDevice->IsAttached = TRUE;
        pDevice->AttachType = attach_type;
        pDevice->CcOrientation = (cc_status & SM5714_CC_CABLE_FLIP) ? 1 : 0;
        pDevice->VbusPresent = (status1 & SM5714_INT_STATUS1_VBUSPOK) ? TRUE : FALSE;

        Print(DEBUG_LEVEL_INFO, DBG_INIT,
              "Boot attached: type=%d, CC=%d, VBUS=%d\n",
              attach_type, pDevice->CcOrientation, pDevice->VbusPresent);

        status = typec_apply_attach_policy(pDevice);
        if (!NT_SUCCESS(status))
            typec_set_disconnected_state(pDevice);

        return status;
    }
    else
    {
        typec_set_disconnected_state(pDevice);

        battery_status = Sm5714BatterySetExternalPower(pDevice, FALSE);
        if (!NT_SUCCESS(battery_status))
        {
            Print(DEBUG_LEVEL_ERROR, DBG_INIT,
                  "Initial battery power state update failed - 0x%x\n",
                  battery_status);
        }
    }

    return STATUS_SUCCESS;
}

//
// Process an attach event from SM5714 USBPD.
// Reads CC_STATUS to determine attach type, orientation, and RP current level.
//
static NTSTATUS typec_process_attach(_In_ PDEVICE_CONTEXT pDevice)
{
    NTSTATUS status;
    UCHAR cc_status = 0;
    UCHAR status_reg = 0;
    ULONG idx = SPB_USBPD_INDEX;

    status = read_reg8(pDevice, idx, SM5714_REG_CC_STATUS, &cc_status);
    if (!NT_SUCCESS(status))
        return status;

    status = read_reg8(pDevice, idx, SM5714_REG_STATUS1, &status_reg);
    if (!NT_SUCCESS(status))
        return status;

    UCHAR attach_type = cc_status & SM5714_CC_ATTACH_TYPE;
    UCHAR cc_flip = (cc_status & SM5714_CC_CABLE_FLIP) ? 1 : 0;
    BOOLEAN vbus_ok = (status_reg & SM5714_INT_STATUS1_VBUSPOK) ? TRUE : FALSE;

    Print(DEBUG_LEVEL_INFO, DBG_IOCTL,
          "Attach: CC_STATUS=0x%02x type=%d flip=%d vbus=%d\n",
          cc_status, attach_type, cc_flip, vbus_ok);

    if (attach_type == SM5714_ATTACH_NONE)
    {
        typec_set_disconnected_state(pDevice);

        status = Sm5714BatterySetExternalPower(pDevice, FALSE);
        if (!NT_SUCCESS(status))
        {
            Print(DEBUG_LEVEL_ERROR, DBG_IOCTL,
                  "Battery power state update failed - 0x%x\n", status);
        }

        return typec_notify_qualcomm_state(pDevice);
    }

    if (!typec_is_source_like_attach(attach_type) &&
        attach_type != SM5714_ATTACH_SINK &&
        attach_type != SM5714_ATTACH_AUDIO)
    {
        Print(DEBUG_LEVEL_ERROR, DBG_IOCTL,
              "Unsupported attach type 0x%x\n", attach_type);
        typec_set_disconnected_state(pDevice);

          status = Sm5714BatterySetExternalPower(pDevice, FALSE);
          if (!NT_SUCCESS(status))
          {
            Print(DEBUG_LEVEL_ERROR, DBG_IOCTL,
                "Battery power state update failed - 0x%x\n", status);
          }

        return STATUS_INVALID_DEVICE_STATE;
    }

    pDevice->IsAttached = TRUE;
    pDevice->AttachType = attach_type;
    pDevice->CcOrientation = cc_flip;
    pDevice->VbusPresent = vbus_ok;
    pDevice->Bc12Type = 0;
    pDevice->RpCurrentAdvertised = 0;

    status = typec_apply_attach_policy(pDevice);
    if (!NT_SUCCESS(status))
    {
        NTSTATUS rollback_status;

        Print(DEBUG_LEVEL_ERROR, DBG_IOCTL,
              "Attach policy failed - 0x%x, rolling back state\n", status);
        typec_set_disconnected_state(pDevice);

        rollback_status = muic_set_usb_path(pDevice, FALSE);
        if (!NT_SUCCESS(rollback_status))
        {
            Print(DEBUG_LEVEL_ERROR, DBG_IOCTL,
                  "MUIC rollback after attach failure failed - 0x%x\n",
                  rollback_status);
        }

        rollback_status = Ps5169ConfigureRedriver(
            pDevice,
            PS5169_CONFIG_ATTACH_NONE,
            2);
        if (!NT_SUCCESS(rollback_status))
        {
            Print(DEBUG_LEVEL_ERROR, DBG_IOCTL,
                  "PS5169 rollback after attach failure failed - 0x%x\n",
                  rollback_status);
        }

        rollback_status = Sm5714BatterySetExternalPower(pDevice, FALSE);
        if (!NT_SUCCESS(rollback_status))
        {
            Print(DEBUG_LEVEL_ERROR, DBG_IOCTL,
                  "Battery rollback after attach failure failed - 0x%x\n",
                  rollback_status);
        }

        rollback_status = typec_notify_qualcomm_state(pDevice);
        if (!NT_SUCCESS(rollback_status))
        {
            Print(DEBUG_LEVEL_ERROR, DBG_IOCTL,
                  "Qualcomm rollback after attach failure failed - 0x%x\n",
                  rollback_status);
        }
    }

    return status;
}

//
// Process a detach event.
// Resets Type-C state and returns to DRP toggling.
//
static NTSTATUS typec_process_detach(_In_ PDEVICE_CONTEXT pDevice)
{
    NTSTATUS status = STATUS_SUCCESS;
    NTSTATUS operation_status;
    ULONG idx = SPB_USBPD_INDEX;
    BOOLEAN was_otg = (pDevice->AttachType == SM5714_ATTACH_SINK);

    Print(DEBUG_LEVEL_INFO, DBG_IOCTL, "Detach event\n");

    typec_set_disconnected_state(pDevice);

    // Disable OTG if it was active
    if (was_otg)
    {
        operation_status = typec_set_otg_mode(pDevice, FALSE);
        if (NT_SUCCESS(status) && !NT_SUCCESS(operation_status))
            status = operation_status;

        // Re-enable charging after exiting OTG mode
        operation_status = ChargerEnable(pDevice, TRUE);
        if (NT_SUCCESS(status) && !NT_SUCCESS(operation_status))
            status = operation_status;
    }

    // Return to DRP toggling
    operation_status = write_reg8(pDevice, idx, SM5714_REG_CC_CNTL1, 0x41);
    if (NT_SUCCESS(status) && !NT_SUCCESS(operation_status))
        status = operation_status;
    operation_status = write_reg8(pDevice, idx, SM5714_REG_CC_CNTL3, 0x80);
    if (NT_SUCCESS(status) && !NT_SUCCESS(operation_status))
        status = operation_status;

    // Disconnect D+/D- data lines through the MUIC
    operation_status = muic_set_usb_path(pDevice, FALSE);
    if (NT_SUCCESS(status) && !NT_SUCCESS(operation_status))
        status = operation_status;

    operation_status = Ps5169ConfigureRedriver(
        pDevice,
        PS5169_CONFIG_ATTACH_NONE,
        2);
    if (NT_SUCCESS(status) && !NT_SUCCESS(operation_status))
        status = operation_status;

    operation_status = Sm5714BatterySetExternalPower(pDevice, FALSE);
    if (NT_SUCCESS(status) && !NT_SUCCESS(operation_status))
        status = operation_status;

    // Notify the Qualcomm USB connector manager.
    operation_status = typec_notify_qualcomm_state(pDevice);
    if (NT_SUCCESS(status) && !NT_SUCCESS(operation_status))
        status = operation_status;

    return status;
}

//
// Main interrupt handler called from the DPC.
// Reads interrupt registers, clears them, and dispatches to attach/detach handlers.
//
NTSTATUS typec_process_interrupt(_In_ PDEVICE_CONTEXT pDevice)
{
    NTSTATUS status;
    NTSTATUS read_status;
    NTSTATUS event_status;
    UCHAR intr[5] = { 0 };
    UCHAR stat[5] = { 0 };
    UCHAR cc_status = 0;
    UCHAR current_attach;
    ULONG idx = SPB_USBPD_INDEX;
    ULONG register_index;

    // Read every interrupt register so a transient failure does not leave
    // another readable register latched and the level-triggered line asserted.
    status = STATUS_SUCCESS;
    for (register_index = 0; register_index < ARRAYSIZE(intr); register_index++)
    {
        read_status = read_reg8(
            pDevice,
            idx,
            (UCHAR)(SM5714_REG_INT1 + register_index),
            &intr[register_index]);
        if (!NT_SUCCESS(read_status) && NT_SUCCESS(status))
            status = read_status;
    }

    if (!NT_SUCCESS(status))
        return status;

    // Read current status
    for (register_index = 0; register_index < ARRAYSIZE(stat); register_index++)
    {
        status = read_reg8(
            pDevice,
            idx,
            (UCHAR)(SM5714_REG_STATUS1 + register_index),
            &stat[register_index]);
        if (!NT_SUCCESS(status))
            return status;
    }

    Print(DEBUG_LEVEL_VERBOSE, DBG_IOCTL,
          "IRQ: INT[%02x %02x %02x %02x %02x] STAT[%02x %02x %02x %02x %02x]\n",
          intr[0], intr[1], intr[2], intr[3], intr[4],
          stat[0], stat[1], stat[2], stat[3], stat[4]);

    if (intr[4] & SM5714_INT_STATUS5_SBU1_OVP)
    {
        Print(DEBUG_LEVEL_ERROR, DBG_IOCTL, "SBU1 over-voltage detected\n");
    }

    if (intr[4] & SM5714_INT_STATUS5_SBU2_OVP)
    {
        Print(DEBUG_LEVEL_ERROR, DBG_IOCTL, "SBU2 over-voltage detected\n");
    }

    if (intr[4] & SM5714_INT_STATUS5_CC_ABNORMAL)
    {
        Print(DEBUG_LEVEL_ERROR, DBG_IOCTL, "Abnormal CC condition detected\n");
    }

    //
    // Water detection: log only, no action needed on Windows
    //
    if ((intr[2] & SM5714_INT_STATUS3_WATER) &&
        (stat[2] & SM5714_INT_STATUS3_WATER))
    {
        Print(DEBUG_LEVEL_ERROR, DBG_IOCTL, "WARNING: Water detected on CC!\n");
    }

    if (intr[2] & SM5714_INT_STATUS3_WATER_RLS)
    {
        if (!(stat[2] & SM5714_INT_STATUS3_WATER))
            Print(DEBUG_LEVEL_INFO, DBG_IOCTL, "Water condition released\n");
    }

    //
    // VBUS 0V event: charger cable was removed while still attached
    //
    if (intr[1] & SM5714_INT_STATUS2_VBUS_0V)
    {
        pDevice->VbusPresent = FALSE;
        Print(DEBUG_LEVEL_INFO, DBG_IOCTL, "VBUS dropped to 0V\n");
    }

    //
    // VBUS present event
    //
    if ((intr[0] & SM5714_INT_STATUS1_VBUSPOK) &&
        (stat[0] & SM5714_INT_STATUS1_VBUSPOK))
    {
        pDevice->VbusPresent = TRUE;
        Print(DEBUG_LEVEL_INFO, DBG_IOCTL, "VBUS detected\n");
    }

    // Resolve attach/detach races from the current CC state. Both event bits
    // may be latched during a quick role transition, but only one final state
    // must be published to the battery and Qualcomm clients.
    if (intr[0] & (SM5714_INT_STATUS1_ATTACH | SM5714_INT_STATUS1_DETACH))
    {
        event_status = read_reg8(
            pDevice,
            idx,
            SM5714_REG_CC_STATUS,
            &cc_status);
        if (!NT_SUCCESS(event_status))
        {
            if (NT_SUCCESS(status))
                status = event_status;
            goto ProcessInterruptEnd;
        }

        current_attach = cc_status & SM5714_CC_ATTACH_TYPE;
        if (current_attach != SM5714_ATTACH_NONE &&
            (stat[0] & SM5714_INT_STATUS1_ATTACH))
        {
            event_status = typec_process_attach(pDevice);
        }
        else if (current_attach == SM5714_ATTACH_NONE &&
                 (stat[0] & SM5714_INT_STATUS1_DETACH))
        {
            event_status = typec_process_detach(pDevice);
        }
        else
        {
            Print(DEBUG_LEVEL_INFO, DBG_IOCTL,
                  "Ignoring stale Type-C event: INT1=0x%02x STATUS1=0x%02x CC_STATUS=0x%02x\n",
                  intr[0], stat[0], cc_status);
            event_status = STATUS_SUCCESS;
        }

        if (NT_SUCCESS(status) && !NT_SUCCESS(event_status))
            status = event_status;
    }

    if (intr[3] & SM5714_INT_STATUS4_RX_DONE)
    {
        event_status = typec_process_pd_rx(pDevice);
        if (!NT_SUCCESS(event_status))
        {
            Print(DEBUG_LEVEL_ERROR, DBG_IOCTL,
                  "PD RX processing failed - 0x%x\n", event_status);
        }
    }

    //
    // RP current level change (charger changed advertisement)
    //
    if (intr[1] & SM5714_INT_STATUS2_SRC_ADV_CHG)
    {
        if (pDevice->IsAttached && pDevice->AttachType == SM5714_ATTACH_SOURCE)
        {
            UCHAR cc_status = 0;
            event_status = read_reg8(pDevice, idx,
                                     SM5714_REG_CC_STATUS, &cc_status);
            if (!NT_SUCCESS(event_status))
            {
                if (NT_SUCCESS(status)) status = event_status;
                goto ProcessInterruptEnd;
            }

            UCHAR adv = cc_status & SM5714_CC_ADV_CURR;
            Print(DEBUG_LEVEL_INFO, DBG_IOCTL,
                  "RP current advertisement changed: 0x%02x\n", adv);

            /*
             * Refresh RpCurrentAdvertised and re-apply the full ICL policy
             * so that a charger upgrading from default-500mA to 1.5A or 3A
             * is recognised immediately.
             */
            event_status = ChargerAdaptRpCurrent(pDevice);
            if (NT_SUCCESS(status) && !NT_SUCCESS(event_status))
                status = event_status;
            if (NT_SUCCESS(event_status))
            {
                event_status = ChargerApplyInputCurrentPolicy(pDevice);
                if (NT_SUCCESS(status) && !NT_SUCCESS(event_status))
                    status = event_status;
            }
        }
    }

ProcessInterruptEnd:
    return status;
}

//
// Control the charger's OTG (boost) mode.
// Enables 5V VBUS output when the tablet acts as USB host.
//
// OTG enable sequence (from kernel sm5714_charger_oper.c):
//   1. Set BSTCNTL1: BSTOUT=5100mV (bits[3:0]=0x6), OTG_CURRENT=900mA (bits[7:6]=0x1)
//   2. Set CNTL2: OP_MODE=USB_OTG (bits[3:0]=0x7)
//
// OTG disable sequence:
//   1. Set CNTL2: OP_MODE=CHG_ON_VBUS (bits[3:0]=0x5)
//
NTSTATUS typec_set_otg_mode(_In_ PDEVICE_CONTEXT pDevice, _In_ BOOLEAN enable)
{
    NTSTATUS status;

    if (enable)
    {
        Print(DEBUG_LEVEL_INFO, DBG_IOCTL, "Enabling OTG VBUS output\n");

        // Configure boost: 5.1V output, 900mA current limit
        // BSTCNTL1: bits[3:0] = BSTOUT, bits[7:6] = OTG_CURRENT
        status = update_reg8(pDevice, SPB_CHARGER_INDEX,
                             SM5714_CHG_REG_BSTCNTL1,
                             0xCF,  // mask: bits[3:0] + bits[7:6]
                             BSTOUT_5100mV | OTG_CURRENT_900mA);
        if (!NT_SUCCESS(status))
        {
            Print(DEBUG_LEVEL_ERROR, DBG_IOCTL,
                  "Failed to set BSTCNTL1 - 0x%x\n", status);
            return status;
        }

        // Set operation mode to USB_OTG (CNTL2 bits[3:0])
        status = update_reg8(pDevice, SPB_CHARGER_INDEX,
                             SM5714_CHG_REG_CNTL2, OP_MODE_MASK, OP_MODE_USB_OTG);
    }
    else
    {
        Print(DEBUG_LEVEL_INFO, DBG_IOCTL, "Disabling OTG, returning to charge mode\n");

        // Set operation mode back to CHG_ON_VBUS (CNTL2 bits[3:0])
        status = update_reg8(pDevice, SPB_CHARGER_INDEX,
                             SM5714_CHG_REG_CNTL2, OP_MODE_MASK, OP_MODE_CHG_ON_VBUS);
    }

    return status;
}

//
// Evaluate the PM3P.USBR ACPI method to update global
// Type-C state variables and notify the USB connector manager (UCS0).
//
NTSTATUS typec_notify_qualcomm_state(_In_ PDEVICE_CONTEXT pDevice)
{
    NTSTATUS status;
    UCHAR attach = typec_reported_usb_attach(pDevice->AttachType);
    UCHAR cc_out = pDevice->CcOrientation;
    UCHAR vbus = pDevice->VbusPresent ? 1 : 0;

    if (attach == SM5714_ATTACH_NONE)
    {
        cc_out = 2;
        vbus = 0;
    }

    // Build ACPI method input:
    // USBR(AttachType, CcOrientation, VbusPresent, SuperSpeedReady)
    ULONG inputSize = sizeof(ACPI_EVAL_INPUT_BUFFER_COMPLEX) +
                      4 * sizeof(ACPI_METHOD_ARGUMENT);

    PACPI_EVAL_INPUT_BUFFER_COMPLEX pInput =
        (PACPI_EVAL_INPUT_BUFFER_COMPLEX)ExAllocatePoolZero(
            NonPagedPoolNx, inputSize, 'rsbU');
    if (!pInput)
        return STATUS_INSUFFICIENT_RESOURCES;

    pInput->Signature = ACPI_EVAL_INPUT_BUFFER_COMPLEX_SIGNATURE;
    RtlCopyMemory(pInput->MethodName, "USBR", 4);
    pInput->Size = 4 * sizeof(ACPI_METHOD_ARGUMENT);
    pInput->ArgumentCount = 4;

    PACPI_METHOD_ARGUMENT pArg = &pInput->Argument[0];
    ACPI_METHOD_SET_ARGUMENT_INTEGER(pArg, (ULONG)attach);

    pArg = ACPI_METHOD_NEXT_ARGUMENT(pArg);
    ACPI_METHOD_SET_ARGUMENT_INTEGER(pArg, (ULONG)cc_out);

    pArg = ACPI_METHOD_NEXT_ARGUMENT(pArg);
    ACPI_METHOD_SET_ARGUMENT_INTEGER(pArg, (ULONG)vbus);

    pArg = ACPI_METHOD_NEXT_ARGUMENT(pArg);
    ACPI_METHOD_SET_ARGUMENT_INTEGER(
        pArg,
        pDevice->SuperSpeedReady ? 1UL : 0UL);

    WDF_MEMORY_DESCRIPTOR inDesc;
    WDF_MEMORY_DESCRIPTOR_INIT_BUFFER(&inDesc, pInput, inputSize);

    status = WdfIoTargetSendIoctlSynchronously(
        WdfDeviceGetIoTarget(pDevice->FxDevice),
        NULL,
        IOCTL_ACPI_EVAL_METHOD,
        &inDesc,
        NULL,
        NULL,
        NULL);

    if (!NT_SUCCESS(status))
    {
        Print(DEBUG_LEVEL_ERROR, DBG_IOCTL,
              "USBR ACPI method failed - 0x%x\n", status);
    }
    else
    {
        Print(DEBUG_LEVEL_INFO, DBG_IOCTL,
              "USB state notified: attach=%d cc=%d vbus=%d ss=%d\n",
              attach, cc_out, vbus, pDevice->SuperSpeedReady);
    }

    ExFreePool(pInput);
    return status;
}