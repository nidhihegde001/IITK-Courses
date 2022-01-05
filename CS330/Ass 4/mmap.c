#include<types.h>
#include<mmap.h>

u64 tmb = 2*(1<<20);

// Helper function to create a new vm_area
struct vm_area* create_vm_area(u64 start_addr, u64 end_addr, u32 flags, u32 mapping_type)
{
	struct vm_area *new_vm_area = alloc_vm_area();
	new_vm_area-> vm_start = start_addr;
	new_vm_area-> vm_end = end_addr;
	new_vm_area-> access_flags = flags;
	new_vm_area->mapping_type = mapping_type;
	return new_vm_area;
}

// checks phy mapping for 4kb page
u64 check_phy_mapping_4kb(struct exec_context *ctx, u64 addr){
	u64 *vaddr_base = (u64 *)osmap(ctx->pgd);
	u64 *entry;
	u64 pfn;

	// find the entry in page directory
	entry = vaddr_base + ((addr & PGD_MASK) >> PGD_SHIFT);
	if(*entry & 0x1) {
		// PGD->PUD Present, access it
		pfn = (*entry >> PTE_SHIFT) & 0xFFFFFFFF;
		vaddr_base = (u64 *)osmap(pfn);

		entry = vaddr_base + ((addr & PUD_MASK) >> PUD_SHIFT);

		if(*entry & 0x1) {
			// PUD->PMD Present, access it
			pfn = (*entry >> PTE_SHIFT) & 0xFFFFFFFF;
			vaddr_base = (u64 *)osmap(pfn);
			entry = vaddr_base + ((addr & PMD_MASK) >> PMD_SHIFT);

			if(*entry & 0x1) {
				// PMD->PTE Present, access it
				pfn = (*entry >> PTE_SHIFT) & 0xFFFFFFFF;
				vaddr_base = (u64 *)osmap(pfn);
				entry = vaddr_base + ((addr & PTE_MASK) >> PTE_SHIFT);
				if (*entry & 0x1) {// last level PTE, check corresponding bits{
					pfn = (*entry >> PTE_SHIFT) & 0xFFFFFFFF;
					return pfn;
				}
			}
		}
	}
	return 0; // no mapping
}

// checks phy mapping for 2mb page
u64 check_phy_mapping_2mb(struct exec_context *ctx, u64 addr){
	u64 *vaddr_base = (u64 *)osmap(ctx->pgd);
	u64 *entry;
	u64 pfn;

	// find the entry in page directory
	entry = vaddr_base + ((addr & PGD_MASK) >> PGD_SHIFT);
	if(*entry & 0x1) {
		// PGD->PUD Present, access it
		pfn = (*entry >> PTE_SHIFT) & 0xFFFFFFFF;
		vaddr_base = (u64 *)osmap(pfn);

		entry = vaddr_base + ((addr & PUD_MASK) >> PUD_SHIFT);

		if(*entry & 0x1) {
			// PUD->PMD Present, access it
			pfn = (*entry >> PTE_SHIFT) & 0xFFFFFFFF;
			vaddr_base = (u64 *)osmap(pfn);
			entry = vaddr_base + ((addr & PMD_MASK) >> PMD_SHIFT);

			if ((*entry & 0b1) && (*entry & 0b10000000)){ // check 0 and 7 bit position
				// huge page mapping present
				pfn = (*entry >> 21) & 0xFFFFFFFF;
				// return pfn
				return pfn;
			}
		}
	}
	return 0; // no mapping
}

void install_page_table_for_huge_page(struct exec_context *ctx, u64 addr, u64 error_code, u64 pfn_huge_pg) {
	// get base addr of pgdir
	u64 *vaddr_base = (u64 *)osmap(ctx->pgd);
	u64 *entry;
	u64 pfn;
	// set User and Present flags
	// set Write flag if specified in error_code
	u64 ac_flags = 0x5 | (error_code & 0x2);
	
	// find the entry in page directory
	entry = vaddr_base + ((addr & PGD_MASK) >> PGD_SHIFT);
	if(*entry & 0x1) {
		// PGD->PUD Present, access it
		pfn = (*entry >> PTE_SHIFT) & 0xFFFFFFFF;
		vaddr_base = (u64 *)osmap(pfn);
	}else{
		// allocate PUD
		pfn = os_pfn_alloc(OS_PT_REG);
		*entry = (pfn << PTE_SHIFT) | ac_flags;
		vaddr_base = osmap(pfn);
	}

	entry = vaddr_base + ((addr & PUD_MASK) >> PUD_SHIFT);
	if(*entry & 0x1) {
		// PUD->PMD Present, access it
		pfn = (*entry >> PTE_SHIFT) & 0xFFFFFFFF;
		vaddr_base = (u64 *)osmap(pfn);
	}else{
		// allocate PMD
		pfn = os_pfn_alloc(OS_PT_REG);
		*entry = (pfn << PTE_SHIFT) | ac_flags;
		vaddr_base = osmap(pfn);
	}

	entry = vaddr_base + ((addr & PMD_MASK) >> PMD_SHIFT);

	// allocate entry for PMD
	// set 0 and 7th bit
	ac_flags = ac_flags | 0b10000001;
	*entry = (pfn_huge_pg << HUGEPAGE_SHIFT) | ac_flags;
	// printk("*Entry: %x\n", *entry);

}

void install_page_table_for_small_page(struct exec_context *ctx, u64 addr, u64 error_code) {
	// get base addr of pgdir
	u64 *vaddr_base = (u64 *)osmap(ctx->pgd);
	u64 *entry;
	u64 pfn;
	// set User and Present flags
	// set Write flag if specified in error_code
	u64 ac_flags = 0x5 | (error_code & 0x2);
	
	// find the entry in page directory
	entry = vaddr_base + ((addr & PGD_MASK) >> PGD_SHIFT);
	if(*entry & 0x1) {
		// PGD->PUD Present, access it
		pfn = (*entry >> PTE_SHIFT) & 0xFFFFFFFF;
		vaddr_base = (u64 *)osmap(pfn);
	}else{
		// allocate PUD
		pfn = os_pfn_alloc(OS_PT_REG);
		*entry = (pfn << PTE_SHIFT) | ac_flags;
		vaddr_base = osmap(pfn);
	}

	entry = vaddr_base + ((addr & PUD_MASK) >> PUD_SHIFT);
	if(*entry & 0x1) {
		// PUD->PMD Present, access it
		pfn = (*entry >> PTE_SHIFT) & 0xFFFFFFFF;
		vaddr_base = (u64 *)osmap(pfn);
	}else{
		// allocate PMD
		pfn = os_pfn_alloc(OS_PT_REG);
		*entry = (pfn << PTE_SHIFT) | ac_flags;
		vaddr_base = osmap(pfn);
	}

	entry = vaddr_base + ((addr & PMD_MASK) >> PMD_SHIFT);
	if(*entry & 0x1) {
		// PMD->PTE Present, access it
		pfn = (*entry >> PTE_SHIFT) & 0xFFFFFFFF;
		vaddr_base = (u64 *)osmap(pfn);
	}else{
		// allocate PLD 
		pfn = os_pfn_alloc(OS_PT_REG);
		*entry = (pfn << PTE_SHIFT) | ac_flags;
		vaddr_base = osmap(pfn);
	}

	entry = vaddr_base + ((addr & PTE_MASK) >> PTE_SHIFT);
	// since this fault occured as frame was not present, we don't need present check here
	pfn = os_pfn_alloc(USER_REG);
	*entry = (pfn << PTE_SHIFT) | ac_flags;
}


void phys_unmap_util(struct exec_context *ctx, u64 addr, int map_type) 
{
// Clear the PTE

	// !!!!! check mapping exists or not 

	// get base addr of pgdir
	// printk("Entered phys_unmap_util\n");
	u64 *vaddr_base = (u64 *)osmap(ctx->pgd);
	u64 *entry;
	u64 pfn;

	// find the entry in page directory
	entry = vaddr_base + ((addr & PGD_MASK) >> PGD_SHIFT);
	if(*entry & 0x1) {
		// PGD->PUD Present, access it
		pfn = (*entry >> PTE_SHIFT) & 0xFFFFFFFF;
		vaddr_base = (u64 *)osmap(pfn);

		entry = vaddr_base + ((addr & PUD_MASK) >> PUD_SHIFT);

		if(*entry & 0x1) {
			// PUD->PMD Present, access it
			pfn = (*entry >> PTE_SHIFT) & 0xFFFFFFFF;
			vaddr_base = (u64 *)osmap(pfn);
			entry = vaddr_base + ((addr & PMD_MASK) >> PMD_SHIFT);

			if(*entry & 0x1) {
				// PMD->PTE Present, access it
				if (*entry & 0b10000000) // 2mb page mapping present
				{
					if (map_type == HUGE_PAGE_MAPPING) {
						pfn = (*entry >> HUGEPAGE_SHIFT) & 0xFFFFFFFF;
						vaddr_base = (u64 *)osmap(pfn);
						os_hugepage_free(vaddr_base); // freee 2mb page
						*entry = 0; // free the entry in PMD
					}
				}

				else // 4kb mapping type
				{

					pfn = (*entry >> PTE_SHIFT) & 0xFFFFFFFF;
					vaddr_base = (u64 *)osmap(pfn);
					entry = vaddr_base + ((addr & PTE_MASK) >> PTE_SHIFT); // entry in PTE
					if (*entry &0x1)
					{
						if (map_type == NORMAL_PAGE_MAPPING){
							pfn = (*entry >> PTE_SHIFT) & 0xFFFFFFFF;
							os_pfn_free(USER_REG, pfn); // free 4kb page
							*entry = 0; // free the entry in PTE
						}
						
					}
				}
			}
				
		}
	}
	 // Flush TLB
	asm volatile ("invlpg (%0);" 
                    :: "r"(addr) 
                    : "memory");   
    return ;
}


void phys_unmap(struct exec_context *current,u64 start,u64 end, int map_type){
	u64 inc_type;

	if (map_type == HUGE_PAGE_MAPPING)
		inc_type = tmb;
	else
		inc_type = 4096;
    for( u64 addr = start ; addr< end; addr+= inc_type){
        phys_unmap_util(current,addr, map_type);
    }
}


/**
 * Function will invoked whenever there is page fault. (Lazy allocation)
 * 
 * For valid access. Map the physical page 
 * Return 0
 * 
 * For invalid access, i.e Access which is not matching the vm_area access rights (Writing on ReadOnly pages)
 * return -1. 
 */
int vm_area_pagefault(struct exec_context *current, u64 addr, int error_code)
{

	// check existence of vm addr
	// printk("Entered PFH, error code %d\n at addr:", error_code);
	struct vm_area * vm = current->vm_area;
	struct vm_area *curr_vm = NULL;
	int present = 0;
	while(vm !=NULL){
		// printk("Entered while\n");
		if ((vm->vm_start <= addr) && (vm->vm_end > addr)){
			present = 1;
			curr_vm = vm;
			break; //addr lies within allocated vm
		}
		vm= vm->vm_next;
	}

	if (present == 0)
		return -1; // vm addr does not exist

	// // check for permissions
	int read = curr_vm->access_flags & PROT_READ;
	int write = curr_vm->access_flags & PROT_WRITE;
	
	if (error_code == 0x7){ // raise SIGSEGV may be
		return -1;
	}

	if ((error_code == 0x4) && (!read)){
		// printk("No read permission\n");
		return -1;
	}

	if ((error_code == 0x6) && (!write)){
		// printk("No write permission\n");
		return -1;
	}
		

	if (addr%4096 != 0){
		// printk("Wrong Adddr");
		return -1;
	}

	if (curr_vm->mapping_type == NORMAL_PAGE_MAPPING){
		// printk("created small page entry");
		install_page_table_for_small_page(current,addr,error_code);
	}
	else{
		// huge page allocation, and installation
		//printk("created huge page entry\n");
		u64 pfn_huge_pg = get_hugepage_pfn(os_hugepage_alloc());
		install_page_table_for_huge_page(current,addr,error_code,pfn_huge_pg);
	}

	return 1;
}

/*
 * mmap system call implementation.
 */

long vm_area_map(struct exec_context *current, u64 addr, int length, int prot, int flags)
{	

	int prot_invld = 1;
	if ((prot == PROT_READ) || (prot == PROT_WRITE) || (prot == (PROT_READ | PROT_WRITE)))
		prot_invld = 0;

	int flag_invld = 1;
	if ((flags == MAP_FIXED) || (flags == 0))
		flag_invld = 0;

	if ((prot_invld == 1) || (length <= 0) || (addr < 0) || (flag_invld == 1)) // check prot and flags
		return -1;


	if (current->vm_area == NULL) {
		// add a dummy node
		current->vm_area = create_vm_area(MMAP_AREA_START, MMAP_AREA_START+ 0x1000, 0x4, NORMAL_PAGE_MAPPING);
		current->vm_area->vm_next = NULL;
		// printk("Dummy node created\n");
	}

	struct vm_area * vm = current->vm_area;

	u64 st_addr = -1;
	u64 end_addr = -1;

	struct vm_area * curr = vm->vm_next;
	struct vm_area * prev = vm;
	int m_flag = 0; 
	/*
	0-> no merge
	1-> m_prev
	2-> m_curr
	3-> m_both
	*/

	int found = 0;
	struct vm_area * vm_l, *vm_r; // for merge

	if ((u64 *)addr == NULL){
		// printk("Addr is NULL");
		if (flags == MAP_FIXED)
			return -1; // error when addr is NULL

		if (curr == NULL){
			// check permissions with prev
			// printk("Only Dummy node in VMA");
			if ((prev->access_flags==prot) && (prev->mapping_type == NORMAL_PAGE_MAPPING)){
				m_flag = 1;
			}
			else{
				m_flag = 0;
			}

			found = 1;
			st_addr = prev->vm_end;
			end_addr = prev->vm_end + ((length + 4095)/4096) * 4096;
			vm_l = prev;
			vm_r = curr;
		}

		else{
			while((curr!=NULL) && (!found)){
				if (curr->vm_start - prev->vm_end  >= length){
					
					st_addr = prev->vm_end;
					end_addr = prev->vm_end + ((length + 4095)/4096) * 4096;

					// check permissions 
					if ((prev->access_flags==prot) && (prev->mapping_type == NORMAL_PAGE_MAPPING)){
						m_flag = 1;
					}
					if (end_addr == curr->vm_start){
							if ((prev->access_flags==prot) && (curr->access_flags==prot) && (prev->mapping_type == NORMAL_PAGE_MAPPING) && (curr->mapping_type == NORMAL_PAGE_MAPPING))
								m_flag = 3;
							else if ((curr->access_flags==prot) && (curr->mapping_type == NORMAL_PAGE_MAPPING))
								m_flag = 2;
					}

					found = 1;
					vm_l = prev;
					vm_r = curr;
					break;
				}
				prev = curr;
				curr = curr->vm_next;
			}

			// reached end of VM addr list
			if ((!found) && ((MMAP_AREA_END - prev->vm_end) >= length)){
				st_addr = prev->vm_end;
				end_addr = prev->vm_end + ((length + 4095)/4096) * 4096;

				// check permissions 
				if ((prev->access_flags==prot) && (prev->mapping_type == NORMAL_PAGE_MAPPING))
					m_flag = 1;

				found = 1;
				vm_l = prev;
				vm_r = NULL;
				
			}
		}
	}
		
	else{ // addr is non-null (hint is given)

		if (addr%4096 == 0)
			st_addr = addr;
		else
			st_addr = (addr/4096+1)* 4096; // next multiple of 4k
	
        end_addr = st_addr + ((length + 4095)/4096) * 4096; // [st_addr, end_addr)

		// printk("Start address: %x, End Add: %x\n", st_addr, end_addr);

		if (curr == NULL){

			if (st_addr < prev->vm_end){
				// st_addr can't be allocated. So, look for the next available memory (when there is only one node, no need to start re-iterating)
				if (flags == MAP_FIXED){
					return -1;
				}
				st_addr = prev->vm_end;
				end_addr = st_addr + ((length + 4095)/4096) * 4096;

				if ((prev->access_flags==prot) && (prev->mapping_type == NORMAL_PAGE_MAPPING)){
					m_flag = 1;
				}

			}

			else if(prev->vm_end == st_addr){
				// check permissions with prev
				if ((prev->access_flags==prot) && (prev->mapping_type == NORMAL_PAGE_MAPPING)){
					m_flag = 1;
				}
			}

			else{
				m_flag = 0;
			}

			found = 1;
			vm_l = prev;
			vm_r = curr;
		}

		else{

			struct vm_area * tmp_curr = curr;
			struct vm_area * tmp_prev = prev;

			// try allocating from st_addr
			while((tmp_curr != NULL) && (found==0)){

				if ((tmp_prev->vm_end <= st_addr) && (tmp_curr->vm_start >= end_addr))
				{
					// 
					//printk("Do not enter\n");
					found = 1; // space is available

					if ((st_addr == tmp_prev->vm_end) && (end_addr == tmp_curr->vm_start)){
							// printk("Enter here\n");
							if ((tmp_prev->mapping_type == NORMAL_PAGE_MAPPING) && (tmp_curr->mapping_type == NORMAL_PAGE_MAPPING) && (tmp_prev->access_flags==prot) && (tmp_curr->access_flags==prot)){
								m_flag = 3;
								// printk("m_flag 3\n");
							}
								
							else if ((tmp_curr->access_flags==prot) && (tmp_curr->mapping_type == NORMAL_PAGE_MAPPING))
								m_flag = 2;
							else if ((tmp_prev->access_flags==prot) && (tmp_prev->mapping_type == NORMAL_PAGE_MAPPING))
								m_flag = 1;
					}

					else if (st_addr == tmp_prev->vm_end){
						if ((tmp_prev->access_flags==prot) && (tmp_prev->mapping_type == NORMAL_PAGE_MAPPING))
								m_flag = 1;
					}

					else if (end_addr == tmp_curr->vm_start){
						if ((tmp_curr->access_flags==prot) && (tmp_curr->mapping_type == NORMAL_PAGE_MAPPING))
								m_flag = 2;
					}

					else{
						m_flag = 0;
					}

					vm_l = tmp_prev;
					vm_r = tmp_curr;

					//printk("vm_l: %x, vm_r: %x\n", vm_l, vm_r);

					// break;
				}

				else if ((st_addr >= tmp_curr->vm_start) && (st_addr < tmp_curr->vm_end)){ // start addr is inside tmp_curr
					// can't be allocated, so assume addr is NULL and re-start
					break;
				}

				else if ((st_addr >= tmp_curr->vm_end) && (tmp_curr->vm_next == NULL)){
					//printk("Entered correct if !\n");
					// next time in while loop, tmp_curr becomes NULL
					if (st_addr == tmp_curr->vm_end){
						if ((tmp_curr->access_flags == prot) && (tmp_curr->mapping_type == NORMAL_PAGE_MAPPING)){
							m_flag = 1;
							found = 1;
							vm_l = tmp_curr;
							vm_r = NULL;
						}

						else{ // can't be merged with prev
							found = 1;
							vm_l = tmp_curr;
							vm_r = NULL;
							m_flag = 0;

						}
					}

					else{
						m_flag = 0;
						found = 1;
						vm_l = tmp_curr;
						vm_r = NULL;
					}
				}
				
				tmp_prev = tmp_curr;
				tmp_curr = tmp_curr->vm_next;
			}

			if (found== 0){
				// assume addr is NULL and re-start
			//	printk("Restarted!\n");
				if (flags == MAP_FIXED)
					return -1;

				while((curr!=NULL) && (!found)){
					if ((curr->vm_start - prev->vm_end) >= length){
						
						st_addr = prev->vm_end;
						// printk("Space found starting at: %x", st_addr);
						end_addr = prev->vm_end + ((length + 4095)/4096) * 4096;

						// check permissions 
						if (end_addr == curr->vm_start){
								if ((prev->access_flags==prot) && (curr->access_flags==prot) && (prev->mapping_type == NORMAL_PAGE_MAPPING) && (curr->mapping_type == NORMAL_PAGE_MAPPING))
									m_flag = 3;
								else if ((curr->access_flags==prot) && (curr->mapping_type == NORMAL_PAGE_MAPPING))
									m_flag = 2;
								else if ((prev->access_flags==prot) && (prev->mapping_type == NORMAL_PAGE_MAPPING))
									m_flag = 1;
						}
						found = 1;
						vm_l = prev;
						vm_r = curr;
						break;
					}
					prev = curr;
					curr = curr->vm_next;
				}

				// reached end of VM addr list
				if ((!found) && (MMAP_AREA_END - prev->vm_end >= length)){
					st_addr = prev->vm_end;
					end_addr = prev->vm_end + ((length + 4095)/4096) * 4096;

					// check permissions 
					if ((prev->access_flags==prot) && (prev->mapping_type == NORMAL_PAGE_MAPPING))
						m_flag = 1;

					found = 1;
					vm_l = prev;
					vm_r = NULL;

				}
			}		
		}
	}

	// merge areas depending on m_flag
	// printk("Found = %d", found);
   if(found){
        if(m_flag == 0){
			// printk("m_flag = 0\n");
            // if (count == 128 )
            //     return -EINVAL;
            if(end_addr>MMAP_AREA_END){
                return -1;
            }
			struct vm_area * new = create_vm_area(st_addr, end_addr, prot, NORMAL_PAGE_MAPPING);
			new->vm_next = vm_r;
            if(vm_l!=NULL){
                vm_l->vm_next = new;
			}
            else
            {
                current->vm_area = new;
            }
        }
        else if (m_flag == 1){
            vm_l->vm_end = end_addr;
        } 
        else if (m_flag == 2 ){
            vm_r->vm_start = st_addr;
        }
        else {
            vm_l->vm_end = vm_r->vm_end;
            vm_l->vm_next = vm_r->vm_next;
            dealloc_vm_area(vm_r);
        }
	}

	return st_addr;
}


/**
 * munmap system call implemenations
 */

int vm_area_unmap(struct exec_context *current, u64 addr, int length)
{
	if ((length <= 0) || (addr%4096 != 0)) // addr is not page-aligned
		return -1;

	u64 str_addr = addr, end_addr = ((addr + length + 4095)/4096)*4096; // end_addr is not to be assigned now

	struct vm_area * str = NULL, *end = NULL, *str_prev = NULL;
	// int count_vm = 0;
	int s_found = 0, e_found = 0; 
	int d_flag = 0;
	/*
	0->no map
	1->end of vm_a, starts within same vm_a
	2->start of vm_a, ends within same vm_a
	3-> middle of a vm_a 
	4-> complete vm_a
	5-> starts in 1, ends in other
	*/

	if (current->vm_area == NULL)
		return -1; // assumption- dummy node always present
		
	struct vm_area *curr  = current->vm_area->vm_next;
	struct vm_area *prev = current->vm_area;
	struct vm_area *p_prev = NULL;

	if (curr == NULL){
		//printk("Enter on second unmap\n");
		if (prev->vm_end == MMAP_AREA_START+ 0x1000){
			d_flag = 0;
			return 0; // no unmap needed, assuming unmap is not called on dummy node
		}

		else{
			if (str_addr <= prev->vm_end){

				s_found = 1;

				if (end_addr < prev->vm_end){
					e_found = 1;
					d_flag = 3;
				}

				else{
					e_found = 1;					
					end_addr = prev->vm_end;
					d_flag = 1;
				}

				str = prev;
				end = prev;
				// phys_unmap(prev,str_addr,prev->vm_end);
				// prev->vm_end = str_addr;
			}
		}
	}


	else{

		// printk("Enter on first unmap\n");
		int enter = 0;
		int s_map_type = -1;
		// prinft("Start address: %x, End Add: %x\n", str_addr, end_addr);
		while((prev!=NULL) && (s_found == 0)){
			// printk("prev strt: %x, prev_end: %x\n", prev->vm_start, prev->vm_end);
			enter++;
			if ((str_addr >= prev->vm_start) && (str_addr < prev->vm_end)){ // addr lies in this vm_node
				// printk("%d node\n", enter);
				s_found = 1;
				str = prev;
				s_map_type = str->mapping_type;
				str_prev = p_prev;

				if (s_map_type == NORMAL_PAGE_MAPPING)
					end_addr = ((addr + length + 4095)/4096)*4096;
				else if (s_map_type == HUGE_PAGE_MAPPING){
					if (str_addr % tmb !=0){ // starting addr not aligned, find nearest 2mb alignment before
						str_addr = (str_addr/tmb)*tmb; // guaranteed that huge page can't start after this new str_addr
					}
					end_addr = ((addr + length + tmb-1)/tmb)*tmb; // find the next tmb aligned addr
				}
					
				if (end_addr <= prev->vm_end){
					e_found = 1;
					end = prev;

					if ((str_addr == prev->vm_start) && (end_addr == prev->vm_end)){
						// printk("d_flag set t0 4\n");
						d_flag = 4; // complete node removed
					}

					else if (str_addr == prev->vm_start){
						d_flag = 2;
					}

					else if (end_addr == prev->vm_end){
						d_flag = 1;
					}

					else{
						d_flag = 3; // middle
					}
				}

				else{// end does not lie within the same node

					if (curr == NULL){

						end_addr = prev->vm_end; // end_addr lies outside prev->vm_end
						e_found = 1;
						end = prev;
						if (str_addr == prev->vm_start){
							d_flag = 4; // complete node removed
						}
						else {
							d_flag = 1;
						}
						// break;
					}

					else{
						// find end node str = prev
						while((curr != NULL) && (e_found == 0)){
							if ((end_addr > curr->vm_start) && (end_addr <= curr->vm_end)){ // end_addr lies within some vm_node
								e_found = 1;
								end = curr;
								d_flag = 5;
							}

							else if (end_addr < curr->vm_start){ // already lies in unallocated area

								if (prev == str)
									d_flag = 1;
								else
									d_flag = 5;

								e_found = 1;
								end = prev;
								end_addr = prev->vm_end;
							}

							prev = curr;
							curr = curr->vm_next;
						}
						// lies outside of last node, curr becomes null before finding end
						if (e_found == 0){
							e_found = 1;
							d_flag = 5; // start and end can't be same
							end = prev; // last node of list
							end_addr = prev->vm_end;
						}
					}
				}
			}

			else if (str_addr < prev->vm_start){ // start lies in unallocated region
				s_found = 1;
				str = prev;
				s_map_type = str->mapping_type;
				str_prev = p_prev;
				str_addr = prev->vm_start;

				if (s_map_type == NORMAL_PAGE_MAPPING)
					end_addr = ((addr + length + 4095)/4096)*4096;
				else if (s_map_type == HUGE_PAGE_MAPPING){
					//  str_addr will always be tmb aligned for a huge page
					end_addr = ((addr + length + tmb-1)/tmb)*tmb; // find the next tmb aligned addr
				}
					
				if (end_addr <= prev->vm_end){ // end lies within same node
					e_found = 1; 

					if (end_addr == prev->vm_end){
						d_flag = 4; // remove complete node
					}

					else{
						d_flag = 2;
					}
					end = prev;
				}

				else{// end does not lie within the same node

					if (curr == NULL){
						end_addr = prev->vm_end; // end_addr lies outside prev->vm_end
						e_found = 1;
						d_flag = 4;
						end = prev;
					}

					else{
						while((curr != NULL) && (e_found == 0)){
							if ((end_addr > curr->vm_start) && (end_addr <= curr->vm_end)){ // end_addr lies within some vm_node
								e_found = 1;
								end = curr;
								d_flag = 5;
							}

							else if (end_addr < curr->vm_start){ // already lies in unallocated area
								e_found = 1;
								end = prev;
								end_addr = prev->vm_end;

								if (prev == str)
									d_flag = 4;
								else
									d_flag = 5;
							}

							prev = curr;
							curr = curr->vm_next;
						}
						// lies outside of last node
						if (e_found == 0){
							e_found = 1;
							if (str == prev){
								d_flag = 4;
							}
							else{
							d_flag = 5;
							}
							end = prev; // last node of list
							end_addr = prev->vm_end;
						}
					}
				}

			}

			p_prev = prev;
			prev = curr;
			curr = curr->vm_next;
		}
	}


	if (s_found == 0){ // s is out of allocated memory
		return 0;	
	}

	if (d_flag == 1){
		str->vm_end = str_addr;
		phys_unmap(current,str_addr,end_addr, str->mapping_type);
	}

	else if(d_flag == 2){
		str->vm_start = end_addr;
		phys_unmap(current,str_addr,end_addr, str->mapping_type);
	}

	else if (d_flag == 3){
		struct vm_area*  new = alloc_vm_area();
		new->vm_start = end_addr;           
		new->vm_end = str->vm_end;
		new->vm_next = str->vm_next;
		new->access_flags = str->access_flags;
		new->mapping_type = str->mapping_type;
		str->vm_end = str_addr;
		str->vm_next = new;
		phys_unmap(current,str_addr,end_addr, str->mapping_type);
	}
	else if(d_flag == 4){
		// remove complete node
		// printk("Entered - remove complete node\n");
		if (str_prev == NULL)
			current->vm_area = str->vm_next;
		else
			str_prev->vm_next = str->vm_next;

		phys_unmap(current,str->vm_start,str->vm_end, str->mapping_type);
		dealloc_vm_area(str);
	}

	else if (d_flag == 5){
		// spans across multiple nodes
		struct vm_area * er_curr = str, *tmp = NULL;

		// phys_unmap(current,str_addr,end_addr);

		if ((str->vm_start == str_addr) && (end->vm_end == end_addr)){

			str_prev->vm_next = end->vm_next;

			// deallocate all vm areas in between and unmap the space

			while(er_curr != end->vm_next){
				phys_unmap(current, er_curr->vm_start, er_curr->vm_end, er_curr->mapping_type);
				tmp = er_curr->vm_next; // deallocate all vm areas in between
				dealloc_vm_area(er_curr);
				er_curr = tmp;
			}
    	}

		else if (str->vm_start == str_addr){
			// deallocate start completely
			str_prev->vm_next = end;

			while(er_curr != end){
				phys_unmap(current, er_curr->vm_start, er_curr->vm_end, er_curr->mapping_type);
				tmp = er_curr->vm_next; // deallocate all vm areas in between
				dealloc_vm_area(er_curr);
				er_curr = tmp;
			}

			phys_unmap(current, end->vm_start, end_addr, end->mapping_type);

			end->vm_start = end_addr;
		}

		else if (end->vm_end == end_addr){
			// deallocate end completely

			er_curr = str->vm_next;
			str->vm_next = end->vm_next;

			while(er_curr != end->vm_next){
				phys_unmap(current, er_curr->vm_start, er_curr->vm_end, er_curr->mapping_type);
				tmp = er_curr->vm_next; // deallocate all vm areas in between
				dealloc_vm_area(er_curr);
				er_curr = tmp;
			}

			phys_unmap(current, str_addr, str->vm_end, str->mapping_type);

			str->vm_end = str_addr;

		}

		else {// both lie in middle of the respective nodes

			// remove intermediate nodes
			er_curr = str->vm_next;
			str->vm_next = end;

			while(er_curr != end){
				phys_unmap(current, er_curr->vm_start, er_curr->vm_end, er_curr->mapping_type);
				tmp = er_curr->vm_next; // deallocate all vm areas in between
				dealloc_vm_area(er_curr);
				er_curr = tmp;
			}

			phys_unmap(current, str_addr, str->vm_end, str->mapping_type);
			phys_unmap(current, end->vm_start, end_addr, end->mapping_type);

			str->vm_end = str_addr;
			end->vm_start = end_addr;
		}



	}

	return 0;
}



/**
 * make_hugepage system call implemenation
 */


long vm_area_make_hugepage(struct exec_context *current, void *addr, u32 length, u32 prot, u32 force_prot)
{

	// virtual memory part

	// check for fault in arguments
	int prot_invld = 1;
	// or combination
	if ((prot == PROT_READ) || (prot == PROT_WRITE) || (prot == (PROT_READ | PROT_WRITE))){
		prot_invld = 0;
	}

	int force_invld = 1;
	if ((force_prot == 0) || (force_prot == 1))
		 force_invld =0;

	if ((prot_invld == 1) || (force_invld == 1) || (addr == NULL) || (length <= 0))
		return -EINVAL;


	if (current->vm_area == NULL)
		return -1;

	struct vm_area *curr = current->vm_area->vm_next;
	struct vm_area *prev = current->vm_area;

	u64 st_ad, en_ad;
	st_ad = (u64)addr;
	en_ad = (u64)addr + length;

	struct vm_area *str = NULL, *end = NULL, *str_prev = NULL;

	int s_found = 0, e_found = 0;

	while((curr != NULL) && ((!s_found) || (!e_found))){

		if (!s_found){
			if ((st_ad >= prev->vm_end) && (st_ad < curr->vm_start)){
				return -ENOMAPPING;
			}

			else if ((st_ad < curr->vm_end) && (st_ad >= curr->vm_start)){
				// st_ad in current node
				// printk("START found\n");
				s_found = 1;
				str = curr;
				str_prev = prev;
			}
		}

		if (!e_found){
			if ((en_ad > prev->vm_end) && (en_ad <= curr->vm_start)){
				return -ENOMAPPING;
			}

			else if ((en_ad <= curr->vm_end) && (en_ad > curr->vm_start)){
				// printk("END found\n");
				e_found =1;
				// st_ad in current node
				end = curr;
			}
		}

		prev = curr;
		curr = curr->vm_next;
	}

	if (s_found==0){
		return -1;
	}

	if (e_found==0){
		return -1;
	}


	//3. contiguous pages check
	
	struct vm_area *iter = str;
	struct vm_area *iter_prev = str_prev;
	

	while(iter != end->vm_next){
		// printk("Entered ctgs check\n");
		if ((iter != str)){
			if (iter_prev->vm_end != iter->vm_start)
				return -ENOMAPPING; // contiguous check

		}
		if (iter->mapping_type == HUGE_PAGE_MAPPING){
			return -EVMAOCCUPIED;
		}
		if (force_prot == 0){
			if (iter->access_flags != prot)
				return -EDIFFPROT;
		}
		iter_prev = iter;
		iter = iter->vm_next;
	}

	//set st_ad and en_ad to correct values
	// printk("Addr: %x, len: %x\n", addr, length);
	if (((u64)addr)% tmb == 0)
		st_ad = (u64)addr;
	else
		st_ad = ((u64)addr/tmb +1)*tmb; // next multiple of 2MB

	en_ad = (u64)addr + length;

	if (en_ad % tmb  != 0)
		en_ad = (en_ad/tmb )*tmb ; // nearest 2mb aligned

	// printk("St_ad: %x, en_ad: %x\n", st_ad, en_ad);


	//1. find st_ad
	curr = current->vm_area->vm_next;
	prev = current->vm_area;

	int s_flag = 0, e_flag = 0; // 3-middle, 2-end

	while((curr != NULL) && (s_flag == 0)){
		if ((st_ad < curr->vm_end) && (st_ad >= curr->vm_start)){
			// st_ad in current node
			str = curr;
			str_prev = prev;
			if (st_ad == curr->vm_start){
				// printk("S flag is 2\n");
				s_flag = 2;
			}

			else{
				// printk("S flag is 3\n");
				s_flag = 3;
			}
			break;
		}
		prev = curr;
		curr = curr->vm_next;
	}


	//2. find en_ad
	curr = current->vm_area->vm_next;
	prev = current->vm_area;

	while((curr != NULL)&& (e_flag == 0)){
		if ((en_ad <= curr->vm_end) && (en_ad > curr->vm_start)){
			// st_ad in current node
			end = curr;
			if (en_ad == curr->vm_end){
				// printk("e flag is 2\n");
				e_flag = 2;
			}

			else{
				// printk("e flag is 3\n");
				e_flag = 3;
			}
			break;
		}
		prev = curr;
		curr = curr->vm_next;
	}

	// 5.check/copy Physical mapping info of pages in - st_ad, en_ad 
	// printk("Phy mapping started\n");
	int map_present = 0;
	u64 pfn = -1;
	u64 * v_huge_page  = NULL;
	u64 *vaddr_base;


	for (u64 pg = st_ad; pg <en_ad; pg+=tmb){ // huge pages in new area
		map_present = 0;
		for (u64 s_pg = pg; s_pg < pg + tmb; s_pg+=4096){ // small pages
			pfn = check_phy_mapping_4kb(current, s_pg);
			if (pfn){
				// mapping present
				if (!map_present){
					map_present = 1;
					
					// allocate a huge page
					v_huge_page = (u64 *)os_hugepage_alloc();
					// printk("Allocated huge page physically when s_pg = %x\n", s_pg);
				}

				vaddr_base = osmap(pfn); // physical mapping addr		
				// copy memory
				// printk("Arguments : %x, %x", (u64)((char *)v_huge_page+s_pg-pg), (u64)vaddr_base);
				memcpy((char *)((char *)v_huge_page+s_pg-pg), (char *)vaddr_base, 4096);
			}
		}
		// per huge page
		// update PTE's for the new frame v_huge_page
		if (map_present){ // at least one of the pages has a physical mapping
			// install pte
			phys_unmap(current, pg, pg + tmb, NORMAL_PAGE_MAPPING); // free the old 4kb pages
			u64 pfn_huge_pg = get_hugepage_pfn(v_huge_page);
			install_page_table_for_huge_page(current, (u64)pg, (u64)prot, pfn_huge_pg);
			// printk("Installed pg_entry\n");
			// free old frame
			
		}
	}



	//4. Merge normal pages
	struct vm_area* new = alloc_vm_area();
	new->vm_start = st_ad;
	new->vm_end = en_ad;
	new->mapping_type = HUGE_PAGE_MAPPING;
	new->access_flags = prot;
	new->vm_next = NULL; // temp
	// merge huge pages flag
	int m_flag = 0; 
	struct vm_area *tmp = NULL;

	// 6. deallocate vm areas and reassign the pointers
	iter = str;
	// printk("str: %x, end:%x\n", str, end);
	while(iter != end->vm_next){

		if ((iter==str) && (iter==end)){
			// printk("Entered iter==str==end\n");
			if ((s_flag == 2) && (e_flag==2)){
				if (str_prev->mapping_type == HUGE_PAGE_MAPPING){
					// merge
					if ((str_prev->vm_end == st_ad) && (str_prev->access_flags == prot)){ // can be merged with prev node
						// printk("Mergable with prev node, m_flag = 1\n");
						m_flag = 1;
					}
				}
	
				if ((end->vm_next != NULL) && (end->vm_next)->mapping_type == HUGE_PAGE_MAPPING){
					// merge
					if (((end->vm_next)->vm_start == en_ad) && ((end->vm_next)->access_flags == prot)){ // can be merged with next node
						if (m_flag == 1) // merge possible at left side too
							m_flag = 3;
						else 
							m_flag = 2;
					}
				}

				iter->mapping_type = HUGE_PAGE_MAPPING;
				iter->access_flags = prot;
				dealloc_vm_area(new); // no need of this now
				new = iter;
			}

			else if((s_flag == 3) && (e_flag == 2)){ // str can't be a huge page
				if (!(end->vm_next == NULL) && (end->vm_next)->mapping_type == HUGE_PAGE_MAPPING){
					// merge
					if (((end->vm_next)->vm_start == en_ad) && ((end->vm_next)->access_flags == prot)){ // can be merged with next node
						if (m_flag == 1) // merge possible at left side too
							m_flag = 3;
						else 
							m_flag = 2;
					}
				}
				new->vm_next = end->vm_next;

				str->vm_end = st_ad;
				str->vm_next = new;
			}

			else if ((s_flag == 2) && (e_flag == 3)){

				if (str_prev->mapping_type == HUGE_PAGE_MAPPING){
					// merge
					if ((str_prev->vm_end == st_ad) && (str_prev->access_flags == prot)){ // can be merged with prev node
						// printk("Mergable with prev node, m_flag = 1\n");
						m_flag = 1;
					}
				}
				str_prev->vm_next = new;

				end->vm_start = en_ad;
				new->vm_next = end;
			}

			else if ((s_flag == 3) && (e_flag == 3)){
				// divide into 3 nodes
				struct vm_area* upd_end = alloc_vm_area();
				upd_end->vm_start = en_ad;
				upd_end->vm_end = end->vm_end;
				upd_end->mapping_type = end->mapping_type;
				upd_end->access_flags = end->access_flags;
				upd_end->vm_next = end->vm_next; 

				str->vm_end = st_ad;
				str->vm_next = new;

				new->vm_next = upd_end;
				m_flag = 0;
			}

			break;
		}
		else if (iter == str){
			if (s_flag == 2){
				if (str_prev->mapping_type == HUGE_PAGE_MAPPING){
					// merge
					if ((str_prev->vm_end == st_ad) && (str_prev->access_flags == prot)){ // can be merged with prev node
						// printk("Mergable with prev node, m_flag = 1\n");
						m_flag = 1;
					}
				}
				// printk("start addr of str_prev: %x\n", str_prev->vm_start);
				str_prev->vm_next = new;
				// printk("new: %x\n", new->vm_start);
				iter = str->vm_next;
				// deallocate whole area
				// phys_unmap(current, iter->vm_start, iter->vm_end);
				dealloc_vm_area(str);
			}
			else if(s_flag == 3){ // can't be a huge page
				// phys_unmap(current, st_ad,  iter->vm_end);
				str->vm_end = st_ad;
				iter = str->vm_next;
				str->vm_next = new;
			}
		}

		else if (iter == end){
			if (e_flag == 2){
				if ((end->vm_next != NULL) && (end->vm_next)->mapping_type == HUGE_PAGE_MAPPING){
					// merge
					if (((end->vm_next)->vm_start == en_ad) && ((end->vm_next)->access_flags == prot)){ // can be merged with next node
						if (m_flag == 1) // merge possible at left side too
							m_flag = 3;
						else 
							m_flag = 2;
					}
				}
				new->vm_next = end->vm_next;
				// deallocate whole area
				// phys_unmap(current, iter->vm_start, iter->vm_end);
				dealloc_vm_area(end);
			}
			else if(e_flag == 3){ // can't be a huge page
				// phys_unmap(current, end->vm_start, en_ad);
				end->vm_start = en_ad;
				new->vm_next = end;
			}
		break;
		}

		else if ((iter!= str) && (iter!= end)){
			// phys_unmap(current, iter->vm_start, iter->vm_end);
			tmp = iter->vm_next;
			dealloc_vm_area(iter); // deallocate intermediate nodes
			iter = tmp;
		}
	}

	//7.Merging of huge pages

	if (m_flag == 1){

		if (s_flag == 2){
			str_prev->vm_next = new->vm_next;
			str_prev->vm_end = new->vm_end;
		}
		else if (s_flag == 3){
			str->vm_next = new->vm_next;
			str->vm_end = new->vm_end;
		}

		dealloc_vm_area(new);
	}

	else if(m_flag == 2){



		if (e_flag = 2){
			end = new->vm_next;
		}
		end->vm_start = st_ad;

		if (s_flag == 2)
			str_prev->vm_next = end;
		else if (s_flag == 3)
			str->vm_next = end;

		dealloc_vm_area(new);
	}

	else if (m_flag == 3){

		if ((s_flag == 2) && (e_flag == 2)){
			str_prev->vm_end = (new->vm_next)->vm_end;
			str_prev->vm_next = (new->vm_next)->vm_next;
			dealloc_vm_area(new->vm_next);
			dealloc_vm_area(new);
		}

		else if ((s_flag == 2)&&(e_flag == 3)){

			str_prev->vm_end = (end)->vm_end;
			str_prev->vm_next = (end)->vm_next;
			dealloc_vm_area(end);
			dealloc_vm_area(new);
		}


		else if ((s_flag == 3) && (e_flag == 2)){
			str->vm_end = (new->vm_next)->vm_end;
			str->vm_next = (new->vm_next)->vm_next;
			dealloc_vm_area(new->vm_next);
			dealloc_vm_area(new);
		}

		else if ((s_flag == 3)&&(e_flag == 3)){

			str->vm_end = (end)->vm_end;
			str->vm_next = (end)->vm_next;
			dealloc_vm_area(end);
			dealloc_vm_area(new);
		}
		
	}
	
	return (long)st_ad;
}


/**
 * break_system call implemenation
 */

void phys_break_util(struct exec_context *ctx, u64 addr,u64 huge_pg_pfn, struct vm_area*vm) // addr is the v address of  huge page that is to be broken
{

	// printk("Entered phys_break_util\n");
	u64 pfn, pfn_pte, pfn_4kpg;
	u64* v_huge_page = (u64*)osmap(huge_pg_pfn);
	// printk("V huge page: %x\n", v_huge_page);
	u64 *vaddr_base = (u64 *)osmap(ctx->pgd);
	u64 *vaddr_pte;
	u64 *entry;

	// set User and Present flags
	// set Write flag if specified in error_code
	u64 ac_flags = 0x5 | (vm->access_flags & 0x2); // ?????


	// allocate 512 pages of 4kb
	// copy contents
	// change page table 

	// find pmd

	entry = vaddr_base + ((addr & PGD_MASK) >> PGD_SHIFT); // entry in PGD

	if(*entry & 0x1) {
		// PGD->PUD Present, access it
		pfn = (*entry >> PTE_SHIFT) & 0xFFFFFFFF;
		vaddr_base = (u64 *)osmap(pfn);
	
		entry = vaddr_base + ((addr & PUD_MASK) >> PUD_SHIFT); // entry in PUD
		if(*entry & 0x1) {
			// PUD->PMD Present, access it
			pfn = (*entry >> PTE_SHIFT) & 0xFFFFFFFF;
			vaddr_base = (u64 *)osmap(pfn);

			entry = vaddr_base + ((addr & PMD_MASK) >> PMD_SHIFT); // entry in PMD

			if(((*entry & 0x1) == 0x1) && ((*entry & (0x1 << 7)) == (0x1 << 7))){ // there is a hugepage mapping for this PMD
				// assign u64 v_huge_page
				// printk("Huge page intercepted\n");
				// v_huge_page = (u64*) osmap((*entry >> HUGEPAGE_SHIFT) & 0xFFFFFFFF);
				// printk("V huge page correct: %x\n", v_huge_page);

				pfn_pte = os_pfn_alloc(OS_PT_REG); 

				*entry = (pfn_pte << PTE_SHIFT) | ac_flags; // changing PMD entry to a newly allocated PTE
				vaddr_pte = osmap(pfn_pte); // virtual addr of PTE entry

				// fille entries of this PTE from 0 to 2^9

				for (int pg = 0; pg < 512; pg++){

					pfn_4kpg = os_pfn_alloc(USER_REG); // allocate a pfn for 4kb
					u64 *vaddr_base = (u64 *)osmap(pfn_4kpg);

					memcpy((char *)vaddr_base, (char *)((char *)v_huge_page + pg*4096), 4096); // !!! copy contents

					// install PTE entry for the pfn at addr+pg*4096
					entry = vaddr_pte + ((addr & PTE_MASK) >> PTE_SHIFT); // going to the correct PTE entry
					//  entry = vaddr_pte + pg;
					*entry = (pfn_4kpg << PTE_SHIFT) | ac_flags; // assign PTE entry
					addr = addr+ pg*4096;
				}
			}
		}
	}

	// Flush TLB
	asm volatile ("invlpg (%0);" 
					:: "r"(addr) 
					: "memory");   
	os_hugepage_free((u64 *)addr);	
}

void break_util(struct exec_context *current, struct vm_area * vm, u64 start, u64 end){
    for( u64 addr = start ; addr< end; addr+=tmb){
		u64 pfn = check_phy_mapping_2mb(current, addr);
		if ( pfn != 0) // mapping present for that huge page
        	phys_break_util(current,addr,pfn, vm);
    }
}


int vm_area_break_hugepage(struct exec_context *current, void *addr, u32 length)
{

	if (((u64)addr % tmb !=0) || (length % tmb !=0))
		return -EINVAL;

	u64 st_ad = (u64)addr;
	u64 en_ad = (u64)addr + length;

	// printk("St_ad: %x, en_ad:%x for break\n", st_ad, en_ad);


	int s_flag = 0, e_flag = 0; // 3-middle, 2-end, 0-unmapped region

	if (current->vm_area == NULL)
		return -1;

	struct vm_area *curr = current->vm_area->vm_next;
	struct vm_area *prev = current->vm_area;

	struct vm_area *str=NULL, *end = NULL, *str_prev = NULL, *end_prev = NULL;


	// find st_ad
	while((curr != NULL) && (s_flag == 0)){
		if ((st_ad < curr->vm_end) && (st_ad >= curr->vm_start)){
			// st_ad in current node
			str = curr;
			str_prev = prev;
			if (st_ad == curr->vm_start){
				// printk("Break entered s_flag = 2");
				s_flag = 2;
			}

			else{
				s_flag = 3;
			}
		}

		else if ((st_ad >= prev->vm_end) && (st_ad < curr->vm_start)){
			// lies in unmapped region
			str_prev =prev;
			str  = curr;
			st_ad = str->vm_start;
			break;
		}
		prev = curr;
		curr = curr->vm_next;
	}


	//2. find en_ad
	curr = current->vm_area->vm_next;
	prev = current->vm_area;

	while((curr != NULL)&& (e_flag == 0)){
		if ((en_ad <= curr->vm_end) && (en_ad > curr->vm_start)){
			// st_ad in current node
			end_prev = prev;
			end = curr;
			if (en_ad == curr->vm_end){
				// printk("Break entered e_flag = 2");
				e_flag = 2;
			}

			else{
				e_flag = 3;
			}
		}

		else if ( ((en_ad > prev->vm_end) && (en_ad <= curr->vm_start))){
			// lies in unmapped region
			end  = prev;
			en_ad = prev->vm_end;
			break;
		}
		prev = curr;
		curr = curr->vm_next;
	}

	// printk("str: %x, end: %x\n", (u64)str, (u64)end);
	// printk("str_prev: %x, end_prev: %x\n", (u64)str_prev, (u64)end_prev);

	struct vm_area *iter = str;
	int m_flag = 0; // 0->none
	struct vm_area *iter_prev = NULL;


	struct vm_area * new_s = NULL, *new_e = NULL;
	
	while(iter != end->vm_next){
		if (iter == str){
			if (s_flag == 2){
				if (str->mapping_type == HUGE_PAGE_MAPPING){
					// printk("Entered correct if: break");
					if ((str_prev->vm_end == st_ad) && (str_prev->mapping_type == NORMAL_PAGE_MAPPING) && (str_prev->access_flags == str->access_flags)){ // can be merged with prev node
						m_flag = 1;
					}
					break_util(current, iter, iter->vm_start, iter->vm_end);
					iter->mapping_type = NORMAL_PAGE_MAPPING;
					new_s = iter;
				}
			}

			else if(s_flag == 3){ 
				if (str->mapping_type == HUGE_PAGE_MAPPING){ // in middle
					// break into 2
					new_s = create_vm_area(st_ad, iter->vm_end, str->access_flags, NORMAL_PAGE_MAPPING);
					new_s->vm_next = str->vm_next;
					str->vm_next = new_s;
					str->vm_end = st_ad;
					break_util(current, new_s, new_s->vm_start, new_s->vm_end);
					// can't be merged with prev

				}
			}
		}

		else if (iter == end){
			if (e_flag == 2){
				if (end->mapping_type == HUGE_PAGE_MAPPING){
					// merge
					if ((end->vm_next != NULL) && ((end->vm_next)->vm_start == en_ad) && ((end->vm_next)->mapping_type == NORMAL_PAGE_MAPPING) && ((end->vm_next)->access_flags == end->access_flags)){ // can be merged with next node
						if (m_flag == 1) // merge possible at left side too
							m_flag = 3;
						else 
							m_flag = 2;
					}
					break_util(current,  iter, iter->vm_start, iter->vm_end);
					iter->mapping_type = NORMAL_PAGE_MAPPING;
					new_e = iter;
				}
			}
			else if(e_flag == 3){ 
				if (end->mapping_type == HUGE_PAGE_MAPPING){ // in middle
					// break into 2
					new_e = create_vm_area(iter->vm_start, en_ad, end->access_flags, NORMAL_PAGE_MAPPING);
					new_e->vm_next = end;
					end_prev->vm_next = new_e;
					end->vm_start = en_ad;
					break_util(current, new_e, new_e->vm_start, new_e->vm_end);
					// can't be merged with next
				}
			}
		}

		else{
			
			if (iter->mapping_type == HUGE_PAGE_MAPPING){
				break_util(current,  iter, iter->vm_start, iter->vm_end); // break entire huge page
				iter->mapping_type = NORMAL_PAGE_MAPPING;
			}
		}
		iter = iter->vm_next;
	}


	// merge start and end if possible

	if (m_flag == 1){
		// new_s can be merged with str_prev
		if (s_flag == 2){
			str_prev->vm_next = new_s->vm_next;
			str_prev->vm_end = new_s->vm_end;
			dealloc_vm_area(new_s);
		}
	}

	else if(m_flag == 2){
		// new_e can be merged with end->next
		if (e_flag == 2){
			new_e->vm_next = (end->vm_next)->vm_next;
			new_e->vm_end = (end->vm_next)->vm_end;
			dealloc_vm_area(end->vm_next);
		}
	}

	else if (m_flag == 3){
		// new_s can be merged with str_prev and  new_e can be merged with end->next
		if (s_flag == 2){
			str_prev->vm_next = new_s->vm_next;
			str_prev->vm_end = new_s->vm_end;
			dealloc_vm_area(new_s);
		}

		if (e_flag == 2){
			new_e->vm_next = (end->vm_next)->vm_next;
			new_e->vm_end = (end->vm_next)->vm_end;
			dealloc_vm_area(end->vm_next);
		}
	}
		


	// merge iteration for normal pages formed from new_s to new_e

	iter = new_s->vm_next;
	iter_prev = new_s;

	struct vm_area *tmp = NULL;


	while(iter != new_e->vm_next){
		if (iter->mapping_type == HUGE_PAGE_MAPPING){
			// printk("Error, still huge page mapping exists in VM area\n");
		}
		if ((iter_prev->vm_end == iter->vm_start) && (iter_prev->access_flags == iter->access_flags)){
			// merge them
			iter_prev->vm_end = iter->vm_end;
			iter_prev->vm_next = iter->vm_next; 
			tmp = iter->vm_next;
			dealloc_vm_area(iter);
			iter = tmp;
		}

		else{
			iter_prev = iter;
			iter = iter->vm_next;
		}
	}

	return 0;
}
