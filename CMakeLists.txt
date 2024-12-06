cmake_minimum_required(VERSION 3.15...3.31)

project(
  naa-communication-prototype
  VERSION 1.0
  LANGUAGES C)

option(USE_EMA "Use EMA" OFF)

add_compile_options(-MMD -MP -Wall -pedantic -Wextra -gdwarf-2 $<$<CONFIG:DEBUG>:-g> $<$<CONFIG:RELEASE>:-O3>)

include(FetchContent)

FetchContent_Declare(ema
    GIT_REPOSITORY git@gitup.uni-potsdam.de:bsvs/gc/ema.git
    GIT_TAG master
)

if(USE_EMA)
    FetchContent_MakeAvailable(ema)
endif(USE_EMA)

add_subdirectory(lib)
add_subdirectory(src)