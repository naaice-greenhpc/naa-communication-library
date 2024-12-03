find_package(Threads REQUIRED)
find_library(IBVERBS_LIB ibverbs REQUIRED)
find_library(RDMACM_LIB rdmacm REQUIRED)

add_library(naaice_low_level STATIC naaice.c)
target_include_directories(naaice_low_level PUBLIC ${CMAKE_SOURCE_DIR}/lib ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(naaice_low_level PUBLIC ${IBVERBS_LIB} ${RDMACM_LIB} Threads::Threads)

add_library(naaice_middleware STATIC naaice_ap2.c)
target_include_directories(naaice_middleware PUBLIC ${CMAKE_SOURCE_DIR}/lib ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(naaice_middleware PUBLIC naaice_low_level)

add_library(naaice_swnaa STATIC naaice_swnaa.c)
target_include_directories(naaice_swnaa PUBLIC ${CMAKE_SOURCE_DIR}/lib ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(naaice_swnaa PUBLIC naaice_low_level)