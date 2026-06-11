package fuzzycrypto

import (
    "bytes"
    "crypto/elliptic"
    "crypto/sha256"
    "fmt"
    "github.com/becgabri/fuzzycrypto/toygarble"
    "io"
    "math"
    "math/big"
    "math/rand"
    "os"
    "strconv"
)
// kappa (statistical param)
const SECURITYPARAM int = 40

var CIRCUITFILES []string = []string{"48Num8Mod.circ","64Num24Mod.circ"}


type Fractional struct {
}

func computeHashI(curve elliptic.Curve, one *GroupElement, two *GroupElement) []byte {
    serialized := []byte("HashI")
    serialized = append(serialized, elliptic.Marshal(curve, &(*one.X), &(*one.Y))...)
    serialized = append(serialized, elliptic.Marshal(curve, &(*two.X), &(*two.Y))...)
    h := sha256.Sum256(serialized)

    return h[:]
}

// Implementing the fuzzy scheme interface
func (frac *Fractional) KeyGen(curve elliptic.Curve, gamma int, rand io.Reader) (priv *SecKey, pub *PubKey) {
    // check the constant param to see if we have a circuit
    // file for that granularity
    if gamma != 8 && gamma != 24 {
        // fail open
        return
    }
    // create 2*numKeys public/private key pairs 
    // from a Ambig. Enc scheme
    pub = new(PubKey)
    pub.NumKeys = 2*gamma
    pub.PubKeys = make([]*GroupElement, 2*gamma)

    priv = new(SecKey)
    priv.numKeys = 2*gamma
    priv.secKeys = make([]*big.Int, 2*gamma)
    // Recall this is a uint -- should be highest value 
    priv.prob = ^uint32(0) 

    for i := 0; i < 2*gamma; i++ {
        priv.secKeys[i], pub.PubKeys[i] = keyGenSingle(curve, rand)        
    }

    return
}

func encryptLabel(curve elliptic.Curve, myShare *GroupElement, sharedKey *GroupElement, label toygarble.Label_t) []byte {
    bytestream := computeHashI(curve,myShare, sharedKey)
    enc := make([]byte, toygarble.LABEL_LEN_BYTES) 
    for i := 0; i < toygarble.LABEL_LEN_BYTES; i++ {
        enc[i] = label[i] ^ bytestream[i]
    }
    return enc 
}

func generateAnonKeyStream(curve elliptic.Curve, pub *PubKey, b *big.Int, bG *GroupElement, random io.Reader, numOfWires int) []toygarble.SimpleWireLabelSet {
    // compute all shared labels 
    allLabels := make([]toygarble.SimpleWireLabelSet, numOfWires)
    for i := 0; i < pub.NumKeys; i++ {
        sharedKey := new(GroupElement)
        sharedKey.X, sharedKey.Y = curve.ScalarMult(pub.PubKeys[i].X, pub.PubKeys[i].Y, b.Bytes())
        // Hash this and it's a new label
        labelValue := i % 2
        inputPair := i / 2
        byte_stream := computeHashI(curve, bG, sharedKey)
        allLabels[inputPair].WireLabelPair[labelValue] = byte_stream[:toygarble.LABEL_LEN_BYTES]
    }

    return allLabels
}

func writeCompactDHShare(curve elliptic.Curve, bG *GroupElement, buffer *bytes.Buffer) {
    sizeOfInts := curve.Params().P.BitLen()
    xCoord := make([]byte, sizeOfInts)
    yCoord := make([]byte, sizeOfInts)
    signX := 0
    if bG.X.Sign() == -1 {
        signX = 1
    }
    err := buffer.WriteByte(byte(signX))
    check(err)
    bG.X.FillBytes(xCoord)
    _, err = buffer.Write(xCoord)
    check(err)
    
    signY := 0
    if bG.Y.Sign() == -1 {
        signY = 1
    }
    err = buffer.WriteByte(byte(signY))
    check(err)
    bG.Y.FillBytes(yCoord)
    _, err = buffer.Write(yCoord)
    check(err)
}

func (frac *Fractional) Flag(curve elliptic.Curve, random io.Reader, pk *PubKey) []byte {
    // first, read in the correct bristol circuit
    MOD_SIZE := pk.NumKeys / 2
    circuit_idx := 0
    if MOD_SIZE == 24 {
        circuit_idx = 1
    } 
    f, err := os.Open(CIRCUITFILES[circuit_idx]) 
    check(err)
    circuit := toygarble.Circuit{}
    if !toygarble.ParseBRISTOLCircuitFile(&circuit, f) {
        panic("Unable to parse circuit file")
    }

    // generate the input labels...
    // generate your DH Share
    b, bG := keyGenSingle(curve, random)
    
    // garble the circuit and get back the input labels
    var garble toygarble.SimpleGarbledCircuit
    var src toygarble.CryptoSource
    rnd := rand.New(src)
    success := garble.GarbleCircuit(&circuit, rnd) 
    if !success {
        fmt.Printf("Could not garble circuit")
        return nil
    }

    // ciphertext output: 
    // [DH Share][Encrypted Labels][Unencrypted Labels corresponding to random number ][Garbled Circuit]
    ctBuff := new(bytes.Buffer)
    writeCompactDHShare(curve, bG, ctBuff)

    inputPads := generateAnonKeyStream(curve, pk, b, bG, random, MOD_SIZE)
    for i := 0; i < MOD_SIZE; i++ {
        for j := 0; j < 2; j++ {
            idx := garble.NumInputWires - MOD_SIZE
            cipher_text := garble.WireLabels[idx+i].WireLabelPair[j]
            for k := 0; k < toygarble.LABEL_LEN_BYTES; k++ {
                cipher_text[k] = cipher_text[k] ^ inputPads[i].WireLabelPair[j][k]
            }
            num, err := ctBuff.Write(cipher_text)
            check(err)
            if num != toygarble.LABEL_LEN_BYTES {
	        fmt.Printf("Can't write into the buffer!!")
	        return nil
            }
        }
    }

    randomInput := make([]byte, 16)
    _, err = io.ReadFull(random, randomInput)
    check(err)

    randomNumber := big.NewInt(0).SetBytes(randomInput)
    for i := 0; i < circuit.NumInputWires - MOD_SIZE; i++ {
        bit := randomNumber.Bit(i)
        num, err := ctBuff.Write(garble.WireLabels[i].WireLabelPair[bit]) 
        check(err)
        if num != toygarble.LABEL_LEN_BYTES {
            fmt.Printf("Can't write into the buffer!!")
            return nil
        }
    }
    _, err = ctBuff.Write(garble.PackedMarshal())
    check(err)
    return ctBuff.Bytes()
}
// the interface here is a little mixed up between
// types and not intuitive....
func (frac *Fractional) Extract(numerator int, priv *SecKey) (dsk *SecKey) {
    MOD_SIZE := priv.numKeys / 2
    // is the numerator bigger than the modulus
    if numerator < 0 || numerator >= int(math.Pow(float64(2),float64(MOD_SIZE))) {
        return nil
    }
    dsk = new(SecKey)
    dsk.numKeys = MOD_SIZE
    dsk.secKeys = make([]*big.Int, MOD_SIZE)
    dsk.prob = uint32(numerator)
    // interpret numKeys as a bit string and give up keys
    for i := 0; i < MOD_SIZE; i++ {
        // WARNING: bit selector here might be flipped so you'll want to check this 
        bitSelector := (numerator >> i) & 1
        if bitSelector == 1 {
            dsk.secKeys[i] = priv.secKeys[2*i+1]
        } else { 
            dsk.secKeys[i] = priv.secKeys[2*i]
        }
    }

    return
}

func decodeCompactDHShare(curve elliptic.Curve, el *GroupElement, buffer *bytes.Buffer) {
    fieldSize := curve.Params().P.BitLen()

    xCoord := make([]byte, fieldSize)
    signX, err := buffer.ReadByte()
    check(err)

    _, err = buffer.Read(xCoord)
    check(err)
    el.X = big.NewInt(0).SetBytes(xCoord)

    if signX == 1 {
        el.X.Neg(el.X)
    }

    yCoord := make([]byte, fieldSize)
    signY, err := buffer.ReadByte()
    check(err)

    _, err = buffer.Read(yCoord)
    check(err)
    el.Y = big.NewInt(0).SetBytes(yCoord)
    
    if signY == 1 {
        el.Y.Neg(el.Y)
    }
}

func (frac *Fractional) Test(curve elliptic.Curve, ctBytes []byte, priv *SecKey) bool {
    // first, read in the correct bristol circuit
    MOD_SIZE := priv.numKeys
    if MOD_SIZE != 8 && MOD_SIZE != 24 {
        return false
    }
    circuit_idx := 0
    if MOD_SIZE == 24 {
        circuit_idx = 1
    }
    f, err := os.Open(CIRCUITFILES[circuit_idx]) 
    check(err)
    circuit := toygarble.Circuit{}
    if !toygarble.ParseBRISTOLCircuitFile(&circuit, f) {
        panic("Unable to parse circuit file")
    }

    // marshal the ciphertext correctly
    // CT: DH Share || Encrypted Labels || Labels for other input || Garbled Circuit
    ctBuff := bytes.NewBuffer(ctBytes)
    var otherShare GroupElement
    decodeCompactDHShare(curve, &otherShare, ctBuff)

    // decrypt the labels corresponding to the secret key you hold 
    inputLabels := make([]toygarble.Label_t, circuit.NumInputWires)
    allModLabels := make([]toygarble.SimpleWireLabelSet, MOD_SIZE)
    for i := 0; i<MOD_SIZE; i++ {
        for j := 0; j<2; j++ {
            label := make([]byte, toygarble.LABEL_LEN_BYTES)
            _, err = ctBuff.Read(label)
            check(err)
            allModLabels[i].WireLabelPair[j] = label
        }
    }
    for i := circuit.NumInputWires - MOD_SIZE; i < circuit.NumInputWires; i++ {
        var sharedKey GroupElement
        idx := i - (circuit.NumInputWires - MOD_SIZE)
        sharedKey.X, sharedKey.Y = curve.ScalarMult(otherShare.X, otherShare.Y, priv.secKeys[idx].Bytes())
        byte_stream := computeHashI(curve, &otherShare, &sharedKey)
        inputLabels[i] = byte_stream[:toygarble.LABEL_LEN_BYTES]
        wireChoice := (priv.prob >> idx) & 1
        // decrypt the right label
        for j := 0; j < toygarble.LABEL_LEN_BYTES; j++ {
            inputLabels[i][j] = inputLabels[i][j] ^ allModLabels[idx].WireLabelPair[wireChoice][j]
        } 
    }

    for i := 0; i < circuit.NumInputWires - MOD_SIZE; i++ {
        label := make([]byte, toygarble.LABEL_LEN_BYTES)
        _, err = ctBuff.Read(label)
        check(err)
        inputLabels[i] = label
    }

    garb := new(toygarble.SimpleGarbledCircuit)
    ctGCBytes := ctBuff.Bytes()
    err = garb.PackedUnmarshal(ctGCBytes, &circuit)
    check(err)
    evalCheck, output := garb.EvaluateCircuit(&circuit, inputLabels)
    if !evalCheck {
        return false
    }
    // check if the value is less than it should be
    output_str := toygarble.DecodePlaintextOutputLabels(output)
    numAsInt, err := strconv.ParseUint(output_str,2,MOD_SIZE)
    check(err)
    return uint32(numAsInt) < priv.prob 
}

func (frac *Fractional) JsonifySK(sk *SecKey) []byte {
    return nil
}

func (frac *Fractional) MarshalSK(fname string) *SecKey {
    return nil
}

