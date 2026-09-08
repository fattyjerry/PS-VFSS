# PSVFSS Eval/proof-check split pilot

- Ns: [256, 512, 1024, 2048, 4096, 8192, 16384]; k=50; one warmup and 3 measured trials.
- Signal_Verification is the phase-critical admission timer.
- Eval is only the retrieval EvalWithProof computation loop.
- Retrieval_Proof_Check is proof exchange and comparison after Eval.
- Offline preprocessing, setup, diagnostics, and reconstruction are excluded from server retrieval.
- Protocol operations and message order were not changed; only timer boundaries and JSON fields were added.
- Results remain component-composed/local-trusted pilot and are not security-equivalent end-to-end results.
- Source SHA-256: `9f32238fa420d5d436567527672450c88f4c61953beeb240a8d9fc523689cfa7`
- Binary SHA-256: `d11cc7479799ceba2ad319c55874ae952a4da04a9db283b8969349c976bf6458`
