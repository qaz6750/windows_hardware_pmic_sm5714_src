/*
 * SM5714Battery_regs.h
 *
 * Header of Siliconmitus SM5714 PMIC Fuel Gauge registers
 */

#ifndef SM5714BATTERY_REGS
#define SM5714BATTERY_REGS

// Register definitions
#define SM5714_FG_REG_DEVICE_ID           0x00
#define SM5714_FG_REG_CTRL				  0x01
#define SM5714_FG_REG_INTFG               0x02
#define SM5714_FG_REG_STATUS              0x03
#define SM5714_FG_REG_INTFG_MASK          0x04

#define SM5714_FG_REG_SRAM_PROT		      0x8B
#define SM5714_FG_REG_SRAM_RADDR		  0x8C
#define SM5714_FG_REG_SRAM_RDATA		  0x8D
#define SM5714_FG_REG_SRAM_WADDR		  0x8E
#define SM5714_FG_REG_SRAM_WDATA		  0x8F

// SRAM Addresses
#define SM5714_FG_ADDR_SRAM_SOC              0x00
#define SM5714_FG_ADDR_SRAM_OCV              0x01
#define SM5714_FG_ADDR_SRAM_VBAT             0x03
#define SM5714_FG_ADDR_SRAM_VSYS             0x04
#define SM5714_FG_ADDR_SRAM_CURRENT          0x05
#define SM5714_FG_ADDR_SRAM_TEMPERATURE      0x07
#define SM5714_FG_ADDR_SRAM_VBAT_AVG         0x08
#define SM5714_FG_ADDR_SRAM_CURRENT_AVG      0x09
#define SM5714_FG_ADDR_SRAM_STATE            0x15
#define SM5714_FG_ADDR_SRAM_AGING_RATE_FILT  0x46
#define SM5714_FG_ADDR_SRAM_SOC_CYCLE        0x87
#define SM5714_FG_ADDR_SRAM_USER_RESERV_2    0x8B

// Converts 8.8 fixed-point format into standard integer scaled by extend_orders
#define FIXED_POINT_8_8_EXTEND_TO_INT(fp_value, extend_orders) ((((fp_value & 0xff00) >> 8) * extend_orders) + (((fp_value & 0xff) * extend_orders) / 256))

// Read data register
unsigned char readCmd = (unsigned char)SM5714_FG_REG_SRAM_RDATA;

// 3-byte write buffers used as the first step of a SRAM read sequence:
// { SRAM_RADDR register, target SRAM address, padding byte }
static const UCHAR write_state[3]       = { (UCHAR)SM5714_FG_REG_SRAM_RADDR, (UCHAR)SM5714_FG_ADDR_SRAM_STATE,           0 };
static const UCHAR write_cycle[3]       = { (UCHAR)SM5714_FG_REG_SRAM_RADDR, (UCHAR)SM5714_FG_ADDR_SRAM_SOC_CYCLE,       0 };
static const UCHAR write_temperature[3] = { (UCHAR)SM5714_FG_REG_SRAM_RADDR, (UCHAR)SM5714_FG_ADDR_SRAM_TEMPERATURE,     0 };
static const UCHAR write_capacity[3]    = { (UCHAR)SM5714_FG_REG_SRAM_RADDR, (UCHAR)SM5714_FG_ADDR_SRAM_SOC,             0 };
static const UCHAR write_vbat_avg[3]    = { (UCHAR)SM5714_FG_REG_SRAM_RADDR, (UCHAR)SM5714_FG_ADDR_SRAM_VBAT_AVG,        0 };
static const UCHAR write_current_avg[3] = { (UCHAR)SM5714_FG_REG_SRAM_RADDR, (UCHAR)SM5714_FG_ADDR_SRAM_CURRENT_AVG,    0 };
static const UCHAR write_aging_rate[3]  = { (UCHAR)SM5714_FG_REG_SRAM_RADDR, (UCHAR)SM5714_FG_ADDR_SRAM_AGING_RATE_FILT, 0 };
static const UCHAR write_user_reserv2[3]= { (UCHAR)SM5714_FG_REG_SRAM_RADDR, (UCHAR)SM5714_FG_ADDR_SRAM_USER_RESERV_2,  0 };

#endif // SM5714BATTERY_REGS

