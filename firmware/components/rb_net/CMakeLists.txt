set(priv_requires rb_config rb_wifi rb_time esp_timer esp_eth esp_netif esp_event esp_wifi esp_driver_spi esp_driver_gpio)

# espressif/w5500 comes from the component registry (idf_component.yml).
# Offline builds (IDF_COMPONENT_MANAGER=0) must provide a component named
# "w5500" through EXTRA_COMPONENT_DIRS instead; see firmware/README.md.
if("$ENV{IDF_COMPONENT_MANAGER}" STREQUAL "0")
    list(APPEND priv_requires w5500)
endif()

idf_component_register(SRCS "src/rb_net.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_netif
                       PRIV_REQUIRES ${priv_requires})
