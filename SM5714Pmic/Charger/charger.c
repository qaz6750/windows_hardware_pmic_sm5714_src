#include "..\Common\registers.h"
#include "..\Common\spbhelper.h"
#include "charger.h"
#include "..\Common\driver.h"

static ULONG DebugLevel = 100;
static ULONG DebugCatagories = DBG_INIT || DBG_PNP || DBG_IOCTL;

int set_autostop(_In_ PDEVICE_CONTEXT pDevice, bool enable)
{
    // bit 6 controls autostop.
    UCHAR mask = (0x1 << 6);
    UCHAR val = (enable ? (0x1 << 6) : 0);
    return update_reg8(pDevice, 0, 0x1A, mask, val);
}

int set_input_current_limit(_In_ PDEVICE_CONTEXT pDevice, unsigned int mA)
{
    UCHAR mask = 0x7F;
    UCHAR val;
    unsigned char offset;

    if (mA < 100)
        offset = 0x00;
    else
        offset = ((mA - 100) / 25) & 0x7F;

    val = offset;

    return update_reg8(pDevice, 0, SM5714_CHG_REG_VBUSCNTL, mask, val);
}

int set_charging_current(_In_ PDEVICE_CONTEXT pDevice, unsigned int mA)
{
    UCHAR mask = 0xFF;
    UCHAR val;
    unsigned char offset;
    unsigned int uA;

    uA = mA * 1000;         // mA to uA

    if (uA < 109375)
        offset = 0x07;
    else if (uA > 3500000)
        offset = 0xE0;
    else
        offset = (7 + ((uA - 109375) / 15625)) & 0xFF;

    val = offset;

    return update_reg8(pDevice, 0, SM5714_CHG_REG_CHGCNTL2, mask, val);
}

int set_topoff_current(_In_ PDEVICE_CONTEXT pDevice, unsigned int mA)
{
    UCHAR mask = 0x1F;
    UCHAR val;
    unsigned char offset;

    if (mA < 100)
        offset = 0x0;
    else if (mA < 800)
        offset = (mA - 100) / 25;
    else
        offset = 0x1C;

    val = offset;

    return update_reg8(pDevice, 0, SM5714_CHG_REG_CHGCNTL5, mask, val);
}

int charger_probe(_In_ PDEVICE_CONTEXT pDevice)
{
    // Configure charging parameters
    set_autostop(pDevice, (bool)pDevice->Autostop);
    set_input_current_limit(pDevice, pDevice->InputCurrentLimit);
    set_charging_current(pDevice, pDevice->ChargingCurrent);
    set_topoff_current(pDevice, pDevice->TopoffCurrent);
    return 0; // fix this
}

int enable_charging(_In_ PDEVICE_CONTEXT pDevice, bool enable)
{
    UCHAR mask = (0x1 << 3);  // mask for bit 3 = 0x08
    if (enable) {
        UCHAR val = (1 << 3);     // Set bit 3 to 1
        Print(DEBUG_LEVEL_INFO, DBG_INIT, "Start charging\n");
        return update_reg8(pDevice, 0, SM5714_CHG_REG_CNTL1, mask, val);
    }
    else {
        UCHAR val = 0; // Clear bit 3 to disable charging
        Print(DEBUG_LEVEL_INFO, DBG_INIT, "Stop charging\n");
        return update_reg8(pDevice, 0, SM5714_CHG_REG_CNTL1, mask, val);
    }
}