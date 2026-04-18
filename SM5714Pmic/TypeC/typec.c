#include "..\Common\registers.h"
#include "..\Common\spbhelper.h"
#include "..\Charger\charger.h"
#include "typec.h"

#include <acpiioct.h>

static ULONG DebugLevel = 100;
static ULONG DebugCatagories = DBG_INIT || DBG_PNP || DBG_IOCTL;

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
static NTSTATUS muic_set_usb_path(_In_ PDEVICE_CONTEXT pDevice, _In_ bool connect)
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

//
// Initialize SM5714 USBPD controller registers.
// Sets up interrupt masks, enables DRP mode, and clears pending interrupts.
//
NTSTATUS typec_reg_init(_In_ PDEVICE_CONTEXT pDevice)
{
    NTSTATUS status;
    UCHAR dummy;
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
    LARGE_INTEGER delay;
    delay.QuadPart = -20000;  // 2 ms in 100ns units
    KeDelayExecutionThread(KernelMode, FALSE, &delay);

    // Clear pending interrupts by reading INT1-INT5
    read_reg8(pDevice, idx, SM5714_REG_INT1, &dummy);
    read_reg8(pDevice, idx, SM5714_REG_INT2, &dummy);
    read_reg8(pDevice, idx, SM5714_REG_INT3, &dummy);
    read_reg8(pDevice, idx, SM5714_REG_INT4, &dummy);
    read_reg8(pDevice, idx, SM5714_REG_INT5, &dummy);

    // Enable only the interrupts we handle:
    // MASK register: 0 = enabled, 1 = masked (inverted logic)
    write_reg8(pDevice, idx, SM5714_REG_INT_MASK1, (UCHAR)~USBPD_ENABLED_INT1);
    write_reg8(pDevice, idx, SM5714_REG_INT_MASK2, (UCHAR)~USBPD_ENABLED_INT2);
    write_reg8(pDevice, idx, SM5714_REG_INT_MASK3, (UCHAR)~USBPD_ENABLED_INT3);
    write_reg8(pDevice, idx, SM5714_REG_INT_MASK4, 0xFF);  // Mask all PD message IRQs
    write_reg8(pDevice, idx, SM5714_REG_INT_MASK5, 0xFF);  // Mask all aux IRQs

    // JIGON / COMP / CLK defaults (from kernel reference)
    write_reg8(pDevice, idx, SM5714_REG_JIGON_CONTROL, 0x03);
    write_reg8(pDevice, idx, SM5714_REG_COMP_CNTL, 0x98);
    write_reg8(pDevice, idx, SM5714_REG_CLK_CNTL, 0x08);

    // Enable DRP toggling: CC_CNTL1 = 0x41 (DRP mode)
    write_reg8(pDevice, idx, SM5714_REG_CC_CNTL1, 0x41);

    // Enable CC detection with pull-up/down
    write_reg8(pDevice, idx, SM5714_REG_CC_CNTL3, 0x80);

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

        // Apply charger mode based on detected state
        if (attach_type == SM5714_ATTACH_SINK)
        {
            // OTG host mode: enable VBUS output
            typec_set_otg_mode(pDevice, true);
        }

        // Connect D+/D- data lines through the MUIC
        muic_set_usb_path(pDevice, true);

        // Report to the USB stack
        typec_notify_usb_state(pDevice);
    }
    else
    {
        pDevice->IsAttached = FALSE;
        pDevice->AttachType = SM5714_ATTACH_NONE;
        pDevice->CcOrientation = 2;
        pDevice->VbusPresent = FALSE;
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

    pDevice->IsAttached = TRUE;
    pDevice->AttachType = attach_type;
    pDevice->CcOrientation = cc_flip;
    pDevice->VbusPresent = vbus_ok;

    if (attach_type == SM5714_ATTACH_SOURCE)
    {
        // We are Sink (charger cable connected)
        // Set data role UFP, power role Sink
        update_reg8(pDevice, idx, SM5714_REG_PD_CNTL2, 0x03, 0x00);

        // Keep charger in normal charging mode
        typec_set_otg_mode(pDevice, false);

        // Enable charging from VBUS
        enable_charging(pDevice, true);

        Print(DEBUG_LEVEL_INFO, DBG_IOCTL, "Role: Sink (UFP)\n");
    }
    else if (attach_type == SM5714_ATTACH_SINK)
    {
        // We are Source (OTG device connected)
        // Set data role DFP, power role Source
        update_reg8(pDevice, idx, SM5714_REG_PD_CNTL2, 0x03, 0x03);

        // Disable charging before entering OTG boost mode
        enable_charging(pDevice, false);

        // Enable OTG VBUS output
        typec_set_otg_mode(pDevice, true);

        Print(DEBUG_LEVEL_INFO, DBG_IOCTL, "Role: Source (DFP/OTG)\n");
    }
    else
    {
        Print(DEBUG_LEVEL_INFO, DBG_IOCTL,
              "Unknown attach type 0x%x, ignored\n", attach_type);
        return STATUS_SUCCESS;
    }

    // Connect D+/D- data lines through the MUIC analog switch
    muic_set_usb_path(pDevice, true);

    // Notify the USB stack about the new state
    return typec_notify_usb_state(pDevice);
}

//
// Process a detach event.
// Resets Type-C state and returns to DRP toggling.
//
static NTSTATUS typec_process_detach(_In_ PDEVICE_CONTEXT pDevice)
{
    ULONG idx = SPB_USBPD_INDEX;

    Print(DEBUG_LEVEL_INFO, DBG_IOCTL, "Detach event\n");

    // Disable OTG if it was active
    if (pDevice->AttachType == SM5714_ATTACH_SINK)
    {
        typec_set_otg_mode(pDevice, false);

        // Re-enable charging after exiting OTG mode
        enable_charging(pDevice, true);
    }

    pDevice->IsAttached = FALSE;
    pDevice->AttachType = SM5714_ATTACH_NONE;
    pDevice->CcOrientation = 2;  // Open
    pDevice->VbusPresent = FALSE;

    // Return to DRP toggling
    write_reg8(pDevice, idx, SM5714_REG_CC_CNTL1, 0x41);
    write_reg8(pDevice, idx, SM5714_REG_CC_CNTL3, 0x80);

    // Disconnect D+/D- data lines through the MUIC
    muic_set_usb_path(pDevice, false);

    // Notify the USB stack
    return typec_notify_usb_state(pDevice);
}

//
// Main interrupt handler called from the DPC.
// Reads interrupt registers, clears them, and dispatches to attach/detach handlers.
//
NTSTATUS typec_process_interrupt(_In_ PDEVICE_CONTEXT pDevice)
{
    NTSTATUS status;
    UCHAR intr[5] = { 0 };
    UCHAR stat[5] = { 0 };
    ULONG idx = SPB_USBPD_INDEX;

    // Read interrupt flags (reading clears them on SM5714)
    status = read_reg8(pDevice, idx, SM5714_REG_INT1, &intr[0]);
    if (!NT_SUCCESS(status))
        return status;
    read_reg8(pDevice, idx, SM5714_REG_INT2, &intr[1]);
    read_reg8(pDevice, idx, SM5714_REG_INT3, &intr[2]);
    read_reg8(pDevice, idx, SM5714_REG_INT4, &intr[3]);
    read_reg8(pDevice, idx, SM5714_REG_INT5, &intr[4]);

    // Read current status
    read_reg8(pDevice, idx, SM5714_REG_STATUS1, &stat[0]);
    read_reg8(pDevice, idx, SM5714_REG_STATUS2, &stat[1]);
    read_reg8(pDevice, idx, SM5714_REG_STATUS3, &stat[2]);
    read_reg8(pDevice, idx, SM5714_REG_STATUS4, &stat[3]);
    read_reg8(pDevice, idx, SM5714_REG_STATUS5, &stat[4]);

    Print(DEBUG_LEVEL_VERBOSE, DBG_IOCTL,
          "IRQ: INT[%02x %02x %02x %02x %02x] STAT[%02x %02x %02x %02x %02x]\n",
          intr[0], intr[1], intr[2], intr[3], intr[4],
          stat[0], stat[1], stat[2], stat[3], stat[4]);

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

    //
    // Detach: process before attach to handle quick re-plug
    //
    if ((intr[0] & SM5714_INT_STATUS1_DETACH) &&
        (stat[0] & SM5714_INT_STATUS1_DETACH))
    {
        typec_process_detach(pDevice);
    }

    //
    // Attach event
    //
    if ((intr[0] & SM5714_INT_STATUS1_ATTACH) &&
        (stat[0] & SM5714_INT_STATUS1_ATTACH))
    {
        typec_process_attach(pDevice);
    }

    //
    // RP current level change (charger changed advertisement)
    //
    if (intr[1] & SM5714_INT_STATUS2_SRC_ADV_CHG)
    {
        if (pDevice->IsAttached && pDevice->AttachType == SM5714_ATTACH_SOURCE)
        {
            UCHAR cc_status = 0;
            read_reg8(pDevice, idx, SM5714_REG_CC_STATUS, &cc_status);

            UCHAR adv = cc_status & SM5714_CC_ADV_CURR;
            Print(DEBUG_LEVEL_INFO, DBG_IOCTL,
                  "RP current advertisement changed: 0x%02x\n", adv);
        }
    }

    return STATUS_SUCCESS;
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
NTSTATUS typec_set_otg_mode(_In_ PDEVICE_CONTEXT pDevice, _In_ bool enable)
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
NTSTATUS typec_notify_usb_state(_In_ PDEVICE_CONTEXT pDevice)
{
    NTSTATUS status;
    UCHAR attach = pDevice->AttachType;
    UCHAR cc_out = pDevice->CcOrientation;
    UCHAR vbus = pDevice->VbusPresent ? 1 : 0;

    // Build ACPI method input: USBR(AttachType, CcOrientation, VbusPresent)
    ULONG inputSize = sizeof(ACPI_EVAL_INPUT_BUFFER_COMPLEX) +
                      3 * sizeof(ACPI_METHOD_ARGUMENT);

    PACPI_EVAL_INPUT_BUFFER_COMPLEX pInput =
        (PACPI_EVAL_INPUT_BUFFER_COMPLEX)ExAllocatePoolZero(
            NonPagedPoolNx, inputSize, 'rsbU');
    if (!pInput)
        return STATUS_INSUFFICIENT_RESOURCES;

    pInput->Signature = ACPI_EVAL_INPUT_BUFFER_COMPLEX_SIGNATURE;
    RtlCopyMemory(pInput->MethodName, "USBR", 4);
    pInput->Size = 3 * sizeof(ACPI_METHOD_ARGUMENT);
    pInput->ArgumentCount = 3;

    PACPI_METHOD_ARGUMENT pArg = &pInput->Argument[0];
    ACPI_METHOD_SET_ARGUMENT_INTEGER(pArg, (ULONG)attach);

    pArg = ACPI_METHOD_NEXT_ARGUMENT(pArg);
    ACPI_METHOD_SET_ARGUMENT_INTEGER(pArg, (ULONG)cc_out);

    pArg = ACPI_METHOD_NEXT_ARGUMENT(pArg);
    ACPI_METHOD_SET_ARGUMENT_INTEGER(pArg, (ULONG)vbus);

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
              "USB state notified: attach=%d cc=%d vbus=%d\n",
              attach, cc_out, vbus);
    }

    ExFreePool(pInput);
    return status;
}