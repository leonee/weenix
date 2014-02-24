#include "util/debug.h"
#include "drivers/blockdev.h"
#include "proc/proc.h"
#include "test/kshell/kshell.h"
#include "test/kshell/io.h"

#define BLOCKNUM_1 50
#define BLOCKNUM_2 52
#define BLOCKS_TO_WRITE 2
#define BLOCKS_TO_READ BLOCKS_TO_WRITE

typedef struct rw_args {
    blockdev_t *bd;
    char *data;
    int blocknum;
    int num_blocks;
} rw_args_t;

static void * write_func(int arg1, void *arg2){
    rw_args_t *args = (rw_args_t *) arg2;

    dbg(DBG_TEST | DBG_DISK, "writing data to block %d\n", args->blocknum);

    char *writebuf = args->data;

    int write_result = args->bd->bd_ops->
                write_block(args->bd, args->data, args->blocknum, args->num_blocks);

    KASSERT(write_result == 0);

    dbg(DBG_TEST | DBG_DISK, "successfully wrote data to block %d\n", args->blocknum);
 
    return NULL;
}

static void * read_func(int arg1, void *arg2){
    rw_args_t *args = (rw_args_t *) arg2;

    dbg(DBG_TEST | DBG_DISK, "reading data from block %d\n", args->blocknum);

    char *readbuf = args->data;

    int read_result = args->bd->bd_ops->
                 read_block(args->bd, args->data, args->blocknum, args->num_blocks);

    KASSERT(read_result == 0);

    dbg(DBG_TEST | DBG_DISK, "successfully read data from block %d\n", args->blocknum);

    return NULL;
}

void test_multiple_threads(){
    dbg(DBG_TEST | DBG_DISK, "testing reading and writing to disk with multiple threads\n");
    blockdev_t *bd = blockdev_lookup(MKDEVID(1, 0));

    KASSERT (bd != NULL);

    char *readbuf1 = (char *) page_alloc_n(BLOCKS_TO_READ);
    char *readbuf2 = (char *) page_alloc_n(BLOCKS_TO_READ);
    char *writebuf1 = (char *) page_alloc_n(BLOCKS_TO_WRITE);
    char *writebuf2 = (char *) page_alloc_n(BLOCKS_TO_WRITE);

    KASSERT(readbuf1 != NULL && 
            readbuf2 != NULL &&
            writebuf2 != NULL &&
            writebuf2 != NULL &&
            "not enough memory");

    unsigned int i;
    for (i = 0; i < (BLOCK_SIZE * BLOCKS_TO_WRITE); i++){
        writebuf1[i] = 'a';
        writebuf2[i] = 'b';
    }

    /* create and run procs and threads for writing */
    rw_args_t write_args_1 = {bd, writebuf1, BLOCKNUM_1, BLOCKS_TO_WRITE};
    rw_args_t write_args_2 = {bd, writebuf2, BLOCKNUM_2, BLOCKS_TO_WRITE};

    proc_t *wp1 = proc_create("ata_write_proc_1");
    proc_t *wp2 = proc_create("ata_write_proc_2");

    kthread_t *wt1 = kthread_create(wp1, write_func, 0, (void *) &write_args_1); 
    kthread_t *wt2 = kthread_create(wp2, write_func, 0, (void *) &write_args_2);

    sched_make_runnable(wt1);
    sched_make_runnable(wt2);

    int status;
    do_waitpid(wp1->p_pid, 0, &status);
    do_waitpid(wp2->p_pid, 0, &status);

    /* create and run procs and threads for reading */
    rw_args_t read_args_1 = {bd, readbuf1, BLOCKNUM_1, BLOCKS_TO_READ};
    rw_args_t read_args_2 = {bd, readbuf2, BLOCKNUM_2, BLOCKS_TO_READ};

    proc_t *rp1 = proc_create("ata_read_proc_1");
    proc_t *rp2 = proc_create("ata_read_proc_2");

    kthread_t *rt1 = kthread_create(rp1, read_func, 0, (void *) &read_args_1);
    kthread_t *rt2 = kthread_create(rp2, read_func, 0, (void *) &read_args_2);

    sched_make_runnable(rt1);
    sched_make_runnable(rt2);

    do_waitpid(rp1->p_pid, 0, &status);
    do_waitpid(rp2->p_pid, 0, &status);

    /* make sure that we wrote and read properly */
    unsigned int j;
    for (j = 0; j < BLOCK_SIZE * BLOCKS_TO_READ; j++){
        KASSERT(readbuf1[j] == 'a');
        KASSERT(readbuf2[j] == 'b');
    }

    page_free_n((void *) readbuf1, BLOCKS_TO_READ);
    page_free_n((void *) readbuf2, BLOCKS_TO_READ);
    page_free_n((void *) writebuf1, BLOCKS_TO_WRITE);
    page_free_n((void *) writebuf2, BLOCKS_TO_WRITE);

    dbg(DBG_TESTPASS, "All multi-threaded read/write tests passed\n");
}

static void simple_write(rw_args_t write_args){
    proc_t *wp = proc_create("ata_write_proc");
    kthread_t *wt = kthread_create(wp, write_func, 0, (void *) &write_args);
    sched_make_runnable(wt);

    int status;
    do_waitpid(wp->p_pid, 0, &status);
}

static void simple_read(rw_args_t read_args){
    proc_t *rp = proc_create("ata_read_proc");
    kthread_t *rt = kthread_create(rp, read_func, 0, (void *) &read_args);
    sched_make_runnable(rt);

    int status2;
    do_waitpid(rp->p_pid, 0, &status2);
}

void test_single_rw(){
    dbg(DBG_TEST | DBG_DISK, "testing reading and writing to disk\n");

    blockdev_t *bd = blockdev_lookup(MKDEVID(1, 0));

    KASSERT(bd != NULL);

    char *writebuf = (char *) page_alloc();
    char *readbuf = (char *) page_alloc();

    KASSERT(readbuf != NULL && writebuf != NULL && "not enough memory");

    unsigned int i;
    for (i = 0; i < BLOCK_SIZE; i++){
        writebuf[i] = 'o';
    }

    int block_to_write = 60;

    rw_args_t read_args = {bd, readbuf, block_to_write, 1};
    rw_args_t write_args = {bd, writebuf, block_to_write, 1};

    simple_write(write_args);
    simple_read(read_args);
 
    unsigned int j;
    for (j = 0; j < BLOCK_SIZE; j++){
        KASSERT(readbuf[j] == 'o');
    }

    page_free((void *) readbuf);
    page_free((void *) writebuf);

    dbg(DBG_TESTPASS, "all simple ata tests passed\n");
}

void run_ata_tests(){ 
    test_single_rw();
    test_multiple_threads();
    
    dbg(DBG_TESTPASS, "All ata tests passed!\n");
}

int get_digit(char *s){
    switch(*s){
        case '0' :
            return 0;
        case '1' :
            return 1;
        case '2' : 
            return 2;
        case '3' :
            return 3;
        case '4' :
            return 4;
        case '5' :
            return 5;
        case '6' :
            return 6;
        case '7' :
            return 7;
        case '8' :
            return 8;
        case '9' :
            return 9;
        default :
            return -1;
    }
}

int toint(char *s){
    int i = 0;
    int curr;

    while ((curr = get_digit(s)) != -1){
        i = i * 10 + curr;
        s++;
    }

    return i;
}

int kshell_ata_read(kshell_t *k, int argc, char **argv){
    if (argc != 3){
        dbg(DBG_DISK | DBG_TERM, "received wrong amount of arguments\n");
        kprintf(k, "Usage: <read_block> <num_blocks>\n");
        return -1;
    }

    blockdev_t *bd = blockdev_lookup(MKDEVID(1, 0));

    int blocknum = toint(argv[1]);
    int count = toint(argv[2]);

    char *data = (char *) page_alloc_n(count);

    if (data == NULL){
        kprintf(k, "not enough memory");
        return -1;
    }

    int result = bd->bd_ops->read_block(bd, data, blocknum, count);

    char newline[2] = {'\n', '\0'};

    kprintf(k, data);
    kprintf(k, newline);

    page_free_n((void *) data, count);

    return result;
}

int kshell_ata_write(kshell_t *k, int argc, char **argv){
    if (argc != 3){
        dbg(DBG_DISK | DBG_TERM, "received wrong amount of arguments\n");
        kprintf(k, "Usage: <read_block> <string>\n");
        return -1;
    }

    blockdev_t *bd = blockdev_lookup(MKDEVID(1, 0));

    int blocknum = toint(argv[1]);

    char *input_text = argv[2];
    char *data = (void *) page_alloc();

    if (data == NULL){
        kprintf(k, "not enough memory");
        return -1;
    }

    int i = 0;
    while (input_text[i] != NULL){
        data[i] = input_text[i];
        i++;
    }

    data[i] = '\0';

    int result = bd->bd_ops->write_block(bd, data, blocknum, 1);

    return result;
}
