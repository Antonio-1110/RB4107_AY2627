# Waveshare ESP32-S3-ETH-8DI-8RO board support: the shared I2C bus (TCA9554
# relays, PCF85063 RTC) and a bus scan for bring-up.
idf_component_register(SRCS "src/rb_board_s3.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_driver_i2c
                       PRIV_REQUIRES rb_config)
