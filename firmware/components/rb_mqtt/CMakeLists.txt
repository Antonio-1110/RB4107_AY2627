set(priv_requires rb_config esp_hw_support)

# espressif/mqtt comes from the component registry (idf_component.yml).
# Offline builds (IDF_COMPONENT_MANAGER=0) must provide a component named
# "mqtt" through EXTRA_COMPONENT_DIRS; see firmware/README.md.
if("$ENV{IDF_COMPONENT_MANAGER}" STREQUAL "0")
    list(APPEND priv_requires mqtt)
endif()

idf_component_register(SRCS "src/rb_mqtt.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_event
                       PRIV_REQUIRES ${priv_requires})
