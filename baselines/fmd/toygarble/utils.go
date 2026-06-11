package toygarble

import(
    crand "crypto/rand"
    "encoding/binary"
    "log"
)
//
// Assemble a PRNG from the cryptographic CSPRNG, man this is a bad hack
// I got off the Internet -- TODO make it better
// <-- Gabby modifiying so that this can be used also in the standalone library 
type CryptoSource struct{}

func (s CryptoSource) Seed(seed int64) {}

func (s CryptoSource) Int63() int64 {
    return int64(s.Uint64() & ^uint64(1 << 63))
}

func (s CryptoSource) Uint64() (v uint64) {
    err := binary.Read(crand.Reader, binary.BigEndian, &v)
    if err != nil {
        log.Fatal(err)
    }
    return
}

func check(e error) {
    if e != nil {
        panic(e)
    }
}
