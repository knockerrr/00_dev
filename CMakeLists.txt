# Project CMakeLists.txt
# 1
cmake_minimum_required(VERSION 3.5)
set(PROJECT_NAME "00_dev")
project(${PROJECT_NAME})

set(CMAKE_CURRENT_LIST_DIR ~/esp)

# 2
set(EXTRA_COMPONENT_DIRS ${CMAKE_CURRENT_LIST_DIR}/components)

# 3
set(IDF_PATH ~/esp)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# 4
