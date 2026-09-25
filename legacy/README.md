# Legacy prototypes

Kept for reference only. They are superseded by the projects under
[`firmware/`](../firmware), and nothing in the new structure builds them.

- `main_controller/`: first ESP32-S3 controller (ESP-IDF). Its pin map
  (W5500, TCA9554 relays, PCF85063 RTC, buzzer GPIO46) is the source of the
  defaults in `firmware/components/rb_config/Kconfig`.
- `arduino/MLX90640_Testing/`: Arduino MLX90640 bring-up sketch.
- `arduino/RB4107_MQTT_POC/`: Arduino MQTT proof of concept.
