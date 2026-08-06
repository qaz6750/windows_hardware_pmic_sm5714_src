#ifndef __SM5714_H__
#define __SM5714_H__

//
// Charger Register definitions
//
enum chg_status_regs {
    SM5714_CHG_REG_INT1        = 0x01,
    SM5714_CHG_REG_INT2        = 0x02,
    SM5714_CHG_REG_INT3        = 0x03,
    SM5714_CHG_REG_INT4        = 0x04,
    SM5714_CHG_REG_INT5        = 0x05,
	SM5714_CHG_REG_INTMSK1     = 0x07,
	SM5714_CHG_REG_INTMSK2     = 0x08,
	SM5714_CHG_REG_INTMSK3     = 0x09,
	SM5714_CHG_REG_INTMSK4     = 0x0A,
	SM5714_CHG_REG_INTMSK5     = 0x0B,
    SM5714_CHG_REG_STATUS1      = 0x0D,
    SM5714_CHG_REG_STATUS2      = 0x0E,
    SM5714_CHG_REG_STATUS3      = 0x0F,
    SM5714_CHG_REG_STATUS4      = 0x10,
    SM5714_CHG_REG_STATUS5      = 0x11,
};

enum chg_cntl_regs {
    SM5714_CHG_REG_CNTL1 = 0x13,
    SM5714_CHG_REG_CNTL2 = 0x14,       // OP_MODE register (bits [3:0])
    SM5714_CHG_REG_VBUSCNTL = 0x15,
    SM5714_CHG_REG_CHGCNTL1 = 0x17,
    SM5714_CHG_REG_CHGCNTL2 = 0x18,
    SM5714_CHG_REG_CHGCNTL3 = 0x19,    // Trickle charging current
    SM5714_CHG_REG_CHGCNTL4 = 0x1A,    // BATREG (float voltage) + AUTOSTOP
    SM5714_CHG_REG_CHGCNTL5 = 0x1B,    // Topoff current
    SM5714_CHG_REG_CHGCNTL6 = 0x1C,    // Discharge current limit (OCP)
    SM5714_CHG_REG_CHGCNTL7 = 0x1D,    // Topoff timer
    SM5714_CHG_REG_CHGCNTL8 = 0x1E,    // LX slope control
    SM5714_CHG_REG_WDTCNTL  = 0x22,    // Watchdog timer control
    SM5714_CHG_REG_BSTCNTL1 = 0x23,    // BSTOUT[3:0] + OTG_CURRENT[7:6]
    SM5714_CHG_REG_FACTORY1  = 0x25,   // Factory / Bypass control
    SM5714_CHG_REG_FACTORY2  = 0x26,
    SM5714_CHG_REG_CHGCNTL11 = 0x46,   // AICL threshold + Ship mode control
    SM5714_CHG_REG_DEVICEID  = 0x50,
};

//
// Charger STATUS1 bit masks
//
#define CHG_STAT1_VBUSPOK      (1 << 0)
#define CHG_STAT1_VBUSUVLO     (1 << 1)
#define CHG_STAT1_VBUSOVP      (1 << 2)
#define CHG_STAT1_VBUSLIMIT    (1 << 3)

//
// Charger STATUS2 bit masks
//
#define CHG_STAT2_AICL         (1 << 0)
#define CHG_STAT2_BATOVP       (1 << 1)
#define CHG_STAT2_NOBAT        (1 << 2)
#define CHG_STAT2_CHG_ON       (1 << 3)
#define CHG_STAT2_Q4FULLON     (1 << 4)
#define CHG_STAT2_TOPOFF       (1 << 5)
#define CHG_STAT2_DONE         (1 << 6)
#define CHG_STAT2_WDT_EXP      (1 << 7)

//
// Charger STATUS3 bit masks
//
#define CHG_STAT3_THERMREG     (1 << 0)
#define CHG_STAT3_THERMSHDN    (1 << 1)
#define CHG_STAT3_OTGFAIL      (1 << 2)
#define CHG_STAT3_DISLIMIT     (1 << 3)
#define CHG_STAT3_TRICKLE_TMR  (1 << 4)
#define CHG_STAT3_FAST_TMR     (1 << 5)
#define CHG_STAT3_nENQ4        (1 << 6)
#define CHG_STAT3_VSYSOVP      (1 << 7)

//
// Charger CNTL1 bit masks
//
#define CHG_CNTL1_ENQ4FET      (1 << 3)   // Enable charging FET
#define CHG_CNTL1_AICLEN_VBUS  (1 << 6)   // AICL enable for VBUS

//
// Charger CHGCNTL4 bit masks
//
#define CHG_BATREG_MASK         0x3F      // Bits [5:0]: float voltage
#define CHG_AUTOSTOP_MASK       (1 << 6)  // Bit 6: autostop

//
// Charger CHGCNTL6 bit masks (Discharge current limit)
//
#define CHG_DISCHG_LIMIT_MASK   (0x7 << 1)  // Bits [3:1]

//
// Charger CHGCNTL7 bit masks (Topoff timer)
//
#define CHG_TOPOFF_TMR_MASK     (0x3 << 3)  // Bits [4:3]

//
// Charger CHGCNTL8 bit masks (LX slope)
//
#define CHG_LXSLOPE_MASK        0x03       // Bits [1:0]

//
// Watchdog timer control (WDTCNTL)
//
#define WDT_ENABLE              (1 << 0)
#define WDT_TIMER_MASK          (0x3 << 1)  // Bits [2:1]
#define WDT_KICK                (1 << 3)
#define WDT_EXP_CLEAR           (1 << 6)

//
// AICL threshold (CHGCNTL11)
//
#define AICL_TH_MASK            (0x3 << 6)  // Bits [7:6]

//
// Ship mode control (CHGCNTL11)
//
#define SHIP_FORCED             (1 << 5)
#define SHIP_AUTO_TIME_MASK     (0x3 << 3)  // Bits [4:3]
#define SHIP_AUTO_VREF_MASK     (0x3 << 1)  // Bits [2:1]

//
// Factory control (FACTORY1 bit masks)
//
#define FACTORY1_BYPASS_MODE    (1 << 1)    // Bypass / factory mode enable

//
// Trickle current (CHGCNTL3)
//
#define CHG_TRICKLE_MASK        0xFF        // Bits [7:0]: trickle charge current

//
// Reset control (SYS_CNTL)
//
#define SYS_CNTL_SWRST          (1 << 7)    // Software reset

//
// Charger operation modes (CNTL2 bits [3:0])
//
#define OP_MODE_SUSPEND         0x00
#define OP_MODE_CHG_ON_VBUS     0x05
#define OP_MODE_USB_OTG         0x07
#define OP_MODE_FLASH_BOOST     0x08
#define OP_MODE_MASK            0x0F

//
// Boost output voltage (BSTCNTL1 bits [3:0])
//
#define BSTOUT_5100mV           0x06

//
// OTG current limit (BSTCNTL1 bits [7:6])
//
#define OTG_CURRENT_500mA       (0x0 << 6)
#define OTG_CURRENT_900mA       (0x1 << 6)
#define OTG_CURRENT_1200mA      (0x2 << 6)
#define OTG_CURRENT_1500mA      (0x3 << 6)

//
// Type-C / USBPD Register definitions
//
enum typec_pdic_rid {
	REG_RID_UNDF = 0x00,
	REG_RID_255K = 0x03,
	REG_RID_301K = 0x04,
	REG_RID_523K = 0x05,
	REG_RID_619K = 0x06,
	REG_RID_OPEN = 0x07,
	REG_RID_MAX = 0x08,
};

enum typec_usbpd_reg {
	SM5714_REG_INT1 = 0x01,
	SM5714_REG_INT2 = 0x02,
	SM5714_REG_INT3 = 0x03,
	SM5714_REG_INT4 = 0x04,
	SM5714_REG_INT5 = 0x05,
	SM5714_REG_INT_MASK1 = 0x06,
	SM5714_REG_INT_MASK2 = 0x07,
	SM5714_REG_INT_MASK3 = 0x08,
	SM5714_REG_INT_MASK4 = 0x09,
	SM5714_REG_INT_MASK5 = 0x0A,
	SM5714_REG_STATUS1 = 0x0B,
	SM5714_REG_STATUS2 = 0x0C,
	SM5714_REG_STATUS3 = 0x0D,
	SM5714_REG_STATUS4 = 0x0E,
	SM5714_REG_STATUS5 = 0x0F,
	SM5714_REG_JIGON_CONTROL = 0x17,
	SM5714_REG_FACTORY = 0x18,
	SM5714_REG_ADC_CNTL1 = 0x19,
	SM5714_REG_ADC_CNTL2 = 0x1A,
	SM5714_REG_SYS_CNTL = 0x1B,
	SM5714_REG_COMP_CNTL = 0x1C,
	SM5714_REG_CLK_CNTL = 0x1D,
	SM5714_REG_USBK_CNTL = 0x1E,
	SM5714_REG_CORR_CNTL1 = 0x20,
	SM5714_REG_CORR_CNTL4 = 0x23,
	SM5714_REG_CORR_CNTL5 = 0x24,
	SM5714_REG_CORR_CNTL6 = 0x25,
	SM5714_REG_CC_STATUS = 0x28,
	SM5714_REG_CC_CNTL1 = 0x29,
	SM5714_REG_CC_CNTL2 = 0x2A,
	SM5714_REG_CC_CNTL3 = 0x2B,
	SM5714_REG_CC_CNTL4 = 0x2C,
	SM5714_REG_CC_CNTL5 = 0x2D,
	SM5714_REG_CC_CNTL6 = 0x2E,
	SM5714_REG_CC_CNTL7 = 0x2F,
	SM5714_REG_CABLE_POL_SEL = 0x33,
	SM5714_REG_GEN_TMR_L = 0x35,
	SM5714_REG_GEN_TMR_U = 0x36,
	SM5714_REG_PD_CNTL1 = 0x38,
	SM5714_REG_PD_CNTL2 = 0x39,
	SM5714_REG_PD_CNTL4 = 0x3B,
	SM5714_REG_PD_CNTL5 = 0x3C,
	SM5714_REG_RX_SRC = 0x41,
	SM5714_REG_RX_HEADER_00 = 0x42,
	SM5714_REG_RX_HEADER_01 = 0x43,
	SM5714_REG_RX_PAYLOAD = 0x44,
	SM5714_REG_RX_BUF = 0x5E,
	SM5714_REG_RX_BUF_ST = 0x5F,
	SM5714_REG_TX_HEADER_00 = 0x60,
	SM5714_REG_TX_HEADER_01 = 0x61,
	SM5714_REG_TX_PAYLOAD = 0x62,
	SM5714_REG_TX_BUF_CTRL = 0x63,
	SM5714_REG_TX_REQ = 0x7E,
	SM5714_REG_BC12_DEV_TYPE = 0x88,
	SM5714_REG_TA_STATUS = 0x89,
	SM5714_REG_CORR_CNTL9 = 0x92,
	SM5714_REG_CORR_TH3 = 0xA4,
	SM5714_REG_CORR_TH6 = 0xA7,
	SM5714_REG_CORR_TH7 = 0xA8,
	SM5714_REG_CORR_OPT4 = 0xC8,
	SM5714_REG_PROBE0 = 0xD0,
	SM5714_REG_PD_STATE0 = 0xD5,
	SM5714_REG_PD_STATE1 = 0xD6,
	SM5714_REG_PD_STATE2 = 0xD7,
	SM5714_REG_PD_STATE3 = 0xD8,
	SM5714_REG_PD_STATE4 = 0xD9,
	SM5714_REG_PD_STATE5 = 0xDA
};

//
// INT/STATUS register bit masks
//
#define SM5714_INT_STATUS1_VBUSPOK       (1 << 0)
#define SM5714_INT_STATUS1_TMR_EXP       (1 << 1)
#define SM5714_INT_STATUS1_ATTACH        (1 << 3)
#define SM5714_INT_STATUS1_DETACH        (1 << 4)
#define SM5714_INT_STATUS1_ABNORMAL_DEV  (1 << 7)

#define SM5714_INT_STATUS2_PD_RID_DETECT (1 << 0)
#define SM5714_INT_STATUS2_VCONN_DISCHG  (1 << 2)
#define SM5714_INT_STATUS2_SRC_ADV_CHG   (1 << 4)
#define SM5714_INT_STATUS2_VBUS_0V       (1 << 5)

#define SM5714_INT_STATUS3_WATER         (1 << 0)
#define SM5714_INT_STATUS3_VCONN_OCP     (1 << 3)
#define SM5714_INT_STATUS3_WATER_RLS     (1 << 4)

#define SM5714_INT_STATUS4_RX_DONE       (1 << 0)
#define SM5714_INT_STATUS4_TX_DONE       (1 << 1)
#define SM5714_INT_STATUS4_TX_SOP_ERR    (1 << 2)
#define SM5714_INT_STATUS4_PRL_RST_DONE  (1 << 4)
#define SM5714_INT_STATUS4_HRST_RCVED    (1 << 5)
#define SM5714_INT_STATUS4_HCRST_DONE    (1 << 6)
#define SM5714_INT_STATUS4_TX_DISCARD    (1 << 7)

#define SM5714_INT_STATUS5_SBU1_OVP      (1 << 0)
#define SM5714_INT_STATUS5_SBU2_OVP      (1 << 1)
#define SM5714_INT_STATUS5_JIG_CASE_ON   (1 << 3)
#define SM5714_INT_STATUS5_DPDM_SHORT    (1 << 6)
#define SM5714_INT_STATUS5_CC_ABNORMAL   (1 << 7)

//
// CC_STATUS register (0x28) field masks
//
#define SM5714_CC_ATTACH_TYPE            0x07
#define SM5714_CC_ADV_CURR               0x18
#define SM5714_CC_CABLE_FLIP             0x20

// Attach type values (CC_STATUS bits [2:0])
#define SM5714_ATTACH_NONE               0x00
#define SM5714_ATTACH_SOURCE             0x01   // We are Sink (charger connected)
#define SM5714_ATTACH_SINK               0x02   // We are Source (OTG device connected)
#define SM5714_ATTACH_AUDIO              0x03   // Audio accessory (Ra/Ra)
#define SM5714_ATTACH_UN_ORI_DEBUG_SOURCE 0x04  // Debug accessory, source, unoriented
#define SM5714_ATTACH_ORI_DEBUG_SOURCE   0x05   // Debug accessory, source, oriented

// RP current advertisement (CC_STATUS bits [4:3])
#define SM5714_RP_CURRENT_DEFAULT        0x00   // 500mA
#define SM5714_RP_CURRENT_1_5A           0x08   // 1.5A
#define SM5714_RP_CURRENT_3_0A           0x10   // 3.0A

//
// Interrupt masks for enabling events we care about
//
#define USBPD_ENABLED_INT1   (SM5714_INT_STATUS1_VBUSPOK | \
                              SM5714_INT_STATUS1_ATTACH  | \
                              SM5714_INT_STATUS1_DETACH)

#define USBPD_ENABLED_INT2   (SM5714_INT_STATUS2_SRC_ADV_CHG | \
                              SM5714_INT_STATUS2_VBUS_0V)

#define USBPD_ENABLED_INT3   (SM5714_INT_STATUS3_WATER | \
                              SM5714_INT_STATUS3_WATER_RLS)

#define USBPD_ENABLED_INT4   (SM5714_INT_STATUS4_RX_DONE      | \
                              SM5714_INT_STATUS4_TX_DONE      | \
                              SM5714_INT_STATUS4_TX_SOP_ERR   | \
                              SM5714_INT_STATUS4_PRL_RST_DONE | \
                              SM5714_INT_STATUS4_HRST_RCVED   | \
                              SM5714_INT_STATUS4_HCRST_DONE   | \
                              SM5714_INT_STATUS4_TX_DISCARD)

#define USBPD_ENABLED_INT5   (SM5714_INT_STATUS5_SBU1_OVP | \
                              SM5714_INT_STATUS5_SBU2_OVP | \
                              SM5714_INT_STATUS5_CC_ABNORMAL)

//
// SM5714 TX request values
//
#define SM5714_REG_MSG_SEND_TX_SOP_REQ    0x07
#define SM5714_REG_MSG_SEND_TX_SOPP_REQ   0x17
#define SM5714_REG_MSG_SEND_TX_SOPPP_REQ  0x27

//
// USB Power Delivery / VDM constants used for DisplayPort Alt Mode.
//
#define USBPD_REV_20                      1
#define USBPD_DFP                         1
#define USBPD_SINK                        0
#define USBPD_SOURCE                      1
#define USBPD_VENDOR_DEFINED              0x0F

#define PD_SID_DISPLAYPORT                0xFF01
#define VDM_TYPE_STRUCTURED               1
#define VDM_COMMAND_TYPE_INITIATOR        0
#define VDM_COMMAND_TYPE_RESPONDER_ACK    1
#define VDM_DISCOVER_IDENTITY             1
#define VDM_DISCOVER_SVIDS                2
#define VDM_DISCOVER_MODES                3
#define VDM_ENTER_MODE                    4
#define VDM_EXIT_MODE                     5
#define VDM_ATTENTION                     6
#define VDM_DISPLAYPORT_STATUS_UPDATE     0x10
#define VDM_DISPLAYPORT_CONFIGURE         0x11

#define DP_PORT_CONNECTED_NONE            0
#define DP_PORT_CONNECTED_DFP_D           1
#define DP_PORT_CONNECTED_UFP_D           2
#define DP_PORT_CONNECTED_BOTH            3
#define DP_CONFIG_USB                     0
#define DP_CONFIG_USB_U_AS_DFP_D          1
#define DP_CONFIG_USB_U_AS_UFP_D          2
#define DP_PROTOCOL_DP_V13                1

#define DP_PIN_ASSIGNMENT_A               0x01
#define DP_PIN_ASSIGNMENT_B               0x02
#define DP_PIN_ASSIGNMENT_C               0x04
#define DP_PIN_ASSIGNMENT_D               0x08
#define DP_PIN_ASSIGNMENT_E               0x10
#define DP_PIN_ASSIGNMENT_F               0x20

//
// Charger interrupt bit masks (CHG_REG_INT1-5)
//
#define CHG_INT1_VBUSLIMIT     (1 << 3)
#define CHG_INT1_VBUSOVP       (1 << 2)
#define CHG_INT1_VBUSUVLO      (1 << 1)
#define CHG_INT1_VBUSPOK       (1 << 0)

#define CHG_INT2_WDTMROFF      (1 << 7)
#define CHG_INT2_DONE          (1 << 6)
#define CHG_INT2_TOPOFF        (1 << 5)
#define CHG_INT2_Q4FULLON      (1 << 4)
#define CHG_INT2_CHGON         (1 << 3)
#define CHG_INT2_NOBAT         (1 << 2)
#define CHG_INT2_BATOVP        (1 << 1)
#define CHG_INT2_AICL          (1 << 0)

#define CHG_INT3_VSYSOVP       (1 << 7)
#define CHG_INT3_nENQ4         (1 << 6)
#define CHG_INT3_FASTTMROFF    (1 << 5)
#define CHG_INT3_TRICKLETMROFF (1 << 4)
#define CHG_INT3_DISLIMIT      (1 << 3)
#define CHG_INT3_OTGFAIL       (1 << 2)
#define CHG_INT3_THEMSHDN      (1 << 1)
#define CHG_INT3_THEMREG       (1 << 0)

#define CHG_INT4_CVMODE        (1 << 7)
#define CHG_INT4_BOOSTPOK      (1 << 1)
#define CHG_INT4_BOOSTPOK_NG   (1 << 0)

#define CHG_INT5_ABSTMROFF     (1 << 6)
#define CHG_INT5_FLEDOPEN      (1 << 1)
#define CHG_INT5_FLEDSHORT     (1 << 0)

//
// Charger IRQ mask values (0 = enabled, 1 = masked)
//
#define CHG_INT1_MASK_VALUE    ((UCHAR)~(CHG_INT1_VBUSPOK | \
                                         CHG_INT1_VBUSOVP | \
                                         CHG_INT1_VBUSUVLO))
#define CHG_INT2_MASK_VALUE    ((UCHAR)~(CHG_INT2_DONE  | \
                                         CHG_INT2_TOPOFF | \
                                         CHG_INT2_CHGON  | \
                                         CHG_INT2_NOBAT  | \
                                         CHG_INT2_BATOVP | \
                                         CHG_INT2_AICL))
#define CHG_INT3_MASK_VALUE    ((UCHAR)~(CHG_INT3_OTGFAIL | \
										 CHG_INT3_THEMSHDN | \
										 CHG_INT3_THEMREG))
#define CHG_INT4_MASK_VALUE    0xFF
#define CHG_INT5_MASK_VALUE    0xFF

//
// MUIC registers (I2C addr 0x4A >> 1 = 0x25)
//
enum muic_regs {
    SM5714_MUIC_REG_DEVICEID     = 0x00,
    SM5714_MUIC_REG_INT1         = 0x01,
    SM5714_MUIC_REG_INT2         = 0x02,
    SM5714_MUIC_REG_INTMASK1     = 0x03,
    SM5714_MUIC_REG_INTMASK2     = 0x04,
    SM5714_MUIC_REG_CNTL         = 0x05,
    SM5714_MUIC_REG_MANUAL_SW    = 0x06,
    SM5714_MUIC_REG_DEVICETYPE1  = 0x07,
    SM5714_MUIC_REG_DEVICETYPE2  = 0x08,
    SM5714_MUIC_REG_AFCCNTL      = 0x09,
    SM5714_MUIC_REG_AFCTXD       = 0x0A,
    SM5714_MUIC_REG_AFCSTATUS    = 0x0B,
    SM5714_MUIC_REG_VBUS_VOLTAGE = 0x0C,
    SM5714_MUIC_REG_RESET        = 0x1E,
};

//
// DEVICETYPE1 values (BC1.2 + proprietary)
//
#define MUIC_DEVTYPE1_DCP         (1 << 0)
#define MUIC_DEVTYPE1_CDP         (1 << 1)
#define MUIC_DEVTYPE1_SDP         (1 << 2)
#define MUIC_DEVTYPE1_OTG         (1 << 3)
#define MUIC_DEVTYPE1_QC20_TA     (1 << 4)

//
// AFCCTRL register bits
//
#define MUIC_AFCCTRL_ENAFC        (1 << 0)
#define MUIC_AFCCTRL_VBUS_READ    (1 << 1)
#define MUIC_AFCCTRL_QC20_5V      0x00
#define MUIC_AFCCTRL_QC20_9V      (1 << 6)
#define MUIC_AFCCTRL_QC20_12V     (1 << 7)

//
// MUIC INT2 bits
//
#define MUIC_INT2_AFC_ACCEPTED    (1 << 0)
#define MUIC_INT2_AFC_ERROR       (1 << 1)
#define MUIC_INT2_VBUS_UPDATE     (1 << 2)

//
// BC1.2 device type (USBPD register 0x88)
//
#define BC12_TYPE_DCP             0x01
#define BC12_TYPE_CDP             0x02
#define BC12_TYPE_SDP             0x04
#define BC12_TYPE_PROPRIETARY     0x08

//
// USB PD Sink PDOs from DTS config:
//   max_power=5000mW, max_voltage=6000mV, max_current=2000mA
//
#define PD_SINK_PDO_5V_500MA     0x0001912C
#define PD_SINK_PDO_5V_1500MA    0x0002F12C
#define PD_SINK_PDO_5V_2000MA    0x0003E12C
#define PD_SINK_PDO_9V_1500MA    0x0002F164
#define PD_SINK_PDO_9V_2000MA    0x0003E164

//
// USB PD control
//
#define PD_CNTL1_SEND_PDO         (1 << 0)
#define PD_CNTL2_DATA_ROLE_UFP    0x00
#define PD_CNTL2_DATA_ROLE_DFP    0x03
#define PD_CNTL2_POWER_ROLE_SINK  0x00
#define PD_CNTL2_POWER_ROLE_SRC   0x0C

#endif