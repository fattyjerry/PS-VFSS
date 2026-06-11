aux_source_directory(. SRCS)
list(REMOVE_ITEM SRCS "./net.cc")

add_executable(test ${SRCS})
target_compile_features(test PRIVATE cxx_std_20)
