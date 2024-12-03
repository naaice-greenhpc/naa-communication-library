add_executable(naaice_client naaice_client.c)
add_executable(naaice_client_ap2 naaice_client_ap2.c)
add_executable(naaice_server naaice_server.c)

if(USE_EMA)
    add_executable(naaice_client_ema naaice_client_ema.c)
    target_link_libraries(naaice_client_ema PRIVATE ema)
endif(USE_EMA)

target_link_libraries(naaice_client PRIVATE naaice_low_level)
target_link_libraries(naaice_client_ap2 PRIVATE naaice_middleware)
target_link_libraries(naaice_server PRIVATE naaice_swnaa)