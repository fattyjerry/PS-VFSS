cmake_minimum_required(VERSION 3.21)

project(pvfss LANGUAGES C CXX)

if(DEFINED DEBUG)
    add_definitions(-DDEBUG)
endif()

string(ASCII 27 Esc)
set(E "${Esc}[m")
set(R "${Esc}[31m")
set(G "${Esc}[32m")

include(cmake/fssPreamble.cmake)

set(VDPF_ROOT "${PROJECT_SOURCE_DIR}/../vdpf" CACHE PATH
    "Path to the bundled VDPF implementation")
set(EMP_ROOT "" CACHE PATH
    "EMP installation prefix containing include/emp-tool and include/emp-ot")

find_package(OpenSSL REQUIRED)
find_package(Threads REQUIRED)
find_package(cryptoTools REQUIRED)
find_library(RELIC_LIB NAMES relic REQUIRED)
find_path(RELIC_INCLUDE_DIR relic/relic.h REQUIRED)
find_path(EMP_TOOL_INCLUDE_DIR emp-tool/emp-tool.h
    HINTS "${EMP_ROOT}/include" REQUIRED)
find_path(EMP_OT_INCLUDE_DIR emp-ot/emp-ot.h
    HINTS "${EMP_ROOT}/include" REQUIRED)

set(VDPF_LIBRARY "${VDPF_ROOT}/src/libdpf.a")
add_custom_command(
    OUTPUT "${VDPF_LIBRARY}"
    COMMAND "${CMAKE_MAKE_PROGRAM}" -C "${VDPF_ROOT}/src"
    DEPENDS
        "${VDPF_ROOT}/src/dpf.c"
        "${VDPF_ROOT}/src/vdpf.c"
        "${VDPF_ROOT}/src/common.c"
        "${VDPF_ROOT}/src/mmo.c"
        "${VDPF_ROOT}/src/sha256.c"
    COMMENT "Building bundled VDPF library"
)
add_custom_target(vdpf_library DEPENDS "${VDPF_LIBRARY}")

add_subdirectory(src)
add_dependencies(test vdpf_library)

target_include_directories(test PRIVATE
    "${RELIC_INCLUDE_DIR}"
    "${EMP_TOOL_INCLUDE_DIR}"
    "${EMP_OT_INCLUDE_DIR}"
)
target_compile_options(test PRIVATE
    -maes -msse2 -msse3 -msse4.1 -mpclmul -mavx2 -march=native
)
target_link_libraries(test PRIVATE
    "${VDPF_LIBRARY}"
    "${RELIC_LIB}"
    OpenSSL::SSL
    OpenSSL::Crypto
    Threads::Threads
    cryptoTools
    m
)
