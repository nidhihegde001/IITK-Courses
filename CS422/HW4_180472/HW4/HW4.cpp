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


// Global Variables
UINT64 L1_access, L2_access;
UINT64 L1_miss, L2_miss;
UINT64 d_on_fill, L2_blocks_filled;
UINT64 L2_hits_al_2, L2_hits_al_1;
UINT64 L1_miss_SRRIP, L2_miss_SRRIP;
UINT64 L1_miss_NRU, L2_miss_NRU;


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

/* ===================================================================== */
// cache Class Definitions 
/* ===================================================================== */

class CacheLine
{
    public:
        bool valid;
        // bool S;
        UINT64 tag;

        int hits; // relevant for L2 cache

        int LRUstate;
        int age; // SRRIP 
        int ref; //NRU
        
        CacheLine()
        {
            valid = false;
            // S = false;
            tag = 0;
            hits = 0; // LRU
            age = 3; // SRRIP
            ref = 1; // NRU
        }

        void Update_LRU(UINT64 TAG, int HITS)
        {
            tag = TAG;
            hits = HITS;
            valid = true;
        }

        void Update_SRRIP(UINT64 TAG, int AGE){
            tag = TAG;
            valid = true;
            age = AGE;
        }

        void Update_NRU(UINT64 TAG, int REF){
            tag = TAG;
            valid = true;
            ref = REF;
        }
};

class CacheSet // Collection of cache lines
{
   public:
        deque <CacheLine> set_; // in order of most recently accessed (LRU state) - Oldest entry at the back odf queue
        UINT32 way_MRA; // way index of most recently accessed in the set
        CacheSet(UINT32 ways)
        {
            set_.clear();

            // for (UINT32 i = 0; i < ways; i++){
            //     // CacheLine *newline = new CacheLine(); // invalid line
            //     // Cacheline newline;
            //     // set_.push_back(newline);
            // }
            way_MRA = 50;
        }
};


class Cache1 // LRU Replacement
{
    public:

        vector <CacheSet> L1; 
        vector <CacheSet> L2; 

        UINT32 L1_ways, L2_ways;
        int L1_sets, L2_sets; 

        Cache1(int l1_sets, UINT32 l1_ways, int l2_sets, UINT32 l2_ways)
        {
            L1_sets = l1_sets;
            L1_ways = l1_ways;
            L2_sets = l2_sets;
            L2_ways = l2_ways;

            L1.clear();
            L2.clear();

            // Initialize L1 and L2 cache entries 
            for (int i = 0; i < L1_sets; i++){
                // CacheSet *newL1set = new CacheSet(L1_ways);
                CacheSet newL1set(L1_ways);
                L1.push_back(newL1set);
            }

            for (int i = 0; i < L2_sets; i++){
                // CacheSet *newL2set = new CacheSet(L2_ways);
                CacheSet newL2set(L2_ways);
                L2.push_back(newL2set);
            }
        }

        void Report_Zero_Hits(void){
            // Loop through L2 cache to count dof's (the ones that remain and not get evicted till the end)

            for (int si = 0; si < 16; si++){
                for (UINT32 i = 0; i < L2[si].set_.size(); i++){
                    if ((L2[si].set_[i].valid == true) && (L2[si].set_[i].hits == 0)){
                        d_on_fill += 1;
                    }
                }
            }
        }

        void L2_evict(UINT32 L2_index){
            // Remove corresponding entry from L1 as well
            if (L2[L2_index].set_.size() == 0)
                *out << "wrong eviction" << endl;

            CacheLine old_entry = L2[L2_index].set_.back();
            
            if (old_entry.hits == 0){
                d_on_fill += 1;
            }
            
            UINT64 L1_tag = (old_entry.tag)*8 + ((L2_index >> 7) & 7); 
            L2[L2_index].set_.pop_back(); // remove entry from L2

            UINT32 L1_index = L2_index & 127;

            bool found = false; // entry in L1
            int L1_entry_way = -1;

            // Tag Matching in L1
            for (UINT32 e = 0; e < L1[L1_index].set_.size(); e++){
                // Hit
                if ((L1[L1_index].set_[e].valid == true) && (L1[L1_index].set_[e].tag == L1_tag)){
                    found = true;
                    L1_entry_way = e;
                    break;
                }
            }


            if (found){
                // remove entry from L1
                deque<CacheLine>::iterator it;
                it = L1[L1_index].set_.begin() + L1_entry_way;
                L1[L1_index].set_.erase(it);

            }
        }

        void Lookup(UINT64 st_addr, UINT64 e_addr) {
            // blocks
            UINT64 st_block = st_addr/64;
            UINT64 e_block = e_addr/64; 

            UINT64 curr_addr;
            int L1_entry_way = -1, L2_entry_way = -1;

            for (UINT64 block = st_block; block <= e_block; block++){

                L1_access += 1;  // Accessing L1 cache
                bool is_L1_miss = true;

                curr_addr = block*64; // starting address of block

                UINT32 L1_index = (curr_addr / 64 ) & 127 ;
                UINT64 L1_tag = curr_addr /  (1 << 13);

                // Tag Matching in L1
                for (UINT32 e = 0; e < L1[L1_index].set_.size(); e++){
                    // Hit
                    if ((L1[L1_index].set_[e].valid == true) && (L1[L1_index].set_[e].tag == L1_tag)){
                        is_L1_miss = false;
                        L1_entry_way = e;
                        L1[L1_index].set_[e].hits += 1; // L1 hits
                        break;
                    }
                }

                if (is_L1_miss){
                    L1_miss += 1;
                    L2_access += 1;  // Accessing L2 cache
                    bool is_L2_miss = true;

                    UINT32 L2_index = (curr_addr / 64 ) & 1023;
                    UINT64 L2_tag = curr_addr / (1 << 16); 

                    // Tag Matching in L2
                    for (UINT32 e = 0; e < L2[L2_index].set_.size(); e++){
                        // Hit
                        if ((L2[L2_index].set_[e].valid == true) && (L2[L2_index].set_[e].tag == L2_tag)){
                            is_L2_miss = false;
                            L2_entry_way = e;
                            
                            if (L2[L2_index].set_[e].hits == 0)
                                L2_hits_al_1 += 1;
                            if (L2[L2_index].set_[e].hits == 1)
                                L2_hits_al_2 += 1;

                            L2[L2_index].set_[e].hits += 1; // L2 hits
                            break;
                        }
                    }
                
                    if (is_L2_miss){ // L1 miss, L2 Miss, fill entries in L1 and L2
                        L2_miss += 1;

                        if (L2[L2_index].set_.size() == L2_ways)
                            L2_evict(L2_index);  //  Evict from L2

                        if ((L2[L2_index].set_.size() > L2_ways))
                            *out << "Size exceeded";
                        
                        // Insert in L2
                        L2_blocks_filled += 1;
                        CacheLine newline2;
                        newline2.Update_LRU(L2_tag, 0);
                        L2[L2_index].set_.push_front(newline2);
                        

                        if (L1[L1_index].set_.size() == L1_ways)
                            L1[L1_index].set_.pop_back();  //  Evict from L1, assume no modified bit
                        
                        // Insert in L1
                        CacheLine newline1;
                        newline1.Update_LRU(L1_tag, 0);
                        L1[L1_index].set_.push_front(newline1);
                    }

                    else { // L1 miss, L2 Hit, copy entry from to L1

                        if (L1[L1_index].set_.size() == L1_ways)
                            L1[L1_index].set_.pop_back();  //  Erase the oldest entry in L1 if full
                        
                        // Insert in L1
                        CacheLine *newline1 = new CacheLine();
                        newline1->Update_LRU(L1_tag, 0);
                        L1[L1_index].set_.push_front(*newline1);
                        delete newline1;


                        // refresh L2 LRU state 
                        deque<CacheLine>::iterator it;
                        if (L2_entry_way == -1){
                            *out << "Wrong Index" << endl;
                        }
                        it = L2[L2_index].set_.begin() + L2_entry_way;

                        CacheLine *newline2 = new CacheLine();
                        int old_hits = L2[L2_index].set_[L2_entry_way].hits;
                        newline2->Update_LRU(L2_tag, old_hits);
                        L2[L2_index].set_.erase(it); 
                        L2[L2_index].set_.push_front(*newline2);
                        delete newline2;
                    }

                }

                else {
                    // refresh L1 LRU State
                        deque<CacheLine>::iterator it;
                        if (L1_entry_way == -1){
                            *out << "Wrong Index" << endl;
                        }
                        it = L1[L1_index].set_.begin() + L1_entry_way;
                        int old_hits = L1[L1_index].set_[L1_entry_way].hits;
                        CacheLine *newline1 = new CacheLine();
                        newline1->Update_LRU(L1_tag, old_hits);
                        L1[L1_index].set_.erase(it);
                        L1[L1_index].set_.push_front(*newline1);
                        delete newline1;
                }
            }
        }
};


class Cache2 //  SRRIP Replacement for L2
{
    public:
        vector <CacheSet> L1; 
        vector <CacheSet> L2; 


        UINT32 L1_ways, L2_ways;
        int L1_sets, L2_sets; 

        Cache2(int l1_sets, UINT32 l1_ways, int l2_sets, UINT32 l2_ways)
        {
            L1_sets = l1_sets;
            L1_ways = l1_ways;
            L2_sets = l2_sets;
            L2_ways = l2_ways;

            L1.clear();
            L2.clear();

            // Initialize L1 and L2 cache entries 
            for (int i = 0; i < L1_sets; i++){
                CacheSet *newL1set = new CacheSet(L1_ways);
                L1.push_back(*newL1set);
            }

            for (int i = 0; i < L2_sets; i++){
                CacheSet *newL2set = new CacheSet(L2_ways);
                L2.push_back(*newL2set);
            }
        }

        void L2_evict_and_insert(UINT32 L2_index, UINT64 L2_tag){

            int L2_entry_way = -1;
            bool evict = false;

            while(!evict){
                for (UINT32 oe = 0; oe < L2[L2_index].set_.size(); oe++){
                // check age 
                    if ((L2[L2_index].set_[oe].valid == true) && (L2[L2_index].set_[oe].age == 3)){
                        L2_entry_way = oe;
                        evict = true; // found an evictable entry
                        break;
                    }

                    if ((L2[L2_index].set_[oe].valid == false) || (L2[L2_index].set_[oe].age > 3)){
                        *out << "Error, Invalid entry in SRRIP L2" << endl;
                    }
                }

                if (!evict){
                    for (UINT32 oe = 0; oe < L2[L2_index].set_.size(); oe++){
                        if(L2[L2_index].set_[oe].valid == true)
                            L2[L2_index].set_[oe].age += 1; // increment the age of all by 1
                    }
                }
            }
            CacheLine old_entry = L2[L2_index].set_[L2_entry_way]; // found entry to evict in L2

            UINT64 L1_tag = (old_entry.tag * 8) + ((L2_index >> 7) & 7);
            UINT32 L1_index = L2_index & 127;

            // Update L2 old entry to new entry
            L2[L2_index].set_[L2_entry_way].Update_SRRIP(L2_tag, 2);

            bool found = false;
            int L1_entry_way = -1;

            // Tag Matching in L1
            for (UINT32 e = 0; e < L1[L1_index].set_.size(); e++){
                // Hit
                if ((L1[L1_index].set_[e].valid == true) && (L1[L1_index].set_[e].tag == L1_tag)){
                    found = true;
                    L1_entry_way = e;
                    break;
                }
            }

            if (found){
                // remove from L1
                deque<CacheLine>::iterator it;
                it = L1[L1_index].set_.begin() + L1_entry_way;
                L1[L1_index].set_.erase(it);
            }
        }


        void Lookup(UINT64 st_addr, UINT64 e_addr) {

            UINT64 st_block = st_addr/64; 
            UINT64 e_block = e_addr/64;
            UINT64 curr_addr;
            int L1_entry_way = -1;

            for (UINT64 block = st_block; block <= e_block; block++){

                bool is_L1_miss = true;

                curr_addr = block*64; // starting address of block

                UINT32 L1_index = (curr_addr >> 6 ) & 127 ;
                UINT64 L1_tag = curr_addr >> 13;

                // Tag Matching in L1
                for (UINT32 e = 0; e < L1[L1_index].set_.size(); e++){
                    // Hit
                    if ((L1[L1_index].set_[e].valid == true) && (L1[L1_index].set_[e].tag == L1_tag)){
                        is_L1_miss = false;
                        L1_entry_way = e;
                        L1[L1_index].set_[e].hits += 1;
                        break;
                    }
                }

                if (is_L1_miss){
                    L1_miss_SRRIP += 1;
                    bool is_L2_miss = true;

                    UINT32 L2_index = (curr_addr / 64 ) & 1023;
                    UINT64 L2_tag = curr_addr / (1 << 16); 

                    // Tag Matching in L2
                    for (UINT32 e = 0; e < L2[L2_index].set_.size(); e++){
                        // Hit
                        if ((L2[L2_index].set_[e].valid == true) && (L2[L2_index].set_[e].tag == L2_tag)){
                            is_L2_miss = false;
                            L2[L2_index].set_[e].age = 0; // assign an age of 0 upon hit
                            break;
                        }
                    }
                
                    if (is_L2_miss){ // L1 miss, L2 Miss, fill entries in L1 and L2
                        L2_miss_SRRIP += 1;

                        if (L2[L2_index].set_.size() == L2_ways){
                            L2_evict_and_insert(L2_index, L2_tag);  //  Evict from L2
                        }
                            
                        else { // intial "way" number of misses at each index
                        // Insert in L2
                        CacheLine *newline2 = new CacheLine();
                        newline2->Update_SRRIP(L2_tag, 2);
                        L2[L2_index].set_.push_back(*newline2); // push at back (SRRIP)
                        delete newline2;

                        }

                        if (L1[L1_index].set_.size() == L1_ways)
                            L1[L1_index].set_.pop_back();  //  Evict from L1, assume no modified bit
                        
                        // Insert in L1
                        CacheLine *newline1 = new CacheLine();
                        newline1->Update_LRU(L1_tag, 0);
                        L1[L1_index].set_.push_front(*newline1);
                        delete newline1;
                        
                    }

                    else { // L1 miss, L2 Hit, copy entry from to L1

                        if (L1[L1_index].set_.size() == L1_ways)
                            L1[L1_index].set_.pop_back();  //  Erase the oldest entry in L1 if full
                        
                        // Insert in L1
                        CacheLine *newline1 = new CacheLine();
                        newline1->Update_LRU(L1_tag, 0);
                        L1[L1_index].set_.push_front(*newline1);
                        delete newline1;
                    }

                }

                else {
                    // refresh L1 LRU State
                        deque<CacheLine>::iterator it;
                        it = L1[L1_index].set_.begin() + L1_entry_way;
                        CacheLine *newline1 = new CacheLine();
                        int old_hits = L1[L1_index].set_[L1_entry_way].hits;
                        newline1->Update_LRU(L1_tag, old_hits);
                        L1[L1_index].set_.erase(it);
                        L1[L1_index].set_.push_front(*newline1);
                        delete newline1;
                }
            }
        }
};




class Cache3 //  NRU Replacement for L2
{
    public:
        vector <CacheSet> L1; 
        vector <CacheSet> L2; 
        UINT32 L1_ways, L2_ways;
        int L1_sets, L2_sets; 

        Cache3(int l1_sets, UINT32 l1_ways, int l2_sets, UINT32 l2_ways)
        {
            L1_sets = l1_sets;
            L1_ways = l1_ways;
            L2_sets = l2_sets;
            L2_ways = l2_ways;

            L1.clear();
            L2.clear();

            // Initialize L1 and L2 cache entries 
            for (int i = 0; i < L1_sets; i++){
                CacheSet *newL1set = new CacheSet(L1_ways);
                L1.push_back(*newL1set);
            }

            for (int i = 0; i < L2_sets; i++){
                CacheSet *newL2set = new CacheSet(L2_ways);
                L2.push_back(*newL2set);
            }
        }

        void refresh_REF_bits(UINT32 L2_index){ // resets ref bits if all refs are 1, called upon hit and fill

            bool all_set = true;
            for (UINT32 i = 0; i < L2[L2_index].set_.size(); i++){
                if ((L2[L2_index].set_[i].valid == true) && (L2[L2_index].set_[i].ref == 0))
                    all_set = false;
            }
            if (all_set){ // reset all except MRA way
                for (UINT32 i = 0; i < L2[L2_index].set_.size(); i++){
                    if (L2[L2_index].way_MRA == 50)
                        *out << "Erorr in NRU - MRA_way" << endl;

                    if ((L2[L2_index].set_[i].valid == true) && (i != L2[L2_index].way_MRA))
                        L2[L2_index].set_[i].ref = 0;
                }
            }

        }

        void L2_evict_and_insert(UINT32 L2_index, UINT64 L2_tag){

            int L2_entry_way = -1;
            bool evict = false; 
            
            for (UINT32 oe = 0; oe < L2[L2_index].set_.size(); oe++){
            // check age 
                if ((L2[L2_index].set_[oe].valid == true) && (L2[L2_index].set_[oe].ref == 0)){
                    L2_entry_way = oe;
                    evict = true; // found an evictable entry
                    break;
                }

                if ((L2[L2_index].set_[oe].valid == false) || (L2[L2_index].set_[oe].ref > 1)){
                    *out << "Error, Invalid entry in NRU L2" << endl;
                }
            }

            if (!evict){
                *out << "ERROR: No entry found to evict in NRU L2" << endl;
                return;

            }
                
            CacheLine old_entry = L2[L2_index].set_[L2_entry_way]; // found entry to evict in L2

            UINT64 L1_tag = (old_entry.tag * 8) + ((L2_index >> 7) & 7); 
            UINT32 L1_index = L2_index & 127;

            // Update L2 old entry to new entry
            L2[L2_index].set_[L2_entry_way].Update_NRU(L2_tag, 1); // filled block REF 1
            L2[L2_index].way_MRA = L2_entry_way;
            refresh_REF_bits(L2_index);

            bool found = false;
            int L1_entry_way = -1;

            // Tag Matching in L1
            for (UINT32 e = 0; e < L1[L1_index].set_.size(); e++){
                // Hit
                if ((L1[L1_index].set_[e].valid == true) && (L1[L1_index].set_[e].tag == L1_tag)){
                    found = true;
                    L1_entry_way = e;
                    break;
                }
            }

            if (found){
                // remove from L1
                deque<CacheLine>::iterator it;
                it = L1[L1_index].set_.begin() + L1_entry_way;
                L1[L1_index].set_.erase(it);
            }
        }


        void Lookup(UINT64 st_addr, UINT64 e_addr) {
            // blocks
            UINT64 st_block = st_addr/64; 
            UINT64 e_block = e_addr/64;
            UINT64 curr_addr;
            int L1_entry_way = -1;

            for (UINT64 block = st_block; block <= e_block; block++){

                bool is_L1_miss = true;

                curr_addr = block*64; // starting address of block

                UINT32 L1_index = (curr_addr >> 6 ) & 127 ;
                UINT64 L1_tag = curr_addr >> 13;

                // Tag Matching in L1
                for (UINT32 e = 0; e < L1[L1_index].set_.size(); e++){
                    // Hit
                    if ((L1[L1_index].set_[e].valid == true) && (L1[L1_index].set_[e].tag == L1_tag)){
                        is_L1_miss = false;
                        L1_entry_way = e;
                        L1[L1_index].set_[e].hits += 1;
                        break;
                    }
                }

                if (is_L1_miss){
                    L1_miss_NRU += 1;
                    bool is_L2_miss = true;

                    UINT32 L2_index = (curr_addr / 64 ) & 1023;
                    UINT64 L2_tag = curr_addr / (1 << 16); 

                    // Tag Matching in L2
                    for (UINT32 e = 0; e < L2[L2_index].set_.size(); e++){
                        // Hit
                        if ((L2[L2_index].set_[e].valid == true) && (L2[L2_index].set_[e].tag == L2_tag)){
                            is_L2_miss = false;
                            L2[L2_index].set_[e].ref = 1; // assign an ref of 1 upon hit
                            L2[L2_index].way_MRA = e; // MR Accessed way index
                            refresh_REF_bits(L2_index); // Filled entry
                            break;
                        }
                    }
                
                    if (is_L2_miss){ // L1 miss, L2 Miss, fill entries in L1 and L2
                        L2_miss_NRU += 1;

                        if (L2[L2_index].set_.size() == L2_ways){
                            L2_evict_and_insert(L2_index, L2_tag);  //  Evict from L2
                        }
                            
                        else { // intial "way" number of misses at each index
                        // Insert in L2
                        CacheLine *newline2 = new CacheLine();
                        newline2->Update_NRU(L2_tag, 1); // ref bit 1
                        L2[L2_index].way_MRA = L2[L2_index].set_.size();
                        L2[L2_index].set_.push_back(*newline2); // push at back (NRU)
                        delete newline2;
                        refresh_REF_bits(L2_index);
                        }

                        if (L1[L1_index].set_.size() == L1_ways)
                            L1[L1_index].set_.pop_back();  //  Evict from L1, assume no modified bit
                        
                        // Insert in L1
                        CacheLine *newline1 = new CacheLine();
                        newline1->Update_LRU(L1_tag, 0);
                        L1[L1_index].set_.push_front(*newline1);
                        delete newline1;
                        
                    }

                    else { // L1 miss, L2 Hit, copy entry from to L1

                        if (L1[L1_index].set_.size() == L1_ways)
                            L1[L1_index].set_.pop_back();  //  Erase the oldest entry in L1 if full
                        
                        // Insert in L1
                        CacheLine *newline1 = new CacheLine();
                        newline1->Update_LRU(L1_tag, 0);
                        L1[L1_index].set_.push_front(*newline1);
                        delete newline1;
                    }

                }

                else {
                    // refresh L1 LRU State
                        deque<CacheLine>::iterator it;
                        it = L1[L1_index].set_.begin() + L1_entry_way;
                        CacheLine *newline1 = new CacheLine();
                        int old_hits = L1[L1_index].set_[L1_entry_way].hits;
                        newline1->Update_LRU(L1_tag, old_hits);
                        L1[L1_index].set_.erase(it);
                        L1[L1_index].set_.push_front(*newline1);
                        delete newline1;
                }
            }
        }
};

/* ================================================================== */
// Caches
/* ================================================================== */

Cache1 LRUCache(128, 8, 1024, 16);
Cache2 SRRIPCache(128, 8, 1024, 16);
Cache3 NRUCache(128, 8, 1024, 16);

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
    *out << endl << "===================================== Part A (LRU Policy) ===========================================";

    double perc_dof ;
    double perc_al_2;

    LRUCache.Report_Zero_Hits();

    perc_dof = (double)d_on_fill*100 / L2_blocks_filled;

    if (L2_hits_al_1 > 0)
        perc_al_2 = (double)L2_hits_al_2*100 / L2_hits_al_1; 
    else 
        perc_al_2 = 0;

    *out
    << endl
    << "No of L1 accesses: "
    << L1_access
    << endl
    << "No of L2 accesses: "
    << L2_access
    << endl
    << "No of L1 Cache Misses: "    
    << L1_miss
    << endl
    << "No of L2 Cache Misses: "    
    << L2_miss
    << endl
    << "Percentage of L2 DoF Blocks: "
    << perc_dof
    << endl
    << "Number of Total DOF Blocks: "
    << d_on_fill
    << endl
    << "Percentage of L2 Blocks(>= 2 hits): "
    << perc_al_2
    << endl
    << "L2 blocks with atleast 2 hits: "
    << L2_hits_al_2
    << endl
    << "L2 blocks with atleast 1 hit: "
    << L2_hits_al_1
    << endl;


    *out << endl << "===================================== Part B (SRRIP Policy) ===========================================";

    *out
    << endl
    << "No of L1 Cache Misses: "    
    << L1_miss_SRRIP
    << endl
    << "No of L2 Cache Misses: "    
    << L2_miss_SRRIP
    << endl;


    *out << endl << "===================================== Part C (NRU Policy) ===========================================";

    *out
    << endl
    << "No of L1 Cache Misses: "    
    << L1_miss_NRU
    << endl
    << "No of L2 Cache Misses: "    
    << L2_miss_NRU
    << endl;


    exit(0);
}


VOID CacheSimulation(VOID * ip, UINT32 size)
{
    // fprintf(trace,"%p: R %p\n", ip, addr);
    UINT64 startaddr = (UINT64)ip; 
    UINT64 endaddr = (UINT64)ip + size;

    LRUCache.Lookup(startaddr, endaddr);
    SRRIPCache.Lookup(startaddr, endaddr);
    NRUCache.Lookup(startaddr, endaddr);
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

    // Instruments memory accesses using a predicated call, i.e.
    // the instrumentation is called iff the instruction will actually be executed.
    //
    // On the IA-32 and Intel(R) 64 architectures conditional moves and REP 
    // prefixed instructions appear as predicated instructions in Pin.
    UINT32 memOperands = INS_MemoryOperandCount(ins);

    // Iterate over each memory operand of the instruction.
    for (UINT32 memOp = 0; memOp < memOperands; memOp++)
    {
        UINT32 memOpSize = INS_MemoryOperandSize(ins, memOp);
        // UINT32 cache_block_size = 64;

        if (INS_MemoryOperandIsRead(ins, memOp))
        {
            INS_InsertIfCall(ins, IPOINT_BEFORE, (AFUNPTR)FastForward, IARG_END);
            INS_InsertThenPredicatedCall(
                ins, IPOINT_BEFORE, (AFUNPTR)CacheSimulation,
                IARG_MEMORYOP_EA, memOp,
                IARG_UINT32, memOpSize,
                IARG_END);
        }
        // Note that in some architectures a single memory operand can be 
        // both read and written (for instance incl (%eax) on IA-32)
        // In that case we instrument it once for read and once for write.
        if (INS_MemoryOperandIsWritten(ins, memOp))
        {
            INS_InsertIfCall(ins, IPOINT_BEFORE, (AFUNPTR)FastForward, IARG_END);
            INS_InsertThenPredicatedCall(
                ins, IPOINT_BEFORE, (AFUNPTR)CacheSimulation,
                IARG_MEMORYOP_EA, memOp,
                IARG_UINT32, memOpSize,
                IARG_END);
        }
    }


}


VOID Fini(INT32 code, VOID* v)
{
    *out << "===============================================" << endl;
    *out << "HW4 analysis results: " << endl;
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
    cerr << "This application is instrumented by HW4 Tool" << endl;
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
