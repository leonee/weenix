#include "util/debug.h"

#include "drivers/bytedev.h"
#include "mm/kmalloc.h"
#include "drivers/tty/tty.h"
#include "drivers/tty/n_tty.h"

#include "proc/proc.h"

#define bd_to_tty(bd) \
        CONTAINER_OF(bd, tty_device_t, tty_cdev)

char newline1[] = {'\n'};
char newline2[] = {'\r'};

static void write_char(bytedev_t *bd, char c){
    tty_global_driver_callback((void *) bd_to_tty(bd), c);
}

static void write_chars(bytedev_t *bd, int count, char c){
    int i;
    for (i = 0; i < count; i++){
        write_char(bd, c);
    }
}

static void simple_ld_test(bytedev_t *bd){
    dbg(DBG_TEST, "testing simple read/write functionality\n");

    write_chars(bd, 10, 'a');
    write_char(bd, '\n');
    write_chars(bd, 15, 'a'); 
    write_char(bd, '\r');
    write_chars(bd, 5, 'a');

    /* buffer now has 10 a's, newline, 15 a's, newline, 5 a's */ 
    char *read_buf = (char *) kmalloc(1000 * sizeof(char));

    if (read_buf == NULL){
        panic("kmalloc failed while running test\n");
    }

    /* make sure we're only reading up until newlines (both types) */
    KASSERT(bd->cd_ops->read(bd, 0, read_buf, 1000) == 11);
    KASSERT(bd->cd_ops->read(bd, 0, (read_buf + 11), 1000) == 16);

    int i;
    for (i = 0; i < 10; i++){
        KASSERT(read_buf[i] == 'a');
    }

    KASSERT(read_buf[10] == '\n');

    int j;
    for (j = 11; j < 26; j++){
        KASSERT(read_buf[j] == 'a');
    }

    KASSERT(read_buf[26] == '\r');

    /* read everything else to "clear" the buffer for future tests */
    write_char(bd, '\n');
    bd->cd_ops->read(bd, 0, read_buf, 1000);

    kfree((void *) read_buf);

    dbg(DBG_TESTPASS, "simple ld test passed\n");
}

static void * read_chars(int c, void *arg2){
    dbg(DBG_TEST, "attempting to read char %c from buffer\n", c);
    bytedev_t *bd = (bytedev_t *) arg2;

    char readbuf[20];

    int i;
    for(i = 0; i < 4; i++){
        bd->cd_ops->read(bd, 0, readbuf, 20);
        
        int j;
        for (j = 0; j < 10; j++){
            KASSERT(readbuf[j] == c);
        }

        KASSERT(readbuf[10] == '\n');
    }

    return NULL;
}

static void * write_to_buf(int arg1, void *arg2){

    bytedev_t *bd = (bytedev_t *) arg2;

    int i;
    for(i = 0; i < 4; i++){
        write_chars(bd, 10, 'a');
        write_char(bd, '\n');
        yield();
        write_chars(bd, 10, 'b');
        write_char(bd, '\n');
        yield();
    }

    return NULL;
}

static void multithreaded_read_test(bytedev_t *bd){
    dbg(DBG_TEST, "testing multithreaded tty reads and writes\n");

   
    proc_t *p1 = proc_create("multithreaded_reading_proc_1");
    kthread_t *t1 = kthread_create(p1, read_chars, 'a', (void *) bd);

    proc_t *p2 = proc_create("multithreaded_reading_proc_2");
    kthread_t *t2 = kthread_create(p2, read_chars, 'b', (void *) bd);

    proc_t *writer = proc_create("multithreaded_reading_writer");
    kthread_t *writer_thread = kthread_create(writer, write_to_buf, 0, bd);

    sched_make_runnable(t1);
    sched_make_runnable(t2);
    sched_make_runnable(writer_thread);

    int status;
    do_waitpid(p1->p_pid, 0, &status);
    do_waitpid(p2->p_pid, 0, &status);
    do_waitpid(writer->p_pid, 0, &status);

    dbg(DBG_TESTPASS, "all multithreaded tty reading tests passed\n");
}

void stress_test(bytedev_t *bd){
    dbg(DBG_TESTPASS, "stress testing tty\n");

    int rw_size = (5.0/6.0) * TTY_BUF_SIZE;

    char readbuf[400];

    /* first write */
    write_chars(bd, rw_size, 'a');
    write_char(bd, '\n');

    int chars_read = bd->cd_ops->read(bd, 0, readbuf, rw_size + 10);

    KASSERT(chars_read == rw_size + 1);

    int i;
    for (i = 0; i < rw_size; i++){
        KASSERT(readbuf[i] == 'a');
    }

    KASSERT(readbuf[rw_size] == '\n');

    /* second write */
    write_chars(bd, rw_size, 'b');
    write_char(bd, '\n');

    int chars_read2 = bd->cd_ops->read(bd, 0, readbuf, rw_size + 10);

    KASSERT(chars_read2 == rw_size + 1);

    int j;
    for (j = 0; j < rw_size; j++){
        KASSERT(readbuf[j] == 'b');
    }

    KASSERT(readbuf[rw_size] == '\n');
}

void test_line_discipline(){
    dbg(DBG_TEST, "testing line discipline\n");

    bytedev_t *bd = bytedev_lookup(MKDEVID(TTY_MAJOR, 0));

    KASSERT(bd != NULL && "couldn't find tty");

    simple_ld_test(bd);
    multithreaded_read_test(bd);
    stress_test(bd);
}


void run_tty_tests(){
    test_line_discipline();
    dbg(DBG_TESTPASS, "all tty tests passed!\n");
}

