#ifndef __DEBUG_H_
#define __DEBUG_H_

#include <types.h>
#include <context.h>

#define MAX_BACKTRACE	64
#define MAX_BREAKPOINTS 8

#define INT3_OPCODE 	0xCC 
#define PUSHRBP_OPCODE 	0x55 
#define END_ADDR 	0x10000003B

#define CHILD_EXIT	0 

/*
 * DO NOT MODIFY
 * node of breakpoint list
 * for kernel use
 */
struct breakpoint_info{
	u32 num;		       // breakpoint number
	u32 status;		       // breakpoint status
	u64 addr;		       // address on which breakpoint is set
	struct breakpoint_info *next;  // pointer to next node in breakpoint list 
};

/*
 * stores information about debugger
 * NULL for all other processes
 */
struct debug_info{
	
	// head of breakpoint list
	// DO NOT MODIFY
	struct breakpoint_info *head;   	
	////Add your definitions//////
	unsigned char nextInstr[MAX_BREAKPOINTS];
	u64 backtrace_addr[MAX_BACKTRACE];
	int num_bp;
	int num_backtrace;
	unsigned char tmp; // stores next instruction of most recently removed breakpoint 
};

/*
 * contains information about a breakpoint
 * that can be passed to user space
 * DO NOT MODIFY
 */
struct breakpoint{
        int num;        //breakpoint number
        int status;     //enabled or disabled
        u64 addr;       //address on which breakpoint is set
};

/*
 * stores information about registers
 * of the debugged process
 */
struct registers{
	
	u64 r15;
	u64 r14;
	u64 r13;
	u64 r12;
	u64 r11;
	u64 r10;
	u64 r9;
	u64 r8;
	u64 rbp;
	u64 rdi;
	u64 rsi;
	u64 rdx;
	u64 rcx;
	u64 rbx;
	u64 rax;
	u64 entry_rip;  
	u64 entry_cs;  
	u64 entry_rflags;
	u64 entry_rsp;
	u64 entry_ss;
};

extern long int3_handler(struct exec_context *ctx);
extern void debugger_on_fork(struct exec_context *child_ctx);
extern void debugger_on_exit(struct exec_context *ctx);
extern int do_become_debugger(struct exec_context *ctx);
extern int do_set_breakpoint(struct exec_context *ctx, void *addr);
extern int do_remove_breakpoint(struct exec_context *ctx, void *addr);
extern int do_enable_breakpoint(struct exec_context *ctx, void *addr);
extern int do_disable_breakpoint(struct exec_context *ctx, void *addr);
extern int do_info_breakpoints(struct exec_context *ctx, struct breakpoint *bp);
extern int do_info_registers(struct exec_context *ctx, struct registers *reg);
extern int do_backtrace(struct exec_context *ctx, u64 bt);
extern s64 do_wait_and_continue(struct exec_context *ctx);

// for testing 
extern void print_breakpoints(struct breakpoint_info *head);
#endif
