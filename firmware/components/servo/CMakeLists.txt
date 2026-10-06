# Hobby servo on one LEDC PWM channel (valve node). Builds for every ESP32 target.
idf_component_register(SRCS "src/servo.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_common
                       PRIV_REQUIRES esp_driver_ledc esp_driver_gpio)
