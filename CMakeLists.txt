cmake_minimum_required(VERSION 3.15...3.31)

project(
  naa-communication-prototype
    VERSION 0.1
  LANGUAGES C)

option(USE_EMA "Use EMA" OFF)
option(NAAICE_DISABLE_LOGGING "Disable microlog completely" OFF)
option(LOG_VERBOSE "Enable verbose log output" OFF)

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
    GIT_REPOSITORY git@github.com:sobc/microlog.git
    GIT_TAG main
    EXCLUDE_FROM_ALL)

set(CMAKE_WARN_DEPRECATED OFF CACHE BOOL "" FORCE)
set(CMAKE_POLICY_DEFAULT_CMP0177 OLD)

if(NAAICE_DISABLE_LOGGING)
    FetchContent_MakeAvailable(ulog)
       
    target_compile_definitions(microlog INTERFACE ULOG_BUILD_DISABLED=1)
else()
    if(LOG_VERBOSE)
        add_definitions(-DULOG_BUILD_SOURCE_LOCATION=1)
    else()
        add_definitions(-DULOG_BUILD_SOURCE_LOCATION=0)
    endif()
    add_definitions(-DULOG_BUILD_COLOR=1) # enable color output in log messages
    
    FetchContent_MakeAvailable(ulog)  
    
    set(LOG_LEVEL "ULOG_LEVEL_INFO" CACHE STRING "Set log level (e.g. ULOG_LEVEL_TRACE, ULOG_LEVEL_INFO, ULOG_LEVEL_DEBUG, ULOG_LEVEL_WARN, ULOG_LEVEL_ERROR)")
    add_compile_definitions(LOG_LEVEL=${LOG_LEVEL})
endif()

add_subdirectory(lib)
add_subdirectory(examples)
add_subdirectory(tests)

# install licences in different locations
install(FILES 
        ${CMAKE_CURRENT_SOURCE_DIR}/LICENSE
        ${CMAKE_CURRENT_SOURCE_DIR}/Readme.md
        DESTINATION ${CMAKE_INSTALL_PREFIX})

install(FILES 
        ${ulog_SOURCE_DIR}/LICENSE
        DESTINATION ${CMAKE_INSTALL_PREFIX}/share/doc/microlog)

if(NOT NAAICE_DISABLE_LOGGING)
    install(FILES 
            ${CMAKE_BINARY_DIR}/_deps/ulog-build/micrologConfig.cmake
            ${CMAKE_BINARY_DIR}/_deps/ulog-build/micrologConfigVersion.cmake
            ${ulog_SOURCE_DIR}/cmake/micrologTargets.cmake
            DESTINATION lib/cmake/microlog)
endif()

