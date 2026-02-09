find_package(Threads REQUIRED)
find_library(IBVERBS_LIB ibverbs REQUIRED)
find_library(RDMACM_LIB rdmacm REQUIRED)


add_library(naaice_low_level STATIC naaice.c)
add_library(naaice::low_level ALIAS naaice_low_level)
set_target_properties(naaice_low_level PROPERTIES EXPORT_NAME low_level)
target_include_directories(naaice_low_level 
                            PUBLIC 
                                $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
                                $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
                                $<INSTALL_INTERFACE:include/naaice>)

target_link_libraries(naaice_low_level 
                    PUBLIC ${IBVERBS_LIB} 
                    PUBLIC ${RDMACM_LIB} 
                    PUBLIC Threads::Threads
                    PUBLIC microlog)

add_library(naaice_middleware STATIC naaice_ap2.c)
add_library(naaice::middleware ALIAS naaice_middleware)
set_target_properties(naaice_middleware PROPERTIES EXPORT_NAME middleware)
target_include_directories(naaice_middleware 
                            PUBLIC 
                                $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
                                $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
                                $<INSTALL_INTERFACE:include/naaice>)
target_link_libraries(naaice_middleware 
                    PUBLIC naaice_low_level
                    PUBLIC microlog)

add_library(naaice_swnaa STATIC naaice_swnaa.c)
add_library(naaice::swnaa ALIAS naaice_swnaa)
set_target_properties(naaice_swnaa PROPERTIES EXPORT_NAME swnaa)
target_include_directories(naaice_swnaa 
                            PUBLIC 
                                $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
                                $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/kernels>
                                $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
                                $<INSTALL_INTERFACE:include/naaice>)
target_link_libraries(naaice_swnaa 
                    PUBLIC naaice_low_level
                    PUBLIC microlog)

# Installation rules
install(TARGETS naaice_low_level naaice_middleware naaice_swnaa microlog
        EXPORT naaice-targets
        ARCHIVE DESTINATION lib
        LIBRARY DESTINATION lib
        RUNTIME DESTINATION bin
        INCLUDES DESTINATION include/naaice)

# Install header files
install(FILES 
        naaice.h
        naaice_ap2.h
        naaice_swnaa.h
        DESTINATION include/naaice)

# Install microlog header
install(FILES 
        ${CMAKE_BINARY_DIR}/_deps/ulog-src/include/ulog.h
        DESTINATION include/naaice)

# Install export targets
install(EXPORT naaice-targets
        FILE naaice-targets.cmake
        NAMESPACE naaice::
        DESTINATION lib/cmake/naaice)

# Create and install config file
include(CMakePackageConfigHelpers)
configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/naaice-config.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/naaice-config.cmake
    INSTALL_DESTINATION lib/cmake/naaice)

write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/naaice-config-version.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion)

install(FILES
        ${CMAKE_CURRENT_BINARY_DIR}/naaice-config.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/naaice-config-version.cmake
        DESTINATION lib/cmake/naaice)
