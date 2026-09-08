# Server latency extension: N=8192 and N=16384

- PPS-GC was not rerun. Its full online admission already exceeded the campaign limit above N=1024.
- PSVFSS uses the current Paillier-3072 component-pilot binary and local trusted preprocessing outside all timers. Each point has one excluded warmup and three measured trials.
- FMD uses gamma=16. Each point has one excluded warmup and three measured trials.
- OMR uses one real measured smoke per point with a fixed poly modulus degree and physical slot count of 16384. Both final ciphertexts were constructed and serialized.
- The OMR 8192 and 16384 retrieval times differ by about 1.22%, confirming that the fixed-physical-slot curve is nearly flat over these two logical workloads.
- Each row retains its measurement tier and correctness status; FMD is the
  end-to-end pilot series, while PSVFSS and OMR are preliminary series.
- Do not connect the old OMR degree-4096 points directly to these degree-16384 points as one fixed-parameter curve. A fully consistent OMR curve requires rerunning N=256 through 4096 at degree=16384.
