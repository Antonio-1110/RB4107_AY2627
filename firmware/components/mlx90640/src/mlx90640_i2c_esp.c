/*
 * ESP-IDF implementation of the Melexis I2C hooks (MLX90640_I2C_Driver.h).
 * The Melexis API passes the 7-bit address on every call; this shim talks
 * to the single device registered by mlx90640_init().
 */
#include <string.h>
#include "MLX90640_API.h"
#include "MLX90640_I2C_Driver.h"
#include "driver/i2c_master.h"
#include "mlx90640_i2c_esp.h"

#define I2C_TIMEOUT_MS 100

static i2c_master_dev_handle_t s_dev;
/* Largest read is the 832-word EEPROM dump. */
static uint8_t s_rx[MLX90640_EEPROM_DUMP_NUM * 2];

void mlx90640_i2c_set_device(i2c_master_dev_handle_t dev)
{
    s_dev = dev;
}

void MLX90640_I2CInit(void)
{
}

int MLX90640_I2CGeneralReset(void)
{
    /* Only used by MLX90640_TriggerMeasurement(), which this project doesn't call. */
    return -MLX90640_I2C_NACK_ERROR;
}

void MLX90640_I2CFreqSet(int freq)
{
    (void)freq; /* bus speed is fixed when the device is added */
}

int MLX90640_I2CRead(uint8_t slaveAddr, uint16_t startAddress, uint16_t nMemAddressRead, uint16_t *data)
{
    (void)slaveAddr;
    if (s_dev == NULL || nMemAddressRead * 2u > sizeof(s_rx)) {
        return -MLX90640_I2C_NACK_ERROR;
    }
    const uint8_t reg[2] = {startAddress >> 8, startAddress & 0xFF};
    if (i2c_master_transmit_receive(s_dev, reg, sizeof(reg), s_rx, nMemAddressRead * 2u, I2C_TIMEOUT_MS) != ESP_OK) {
        return -MLX90640_I2C_NACK_ERROR;
    }
    for (uint16_t i = 0; i < nMemAddressRead; i++) {
        data[i] = (uint16_t)((s_rx[2 * i] << 8) | s_rx[2 * i + 1]);
    }
    return MLX90640_NO_ERROR;
}

int MLX90640_I2CWrite(uint8_t slaveAddr, uint16_t writeAddress, uint16_t data)
{
    if (s_dev == NULL) {
        return -MLX90640_I2C_NACK_ERROR;
    }
    const uint8_t buf[4] = {writeAddress >> 8, writeAddress & 0xFF, data >> 8, data & 0xFF};
    if (i2c_master_transmit(s_dev, buf, sizeof(buf), I2C_TIMEOUT_MS) != ESP_OK) {
        return -MLX90640_I2C_NACK_ERROR;
    }
    /* Read back, as the Melexis reference driver does. */
    uint16_t check = 0;
    if (MLX90640_I2CRead(slaveAddr, writeAddress, 1, &check) != MLX90640_NO_ERROR) {
        return -MLX90640_I2C_NACK_ERROR;
    }
    return check == data ? MLX90640_NO_ERROR : -MLX90640_I2C_WRITE_ERROR;
}
