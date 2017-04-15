#include <types.h>
#include <lib.h>
#include <vm.h>
#include <spinlock.h>
#include <mainbus.h>
#include <pagetable.h>
#include <proc.h>
#include <kern/errno.h>
#include <current.h>
#include <addrspace.h>

struct coremap_entry *coremap;
int NUM_ENTRIES;
struct spinlock coremap_spinlock = SPINLOCK_INITIALIZER;
int bytes_left;

/* VM functions */

void vm_bootstrap()
{
    /* implement*/
}

/* We need to make alloc_upages and free_upages functions */

/* Allocate/free some kernel-space virtual pages */
paddr_t
get_ppages(unsigned npages){
	
	paddr_t pa;
	// Critical section. Protect the coremap
	spinlock_acquire(&coremap_spinlock);
	
	if((int)(npages*PAGE_SIZE) > bytes_left || npages==0){
        	pa = 0;
		spinlock_release(&coremap_spinlock);
        	return pa;
	}
	for(int i=0; i<NUM_ENTRIES; i++) {
		if(coremap[i].pg_state == PAGE_FREE && coremap[i].use_state == REUSE){
		unsigned cont;
		for(cont = 0; cont<npages; cont++){
			if(coremap[i+cont].pg_state != PAGE_FREE || coremap[i+cont].use_state != REUSE){
			i+=cont;
			break;
			}
		}
		if(cont==npages){
			pa = coremap[i].pas;
			for(unsigned n=0; n<npages; n++){
				if(n==0){
					coremap[i+n].blk_state = BLOCK_PARENT;
				}
				else{
					coremap[i+n].blk_state = BLOCK_CHILD;
				}
				coremap[i+n].block_size = npages;
				coremap[i+n].pg_state = PAGE_FIXED;
				coremap[i+n].use_state = REUSE;
				}
			// Update bytes_left
			bytes_left -= (npages*PAGE_SIZE);
			spinlock_release(&coremap_spinlock);
			return pa;
			}
		}
	}
	pa = 0;
	spinlock_release(&coremap_spinlock);
	return pa;

}

vaddr_t
alloc_kpages(unsigned npages)
{
	paddr_t pa;
	pa = get_ppages(npages);
	if(pa == 0){
		return pa;
	}
	return PADDR_TO_KVADDR(pa);
}

void
free_kpages(vaddr_t addr)
{

    // Critical section. Protect the coremap
    spinlock_acquire(&coremap_spinlock);
   
    //NEW STUFF
    int i = 0;
    for(; i<NUM_ENTRIES; i++){
        if(addr == coremap[i].vas){
            if(coremap[i].blk_state==BLOCK_CHILD || coremap[i].use_state==NO_REUSE){
                break; //Fail
            }
            int n = coremap[i].block_size;
            int npages = coremap[i].block_size;
            while(n>0){
                coremap[i].as = NULL;
                coremap[i].block_size = 0;
                coremap[i].pg_state = PAGE_FREE;
                coremap[i].blk_state = BLOCK_CHILD;
                n--;
                i++;
            }
            if(n==0){
                bytes_left += (npages*PAGE_SIZE);
            }
        }
    }

    spinlock_release(&coremap_spinlock);
	(void)addr;
}

/* I believe these will access the page table instead of the coremap
vaddr_t
alloc_upages(unsigned npages)
{

    vaddr_t va; // What we're returning

    // Theres not enough free pages to allocate
    if((int)(npages*PAGE_SIZE) > bytes_left || npages==0){
        va = 0;
        return va;
    }
	
	//loop through page_table and find free pages 
	struct pagetable_node *iterator;
	iterator = head;
	while(iterator != NULL){
		if(iterator->page_entry.state == MEM){
			// a little lost here
			va = iterator->page_entry->vpn + pas (offset)? 
		}
	}

	for(int i=0; i<NUM_ENTRIES; i++) {
        if(coremap[i].pg_state == PAGE_FREE && coremap[i].use_state == REUSE){
            unsigned cont = 0;
            for(; cont<npages; cont++){
                if(coremap[i+cont].pg_state != PAGE_FREE || coremap[i+cont].use_state != REUSE){
                    i+=cont;
                    break;
                }
            }
            if(cont==npages){
                va = coremap[i].vas;
                for(unsigned n=0; n<npages; n++){
                    if(n==0){
                        coremap[i+n].blk_state = BLOCK_PARENT;
                    }
                    else{
                        coremap[i+n].blk_state = BLOCK_CHILD;
                    }
                    coremap[i+n].block_size = npages;
                    coremap[i+n].pg_state = PAGE_FIXED;
                    coremap[i+n].use_state = REUSE;
                }
                // Update bytes_left
                bytes_left -= (npages*PAGE_SIZE);
                spinlock_release(&coremap_spinlock);
                return va;
            }
        }
    }
    va = 0;
    spinlock_release(&coremap_spinlock);
    return va;
}*/

/*
void
free_upages(vaddr_t addr)
{

    // Critical section. Protect the coremap
    spinlock_acquire(&coremap_spinlock);
   
    //NEW STUFF
    int i = 0;
    for(; i<NUM_ENTRIES; i++){
        if(addr == coremap[i].vas){
            if(coremap[i].blk_state==BLOCK_CHILD || coremap[i].use_state==NO_REUSE){
                break; //Fail
            }
            int n = coremap[i].block_size;
            int npages = coremap[i].block_size;
            while(n>0){
                coremap[i].as = NULL;
                coremap[i].block_size = 0;
                coremap[i].pg_state = PAGE_FREE;
                coremap[i].blk_state = BLOCK_CHILD;
                n--;
                i++;
            }
            if(n==0){
                bytes_left += (npages*PAGE_SIZE);
            }
        }
    }

    spinlock_release(&coremap_spinlock);
	(void)addr;
}
*/

unsigned
int
coremap_used_bytes() {
    size_t ramsize;
    ramsize = mainbus_ramsize();
    return ramsize - bytes_left;
}

void
vm_tlbshootdown(const struct tlbshootdown *ts)
{
	(void)ts;
	/* Implement */
}

/* Acts as a boolean */
bool region_check(vaddr_t faultaddress, struct region *region){
	struct region *temp;
	temp = region;
	while(temp != NULL){
		/* is the address within range */
		if(faultaddress >= temp->as_vbase && faultaddress <= temp->as_vend){
			return true;
		}
		temp = temp->next;
	}
	temp = NULL;
	return false;
}


int valid_address(vaddr_t faultaddress, struct addrspace *as){
	/* Checks it falls in code and text addr */
	bool valid = false;
	valid = region_check(faultaddress, as->region_table);
	if(!valid){
		/* If not in code or text check the stack */
		valid = region_check(faultaddress, as->stack_region);
		if(!valid){
			/* If not in stack, check the heap */
			valid = region_check(faultaddress, as->heap_region);
			/* If it gets here, it's not valid*/
			if(!valid){
				kprintf("Address could not be found in any region");
				return EFAULT;	
			}
			return 0;
		}
		return 0;
	}
	return 0;
}

int
vm_fault(int faulttype, vaddr_t faultaddress){
	(void)faulttype;
	int err;
	
	if (curproc == NULL) {
		/*
		 * No process. This is probably a kernel fault early
		 * in boot. Return EFAULT so as to panic instead of
		 * getting into an infinite faulting loop.
		 */
		return EFAULT;
	}

	struct addrspace *as = proc_getas();
	if (as == NULL) {
		/*
		 * No address space set up. This is probably also a
		 * kernel fault early in boot.
		 */
		return EFAULT;
	}

	err = valid_address(faultaddress, as);
	if(err){
		return err; //check valid_addr return value
	}
	kprintf("It does get here");	
	
	return 0;
}

void 
init_coremap(size_t ramsize, paddr_t firstpaddr){
	bytes_left = ramsize;
	NUM_ENTRIES = ramsize / PAGE_SIZE; // 256 entries - pgs are 4K
	
	//How many pages do our coremap_spinlock & coremap take?
	size_t spinlock_pgs;
	spinlock_pgs = (sizeof(struct spinlock)+PAGE_SIZE-1) / PAGE_SIZE; // Round ^UP^
	size_t coremap_pgs;
	coremap_pgs = ((sizeof(struct coremap_entry)*NUM_ENTRIES)+PAGE_SIZE-1)/ PAGE_SIZE; // Round UP

	// firstpaddr should be moved up beyond coremap & spinlock
	firstpaddr += spinlock_pgs*PAGE_SIZE;
	coremap = (struct coremap_entry *)PADDR_TO_KVADDR(firstpaddr);
    
	// Mem addr for cmap entries (had to move beyond spinlock)
	paddr_t entrypaddr;
	entrypaddr = firstpaddr; // Both beyond kernel & spinlock

	// How many pages are already used?
	firstpaddr += coremap_pgs*PAGE_SIZE;//Next allocatable addr
	int pgs_used;
	pgs_used = firstpaddr/PAGE_SIZE; 

	//  THE pas AND vas MUST BE INCREMENTED BY PAGE_SIZE
	// Initialize all coremap entries
	paddr_t pg_paddr;
	pg_paddr = 0;

	for (int i = 0; i<NUM_ENTRIES; i++){
		coremap[i].entry_pas = entrypaddr;
		coremap[i].as = NULL;
		coremap[i].pas = pg_paddr;
		//kprintf("Coremap[index].pas: %d\n", coremap[i].pas);
		coremap[i].vas = PADDR_TO_KVADDR(pg_paddr);
		coremap[i].block_size = 0;
		coremap[i].pg_state = PAGE_FREE;
		coremap[i].blk_state = BLOCK_CHILD;
		coremap[i].use_state = REUSE;
		//These are pgs already used. Must not be recycled EVER
		if (i<pgs_used){
			coremap[i].pg_state = PAGE_FIXED;
			coremap[i].use_state = NO_REUSE;
		}
		pg_paddr += PAGE_SIZE;
		entrypaddr += sizeof(struct coremap_entry);
	}

	firstpaddr = coremap[pgs_used].pas;

    // Update the amount of available bytes left in coremap
	bytes_left -= firstpaddr;

	spinlock_release(&coremap_spinlock);
}
