cmake_minimum_required(VERSION 3.15...3.31)

project(
  naa-communication-prototype
    VERSION 0.1
  LANGUAGES C)

option(USE_EMA "Use EMA" OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_C_STANDARD 17)

add_compile_options(-MMD -MP -Wall -pedantic -Wextra -gdwarf-2 $<$<CONFIG:DEBUG>:-g> $<$<CONFIG:RELEASE>:-O3>)

include(FetchContent)
include(ExternalProject)

# Set default install prefix to build/install only if not specified by user
if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    set(CMAKE_INSTALL_PREFIX ${CMAKE_BINARY_DIR}/install CACHE PATH "Installation directory" FORCE)
endif()
message(STATUS "CMAKE_INSTALL_PREFIX: ${CMAKE_INSTALL_PREFIX}")

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
add_subdirectory(examples)
add_subdirectory(tests)

# install licences in different locations
install(FILES 
        ${CMAKE_CURRENT_SOURCE_DIR}/LICENSE
        ${CMAKE_CURRENT_SOURCE_DIR}/Readme.md
        DESTINATION ${CMAKE_INSTALL_PREFIX})

install(FILES 
        ${CMAKE_BINARY_DIR}/_deps/ulog-src/LICENSE
        DESTINATION ${CMAKE_INSTALL_PREFIX}/share/doc/microlog)

install(FILES 
        ${CMAKE_BINARY_DIR}/_deps/ulog-build/micrologConfig.cmake
        ${CMAKE_BINARY_DIR}/_deps/ulog-build/micrologConfigVersion.cmake
        ${CMAKE_BINARY_DIR}/_deps/ulog-src/cmake/micrologTargets.cmake
        DESTINATION lib/cmake/microlog)
