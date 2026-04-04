cmake_minimum_required(VERSION 3.20)
project(speexdsp_resampler C)
add_library(speexdsp_resampler STATIC src/resample.c)
target_compile_definitions(speexdsp_resampler PUBLIC
    OUTSIDE_SPEEX=1
    RANDOM_PREFIX=airbeam
    FLOATING_POINT=0
    FIXED_POINT=1
)
target_include_directories(speexdsp_resampler
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include/
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/
)
if(MSVC)
    target_compile_options(speexdsp_resampler PRIVATE /W0)
endif()
