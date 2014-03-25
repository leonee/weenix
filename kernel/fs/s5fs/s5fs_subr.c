/*
 *   FILE: s5fs_subr.c
 * AUTHOR: afenn
 *  DESCR:
 *  $Id: s5fs_subr.c,v 1.1.2.1 2006/06/04 01:02:15 afenn Exp $
 */

#include "kernel.h"
#include "util/debug.h"
#include "mm/kmalloc.h"
#include "globals.h"
#include "proc/sched.h"
#include "proc/kmutex.h"
#include "errno.h"
#include "util/string.h"
#include "util/printf.h"
#include "mm/pframe.h"
#include "mm/mmobj.h"
#include "drivers/dev.h"
#include "drivers/blockdev.h"
#include "fs/stat.h"
#include "fs/vfs.h"
#include "fs/vnode.h"
#include "fs/s5fs/s5fs_subr.h"
#include "fs/s5fs/s5fs.h"
#include "mm/mm.h"
#include "mm/page.h"

#define dprintf(...) dbg(DBG_S5FS, __VA_ARGS__)

#define s5_dirty_super(fs)                                           \
        do {                                                         \
                pframe_t *p;                                         \
                int err;                                             \
                pframe_get(S5FS_TO_VMOBJ(fs), S5_SUPER_BLOCK, &p);   \
                KASSERT(p);                                          \
                err = pframe_dirty(p);                               \
                KASSERT(!err                                         \
                        && "shouldn\'t fail for a page belonging "   \
                        "to a block device");                        \
        } while (0)

#define NDIRENTS 10

static void s5_free_block(s5fs_t *fs, int block);
static int s5_alloc_block(s5fs_t *);

/* allocated an indirect block for a vnode who's indirect block is currently sparse */
static int alloc_indirect_block(vnode_t *v){

    /* an array of 0's that we'll use to quickly create blocks of zeros */
    static int zero_array[BLOCK_SIZE] = {};

    s5_inode_t *inode = VNODE_TO_S5INODE(v);

    KASSERT(inode->s5_indirect_block == 0);

    /* first, get an indirect block */
    int indirect_block = s5_alloc_block(VNODE_TO_S5FS(v));

    if (indirect_block == -ENOSPC){
        dbg(DBG_S5FS, "couldn't alloc a new block\n");
        return -ENOSPC;
    }

    KASSERT(indirect_block > 0 && "forgot to handle an error case");

    /* then, zero it */
    pframe_t *ind_page;
    mmobj_t *mmo = S5FS_TO_VMOBJ(VNODE_TO_S5FS(v));

    int get_res = pframe_get(mmo, indirect_block, &ind_page);

    if (get_res < 0){
        return get_res;
    }

    memcpy(ind_page->pf_addr, zero_array, BLOCK_SIZE);

    pframe_dirty(ind_page);

    /* finally, set this block to be the indirect block of the inode */
    inode->s5_indirect_block = indirect_block;
    s5_dirty_inode(VNODE_TO_S5FS(v), inode);

    return 0;
}

/*
 * Return the disk-block number for the given seek pointer (aka file
 * position).
 *
 * If the seek pointer refers to a sparse block, and alloc is false,
 * then return 0. If the seek pointer refers to a sparse block, and
 * alloc is true, then allocate a new disk block (and make the inode
 * point to it) and return it.
 *
 * Be sure to handle indirect blocks!
 *
 * If there is an error, return -errno.
 *
 * You probably want to use pframe_get, pframe_pin, pframe_unpin, pframe_dirty.
 */
int
s5_seek_to_block(vnode_t *vnode, off_t seekptr, int alloc)
{
    int block_index = S5_DATA_BLOCK(seekptr);

    if ((unsigned) block_index >= S5_MAX_FILE_BLOCKS){
        dbg(DBG_S5FS, "file too large");
        return -EFBIG;
    }

    s5_inode_t *inode = VNODE_TO_S5INODE(vnode);

    uint32_t block_num;

    if (block_index >= S5_NDIRECT_BLOCKS){
        pframe_t *ind_page;

        mmobj_t *mmo = S5FS_TO_VMOBJ(VNODE_TO_S5FS(vnode));

        if (inode->s5_indirect_block == 0){
            if (!alloc){
                return 0;
            }

            int alloc_res = alloc_indirect_block(vnode);
            if (alloc_res < 0){
                dbg(DBG_S5FS, "error allocating indirect block\n");
                return alloc_res;
            }
        }

        if (pframe_get(mmo, inode->s5_indirect_block, &ind_page) < 0){
            panic("an indirect block is messed up\n");
        }

        block_num = ((uint32_t *) ind_page->pf_addr)[block_index - S5_NDIRECT_BLOCKS];

        /* case where we've found a sparse block and need to allocate*/
        if (block_num == 0 && alloc){
            int block_num = s5_alloc_block(VNODE_TO_S5FS(vnode));

            if (block_num == -ENOSPC){
                dbg(DBG_S5FS, "couldn't alloc a new block\n");
                return -ENOSPC;
            }

            KASSERT(block_num > 0 && "forgot to handle an error case");

            ((uint32_t *) ind_page->pf_addr)[block_index - S5_NDIRECT_BLOCKS] = block_num;

            pframe_pin(ind_page);
            pframe_dirty(ind_page);
            pframe_unpin(ind_page);

        }

    } else {
        block_num = inode->s5_direct_blocks[block_index];

        /* case where we've found a sparse block and need to allocate*/
        if (block_num == 0 && alloc){
            uint32_t block_num = s5_alloc_block(VNODE_TO_S5FS(vnode));

            if ((signed) block_num == -ENOSPC){
                dbg(DBG_S5FS, "couldn't alloc a new block\n");
                return -ENOSPC;
            }

            KASSERT(block_num > 0 && "forgot to handle an error case");

            inode->s5_direct_blocks[block_index] = block_num;
            s5_dirty_inode(VNODE_TO_S5FS(vnode), inode);
        }
    }

    return block_num;
}


/*
 * Locks the mutex for the whole file system
 */
static void
lock_s5(s5fs_t *fs)
{
        kmutex_lock(&fs->s5f_mutex);
}

/*
 * Unlocks the mutex for the whole file system
 */
static void
unlock_s5(s5fs_t *fs)
{
        kmutex_unlock(&fs->s5f_mutex);
}

static off_t max(off_t a, off_t b){
    return (a >= b) ? a : b;
}

/*
 * Write len bytes to the given inode, starting at seek bytes from the
 * beginning of the inode. On success, return the number of bytes
 * actually written (which should be 'len', unless there's only enough
 * room for a partial write); on failure, return -errno.
 *
 * This function should allow writing to files or directories, treating
 * them identically.
 *
 * Writing to a sparse block of the file should cause that block to be
 * allocated.  Writing past the end of the file should increase the size
 * of the file. Blocks between the end and where you start writing will
 * be sparse.
 *
 * Do not call s5_seek_to_block() directly from this function.  You will
 * use the vnode's pframe functions, which will eventually result in a
 * call to s5_seek_to_block().
 *
 * You will need pframe_dirty(), pframe_get(), memcpy().
 */
int
s5_write_file(vnode_t *vnode, off_t seek, const char *bytes, size_t len)
{
    if (seek < 0){
        dbg(DBG_S5FS, "invalid seek value\n");
        return -EINVAL;
    }

    if (seek + len > S5_MAX_FILE_SIZE){
        len = S5_MAX_FILE_SIZE - seek;
    }

    /* extend file size, if necessary */
    vnode->vn_len = max(seek + len, vnode->vn_len);
    VNODE_TO_S5INODE(vnode)->s5_size = vnode->vn_len;

    unsigned int srcpos = 0;
    int get_res;
    int write_size;
    pframe_t *p;

    while (srcpos < len){
        int data_offset = S5_DATA_OFFSET(seek);

        get_res = pframe_get(&vnode->vn_mmobj, S5_DATA_BLOCK(seek), &p);

        if (get_res < 0){
            dbg(DBG_S5FS, "error getting page\n");
            return get_res;
        }

        if (PAGE_SIZE - data_offset > len){
            write_size = S5_DATA_OFFSET(len);
        } else {
            write_size = PAGE_SIZE - data_offset;
        }

        KASSERT(write_size >= 0 && "write size is negative");
        memcpy((char *) p->pf_addr + data_offset, (void *) bytes, write_size);
        pframe_pin(p);
        pframe_dirty(p);
        pframe_unpin(p);

        srcpos += write_size;
        seek += write_size; 
    }

    return srcpos;
}

/*
 * Read up to len bytes from the given inode, starting at seek bytes
 * from the beginning of the inode. On success, return the number of
 * bytes actually read, or 0 if the end of the file has been reached; on
 * failure, return -errno.
 *
 * This function should allow reading from files or directories,
 * treating them identically.
 *
 * Reading from a sparse block of the file should act like reading
 * zeros; it should not cause the sparse blocks to be allocated.
 *
 * Similarly as in s5_write_file(), do not call s5_seek_to_block()
 * directly from this function.
 *
 * If the region to be read would extend past the end of the file, less
 * data will be read than was requested.
 *
 * You probably want to use pframe_get(), memcpy().
 */
int
s5_read_file(struct vnode *vnode, off_t seek, char *dest, size_t len)
{   
    if (seek < 0){
        dbg(DBG_S5FS, "invalid seek value\n");
        return -EINVAL;
    }

    if (seek + len > (unsigned) vnode->vn_len){
        len = vnode->vn_len - seek;
    }

    KASSERT(len > 0);
            
    unsigned int destpos = 0;
    int get_res;
    int read_size;     
    pframe_t *p;

    while (destpos < len){
        int data_offset = S5_DATA_OFFSET(seek);

        get_res = pframe_get(&vnode->vn_mmobj, S5_DATA_BLOCK(seek), &p);

        if (get_res < 0){
            dbg(DBG_S5FS, "error getting page\n");
            return get_res;
        }
       
        if (PAGE_SIZE - data_offset > len){
            read_size = S5_DATA_OFFSET(len);
        } else {
            read_size = PAGE_SIZE - data_offset;
        }

        memcpy((void *) dest, (char *) p->pf_addr + data_offset, read_size);

        destpos += read_size;
        seek += read_size;
    }

    return destpos;
}

/*
 * Allocate a new disk-block off the block free list and return it. If
 * there are no free blocks, return -ENOSPC.
 *
 * This will not initialize the contents of an allocated block; these
 * contents are undefined.
 *
 * If the super block's s5s_nfree is 0, you need to refill 
 * s5s_free_blocks and reset s5s_nfree.  You need to read the contents 
 * of this page using the pframe system in order to obtain the next set of
 * free block numbers.
 *
 * Don't forget to dirty the appropriate blocks!
 *
 * You'll probably want to use lock_s5(), unlock_s5(), pframe_get(),
 * and s5_dirty_super()
 */
static int
s5_alloc_block(s5fs_t *fs)
{
    s5_super_t *s = fs->s5f_super;

    lock_s5(fs);

    KASSERT(S5_NBLKS_PER_FNODE > s->s5s_nfree);

    int free_block_num;

    if (s->s5s_nfree == 0){
        free_block_num = s->s5s_free_blocks[S5_NBLKS_PER_FNODE - 1];

        if (free_block_num == -1){
            unlock_s5(fs);
            return -ENOSPC;
        }

        /* get the pframe from which we'll replenish our list of free block nums */
        pframe_t *next_free_blocks;
        KASSERT(fs->s5f_bdev);
        int get_res = pframe_get(&fs->s5f_bdev->bd_mmobj, free_block_num,
                &next_free_blocks);

        if (get_res < 0){
            dbg(DBG_S5FS, "error in pframe_get\n");
            unlock_s5(fs);
            return get_res;
        }

        memcpy((void *)(s->s5s_free_blocks), next_free_blocks->pf_addr, 
                S5_NBLKS_PER_FNODE * sizeof(int));

        s->s5s_nfree = S5_NBLKS_PER_FNODE;
    } else {
        free_block_num = s->s5s_free_blocks[s->s5s_nfree--];
    }

    s5_dirty_super(fs);

    unlock_s5(fs);
    return free_block_num;
}


/*
 * Given a filesystem and a block number, frees the given block in the
 * filesystem.
 *
 * This function may potentially block.
 *
 * The caller is responsible for ensuring that the block being placed on
 * the free list is actually free and is not resident.
 */
static void
s5_free_block(s5fs_t *fs, int blockno)
{
        s5_super_t *s = fs->s5f_super;


        lock_s5(fs);

        KASSERT(S5_NBLKS_PER_FNODE > s->s5s_nfree);

        if ((S5_NBLKS_PER_FNODE - 1) == s->s5s_nfree) {
                /* get the pframe where we will store the free block nums */
                pframe_t *prev_free_blocks = NULL;
                KASSERT(fs->s5f_bdev);
                pframe_get(&fs->s5f_bdev->bd_mmobj, blockno, &prev_free_blocks);
                KASSERT(prev_free_blocks->pf_addr);

                /* copy from the superblock to the new block on disk */
                memcpy(prev_free_blocks->pf_addr, (void *)(s->s5s_free_blocks),
                       S5_NBLKS_PER_FNODE * sizeof(int));
                pframe_dirty(prev_free_blocks);

                /* reset s->s5s_nfree and s->s5s_free_blocks */
                s->s5s_nfree = 0;
                s->s5s_free_blocks[S5_NBLKS_PER_FNODE - 1] = blockno;
        } else {
                s->s5s_free_blocks[s->s5s_nfree++] = blockno;
        }

        s5_dirty_super(fs);

        unlock_s5(fs);
}

/*
 * Creates a new inode from the free list and initializes its fields.
 * Uses S5_INODE_BLOCK to get the page from which to create the inode
 *
 * This function may block.
 */
int
s5_alloc_inode(fs_t *fs, uint16_t type, devid_t devid)
{
        s5fs_t *s5fs = FS_TO_S5FS(fs);
        pframe_t *inodep;
        s5_inode_t *inode;
        int ret = -1;

        KASSERT((S5_TYPE_DATA == type)
                || (S5_TYPE_DIR == type)
                || (S5_TYPE_CHR == type)
                || (S5_TYPE_BLK == type));


        lock_s5(s5fs);

        if (s5fs->s5f_super->s5s_free_inode == (uint32_t) -1) {
                unlock_s5(s5fs);
                return -ENOSPC;
        }

        pframe_get(&s5fs->s5f_bdev->bd_mmobj,
                   S5_INODE_BLOCK(s5fs->s5f_super->s5s_free_inode),
                   &inodep);
        KASSERT(inodep);

        inode = (s5_inode_t *)(inodep->pf_addr)
                + S5_INODE_OFFSET(s5fs->s5f_super->s5s_free_inode);

        KASSERT(inode->s5_number == s5fs->s5f_super->s5s_free_inode);

        ret = inode->s5_number;

        /* reset s5s_free_inode; remove the inode from the inode free list: */
        s5fs->s5f_super->s5s_free_inode = inode->s5_next_free;
        pframe_pin(inodep);
        s5_dirty_super(s5fs);
        pframe_unpin(inodep);


        /* init the newly-allocated inode: */
        inode->s5_size = 0;
        inode->s5_type = type;
        inode->s5_linkcount = 0;
        memset(inode->s5_direct_blocks, 0, S5_NDIRECT_BLOCKS * sizeof(int));
        if ((S5_TYPE_CHR == type) || (S5_TYPE_BLK == type))
                inode->s5_indirect_block = devid;
        else
                inode->s5_indirect_block = 0;

        s5_dirty_inode(s5fs, inode);

        unlock_s5(s5fs);

        return ret;
}


/*
 * Free an inode by freeing its disk blocks and putting it back on the
 * inode free list.
 *
 * You should also reset the inode to an unused state (eg. zero-ing its
 * list of blocks and setting its type to S5_FREE_TYPE).
 *
 * Don't forget to free the indirect block if it exists.
 *
 * You probably want to use s5_free_block().
 */
void
s5_free_inode(vnode_t *vnode)
{
        uint32_t i;
        s5_inode_t *inode = VNODE_TO_S5INODE(vnode);
        s5fs_t *fs = VNODE_TO_S5FS(vnode);

        KASSERT((S5_TYPE_DATA == inode->s5_type)
                || (S5_TYPE_DIR == inode->s5_type)
                || (S5_TYPE_CHR == inode->s5_type)
                || (S5_TYPE_BLK == inode->s5_type));

        /* free any direct blocks */
        for (i = 0; i < S5_NDIRECT_BLOCKS; ++i) {
                if (inode->s5_direct_blocks[i]) {
                        dprintf("freeing block %d\n", inode->s5_direct_blocks[i]);
                        s5_free_block(fs, inode->s5_direct_blocks[i]);

                        s5_dirty_inode(fs, inode);
                        inode->s5_direct_blocks[i] = 0;
                }
        }

        if (((S5_TYPE_DATA == inode->s5_type)
             || (S5_TYPE_DIR == inode->s5_type))
            && inode->s5_indirect_block) {
                pframe_t *ibp;
                uint32_t *b;

                pframe_get(S5FS_TO_VMOBJ(fs),
                           (unsigned)inode->s5_indirect_block,
                           &ibp);
                KASSERT(ibp
                        && "because never fails for block_device "
                        "vm_objects");
                pframe_pin(ibp);

                b = (uint32_t *)(ibp->pf_addr);
                for (i = 0; i < S5_NIDIRECT_BLOCKS; ++i) {
                        KASSERT(b[i] != inode->s5_indirect_block);
                        if (b[i])
                                s5_free_block(fs, b[i]);
                }

                pframe_unpin(ibp);

                s5_free_block(fs, inode->s5_indirect_block);
        }

        inode->s5_indirect_block = 0;
        inode->s5_type = S5_TYPE_FREE;
        s5_dirty_inode(fs, inode);

        lock_s5(fs);
        inode->s5_next_free = fs->s5f_super->s5s_free_inode;
        fs->s5f_super->s5s_free_inode = inode->s5_number;
        unlock_s5(fs);

        s5_dirty_inode(fs, inode);
        s5_dirty_super(fs);
}

int min(int a, int b){
    return (a <= b) ? a : b;
}

static int s5_find_dirent_helper(vnode_t *vnode, const char *name, size_t namelen,
        off_t *offset, int *ino){
    s5_dirent_t dirents[NDIRENTS];
    size_t readsize = NDIRENTS * sizeof(s5_dirent_t);

    off_t seek = 0;

    while (seek < vnode->vn_len){
        int readsize = min(vnode->vn_len - seek, NDIRENTS * sizeof(s5_dirent_t));
        int dirents_read = readsize / sizeof(s5_dirent_t);

        int read_res = s5_read_file(vnode, seek, (char *) dirents, readsize);

        if (read_res < 0){
            dbg(DBG_S5FS, "error getting dirents\n");
            return read_res;
        }
        int i;
        for (i = 0; i < dirents_read; i++){
            if (name_match(dirents[i].s5d_name, name, namelen)){
                if (offset != NULL){
                    *offset = seek + (i * sizeof(s5_dirent_t));
                }

                if (ino != NULL){
                    *ino = dirents[i].s5d_inode;
                }

                return 0;
            }            
        }
        seek += read_res;
    }

    return -ENOENT;
}

/* returns the offset of the first empy dirent in vnode, or the length of the vnode
 *  if none exists. This function May also return any error that s5_find_dirent_helper
 *   returns. This function assumes that vnode is a directory.
 */
static int find_empty_dirent(vnode_t *vnode){
    KASSERT(vnode->vn_ops->mkdir != NULL);
    off_t offset;
    int find_res = s5_find_dirent_helper(vnode, "", 0, &offset, NULL);

    switch (find_res){
        case 0:
            return offset;
        case -ENOENT:
            return vnode->vn_len;
        default:
            return find_res;
    }
}

/*
 * Locate the directory entry in the given inode with the given name,
 * and return its inode number. If there is no entry with the given
 * name, return -ENOENT.
 *
 * You'll probably want to use s5_read_file and name_match
 *
 * You can either read one dirent at a time or optimize and read more.
 * Either is fine.
 */
int
s5_find_dirent(vnode_t *vnode, const char *name, size_t namelen)
{
    int ino;
    int find_res = s5_find_dirent_helper(vnode, name, namelen, NULL, &ino);

    if (find_res == 0){
        return ino;
    } else {
        dbg(DBG_S5FS, "unable to locate directory\n");
        return find_res;
    }
}

/*
 * Locate the directory entry in the given inode with the given name,
 * and delete it. If there is no entry with the given name, return
 * -ENOENT.
 *
 * In order to ensure that the directory entries are contiguous in the
 * directory file, you will need to move the last directory entry into
 * the remove dirent's place.
 *
 * When this function returns, the inode refcount on the removed file
 * should be decremented.
 *
 * It would be a nice extension to free blocks from the end of the
 * directory file which are no longer needed.
 *
 * Don't forget to dirty appropriate blocks!
 *
 * You probably want to use vget(), vput(), s5_read_file(),
 * s5_write_file(), and s5_dirty_inode().
 */
int
s5_remove_dirent(vnode_t *vnode, const char *name, size_t namelen)
{
    panic("nyi");
        NOT_YET_IMPLEMENTED("S5FS: s5_remove_dirent");
        return -1;
}

/*
 * Create a new directory entry in directory 'parent' with the given name, which
 * refers to the same file as 'child'.
 *
 * When this function returns, the inode refcount on the file that was linked to
 * should be incremented.
 *
 * Remember to increment the ref counts appropriately
 *
 * You probably want to use s5_find_dirent(), s5_write_file(), and s5_dirty_inode().
 */
int
s5_link(vnode_t *parent, vnode_t *child, const char *name, size_t namelen)
{
    KASSERT(parent->vn_ops->mkdir != NULL);
    KASSERT(s5_find_dirent(parent, name, namelen) == -ENOENT && "file exists\n");

    int init_refcount = VNODE_TO_S5INODE(child)->s5_linkcount;

    s5_dirent_t d;
    d.s5d_inode = VNODE_TO_S5INODE(child)->s5_number;
    memcpy(d.s5d_name, name, namelen);
    d.s5d_name[namelen] = '\0';

    off_t write_offset = find_empty_dirent(parent);

    if (write_offset < 0){
        dbg(DBG_S5FS, "error finding dirent to write to\n");
    }

    int res = s5_write_file(parent, write_offset, (char *) &d,
            sizeof(s5_dirent_t));

    if (res < 0){
        dbg(DBG_S5FS, "error writing child entry in parent\n");
        return res;
    }

    s5_dirty_inode(VNODE_TO_S5FS(parent), VNODE_TO_S5INODE(parent));

    if (parent != child){
        dbg(DBG_S5FS, "incrementing link count on inode %d from %d to %d\n",
            VNODE_TO_S5INODE(child)->s5_number, VNODE_TO_S5INODE(child)->s5_linkcount,
            VNODE_TO_S5INODE(child)->s5_linkcount + 1);

        VNODE_TO_S5INODE(child)->s5_linkcount++;
        s5_dirty_inode(VNODE_TO_S5FS(child), VNODE_TO_S5INODE(child));

        KASSERT(VNODE_TO_S5INODE(child)->s5_linkcount == init_refcount + 1 &&
                "link count not incremented properly");
    }

    return 0;
}

/*
 * Return the number of blocks that this inode has allocated on disk.
 * This should include the indirect block, but not include sparse
 * blocks.
 *
 * This is only used by s5fs_stat().
 *
 * You'll probably want to use pframe_get().
 */
int
s5_inode_blocks(vnode_t *vnode)
{
    s5_inode_t *inode = VNODE_TO_S5INODE(vnode);
    int allocated_blocks;

    int i;
    for (i = 0; i < S5_NDIRECT_BLOCKS; i++){
        if (inode->s5_direct_blocks[i] != 0){
            allocated_blocks++;
        }
    }

    if (inode->s5_indirect_block == 0){
        return allocated_blocks;
    }

    /* count the indirect block as an allocated block */
    allocated_blocks++;

    pframe_t *p;
    mmobj_t *mmobj = S5FS_TO_VMOBJ(VNODE_TO_S5FS(vnode));

    int get_res = pframe_get(mmobj, inode->s5_indirect_block, &p);

    if (get_res < 0){
        return get_res;
    }

    int j;
    for (j = 0; j < S5_NDIRECT_BLOCKS; j++){
        if (((int *)p->pf_addr)[j] != 0){
            allocated_blocks++;
        }
    }

    return allocated_blocks;
}

