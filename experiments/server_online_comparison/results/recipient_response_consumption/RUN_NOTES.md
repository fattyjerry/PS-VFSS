# Recipient response consumption experiment

- Fixed workload: N=4096, k_actual=k_cap=50.
- Response construction and server computation are outside recipient timers.
- PSVFSS: deserialize both share vectors, field61 addition, identity LocDec used by the pilot location-handle encoding.
- PPS-GC: deserialize both 32-byte row-share vectors and XOR reconstruct every entry.
- FMD: deserialize count and every candidate uint64 handle; gamma=16; false positives remain in the buffer.
- OMR: parse framing, native SEAL Ciphertext::load twice, decrypt, and decode; degree/slots=4096 reduced-parameter pilot.
- OMR and PSVFSS correctness flags are preserved as observed and are not rewritten to true.
- Binary SHA-256: `{"FMD": "a82502e581e742df0255bdf59b44a37b9a0c2422a3f0cc24daadf959fa873fab", "OMR": "9334f3d12c9878a39f3f31a39fecf03e4d57299772971804a576a7e8ad0ae2ff", "PPS-GC": "b4d788b21219c80316215e6ca3c06ca6040bd142b3bcfb357f27ca7c49519054", "PSVFSS": "17c18edd37f88c8ad6ac14b7a0bcd76b85eb72673416a66c877d078d7c074512"}`
