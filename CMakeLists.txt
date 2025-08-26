### root CMakeLists.txt ###

cmake_minimum_required(VERSION 3.16)


include($ENV{IDF_PATH}/tools/cmake/project.cmake)


set(PROJECT_NAME "00_dev")
set(EXTRA_COMPONENT_DIRS ${CMAKE_CURRENT_LIST_DIR}/components)


project(${PROJECT_NAME})
