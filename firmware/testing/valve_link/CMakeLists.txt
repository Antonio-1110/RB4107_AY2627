# Test only: the controller's wireless valve link on the S3, without the safety system.
cmake_minimum_required(VERSION 3.22)

# Shared RB4107 components live in firmware/components.
list(APPEND EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../../components")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# Only build main and the components it requires.
idf_build_set_property(MINIMAL_BUILD ON)
project(rb4107_test_valve_link)
