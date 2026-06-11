package fuzzycrypto

import (
    "crypto/elliptic"
    "io"
    "math/big"
)

func check(e error) {
    if e != nil {
        panic(e)
    }
}

//
// Generate a sub-singlekeypair
// lots of this code duplicated from https://golang.org/src/crypto/elliptic/elliptic.go, GenerateKey()
func keyGenSingle(curve elliptic.Curve, random io.Reader) (priv *big.Int, pub *GroupElement) {

    // Sample a random private key sk
    priv = sampleRandomScalar(curve, random)

    // Compute the public key PK = priv*P where P is the generator
    pub = new(GroupElement)
    pub.X, pub.Y = curve.ScalarBaseMult(priv.Bytes())
    
    return
}

var mask = []byte{0xff, 0x1, 0x3, 0x7, 0xf, 0x1f, 0x3f, 0x7f}

//
// Sample a random scalar
// lots of this code duplicated from https://golang.org/src/crypto/elliptic/elliptic.go, GenerateKey()
func sampleRandomScalar(curve elliptic.Curve, random io.Reader) (x *big.Int) {

    done := false
    
    // Get the group order from the curve parameters
    N := curve.Params().N
    bitSize := N.BitLen()
    byteLen := (bitSize + 7) >> 3
    priv := make([]byte, byteLen)
        
    // Sample a private scalar
    for done == false {
        _, err := io.ReadFull(random, priv)
        if err != nil {
            return nil
        }

        // We have to mask off any excess bits in the case that the size of the
        // underlying field is not a whole number of bytes.
        priv[0] &= mask[bitSize%8]

        // This is because, in tests, rand will return all zeros and we don't
        // want to get the point at infinity and loop forever.
        priv[1] ^= 0x42

        // If the scalar is out of range, sample another random number.
        x = new(big.Int).SetBytes(priv)
        if new(big.Int).SetBytes(priv).Cmp(N) >= 0 {
            continue
        }
        
        done = true
    }
    return
}

