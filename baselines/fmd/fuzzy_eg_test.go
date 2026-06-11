package fuzzycrypto

import (
    "testing"
    "crypto/elliptic"
    "crypto/rand"
    "fmt"
)

const NUM_TOTAL_KEYS = 24
const NUM_EXTRACT_SMALL = 5
const NUM_EXTRACT_MED = 10
const NUM_EXTRACT_LARGE = 15

func BenchmarkKeygenEG(b *testing.B) {
    var testB *ElGamalPower2
    for n := 0; n < b.N; n++ {
        testB.KeyGen(elliptic.P256(), NUM_TOTAL_KEYS, rand.Reader)
    }
}

func BenchmarkEncryptEG(b *testing.B) {
    var testB *ElGamalPower2
    sk, pk := testB.KeyGen(elliptic.P256(), NUM_TOTAL_KEYS, rand.Reader)
    _ = sk
    
    b.ResetTimer()
    for n := 0; n < b.N; n++ {
        ctext := testB.Flag(elliptic.P256(), rand.Reader, pk)
        _ = ctext
    }
}

func BenchmarkExtractSmallEG(b *testing.B) {
    var testB *ElGamalPower2
    sk, pk := testB.KeyGen(elliptic.P256(), NUM_TOTAL_KEYS, rand.Reader)
    _ = pk
    
    b.ResetTimer()
    for n := 0; n < b.N; n++ {
        dsk := testB.Extract(NUM_EXTRACT_SMALL, sk)
        _ = dsk
    }
}

func BenchmarkExtractMedEG(b *testing.B) {
    var testB *ElGamalPower2
    sk, pk := testB.KeyGen(elliptic.P256(), NUM_TOTAL_KEYS, rand.Reader)
    _ = pk
    
    b.ResetTimer()
    for n := 0; n < b.N; n++ {
        dsk := testB.Extract(NUM_EXTRACT_MED, sk)
        _ = dsk
    }
}

func BenchmarkExtractLargeEG(b *testing.B) {
    var testB *ElGamalPower2
    sk, pk := testB.KeyGen(elliptic.P256(), NUM_TOTAL_KEYS, rand.Reader)
    _ = pk
    
    b.ResetTimer()
    for n := 0; n < b.N; n++ {
        dsk := testB.Extract(NUM_EXTRACT_LARGE, sk)
        _ = dsk
    }
}

func BenchmarkTestSmallEG(b *testing.B) {
    var testB *ElGamalPower2
    sk, pk := testB.KeyGen(elliptic.P256(), NUM_TOTAL_KEYS, rand.Reader)
    _ = pk
    ctext := testB.Flag(elliptic.P256(), rand.Reader, pk)
    dsk := testB.Extract(NUM_EXTRACT_SMALL, sk)

    b.ResetTimer()
    for n := 0; n < b.N; n++ {
        testB.Test(elliptic.P256(), ctext, dsk)
    }
}

func BenchmarkTestMedEG(b *testing.B) {
    var testB *ElGamalPower2
    sk, pk := testB.KeyGen(elliptic.P256(), NUM_TOTAL_KEYS, rand.Reader)
    _ = pk
    ctext := testB.Flag(elliptic.P256(), rand.Reader, pk)
    dsk := testB.Extract(NUM_EXTRACT_MED, sk)

    b.ResetTimer()
    for n := 0; n < b.N; n++ {
        testB.Test(elliptic.P256(), ctext, dsk)
    }
}

func BenchmarkTestLargeEG(b *testing.B) {
    var testB *ElGamalPower2
    sk, pk := testB.KeyGen(elliptic.P256(), NUM_TOTAL_KEYS, rand.Reader)
    _ = pk
    ctext := testB.Flag(elliptic.P256(), rand.Reader, pk)
    dsk := testB.Extract(NUM_EXTRACT_LARGE, sk)

    b.ResetTimer()
    for n := 0; n < b.N; n++ {
        testB.Test(elliptic.P256(), ctext, dsk)
    }
}

func TestFindTheoreticalCTSize(t * testing.T) {
    var testB *ElGamalPower2
    _, pk := testB.KeyGen(elliptic.P256(), NUM_TOTAL_KEYS, rand.Reader)
    
    ctext := testB.TheoreticalFlag(elliptic.P256(), rand.Reader, pk)
    len := efficientSerializeMeasure(elliptic.P256(),ctext)
    fmt.Printf("Length of el gamal text would be: %d\n", len) 
}

func TestWrongFlagEG(t * testing.T) {
    var testB *ElGamalPower2
    _, pk0 := testB.KeyGen(elliptic.P256(), NUM_TOTAL_KEYS, rand.Reader)
    sk1,_ := testB.KeyGen(elliptic.P256(), NUM_TOTAL_KEYS, rand.Reader)
    dsk1 := testB.Extract(NUM_TOTAL_KEYS-1, sk1)
    // notice this isn't a guarantee but it should work
    ctext := testB.Flag(elliptic.P256(), rand.Reader, pk0) 
    if len(ctext) == 0 {
        t.Errorf("Something went wrong -- ciphertext length is 0")
    }
    if testB.Test(elliptic.P256(), ctext, dsk1) {
        t.Errorf("Unlikely event occurred -- dsk1 succeeded on ct intended for pk0 where dsk1 had prob. 1/2^%d of passing", NUM_TOTAL_KEYS-1)
    }
}
