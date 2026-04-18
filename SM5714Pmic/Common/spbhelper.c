#include "spbhelper.h"

NTSTATUS write_reg(
    PDEVICE_CONTEXT pDevice,
    unsigned long   spbIndex,
    unsigned char   reg,
    unsigned short  data
)
{
    unsigned char buf[3];

    // Set register address
    buf[0] = reg;
    // Little-endian format: LSB sent first
    buf[1] = data & 0xFF;
    buf[2] = (data >> 8) & 0xFF;
    SPB_CONTEXT* spbCtx = &pDevice->SpbContexts[spbIndex];
    return SpbWriteDataSynchronously(spbCtx, buf, sizeof(buf));
}

NTSTATUS read_reg(
    PDEVICE_CONTEXT pDevice,
    unsigned long   spbIndex,
    unsigned char   reg,   // Write 1 byte
    unsigned short* data // Read 2 bytes
) {
    NTSTATUS status;
    unsigned char reg_addr = reg;
    unsigned char read_buf[2];

    SPB_CONTEXT* spbCtx = &pDevice->SpbContexts[spbIndex];
    status = SpbWriteRead(spbCtx, &reg_addr, sizeof(reg_addr), read_buf, sizeof(read_buf), 0);

    // Combine 2 bytes into a 16-bit (LSB first)
    *data = ((unsigned short)read_buf[1] << 8) | read_buf[0];
    return status;
}

NTSTATUS update_reg(
    PDEVICE_CONTEXT pDevice,
    unsigned long   spbIndex,
    unsigned char   reg,
    unsigned short  mask,
    unsigned short  val)
{
    NTSTATUS status;
    unsigned short current;
    unsigned short new_val;

    // Read current 16-bit value
    status = read_reg(pDevice, spbIndex, reg, &current);

    // Clear the bits defined by mask, then OR in (val & mask).
    new_val = (current & ~mask) | (val & mask);

    // If there's no change, return success
    if (current == new_val)
    {
        status = STATUS_SUCCESS;
        return status;
    }

    // Write back the modified value
    status = write_reg(pDevice, spbIndex, reg, new_val);

    return status;
}

NTSTATUS write_reg8(
    PDEVICE_CONTEXT pDevice,
    unsigned long   spbIndex,
    unsigned char   reg,
    unsigned char   data
)
{
    unsigned char buf[2];

    buf[0] = reg;
    buf[1] = data;

    SPB_CONTEXT* spbCtx = &pDevice->SpbContexts[spbIndex];
    return SpbWriteDataSynchronously(spbCtx, buf, sizeof(buf));
}

NTSTATUS read_reg8(
    PDEVICE_CONTEXT pDevice,
    unsigned long   spbIndex,
    unsigned char   reg,
    unsigned char*  data
)
{
    NTSTATUS status;
    unsigned char reg_addr = reg;
    unsigned char read_buf[1];

    SPB_CONTEXT* spbCtx = &pDevice->SpbContexts[spbIndex];
    status = SpbWriteRead(spbCtx, &reg_addr, sizeof(reg_addr),
                          read_buf, sizeof(read_buf), 0);

    if (NT_SUCCESS(status))
        *data = read_buf[0];

    return status;
}

NTSTATUS update_reg8(
    PDEVICE_CONTEXT pDevice,
    unsigned long   spbIndex,
    unsigned char   reg,
    unsigned char   mask,
    unsigned char   val
)
{
    NTSTATUS status;
    unsigned char current;
    unsigned char new_val;

    status = read_reg8(pDevice, spbIndex, reg, &current);
    if (!NT_SUCCESS(status))
        return status;

    new_val = (current & ~mask) | (val & mask);

    if (current == new_val)
        return STATUS_SUCCESS;

    return write_reg8(pDevice, spbIndex, reg, new_val);
}