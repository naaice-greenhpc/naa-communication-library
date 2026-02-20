add_executable(naaice_client naaice_client.c)
add_executable(naaice_client_ap2 naaice_client_ap2.c)
add_executable(naaice_server naaice_server.c ${CMAKE_SOURCE_DIR}/kernels/swnaa_kernels.c)

if(USE_EMA)
    include_directories(${CMAKE_INSTALL_PREFIX}/include)
    link_directories(${CMAKE_INSTALL_PREFIX}/lib)
    add_executable(naaice_client_ema naaice_client_ema.c)
    target_link_libraries(naaice_client_ema
                            PRIVATE ${CMAKE_INSTALL_PREFIX}/lib/libEMA.so
                            PRIVATE naaice::low_level)
endif(USE_EMA)

target_link_libraries(naaice_client PRIVATE naaice::low_level)
target_link_libraries(naaice_client_ap2 PRIVATE naaice::middleware)
target_link_libraries(naaice_server PRIVATE naaice::swnaa)