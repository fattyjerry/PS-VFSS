aux_source_directory(. SRCS)
list(REMOVE_ITEM SRCS "./net.cc" "./admission_test.cc")

add_executable(test ${SRCS})
target_compile_features(test PRIVATE cxx_std_20)

add_executable(admission_test
    admission_test.cc
    admission.cc
    dpf.cc
)
target_compile_features(admission_test PRIVATE cxx_std_20)
