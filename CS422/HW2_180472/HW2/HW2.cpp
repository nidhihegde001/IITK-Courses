#include "pin.H"
#include <iostream>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <unordered_map>
#include<vector>
#include <string> 
#include <deque>  

using std::string;
using namespace std;

/* ================================================================== */
// Global variables
/* ================================================================== */
int debug = 0;
UINT64 icount = 0; //number of dynamically executed instructions

// Prediction Counts
UINT64 forward_wrongPred[8];
UINT64 backward_wrongPred[8];
UINT64 overall_wrongPred[8];

UINT64 forward_branches = 0;
UINT64 backward_branches = 0;
UINT64 total_branches = 0;

UINT64 miss[2] = {0,0};
UINT64 mispred[2] = {0,0};
UINT64 total_indirectCF = 0;

const char* DPNames[] = {
    "FNBT",
    "Bimodal",
    "SAg",
    "GAg",
    "gshare",
    "SAg and GAg Hybrid",
    "SAg, GAg, and gshare Hybrid(Majority)",
    "SAg, GAg, and gshare Hybrid(Tournament)"
};

const char* BTB_Names[] = {
    "BTB(Indexed with PC)",
    "BTB(Indexed with PC and GHR Hash)"
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


INT32 Usage()
{
    cerr << "This tool is written as a part of HW2 solution for the course CS422 " << endl
         << endl;

    cerr << KNOB_BASE::StringKnobSummary() << endl;

    return -1;
}

// Print binary form of an integer
void bin(ADDRINT n)
{
    if (n > 1)
        bin(n >> 1);

    int ans = n & 1;
 
    *out << ans;
}


/* ===================================================================== */
// Part A Class Definitions 
/* ===================================================================== */
class GlobalHistoryRegister
{
    public:
        ADDRINT value; 
        int width;

        GlobalHistoryRegister(int history_width)
        {
            value = 0;
            width = history_width;
        }

        void UpdateHistory(bool prediction)
        {
            value = ((value << 1) % (1 << width)) + prediction;
        }

        void printHistory()
        {
            bin(value);
            *out << endl;
        }
};

class NbitSaturatingCounter 
{

    private:
        int value; // counter value
        int maxVal; // min Value
        int minVal; // max Value

    public:
        NbitSaturatingCounter(int N){
            value = 0;  // initialization to 0
            minVal = 0;
            maxVal = (1 << N) - 1;
        }

        void UpdateCounter(bool prediction){ // 1 if Taken

            if ((value == minVal) && (prediction == 0))
                return;

            if ((value == maxVal) && (prediction))
                return;

            if (prediction)
                value += 1;
            else
                value -= 1;   
        }

        bool PredictBranch(){
            if (value >= (maxVal+1)/2)
                return 1; // Taken
            else
                return 0; // Not Taken
        }


};


///////////////////////////////////////////////////////////////////////* Base Classes (Virtual) */////////////////////////////////////////////////////////////
class StaticDirectionPredictor
{
    public:
    virtual bool GetPrediction(bool outcome) = 0; // taken/forward etc.

};

class AdaptiveDirectionPredictor 
{
    public:
        virtual void UpdatePredictor(ADDRINT pc, bool taken) = 0;
        virtual bool GetPrediction(ADDRINT pc) = 0;

    protected:
        // All Adaptive DP's have a PHT
        int PHT_size;
        int PHT_entry_size;
        vector<NbitSaturatingCounter> PHT;
};

///////////////////////////////////////////////////////////////////////* Direction Predictors */////////////////////////////////////////////////////////////
class FNBT : public StaticDirectionPredictor
{
    public:
        bool GetPrediction(bool is_forward_branch)
        {
            return !(is_forward_branch);
        }
};

class BimodalPredictor : public AdaptiveDirectionPredictor 
{
    public:
        BimodalPredictor(int num_entries, int size)
        {
            PHT_size = num_entries;
            PHT_entry_size = size;

            for (int i = 0; i < PHT_size; i++)
                PHT.push_back(NbitSaturatingCounter(size)); // initialise PHT with all entries 0

        }

        void UpdatePredictor(ADDRINT pc, bool taken)
        {
            int index = pc % PHT_size;
            PHT[index].UpdateCounter(taken); // Update respective Counter
        }

        bool GetPrediction(ADDRINT pc)
        {
            int index = pc % PHT_size;
            return PHT[index].PredictBranch();
        }
};

class SAg : public AdaptiveDirectionPredictor 
{
    private:
        // define BHT variables
        int BHT_size;
        int BHT_entry_size;
        vector <int> BHT;
        // maintain PHT counter index used in the last prediction
        int last_pred_index; 

    public:
        SAg(int m, int K, int counter_size)
        {
            BHT_size = K;
            BHT_entry_size = m;
            PHT_size = 1 << m;
            PHT_entry_size = counter_size;
            last_pred_index = -1; 

            for (int i = 0; i < BHT_size; i++)
                BHT.push_back(0); // initialise BHT with all entries 0
            for (int i = 0; i < PHT_size; i++)
                PHT.push_back(NbitSaturatingCounter(PHT_entry_size)); // initialise PHT with all entries 0

        }

        void UpdatePredictor(ADDRINT pc, bool taken)
        {
            // index into BHT
            int BHT_index = pc % BHT_size;
            int history = BHT[BHT_index];

            BHT[BHT_index] = ((history << 1) + taken) % (1 << BHT_entry_size); // Update the history of the current PC index
            if (last_pred_index < 0)
            {
                printf("Error while Updating PHT entry: Entry does not exist at index %d", last_pred_index);
                return; 
            } 
            PHT[last_pred_index].UpdateCounter(taken); // Update the Counter that made the last prediction
        }

        bool GetPrediction(ADDRINT pc)
        {
            int BHT_index = pc % BHT_size;
            int history = BHT[BHT_index];
            int PHT_index = history % PHT_size;
            last_pred_index = PHT_index; // Carry forward the PHT index till the prediction is verified 
            return PHT[PHT_index].PredictBranch();
        }
};

class GAg : public AdaptiveDirectionPredictor 
{
    
    private:
        int last_pred_index; // maintain PHT counter index for a prediction
        GlobalHistoryRegister *GHR;

    public:
        GAg(int counter_size, GlobalHistoryRegister *ghr)
        {
            int ghr_width = ghr->width;
            PHT_size = 1 << ghr_width;
            PHT_entry_size = counter_size;
            last_pred_index = -1; 
            GHR = ghr;

            for (int i = 0; i < PHT_size; i++)
                PHT.push_back(NbitSaturatingCounter(PHT_entry_size)); // initialise PHT with all entries 0

        }

        void UpdatePredictor(ADDRINT pc, bool taken)
        {
            if (last_pred_index < 0)
            {
                printf("Error while Updating PHT entry: Entry does not exist at index %d", last_pred_index);
                return; 
            } 
            PHT[last_pred_index].UpdateCounter(taken); // Update the Counter that made the last prediction
        }

        bool GetPrediction(ADDRINT pc)
        {
            int history = GHR->value;
            int PHT_index = history % PHT_size;
            last_pred_index = PHT_index; // Carry forward the PHT index till the prediction is verified 
            return PHT[PHT_index].PredictBranch();
        }
};


class gshare : public AdaptiveDirectionPredictor {
    private:
        int last_pred_index; // maintain PHT counter index for a prediction
        GlobalHistoryRegister *GHR;

    public:
        gshare(int counter_size, GlobalHistoryRegister *ghr)
        {
            int ghr_width = ghr->width;
            PHT_size = 1 << ghr_width;
            PHT_entry_size = counter_size;
            last_pred_index = -1; 
            GHR = ghr;

            for (int i = 0; i < PHT_size; i++)
                PHT.push_back(NbitSaturatingCounter(PHT_entry_size)); // initialise PHT with all entries 0
        }


        void UpdatePredictor(ADDRINT pc, bool taken)
        {
            if (last_pred_index < 0)
            {
                printf("Error while Updating PHT entry: Entry does not exist at index %d", last_pred_index);
                return; 
            } 
            PHT[last_pred_index].UpdateCounter(taken); // Update the Counter that made the last prediction
        }

        bool GetPrediction(ADDRINT pc)
        {
            int history = GHR->value;
            int PHT_index = (history^pc) % PHT_size;
            last_pred_index = PHT_index; // Carry forward the PHT index till the prediction is verified 
            return PHT[PHT_index].PredictBranch();
        }
};


class SAgGAgHybrid
{
    private: 
        SAg* SAgPredictor;
        GAg* GAgPredictor;
        BimodalPredictor* selector;
        GlobalHistoryRegister *GHR;

    public:
        SAgGAgHybrid(SAg* sag, GAg* gag, GlobalHistoryRegister *ghr, int selector_width)
        {
            SAgPredictor = sag;
            GAgPredictor = gag;
            GHR = ghr;
            int num_entries = 1 << ghr->width;
            selector = new BimodalPredictor(num_entries, selector_width);
        }

        bool PredictandSelectorUpdate(ADDRINT pc, bool taken)
        {
            bool SAgPred = SAgPredictor->GetPrediction(pc);
            bool GAgPred = GAgPredictor->GetPrediction(pc);
            bool select = selector->GetPrediction(GHR->value); // select SAg(0) or GAg(1) prediction

            int update_bit = 0;

            // Both predictions agree , do nothing
            if (SAgPred != GAgPred)
            {
                if (SAgPred == taken)
                    update_bit = 0;

                else if (GAgPred == taken)
                    update_bit = 1;

                selector->UpdatePredictor(GHR->value, update_bit); // Increment counter if GAg is correct
                
            }

            // Return Prediction
            if (select == 0)
                return SAgPred;
            else
                return GAgPred;
        }
};


class SAgGAgGshareHybrid_Majority
{
    
    private: 
        SAg* SAgPredictor;
        GAg* GAgPredictor;
        gshare* gsharePredictor;

    public:
        SAgGAgGshareHybrid_Majority(SAg* sag, GAg* gag, gshare* g_share)
        {
            SAgPredictor = sag;
            GAgPredictor = gag;
            gsharePredictor = g_share;
        }

        bool Predict(ADDRINT pc)
        {
            // Get predictions
            bool SAgPred = SAgPredictor->GetPrediction(pc);
            bool GAgPred = GAgPredictor->GetPrediction(pc);
            bool gsharePred = gsharePredictor->GetPrediction(pc);

            // Majority Prediction
            bool majority_pred = false;
            if (SAgPred + GAgPred + gsharePred >=2)
                majority_pred = true;

            return majority_pred; 
        }

};


class SAgGAgGshareHybrid_Tournament
{
    private: 
        SAg* SAgPredictor;
        GAg* GAgPredictor;
        gshare* gsharePredictor;
        GlobalHistoryRegister *GHR;

        // Tournament Meta-Predictor
        BimodalPredictor* selector1;
        BimodalPredictor* selector2;
        BimodalPredictor* selector3;

    public:
        SAgGAgGshareHybrid_Tournament(SAg* sag, GAg* gag, gshare* g_share, GlobalHistoryRegister *ghr, int selector_width)
        {
            SAgPredictor = sag;
            GAgPredictor = gag;
            gsharePredictor = g_share;
            GHR = ghr;

            int num_entries = 1 << ghr->width; // 512
            selector1 = new BimodalPredictor(num_entries, selector_width); // SAg and GAg
            selector2 = new BimodalPredictor(num_entries, selector_width); // GAg and gshare
            selector3 = new BimodalPredictor(num_entries, selector_width); // gshare and SAg
        }

        bool PredictandSelectorUpdate(ADDRINT pc, bool taken)
        {

            bool SAgPred = SAgPredictor->GetPrediction(pc);
            bool GAgPred = GAgPredictor->GetPrediction(pc);
            bool gsharePred = gsharePredictor->GetPrediction(pc);

            // Select criteria
            bool select1 = selector1->GetPrediction(GHR->value); // select SAg(0) or GAg(1) prediction
            bool select2 = selector2->GetPrediction(GHR->value); // select GAg(0) or gshare(1) prediction
            bool select3 = selector3->GetPrediction(GHR->value); // select gshare(0) or SAg(1) prediction

            // Compute Update Bits
            int update_bit[3] = {0, 0, 0};
            if (SAgPred != GAgPred) // selector 1 update
            {
                if (SAgPred == taken)
                    update_bit[0] = 0;

                else if (GAgPred == taken)
                    update_bit[0] = 1;       

                selector1->UpdatePredictor(GHR->value, update_bit[0]);         
            }

            if (GAgPred != gsharePred) // selector 2 update
            {
                if (GAgPred == taken)
                    update_bit[1] = 0;

                else if (gsharePred == taken)
                    update_bit[1] = 1;

                selector2->UpdatePredictor(GHR->value, update_bit[1]);
            }

            if (gsharePred != SAgPred) // selector 3 update
            {
                if (gsharePred == taken)
                    update_bit[2] = 0;

                else if (SAgPred == taken)
                    update_bit[2] = 1;

                selector3->UpdatePredictor(GHR->value, update_bit[2]);
            }


            // Decide Prediction
            if (select1 == 0)
            { // Sag wins to GAg
                if (select3 == 0)
                    return gsharePred;
                else
                    return SAgPred;
            }
            else
            { // GAg wins to SAg
                 if (select2 == 0)
                    return GAgPred;
                else
                    return gsharePred;
            }
        }

};

/* ===================================================================== */
// Part B Class Definitions 
/* ===================================================================== */

class BTB_entry
{
    public:
        bool valid;
        ADDRINT tag;
        ADDRINT target;
        BTB_entry(ADDRINT TAG, ADDRINT TARGET, bool VALID)
        {
            tag = TAG;
            target = TARGET;
            valid = VALID;
        }
};

class BTB_set
{
   public:
        deque <BTB_entry> set_; // in order of most recently accessed (LRU state) - Oldest entry at the back odf queue
        BTB_set()
        {
            set_.clear();
        }
};


class BTB1
{
    public:
        vector <BTB_set> BTB_table; // each set can contain one or more BTB entries
        UINT32 set_size; // number of elements in a set
        int num_sets;

        BTB1(int sets, UINT32 ways)
        {
            set_size = ways;
            num_sets = sets;
            BTB_table.clear();
            for (int i = 0; i < num_sets; i++){
                BTB_set *new_set = new BTB_set();
                BTB_table.push_back(*new_set); // empty BTB_set object
            }
        }

        void BTB1_Update(ADDRINT pc, ADDRINT target, ADDRINT next_pc) {

            int index = pc % num_sets; // index into correct set
            ADDRINT tag = pc >> (UINT32)log2(num_sets); // remaining bits match the tag

            BTB_entry *curr_entry = NULL;
            int entry_index = -1;
            bool is_miss = true;

            // Iterate through entries in that set
            for (UINT32 e = 0; e < BTB_table[index].set_.size(); e++){
                if ((BTB_table[index].set_[e].valid == true) && (BTB_table[index].set_[e].tag == tag)){
                    curr_entry = &(BTB_table[index].set_[e]);
                    entry_index = e;
                    is_miss = false;
                    break;
                }
            }

            if (is_miss){ // Miss
                miss[0] += 1;

                if (debug)
                    *out << endl << "Miss at pc: " << pc << "  next_pc:" << next_pc << "  target:" << target << endl; 

                if (target != next_pc) { // insert a new entry if target is not next_pc
                    mispred[0] += 1;
                    if (BTB_table[index].set_.size() == set_size){ 
                        BTB_table[index].set_.pop_back();  //  Erase the oldest entry at the end
                    }
                    else if (BTB_table[index].set_.size() > set_size) {
                        *out << "Error in BTB set, size out of range" << endl;
                    }

                    BTB_entry new_entry(tag, target, true);
                    BTB_table[index].set_.push_front(new_entry);
                }
                
            }

            else { // Hit 
                if (debug)
                    *out << endl << "Hit at pc: " << pc << "  next_pc:" << next_pc << "  target:" << target << endl; 

                if (curr_entry->target != target)
                    mispred[0] += 1;

                deque<BTB_entry>::iterator it;
                it = BTB_table[index].set_.begin() + entry_index;

                // Update LRU array
                if ((target != next_pc) && (curr_entry->target != target)){ // Taken Entry Not correct
                    // Create new entry
                    BTB_entry new_entry(tag, target, true);
                    // Erase the older version
                    BTB_table[index].set_.erase(it);
                    // Insert new entry at front
                    BTB_table[index].set_.push_front(new_entry);
                }

                else if (target == next_pc) // Erase the entry
                    BTB_table[index].set_.erase(it);
            }
        }
};


class BTB2
{
    public:
        vector <BTB_set> BTB_table;
        GlobalHistoryRegister *GHR;
        UINT32 set_size;
        int num_sets;

        BTB2(int sets, UINT32 ways, GlobalHistoryRegister *ghr)
        {
            set_size = ways;
            num_sets = sets;
            GHR = ghr;
            BTB_table.clear();
            for (int i = 0; i < sets; i++){
                BTB_set *new_set = new BTB_set();
                BTB_table.push_back(*new_set); // empty set object
            }
        }

        void BTB2_Update(ADDRINT pc, ADDRINT target, ADDRINT next_pc) {

            int width = GHR->width;
            ADDRINT upd_pc = pc % (1 << width); // least significant width bits

            int index = upd_pc^(GHR->value) % (1 << width); // index into correct set
            ADDRINT tag = pc; 

            BTB_entry *curr_entry = NULL;
            int entry_index = -1;
            bool is_miss = true;

            // Iterate through entries in that set
            for (UINT32 e = 0; e < BTB_table[index].set_.size(); e++){
                if ((BTB_table[index].set_[e].valid) && (BTB_table[index].set_[e].tag == tag)){
                    curr_entry = &(BTB_table[index].set_[e]);
                    entry_index = e;
                    is_miss = false;
                    break;
                }
            }

            if (is_miss){ // Miss
                miss[1] += 1;

                if (target != next_pc) { // insert a new entry
                    mispred[1] += 1;
                    if (BTB_table[index].set_.size() == set_size){
                        //  Erase the oldest entry at the end
                        BTB_table[index].set_.pop_back();
                    }
                    else if (BTB_table[index].set_.size() > set_size) {
                        *out << "Error in BTB set, size out of range" << endl;
                    }

                    BTB_entry new_entry(tag, target, true);
                    BTB_table[index].set_.push_front(new_entry);
                }
            }

            else { // Hit 

                if (curr_entry->target != target)
                    mispred[1] += 1;

                deque<BTB_entry>::iterator it;
                it = BTB_table[index].set_.begin() + entry_index;

                // Update LRU array
                if ((target != next_pc) && (curr_entry->target != target)){ // Taken Entry Not correct
                    // Update the entry and the lru order
                    // Create new entry
                    BTB_entry new_entry(tag, target, true);
                    // Erase the older version
                    BTB_table[index].set_.erase(it);
                    // Insert new entry at front
                    BTB_table[index].set_.push_front(new_entry);
                }

                else if (target == next_pc)  // Erase the entry
                    BTB_table[index].set_.erase(it);
            }
        }
};



/* ================================================================== */
// Direction Predictors Definition
/* ================================================================== */

GlobalHistoryRegister GHR_A(9);
GlobalHistoryRegister GHR_B(7);

// Static
FNBT FNBTPred;

// Adaptive
BimodalPredictor bimodalPred(512, 2);
SAg SAgPred(9, 1024, 2);
GAg GAgPred(3, &GHR_A);
gshare gsharePred(3, &GHR_A);

// Hybrid (Adaptive)
SAgGAgHybrid SAgGAgHybridPred(&SAgPred, &GAgPred, &GHR_A, 2);
SAgGAgGshareHybrid_Majority SAgGAgGshareHybrid_MajorityPred(&SAgPred, &GAgPred, &gsharePred);
SAgGAgGshareHybrid_Tournament SAgGAgGshareHybrid_TournamentPred(&SAgPred, &GAgPred, &gsharePred, &GHR_A, 2);

// BTB's
BTB1 BTB_1(128, 4);
BTB2 BTB_2(128, 4, &GHR_B);


/* ===================================================================== */
// Analysis routines
/* ===================================================================== */

VOID InsCount(void) {
    icount++; // total number of instructions (pred + non-pred)
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

    *out
    << endl
    << left
    << setw(10)
    << "S.No"
    << left
    << setw(45)
    << "Prediction Technique"
    << left
    << setw(35)
    << "Misprediction Fraction(FCB)"
    << left
    << setw(35)
    << "Misprediction Fraction(BCB)"    
    << left
    << setw(35)
    << "Misprediction Fraction(Overall)"
    << endl;

    for (int i = 0; i < 8; i++)
    {
        double FCB_fraction = (double)forward_wrongPred[i] / forward_branches;
        double BCB_fraction = (double)backward_wrongPred[i] / backward_branches;
        double Overall_fraction = (double)(forward_wrongPred[i] + backward_wrongPred[i]) / total_branches; 

        *out
        << std::fixed << std::setprecision(3)
        << left
        << setw(10)
        << i+1
        << left
        << setw(45)
        << DPNames[i]
        << left
        << setw(35)
        << FCB_fraction
        << left
        << setw(35)
        << BCB_fraction
        << left
        << setw(35)
        << Overall_fraction
        << endl;
    }

    *out << endl << "===================================== Part B ===========================================";

    *out
    << endl
    << left
    << setw(10)
    << "S.No"
    << left
    << setw(45)
    << "Predictor"
    << left
    << setw(35)
    << "Misprediction Fraction"
    << left
    << setw(35)
    << "BTB Miss rate"    
    << endl;

    for (int i = 0; i < 2; i++)
    {
        double mispred_fraction = (double)mispred[i] / total_indirectCF;
        double miss_rate = (double)miss[i] / total_indirectCF;

        *out
        << std::fixed << std::setprecision(10)
        << left
        << setw(10)
        << i+1
        << left
        << setw(45)
        << BTB_Names[i]
        << left
        << setw(35)
        << mispred_fraction
        << left
        << setw(35)
        << miss_rate
        << endl;

    }

    exit(0);
}


void ConditionalBranchAnalysis(ADDRINT pc, bool taken, ADDRINT target_addr) {
    // Called for every conditional Branch instruction
    bool is_forward_branch = false;
    bool pred[8]; // store predictions of each DP for the current branch instruction

    if (target_addr >= pc){ // forward
        is_forward_branch = true;
        forward_branches += 1;
    }
    else{
        backward_branches += 1;
    }
        
    total_branches += 1;

    // FNBT Predictor
    pred[0] = FNBTPred.GetPrediction(is_forward_branch);

    // Bimodal Predictor
    pred[1] = bimodalPred.GetPrediction(pc);

    // SAg Predictor
    pred[2] = SAgPred.GetPrediction(pc);

    // GAg Predictor
    pred[3] = GAgPred.GetPrediction(pc);
    
    // gshare predictor
    pred[4] = gsharePred.GetPrediction(pc);

    // SAg and GAg
    pred[5] = SAgGAgHybridPred.PredictandSelectorUpdate(pc, taken);

    // SAg, GAg, and gshare - Majority 
    pred[6] = SAgGAgGshareHybrid_MajorityPred.Predict(pc);

    // SAg, GAg, and gshare - Torunament
    pred[7] = SAgGAgGshareHybrid_TournamentPred.PredictandSelectorUpdate(pc, taken);

    for (int i = 0; i < 8; i++){
        // Update Wrong Prediction Counters
        if (is_forward_branch){
            if (pred[i] != taken)
                forward_wrongPred[i] += 1;
        }
        else {
            if (pred[i] != taken)
                backward_wrongPred[i] += 1;
        }

    }

    // Update Predictor Components (Selectors already updated)
    bimodalPred.UpdatePredictor(pc, taken);
    SAgPred.UpdatePredictor(pc, taken);
    GAgPred.UpdatePredictor(pc, taken);
    gsharePred.UpdatePredictor(pc, taken);


    // Update GHR
    GHR_A.UpdateHistory(taken);
    GHR_B.UpdateHistory(taken); // Update on encountering a conditional branch

} 


void IndirectControlFlowAnalysis(ADDRINT pc, bool taken, ADDRINT target_addr, ADDRINT next_pc) {
    total_indirectCF += 1;

    // Update BTB's and counters
    BTB_1.BTB1_Update(pc, target_addr, next_pc);
    BTB_2.BTB2_Update(pc, target_addr, next_pc);
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


    if (INS_IsIndirectControlFlow(ins)){
        // Indirect Control Flow Instruction
        INS_InsertIfCall(ins, IPOINT_BEFORE, (AFUNPTR)FastForward, IARG_END);
		INS_InsertThenPredicatedCall(ins,IPOINT_BEFORE,(AFUNPTR)IndirectControlFlowAnalysis,
			IARG_INST_PTR,
            IARG_BRANCH_TAKEN,
			IARG_BRANCH_TARGET_ADDR,
			IARG_ADDRINT,INS_NextAddress(ins),
			IARG_END);
	}
    
    if (INS_Category(ins) == XED_CATEGORY_COND_BR) {
        // Conditional branches
        INS_InsertIfCall(ins, IPOINT_BEFORE, (AFUNPTR)FastForward, IARG_END);
		INS_InsertThenPredicatedCall(ins, IPOINT_BEFORE, (AFUNPTR)ConditionalBranchAnalysis,
			IARG_INST_PTR, 
			IARG_BRANCH_TAKEN,
			IARG_BRANCH_TARGET_ADDR,
			IARG_END); 
    }

}


VOID Fini(INT32 code, VOID* v)
{
    *out << "===============================================" << endl;
    *out << "HW2 analysis results: " << endl;
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
    cerr << "This application is instrumented by HW2 Tool" << endl;
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
