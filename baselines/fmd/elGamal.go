package fuzzycrypto

import (
    "bytes"
    "fmt"
    "math/big"
    "crypto/elliptic"
    "io"
    "encoding/json"
    //"golang.org/x/crypto/blake2b"
    "crypto/sha256"
    "os"
    //b64 "encoding/base64"
)

//
// Constants and types
//


type ElGamalPower2 struct {
}

type Ciphertext struct {
    U           GroupElement
    BitVec      []byte
    Y           *big.Int
}

// Generate a full keypair for an n-bit ciphertext
//
func (el *ElGamalPower2) KeyGen(curve elliptic.Curve, numKeys int, rand io.Reader) (priv *SecKey, pub *PubKey) {
    
    // Allocate the structs for public and secret key
    pub = new(PubKey)
    pub.NumKeys = numKeys
    pub.PubKeys = make([]*GroupElement, numKeys)
    
    priv = new(SecKey)
    priv.numKeys = numKeys
    priv.secKeys = make([]*big.Int, numKeys)
    priv.prob = uint32(numKeys)
    // Now generate each individual public and secret key
    for i := 0; i < numKeys; i++ {
        priv.secKeys[i], pub.PubKeys[i]  = keyGenSingle(curve, rand)
    }

    return     
}

//
// Encrypt a ciphertext
func (el *ElGamalPower2) Flag(curve elliptic.Curve, rand io.Reader, pk *PubKey) []byte {

    var pkR GroupElement
    var Z GroupElement
    
    // Allocate the ciphertext struct and the vector of bits
    ctext := new(Ciphertext)
    ctext.BitVec = make([]byte, (pk.NumKeys + 7) / 8)
    
    // First generate two random scalar values r, z
    r := sampleRandomScalar(curve, rand)
    z := sampleRandomScalar(curve, rand)
    
    // Compute u = r * P
    ctext.U.X, ctext.U.Y = curve.ScalarBaseMult(r.Bytes())
    
    // Compute Z = z * P
    Z.X, Z.Y = curve.ScalarBaseMult(z.Bytes())

    // For each bit 1...NumKeys, encrypt "1" under the appropriate public key
    for i := 0; i < pk.NumKeys; i++ {
    
        // Compute pkR = r * pk_i
        pkR.X, pkR.Y = curve.ScalarMult(pk.PubKeys[i].X, pk.PubKeys[i].Y, r.Bytes())
        
        // Compute pad = H(pk_i || pkR || Z), truncated to 1 bit, then XOR with "1"
        // (we obtain this as a uint8 to make life easier)
        padChar := computeHashH(curve, &ctext.U, &pkR, &Z)
        padChar ^= 0x01
        
        // Now pack this into the appropriate location in bitVec
        ctext.BitVec[i / 8] |= padChar << (i % 8)
    }
    
    // Now hash the resulting ciphertext elements to obtain v = G(u, bitVec)
    v := computeHashG(curve, ctext.U, ctext.BitVec)
    
    // Find a solution to "y" such that v*P + y*u = zP
    // (since u = r*P, this means: v + yr = z mod N, or y = (z-v)/r mod N)
    ctext.Y = new(big.Int)
    ctext.Y.Sub(z, v)
    ctext.Y.Mod(ctext.Y, curve.Params().N)
    rInv := new(big.Int)
    rInv.ModInverse(r, curve.Params().N)
    ctext.Y.Mul(ctext.Y, rInv)
    ctext.Y.Mod(ctext.Y, curve.Params().N)
    
    ctAsBytes, err := json.Marshal(*ctext)
    check(err)
    return ctAsBytes
}


//
// Encrypt a ciphertext
func (el *ElGamalPower2) TheoreticalFlag(curve elliptic.Curve, rand io.Reader, pk *PubKey) *Ciphertext {

    var pkR GroupElement
    var Z GroupElement
    
    // Allocate the ciphertext struct and the vector of bits
    ctext := new(Ciphertext)
    ctext.BitVec = make([]byte, (pk.NumKeys + 7) / 8)
    
    // First generate two random scalar values r, z
    r := sampleRandomScalar(curve, rand)
    z := sampleRandomScalar(curve, rand)
    
    // Compute u = r * P
    ctext.U.X, ctext.U.Y = curve.ScalarBaseMult(r.Bytes())
    
    // Compute Z = z * P
    Z.X, Z.Y = curve.ScalarBaseMult(z.Bytes())

    // For each bit 1...NumKeys, encrypt "1" under the appropriate public key
    for i := 0; i < pk.NumKeys; i++ {
    
        // Compute pkR = r * pk_i
        pkR.X, pkR.Y = curve.ScalarMult(pk.PubKeys[i].X, pk.PubKeys[i].Y, r.Bytes())
        
        // Compute pad = H(pk_i || pkR || Z), truncated to 1 bit, then XOR with "1"
        // (we obtain this as a uint8 to make life easier)
        padChar := computeHashH(curve, &ctext.U, &pkR, &Z)
        padChar ^= 0x01
        
        // Now pack this into the appropriate location in bitVec
        ctext.BitVec[i / 8] |= padChar << (i % 8)
    }
    
    // Now hash the resulting ciphertext elements to obtain v = G(u, bitVec)
    v := computeHashG(curve, ctext.U, ctext.BitVec)
    
    // Find a solution to "y" such that v*P + y*u = zP
    // (since u = r*P, this means: v + yr = z mod N, or y = (z-v)/r mod N)
    ctext.Y = new(big.Int)
    ctext.Y.Sub(z, v)
    ctext.Y.Mod(ctext.Y, curve.Params().N)
    rInv := new(big.Int)
    rInv.ModInverse(r, curve.Params().N)
    ctext.Y.Mul(ctext.Y, rInv)
    ctext.Y.Mod(ctext.Y, curve.Params().N)
    
    return ctext
}

//
// Extract a dsk from a secret key. In practice this just involves making a copy of the
// same structure, but it contains only a subset of the private keys.
func (el *ElGamalPower2) Extract(numKeys int, priv *SecKey) (dsk *SecKey) {
    
    // Make sure this number of keys makes sense
    if (numKeys > priv.numKeys) {
        return nil
    }
    
    // Else, allocate a new empty SecKey structure
    dsk = new(SecKey)
    dsk.numKeys = numKeys
    dsk.secKeys = make([]*big.Int, numKeys)
    dsk.prob = uint32(numKeys)
    for i := 0; i < numKeys; i++ {
        dsk.secKeys[i] = priv.secKeys[i]
    }
    
    return
}

//
// Test a ciphertext given a dsk, and return true/false.
// Note that if the number of subkeys in dsk is 0, this will always return "true".
func (el *ElGamalPower2) Test(curve elliptic.Curve, ctBytes []byte, priv *SecKey) bool {
    // transform this into actual ciphertext
    var ctext Ciphertext 
    
    err := json.Unmarshal(ctBytes, &ctext)
    check(err)

    var Z GroupElement
    var temp GroupElement
    var pkR GroupElement
    
    // The default output of this function is "true". If the dsk
    // has zero subkeys, then it will always return true.
    result := true
    
    // Hash the ciphertext elements to obtain v = G(u, bitVec)
    v := computeHashG(curve, ctext.U, ctext.BitVec)
    
    // Compute Z = vP + yU
    Z.X, Z.Y = curve.ScalarBaseMult(v.Bytes())
    temp.X, temp.Y = curve.ScalarMult(ctext.U.X, ctext.U.Y, ctext.Y.Bytes())
    Z.X, Z.Y = curve.Add(Z.X, Z.Y, temp.X, temp.Y)
    
    // For each subkey 1...numKeys in the secret key, decrypt that bit
    for i := 0; i < priv.numKeys; i++ {
    
        // Compute pkR = u^{sk_i}
        pkR.X, pkR.Y = curve.ScalarMult(ctext.U.X, ctext.U.Y, priv.secKeys[i].Bytes())
        
        // Compute pad = H(pk_i || pkR || Z) XOR the i^th bit of ctext.BitVec
        padChar := computeHashH(curve, &ctext.U, &pkR, &Z)
        padChar ^= ((ctext.BitVec[i / 8] >> (i % 8))) & 0x01
        
        // All bits must be 1. If any result is 0, the overall output
        // of this function should be false.
        if padChar == 0 {
            result = false
        }
    }
    
    return result
}

func (el *ElGamalPower2) JsonifySK(sk *SecKey) []byte{
    var write_buff bytes.Buffer;
    
    for i := range sk.secKeys {
        _, err := fmt.Fprintf(&write_buff, "%d\n",sk.secKeys[i]);
        if err != nil {
            panic(err);
        }
    }

    return write_buff.Bytes()
}

func (el *ElGamalPower2) MarshalSK(fname string) *SecKey {
    returnKey := new(SecKey);
    returnKey.secKeys = make([]*big.Int, 0, 0)
    bytestream, err := os.Open(fname)
    if err != nil {
        return nil
    }
    exp := big.NewInt(0);
    numKeys := 0;
    _, err = fmt.Fscanln(bytestream, exp)
    for err != io.EOF && err != io.ErrUnexpectedEOF { 
        if err == io.EOF {
            fmt.Printf("This is an end of file, quitting")
            break
        }
        if err != nil {
            fmt.Printf("Couldn't read into %v", exp)
            panic(err)
        }
        returnKey.secKeys = append(returnKey.secKeys, exp);
        exp = big.NewInt(0);
        numKeys++;
        _, err = fmt.Fscanln(bytestream, exp)
    }
    returnKey.numKeys = numKeys;
    return returnKey
}

//
// Compute the hash function H(A, B, C) where A, B, C are group elements
func computeHashH(curve elliptic.Curve, one *GroupElement, two *GroupElement, three *GroupElement) uint8 {
    
    // Turn the group elements into byte arrays and concatenate them as ("HashH" || one || two)
    serialized := []byte("HashH")
    serialized = append(serialized, elliptic.Marshal(curve, &(*one.X), &(*one.Y))...)
    serialized = append(serialized, elliptic.Marshal(curve, &(*two.X), &(*two.Y))...)
    serialized = append(serialized, elliptic.Marshal(curve, &(*three.X), &(*three.Y))...)

    //s, _ := json.MarshalIndent(serialized, "", "\t");
    //fmt.Println(string(s))
    
    // Compute SHA256(serialized)
    h := sha256.Sum256(serialized)
    
    // Return a single bit of the resulting hash
    return h[0] & 0x01
}

//
// Compute the hash function G(u, bitVec) u is a group element and bitVec is a byte slice.
// Return an integer in the range [0...group order - 1]
func computeHashG(curve elliptic.Curve, u GroupElement, bitVec []byte) (result *big.Int) {
    
    N := curve.Params().N
    bitSize := N.BitLen()
    
    // Turn the values into byte arrays and concatenate them as ("HashG" || u || bitVec)
    serialized := []byte("HashG")
    serialized = append(serialized, elliptic.Marshal(curve, &(*u.X), &(*u.Y))...)
    serialized = append(serialized, bitVec...)
    
    // Allocate a buffer at least 64 bits too long
    bitSize += 64
    bitsHashed := 0
    h := make([]byte, 0)
 
    // Now repeatedly hash until we've filled the buffer
    for (bitsHashed < bitSize) {

        // Hash the serialized value and append to "h"
        hash := sha256.Sum256(serialized)
        h = append(h, hash[:32]...)
        bitsHashed = len(h) / 8
        
        // Concatenate an "X" onto the end of serialized each time through this loop
        // This ensures that serialized is different each time we hash it
        if (bitsHashed < bitSize) {
            serialized = append(serialized, []byte("X")...)
        }
    }
    
    // Now cast the resulting byte array into a Big.int
    // and compute result % group order
    result = new(big.Int)
    result.SetBytes(h)
    result.Mod(result, curve.Params().N)
    
    return
}

func efficientSerializeMeasure(curve elliptic.Curve, ct *Ciphertext) int {
    serialized := []byte("")
    serialized = append(serialized, elliptic.MarshalCompressed(curve, &(*ct.U.X), &(*ct.U.Y))...)
    serialized = append(serialized, (ct.Y).Bytes()...)
    serialized = append(serialized, (ct.BitVec)...)
    
    return len(serialized)
}

/*
func main() {
    // Generate a key
    fmt.Println("Generating a key")
    sk, pk := keyGen(elliptic.P256(), 80, rand.Reader)
    
    //s, _ := json.MarshalIndent(sk, "", "\t");
    //fmt.Print(string(s))
    
    //t, _ := json.MarshalIndent(pk, "", "\t");
    //fmt.Print(string(t))
    
    // Encrypt
    fmt.Println("\nEncrypting a flag ciphertext")
    ctext := encrypt(elliptic.P256(), rand.Reader, pk)
    
    s, _ := json.MarshalIndent(ctext, "", "\t");
    fmt.Print(string(s))
    
    // Extract
    fmt.Println("\nExtracting a dsk, N=3")
    dsk := extract(3, sk)
    
    //s, _ = json.MarshalIndent(dsk, "", "\t");
    //fmt.Print(string(s))

    // Test
    fmt.Println("\nTesting ciphertext with correct dsk")
    if test(elliptic.P256(), ctext, dsk) {
        fmt.Println("Result = TRUE")
    } else {
        fmt.Println("Result = FALSE")
    }
    
    // Test with the wrong key
    sk2, pk2 := keyGen(elliptic.P256(), 80, rand.Reader)
    _ = pk2 // just do this to avoid annoying "not used" Go error
    dsk2 := extract(3, sk2)

    fmt.Println("\nTesting ciphertext with wrong dsk")
    if test(elliptic.P256(), ctext, dsk2) {
        fmt.Println("Result = TRUE")
    } else {
        fmt.Println("Result = FALSE")
    }
}
*/
