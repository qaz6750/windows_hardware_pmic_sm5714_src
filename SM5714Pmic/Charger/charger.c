#include "..\Common\registers.h"
#include "..\Common\spbhelper.h"
#include "charger.h"
#include "..\TypeC\typec.h"
#include "..\Common\driver.h"

static ULONG DebugLevel = 100;
static ULONG DebugCategories = DBG_INIT || DBG_PNP || DBG_IOCTL;

#define CHG_SPB SPB_CHARGER_INDEX

// ---- Charge enable / disable ------------------------------------------------

int
ChargerEnable(
    _In_ PDEVICE_CONTEXT pDevice,
    BOOLEAN                 enable
    )
{
    UCHAR mask;
    UCHAR val;

    mask = CHG_CNTL1_ENQ4FET;

    if (enable) {
        val = CHG_CNTL1_ENQ4FET;
        Print(DEBUG_LEVEL_INFO, DBG_INIT, "Start charging\n");
    } else {
        val = 0;
        Print(DEBUG_LEVEL_INFO, DBG_INIT, "Stop charging\n");
    }

    pDevice->IsChargingEnabled = enable;
    return update_reg8(pDevice, CHG_SPB, SM5714_CHG_REG_CNTL1, mask, val);
}

// ---- Auto-stop (charge termination on full) ---------------------------------

int
ChargerSetAutostop(
    _In_ PDEVICE_CONTEXT pDevice,
    BOOLEAN                 enable
    )
{
    UCHAR mask;
    UCHAR val;

    mask = CHG_AUTOSTOP_MASK;

    if (enable) {
        val = CHG_AUTOSTOP_MASK;
    } else {
        val = 0;
    }

    return update_reg8(pDevice, CHG_SPB, SM5714_CHG_REG_CHGCNTL4, mask, val);
}

// ---- Input current limit (VBUSCNTL) ----------------------------------------

int
ChargerSetInputCurrent(
    _In_ PDEVICE_CONTEXT pDevice,
    unsigned int         mA
    )
{
    UCHAR mask;
    UCHAR val;

    mask = 0x7F;

    if (mA < 100) {
        val = 0x00;
    } else {
        val = ((mA - 100) / 25) & 0x7F;
    }

    return update_reg8(pDevice, CHG_SPB, SM5714_CHG_REG_VBUSCNTL, mask, val);
}

// ---- Charging current (CHGCNTL2) -------------------------------------------

int
ChargerSetChargeCurrent(
    _In_ PDEVICE_CONTEXT pDevice,
    unsigned int         mA
    )
{
    UCHAR        mask;
    UCHAR        val;
    unsigned int uA;

    mask = 0xFF;
    uA   = mA * 1000;

    if (uA < 109375) {
        val = 0x07;
    } else if (uA > 3500000) {
        val = 0xE0;
    } else {
        val = (UCHAR)(7 + ((uA - 109375) / 15625)) & 0xFF;
    }

    return update_reg8(pDevice, CHG_SPB, SM5714_CHG_REG_CHGCNTL2, mask, val);
}

// ---- Float voltage (BATREG in CHGCNTL4) ------------------------------------
//  Range: 3700 mV .. 4620 mV, 10 mV per LSB.

int
ChargerSetFloatVoltage(
    _In_ PDEVICE_CONTEXT pDevice,
    unsigned int         mV
    )
{
    UCHAR mask;
    UCHAR val;

    mask = CHG_BATREG_MASK;

    if (mV < 3700) {
        val = 0x00;
    } else if (mV > 4620) {
        val = 0x3F;
    } else {
        val = (UCHAR)((mV - 3700) / 10);
    }

    Print(DEBUG_LEVEL_INFO, DBG_IOCTL,
          "Float voltage: %u mV (reg 0x%02x)\n", mV, val);
    return update_reg8(pDevice, CHG_SPB, SM5714_CHG_REG_CHGCNTL4, mask, val);
}

int
ChargerGetFloatVoltage(
    _In_ PDEVICE_CONTEXT pDevice
    )
{
    UCHAR data;
    int   ret;
    int   mV;

    data = 0;
    ret  = read_reg8(pDevice, CHG_SPB, SM5714_CHG_REG_CHGCNTL4, &data);
    if (ret < 0) {
        return ret;
    }

    mV = 3700 + ((int)(data & CHG_BATREG_MASK) * 10);
    return mV;
}

// ---- Top-off current (CHGCNTL5) --------------------------------------------

int
ChargerSetTopoffCurrent(
    _In_ PDEVICE_CONTEXT pDevice,
    unsigned int         mA
    )
{
    UCHAR mask;
    UCHAR val;

    mask = 0x1F;

    if (mA < 100) {
        val = 0x0;
    } else if (mA < 800) {
        val = (UCHAR)((mA - 100) / 25);
    } else {
        val = 0x1C;
    }

    return update_reg8(pDevice, CHG_SPB, SM5714_CHG_REG_CHGCNTL5, mask, val);
}

// ---- Trickle (pre-charge) current (CHGCNTL3) -------------------------------

int
ChargerSetTrickleCurrent(
    _In_ PDEVICE_CONTEXT pDevice,
    unsigned int         mA
    )
{
    UCHAR        mask;
    UCHAR        val;
    unsigned int uA;

    mask = CHG_TRICKLE_MASK;
    uA   = mA * 1000;

    if (uA < 109375) {
        val = 0x07;
    } else if (uA > 3500000) {
        val = 0xE0;
    } else {
        val = (UCHAR)(7 + ((uA - 109375) / 15625)) & 0xFF;
    }

    return update_reg8(pDevice, CHG_SPB, SM5714_CHG_REG_CHGCNTL3, mask, val);
}

// ---- Watchdog timer (WDTCNTL) ----------------------------------------------
//  Timer values: 0 = 10 s, 1 = 20 s, 2 = 40 s, 3 = 80 s.

int
ChargerSetWatchdogEnable(
    _In_ PDEVICE_CONTEXT pDevice,
    BOOLEAN                 enable
    )
{
    UCHAR mask;
    UCHAR val;

    mask = WDT_ENABLE;

    if (enable) {
        val = WDT_ENABLE;
        Print(DEBUG_LEVEL_INFO, DBG_IOCTL, "WDT enabled\n");
    } else {
        val = 0;
        Print(DEBUG_LEVEL_INFO, DBG_IOCTL, "WDT disabled\n");
    }

    return update_reg8(pDevice, CHG_SPB, SM5714_CHG_REG_WDTCNTL, mask, val);
}

int
ChargerSetWatchdogTimer(
    _In_ PDEVICE_CONTEXT pDevice,
    UCHAR                timer_idx
    )
{
    UCHAR mask;
    UCHAR val;

    if (timer_idx > 3) {
        return -1;
    }

    mask = WDT_TIMER_MASK;
    val  = (UCHAR)(timer_idx << 1);

    Print(DEBUG_LEVEL_INFO, DBG_IOCTL,
          "WDT timeout: %u s\n", 10u << timer_idx);
    return update_reg8(pDevice, CHG_SPB, SM5714_CHG_REG_WDTCNTL, mask, val);
}

int
ChargerKickWatchdog(
    _In_ PDEVICE_CONTEXT pDevice
    )
{
    return update_reg8(pDevice, CHG_SPB, SM5714_CHG_REG_WDTCNTL,
                       WDT_KICK, WDT_KICK);
}

int
ChargerClearWatchdog(
    _In_ PDEVICE_CONTEXT pDevice
    )
{
    return update_reg8(pDevice, CHG_SPB, SM5714_CHG_REG_WDTCNTL,
                       WDT_EXP_CLEAR, WDT_EXP_CLEAR);
}

// ---- AICL (automatic input current limit) ----------------------------------
//  CNTL1[6]          : enable
//  CHGCNTL11[7:6]    : threshold (0=4.35 V, 1=4.4 V, 2=4.5 V, 3=4.6 V)

int
ChargerSetAiclEnable(
    _In_ PDEVICE_CONTEXT pDevice,
    BOOLEAN                 enable
    )
{
    UCHAR mask;
    UCHAR val;

    mask = CHG_CNTL1_AICLEN_VBUS;

    if (enable) {
        val = CHG_CNTL1_AICLEN_VBUS;
        Print(DEBUG_LEVEL_INFO, DBG_IOCTL, "AICL enabled\n");
    } else {
        val = 0;
        Print(DEBUG_LEVEL_INFO, DBG_IOCTL, "AICL disabled\n");
    }

    return update_reg8(pDevice, CHG_SPB, SM5714_CHG_REG_CNTL1, mask, val);
}

int
ChargerSetAiclThreshold(
    _In_ PDEVICE_CONTEXT pDevice,
    UCHAR                threshold_idx
    )
{
    UCHAR mask;
    UCHAR val;

    if (threshold_idx > 3) {
        return -1;
    }

    mask = AICL_TH_MASK;
    val  = (UCHAR)(threshold_idx << 6);

    return update_reg8(pDevice, CHG_SPB, SM5714_CHG_REG_CHGCNTL11, mask, val);
}

// ---- ENQ4FET (charging soft-start / soft-stop) ------------------------------

int
ChargerSetSoftStart(
    _In_ PDEVICE_CONTEXT pDevice,
    BOOLEAN                 enable
    )
{
    return ChargerEnable(pDevice, enable);
}

// ---- Battery discharge OCP (CHGCNTL6) --------------------------------------
//  0=2.0 A .. 7=5.5 A (0.5 A steps).

int
ChargerSetDischargeLimit(
    _In_ PDEVICE_CONTEXT pDevice,
    UCHAR                limit_idx
    )
{
    UCHAR mask;
    UCHAR val;

    if (limit_idx > 7) {
        return -1;
    }

    mask = CHG_DISCHG_LIMIT_MASK;
    val  = (UCHAR)(limit_idx << 1);

    return update_reg8(pDevice, CHG_SPB, SM5714_CHG_REG_CHGCNTL6, mask, val);
}

// ---- Top-off timer (CHGCNTL7) ----------------------------------------------
//  0=15 min, 1=30 min, 2=45 min, 3=disabled.

int
ChargerSetTopoffTimer(
    _In_ PDEVICE_CONTEXT pDevice,
    UCHAR                tmr_idx
    )
{
    UCHAR mask;
    UCHAR val;

    if (tmr_idx > 3) {
        return -1;
    }

    mask = CHG_TOPOFF_TMR_MASK;
    val  = (UCHAR)(tmr_idx << 3);

    return update_reg8(pDevice, CHG_SPB, SM5714_CHG_REG_CHGCNTL7, mask, val);
}

// ---- LX slope (CHGCNTL8) ---------------------------------------------------
//  0=0.45 V/us, 1=0.3 V/us, 2=0.15 V/us, 3=0.1 V/us.

int
ChargerSetLxSlope(
    _In_ PDEVICE_CONTEXT pDevice,
    UCHAR                slope
    )
{
    if (slope > 3) {
        return -1;
    }

    return update_reg8(pDevice, CHG_SPB, SM5714_CHG_REG_CHGCNTL8,
                       CHG_LXSLOPE_MASK, slope);
}

// ---- Charger status snapshot -----------------------------------------------

NTSTATUS
ChargerReadStatus(
    _In_ PDEVICE_CONTEXT pDevice
    )
{
    NTSTATUS status;
    UCHAR    s1, s2, s3, s4, s5;

    status = read_reg8(pDevice, CHG_SPB, SM5714_CHG_REG_STATUS1, &s1);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    read_reg8(pDevice, CHG_SPB, SM5714_CHG_REG_STATUS2, &s2);
    read_reg8(pDevice, CHG_SPB, SM5714_CHG_REG_STATUS3, &s3);
    read_reg8(pDevice, CHG_SPB, SM5714_CHG_REG_STATUS4, &s4);
    read_reg8(pDevice, CHG_SPB, SM5714_CHG_REG_STATUS5, &s5);

    pDevice->ChgStatus = s1;

    Print(DEBUG_LEVEL_VERBOSE, DBG_IOCTL,
          "CHG STAT: %02x %02x %02x %02x %02x\n", s1, s2, s3, s4, s5);

    if (s1 & CHG_STAT1_VBUSPOK) {
        Print(DEBUG_LEVEL_VERBOSE, DBG_IOCTL, "  VBUS OK\n");
    }
    if (s1 & CHG_STAT1_VBUSOVP) {
        Print(DEBUG_LEVEL_ERROR, DBG_IOCTL, "  VBUS OVP!\n");
    }
    if (s1 & CHG_STAT1_THERMAL_SD) {
        Print(DEBUG_LEVEL_ERROR, DBG_IOCTL, "  Thermal shutdown!\n");
    }
    if (s1 & CHG_STAT1_BATOVP) {
        Print(DEBUG_LEVEL_ERROR, DBG_IOCTL, "  Battery OVP!\n");
    }

    if (s2 & CHG_STAT2_CHG_ON) {
        Print(DEBUG_LEVEL_VERBOSE, DBG_IOCTL, "  Charging\n");
    }
    if (s2 & CHG_STAT2_DONE) {
        Print(DEBUG_LEVEL_INFO, DBG_IOCTL, "  Charge done\n");
    }
    if (s2 & CHG_STAT2_TOPOFF) {
        Print(DEBUG_LEVEL_VERBOSE, DBG_IOCTL, "  Top-off phase\n");
    }
    if (s2 & CHG_STAT2_BATTERY_PRES) {
        Print(DEBUG_LEVEL_VERBOSE, DBG_IOCTL, "  Battery present\n");
    }
    if (s2 & CHG_STAT2_AICL_FAIL) {
        Print(DEBUG_LEVEL_INFO, DBG_IOCTL, "  AICL fail\n");
    }
    if (s2 & CHG_STAT2_WDT_EXP) {
        Print(DEBUG_LEVEL_ERROR, DBG_IOCTL, "  WDT expired!\n");
    }

    return STATUS_SUCCESS;
}

// ---- Adapt input current from Type-C CC RP advertisement -------------------
//  CC_STATUS[4:3]:  00 = Default (500 mA), 01 = 1.5 A, 10 = 3.0 A.

int
ChargerAdaptRpCurrent(
    _In_ PDEVICE_CONTEXT pDevice
    )
{
    NTSTATUS status;
    ULONG    rp_ma;
    UCHAR    cc_status;

    if (pDevice->SpbContextCount < 2) {
        return -1;
    }

    cc_status = 0;
    status = read_reg8(pDevice, SPB_USBPD_INDEX, SM5714_REG_CC_STATUS,
                       &cc_status);
    if (!NT_SUCCESS(status)) {
        return -1;
    }

    switch (cc_status & SM5714_CC_ADV_CURR) {
    case SM5714_RP_CURRENT_1_5A:
        rp_ma = 1500;
        break;
    case SM5714_RP_CURRENT_3_0A:
        rp_ma = 3000;
        break;
    default:
        rp_ma = 500;
        break;
    }

    pDevice->RpCurrentAdvertised = rp_ma;

    if (pDevice->InputCurrentLimit > rp_ma) {
        Print(DEBUG_LEVEL_INFO, DBG_IOCTL,
              "ICL capped: %lu -> %lu mA (from RP)\n",
              pDevice->InputCurrentLimit, rp_ma);
        ChargerSetInputCurrent(pDevice, rp_ma);
    }

    return 0;
}

// ---- Charge state query ----------------------------------------------------
//  Returns: 0 = idle, 1 = charging, 2 = done, 3 = top-off, <0 = error.

int
ChargerGetState(
    _In_ PDEVICE_CONTEXT pDevice
    )
{
    UCHAR s2;
    int   ret;

    s2  = 0;
    ret = read_reg8(pDevice, CHG_SPB, SM5714_CHG_REG_STATUS2, &s2);
    if (ret < 0) {
        return ret;
    }

    if (s2 & CHG_STAT2_DONE) {
        return 2;
    }
    if (s2 & CHG_STAT2_TOPOFF) {
        return 3;
    }
    if (s2 & CHG_STAT2_CHG_ON) {
        return 1;
    }

    return 0;
}

// ---- Bypass / factory mode (FACTORY1) --------------------------------------
//  Connects VBUS directly to battery.  Must NOT be used on production units.

int
ChargerSetBypass(
    _In_ PDEVICE_CONTEXT pDevice,
    BOOLEAN                 enable
    )
{
    UCHAR mask;
    UCHAR val;

    mask = FACTORY1_BYPASS_MODE;

    if (enable) {
        val = FACTORY1_BYPASS_MODE;
        Print(DEBUG_LEVEL_INFO, DBG_IOCTL, "Bypass mode enabled\n");
    } else {
        val = 0;
        Print(DEBUG_LEVEL_INFO, DBG_IOCTL, "Bypass mode disabled\n");
    }

    return update_reg8(pDevice, CHG_SPB, SM5714_CHG_REG_FACTORY1, mask, val);
}

// ---- Ship mode (CHGCNTL11) -------------------------------------------------
//  Lowest-power shipping state.

int
ChargerSetShipMode(
    _In_ PDEVICE_CONTEXT pDevice,
    BOOLEAN                 forced,
    UCHAR                auto_vref,
    UCHAR                auto_time
    )
{
    UCHAR val;

    if (auto_vref > 3 || auto_time > 3) {
        return -1;
    }

    val = 0;
    if (forced) {
        val |= SHIP_FORCED;
    }
    val |= (UCHAR)((auto_time & 0x03) << 3);
    val |= (UCHAR)((auto_vref & 0x03) << 1);

    Print(DEBUG_LEVEL_INFO, DBG_IOCTL,
          "Ship mode: forced=%d vref=%d time=%d\n",
          forced, auto_vref, auto_time);

    return update_reg8(pDevice, CHG_SPB, SM5714_CHG_REG_CHGCNTL11,
                       0xFF, val);
}

// ---- Probe: apply full charger configuration -------------------------------

int
ChargerProbe(
    _In_ PDEVICE_CONTEXT pDevice
    )
{
    int             ret;
    unsigned int    fv, icl, ichg, top, trkl;
    UCHAR           dischg, ttimer, lxslope;

    Print(DEBUG_LEVEL_INFO, DBG_INIT, "SM5714 charger probe start\n");

    //
    // Watchdog (40 s timeout, kick clear)
    //
    ChargerSetWatchdogEnable(pDevice, true);
    if (pDevice->WdtTimer) {
        ChargerSetWatchdogTimer(pDevice, pDevice->WdtTimer);
    } else {
        ChargerSetWatchdogTimer(pDevice, 2);
    }
    ChargerKickWatchdog(pDevice);
    ChargerClearWatchdog(pDevice);

    //
    // Float voltage
    //
    if (pDevice->FloatVoltage) {
        fv = pDevice->FloatVoltage;
    } else {
        fv = 4350;
    }
    ret = ChargerSetFloatVoltage(pDevice, fv);
    if (ret < 0) goto err;

    //
    // Auto-stop
    //
    ret = ChargerSetAutostop(pDevice, (BOOLEAN)pDevice->Autostop);
    if (ret < 0) goto err;

    //
    // Input current limit
    //
    if (pDevice->InputCurrentLimit) {
        icl = pDevice->InputCurrentLimit;
    } else {
        icl = 1500;
    }
    ret = ChargerSetInputCurrent(pDevice, icl);
    if (ret < 0) goto err;

    //
    // Charging current
    //
    if (pDevice->ChargingCurrent) {
        ichg = pDevice->ChargingCurrent;
    } else {
        ichg = 2000;
    }
    ret = ChargerSetChargeCurrent(pDevice, ichg);
    if (ret < 0) goto err;

    //
    // Top-off current
    //
    if (pDevice->TopoffCurrent) {
        top = pDevice->TopoffCurrent;
    } else {
        top = 200;
    }
    ret = ChargerSetTopoffCurrent(pDevice, top);
    if (ret < 0) goto err;

    //
    // Trickle current
    //
    if (pDevice->TrickleCurrent) {
        trkl = pDevice->TrickleCurrent;
    } else {
        trkl = 450;
    }
    ret = ChargerSetTrickleCurrent(pDevice, trkl);
    if (ret < 0) goto err;

    //
    // AICL
    //
    if (pDevice->AiclEnabled) {
        ChargerSetAiclEnable(pDevice, true);
    }

    //
    // Discharge OCP
    //
    if (pDevice->DischgLimit) {
        dischg = pDevice->DischgLimit;
    } else {
        dischg = 3;
    }
    ret = ChargerSetDischargeLimit(pDevice, dischg);
    if (ret < 0) goto err;

    //
    // Top-off timer
    //
    if (pDevice->TopoffTimer) {
        ttimer = pDevice->TopoffTimer;
    } else {
        ttimer = 0;
    }
    ret = ChargerSetTopoffTimer(pDevice, ttimer);
    if (ret < 0) goto err;

    //
    // LX slope
    //
    if (pDevice->LxSlope) {
        lxslope = pDevice->LxSlope;
    } else {
        lxslope = 0;
    }
    ret = ChargerSetLxSlope(pDevice, lxslope);
    if (ret < 0) goto err;

    Print(DEBUG_LEVEL_INFO, DBG_INIT, "SM5714 charger probe done\n");
    return 0;

err:
    Print(DEBUG_LEVEL_ERROR, DBG_INIT,
          "SM5714 charger probe FAILED: %d\n", ret);
    return ret;
}

// ---- Charger interrupt processing ------------------------------------------
//  Called from EvtChgInterruptIsr.  Reads and clears INT1-5, logs events.

NTSTATUS
ChargerProcessInterrupts(
    _In_ PDEVICE_CONTEXT pDevice
    )
{
    NTSTATUS status;
    UCHAR    i1 = 0, i2 = 0, i3 = 0, i4 = 0, i5 = 0;

    status = read_reg8(pDevice, CHG_SPB, SM5714_CHG_REG_INT1, &i1);
    if (!NT_SUCCESS(status)) {
        return status;
    }
    read_reg8(pDevice, CHG_SPB, SM5714_CHG_REG_INT2, &i2);
    read_reg8(pDevice, CHG_SPB, SM5714_CHG_REG_INT3, &i3);
    read_reg8(pDevice, CHG_SPB, SM5714_CHG_REG_INT4, &i4);
    read_reg8(pDevice, CHG_SPB, SM5714_CHG_REG_INT5, &i5);

    if (i1 || i2 || i3 || i4 || i5) {
        Print(DEBUG_LEVEL_VERBOSE, DBG_IOCTL,
              "CHG IRQ: %02x %02x %02x %02x %02x\n", i1, i2, i3, i4, i5);
    }

    //
    // INT1: VBUS events
    //
    if (i1 & CHG_INT1_VBUSPOK) {
        pDevice->VbusPresent = TRUE;
        Print(DEBUG_LEVEL_INFO, DBG_IOCTL, "  VBUS OK\n");
    }
    if (i1 & CHG_INT1_VBUSOVP) {
        Print(DEBUG_LEVEL_ERROR, DBG_IOCTL, "  VBUS OVP!\n");
        ChargerEnable(pDevice, false);
    }
    if (i1 & CHG_INT1_VBUSUVLO) {
        pDevice->VbusPresent = FALSE;
        Print(DEBUG_LEVEL_INFO, DBG_IOCTL, "  VBUS UVLO\n");
    }

    //
    // INT2: charging state changes
    //
    if (i2 & CHG_INT2_CHGON) {
        pDevice->IsChargingEnabled = TRUE;
        Print(DEBUG_LEVEL_INFO, DBG_IOCTL, "  Charging started\n");
    }
    if (i2 & CHG_INT2_DONE) {
        Print(DEBUG_LEVEL_INFO, DBG_IOCTL, "  Charge done\n");
    }
    if (i2 & CHG_INT2_TOPOFF) {
        Print(DEBUG_LEVEL_INFO, DBG_IOCTL, "  Top-off phase\n");
    }
    if (i2 & CHG_INT2_NOBAT) {
        Print(DEBUG_LEVEL_ERROR, DBG_IOCTL, "  No battery!\n");
        ChargerEnable(pDevice, false);
    }
    if (i2 & CHG_INT2_BATOVP) {
        Print(DEBUG_LEVEL_ERROR, DBG_IOCTL, "  Battery OVP!\n");
        ChargerEnable(pDevice, false);
    }
    if (i2 & CHG_INT2_AICL) {
        Print(DEBUG_LEVEL_INFO, DBG_IOCTL, "  AICL active\n");
    }

    //
    // INT3: system / thermal
    //
    if (i3 & CHG_INT3_OTGFAIL) {
        Print(DEBUG_LEVEL_ERROR, DBG_IOCTL, "  OTG boost fail!\n");
    }
    if (i3 & CHG_INT3_THEMREG) {
        Print(DEBUG_LEVEL_INFO, DBG_IOCTL, "  Thermal regulation active\n");
    }

    //
    // INT4: constant-voltage / boost
    //
    if (i4 & CHG_INT4_CVMODE) {
        Print(DEBUG_LEVEL_VERBOSE, DBG_IOCTL, "  CV mode\n");
    }
    if (i4 & CHG_INT4_BOOSTPOK) {
        Print(DEBUG_LEVEL_INFO, DBG_IOCTL, "  Boost OK\n");
    }

    return STATUS_SUCCESS;
}

// ---- BC1.2 charger type detection ------------------------------------------
//  Reads BC12_DEV_TYPE (0x88) on USBPD I2C to identify DCP / CDP / SDP.

NTSTATUS
ChargerDetectBc12(
    _In_  PDEVICE_CONTEXT pDevice,
    _Out_ UCHAR          *type
    )
{
    NTSTATUS status;

    *type = 0;

    if (pDevice->SpbContextCount < 2) {
        return STATUS_NOT_SUPPORTED;
    }

    status = read_reg8(pDevice, SPB_USBPD_INDEX,
                       SM5714_REG_BC12_DEV_TYPE, type);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    Print(DEBUG_LEVEL_INFO, DBG_IOCTL,
          "BC1.2 type: 0x%02x (%s)\n",
          *type,
          (*type == BC12_TYPE_DCP) ? "DCP" :
          (*type == BC12_TYPE_CDP) ? "CDP" :
          (*type == BC12_TYPE_SDP) ? "SDP" : "unknown");

    return STATUS_SUCCESS;
}

// ---- VBUS voltage reading (via MUIC I2C) -----------------------------------

NTSTATUS
ChargerReadVbusVoltage(
    _In_  PDEVICE_CONTEXT pDevice,
    _Out_ ULONG          *mV
    )
{
    NTSTATUS status;
    UCHAR    raw;

    *mV = 0;

    if (pDevice->SpbContextCount < 3) {
        return STATUS_NOT_SUPPORTED;
    }

    //
    // Trigger a VBUS ADC conversion
    //
    status = write_reg8(pDevice, SPB_MUIC_INDEX,
                        SM5714_MUIC_REG_AFCCNTL,
                        MUIC_AFCCTRL_VBUS_READ);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    //
    // Wait for conversion (poll INT2)
    //
    {
        LARGE_INTEGER delay;
        int           retries = 5;

        delay.QuadPart = -20000;  // 2 ms
        while (retries > 0) {
            KeDelayExecutionThread(KernelMode, FALSE, &delay);
            status = read_reg8(pDevice, SPB_MUIC_INDEX,
                               SM5714_MUIC_REG_INT2, &raw);
            if (NT_SUCCESS(status) && (raw & MUIC_INT2_VBUS_UPDATE)) {
                break;
            }
            retries--;
        }
    }

    //
    // Read result (0x0C: VBUS_VOLTAGE, units of 100mV)
    //
    status = read_reg8(pDevice, SPB_MUIC_INDEX,
                       SM5714_MUIC_REG_VBUS_VOLTAGE, &raw);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    *mV = (ULONG)raw * 100;

    Print(DEBUG_LEVEL_VERBOSE, DBG_IOCTL,
          "VBUS voltage: %lu mV\n", *mV);

    return STATUS_SUCCESS;
}

// ---- Quick Charge 2.0 / AFC voltage selection (via MUIC) -------------------

int
ChargerSetQc20Voltage(
    _In_ PDEVICE_CONTEXT pDevice,
    UCHAR                voltage
    )
{
    UCHAR ctrl;

    if (pDevice->SpbContextCount < 3) {
        return -1;
    }

    switch (voltage) {
    case 5:
        ctrl = MUIC_AFCCTRL_QC20_5V;
        break;
    case 9:
        ctrl = MUIC_AFCCTRL_QC20_9V;
        break;
    case 12:
        ctrl = MUIC_AFCCTRL_QC20_12V;
        break;
    default:
        return -1;
    }

    Print(DEBUG_LEVEL_INFO, DBG_IOCTL,
          "QC 2.0: requesting %u V\n", voltage);

    return write_reg8(pDevice, SPB_MUIC_INDEX,
                      SM5714_MUIC_REG_AFCCNTL,
                      ctrl | MUIC_AFCCTRL_ENAFC);
}

int
ChargerDisableQc20(
    _In_ PDEVICE_CONTEXT pDevice
    )
{
    if (pDevice->SpbContextCount < 3) {
        return -1;
    }

    Print(DEBUG_LEVEL_INFO, DBG_IOCTL, "QC 2.0: reverting to 5 V\n");
    return write_reg8(pDevice, SPB_MUIC_INDEX,
                      SM5714_MUIC_REG_AFCCNTL, 0);
}