# Vendored Melexis MLX90640 driver

Unmodified copy of the official Melexis driver:

- Source: https://github.com/melexis/mlx90640-library
- Commit: `f6be7ca1d4a55146b705f3d347f84b773b29cc86`
- License: Apache-2.0 (see `LICENSE`)

`MLX90640_API.c` does the EEPROM calibration extraction and the
temperature calculation. It reaches the hardware only through the functions
declared in `MLX90640_I2C_Driver.h`; `../src/mlx90640_i2c_esp.c` implements
them with the ESP-IDF I2C master driver.
