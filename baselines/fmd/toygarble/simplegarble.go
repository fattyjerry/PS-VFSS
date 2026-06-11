package toygarble

import (
    "errors"
    "fmt"
    "golang.org/x/crypto/blake2b"
    "math/rand"
    "bytes"
    //b64 "encoding/base64"
    "strconv"
)

//
// Constants and types
//

type Label_t        []byte
type Ciphertext_t   []byte

const (
    LABEL_LEN_BYTES             int = 16
)

type SimpleGarbledCircuit struct {
    NumInputWires           int
    NumOutputWires          int
    GarbledGates            []SimpleGarbledGate
    WireLabels              []SimpleWireLabelSet
    FreeXORDelta            Label_t
}

/* Custom stream-lined format for a 
** garbled circuit -- can't use  
** marshal. 
** What needs to be packed or communicated
** - Input labels
** - All garbled gates (except output)
*/

// this *generically* packs a circuit W/O input labels
// packing here is [all wire labels ordered as input wire i, wire label 0 then wire label 1 for i=1 ... NumInputWires ]
func (g *SimpleGarbledCircuit) PackedMarshal() []byte {
    var packedGC bytes.Buffer

    // Add all input wire labels 
    
    for i := 0; i < g.NumInputWires; i++ {
        _, err := packedGC.Write(g.WireLabels[i].WireLabelPair[0])
        check(err)
        _, err = packedGC.Write(g.WireLabels[i].WireLabelPair[1])
        check(err)
    }
    
    // Add all the gates 
    for i := 0; i < len(g.GarbledGates); i++ {
        for j := 0; j < len(g.GarbledGates[i].Table); j++ {
            _, err := packedGC.Write(g.GarbledGates[i].Table[j])
            check(err)
        }
    }
    return packedGC.Bytes()
}

func (g *SimpleGarbledCircuit) PackedUnmarshal(b []byte, c *Circuit) error {
    g.NumInputWires = c.NumInputWires
    g.NumOutputWires = c.NumOutputWires

    var packedGC *bytes.Buffer = bytes.NewBuffer(b)
    
    g.WireLabels = make([]SimpleWireLabelSet, g.NumInputWires)
    for i := 0; i< g.NumInputWires; i++ {
        for j := 0; j < 2; j++ {
            var label Label_t = make(Label_t, LABEL_LEN_BYTES)
            numRead, err := packedGC.Read(label)
            if numRead != LABEL_LEN_BYTES || err != nil {
                check(err)
                return errors.New("Could not read in the correct number of bytes")
            }
            g.WireLabels[i].WireLabelPair[j] = label
        }
    }
    
    g.GarbledGates = make([]SimpleGarbledGate, len((*c).Gates))
    // how many gates are expected here??
    for i := 0; i < len(c.Gates); i++ {
        // redo-ing the garbling process (somewhat)
        if c.Gates[i].GateType != GateINPUT {
            var tableSize int

            if c.Gates[i].GateType == GateNOT || c.Gates[i].GateType == GateOUTPUT {
                tableSize = 2
            } else if c.Gates[i].GateType == GateCONST {
                tableSize = 1
            } else if c.Gates[i].GateType == GateXOR {
                tableSize = 0
            } else {
                tableSize = 4
            }
            // Allocate memory for the resulting table
            g.GarbledGates[i].Table = make([]Ciphertext_t, tableSize)
            for j := 0; j < tableSize; j++ {
                var row Ciphertext_t = make(Ciphertext_t, LABEL_LEN_BYTES);
                numBytes, err := packedGC.Read(row)
                if numBytes != LABEL_LEN_BYTES || err != nil {
                    check(err)
                    return errors.New("Could not read in the correct number of bytes")
                }
                g.GarbledGates[i].Table[j] = row
            }
        }
    }
    // check if there is more input -- if there IS more input something went wrong
    nullBytes := packedGC.Next(1)
    if len(nullBytes) != 0 {
        fmt.Print("Next byte: ",nullBytes, "\n") 
        return errors.New("Too many bytes left over")
    }
    return nil
}

type SimpleGarbledGate struct {
    Table            []Ciphertext_t
}

type SimpleWireLabelSet struct {
    WireLabelPair      [2]Label_t
}


//
// Garble a given circuit
func (garb *SimpleGarbledCircuit) GarbleCircuit(circ *Circuit, rand *rand.Rand) bool {

    // Make sure that the circuit representation makes sense
    if circ.validCircuit() == false {
        fmt.Printf("Circuit cannot be garbled: not correctly structured\n")
        return false
    }
    
    // Allocate the garbled gate structures and wires. We get one wire
    // per gate in the original circuit (that includes our fictional "input" and "output"
    // gates) as well as one garbled gate for every non-input/output gate in the
    // circuit.
    garb.WireLabels = make([]SimpleWireLabelSet, len((*circ).Gates))
    garb.GarbledGates = make([]SimpleGarbledGate, len((*circ).Gates))
    garb.NumInputWires = circ.NumInputWires
    garb.NumOutputWires = circ.NumOutputWires
    
    // Generate the free XOR variable Delta
    garb.FreeXORDelta = make([]byte, LABEL_LEN_BYTES)
    _, err := rand.Read(garb.FreeXORDelta)
    if (err != nil) {
        return false
    }
    // this will flip so that the last bit is "correct" at least. 
    garb.FreeXORDelta[LABEL_LEN_BYTES-1] |= 0x01 // Set the final bit to 1

    // Walk through each gate of the input circuit, and perform the
    // appropriate label generation
    for i := 0; i < len(circ.Gates); i++ {
        // For input wires in this case -- take them from the input
        garb.assignWireLabelsRecurs(i, circ, rand)
    }
    
    // Walk through each gate of the input circuit, and perform the
    // appropriate garbling
    for i := 0; i < len(circ.Gates); i++ {
        // If the gate is _not_ an "input" or "output" gate, let's garble it.
        // The resulting garbled gate will be added to garb.GarbledGates.
        if (*circ).Gates[i].GateType != GateINPUT {
            // Call a function to do the actual garbling
            if garb.garbleGate(i, circ, rand) == false {
                // Error in garbling
                fmt.Printf("Error garbling a gate\n")
                return false
            }
        }
    }
    
    // Success
    return true
}

//
// Get an array of input labels corresponding to a specific set of bits
func (garb *SimpleGarbledCircuit) GetInputLabelsFromBools(inputs []bool) []Label_t {
    //fmt.Printf("Inside get input label from bool\n%d labels %v", len(inputs), garb.WireLabels)
    if len(inputs) != garb.NumInputWires {
        fmt.Printf("Number wires in garbled circuit is %d\n", garb.NumInputWires)
        return nil
    }
        
    inputLabels := make([]Label_t, len(inputs))
    var inputIndex int
     
    // Go through each input gate and pull out the appropriate wires
    for i := 0; i < garb.NumInputWires; i++ {
        if inputs[i] == false {
            inputIndex = 0
        } else {
            inputIndex = 1
        }
        
        inputLabels[i] = garb.WireLabels[i].WireLabelPair[inputIndex]
    }
    
    // Return the list of labels
    return inputLabels
}

//
// Evaluate a garbled circuit
func (garb *SimpleGarbledCircuit) EvaluateCircuit(circ *Circuit, inputLabels []Label_t) (bool, []Label_t) {
        // Make sure the number of input and output wires matches what we've been given
        if len(inputLabels) != (*circ).NumInputWires || (*circ).NumOutputWires < 1 {
            fmt.Printf("Number of labels does not match number of input wires or number of outputwires is less than one\n")
            fmt.Printf("\nNumber of input labels: %d, Number of InputWires: %d\n",len(inputLabels), (*circ).NumInputWires)
            return false, nil
        }
        
        // Allocate return var and scratch variables to hold onto intermediate values
        visited := make([]bool, len(circ.Gates))                // defaults to all false
        calculated := make([]bool, len(circ.Gates))             // defaults to all empty/nil
        values := make([]Label_t, len(circ.Gates))              // defaults to all false
        result := make([]Label_t, (*circ).NumOutputWires)       // defaults to all empty/nil
        
        // For each output gate, recursively evaluate the entire circuit
        // using the scratch variables
        for i := 0; i < circ.NumOutputWires; i++ {
            // Initialize the visited array to all zero, except for this output gate
            for j := range visited {
                visited[j] = false
            }
            
            // Evaluate the output gate to get a result, error out if it fails
            success, resultLabel := garb.evaluateGarbledGate(circ, (*circ).getOutputGate(i), &visited, &calculated, &values, &inputLabels)
            
            result[i] = resultLabel
            if success == false {
                fmt.Printf("Failed to evaluate garbled circuit\n")
                return false, nil
            }
        }
        
        // Success
        return true, result
}

// Gate evaluation for garbled gates, recursive subroutine
func (garb *SimpleGarbledCircuit) evaluateGarbledGate(circ *Circuit, gateID int, visited *[]bool, calculated *[]bool, labels *[]Label_t, inputLabels *([]Label_t)) (bool, Label_t) {

    // If the gate has already been visited, but not calculated, we're in a loop -- return an error
    if (*visited)[gateID] == true && (*calculated)[gateID] == false {
        return false, nil
    }
    
    // If the gate has been calculated, we're done (but in a good way). Return the cached value.
    if (*calculated)[gateID] == true {
        return true, (*labels)[gateID]
    }
    
    // Evaluate the gate
    (*visited)[gateID] = true
    result := Label_t(make([]byte, LABEL_LEN_BYTES))
    success := true
    
    switch circ.Gates[gateID].GateType {
    case GateINPUT:
        //fmt.Printf("Evaluating IN  gate %d\n", gateID)
        //fmt.Printf("Input label is %s\n", b64.StdEncoding.EncodeToString((*inputLabels)[gateID]))
        result = (*inputLabels)[gateID] // TODO: change this in case input gates aren't 0-aligned

    case GateCONST:
        // Constant gates are easy: the label is in cleartext
        //fmt.Printf("Evaluating COMPUTE  gate %d\n", gateID)
        if len((*circ).Gates[gateID].InFrom) == 0 {
            result = Label_t(garb.GarbledGates[gateID].Table[0])
            success = true
        } else {
            success = false
            fmt.Printf("Error evaluating output 'gate', wrong number of input wires")
        }
        
    default:
        // All real garbled gates
        /*
        fmt.Printf("Evaluating COMPUTE  gate %d\n", gateID)
        if len(circ.Gates[gateID].InFrom) == 1 {
            fmt.Printf("   - input gate is %d\n", circ.Gates[gateID].InFrom[0])
        } else {
            fmt.Printf("   - input gates are %d and %d\n", circ.Gates[gateID].InFrom[0], circ.Gates[gateID].InFrom[1])
        }
        */
        // All other gates, we evaluate a garbled table on the given label(s)
        success = false

        // First verify that our table is the right length -- modifying to ignore XOR dropping tables
        if len((*garb).GarbledGates[gateID].Table) != (1 << len((*circ).Gates[gateID].InFrom)) && circ.Gates[gateID].GateType != GateXOR {
            fmt.Printf("Wrong table size for gate %d of type %d\n", gateID, circ.Gates[gateID].GateType)
            fmt.Printf("Size of table is %d. Number of input wires is %d", len((*garb).GarbledGates[gateID].Table), len((*circ).Gates[gateID].InFrom))
            success = false
        } else {
            // Recursively evaluate the (one or two) input gates
            var success1 bool
            var success2 bool
            var label1  Label_t
            var label2  Label_t
            
            // Recurse on the left gate
            success1, label1 = garb.evaluateGarbledGate(circ, circ.Gates[gateID].InFrom[0], visited, calculated, labels, inputLabels)
            
            // Evaluate the right gate (if there is one)
            if len(circ.Gates[gateID].InFrom) == 2 {
                success2, label2 = garb.evaluateGarbledGate(circ, circ.Gates[gateID].InFrom[1], visited, calculated, labels, inputLabels)
            } else {
                // If there's no right gate, just set this to success
                success2 = true
            }
            
            result = Label_t(make([]byte, len(label1)))

            //fmt.Printf("success1 = %t, success2 = %t\n", success1, success2)
            
            // If both evaluated successfully
            if success1 == true && success2 == true {
                // Special case for XOR gates: simply output the XOR of the two input gates
                if (circ.Gates[gateID].GateType == GateXOR) {
                    //fmt.Printf("Evaluating XOR gate #%d\n", gateID)
                    for i := 0; i < len(label1); i++ {
                        result[i] = label1[i] ^ label2[i]
                    }
                    //fmt.Printf("Evaluating XOR labels are: InLabel1=%s, Inlabel2=%s, Out=%s\n",  b64.StdEncoding.EncodeToString(label1), b64.StdEncoding.EncodeToString(label2), b64.StdEncoding.EncodeToString(result))
                    success = true
                } else {
                    
                    // Go through each entry on the table and attempt to decrypt with the
                    // two possible input gates
                    selector1 := label1[LABEL_LEN_BYTES-1] & 0x01
                    possibleLabel := make(Label_t, LABEL_LEN_BYTES)
                    if len(circ.Gates[gateID].InFrom) == 2 {
                        selector2 := label2[LABEL_LEN_BYTES-1] & 0x01
                        row := 2*int(selector1) + int(selector2)
                        possibleLabel = decryptTableEntry(row, label1[:LABEL_LEN_BYTES-1], label2[:LABEL_LEN_BYTES-1], ((*garb).GarbledGates[gateID].Table[row]))
                        //fmt.Printf("Test decrypting label in row %d\n. Inlabel1=%s, Inlabel2=%s, OutLabel=%s, ciph=%s Gate=%d\n", row, b64.StdEncoding.EncodeToString(label1[:LABEL_LEN_BYTES]), b64.StdEncoding.EncodeToString(label2[:LABEL_LEN_BYTES]),b64.StdEncoding.EncodeToString(possibleLabel), b64.StdEncoding.EncodeToString(((*garb).GarbledGates[gateID].Table[row])), gateID)

                    } else if len(circ.Gates[gateID].InFrom) == 1 {
                        row := int(selector1)
                        possibleLabel = decryptTableEntry(row, label1[:LABEL_LEN_BYTES-1], nil, ((*garb).GarbledGates[gateID].Table[row]))
                        //fmt.Printf("Test decrypting label in row %d\n. Inlabel1=%s, OutLabel=%s, ciph=%s Gate=%d\n", row, b64.StdEncoding.EncodeToString(label1[:LABEL_LEN_BYTES]), b64.StdEncoding.EncodeToString(possibleLabel), b64.StdEncoding.EncodeToString(((*garb).GarbledGates[gateID].Table[row])), gateID)
                        
                    }                    

                    // We found the right label!
                    result = possibleLabel
                    success = true
                                
                }
            }
        }
    }
    
    if success == false {
        fmt.Printf("Error in gate %d\n", gateID)
    } else {
        (*calculated)[gateID] = true
        (*labels)[gateID] = result
    }

    return success, result
}

//
// Returns the input labels
func (garb *SimpleGarbledCircuit) GetInputWireLabels() []SimpleWireLabelSet {
    return garb.WireLabels[0:garb.NumInputWires]
}

//
// Recursively assign wire labels to all gates. This needs to be recursive because of the Free XOR
// optimization. Some gates can have random wire labels, but XOR gates' output labels are equal to the
// combination of their input wires' labels.
func (garb *SimpleGarbledCircuit) assignWireLabelsRecurs(gateID int, circ *Circuit, rand *rand.Rand) bool {

    //fmt.Printf("Entering assignWireLabelsRecurs() at gate %d\n", gateID)

    // Avoid repeating a gate
    if (garb.WireLabels[gateID]).WireLabelPair[0] != nil {
        if len(garb.WireLabels[gateID].WireLabelPair[0]) != 0 {
           //fmt.Printf("There is already a label allocated at gate %d\n", gateID)
            return true
        }
    }
    
    // Recurse on all input gates (unless we're an input gate)
    if (*circ).Gates[gateID].GateType != GateINPUT {
        // For each previous gate, recurse
        for i := 0; i < len((*circ).Gates[gateID].InFrom); i++ {
            if garb.assignWireLabelsRecurs((*circ).Gates[gateID].InFrom[i], circ, rand) == false {
                fmt.Printf("Unable to assign wires at gate %d\n", gateID)
                return false
            }
        }
    }
    
    isStructuredLabel := garb.getIsStructuredLabel(circ, gateID)

    // Allocate memory for the necessary labels for this gate
    for i := 0; i < 2; i++ {
        (garb.WireLabels[gateID]).WireLabelPair[i] = make([]byte, LABEL_LEN_BYTES)
    }

    // If this is an XOR gate, the output labels are equal to the XOR of the input labels
    if (*circ).Gates[gateID].GateType == GateXOR {
        leftGateID  := (*circ).Gates[gateID].InFrom[0]
        rightGateID := (*circ).Gates[gateID].InFrom[1]
        
        // Label 0: left input gate [0] xor right input label [0]
        for i := 0; i < len(garb.WireLabels[gateID].WireLabelPair[0]); i++ {
            (garb.WireLabels[gateID].WireLabelPair[0])[i] = (garb.WireLabels[leftGateID].WireLabelPair[0])[i] ^ (garb.WireLabels[rightGateID].WireLabelPair[0])[i]
        }
        
        // Label 1: left input gate [1] xor right input gate [0]
        for i := 0; i < len(garb.WireLabels[gateID].WireLabelPair[0]); i++ {
            (garb.WireLabels[gateID].WireLabelPair[1])[i] = (garb.WireLabels[leftGateID].WireLabelPair[0])[i] ^ (garb.WireLabels[rightGateID].WireLabelPair[1])[i]
        }
        
        //fmt.Printf("Assigned labels to XOR gate %d\n", gateID)

        // We are done here!
        return true
    }
    
    // Otherwise: this is NOT an XOR gate
    
    // Generate the first label at random
    if isStructuredLabel {
        garb.WireLabels[gateID].WireLabelPair[0] = make([]byte, LABEL_LEN_BYTES)
        garb.WireLabels[gateID].WireLabelPair[1] = make([]byte, LABEL_LEN_BYTES)
        for k := 0; k < LABEL_LEN_BYTES; k++ { 
            garb.WireLabels[gateID].WireLabelPair[1][k] = 0x01
        }
    } else {
        _, err := rand.Read(garb.WireLabels[gateID].WireLabelPair[0])
        garb.WireLabels[gateID].WireLabelPair[0][0] = 0x0
        garb.WireLabels[gateID].WireLabelPair[0][1] = 0x0
        garb.WireLabels[gateID].WireLabelPair[0][2] = 0x0
        garb.WireLabels[gateID].WireLabelPair[0][3] = 0x0
        garb.WireLabels[gateID].WireLabelPair[0][4] = 0x0

        if (err != nil) {
           return false
        }
        // Generate the second label as (first label) XOR freeXORDelta
        for i := 0; i < len(garb.WireLabels[gateID].WireLabelPair[0]); i++ {
            (garb.WireLabels[gateID].WireLabelPair[1])[i] = (garb.WireLabels[gateID].WireLabelPair[0])[i] ^ garb.FreeXORDelta[i]
        }
    } 

    //fmt.Printf("Assigned labels to non-XOR gate %d\nLabel1=%s\nLabel2=%s\n", gateID, b64.StdEncoding.EncodeToString(garb.WireLabels[gateID].WireLabelPair[0]), b64.StdEncoding.EncodeToString(garb.WireLabels[gateID].WireLabelPair[1]))
    return true
}

//
// Garbles one gate of the input circuit and adds the result
// to the garbled gate table.
func (garb *SimpleGarbledCircuit) garbleGate(gateID int, circ *Circuit, rand *rand.Rand) bool {
    //fmt.Printf("Garbling gate %d\n", gateID) 
    var b bool
    success := false
    var tableSize int = 4
    
    // Work out how many rows we need in this table
    if circ.Gates[gateID].GateType == GateNOT || circ.Gates[gateID].GateType == GateOUTPUT {
        tableSize = 2
    } else if circ.Gates[gateID].GateType == GateCONST {
        tableSize = 1
    } else if circ.Gates[gateID].GateType == GateXOR {
        tableSize = 0
    } else {
        tableSize = 4
    }
    
    // Allocate memory for the resulting table
    garb.GarbledGates[gateID].Table = make([]Ciphertext_t, tableSize)

    // Choose a random permutation for the garbled gates
    //fmt.Printf("Table size is %d\n", tableSize) 
    maskBits := make([]int, 0, 2)
    if tableSize >= 2 {
        maskvalLeft :=  garb.WireLabels[(*circ).Gates[gateID].InFrom[0]].WireLabelPair[0][LABEL_LEN_BYTES-1] & 0x01
        maskBits = append(maskBits, int(maskvalLeft))
    }
    if tableSize == 4 {
        maskvalRight := garb.WireLabels[(*circ).Gates[gateID].InFrom[1]].WireLabelPair[0][LABEL_LEN_BYTES-1] & 0x01
        maskBits = append(maskBits, int(maskvalRight))
    }

    gateLocs, success := getGatePermutation(tableSize, maskBits)

    if success == false {
        return false
    }
    
    //fmt.Printf("\n\nGarbling gate %d of type %d\n", gateID, circ.Gates[gateID].GateType)
    
    // Go through all tableSize possible table entries
    for i := 0; i < tableSize; i++ {
        // Garbling varies based on the gate type
        switch circ.Gates[gateID].GateType {
        case GateAND:
            b = ((i & 0x02) != 0) && ((i & 0x01) != 0)
        case GateOR:
            b = ((i & 0x02) != 0) || ((i & 0x01) != 0)
        case GateXOR:
            b = ((i & 0x02) != 0) != ((i & 0x01) != 0)
        case GateNOT:
            b = (i == 0)
        case GateOUTPUT:
            b = (i == 1)
        default:
            fmt.Printf("Unknown gate type %d\n", circ.Gates[gateID].GateType)
            return false
        }
        
        // Compute which wire input bits we're using
        firstLabelBit, secondLabelBit := getLabelBits(i)
        
        // Convert the output bit to an output integer (0,1)
        outputBit := int8(0)
        if b == true {
            outputBit = 1
        }
        outLabel := garb.WireLabels[gateID].WireLabelPair[outputBit] 
        //
        // Garble gates
        //
        // For POINT and PERMUTE using the last byte as a selector (yes I know this is terrible)
        if tableSize == 4 {
            inLabel1 := garb.WireLabels[(*circ).Gates[gateID].InFrom[0]].WireLabelPair[firstLabelBit][:LABEL_LEN_BYTES-1]
            inLabel2 := garb.WireLabels[(*circ).Gates[gateID].InFrom[1]].WireLabelPair[secondLabelBit][:LABEL_LEN_BYTES-1]
            success, garb.GarbledGates[gateID].Table[gateLocs[i]] = encryptTableEntry(gateLocs[i], inLabel1, inLabel2, outLabel)
            
            //fmt.Printf("Encrypting label to row %d\n. Inlabel1=%s, Inlabel2=%s, OutLabel=%s, ciph=%s", gateLocs[i], b64.StdEncoding.EncodeToString(inLabel1), b64.StdEncoding.EncodeToString(inLabel2), b64.StdEncoding.EncodeToString(outLabel),
//                b64.StdEncoding.EncodeToString(garb.GarbledGates[gateID].Table[gateLocs[i]]))
        } else if tableSize == 2 {
            // One input wire label (NOT gates and IDENTITY GATES)
            inLabel1 := garb.WireLabels[(*circ).Gates[gateID].InFrom[0]].WireLabelPair[secondLabelBit][:LABEL_LEN_BYTES-1] 
            success, garb.GarbledGates[gateID].Table[gateLocs[i]] = encryptTableEntry(gateLocs[i], inLabel1, nil, outLabel)
            //fmt.Printf("Encrypting label to row %d\n. Inlabel1=%s, OutLabel=%s, ciph=%s", gateLocs[i], b64.StdEncoding.EncodeToString(inLabel1), b64.StdEncoding.EncodeToString(outLabel),
//                b64.StdEncoding.EncodeToString(garb.GarbledGates[gateID].Table[gateLocs[i]]))
        } else if tableSize == 1 {
            // No input wires (CONST gates)
            // Simply write the appropriate label (unencrypted) into the garbling table
            index := 0
            if (*circ).Gates[gateID].ConstVal == true {
                index = 1
            }
            garb.GarbledGates[gateID].Table[gateLocs[i]] = Ciphertext_t(garb.WireLabels[gateID].WireLabelPair[index])
        }
    }
    //fmt.Printf("Success\n") 
    // Success
    return success
}

//
// Helper function to compute label indices
func getLabelBits(tableRow int) (int, int) {
    firstLabelBit := 0
    if (tableRow & 0x02) != 0 {
        firstLabelBit = 1
    }
    secondLabelBit := (tableRow & 0x01)
    
    return firstLabelBit, secondLabelBit
}

//
// Encrypts a single table entry with one or two labels (keys)
func encryptTableEntry(rowNum int, inLabel1 Label_t, inLabel2 Label_t, outLabel Label_t) (bool, Ciphertext_t) {
    // Create a Blake2b hash instance, because why not?
    h, err := blake2b.New(LABEL_LEN_BYTES, nil)
    if err != nil {
        return false, nil
    }
     
    // Hash in the table row number || inLabel1 || inLabel2 (if non-nil)
    bs := make([]byte, 1)
    bs[0] = byte(rowNum)
    h.Write(bs)
    h.Write(inLabel1)
    if inLabel2 != nil {
        h.Write(inLabel2)
    }
    
    // Encrypt using the output of the hash:
    //   C = [outLabel || (1 byte mask)] XOR hash
    encryptedLabel := h.Sum(nil)
    for i := 0; i < len(outLabel); i++ {
        encryptedLabel[i] ^= outLabel[i]
    }
    
    return true, encryptedLabel
}

//
// Decrypts a single table entry with one or two labels (keys)
// Outputs false if the flag is not valid
func decryptTableEntry(rowNum int, inLabel1 Label_t, inLabel2 Label_t, ciphertext Ciphertext_t) []byte {
    
    // Create a Blake2b hash instance, because why not?
    h, err := blake2b.New(LABEL_LEN_BYTES, nil)
    if err != nil {
        return nil
    }
    
    // Hash in the table row number || inLabel1 || inLabel2 (if non-nil)
    bs := make([]byte, 1)
    bs[0] = byte(rowNum)
    h.Write(bs)
    h.Write(inLabel1)
    if inLabel2 != nil {
        h.Write(inLabel2)
    }
    
    // Decrypt using the output of the hash:
    //   C = [outLabel] XOR hash
    encryptedLabel := h.Sum(nil)
    for i := 0; i < len(ciphertext); i++ {
        encryptedLabel[i] ^= ciphertext[i]
    }
     
    return encryptedLabel
}

//
// Generates a permuted table ordering. Not highly optimized.
func getGatePermutation(tableSize int, masks []int) ([4]int, bool) {
    result := [4]int{0,1,2,3}
    // fall through because why not
    if tableSize == 0 {
        return result, true
    }
    
    if tableSize != 4 && tableSize != 2 {
        fmt.Printf("Only 4-element and 2-element tables can be shuffled\n")
        return result, false
    }
    
    // Permute a two-element table
    if tableSize == 2 {
        // 2-tables: two possible orderings, just flip a coin
        // FLIPPING permutation
        result[masks[0]] = 0
        result[1-masks[0]] = 1
        return result, true
    }
    
    // Permute a four-element table
    for i := 0; i < 4; i++ {
        selectLeftMask := i >> 1 
        selectRightMask := i & 1    
        // FLIPPING permutation
        maskedBit := 2*(selectLeftMask ^ masks[0]) + (selectRightMask ^ masks[1])
        result[maskedBit] = i
    }
    
    return result, true
}

//
// Generates two free-XOR enabled wire labels of length LABEL_LEN_BYTES
// isStructured refers to how the label is constructed -- in the case of output
// gates the label must have the last bit correspond to actual output in the 
// case of our scheme being anonymous 
func GenerateWireLabels(labelSet *SimpleWireLabelSet, freeXORDelta Label_t, rand *rand.Rand, isStructured bool) bool {
    // Allocate both labels
    (*labelSet).WireLabelPair[0] = make([]byte, LABEL_LEN_BYTES-1)
    (*labelSet).WireLabelPair[1] = make([]byte, LABEL_LEN_BYTES-1)
    
    // Generate the first label at random
    _, err := rand.Read((*labelSet).WireLabelPair[0])
    if (err != nil) {
        return false
    }
    
    // Generate the second label as (first label) XOR (second label)
    for i := 0; i < len((*labelSet).WireLabelPair[0]); i++ {
        ((*labelSet).WireLabelPair[1])[i] = ((*labelSet).WireLabelPair[0])[i] ^ freeXORDelta[i]
    }
    
    if isStructured {
        (*labelSet).WireLabelPair[0] = append((*labelSet).WireLabelPair[0], 0)   
        (*labelSet).WireLabelPair[1] = append((*labelSet).WireLabelPair[1], 1)   
    } else {
        rand0 := make([]byte, 1)
        rand1 := make([]byte, 1)
        _, err = rand.Read(rand0)
        if err != nil {
            return false
        }
        _, err = rand.Read(rand1)
        if err != nil {
            return false
        } 
        (*labelSet).WireLabelPair[0] = append((*labelSet).WireLabelPair[0], rand0...)
        (*labelSet).WireLabelPair[1] = append((*labelSet).WireLabelPair[1], rand1...)
    }

    return true
}

// this is decoding for a GC scheme where output is
// NOT encrypted
func DecodePlaintextOutputLabels(outLabels []Label_t) string {
    out := ""
    for _, label := range outLabels {
        actualOutput := int(label[len(label)-1]) & 1
        out = strconv.Itoa(actualOutput) + out
    }
    return out
}

//
// Decodes a set of wire labels into bits, given the label mappings
func decodeResultLabels(resultLabels []Label_t, labelMappings []SimpleWireLabelSet) ([]bool, bool) {
    
    // Allocate the result vector
    result := make([]bool, len(resultLabels))
    
    // Go through all the labels and check for mappings
    for i := 0; i < len(resultLabels); i++ {
    
        // See if it maps to either one
        if bytes.Equal(resultLabels[i], labelMappings[i].WireLabelPair[0]) == true {
            result[i] = false
        } else if bytes.Equal(resultLabels[i], labelMappings[i].WireLabelPair[1]) == true {
            result[i] = true
        } else {
            // No match at all! Return an error
            return nil, false
        }
    }
    
    return result, true
}

func (garb *SimpleGarbledCircuit) getIsStructuredLabel(circ *Circuit, gateID int) bool {
    return (*circ).Gates[gateID].GateType == GateOUTPUT
}
