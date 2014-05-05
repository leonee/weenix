#include "globals.h"
#include "errno.h"
#include "types.h"

#include "mm/mm.h"
#include "mm/tlb.h"
#include "mm/mman.h"
#include "mm/page.h"

#include "proc/proc.h"

#include "util/string.h"
#include "util/debug.h"

#include "fs/vnode.h"
#include "fs/vfs.h"
#include "fs/file.h"

#include "vm/vmmap.h"
#include "vm/mmap.h"

static int valid_map_type(int flags){
    int map_type = flags & MAP_TYPE;
    return (map_type == MAP_SHARED || map_type == MAP_PRIVATE);
}

static int valid_fd(int fd){
    return (fd >= 0 && fd < NFILES);
}

/*
 * This function implements the mmap(2) syscall, but only
 * supports the MAP_SHARED, MAP_PRIVATE, MAP_FIXED, and
 * MAP_ANON flags.
 *
 * Add a mapping to the current process's address space.
 * You need to do some error checking; see the ERRORS section
 * of the manpage for the problems you should anticipate.
 * After error checking most of the work of this function is
 * done by vmmap_map(), but remember to clear the TLB.
 */
int
do_mmap(void *addr, size_t len, int prot, int flags,
        int fd, off_t off, void **ret)
{
    if (len == 0){
        return -EINVAL;
    }

    if (!valid_map_type(flags)){
        return -EINVAL;
    }

    if (!PAGE_ALIGNED(off)){
        return -EINVAL;
    }

    if (!(flags & MAP_ANON) && (flags & MAP_FIXED) && !PAGE_ALIGNED(addr)){
        return -EINVAL;
    }

    if (addr != NULL && (uint32_t) addr < USER_MEM_LOW){
        return -EINVAL;
    }

    if (addr != NULL && (uint32_t) addr + len > USER_MEM_HIGH){
        return -EINVAL;
    }

    if (addr == 0 && (flags & MAP_FIXED)){
        return -EINVAL;
    }

    vnode_t *vnode;
      
    if (!(flags & MAP_ANON)){
    
        if (!valid_fd(fd) || curproc->p_files[fd] == NULL){
            return -EBADF;
        }

        file_t *f = curproc->p_files[fd];
        vnode = f->f_vnode;

        if ((flags & MAP_PRIVATE) && !(f->f_mode & FMODE_READ)){
            return -EACCES;
        }

        if ((flags & MAP_SHARED) && (prot & PROT_WRITE) &&
                !((f->f_mode & FMODE_READ) && f->f_mode & FMODE_WRITE))
        {
            return -EACCES;
        }

        if ((prot & PROT_WRITE) && !(f->f_mode & FMODE_WRITE)){
            return -EACCES;
        }
    } else {
        vnode = NULL;
    }

    vmarea_t *vma;

    int retval = vmmap_map(curproc->p_vmmap, vnode, ADDR_TO_PN(addr),
            (uint32_t) PAGE_ALIGN_UP(len) / PAGE_SIZE, prot, flags, off,
            VMMAP_DIR_HILO, &vma);

    KASSERT(retval == 0 || retval == -ENOMEM);

    if (ret != NULL && retval >= 0){
        *ret = PN_TO_ADDR(vma->vma_start);
        tlb_flush_range((uintptr_t) PN_TO_ADDR(vma->vma_start),
                (uint32_t) PAGE_ALIGN_UP(len) / PAGE_SIZE);
    }

    return retval;
}


/*
 * This function implements the munmap(2) syscall.
 *
 * As with do_mmap() it should perform the required error checking,
 * before calling upon vmmap_remove() to do most of the work.
 * Remember to clear the TLB.
 */
int
do_munmap(void *addr, size_t len)
{
    if ((uintptr_t) addr < USER_MEM_LOW || USER_MEM_HIGH - (uint32_t) addr < len){
        return -EINVAL;
    }

    if (len == 0){
        return -EINVAL;
    }

    if (!PAGE_ALIGNED(addr)){
        return -EINVAL; 
    }

    int ret = vmmap_remove(curproc->p_vmmap, ADDR_TO_PN(addr),
            (uint32_t) PAGE_ALIGN_UP(len) / PAGE_SIZE);

    tlb_flush_range((uintptr_t) addr,
            (uintptr_t) PAGE_ALIGN_UP(len) / PAGE_SIZE);

    return ret;
}

