#include "globals.h"
#include "errno.h"
#include "util/debug.h"

#include "mm/mm.h"
#include "mm/page.h"
#include "mm/mman.h"

#include "vm/mmap.h"
#include "vm/vmmap.h"

#include "proc/proc.h"

/*
 * This function implements the brk(2) system call.
 *
 * This routine manages the calling process's "break" -- the ending address
 * of the process's "dynamic" region (often also referred to as the "heap").
 * The current value of a process's break is maintained in the 'p_brk' member
 * of the proc_t structure that represents the process in question.
 *
 * The 'p_brk' and 'p_start_brk' members of a proc_t struct are initialized
 * by the loader. 'p_start_brk' is subsequently never modified; it always
 * holds the initial value of the break. Note that the starting break is
 * not necessarily page aligned!
 *
 * 'p_start_brk' is the lower limit of 'p_brk' (that is, setting the break
 * to any value less than 'p_start_brk' should be disallowed).
 *
 * The upper limit of 'p_brk' is defined by the minimum of (1) the
 * starting address of the next occuring mapping or (2) USER_MEM_HIGH.
 * That is, growth of the process break is limited only in that it cannot
 * overlap with/expand into an existing mapping or beyond the region of
 * the address space allocated for use by userland. (note the presence of
 * the 'vmmap_is_range_empty' function).
 *
 * The dynamic region should always be represented by at most ONE vmarea.
 * Note that vmareas only have page granularity, you will need to take this
 * into account when deciding how to set the mappings if p_brk or p_start_brk
 * is not page aligned.
 *
 * You are guaranteed that the process data/bss region is non-empty.
 * That is, if the starting brk is not page-aligned, its page has
 * read/write permissions.
 *
 * If addr is NULL, you should NOT fail as the man page says. Instead,
 * "return" the current break. We use this to implement sbrk(0) without writing
 * a separate syscall. Look in user/libc/syscall.c if you're curious.
 *
 * Also, despite the statement on the manpage, you MUST support combined use
 * of brk and mmap in the same process.
 *
 * Note that this function "returns" the new break through the "ret" argument.
 * Return 0 on success, -errno on failure.
 */
int
do_brk(void *addr, void **ret)
{
    KASSERT(ret != NULL);

    if (addr == NULL || addr == curproc->p_brk){
        *ret = curproc->p_brk;
        return 0;
    }

    uint32_t old_brk = (uint32_t) curproc->p_start_brk;

    if ((uint32_t) addr < old_brk || (uint32_t) addr > USER_MEM_HIGH){
        return -ENOMEM;
    }

    if (addr > curproc->p_brk){

        /* the first page of the new brk area that isn't part of the vma
         * for the brk area already. If brk isn't page aligned, then the page
         * that it lies in is already mapped, so we add one to it's page number
         * to find the first page not included in the brk area's vma
         */
        uint32_t first_new_page = ADDR_TO_PN(old_brk) + !PAGE_ALIGNED(old_brk);

        /* exclusive */
        uint32_t brk_end_page = ADDR_TO_PN(addr) + !PAGE_ALIGNED(addr);

        uint32_t npages = brk_end_page - first_new_page;

        if (!vmmap_is_range_empty(curproc->p_vmmap, first_new_page, npages)){
            return -ENOMEM;
        }

        vmarea_t *vma =
            vmmap_lookup(curproc->p_vmmap, ADDR_TO_PN(curproc->p_start_brk));

        /* try to catch off-by-one errors by making sure the last page in 
         * the new brk area doesn't have a mapping */
        KASSERT(vmmap_is_range_empty(curproc->p_vmmap, brk_end_page - 1, 1));

        vma->vma_end = brk_end_page; 
        curproc->p_brk = addr;
    } else {
        panic("nyi");
    }

    return 0;
}
