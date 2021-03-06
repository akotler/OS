/*

 * Copyright (c) 2000, 2001, 2002, 2003, 2004, 2005, 2008, 2009
 *	The President and Fellows of Harvard College.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE UNIVERSITY AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE UNIVERSITY OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <addrspace.h>
#include <vm.h>
#include <proc.h>
#include <pagetable.h>
#include <machine/tlb.h>
#include <spl.h>

/********* Region_table functions ********/

int push_region(struct region **region_table, vaddr_t vaddr, vaddr_t vaddr_end, int npages){
	/* Need to do a check on the head to see if NULL */
	struct region *new_region;
	new_region = kmalloc(sizeof(*new_region));

	if(new_region == NULL){
		kprintf("No mem for new region");
		return ENOSYS;
	}
	
	new_region->as_vbase = vaddr;
	new_region->as_vend = vaddr_end;
	new_region->region_pages = npages;
		
	new_region->next = *region_table;
	*region_table = new_region;

	return 0;
}

struct region * pop_region(struct region **head){
	struct region *temp = NULL;
	if(*head == NULL){
		kprintf("Region_table is NULL");	
	}
	temp = (*head)->next;
	free_kpages((*head)->as_vbase);
	free_upages((*head)->as_pbase);
	kfree(*head);

	*head = temp;
	return *head;
}
/********* Region_table functions ********/


/*
 * Note! If OPT_DUMBVM is set, as is the case until you start the VM
 * assignment, this file is not compiled or linked or in any way
 * used. The cheesy hack versions in dumbvm.c are used instead.
 */

struct addrspace *
as_create(void)
{
	struct addrspace *as;

	as = kmalloc(sizeof(struct addrspace));
	if (as == NULL) {
		kprintf("Code or text region failed, ran out mem");
		return NULL;
	}

	/*
	 * Initialize as needed.
	 */

	as->region_table = kmalloc(sizeof(struct region));
	if (as->region_table == NULL) {
		kprintf("Code or text region failed, ran out mem");
		return NULL;
	}

	as->region_table->as_vbase = 0;
	as->region_table->as_vend = 0;
	as->region_table->as_pbase = 0;
	as->region_table->region_pages = 0;
	as->region_table->next = NULL;
	
	/* Should we allocate space for stack & heap in here?
	 * or in as_define_stack (stack) and as_prepare_load (heap) it?
	 */
	as->stack_region = kmalloc(sizeof(struct region));
	if (as->stack_region== NULL) {
		kprintf("Stack region failed, ran out mem");
		return NULL;
	}

	as->stack_region->as_vbase = USERSTACK- (1024 *PAGE_SIZE);
	as->stack_region->as_vend = USERSTACK;	
	as->stack_region->as_pbase = 0;
	as->stack_region->region_pages = 1024;
	as->stack_region->next = NULL;

	
	as->heap_region = kmalloc(sizeof(struct region));
	if (as->heap_region == NULL) {
		kprintf("Heap region failed, ran out mem");
		return NULL;
	}
	
	as->heap_region->as_vbase = 0;
	as->heap_region->as_vend = 0;
	as->heap_region->as_pbase = 0;
	as->heap_region->region_pages = 0;
	as->heap_region->next = NULL;

	/* Prob in vm_fault*/
	as->page_table = kmalloc(sizeof(struct page_entry));
	if (as->page_table == NULL) {
		return NULL;
	}
	
	as->page_table->vpn = 0;
	as->page_table->pas = 0;
	as->page_table->next = NULL;

	return as;
}

int
as_copy(struct addrspace *old, struct addrspace **ret)
{
	struct addrspace *newas;
	struct region *temp;
	struct page_entry *temp_pte;

	int err = 0;
	newas = as_create();
	if (newas==NULL) {
		return ENOMEM;
	}

	/*
	 *  Start copying old addrspace into newas addrspace.
	 */

	//Copy region_table
	temp = old->region_table;
	while(temp != NULL){
		err = push_region(&(newas->region_table),temp->as_vbase,temp->as_vend,temp->region_pages);
		if(err){
			return err;
		}

	
		temp = temp->next;
	}
	
	//Copy heap region
	temp = old->heap_region;
	while(temp != NULL){
		err = push_region(&(newas->heap_region),temp->as_vbase,temp->as_vend,temp->region_pages);
		if(err){
			return err;
		}
		temp = temp->next;
	}

	//Copy stack region
	temp = old->stack_region;
	while(temp != NULL){
		err = push_region(&(newas->stack_region),temp->as_vbase,temp->as_vend,temp->region_pages);
		if(err){
			return err;
		}

		
		temp = temp->next;
	
	}

	//Copy page_table
	temp_pte = old->page_table;	
	while(temp_pte != NULL){
		err = push_pte(&(newas->page_table),temp_pte->vpn);
		if(err){
			return err;
		}
		//bzero((void*)PADDR_TO_KVADDR(newas->page_table->pas),PAGE_SIZE);
	
		memmove((void*)PADDR_TO_KVADDR(newas->page_table->pas), (const void*)PADDR_TO_KVADDR(temp_pte->pas),PAGE_SIZE);
		temp_pte = temp_pte->next;
	
	}
	*ret = newas;
	
	return 0;
}

void
as_destroy(struct addrspace *as)
{
	/*
	 * Clean up as needed.
	 */
	
	while(as->region_table != NULL){
		as->region_table = pop_region(&as->region_table);
	}
	
	kfree(as->region_table); //head

	while(as->stack_region != NULL){
		as->stack_region = pop_region(&as->stack_region);	
	}
	
	kfree(as->stack_region); //head
	
	while(as->heap_region != NULL){
		as->heap_region = pop_region(&as->heap_region);
	}
		
	kfree(as->heap_region);
	
	struct page_entry * head = destroy_pagetable(as->page_table);
	kfree(head);
	kfree(as);
}

void
as_activate(void)
{
	int i, spl;
	struct addrspace *as;

	as = proc_getas();
	if (as == NULL) {
		return;
	}

	/**** Disable interrupts on this CPU while frobbing the TLB. ****/
	spl = splhigh();

	for (i=0; i<NUM_TLB; i++) {
		tlb_write(TLBHI_INVALID(i), TLBLO_INVALID(), i);
	}

	splx(spl);
}

void
as_deactivate(void)
{
	/*
	 * Write this. For many designs it won't need to actually do
	 * anything. See proc.c for an explanation of why it (might)
	 * be needed.
	 */
}

/*
 * segment in memory extends from VADDR up to (but not including)
 * VADDR+MEMSIZE.
 *
 * The READABLE, WRITEABLE, and EXECUTABLE flags are set if read,
 * write, or execute permission should be set on the segment. At the
 * moment, these are ignored. When you write the VM system, you may
 * want to implement them.
 */
int
as_define_region(struct addrspace *as, vaddr_t vaddr, size_t memsize,
		 int readable, int writeable, int executable)
{
	/*
	 * Write this.
	 */
	
	/* Taken from DUMBVM */
	size_t npages = 0;
	(void)readable;
	(void)writeable;
	(void)executable;

	
	/* Align the region. First, the base... */
	memsize += vaddr & ~(vaddr_t)PAGE_FRAME;
/*	vaddr &= PAGE_FRAME;*/

	/* ...and now the length. */
	memsize = (memsize + PAGE_SIZE - 1) & PAGE_FRAME;

	npages = memsize / PAGE_SIZE;
	
	/* Put it altogether now */
	
	vaddr_t vaddr_end = vaddr + (npages*PAGE_SIZE);
	if(as->region_table->as_vbase== 0){
		as->region_table->as_vbase = vaddr;
		as->region_table->as_vend = vaddr_end;
		as->region_table->region_pages = npages;
	}else{
		int err = 0;
	err = push_region(&(as->region_table), vaddr, vaddr_end, npages);
		if(err){
			return err;
		}
	}

	if(as->heap_region->as_vbase < as->region_table->as_vend){
		as->heap_region->as_vbase = as->region_table->as_vend;
		as->heap_region->as_vend = as->region_table->as_vend;
		return 0;
	}

	return ENOSYS;
}

int
as_prepare_load(struct addrspace *as)
{
	/*
	 * Write this.
	 *
	int npages = as->region_table->region_pages;
	vaddr_t va = as->region_table->as_vbase;
	//where the heap begins
	as->heap_region->as_pbase = va + (PAGE_SIZE * npages);
	
	struct region *temp = as->region_table;
	while(temp != NULL){
		temp->as_pbase = get_ppages(temp->region_pages);
		if(temp->as_pbase == 0){
			return ENOMEM;
		}
		temp = temp->next;
	}*/
	(void)as;
	return 0;
}

int
as_complete_load(struct addrspace *as)
{
	/*
	 * Write this.
	 */

	(void)as;
	return 0;
}

int
as_define_stack(struct addrspace *as, vaddr_t *stackptr)
{
	/*
	 * Write this.
	 */
	
/*	KASSERT(as->stack_region->as_pbase != 0);*/

	(void)as;

	/* Initial user-level stack pointer */
	*stackptr = USERSTACK;

	return 0;
}
