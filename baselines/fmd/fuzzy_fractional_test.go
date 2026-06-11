package fuzzycrypto

import (
    "testing"
    "crypto/elliptic"
    "crypto/rand"
    "fmt"
    mathRand "math/rand" //gotta be careful with this...
)

const SMALL_CONSTANT int = 8 
const LARGE_CONSTANT int = 24

/* First stuff is pure testing... no benchmarks */
// this is NOT cryptographic (doesn't need to be) 
func randomProb(mod int) int {
    return mathRand.Intn(mod)
}

// Can you keyGen?
func TestKeyGenFR(t * testing.T) {
    var testT *Fractional
    _, master_pk := testT.KeyGen(elliptic.P256(),8,rand.Reader) 
    for idx, pk := range master_pk.PubKeys {
        if pk == nil {
            t.Errorf("Failure, index %d is a nil pointer", idx)
        }
    }
}

// Can you flag?
func TestFlagSmallFRAC(t *testing.T) {
    var testT *Fractional
    _, pk := testT.KeyGen(elliptic.P256(), SMALL_CONSTANT, rand.Reader)
    flag := testT.Flag(elliptic.P256(), rand.Reader, pk)
    fmt.Printf("FLAG size for gamma=%d: %d\n",SMALL_CONSTANT, len(flag))
}

func TestFlagLargeFRAC(t *testing.T) {
    var testT *Fractional
    _, pk := testT.KeyGen(elliptic.P256(), LARGE_CONSTANT, rand.Reader)
    flag := testT.Flag(elliptic.P256(), rand.Reader, pk)
    fmt.Printf("FLAG size for gamma=%d: %d\n", LARGE_CONSTANT, len(flag))
}
 
// Can you extract?
func TestExtractFR(t *testing.T) {
    var testT *Fractional
    sk, _ := testT.KeyGen(elliptic.P256(), 8, rand.Reader)
    testT.Extract(56, sk)
}

// Can you detect?
func TestDetectFR(t *testing.T) {
    var testT *Fractional
    sk, pk := testT.KeyGen(elliptic.P256(), 8, rand.Reader)
    flag := testT.Flag(elliptic.P256(), rand.Reader, pk)
    dsk := testT.Extract(31, sk)
    res := testT.Test(elliptic.P256(), flag, dsk)
    if !res {
        t.Errorf("Incorrect result")
    }
}

func TestIncorrectFlag(t *testing.T) {
    var testT *Fractional
    _, pk := testT.KeyGen(elliptic.P256(), 8, rand.Reader)
    sk,_ := testT.KeyGen(elliptic.P256(), 8, rand.Reader)
    flag := testT.Flag(elliptic.P256(), rand.Reader, pk)
    dsk := testT.Extract(2, sk)
    res := testT.Test(elliptic.P256(), flag, dsk)
    if res {
        t.Errorf("Incorrect result -- most likely")
    }
}


// All the benchmarks .....


func BenchmarkKeygenSmallFRAC(b *testing.B) {
    var testB *Fractional
    for n := 0; n < b.N; n++ {
        testB.KeyGen(elliptic.P256(), SMALL_CONSTANT, rand.Reader)
    }
}

func BenchmarkKeygenLargeFRAC(b *testing.B) {
    var testB *Fractional
    for n := 0; n < b.N; n++ {
        testB.KeyGen(elliptic.P256(), LARGE_CONSTANT, rand.Reader)
    }
}

func BenchmarkFlagSmallFRAC(b *testing.B) {
    var testB *Fractional
    _, pk := testB.KeyGen(elliptic.P256(), SMALL_CONSTANT, rand.Reader)
    b.ResetTimer()
    for n := 0; n < b.N; n++ {
        testB.Flag(elliptic.P256(), rand.Reader, pk)
    }
}

func BenchmarkFlagLargeFRAC(b *testing.B) {
    var testB *Fractional
    _, pk := testB.KeyGen(elliptic.P256(), LARGE_CONSTANT, rand.Reader)
    b.ResetTimer()
    for n := 0; n < b.N; n++ {
        testB.Flag(elliptic.P256(), rand.Reader, pk)
    } 
}

func BenchmarkExtractSmallFRAC(b *testing.B) {
    var testB *Fractional
    sk, _ := testB.KeyGen(elliptic.P256(), SMALL_CONSTANT, rand.Reader)
    MOD_SIZE := 1 << SMALL_CONSTANT
    b.ResetTimer()
    for n:= 0; n < b.N; n++ {
        // everything will be the same, value here does not matter for time
        testB.Extract(randomProb(MOD_SIZE),sk)
    }
}

func BenchmarkExtractLargeFRAC(b *testing.B) {
    var testB *Fractional
    sk, _ := testB.KeyGen(elliptic.P256(), LARGE_CONSTANT, rand.Reader)
    MOD_SIZE := 1 << LARGE_CONSTANT
    b.ResetTimer()
    for n:= 0; n < b.N; n++ {
        // everything will be the same, value here does not matter for time
        testB.Extract(randomProb(MOD_SIZE),sk)
    }
}

func BenchmarkTestSmallFRAC(b *testing.B) {
    var testB *Fractional
    sk, pk := testB.KeyGen(elliptic.P256(), SMALL_CONSTANT, rand.Reader)
    ctext := testB.Flag(elliptic.P256(), rand.Reader, pk)
    dsk := testB.Extract(randomProb(1 << SMALL_CONSTANT), sk)
    b.ResetTimer()
    for n := 0; n < b.N; n++ {
        testB.Test(elliptic.P256(), ctext, dsk)
    }
}

func BenchmarkTestLargeFRAC(b *testing.B) {
    var testB *Fractional
    sk, pk := testB.KeyGen(elliptic.P256(), LARGE_CONSTANT, rand.Reader)
    ctext := testB.Flag(elliptic.P256(), rand.Reader, pk)
    dsk := testB.Extract(randomProb(1 << LARGE_CONSTANT), sk)
    
    b.ResetTimer()
    for n := 0; n < b.N; n++ {
        testB.Test(elliptic.P256(), ctext, dsk)
    }
}

