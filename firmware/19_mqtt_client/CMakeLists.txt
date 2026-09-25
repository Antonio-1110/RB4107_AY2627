# MQTT client on the S3 (TODO section 19).
cmake_minimum_required(VERSION 3.22)

# Shared RB4107 components live in firmware/components.
list(APPEND EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../components")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# Only build main and the components it requires.
idf_build_set_property(MINIMAL_BUILD ON)
project(rb4107_19_mqtt_client)
