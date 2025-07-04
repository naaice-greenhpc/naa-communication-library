find_package(Threads REQUIRED)
find_library(IBVERBS_LIB ibverbs REQUIRED)
find_library(RDMACM_LIB rdmacm REQUIRED)


add_library(naaice_low_level STATIC naaice.c)
add_library(naaice::low_level ALIAS naaice_low_level)
target_include_directories(naaice_low_level 
                            PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
                            PUBLIC ${CMAKE_CURRENT_BINARY_DIR})

target_link_libraries(naaice_low_level 
                    PUBLIC ${IBVERBS_LIB} 
                    PUBLIC ${RDMACM_LIB} 
                    PUBLIC Threads::Threads
                    PUBLIC microlog)

add_library(naaice_middleware STATIC naaice_ap2.c)
add_library(naaice::middleware ALIAS naaice_middleware)
target_include_directories(naaice_middleware 
                            PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
                            PUBLIC ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(naaice_middleware 
                    PUBLIC naaice_low_level
                    PUBLIC microlog)

add_library(naaice_swnaa STATIC naaice_swnaa.c)
add_library(naaice::swnaa ALIAS naaice_swnaa)
target_include_directories(naaice_swnaa 
                            PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
                            PUBLIC ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(naaice_swnaa 
                    PUBLIC naaice_low_level
                    PUBLIC microlog)
