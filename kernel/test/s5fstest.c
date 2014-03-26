#include "util/debug.h"

#include "fs/fcntl.h"
#include "fs/lseek.h"
#include "fs/vfs_syscall.h"
#include "fs/vnode.h"
#include "fs/stat.h"

#include "fs/s5fs/s5fs.h"
#include "fs/s5fs/s5fs_subr.h"

#include "fs/open.h"

#define READSIZE (S5_NDIRECT_BLOCKS + 1) * S5_BLOCK_SIZE

static void run_indirect_test(){
    dbg(DBG_TEST, "testing indirect blocks\n");

    int direct_size = S5_NDIRECT_BLOCKS * S5_BLOCK_SIZE;

    int fd = do_open("bigfile", O_RDWR|O_CREAT);

    KASSERT(fd >= 0 && fd < NFILES);

    do_lseek(fd, direct_size, SEEK_SET);

    char writebuf[BLOCK_SIZE];

    int i;
    for (i = 0; i < S5_BLOCK_SIZE; i++){
        writebuf[i] = 'a';
    }

    do_write(fd, (void *) writebuf, S5_BLOCK_SIZE);

    do_lseek(fd, 0, SEEK_SET);

    char readbuf[S5_BLOCK_SIZE];

    /* make sure everything from the start to where we wrote
     * is nulls */
    int j;
    for (j = 0; j < S5_NDIRECT_BLOCKS; j++){

        int chars_read = do_read(fd, readbuf, S5_BLOCK_SIZE);

        KASSERT(chars_read == S5_BLOCK_SIZE);

        int k;
        for (k = 0; k < S5_BLOCK_SIZE; k++){
            KASSERT(readbuf[k] == '\0');
        }
    }

    /* read what we actually wrote */
    int chars_read = do_read(fd, readbuf, S5_BLOCK_SIZE);

    KASSERT(chars_read == S5_BLOCK_SIZE);

    int l;
    for (l = 0; l < S5_BLOCK_SIZE; l++){
        KASSERT(readbuf[l] == 'a');
    }
    
    do_close(fd);

    /* make sure that the inode blocks are still sparse */
    vnode_t *v;

    KASSERT(open_namev("/bigfile", O_RDONLY, &v, NULL) == 0);

    s5_inode_t *inode = VNODE_TO_S5INODE(v);

    int m;
    for (m = 0; m < S5_NDIRECT_BLOCKS; m++){
        KASSERT(inode->s5_direct_blocks[m] == 0);
    }

    vput(v);

    struct stat s;
    do_stat("/bigfile", &s);

    KASSERT(s.st_blocks == 2);

    KASSERT(do_unlink("/bigfile") == 0);

    dbg(DBG_TEST, "indirect block tests passed\n"); 
}


void run_s5fs_tests(){
    run_indirect_test();

    dbg(DBG_TESTPASS, "All s5fs tests passed!\n");
}
