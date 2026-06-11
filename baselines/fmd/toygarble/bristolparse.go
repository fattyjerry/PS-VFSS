package toygarble

import (
    "io"
    "fmt"
    "encoding/csv"
    "strconv"
)

//
// Constants and types
//

//
// Main parsing function
func ParseBRISTOLCircuitFile(circ *Circuit, inReader io.Reader) bool {
    //fmt.Print("Parsing bristol file...")
    totalNumInputWires := 0
    totalNumOutputWires := 0
    
    // Create a new CSV reader
    r := csv.NewReader(inReader)
    r.Comma = ' '               // records are space delimited
    r.Comment = '#'             // no comments in the BRISTOL files, but there should be
    r.FieldsPerRecord = -1      // variable number of fields per record
    
    // First line:
    // <numGates> <numWires>\n
    record, err := r.Read()
    check(err)
    numGates, _ := strconv.Atoi(record[0])
    numWires, _ := strconv.Atoi(record[1])
    
    // Second line:
    // <numInputVariables> <numWiresVar1> ... <numWiresVarN>
    record, err = r.Read()
    check(err)
    numInputVars, _ := strconv.Atoi(record[0])
    numWiresPerIV := make([]int, numInputVars)
    for i := 0; i < numInputVars; i++ {
        numWiresPerIV[i], _ = strconv.Atoi(record[i + 1])
        totalNumInputWires += numWiresPerIV[i]
    }
    
    // Third line:
    // <numOutputVariables> <numWiresVar1> ... <numWiresVarN>
    record, err = r.Read()
    check(err)
    numOutputVars, _ := strconv.Atoi(record[0])
    numWiresPerOV := make([]int, numOutputVars)
    for i := 0; i < numOutputVars; i++ {
        numWiresPerOV[i], _ = strconv.Atoi(record[i + 1])
        totalNumOutputWires += numWiresPerOV[i]
    }
    
    //fmt.Printf("Numgates = %d, NumWires = %d\n", numGates, numWires)
    //fmt.Printf("Input vars=%d, wires = ", numInputVars)
    //fmt.Println(numWiresPerIV)
    //fmt.Printf("Output vars=%d, wires = ", numOutputVars)
    //fmt.Println(numWiresPerOV)
    
    // Initialize the circuit
    (*circ).initializeCircuit(totalNumInputWires, totalNumOutputWires, numInputVars, numOutputVars, numWiresPerIV, numWiresPerOV)

    // Fourth line should be empty, but CSV parser will ignore it
    //
    // Everything after this defines a gate (or wire connection):
    //
    // For gates, the format is:
    //   <numInputWires> <numOutputWires> <inWire1> .. <inWireN> <outWire> <GateType>
    // where <GateType> is one of [AND, XOR, INV]
    //
    // For AND, XOR the number of input gates is always 2, output is 1
    // For INV the number of input gates is always 1, output is 1
    //
    // For wire connections, the format is either:
    //   1 1 <ConstantBit> <outWire> EQ
    //   (this assigns the constant ConstantBit to wire <outWire>
    // or
    //   1 1 <inWire> <outWire> EQW   --- TODO THE DOCS SAY DIFFERENTLY AW HELL
    //   (this connects wire inWire to outWire)

    // Allocate all the new gates
    (*circ).Gates = append((*circ).Gates, make([]Gate, numGates)...)
    wires := make([]int, numWires)
    
    for i := (totalNumInputWires + totalNumOutputWires); i < (totalNumInputWires + totalNumOutputWires) + numGates; i++ {
        // Read in one line of the file
        record, err = r.Read()
        check(err)
        
        // Switch based on the last opcode in the line
        switch record[len(record) - 1] {
        case "AND":
            (*circ).Gates[i].GateType = GateAND
            (*circ).Gates[i].InFrom = make([]int, 2)
            inWire1, _ := strconv.Atoi(record[2])
            inWire2, _ := strconv.Atoi(record[3])
            (*circ).Gates[i].InFrom[0] = wireToGate(wires, inWire1, circ, totalNumInputWires)
            (*circ).Gates[i].InFrom[1] = wireToGate(wires, inWire2, circ, totalNumInputWires)
            if (*circ).Gates[i].InFrom[0] == -1 || (*circ).Gates[i].InFrom[1] == -1 {
                return false
            }
            outWire, _ := strconv.Atoi(record[4])
            wires[outWire] = i
            //fmt.Printf("Added AND gate %d, wired to input gates (%d,%d)\n", i, (*circ).Gates[i].InFrom[0], (*circ).Gates[i].InFrom[1])


        case "XOR":
            (*circ).Gates[i].GateType = GateXOR
            (*circ).Gates[i].InFrom = make([]int, 2)
            inWire1, _ := strconv.Atoi(record[2])
            inWire2, _ := strconv.Atoi(record[3])
            (*circ).Gates[i].InFrom[0] = wireToGate(wires, inWire1, circ, totalNumInputWires)
            (*circ).Gates[i].InFrom[1] = wireToGate(wires, inWire2, circ, totalNumInputWires)
            if (*circ).Gates[i].InFrom[0] == -1 || (*circ).Gates[i].InFrom[1] == -1 {
                return false
            }
            outWire, _ := strconv.Atoi(record[4])
            wires[outWire] = i
        case "OR":
            (*circ).Gates[i].GateType = GateOR
            (*circ).Gates[i].InFrom = make([]int, 2)
            inWire1, _ := strconv.Atoi(record[2])
            inWire2, _ := strconv.Atoi(record[3])
            (*circ).Gates[i].InFrom[0] = wireToGate(wires, inWire1, circ, totalNumInputWires)
            (*circ).Gates[i].InFrom[1] = wireToGate(wires, inWire2, circ, totalNumInputWires)
            if (*circ).Gates[i].InFrom[0] == -1 || (*circ).Gates[i].InFrom[1] == -1 {
                return false
            }
            outWire, _ := strconv.Atoi(record[4])
            wires[outWire] = i
    
        case "INV":
            (*circ).Gates[i].GateType = GateNOT
            (*circ).Gates[i].InFrom = make([]int, 1)
            inWire1, _ := strconv.Atoi(record[2])
            (*circ).Gates[i].InFrom[0] = wireToGate(wires, inWire1, circ, totalNumInputWires)
            if (*circ).Gates[i].InFrom[0] == -1 {
                return false
            }
            outWire, _ := strconv.Atoi(record[3])
            wires[outWire] = i
            //fmt.Printf("Added NOT gate %d, wired to input gate %d\n", i, (*circ).Gates[i].InFrom[0])
            
        case "EQ":
            (*circ).Gates[i].GateType = GateCONST
            (*circ).Gates[i].InFrom = make([]int, 1)
            (*circ).Gates[i].InFrom[0], _ = strconv.Atoi(record[2])
            outWire, _ := strconv.Atoi(record[4])
            wires[outWire] = i
            
        case "EQW":
            // not sure what we're doing here so I'm just filling this out
            (*circ).Gates[i].GateType = GateCOPY
            (*circ).Gates[i].InFrom = make([]int, 1)
            inWire, _ := strconv.Atoi(record[2])
            (*circ).Gates[i].InFrom[0] = wireToGate(wires, inWire, circ, totalNumInputWires)
            if (*circ).Gates[i].InFrom[0] == -1 {
                return false
            }
            outWire, _ := strconv.Atoi(record[3])
            wires[outWire] = i
            
        default:
            fmt.Printf("Unknown gate type or instruction\n")
            return false
        }
    }
    
    // Now go through and connect all of the output gates
    for i := 0; i < totalNumOutputWires; i++ {
        if (*circ).connectOutputWire(wires[i + (numWires - totalNumOutputWires)], i) == false {
            return false
        }
    }
    
    // Success
    return true
}

//
// Convert a wire number into a gate number
func wireToGate(wires []int, wireNum int, circ *Circuit, totalWires int) int {

    // Any wire that's in 0...numInputWires-1 is an input wire
    if wireNum < (*circ).NumInputWires {
        return wireNum
    }
    
    // Any wire that's in the final numOutputWires should be recast into
    // the range numInputWires...(numInputWires+numOutputWires)-1
    if wireNum >= (totalWires-(*circ).NumOutputWires) && wireNum < totalWires {
        // return the number of the output gate
        return (*circ).getOutputGate(wireNum - (totalWires-(*circ).NumOutputWires))
    }
    
    
    // Any other wire should be in the wires[] array, which will give us
    // a mapping to a gate
    if wires[wireNum] != 0 {
        return wires[wireNum]
    } else {
        return -1
    }
}
