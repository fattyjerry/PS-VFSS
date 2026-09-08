aux_source_directory(. SRCS)
list(REMOVE_ITEM SRCS "./net.cc" "./admission_test.cc")
list(REMOVE_ITEM SRCS "./sender_smoke.cc")
list(REMOVE_ITEM SRCS "./beta_semantics_diag.cc")
list(REMOVE_ITEM SRCS "./share_algebra_diag.cc")
list(REMOVE_ITEM SRCS "./end_to_end_semantics_diag.cc")

add_executable(test ${SRCS})
target_compile_features(test PRIVATE cxx_std_20)

add_executable(admission_test admission_test.cc admission.cc dpf.cc)
target_compile_features(admission_test PRIVATE cxx_std_20)

add_executable(ps_vfss_sender_smoke sender_smoke.cc dpf.cc dlen_prng.cc dlen_prg.cc ggm_tree.cc)
target_compile_features(ps_vfss_sender_smoke PRIVATE cxx_std_20)

add_executable(ps_vfss_beta_semantics_diag beta_semantics_diag.cc dpf.cc dlen_prng.cc dlen_prg.cc ggm_tree.cc)
target_compile_features(ps_vfss_beta_semantics_diag PRIVATE cxx_std_20)

add_executable(ps_vfss_share_algebra_diag share_algebra_diag.cc dpf.cc dlen_prng.cc dlen_prg.cc ggm_tree.cc)
target_compile_features(ps_vfss_share_algebra_diag PRIVATE cxx_std_20)

add_executable(ps_vfss_end_to_end_semantics_diag end_to_end_semantics_diag.cc dpf.cc dlen_prng.cc dlen_prg.cc ggm_tree.cc)
target_compile_features(ps_vfss_end_to_end_semantics_diag PRIVATE cxx_std_20)
