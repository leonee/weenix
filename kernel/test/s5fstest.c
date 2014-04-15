#include "util/debug.h"

#include "fs/fcntl.h"
#include "fs/lseek.h"
#include "fs/vfs_syscall.h"
#include "fs/vnode.h"
#include "fs/stat.h"

#include "fs/s5fs/s5fs.h"
#include "fs/s5fs/s5fs_subr.h"

#include "fs/open.h"

#include "errno.h"

#define READSIZE (S5_NDIRECT_BLOCKS + 1) * S5_BLOCK_SIZE

#ifndef __VM__
    #define INODES_IN_USE 11
#else
    #define INODES_IN_USE 41
#endif

#define FREE_INODES (240 - INODES_IN_USE)

/* thanks Scala :) printf("{\"" + (1 to 100).mkString("\",\"") + "}\"") */
static char *filenames[] = {"0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "13", "14", "15", "16", "17", "18", "19", "20", "21", "22", "23", "24", "25", "26", "27", "28", "29", "30", "31", "32", "33", "34", "35", "36", "37", "38", "39", "40", "41", "42", "43", "44", "45", "46", "47", "48", "49", "50", "51", "52", "53", "54", "55", "56", "57", "58", "59", "60", "61", "62", "63", "64", "65", "66", "67", "68", "69", "70", "71", "72", "73", "74", "75", "76", "77", "78", "79", "80", "81", "82", "83", "84", "85", "86", "87", "88", "89", "90", "91", "92", "93", "94", "95", "96", "97", "98", "99", "100", "101", "102", "103", "104", "105", "106", "107", "108", "109", "110", "111", "112", "113", "114", "115", "116", "117", "118", "119", "120", "121", "122", "123", "124", "125", "126", "127", "128", "129", "130", "131", "132", "133", "134", "135", "136", "137", "138", "139", "140", "141", "142", "143", "144", "145", "146", "147", "148", "149", "150", "151", "152", "153", "154", "155", "156", "157", "158", "159", "160", "161", "162", "163", "164", "165", "166", "167", "168", "169", "170", "171", "172", "173", "174", "175", "176", "177", "178", "179", "180", "181", "182", "183", "184", "185", "186", "187", "188", "189", "190", "191", "192", "193", "194", "195", "196", "197", "198", "199", "200", "201", "202", "203", "204", "205", "206", "207", "208", "209", "210", "211", "212", "213", "214", "215", "216", "217", "218", "219", "220", "221", "222", "223", "224", "225", "226", "227", "228", "229", "230", "231", "232", "233", "234", "235", "236", "237", "238", "239"};

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

static void test_max_inodes(){
    dbg(DBG_TEST, "testing hitting max inodes\n");

    char name[4];

    int i = 0;
    
    int fd;
    while((fd = do_open(filenames[i], O_RDONLY|O_CREAT)) == 0){
        do_close(fd);
        i++;
    }

    KASSERT(fd == -ENOSPC);
    KASSERT(do_mknod("/dev/testhahaha", S_IFCHR, 0) == -ENOSPC);
    KASSERT(do_mkdir("/dev/testhahaha") == -ENOSPC);
  
    int j;
    /*for (j = 0; j < FREE_INODES; j++){*/
    for (j = 0; j < i; j++){
        dbg(DBG_TEST, "j = %d\n", j);
        KASSERT(do_unlink(filenames[j]) == 0);
    }

    dbg(DBG_TEST, "all max inodes tests passed\n");
}

static void test_max_file_length(){
    dbg(DBG_TEST, "testing max file length\n");

    int fd = do_open("/largefile", O_RDWR|O_CREAT);
    KASSERT(fd >= 0 && fd < NFILES);

    do_lseek(fd, S5_MAX_FILE_SIZE - 2, SEEK_SET);

    char writebuf[3] = {'a', 'a', 'a'};
    char readbuf[3] = {'b', 'b', 'b'};

    KASSERT(do_write(fd, (void *) writebuf, 1) == 1);

    KASSERT(do_write(fd, (void *) writebuf, 1) == 0);

    do_lseek(fd, S5_MAX_FILE_SIZE - 2, SEEK_SET);

    KASSERT(do_read(fd, (void *) readbuf, 3) == 1);
    KASSERT(readbuf[0] == 'a');
    KASSERT(readbuf[1] == 'b');
    KASSERT(readbuf[2] == 'b');

    KASSERT(do_close(fd) == 0);

    KASSERT(do_unlink("/largefile") == 0);

    dbg(DBG_TEST, "all max file length tests passed\n");
}

static void test_max_data(){
    dbg(DBG_TEST, "testing maxing out on disk space\n");

    int fullfd = do_open("/fullfile", O_RDWR|O_CREAT);
    KASSERT(fullfd == 0);

    do_lseek(fullfd, 0, SEEK_SET);

    char writebuf[BLOCK_SIZE];

    int i;
    for (i = 0; i < S5_BLOCK_SIZE; i++){
        writebuf[i] = 'a';
    }

    unsigned int j;
    for (j = 0; j < S5_MAX_FILE_BLOCKS - 1; j++){
        KASSERT(do_write(fullfd, (void *) writebuf, S5_BLOCK_SIZE) == S5_BLOCK_SIZE);
    }

    KASSERT(do_close(fullfd) == 0);

    int bigfd = do_open("/bigfile", O_RDWR|O_CREAT);
    KASSERT(bigfd == 0);

    do_lseek(bigfd, 0, SEEK_SET);

    /* we can also hold some stuff in RAM, so we can do a few more writes */
    int last_write_res;

    while ((last_write_res = do_write(bigfd, (void *) writebuf, S5_BLOCK_SIZE))
            == S5_BLOCK_SIZE){
        /* do nothing */
    }

    KASSERT(last_write_res == -ENOSPC);

    KASSERT(do_unlink("/fullfile") == 0);

    KASSERT(do_write(bigfd, (void *) writebuf, S5_BLOCK_SIZE) == S5_BLOCK_SIZE);

    KASSERT(do_close(bigfd) == 0);

    KASSERT(do_unlink("/bigfile") == 0);

    dbg(DBG_TEST, "all disk space tests passed\n");
}


void run_s5fs_tests(){
    run_indirect_test();
    test_max_inodes();
    test_max_file_length();
    test_max_data();

    dbg(DBG_TESTPASS, "All s5fs tests passed!\n");
}
