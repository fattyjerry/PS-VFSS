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
if(TARGET cryptoTools)
    set(CRYPTOTOOLS_TARGET cryptoTools)
elseif(TARGET oc::cryptoTools)
    set(CRYPTOTOOLS_TARGET oc::cryptoTools)
else()
    # cryptoTools v1.6.0 builds correctly but its upstream install rule
    # references a package-config file that is absent from the release.
    # Support a pinned manual installation of its archive and headers.
    find_library(CRYPTOTOOLS_LIBRARY NAMES cryptoTools REQUIRED)
    find_path(CRYPTOTOOLS_INCLUDE_DIR cryptoTools/Common/config.h REQUIRED)
    add_library(pvfss_cryptotools STATIC IMPORTED)
    set_target_properties(pvfss_cryptotools PROPERTIES
        IMPORTED_LOCATION "${CRYPTOTOOLS_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${CRYPTOTOOLS_INCLUDE_DIR}"
    )
    set(CRYPTOTOOLS_TARGET pvfss_cryptotools)
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
    "${CRYPTOTOOLS_TARGET}"
    m
)

# Keep the standalone admission regression target from the verified-admission
# integration; it uses the same VDPF and crypto dependencies as the main test.
add_dependencies(admission_test vdpf_library)
target_include_directories(admission_test PRIVATE
    "${RELIC_INCLUDE_DIR}"
    "${EMP_TOOL_INCLUDE_DIR}"
    "${EMP_OT_INCLUDE_DIR}"
)
target_compile_options(admission_test PRIVATE
    -maes -msse2 -msse3 -msse4.1 -mpclmul -mavx2 -march=native
)
target_link_libraries(admission_test PRIVATE
    "${VDPF_LIBRARY}"
    "${RELIC_LIB}"
    OpenSSL::SSL
    OpenSSL::Crypto
    Threads::Threads
    "${CRYPTOTOOLS_TARGET}"
    m
)

add_dependencies(ps_vfss_sender_smoke vdpf_library)
target_include_directories(ps_vfss_sender_smoke PRIVATE "${RELIC_INCLUDE_DIR}")
target_compile_options(ps_vfss_sender_smoke PRIVATE -maes -msse2 -msse3 -msse4.1 -mpclmul -mavx2 -march=native)
target_link_libraries(ps_vfss_sender_smoke PRIVATE "${VDPF_LIBRARY}" OpenSSL::SSL OpenSSL::Crypto Threads::Threads "${CRYPTOTOOLS_TARGET}" m)

add_dependencies(ps_vfss_beta_semantics_diag vdpf_library)
target_include_directories(ps_vfss_beta_semantics_diag PRIVATE "${RELIC_INCLUDE_DIR}")
target_compile_options(ps_vfss_beta_semantics_diag PRIVATE -maes -msse2 -msse3 -msse4.1 -mpclmul -mavx2 -march=native)
target_link_libraries(ps_vfss_beta_semantics_diag PRIVATE "${VDPF_LIBRARY}" OpenSSL::SSL OpenSSL::Crypto Threads::Threads "${CRYPTOTOOLS_TARGET}" m)

add_dependencies(ps_vfss_share_algebra_diag vdpf_library)
target_include_directories(ps_vfss_share_algebra_diag PRIVATE "${RELIC_INCLUDE_DIR}")
target_compile_options(ps_vfss_share_algebra_diag PRIVATE -maes -msse2 -msse3 -msse4.1 -mpclmul -mavx2 -march=native)
target_link_libraries(ps_vfss_share_algebra_diag PRIVATE "${VDPF_LIBRARY}" OpenSSL::SSL OpenSSL::Crypto Threads::Threads "${CRYPTOTOOLS_TARGET}" m)

add_dependencies(ps_vfss_end_to_end_semantics_diag vdpf_library)
target_include_directories(ps_vfss_end_to_end_semantics_diag PRIVATE "${RELIC_INCLUDE_DIR}")
target_compile_options(ps_vfss_end_to_end_semantics_diag PRIVATE -maes -msse2 -msse3 -msse4.1 -mpclmul -mavx2 -march=native)
target_link_libraries(ps_vfss_end_to_end_semantics_diag PRIVATE "${VDPF_LIBRARY}" OpenSSL::SSL OpenSSL::Crypto Threads::Threads "${CRYPTOTOOLS_TARGET}" m)
