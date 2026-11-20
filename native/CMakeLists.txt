cmake_minimum_required(VERSION 3.13)
project(rlottie_jni LANGUAGES C CXX ASM)

if (NOT RLOTTIE_DIR)
    message(FATAL_ERROR "RLOTTIE_DIR set koro")
endif()

set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(LOTTIE_MODULE OFF CACHE BOOL "" FORCE)
set(LOTTIE_THREAD OFF CACHE BOOL "" FORCE)
set(LOTTIE_CACHE ON CACHE BOOL "" FORCE)
set(LOTTIE_TEST OFF CACHE BOOL "" FORCE)
add_subdirectory(${RLOTTIE_DIR} rlottie_build EXCLUDE_FROM_ALL)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

add_library(rlottie_jni SHARED rlottie_jni.cpp)
target_link_libraries(rlottie_jni rlottie jnigraphics log)
target_link_options(rlottie_jni PRIVATE -Wl,-z,max-page-size=16384 -Wl,--gc-sections)
