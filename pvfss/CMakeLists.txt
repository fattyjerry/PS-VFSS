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
find_package(cryptoTools QUIET)
if(TARGET cryptoTools::cryptoTools)
    set(CRYPTOTOOLS_TARGET cryptoTools::cryptoTools)
elseif(TARGET cryptoTools)
    set(CRYPTOTOOLS_TARGET cryptoTools)
else()
    find_path(CRYPTOTOOLS_INCLUDE_DIR
        NAMES cryptoTools/Crypto/PRNG.h
        HINTS "${EMP_ROOT}"
        PATH_SUFFIXES include
        REQUIRED
    )
    find_library(CRYPTOTOOLS_LIBRARY
        NAMES cryptoTools
        HINTS "${EMP_ROOT}"
        PATH_SUFFIXES lib
        REQUIRED
    )
    add_library(cryptoTools_local UNKNOWN IMPORTED)
    set_target_properties(cryptoTools_local PROPERTIES
        IMPORTED_LOCATION "${CRYPTOTOOLS_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${CRYPTOTOOLS_INCLUDE_DIR}"
    )
    set(CRYPTOTOOLS_TARGET cryptoTools_local)
endif()
find_library(RELIC_LIB NAMES relic relic_s REQUIRED)
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
foreach(PVFSS_TARGET IN ITEMS test admission_test)
    add_dependencies(${PVFSS_TARGET} vdpf_library)
    target_include_directories(${PVFSS_TARGET} PRIVATE
        "${RELIC_INCLUDE_DIR}"
        "${EMP_TOOL_INCLUDE_DIR}"
        "${EMP_OT_INCLUDE_DIR}"
    )
    target_compile_options(${PVFSS_TARGET} PRIVATE
        -maes -msse2 -msse3 -msse4.1 -mpclmul -mavx2 -march=native
    )
    target_link_libraries(${PVFSS_TARGET} PRIVATE
        "${VDPF_LIBRARY}"
        "${RELIC_LIB}"
        OpenSSL::SSL
        OpenSSL::Crypto
        Threads::Threads
        ${CRYPTOTOOLS_TARGET}
        m
    )
endforeach()
