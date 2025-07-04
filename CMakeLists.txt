cmake_minimum_required(VERSION 3.15...3.31)

project(
  naa-communication-prototype
  VERSION 1.0
  LANGUAGES C)

option(USE_EMA "Use EMA" OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_compile_options(-MMD -MP -Wall -pedantic -Wextra -gdwarf-2 $<$<CONFIG:DEBUG>:-g> $<$<CONFIG:RELEASE>:-O3>)

include(FetchContent)
include(ExternalProject)

set(CMAKE_INSTALL_PREFIX ${CMAKE_BINARY_DIR}/install)
message(STATUS "CMAKE_INSTALL_PREFIX: ${CMAKE_INSTALL_PREFIX}")


set(SOURCE_IP "10.3.10.41" CACHE STRING "Source IP Address")
set(REMOTE_IP "10.3.10.42" CACHE STRING "Remote IP Address")

configure_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/lib/config.h.in"
  "${CMAKE_CURRENT_BINARY_DIR}/lib/config.h")

if(USE_EMA)
ExternalProject_Add(
    ema
    GIT_REPOSITORY https://github.com/PERFACCT/EMA.git
    GIT_TAG main
    PREFIX ${CMAKE_BINARY_DIR}
    CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=${CMAKE_INSTALL_PREFIX}
    BUILD_COMMAND ${MAKE_EXECUTABLE}
    INSTALL_COMMAND ${MAKE_EXECUTABLE})
endif()

FetchContent_Declare(ulog
    GIT_REPOSITORY https://github.com/an-dr/microlog.git
    GIT_TAG v6.2.1)

add_definitions(-DULOG_HIDE_FILE_STRING)

FetchContent_MakeAvailable(ulog)

set(LOG_LEVEL "LOG_INFO" CACHE STRING "Set log level (e.g. LOG_INFO, LOG_DEBUG, LOG_WARN, LOG_ERROR)")
add_compile_definitions(LOG_LEVEL=${LOG_LEVEL})

add_subdirectory(lib)
add_subdirectory(src)