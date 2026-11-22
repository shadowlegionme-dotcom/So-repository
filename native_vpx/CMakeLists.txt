cmake_minimum_required(VERSION 3.13)
project(vpx_jni LANGUAGES C CXX)

if (NOT VPX_SRC_DIR OR NOT VPX_LIB)
  message(FATAL_ERROR "VPX_SRC_DIR ar VPX_LIB set koro")
endif()

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

add_library(vpx_jni SHARED vpx_jni.cpp)
target_include_directories(vpx_jni PRIVATE ${VPX_SRC_DIR})
target_compile_options(vpx_jni PRIVATE -O2 -fvisibility=default)
target_link_libraries(vpx_jni PRIVATE ${VPX_LIB} jnigraphics log)
target_link_options(vpx_jni PRIVATE -Wl,--exclude-libs,ALL -Wl,-z,max-page-size=16384 -Wl,--gc-sections)
