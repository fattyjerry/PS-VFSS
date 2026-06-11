package fuzzycrypto

import (
    "math/big"
    "crypto/elliptic"
    "io"
)


// this is a file containing the interface that must be implemented by any fuzzy crypto scheme

type GroupElement struct {
    X          *big.Int
    Y          *big.Int
}

type PubKey struct {
    NumKeys int
    PubKeys []*GroupElement
}

type SecKey struct {
    numKeys     int
    secKeys     []*big.Int 
    prob        uint32
}

type FuzzyScheme interface {
    // a public key algorithm that takes in an int representing  the constant param gamma,
    // outputs a public and private key pair
    KeyGen(elliptic.Curve, int, io.Reader) (*SecKey, *PubKey)
    // an algorithm that takes in a secret key and produces detection key with 
    // probability p / q if possible, nil otherwise
    Extract(int, int, *SecKey) *SecKey
    // self-explanatory, flagging alg.
    // io.Reader is the source you're using for randomness so MAKE SURE IT's GOOD 
    Flag(elliptic.Curve, io.Reader, *PubKey) []byte
    // tests whether or not a ciphertext was addressed to a particular individ. 
    Test(elliptic.Curve, []byte, *SecKey) bool
}

