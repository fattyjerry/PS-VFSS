package toygarble

import (
    "os"
    "fmt"
)

//
// Constants and types for gate types
//

type GateType_t int

const (
    MAX_INPUT_DEGREE    int = 2
)

const (
    GateINPUT   GateType_t = 0
    GateOUTPUT  GateType_t = 1
    GateAND     GateType_t = 2
    GateOR      GateType_t = 3
    GateNOT     GateType_t = 4
    GateXOR     GateType_t = 5
    GateCONST   GateType_t = 6
    GateCOPY    GateType_t = 7
)

// Max input wires for gates described above
var min_input_wires = [...]int {0, 0, 2, 2, 1, 2, 0, 1}
var max_input_wires = [...]int {0, 1, 2, 2, 1, 2, 0, 1}

type Circuit struct {
    // Total number of input and output wires
    NumInputWires   int
    NumOutputWires  int

    // Number of input variables, and how they are divided into wires
    NumInputVars    int
    NumWiresIV      []int
    
    // Number of output variables, and how they are divided into wires
    NumOutputVars    int
    NumWiresOV       []int
    
    Gates           []Gate
}

type Gate struct {
    GateType    GateType_t
    ConstVal    bool
    InFrom      []int
}

//
// Initialize an empty circuit. Takes in the number of input and
// output wires.
//

func (circ *Circuit) initializeCircuit(numInputWires int, numOutputWires int, numInputVars int, numOutputVars int, numWiresPerIV []int, numWiresPerOV []int) {
    // Set the number of input and output wires
    circ.NumInputWires = numInputWires
    circ.NumOutputWires = numOutputWires
    
    // Set the number of input and output variables, plus their wire count arrays
    circ.NumInputVars = numInputVars
    circ.NumOutputVars = numOutputVars
    circ.NumWiresIV = numWiresPerIV
    circ.NumWiresOV = numWiresPerOV
    
    // Initialize the gate array with input and output wire "gates"
    for i := 0; i < numInputWires; i++ {
        circ.addGate(GateINPUT, false, nil)
    }
    
    for i := 0; i < numOutputWires; i++ {
        circ.addGate(GateOUTPUT, false, nil)
    }
}

// Adds a new gate. Returns -1 if the gate is invalid.
func (circ *Circuit) addGate(gateType GateType_t, constVal bool, inFrom []int) int {
    // Make sure the gate has the correct number of input wires
    if len(inFrom) < min_input_wires[gateType] || len(inFrom) > max_input_wires[gateType] {
        fmt.Printf("ERROR ADDING GATE, type = %d, input = %d, min = %d, max = %d\n", gateType, len(inFrom), min_input_wires[gateType], max_input_wires[gateType])
        return -1
    }
    
    newGate := Gate{gateType, constVal, inFrom}
    circ.Gates = append(circ.Gates, newGate)
    return len(circ.Gates) - 1
}

// Adds a new gate with two inputs
func (circ *Circuit) addGate2(gateType GateType_t, inFrom1 int, inFrom2 int) int {
    gates := make([]int, 2)
    gates[0] = inFrom1
    gates[1] = inFrom2
    return circ.addGate(gateType, false, gates)
}

// Connects an gate to an output wire
func (circ *Circuit) connectOutputWire(gateNum int, outputNum int) bool {
    //fmt.Printf("connectOutputWire(%d, %d)\n", gateNum, circ.getOutputGate(outputNum))
    if len((circ.Gates[circ.getOutputGate(outputNum)].InFrom)) == 0 {
        circ.Gates[circ.getOutputGate(outputNum)].InFrom = append(circ.Gates[circ.getOutputGate(outputNum)].InFrom, gateNum)
        return true
    } else {
        return false
    }
}

// Check the structure of a circuit to make sure it is valid, and can
// be executed or garbled
func (circ *Circuit) validCircuit() bool {
    // Make sure there are a correct number of gates in the circuit
    if len(circ.Gates) < (circ.NumInputWires + circ.NumOutputWires) {
        return false
    }
    
    // Go through each gate and make sure it is properly connected
    for i := 0; i < len(circ.Gates); i++ {
        if len(circ.Gates[i].InFrom) < min_input_wires[circ.Gates[i].GateType] ||
            len(circ.Gates[i].InFrom) > max_input_wires[circ.Gates[i].GateType] {
            // This gate doesn't have the right number of connected input wires
            return false
        }
    }
            
    return true
}

// Circuit evaluation on concrete inputs. Returns success/failure and a list of output bits.
// Inefficient algorithm used for testing.
func (circ *Circuit) EvaluateCircuit(inputBits []bool) (bool, []bool) {
    // Make sure the number of input and output gates is correct
    if len(inputBits) != circ.NumInputWires || circ.NumOutputWires < 1 {
        return false, nil
    }
    
    // Allocate return var and scratch variables to hold onto intermediate values
    visited := make([]bool, len(circ.Gates))    // defaults to all false
    calculated := make([]bool, len(circ.Gates)) // defaults to all false
    values := make([]bool, len(circ.Gates)) // defaults to all false
    result := make([]bool, circ.NumOutputWires)    // defaults to all false
    
    // For each output gate, recursively evaluate the entire circuit
    // using the scratch variables
    for i := 0; i < circ.NumOutputWires; i++ {
        // Initialize the visited array to all zero, except for this output gate
        for j := range visited {
            visited[j] = false
        }
        
        // Evaluate the output gate to get a result, error out if it fails
        success, resultBit := circ.evaluateGate(circ.getOutputGate(i), &visited, &calculated, &values, &inputBits)
        result[i] = resultBit
        if success == false {
            fmt.Printf("Failed\n")
            return false, nil
        }
    }
    
    // Success
    return true, result
}
        
// Gate evaluation for concrete inputs, recursive subroutine
func (circ *Circuit) evaluateGate(gateID int, visited *[]bool, calculated *[]bool, values *[]bool, inputs *[]bool) (bool, bool) {

    var success1    bool
    var success2    bool
    var result1     bool
    var result2     bool

    // If the gate has already been visited, but not calculated, we're in a loop -- return an error
    if (*visited)[gateID] == true && (*calculated)[gateID] == false {
        return false, false
    }
    
    // If the gate has been calculated, we're done (but in a good way). Return the cached value.
    if (*calculated)[gateID] == true {
        return true, (*values)[gateID]
    }
    
    // Evaluate the gate
    (*visited)[gateID] = true
    result := false
    success := true
    
    // If this is not an input "gate", recurse on any inputs
    if circ.Gates[gateID].GateType != GateINPUT {
        success1, result1 = circ.evaluateGate(circ.Gates[gateID].InFrom[0], visited, calculated, values, inputs)
        
        if len(circ.Gates[gateID].InFrom) == 2 {
            success2, result2 = circ.evaluateGate(circ.Gates[gateID].InFrom[1], visited, calculated, values, inputs)
        }
    }
    
    switch circ.Gates[gateID].GateType {
    case GateINPUT:
        //fmt.Printf("Evaluating IN  gate %d\n", gateID)
        result = (*inputs)[gateID] // TODO: change this in case input gates aren't 0-aligned
        
    case GateOUTPUT:
        // Output "gates" are equal to whatever (solitary) predecessor gate they're wired to,
        // so we recurse on that
        if len(circ.Gates[gateID].InFrom) == 1 {
            //fmt.Printf("Evaluating OUT gate %d\n", gateID)
            success = success1
            result = result1
        } else {
            success = false
            os.Stderr.WriteString("Error evaluating output 'gate', wrong number of input wires")
        }
        
        //fmt.Printf("Success\n")
    case GateCOPY:
        if len(circ.Gates[gateID].InFrom) == 1 {
            success = success1
            result = result1
        } else {
            success = false
            os.Stderr.WriteString("Error evaluating copy gate, there should only be one input")
        }
        
    case GateAND:
        // AND gates must have two inputs, which we recurse on
        if len(circ.Gates[gateID].InFrom) == 2 {
            //fmt.Printf("Evaluating AND gate %d\n", gateID)

            if (success1 && success2) == true {
                result = result1 && result2
            } else {
                fmt.Printf("AND error\n")
                success = false
            }
            
        } else {
            success = false
            os.Stderr.WriteString("Error evaluating AND 'gate', wrong number of input wires")
        }
    
    case GateXOR:
        // XOR gates must have two inputs, which we recurse on
        if len(circ.Gates[gateID].InFrom) == 2 {
            //fmt.Printf("Evaluating XOR gate %d\n", gateID)

            if (success1 && success2) == true {
                result = result1 != result2
            } else {
                fmt.Printf("XOR error\n")
                success = false
            }
            
        } else {
            success = false
            os.Stderr.WriteString("Error evaluating XOR 'gate', wrong number of input wires")
        }
        
    case GateCONST:
        // CONST gates have no inputs, only a constant, which we encode in
        // the wire number as a hack
        if len(circ.Gates[gateID].InFrom) == 0 {
            //fmt.Printf("Evaluating CONST gate %d\n", gateID)
            result = circ.Gates[gateID].ConstVal
        }
        
    case GateOR:
        // OR gates must have two inputs, which we recurse on
        if len(circ.Gates[gateID].InFrom) == 2 {
            //fmt.Printf("Evaluating OR gate %d\n", gateID)

            if success1 && success2 == true {
                result = result1 || result2
            } else {
                success = false
            }
        } else {
            success = false
            os.Stderr.WriteString("Error evaluating AND 'gate', wrong number of input wires")
        }
            
    case GateNOT:
        // NOT gates must have one input, which we recurse on
        if len(circ.Gates[gateID].InFrom) == 1 {
            if success1 == true {
                result = !result1
            } else {
                success = false
            }
        } else {
            success = false
            os.Stderr.WriteString("Error evaluating NOT 'gate', wrong number of input wires")
        }
            
        default:
            fmt.Printf("Unknown gate type %d for %d\n", circ.Gates[gateID].GateType, gateID)
            success = false

    }
    
    if success == false {
        fmt.Printf("Error in gate %d\n", gateID)
    } else {
        (*calculated)[gateID] = true
        (*values)[gateID] = result
    }

    return success, result
}

// Get the gate identities corresponding to specific input wires
func (circ *Circuit) getInputGate(inputWireNo int) int {
    return inputWireNo
}

// Get the gate identities corresponding to specific output wires
func (circ *Circuit) getOutputGate(outputWireNo int) int {
    return circ.NumInputWires + outputWireNo
}

// Convert an array of []byte values into a boolean array
// that's bit-aligned with the circuit inputs
func (circ *Circuit) PadInputsToBoolArray(inputBufs [][]byte) []bool {
    
    currentLoc := 0
    result := make([]bool, circ.NumInputWires)
    
    // Check that the number of given buffers is less than or equal to
    // the number of input variables
    if (len(inputBufs) > circ.NumInputVars) {
        return nil
    }
    
    // Go through each given input and unpack it
    for i := 0; i < len(inputBufs); i++ {
        // First make sure the input is the right length
        if (len(inputBufs[i]) * 8) > circ.NumWiresIV[i] {
            // Input is too big
            return nil
        }
        
        // Copy the input into the bool array
        for j := 0; j < circ.NumWiresIV[i]; j++ {
            //fmt.Printf("currentLoc = %d, i=%d, j=%d, lenInputBufs=%d\n", currentLoc, i, j, len(inputBufs[i]))
            
            if (j/8) < len(inputBufs[i]) {
                // Grab a bit off the buffer
                result[currentLoc] = (int(inputBufs[i][len(inputBufs[i]) - (j/8) - 1]) & (1 << (j % 8)) != 0)
            } else {
                // End of input buffer, pad remaining bits with false
                result[currentLoc] = false
            }
            currentLoc++
        }
    }
    
    // Return the array
    return result
}

//
// Convert an evaluated array of boolean wire values into binary fields,
// one for each output variable in the circuit
func (circ *Circuit) DecodeOutputVariables(outWires []bool) [][]byte {
    
    // Make sure the total number of wires matches what we expect
    if circ.NumOutputWires != len(outWires) {
        return nil
    }
    
    result := make([][]byte, circ.NumOutputVars)
    currentWire := 0
    
    // Go through each output variable and compute a slice that
    // corresponds just to that variable
    for i := 0; i < circ.NumOutputVars; i++ {
        boolSlice := outWires[currentWire:(currentWire+circ.NumWiresOV[i])]
        result[i] = boolArrayToBytes(boolSlice)
        currentWire += circ.NumWiresOV[i]
    }
    
    // Success
    return result
}

// Convert an array of []bool into a byte array
func boolArrayToBytes(input []bool) []byte {

    result := make([]byte, (len(input)+7) / 8)

    for i := 0; i < len(input); i++ {
        if input[i] == true {
            result[len(result) - (i / 8) - 1] += (1 << (i % 8))
        }
    }

    return result
}

// Convert an int64 into a bool array for evaluation
func int64toBoolArray(input int64) []bool {
    result := make([]bool, 64)
    
    for i := 63; i >= 0; i-- {
        result[i] = (input & (1 << i) != 0)
        fmt.Printf("bool=%t\n", result[i])
    }
    
    fmt.Printf("encoding integer=%d\n", input)
    
    return result
}

// Convert an int64 array into a bool array for evaluation
func int64ArraytoBoolArray(input []int64) []bool {
    
    result := make([]bool, 0)
    
    for i := 0; i < len(input); i++ {
        result = append(result, int64toBoolArray(input[i])...)
    }
    
    return result
}

// Convert a bool array back into an int64
func boolArrayToInt64(input []bool) int64 {
    
    result := int64(0)
    
    for i := 0; i < len(input); i++ {
        if input[i] == true {
            result += (1 << i)
        }
    }
    
    return result
}

