
/*! @file
 *  This is an example of the PIN tool that demonstrates some basic PIN APIs 
 *  and could serve as the starting point for developing your first PIN tool
 */

#include "pin.H"
#include <iostream>
#include <fstream>
#include <iomanip>
#include <unordered_map>

using std::cerr;
using std::endl;
using std::string;
using namespace std;

/* ================================================================== */
// Global variables
/* ================================================================== */

UINT64 icount = 0; //number of dynamically executed instructions
UINT64 p_icount    = 0; // number of predicated instructions 
UINT64 mem_icount = 0; // no of instructions that have at least one memory operand
UINT32 maxMemBytes = 0; // unsigned int
UINT64 totMemBytes = 0.0;

unordered_map<UINT32, UINT64> insLength;
unordered_map<UINT32, UINT64> numOp, numMemOp, numReadMemOp, numWriteMemOp;
unordered_map<UINT32, UINT64> regRead, regWrite;

unordered_map<UINT64, UINT32> InsFootprintMap; // Store index of instruction memory chunk and accessed indices
unordered_map<UINT64, UINT32> DataFootprintMap; // Store index of data memory chunk and accessed indices

// signed values
INT32 minI=INT_MAX, maxI=INT_MIN;
ADDRDELTA minDisp=INT_MAX, maxDisp=INT_MIN;

// Counters for All Instruction Types
UINT64 instrCount[17]; 
char const *instrNames[17] = {
    "Loads",
    "Stores",
    "NOPs",
    "Direct calls",
    "Indirect calls",
    "Returns",
    "Unconditional branches",
    "Conditional branches",
    "Logical operations",
    "Rotate and shift",
    "Flag operations",
    "Vector instructions",
    "Conditional moves",
    "MMX and SSE instructions",
    "System calls",
    "Floating-point",
    "The rest"
};

// Command Line Arguments
std::ostream* out = &cerr;
UINT64 fast_forward_count = 0;


/* ===================================================================== */
// Command line switches
/* ===================================================================== */
KNOB< string > KnobOutputFile(KNOB_MODE_WRITEONCE, "pintool", "o", "", "specify file name for MyPinTool output");

KNOB< INT32 > KnobFastFwdAmount(KNOB_MODE_WRITEONCE, "pintool", "f", "", "specify fast forward amount for MyPinTool output");

/* ===================================================================== */
// Utilities
/* ===================================================================== */

/*!
 *  Print out help message.
 */
INT32 Usage()
{
    cerr << "This tool prints out the number of dynamically executed " << endl
         << "instructions, basic blocks and threads in the application." << endl
         << endl;

    cerr << KNOB_BASE::StringKnobSummary() << endl;

    return -1;
}

/*!
 *  CPI Calculation, Called at Exit()
 */
double Compute_CPI(){
    UINT64 latency = 0;
    double time = 0.0;
    for (int i = 0; i < 17; i++)
    {
        if ((i == 0) || (i == 1))
            latency = 50;
        else
            latency = 1;

        time += latency*instrCount[i];
    }
    return time/p_icount;
}

/* ===================================================================== */
// Analysis routines
/* ===================================================================== */

VOID InsCount(void) {
    icount++; // total number of instructions (pred + non-pred)
}

VOID MemInsCount(void) {
    mem_icount++; // total number of memory instructions (pred)
}

ADDRINT Terminate(void){
    // To Terminate at the end of 1 billion instructions
    return (icount >= fast_forward_count + 1000000000);
}

ADDRINT FastForward(void) {
    // To inline Analysis Code
    return (icount >= fast_forward_count && icount);
}

VOID MyExitRoutine(void) {
    *out << endl << "===================================== Part A ===========================================";
    // Pretty Print
    *out
    << endl
    << left
    << setw(10)
    << "S.No"
    << left
    << setw(35)
    << "Instruction Type"
    << left
    << setw(30)
    << "Count"
    << left
    << setw(20)
    << "Percentage"
    << endl;

    for (int i = 0; i < 17; i++)
    {
        double percent = (double)instrCount[i]/p_icount*100;
        *out
        << std::fixed << std::setprecision(2)
        << left
        << setw(10)
        << i+1
        << left
        << setw(35)
        << instrNames[i]
        << left
        << setw(30)
        << instrCount[i]
        << left
        << setw(20)
        << percent
        << endl;
    }

    *out << endl << "Addition of all the counters = " << p_icount << endl;
    *out << "Instructions Executed (Total) = " << icount - fast_forward_count << endl;
    *out << "Number of Memory Instructions (Predicated) = " << mem_icount << endl;

    // Part B
    *out << endl << "===================================== Part B ===========================================";
    double cpi = Compute_CPI();
    *out << endl << "CPI = " << cpi << endl;

    // Part C
    *out << endl << "===================================== Part C ===========================================";
    *out << endl << "Instruction Footprint Size = " << (UINT64)(InsFootprintMap.size())*32 << " bytes" << endl;
    *out << "Data Footprint Size = " << (UINT64)(DataFootprintMap.size())*32 << " bytes" << endl;

    // Part D
    *out << endl << "===================================== Part D ===========================================";
    unordered_map<UINT32, UINT64>:: iterator itr;
    //1
    *out
    << endl
    << "D1: "
    << "Distribution of Instruction Lengths"
    << endl
    << left
    << setw(35)
    << "Length"
    << left
    << setw(30)
    << "Count"
    << endl;
    for (itr = insLength.begin(); itr != insLength.end(); itr++){
            *out
        << left
        << setw(35)
        << itr->first
        << left
        << setw(30)
        << itr->second
        << endl;
    }

    // 2
    *out
    << endl
    << "D2: "
    << "Distribution of  the number of operands"
    << endl
    << left
    << setw(35)
    << "Number of Operands"
    << left
    << setw(30)
    << "Count"
    << endl;
    for (itr = numOp.begin(); itr != numOp.end(); itr++){
            *out
        << left
        << setw(35)
        << itr->first
        << left
        << setw(30)
        << itr->second
        << endl;
    }

    // 3
    *out
    << endl
    << "D3: "
    << "Distribution of  the number of register read operands"
    << endl
    << left
    << setw(35)
    << "Number of Operands"
    << left
    << setw(30)
    << "Count"
    << endl;
    for (itr = regRead.begin(); itr != regRead.end(); itr++){
        *out
        << left
        << setw(35)
        << itr->first
        << left
        << setw(30)
        << itr->second
        << endl;
    }

    // 4
    *out
    << endl
    << "D4: "
    << "Distribution of  the number of register write operands"
    << endl
    << left
    << setw(35)
    << "Number of Operands"
    << left
    << setw(30)
    << "Count"
    << endl;
    for (itr = regWrite.begin(); itr != regWrite.end(); itr++){
        *out
        << left
        << setw(35)
        << itr->first
        << left
        << setw(30)
        << itr->second
        << endl;
    }

     // 5
    *out
    << endl
    << "D5: "
    << "Distribution of  the number of memory operands"
    << endl
    << left
    << setw(35)
    << "Number of Operands"
    << left
    << setw(30)
    << "Count"
    << endl;
    for (itr = numMemOp.begin(); itr != numMemOp.end(); itr++){
        *out
        << left
        << setw(35)
        << itr->first
        << left
        << setw(30)
        << itr->second
        << endl;
    }

     // 6
    *out
    << endl
    << "D6: "
    << "Distribution of  the number of memory read operands"
    << endl
    << left
    << setw(35)
    << "Number of Operands"
    << left
    << setw(30)
    << "Count"
    << endl;
    for (itr = numReadMemOp.begin(); itr != numReadMemOp.end(); itr++){
        *out
        << left
        << setw(35)
        << itr->first
        << left
        << setw(30)
        << itr->second
        << endl;
    }


     // 7
    *out
    << endl
    << "D7: " 
    << "Distribution of  the number of memory write operands"
    << endl
    << left
    << setw(35)
    << "Number of Operands"
    << left
    << setw(30)
    << "Count"
    << endl;
    for (itr = numWriteMemOp.begin(); itr != numWriteMemOp.end(); itr++){
        *out
        << left
        << setw(35)
        << itr->first
        << left
        << setw(30)
        << itr->second
        << endl;
    }


     // 8
     *out << endl << "D8: Maximum and average number of memory bytes touched" << endl;
     *out << "Maximum:" << maxMemBytes << endl;
     double avgMemBytes = (double)totMemBytes/mem_icount;
     *out << "Average:" << avgMemBytes << endl;

     // 9
    *out << endl << "D9: Maximum and minimum Value of Immediate Field" << endl;
    if ((maxI == INT_MIN) && (minI == INT_MAX)){
        *out << "No instruction with immediate operand";
    }
    else{
        *out << "Maximum:" << maxI << endl;
        *out << "Minimum:" << minI << endl;

    }

    // 10
    *out << endl << "D10: Maximum and minimum Value of Displacement Field" << endl;
    if ((maxDisp == INT_MIN) && (minDisp == INT_MAX)){
        *out << "No instruction with Displacement Field";
    }
        
    else{
        *out << "Maximum:" << maxDisp << endl;
        *out << "Minimum:" << minDisp << endl;
    }

    exit(0);
}

void MyPredicatedAnalysis(VOID * counter, UINT32 numReadOps, UINT32 numWriteOps) {
    // Predicated analysis routine(for Type A)
    *(UINT64 *)counter += 1;
    p_icount += 1;

    // Memory read operands in an instruction
    if (numReadMemOp.find(numReadOps) == numReadMemOp.end())
        numReadMemOp[numReadOps] = 1;
    else
        numReadMemOp[numReadOps] += 1;

    // Memory write operands in an instruction
    if (numWriteMemOp.find(numWriteOps) == numWriteMemOp.end())
        numWriteMemOp[numWriteOps] = 1;
    else
        numWriteMemOp[numWriteOps] += 1;

    // Total number of memory operands
    if (numMemOp.find(numReadOps + numWriteOps) == numMemOp.end())
        numMemOp[numReadOps + numWriteOps] = 1;
    else
        numMemOp[numReadOps + numWriteOps] += 1;

} 

void MyAnalysis(VOID *ip, UINT32 inSize, UINT32 operands, UINT32 regReadOperands, UINT32 regWriteOperands, INT32 minIValue, INT32 maxIValue){

    // Instruction Length
    if (insLength.find(inSize) == insLength.end())
        insLength[inSize] = 1;
    else
        insLength[inSize] += 1;

    // Number of Operands 
    if (numOp.find(operands) == numOp.end())
        numOp[operands] = 1;
    else
        numOp[operands] += 1;

    // Number of Reg Read operands
    if (regRead.find(regReadOperands) == regRead.end())
        regRead[regReadOperands] = 1;
    else
        regRead[regReadOperands] += 1;

    // Number of Reg Write operands 
    if (regWrite.find(regWriteOperands) == regWrite.end())
        regWrite[regWriteOperands] = 1;
    else
        regWrite[regWriteOperands] += 1;

    // Update Max and Min Values of Immediate
    if (minIValue < minI)
        minI = minIValue;
    
    if (maxIValue > maxI)
        maxI = maxIValue;

    // Update Instruction Footprint
    UINT64 start_chunk = (UINT64)ip/32; 
    UINT64 end_chunk = ((UINT64)ip + inSize)/32;
    for (UINT64 chunk = start_chunk; chunk <= end_chunk; chunk++)
    {
        InsFootprintMap[chunk] = 1;
    }

}


/* ===================================================================== */
//  Memory Analysis Routines
/* ===================================================================== */

// Called for (read/write) memory operands (Predicated Call)
VOID RecordMemRead(UINT32 numAccess)
{
    instrCount[0] += numAccess;
    p_icount += numAccess; 
}
VOID RecordMemWrite(UINT32 numAccess)
{
    instrCount[1] += numAccess;
    p_icount += numAccess;
}

// Called for All Memory operands (Predicated Call)
VOID DataFootprint(VOID * ip, UINT32 size){
    UINT64 start_chunk = (UINT64)ip/32; 
    UINT64 end_chunk = ((UINT64)ip + size)/32;

    for (UINT64 chunk = start_chunk; chunk <= end_chunk; chunk++)
    {
        DataFootprintMap[chunk] = 1;
    }
}

// Called once per memory instruction with predicated bit 1
VOID MemOperations(VOID *ip, UINT32 memSize, ADDRDELTA minDispValue, ADDRDELTA maxDispValue){

    // Update Memory bytes touched by the instruction
    if (memSize > maxMemBytes)
        maxMemBytes = memSize;
    totMemBytes += (UINT64)memSize;

    // Update Max and Min Values of Displacement Field
    if (minDispValue < minDisp)
        minDisp = minDispValue;

    if (maxDispValue > maxDisp)
        maxDisp = maxDispValue;
}


/* ===================================================================== */
// Instrumentation routines
/* ===================================================================== */

// Pin calls this function every time a new instruction is encountered
VOID Instruction(INS ins, VOID *v)
{

    // Exit
    INS_InsertIfCall(ins, IPOINT_BEFORE, (AFUNPTR) Terminate, IARG_END);
    INS_InsertThenCall(ins, IPOINT_BEFORE, (AFUNPTR)MyExitRoutine, IARG_END);

    // Increment Number of Instructions Executed
    INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR)InsCount, IARG_END);

    UINT32 insSize = INS_Size(ins);
    UINT32 operands = INS_OperandCount(ins);
    UINT32 regReadOperands = INS_MaxNumRRegs(ins); 
    UINT32 regWriteOperands = INS_MaxNumWRegs(ins);

    INT32 IValue;
    INT32 maxIValue = INT_MIN, minIvalue = INT_MAX;

    // Iterate through all the operands of an instruction (Part D9)
    for (UINT32 op = 0; op < operands; op++){
        if (INS_OperandIsImmediate(ins, op)){
            IValue = INS_OperandImmediate(ins, op);
            if ( IValue > maxIValue)
                maxIValue = IValue;
            if ( IValue < minIvalue)
                minIvalue = IValue;
        }
    }

    // (P + NP) Analysis - Part C(Ins Footprint), D1, D2, D3, D4, D9
    INS_InsertIfCall(ins, IPOINT_BEFORE, (AFUNPTR)FastForward, IARG_END);
    INS_InsertThenCall(
        ins, IPOINT_BEFORE, (AFUNPTR)MyAnalysis, 
        IARG_INST_PTR,
        IARG_UINT32, insSize, 
        IARG_UINT32, operands, 
        IARG_UINT32, regReadOperands, 
        IARG_UINT32, regWriteOperands, 
        IARG_ADDRINT, minIvalue,
        IARG_ADDRINT, maxIValue,
        IARG_END);


    UINT32 memOperands = INS_MemoryOperandCount(ins); 
    UINT32 numReadOps = 0, numWriteOps = 0;   
    ADDRDELTA maxDispValue = INT_MIN, minDispValue = INT_MAX; // store min/max within an instruction
    UINT32 memTouched = 0; 

    if (memOperands > 0) {
        // Count load and store instructions for type B

        // Increment number of memory instructions(P)
        INS_InsertIfCall(ins, IPOINT_BEFORE, (AFUNPTR)FastForward, IARG_END);
        INS_InsertThenPredicatedCall(ins, IPOINT_BEFORE, (AFUNPTR)MemInsCount, IARG_END);

        // Iterate over each memory operand of the instruction
        for (UINT32 memOp = 0; memOp < memOperands; memOp++)
        {
            UINT32 memOpSize = INS_MemoryOperandSize(ins, memOp);
            UINT32 accessSize = 4; // 4 bytes = 32 bits
            UINT32 extra = (memOpSize % accessSize > 0) ? 1 : 0; // extra block
            UINT32 numAccesses = (memOpSize / accessSize) + extra;

            // Update min/max Displacement for the memory instruction
            ADDRDELTA memOpdisp = INS_OperandMemoryDisplacement(ins, memOp);
            if (memOpdisp > maxDispValue){
                maxDispValue = memOpdisp;
            }
            if (memOpdisp < minDispValue){
                minDispValue = memOpdisp;
            }

            // Read
            if (INS_MemoryOperandIsRead(ins, memOp))
            {
                memTouched += memOpSize;
                numReadOps += 1;
                INS_InsertIfCall(ins, IPOINT_BEFORE, (AFUNPTR)FastForward, IARG_END);
                INS_InsertThenPredicatedCall(
                    ins, IPOINT_BEFORE, (AFUNPTR)RecordMemRead,
                    IARG_UINT32, numAccesses,
                    IARG_END);
            }
            
            // Write
            if (INS_MemoryOperandIsWritten(ins, memOp))
            {
                memTouched += memOpSize;
                numWriteOps += 1;
                INS_InsertIfCall(ins, IPOINT_BEFORE, (AFUNPTR)FastForward, IARG_END);
                INS_InsertThenPredicatedCall(
                    ins, IPOINT_BEFORE, (AFUNPTR)RecordMemWrite,
                    IARG_UINT32, numAccesses,
                    IARG_END);
            }

            // Update Data Footprint for each memory operand(P) - Part C(Data Footprint)
            INS_InsertIfCall(ins, IPOINT_BEFORE, (AFUNPTR)FastForward, IARG_END);
            INS_InsertThenPredicatedCall(
                    ins, IPOINT_BEFORE, (AFUNPTR)DataFootprint,
                    IARG_MEMORYOP_EA, memOp,
                    IARG_UINT32, memOpSize,
                    IARG_END);
        }

        // Update Memory Operations Data for each memory instruction(P) - Part D8 and D10
        INS_InsertIfCall(ins, IPOINT_BEFORE, (AFUNPTR)FastForward, IARG_END);
        INS_InsertThenPredicatedCall(
                ins, IPOINT_BEFORE, (AFUNPTR)MemOperations,
                IARG_INST_PTR,
                IARG_UINT32, memTouched,
                IARG_ADDRINT, minDispValue,
                IARG_ADDRINT, maxDispValue,
                IARG_END);
    }

    // Categorize all instructions for type A
    UINT64 * counter;
    if (INS_Category(ins) == XED_CATEGORY_NOP) {
        // Increment NOP count by one
        counter = &instrCount[2];    
    }

    else if (INS_Category(ins) == XED_CATEGORY_CALL) {
        if (INS_IsDirectCall(ins)) {
            // Increment direct call count by one
            counter = &instrCount[3];
        }
        else {
            // Increment indirect call count by one
            counter = &instrCount[4];
        }
    }
    else if (INS_Category(ins) == XED_CATEGORY_RET) {
        // Return 
        counter = &instrCount[5];
        
    }
    else if (INS_Category(ins) == XED_CATEGORY_UNCOND_BR) {
        // Unconditional branches 
        counter = &instrCount[6];
    }
    
    else if (INS_Category(ins) == XED_CATEGORY_COND_BR) {
        // Conditional branches 
        counter = &instrCount[7];
    }

    else if (INS_Category(ins) == XED_CATEGORY_LOGICAL) {
        // Logical operations
        counter = &instrCount[8];
    }

    else if ((INS_Category(ins) == XED_CATEGORY_ROTATE) || (INS_Category(ins) == XED_CATEGORY_SHIFT)) {
        // Increment rotate and shift count by one
        counter = &instrCount[9];
    }

    else if (INS_Category(ins) == XED_CATEGORY_FLAGOP) {
        // Flag operations 
        counter = &instrCount[10];
    }

    else if ((INS_Category(ins) == XED_CATEGORY_AVX) || (INS_Category(ins) == XED_CATEGORY_AVX2) 
    || (INS_Category(ins) == XED_CATEGORY_AVX2GATHER) || (INS_Category(ins) == XED_CATEGORY_AVX512)) {
        // Vector instructions
        counter = &instrCount[11];
    }

    else if (INS_Category(ins) == XED_CATEGORY_CMOV) {
        // Conditional moves 
        counter = &instrCount[12];
    }

    else if ((INS_Category(ins) == XED_CATEGORY_MMX) || (INS_Category(ins) == XED_CATEGORY_SSE)) {
        // MMX and SSE instructions
        counter = &instrCount[13];
    }

    else if (INS_Category(ins) == XED_CATEGORY_SYSCALL) {
        //  System calls 
        counter = &instrCount[14];
    }
    
    else if (INS_Category(ins) == XED_CATEGORY_X87_ALU) {
        // Floating Point
        counter = &instrCount[15];
    } 
    else {
        // Others
        counter = &instrCount[16];
    } 

    // (P) Analysis - Part A, D5, D6, D7
    INS_InsertIfCall(ins, IPOINT_BEFORE, (AFUNPTR)FastForward, IARG_END);
    INS_InsertThenPredicatedCall(
        ins, IPOINT_BEFORE, (AFUNPTR)MyPredicatedAnalysis,
        IARG_PTR, counter, 
        IARG_UINT32, numReadOps, 
        IARG_UINT32, numWriteOps, 
        IARG_END); 
}

/*!
 * Print out analysis results.
 * This function is called when the application exits.
 * @param[in]   code            exit code of the application
 * @param[in]   v               value specified by the tool in the 
 *                              PIN_AddFiniFunction function call
 */

VOID Fini(INT32 code, VOID* v)
{
    *out << "===============================================" << endl;
    *out << "HW1 analysis results: " << endl;
    // *out << "Number of instructions: " << insCount << endl;
    // *out << "Number of basic blocks: " << bblCount << endl;
    // *out << "Number of threads: " << threadCount << endl;
    *out << "===============================================" << endl;

    MyExitRoutine();

}

/*!
 * The main procedure of the tool.
 * This function is called when the application image is loaded but not yet started.
 * @param[in]   argc            total number of elements in the argv array
 * @param[in]   argv            array of command line arguments, 
 *                              including pin -t <toolname> -- ...
 */
int main(int argc, char* argv[])
{
    // Initialize PIN library. Print help message if -h(elp) is specified
    // in the command line or the command line is invalid
    if (PIN_Init(argc, argv))
    {
        return Usage();
    }

    string fileName = KnobOutputFile.Value();

    if (!fileName.empty())
    {
        out = new std::ofstream(fileName.c_str());
    }

    fast_forward_count = (UINT64)KnobFastFwdAmount.Value()*1000000000;
    *out << "Fast Forward Amount:  " << fast_forward_count << endl;
    
        
    // Register Instruction to be called to instrument instructions
    INS_AddInstrumentFunction(Instruction, 0);

    // Register function to be called when the application exits
    PIN_AddFiniFunction(Fini, 0);

    cerr << "===============================================" << endl;
    cerr << "This application is instrumented by HW1 Tool" << endl;
    if (!KnobOutputFile.Value().empty())
    {
        cerr << "See file " << KnobOutputFile.Value() << " for analysis results" << endl;
    }
    cerr << "===============================================" << endl;

    // Start the program, never returns
    PIN_StartProgram();

    return 0;
}

/* ===================================================================== */
/* eof */
/* ===================================================================== */
