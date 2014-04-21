#include "kernel.h"
#include "errno.h"
#include "globals.h"

#include "vm/vmmap.h"
#include "vm/shadow.h"
#include "vm/anon.h"

#include "proc/proc.h"

#include "util/debug.h"
#include "util/list.h"
#include "util/string.h"
#include "util/printf.h"

#include "fs/vnode.h"
#include "fs/file.h"
#include "fs/fcntl.h"
#include "fs/vfs_syscall.h"

#include "mm/slab.h"
#include "mm/page.h"
#include "mm/mm.h"
#include "mm/mman.h"
#include "mm/mmobj.h"

#define MIN_PAGENUM ADDR_TO_PN(USER_MEM_LOW) /* inclusive */
#define MAX_PAGENUM ADDR_TO_PN(USER_MEM_HIGH) /* exclusive */

static slab_allocator_t *vmmap_allocator;
static slab_allocator_t *vmarea_allocator;

void
vmmap_init(void)
{
        vmmap_allocator = slab_allocator_create("vmmap", sizeof(vmmap_t));
        KASSERT(NULL != vmmap_allocator && "failed to create vmmap allocator!");
        vmarea_allocator = slab_allocator_create("vmarea", sizeof(vmarea_t));
        KASSERT(NULL != vmarea_allocator && "failed to create vmarea allocator!");
}

vmarea_t *
vmarea_alloc(void)
{
        vmarea_t *newvma = (vmarea_t *) slab_obj_alloc(vmarea_allocator);
        if (newvma) {
                newvma->vma_vmmap = NULL;
        }
        return newvma;
}

void
vmarea_free(vmarea_t *vma)
{
        KASSERT(NULL != vma);
        slab_obj_free(vmarea_allocator, vma);
}

/* Create a new vmmap, which has no vmareas and does
 * not refer to a process. */
vmmap_t *
vmmap_create(void)
{
    vmmap_t *vmm = (vmmap_t *) slab_obj_alloc(vmmap_allocator);

    if (vmm == NULL){
        return NULL;
    }

    list_init(&vmm->vmm_list);
    vmm->vmm_proc = NULL;
    return vmm;
}

void vmarea_cleanup(vmarea_t *vma){
    vma->vma_obj->mmo_ops->put(vma->vma_obj);
    list_remove(&vma->vma_plink);

    if (vma->vma_olink.l_next != NULL || vma->vma_olink.l_prev != NULL){
        list_remove(&vma->vma_olink);
    }
    vmarea_free(vma);
}

/* Removes all vmareas from the address space and frees the
 * vmmap struct. */
void
vmmap_destroy(vmmap_t *map)
{
    vmarea_t *curr;
    list_iterate_begin(&map->vmm_list, curr, vmarea_t, vma_plink){
        vmarea_cleanup(curr);
    } list_iterate_end();

    slab_obj_free(vmmap_allocator, map);
}

/* Add a vmarea to an address space. Assumes (i.e. asserts to some extent)
 * the vmarea is valid.  This involves finding where to put it in the list
 * of VM areas, and adding it. Don't forget to set the vma_vmmap for the
 * area. */
void
vmmap_insert(vmmap_t *map, vmarea_t *newvma)
{
    KASSERT(newvma->vma_start < newvma->vma_end && "bad vmarea bounds");
    KASSERT(newvma->vma_prot == PROT_NONE
         || newvma->vma_prot == PROT_READ
         || newvma->vma_prot == PROT_WRITE
         || newvma->vma_prot == PROT_EXEC
         || newvma->vma_prot == (PROT_READ | PROT_WRITE)
         || newvma->vma_prot == (PROT_READ | PROT_EXEC)
         || newvma->vma_prot == (PROT_WRITE | PROT_EXEC)
         || newvma->vma_prot == (PROT_READ | PROT_WRITE | PROT_EXEC));

    int map_type = newvma->vma_flags & MAP_TYPE;
    KASSERT(map_type == MAP_SHARED || map_type == MAP_PRIVATE);
    
    KASSERT(newvma->vma_vmmap == NULL);
    KASSERT(!(list_link_is_linked(&newvma->vma_plink)));

    list_t *list = &map->vmm_list;
    list_link_t *link = list->l_next;
    for (link = list->l_next; link != list; link = link->l_next){
        vmarea_t *vma = list_item(link, vmarea_t, vma_plink);
        if (vma->vma_end >= newvma->vma_end){
            list_insert_before(link, &newvma->vma_plink);
            return;
        }
    }

    /* if we get here, it goes at the end */
    list_insert_tail(list, &newvma->vma_plink);

    newvma->vma_vmmap = map;
}

static int has_gap(vmarea_t *prev, vmarea_t *curr, uint32_t npages){
    return (prev != NULL && curr != NULL &&
            curr->vma_start - prev->vma_end >= npages);
}

/* Find a contiguous range of free virtual pages of length npages in
 * the given address space. Returns starting vfn for the range,
 * without altering the map. Returns -1 if no such range exists.
 *
 * Your algorithm should be first fit. If dir is VMMAP_DIR_HILO, you
 * should find a gap as high in the address space as possible; if dir
 * is VMMAP_DIR_LOHI, the gap should be as low as possible. */
int
vmmap_find_range(vmmap_t *map, uint32_t npages, int dir)
{
    panic("make sure this isn't completely backwards");
    KASSERT(map != NULL);
    KASSERT(dir == VMMAP_DIR_LOHI || dir == VMMAP_DIR_HILO);

    if (npages > MAX_PAGENUM - MIN_PAGENUM){
        dbg(DBG_VM, "npages (%d) is too large to fit in address space\n", npages);
        return -1;
    }

    list_t *vmm_list = &map->vmm_list;

    if (dir == VMMAP_DIR_LOHI){
        if (list_empty(vmm_list)){
            return MIN_PAGENUM;
        }

        list_link_t *curr = NULL;
        list_link_t *next = vmm_list->l_next;

        vmarea_t *curr_vma = NULL;
        vmarea_t *next_vma = NULL;

        while (next != vmm_list){
            curr_vma = curr ? list_item(curr, vmarea_t, vma_plink) : NULL;
            next_vma = list_item(next, vmarea_t, vma_plink);
            if (has_gap(curr_vma, next_vma, npages)){
                return curr_vma->vma_end;
            }

            curr = next;
            next = next->l_next;
        }

        if (MAX_PAGENUM - curr_vma->vma_end >= npages){
            return curr_vma->vma_end;
        }
    } else {
        if (list_empty(vmm_list)){
            return MAX_PAGENUM - npages;
        }

        list_link_t *curr = NULL;
        list_link_t *prev = vmm_list->l_prev;

        vmarea_t *prev_vma = NULL;
        vmarea_t *curr_vma = NULL;

        while (curr != vmm_list){
            curr_vma = curr ? list_item(curr, vmarea_t, vma_plink) : NULL;
            prev_vma = list_item(prev, vmarea_t, vma_plink);

            if (has_gap(prev_vma, curr_vma, npages)){
                return curr_vma->vma_start - npages;
            }

            curr = prev;
            prev = prev->l_prev;
        }

        if (curr_vma->vma_start - MIN_PAGENUM >= npages){
            return curr_vma->vma_start - npages;
        }
    }

    return -1;
}

/* Find the vm_area that vfn lies in. Simply scan the address space
 * looking for a vma whose range covers vfn. If the page is unmapped,
 * return NULL. */
vmarea_t *
vmmap_lookup(vmmap_t *map, uint32_t vfn)
{
    vmarea_t *curr;
    list_iterate_begin(&map->vmm_list, curr, vmarea_t, vma_plink){
        if (curr->vma_start <= vfn && curr->vma_end > vfn){
            return curr;
        }
    } list_iterate_end();
    
    return NULL;
}

/* Allocates a new vmmap containing a new vmarea for each area in the
 * given map. The areas should have no mmobjs set yet. Returns pointer
 * to the new vmmap on success, NULL on failure. This function is
 * called when implementing fork(2). */
vmmap_t *
vmmap_clone(vmmap_t *map)
{
        NOT_YET_IMPLEMENTED("VM: vmmap_clone");
        return NULL;
}

static void assert_valid_mmap_input(vmmap_t *map, int lopage, int prot, int flags,
        off_t off, int dir)
{
    KASSERT(map != NULL);
    KASSERT(prot == PROT_NONE
         || prot == PROT_READ
         || prot == PROT_WRITE
         || prot == PROT_EXEC
         || prot == (PROT_READ | PROT_WRITE)
         || prot == (PROT_READ | PROT_EXEC)
         || prot == (PROT_WRITE | PROT_EXEC)
         || prot == (PROT_READ | PROT_WRITE | PROT_EXEC));

    int map_type = flags & MAP_TYPE;
    KASSERT(map_type == MAP_SHARED || map_type == MAP_PRIVATE);

    KASSERT(((flags & MAP_FIXED) || (flags & MAP_ANON)) &&
            !(flags & MAP_FIXED && (flags & MAP_ANON)));

    KASSERT(off % PAGE_SIZE == 0);

    if (lopage == 0){
        KASSERT(dir == VMMAP_DIR_LOHI || dir == VMMAP_DIR_HILO);
    }

}

/* Insert a mapping into the map starting at lopage for npages pages.
 * If lopage is zero, we will find a range of virtual addresses in the
 * process that is big enough, by using vmmap_find_range with the same
 * dir argument.  If lopage is non-zero and the specified region
 * contains another mapping that mapping should be unmapped.
 *
 * If file is NULL an anon mmobj will be used to create a mapping
 * of 0's.  If file is non-null that vnode's file will be mapped in
 * for the given range.  Use the vnode's mmap operation to get the
 * mmobj for the file; do not assume it is file->vn_obj. Make sure all
 * of the area's fields except for vma_obj have been set before
 * calling mmap.
 *
 * If MAP_PRIVATE is specified set up a shadow object for the mmobj.
 *
 * All of the input to this function should be valid (KASSERT!).
 * See mmap(2) for for description of legal input.
 * Note that off should be page aligned.
 *
 * Be very careful about the order operations are performed in here. Some
 * operation are impossible to undo and should be saved until there
 * is no chance of failure.
 *
 * If 'new' is non-NULL a pointer to the new vmarea_t should be stored in it.
 */
int
vmmap_map(vmmap_t *map, vnode_t *file, uint32_t lopage, uint32_t npages,
          int prot, int flags, off_t off, int dir, vmarea_t **new)
{
    assert_valid_mmap_input(map, lopage, prot, flags, off, dir);

    vmarea_t *vma = vmarea_alloc();

    if (vma == NULL){
        return -ENOMEM;
    }

    int starting_page = lopage;

    if (lopage == 0){
        starting_page = vmmap_find_range(map, npages, dir);
        if (starting_page < 0){
            vmarea_free(vma);
            return starting_page;
        }
    }

    vma->vma_start = starting_page;
    vma->vma_end = starting_page + npages;
    vma->vma_off = ADDR_TO_PN(off);

    vma->vma_prot = prot;
    vma->vma_flags = flags;

    /*vma->vma_vmmap = map;*/
    list_link_init(&vma->vma_plink);
    list_link_init(&vma->vma_olink);
    
    mmobj_t *new_mmobj;

    if (file != NULL){
        int mmap_res = file->vn_ops->mmap(file, vma, &new_mmobj);

        if (mmap_res < 0){
            vmarea_free(vma);
            return mmap_res;
        }
    } else {
        new_mmobj = anon_create();
        if (new_mmobj == NULL){
            vmarea_free(vma);
            return -ENOSPC;
        }
    }

    int remove_res = vmmap_remove(map, starting_page, npages);

    if (remove_res < 0){
        vmarea_free(vma);
        return remove_res;
    }

    if (flags & MAP_PRIVATE){
        mmobj_t *shadow_obj = shadow_create();

        if (shadow_obj == NULL){
            vmarea_free(vma);
            return -ENOSPC;
        }

        shadow_obj->mmo_shadowed = new_mmobj;
        new_mmobj->mmo_ops->ref(new_mmobj);

        mmobj_t *bottom_obj;

        if (new_mmobj->mmo_shadowed != NULL){
            bottom_obj = new_mmobj->mmo_un.mmo_bottom_obj;
        } else {
            bottom_obj = new_mmobj;
        }

        shadow_obj->mmo_un.mmo_bottom_obj = bottom_obj;
        bottom_obj->mmo_ops->ref(bottom_obj);
        
        new_mmobj = shadow_obj;

        list_insert_tail(&bottom_obj->mmo_un.mmo_vmas, &vma->vma_olink);
    }

    vma->vma_obj = new_mmobj;
    new_mmobj->mmo_ops->ref(new_mmobj);

    vmmap_insert(map, vma);

    if (new != NULL){
        *new = vma;
    }

    return 0;
}

typedef enum {NO_OVERLAP, CASE_1, CASE_2, CASE_3, CASE_4} overlap_t;

static overlap_t get_overlap_type(vmarea_t *vma, uint32_t lopage, uint32_t npages){

    uint32_t vma_start = vma->vma_start;
    uint32_t vma_end = vma->vma_end;

    /* non-inclusive */
    uint32_t hipage = lopage + npages;

    if (vma_end <= lopage || vma_start >= hipage){
        return NO_OVERLAP;
    }

    if (vma_start < lopage && vma_end > hipage){
        return CASE_1;
    } else if (vma_start < lopage && vma_end > lopage && vma_end <= hipage){
        return CASE_2;
    } else if (vma_start >= lopage  && vma_start < hipage && vma_end > hipage){
        return CASE_3;
    } else {
        KASSERT(vma_start >= lopage && vma_end <= hipage);
        return CASE_4;
    }
}

static vmarea_t *vmarea_clone(vmarea_t *old_vma){
    vmarea_t *new_vma = vmarea_alloc();

    if (new_vma == NULL){
        return NULL;
    }

    new_vma->vma_start = -1;
    new_vma->vma_end = -1;
    new_vma->vma_off = old_vma->vma_off;

    new_vma->vma_prot = old_vma->vma_prot;
    new_vma->vma_flags = old_vma->vma_flags;

    new_vma->vma_obj = old_vma->vma_obj;

    if (new_vma->vma_obj != NULL){
        new_vma->vma_obj->mmo_ops->ref(new_vma->vma_obj);
    }

    list_link_init(&new_vma->vma_plink);
    list_link_init(&new_vma->vma_olink);
    list_insert_before(&old_vma->vma_olink, &new_vma->vma_olink);

    return new_vma;
}

/*
 * We have no guarantee that the region of the address space being
 * unmapped will play nicely with our list of vmareas.
 *
 * You must iterate over each vmarea that is partially or wholly covered
 * by the address range [addr ... addr+len). The vm-area will fall into one
 * of four cases, as illustrated below:
 *
 * key:
 *          [             ]   Existing VM Area
 *        *******             Region to be unmapped
 *
 * Case 1:  [   ******    ]
 * The region to be unmapped lies completely inside the vmarea. We need to
 * split the old vmarea into two vmareas. be sure to increment the
 * reference count to the file associated with the vmarea.
 *
 * Case 2:  [      *******]**
 * The region overlaps the end of the vmarea. Just shorten the length of
 * the mapping.
 *
 * Case 3: *[*****        ]
 * The region overlaps the beginning of the vmarea. Move the beginning of
 * the mapping (remember to update vma_off), and shorten its length.
 *
 * Case 4: *[*************]**
 * The region completely contains the vmarea. Remove the vmarea from the
 * list.
 */
int
vmmap_remove(vmmap_t *map, uint32_t lopage, uint32_t npages)
{
    list_t *list = &map->vmm_list;
    list_link_t *currlink = list->l_next;

    while (currlink != list){
        list_link_t *nextlink = currlink->l_next;
        vmarea_t *vma = list_item(currlink, vmarea_t, vma_plink);

        overlap_t overlap = get_overlap_type(vma, lopage, npages);

        switch (overlap){
            case NO_OVERLAP:
                if (vma->vma_start > lopage + npages){
                    return 0;
                }
                break;
            case CASE_1:; /* empty statement so this compiles */
                vmarea_t *next_vma = vmarea_clone(vma);
                if (next_vma == NULL){
                    return -ENOMEM;
                }
                
                next_vma->vma_start = lopage + npages;
                next_vma->vma_end = vma->vma_end;
                next_vma->vma_off = vma->vma_off + (lopage + npages - vma->vma_start);

                vma->vma_end = lopage;

                vmmap_insert(map, next_vma);
                break;
            case CASE_2:
                vma->vma_end = lopage;
                break;
            case CASE_3:
                vma->vma_off += (lopage + npages - vma->vma_start);
                vma->vma_start = lopage + npages;
                break; 
            case CASE_4:; 
                vmarea_t *vma = list_item(currlink, vmarea_t, vma_plink);
                vmarea_cleanup(vma);

                /*list_remove(currlink);*/
        }
        currlink = nextlink;
    }
    
    return 0;
}

/*
 * Returns 1 if the given address space has no mappings for the
 * given range, 0 otherwise.
 */
int
vmmap_is_range_empty(vmmap_t *map, uint32_t startvfn, uint32_t npages)
{
    KASSERT(map != NULL);

    uint32_t endvfn = startvfn + npages;

    vmarea_t *curr;

    list_iterate_begin(&map->vmm_list, curr, vmarea_t, vma_plink){
        if ((startvfn >= curr->vma_start && startvfn < curr->vma_end) ||
            (endvfn >= curr->vma_start && endvfn < curr->vma_end) ||
            (startvfn < curr->vma_start && endvfn >= curr->vma_end))
        {
            return 0;
        }
    } list_iterate_end();

    return 1;
}

uint32_t min(uint32_t a, uint32_t b){
    return a < b ? a : b;
}

/* Read into 'buf' from the virtual address space of 'map' starting at
 * 'vaddr' for size 'count'. To do so, you will want to find the vmareas
 * to read from, then find the pframes within those vmareas corresponding
 * to the virtual addresses you want to read, and then read from the
 * physical memory that pframe points to. You should not check permissions
 * of the areas. Assume (KASSERT) that all the areas you are accessing exist.
 * Returns 0 on success, -errno on error.
 */
int
vmmap_read(vmmap_t *map, const void *vaddr, void *buf, size_t count)
{
    uint32_t destpos = 0;
    const void *curraddr = vaddr;   

    while (destpos < count){
        uint32_t currvfn = ADDR_TO_PN(curraddr);

        vmarea_t *vma = vmmap_lookup(map, currvfn);
        KASSERT(vma != NULL);

        off_t off = vma->vma_off + (currvfn - vma->vma_start);

        uint32_t pages_to_read = min(((count - destpos)/PAGE_SIZE) + 1,
                vma->vma_end - currvfn);

        uint32_t i;
        for (i = 0; i < pages_to_read; i++){
            pframe_t *p;
            int get_res = pframe_get(vma->vma_obj, off + i, &p);

            if (get_res < 0){
                return get_res;
            }

            int data_offset = (int) curraddr % PAGE_SIZE;

            int read_size = min(PAGE_SIZE - data_offset, count - destpos);

            memcpy((char *) buf + destpos, (char *) p->pf_addr + data_offset, read_size);

            destpos += read_size;
            curraddr = (char *) curraddr + read_size;
        }
    }

    return 0;
}

/* Write from 'buf' into the virtual address space of 'map' starting at
 * 'vaddr' for size 'count'. To do this, you will need to find the correct
 * vmareas to write into, then find the correct pframes within those vmareas,
 * and finally write into the physical addresses that those pframes correspond
 * to. You should not check permissions of the areas you use. Assume (KASSERT)
 * that all the areas you are accessing exist. Remember to dirty pages!
 * Returns 0 on success, -errno on error.
 */
int
vmmap_write(vmmap_t *map, void *vaddr, const void *buf, size_t count)
{
    uint32_t srcpos = 0;
    const void *curraddr = vaddr;   

    while (srcpos < count){
        uint32_t currvfn = ADDR_TO_PN(curraddr);

        vmarea_t *vma = vmmap_lookup(map, currvfn);
        KASSERT(vma != NULL);

        off_t off = vma->vma_off + (currvfn - vma->vma_start);

        uint32_t pages_to_read = min(((count - srcpos)/PAGE_SIZE) + 1,
                vma->vma_end - currvfn);

        uint32_t i;
        for (i = 0; i < pages_to_read; i++){
            pframe_t *p;
            int get_res = pframe_get(vma->vma_obj, off + i, &p);

            if (get_res < 0){
                return get_res;
            }

            int data_offset = (int) curraddr % PAGE_SIZE;

            int write_size = min(PAGE_SIZE - data_offset, count - srcpos);

            memcpy((char *) p->pf_addr + data_offset, (char *) buf + srcpos, write_size); 

            int dirty_res = pframe_dirty(p);

            if (dirty_res < 0){
                return dirty_res;
            }

            srcpos += write_size;
            curraddr = (char *) curraddr + write_size;
        }
    }

    return 0;
}

/* a debugging routine: dumps the mappings of the given address space. */
size_t
vmmap_mapping_info(const void *vmmap, char *buf, size_t osize)
{
        KASSERT(0 < osize);
        KASSERT(NULL != buf);
        KASSERT(NULL != vmmap);

        vmmap_t *map = (vmmap_t *)vmmap;
        vmarea_t *vma;
        ssize_t size = (ssize_t)osize;

        int len = snprintf(buf, size, "%21s %5s %7s %8s %10s %12s\n",
                           "VADDR RANGE", "PROT", "FLAGS", "MMOBJ", "OFFSET",
                           "VFN RANGE");

        list_iterate_begin(&map->vmm_list, vma, vmarea_t, vma_plink) {
                size -= len;
                buf += len;
                if (0 >= size) {
                        goto end;
                }

                len = snprintf(buf, size,
                               "%#.8x-%#.8x  %c%c%c  %7s 0x%p %#.5x %#.5x-%#.5x\n",
                               vma->vma_start << PAGE_SHIFT,
                               vma->vma_end << PAGE_SHIFT,
                               (vma->vma_prot & PROT_READ ? 'r' : '-'),
                               (vma->vma_prot & PROT_WRITE ? 'w' : '-'),
                               (vma->vma_prot & PROT_EXEC ? 'x' : '-'),
                               (vma->vma_flags & MAP_SHARED ? " SHARED" : "PRIVATE"),
                               vma->vma_obj, vma->vma_off, vma->vma_start, vma->vma_end);
        } list_iterate_end();

end:
        if (size <= 0) {
                size = osize;
                buf[osize - 1] = '\0';
        }
        /*
        KASSERT(0 <= size);
        if (0 == size) {
                size++;
                buf--;
                buf[0] = '\0';
        }
        */
        return osize - size;
}
