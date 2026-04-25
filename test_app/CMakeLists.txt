cmake_minimum_required(VERSION 3.16)

# Point to your components folder
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../components")

# Which components to test
set(TEST_COMPONENTS daq_core can twai sd)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(unit_test_app)
