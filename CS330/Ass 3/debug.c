#include <debug.h>
#include <context.h>
#include <entry.h>
#include <lib.h>
#include <memory.h>


/*****************************HELPERS******************************************/

/* 
 * allocate the struct which contains information about debugger
 *
 */
struct debug_info *alloc_debug_info()
{
	struct debug_info *info = (struct debug_info *) os_alloc(sizeof(struct debug_info)); 
	if(info)
		bzero((char *)info, sizeof(struct debug_info));
	return info;
}

/*
 * frees a debug_info struct 
 */
void free_debug_info(struct debug_info *ptr)
{
	if(ptr)
		os_free((void *)ptr, sizeof(struct debug_info));
}

/*
 * allocates memory to store registers structure
 */
struct registers *alloc_regs()
{
	struct registers *info = (struct registers*) os_alloc(sizeof(struct registers)); 
	if(info)
		bzero((char *)info, sizeof(struct registers));
	return info;
}

/*
 * frees an allocated registers struct
 */
void free_regs(struct registers *ptr)
{
	if(ptr)
		os_free((void *)ptr, sizeof(struct registers));
}

/* 
 * allocate a node for breakpoint list 
 * which contains information about breakpoint
 */
struct breakpoint_info *alloc_breakpoint_info()
{
	struct breakpoint_info *info = (struct breakpoint_info *)os_alloc(
		sizeof(struct breakpoint_info));
	if(info)
		bzero((char *)info, sizeof(struct breakpoint_info));
	return info;
}

/*
 * frees a node of breakpoint list
 */
void free_breakpoint_info(struct breakpoint_info *ptr)
{
	if(ptr)
		os_free((void *)ptr, sizeof(struct breakpoint_info));
}

/*
 * Fork handler.
 * The child context doesnt need the debug info
 * Set it to NULL
 * The child must go to sleep( ie move to WAIT state)
 * It will be made ready when the debugger calls wait_and_continue
 */
void debugger_on_fork(struct exec_context *child_ctx)
{
	child_ctx->dbg = NULL;	
	child_ctx->state = WAITING;
}


/******************************************************************************/

/* This is the int 0x3 handler
 * Hit from the childs context
 */
long int3_handler(struct exec_context *ctx)
{	 //printk("Entered int3 on %d\n", ctx->regs.entry_rip - 1);
	void *addr = (void *) (ctx->regs.entry_rip - 1); // points to interrupt a, b
	int num_bp = 0;

	struct exec_context * p_ctx = get_ctx_by_pid(ctx->ppid); // debugger
	if (p_ctx->dbg == NULL){
		return -1;
	}

	struct breakpoint_info * ptr = p_ctx->dbg->head;

	int found = 0;
	while(ptr != NULL){
		if (ptr->addr == (u64)addr){
			found = 1;
			break;
		}
		ptr = ptr->next;
	}

	if (found){ // a

		// reinitialize from last call
		p_ctx->dbg->num_backtrace = 0;
		for (int b = 0; b<MAX_BACKTRACE; b++){
			p_ctx->dbg->backtrace_addr[b] = 0x00000000;
		}
	    // store values from this call
		u64* ret_addr = (u64*)ctx->regs.rbp;

		p_ctx->dbg->backtrace_addr[0] = (u64)addr;
		p_ctx->dbg->backtrace_addr[1] = *((u64*)ctx->regs.entry_rsp);
		int bt_num = 2;
		// printk("Here %d\n", bt_num);
		while(1){
			if (*(ret_addr+1) != END_ADDR){
				// printk("Here %d\n", bt_num);
				p_ctx->dbg->backtrace_addr[bt_num++] = *(ret_addr+1);
				ret_addr = (u64*)(*ret_addr); // doubt
			}
				
			else
				break;
		}
		// printk("Here RIP %x\n",ctx->regs.entry_rip);

		p_ctx->dbg->num_backtrace = bt_num;

		// ret value of wait_continue in rax
		p_ctx->regs.rax = (u64)addr;
		
		*(unsigned char *)addr = 0x55; //push rbp instruction i->a
		*((unsigned char *)addr+1) = 0xCC; //b->i
		ctx->regs.entry_rip = (u64)addr; // roll-back by one
		ctx->state = WAITING;
		p_ctx->state = READY;	
		// printk("Here RIP: %x\n",ctx->regs.entry_rip);
		// how to return 0
		ctx->regs.rax = 0;
		// printk("curr_inst %d\n", *(unsigned char *)addr);
		schedule(p_ctx);
		return -1;
	}

	else{ // b
		int num_bp = 0;
		struct breakpoint_info * p_ptr = p_ctx->dbg->head;
		while(p_ptr != NULL){
			num_bp++;
			if (p_ptr->addr == (u64)((unsigned char *)addr-1)){ // find a's number
				break;
			}
			p_ptr = p_ptr->next;
		}

		if (p_ptr == NULL){ // check if a is removed
			*(unsigned char *)addr =  p_ctx->dbg->tmp; // i->b
		}
		else if (p_ptr->status == 0){  // check if a is disabled
			*(unsigned char *)addr =  p_ctx->dbg->nextInstr[num_bp-1]; // i->b

		}
		else{
			*(unsigned char *)addr =  p_ctx->dbg->nextInstr[num_bp-1]; // i->b
			*((unsigned char *)addr-1) = 0xCC; // a->i
		}

		ctx->regs.entry_rip = (u64)addr; // roll-back by one
		p_ctx->state = WAITING;
		ctx->state = READY;	
		schedule(ctx);
		return 0;
	}
	return -1;
}

/*
 * Exit handler.
 * Called on exit of Debugger and Debuggee 
 */
void debugger_on_exit(struct exec_context *ctx)
{
	if (ctx->dbg == NULL){ //debugee
		struct exec_context * p_ctx = get_ctx_by_pid(ctx->ppid);
		p_ctx->regs.rax = CHILD_EXIT;
		ctx->state = EXITING;
		p_ctx->state = READY;
		// schedule(p_ctx);
	}

	else{ //debugger
		struct breakpoint_info * ptr = ctx->dbg->head;
		while(ptr != NULL){
			// resetting instr at ptr->addr
			unsigned char instr = *(unsigned char *)(ptr->addr); // instruction
			if (instr == 0xCC){ //int3 interrupt
				*(unsigned char *)(ptr->addr) = 0x55; // replace it to push rbp
			}
			free_breakpoint_info(ptr);
			ptr = ptr->next;
		}
		free_debug_info(ctx->dbg);
		// ctx->dbg = NULL;
	}

}

/*
 * called from debuggers context
 * initializes debugger state
 */
int do_become_debugger(struct exec_context *ctx)
{
	// Your code
	if (ctx->dbg == NULL){
		ctx->dbg = alloc_debug_info();
		if (ctx->dbg == NULL)
			return -1;
	}

	ctx->dbg->head = NULL;
	for (int i = 0; i< MAX_BREAKPOINTS; i++){
		ctx->dbg->nextInstr[i] = 0x00; //initialize
	}

	// initialize backtrace
	for (int b = 0; b<MAX_BACKTRACE; b++){
		ctx->dbg->backtrace_addr[b] = 0x00000000;
	}
	ctx->dbg->num_bp = 0;
	ctx->dbg->num_backtrace = 0;
	return 0;
}

/*
 * called from debuggers context
 */
int do_set_breakpoint(struct exec_context *ctx, void *addr)
{
	
	// Your code
	if (ctx->dbg == NULL)
		return -1;

	unsigned char nInstr = *((unsigned char *)addr+1); // next instruction
	// unsigned char nInstr = 0x48;
	*(unsigned char *)addr = 0xCC; // point to INT3 instruction

	struct breakpoint_info * ptr = ctx->dbg->head;
	struct breakpoint_info * prev = ctx->dbg->head;
	while(ptr != NULL){
		if (ptr->addr == (u64)addr){
			ptr->status = 1;
			return 0;
		}
		prev = ptr;
		ptr = ptr->next;
	}

	if (ctx->dbg->num_bp == MAX_BREAKPOINTS)
		return -1; //new breakpoint can't be added

	struct breakpoint_info * new_bi = alloc_breakpoint_info();

	if (new_bi == NULL){
		return -1; // error in alloc
	}

	new_bi->addr = (u64)addr;
	new_bi->num = ctx->dbg->num_bp + 1;
	new_bi->status = 1;
	new_bi->next = NULL;

	if (ptr == ctx->dbg->head){
		ctx->dbg->head = new_bi;
	}
	else{
		prev->next = new_bi;
	}
	

	ctx->dbg->num_bp += 1;
	ctx->dbg->nextInstr[ctx->dbg->num_bp-1] = nInstr; //store next instruction at addr+1
	return 0;
}

/*
 * called from debuggers context
 */
int do_remove_breakpoint(struct exec_context *ctx, void *addr)
{
	// Your code	
	if (ctx->dbg == NULL){
		return -1;
	}
	struct breakpoint_info * ptr = ctx->dbg->head;
	struct breakpoint_info * prev = ctx->dbg->head;
	int bp = 0;
	while(ptr != NULL){
		bp++;
		if (ptr->addr == (u64)addr){
			break;
		}
		prev = ptr;
		ptr = ptr->next;
	}
	if (ptr == NULL){
		return -1;     // no addr found
	}
	if (ptr == ctx->dbg->head){
		// modify breakpoints list
		ctx->dbg->head = ptr->next;
		//left shift all Instr
		ctx->dbg->tmp = ctx->dbg->nextInstr[0]; // store 0 before it is over-written
		for (int i = 1; i<ctx->dbg->num_bp; i++){
			ctx->dbg->nextInstr[i-1] = ctx->dbg->nextInstr[i];
		}
		ctx->dbg->nextInstr[ctx->dbg->num_bp-1] = 0x00; // reset
	}
	else{
		// modify breakpoints list
		prev->next = ptr->next;
		//left shift all Instr
		ctx->dbg->tmp = ctx->dbg->nextInstr[bp-1];
		for (int i = bp; i<ctx->dbg->num_bp; i++){
			ctx->dbg->nextInstr[i-1] = ctx->dbg->nextInstr[i];
		}
		ctx->dbg->nextInstr[ctx->dbg->num_bp-1] = 0x00; // reset
	}
	ctx->dbg->num_bp -= 1;

	free_breakpoint_info(ptr);

	// reset the int3 instruction to original
	unsigned char instr = *(unsigned char *)addr; // instruction
	// printk("Breakpoint removed at: addr: %d, instr %x\n", (u64)addr, instr);
	// if (instr == 0xCC){ //int3 interrupt
	*(unsigned char *)addr = 0x55; // replace it to push rbp
	// }
	// printk("Value at: addr+1: %d, instr %x\n", (u64)(addr+1), *((unsigned char *)addr+1));

	return 0;
}

/*
 * called from debuggers context
 */
int do_enable_breakpoint(struct exec_context *ctx, void *addr)
{
	// Your code
// Your code
	if (ctx->dbg == NULL)
		return -1;

	struct breakpoint_info * ptr = ctx->dbg->head;

	while(ptr != NULL){
		if (ptr->addr == (u64)addr){
			ptr->status = 1; //disable
			break;
		}
		ptr = ptr->next;
	}

	if (ptr == NULL) //addr not a breakpoint
		return -1;

	*(unsigned char *)addr = 0xCC; // int3 interrupt
	return 0;
}

/*
 * called from debuggers context
 */
int do_disable_breakpoint(struct exec_context *ctx, void *addr)
{
	// Your code	
	if (ctx->dbg == NULL)
		return -1;

	struct breakpoint_info * ptr = ctx->dbg->head;

	while(ptr != NULL){
		if (ptr->addr == (u64)addr){
			ptr->status = 0; //disable
			break;
		}
		ptr = ptr->next;
	}
	//printk("Breakpoint is disabled at addr %d\n", (u64)addr);
	if (ptr == NULL) //addr not a breakpoint
		return -1;

	*(unsigned char *)addr = 0x55; //restore to push rbp
	return 0;
}

/*
 * called from debuggers context
 */ 
int do_info_breakpoints(struct exec_context *ctx, struct breakpoint *ubp)
{
	if (ctx->dbg == NULL)
		return -1;

	struct breakpoint_info * ptr = ctx->dbg->head;

	int num_bp = 0;
	while(ptr != NULL){
		ubp[num_bp].addr = ptr->addr;
		ubp[num_bp].num = ptr->num;
		ubp[num_bp].status = ptr->status;
// how to check if size of ubp is correct
		ptr = ptr->next;
		num_bp++;
	}

	return num_bp;
}

/*
 * called from debuggers context
 */
int do_info_registers(struct exec_context *ctx, struct registers *regs)
{
	// Your code
	if (ctx->dbg == NULL)
		return -1;
	struct exec_context * c_ctx;
	for (int p = 1; p < MAX_PROCESSES; p++){
		if (ctx->pid == p)
			continue;
		c_ctx = get_ctx_by_pid(p);
		if (c_ctx == NULL)
			continue;
		else
			break;
		
	}

	if (c_ctx == NULL)
		return -1;

	regs->rax = c_ctx->regs.rax;
	regs->rbx = c_ctx->regs.rbx;
	regs->rbp = c_ctx->regs.rbp;
	regs->rcx = c_ctx->regs.rcx;
	regs->rdx = c_ctx->regs.rdx;
	regs->rdi = c_ctx->regs.rdi;
	regs->rsi = c_ctx->regs.rsi;
	regs->entry_rsp = c_ctx->regs.entry_rsp;
	regs->entry_rip = c_ctx->regs.entry_rip;
	regs->entry_cs = c_ctx->regs.entry_cs;
	regs->entry_rflags =  c_ctx->regs.entry_rflags;
	regs->entry_ss =  c_ctx->regs.entry_ss;
	regs->r8 = c_ctx->regs.r8;
	regs->r9 = c_ctx->regs.r9;
	regs->r10 = c_ctx->regs.r10;
	regs->r11 = c_ctx->regs.r11;
	regs->r12 = c_ctx->regs.r12;
	regs->r13 = c_ctx->regs.r13;
	regs->r14 = c_ctx->regs.r14;
	regs->r15 = c_ctx->regs.r15;

	return 0;
}

/* 
 * Called from debuggers context
 */
int do_backtrace(struct exec_context *ctx, u64 bt_buf)
{
	if (ctx->dbg == NULL)
		return -1;

	int i = 0, num_bt = 0;
	u64 * b_pt = (u64*)bt_buf;
	while(ctx->dbg->backtrace_addr[i] != 0x00000000){
		b_pt[i] = ctx->dbg->backtrace_addr[i];
		i++;
		num_bt++;
	}

	return num_bt; // or ctx->dbg->num_backtrace;
}


/*
 * When the debugger calls wait
 * it must move to WAITING state 
 * and its child must move to READY state
 */

s64 do_wait_and_continue(struct exec_context *ctx)
{
	struct exec_context * c_ctx;

	for (int p = 1; p < MAX_PROCESSES; p++){
		if (ctx->pid == p)
			continue;
		c_ctx = get_ctx_by_pid(p);
		if (c_ctx == NULL)
			continue;
		else
			break;
		
	}
	// printk("after for\n");
	if (c_ctx == NULL)
		return -1;
	
	ctx->state = WAITING;
	c_ctx->state = READY;
	// printk("before schedule\n");
	schedule(c_ctx);
	return -1;
}

