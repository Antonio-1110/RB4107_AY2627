idf_component_register(
    SRCS
        "main.c"
        "buzzer.c"
        "data_manager.c"
        "i2c_bus.c"
        "mqtt_client_app.c"
        "network.c"
        "relay_control.c"
        "rtc.c"
        "safety_controller.c"
        "sensor_node.c"
        "sensor_manager.c"
    INCLUDE_DIRS "."
    REQUIRES esp_driver_gpio esp_driver_i2c esp_event esp_netif esp_wifi mqtt nvs_flash)
