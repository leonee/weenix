#include "proc/proc.h"
#include "util/list.h"
#include "proc/sched.h"
#include "globals.h"
#include "util/debug.h"
#include "errno.h"
#include "proc/kmutex.h"
#include "test/kshell/kshell.h"

#define NUM_PROCS 3

typedef enum {ANY, SPECIFIC} waitpid_type_t;

static void * simple_function(int arg1, void *arg2);
static void * test_proc_kill_all_func(int arg1, void *arg2);
    
static int in_child_list(proc_t *myproc){
    list_link_t *link;
    list_t *child_list = &myproc->p_pproc->p_children;

    for (link = child_list->l_next; link != child_list; link = link->l_next){
        proc_t *p = list_item(link, proc_t, p_child_link);
        if (p == myproc){
            return 1;
        }
    }

    return 0;
}

/*
 * Should be called from the init proc 
 */
static void test_proc_create(){
    dbg(DBG_TEST, "testing proc_create\n");

    proc_t *myproc = proc_create("myproc");

    KASSERT(list_empty(&myproc->p_threads));
    KASSERT(list_empty(&myproc->p_children));

    KASSERT(sched_queue_empty(&myproc->p_wait));

    KASSERT(myproc->p_pproc->p_pid == 1 && "created proc's parent isn't the init proc\n");
    KASSERT(myproc->p_state == PROC_RUNNING);

    /* make sure it's in the proc list */
    KASSERT(proc_lookup(myproc->p_pid) == myproc && "created proc not in proc list\n");

    /* make sure it's in it's parent's child list */
    KASSERT(in_child_list(myproc));

    /* clean everything up */
    kthread_t *mythread = kthread_create(myproc, simple_function, NULL, NULL);
    sched_make_runnable(mythread);

    int status;
    do_waitpid(myproc->p_pid, 0, &status);
    
    dbg(DBG_TESTPASS, "all proc_create tests passed!\n");
}

/* 
 * A simple function
 */
static void * simple_function(int arg1, void *arg2){
    dbg(DBG_TEST, "Running a simple method from test thread %d\n", arg1);
    dbg(DBG_TEST, "Exiting a simple method from test thread %d\n", arg1);

    return NULL;
}

static void test_do_waitpid(waitpid_type_t type){
    proc_t *test_procs[NUM_PROCS];
    kthread_t *test_threads[NUM_PROCS];

    int i;
    for (i = 0; i < NUM_PROCS; i++){
        test_procs[i] = proc_create("test proc");
        test_threads[i] = kthread_create(test_procs[i], simple_function, i, NULL);
        sched_make_runnable(test_threads[i]);
    }

    int j;

    for (j = 0; j < NUM_PROCS; j++){

        if (type == ANY){
            int status;
            do_waitpid(-1, 0, &status);
        } else {
            int status;
            pid_t proc_pid = test_procs[j]->p_pid;

            pid_t waitpid_pid = do_waitpid(proc_pid, 0, &status);

            KASSERT(waitpid_pid == proc_pid);
        }
    }

    int k;
    for (k = 0; k < NUM_PROCS; k++){
        proc_t *p = test_procs[k];

        KASSERT(proc_lookup(p->p_pid) == NULL);

        /* make sure all children have been reparented */
        KASSERT(list_empty(&p->p_children));

        /* make sure that it is no longer in it's parent's
         * child list
         */
        KASSERT(!in_child_list(p));

        /* make sure it exited with the correct status */
        KASSERT(p->p_status == 0);

        KASSERT(p->p_state == PROC_DEAD);

        KASSERT(sched_queue_empty(&p->p_wait));
    }
}

static void test_do_waitpid_no_child(){

    pid_t pid;

    /* find a PID that definitely isn't a child of curproc */
    for (pid = 0; proc_lookup(pid) != NULL; pid++){}
    
    int status;

    pid_t returned_pid = do_waitpid(pid, 0, &status);

    KASSERT(returned_pid = -ECHILD);
} 

/*
 * Should be called from a new process
 */
static void * test_do_exit_and_do_waitpid(int arg1, void *arg2){
    int status;

    dbg(DBG_TEST, "testing do_waitpid on an invalid PID\n");
    KASSERT(do_waitpid(-1, 0, &status) == -ECHILD);

    dbg(DBG_TEST, "testing do_waitpid on an empty child list\n");
    KASSERT(do_waitpid(5, 0, &status) == -ECHILD);

    dbg(DBG_TEST, "testing do_waitpid with pid == -1\n");
    test_do_waitpid(ANY);

    dbg(DBG_TEST, "testing do waitpid with specific pids\n");
    test_do_waitpid(SPECIFIC);

    dbg(DBG_TEST, "testing do_waitpid with non-child pid\n");
    test_do_waitpid_no_child();

    dbg(DBG_TESTPASS, "all do_waitpid tests passed!\n");

    return NULL;
}

static void * sleep_function(int arg1, void *arg2){
    dbg(DBG_TEST, "going to sleep...\n");
    sched_cancellable_sleep_on((ktqueue_t *) arg2);
    dbg(DBG_TEST, "awoken from sleep!\n");

    return NULL;
}


static void test_kthread_cancel(){
    dbg(DBG_TEST, "testing kthread_cancel\n");

    proc_t *test_proc = proc_create("kthread_cancel_test_proc");
    kthread_t *test_thread = kthread_create(test_proc, sleep_function, NULL,
                                        (void *) &test_proc->p_wait);

    sched_make_runnable(test_thread);

    /* make sure the thread goes to sleep before we cancel it */
    yield();

    kthread_cancel(test_thread, (void *) 5);

    KASSERT(test_thread->kt_cancelled == 1);
    KASSERT((int) test_thread->kt_retval == 5);

    int status;
    do_waitpid(test_proc->p_pid, 0, &status);

    dbg(DBG_TESTPASS, "all kthread_cancel tests passed!\n");
}

static void test_proc_kill(){
    dbg(DBG_TEST, "testing proc_kill\n");

    proc_t *test_proc = proc_create("proc_kill_test_proc");
    kthread_t *test_thread = kthread_create(test_proc, sleep_function, NULL,
                                        (void *) &test_proc->p_wait);

    sched_make_runnable(test_thread);

    yield();

    proc_kill(test_proc, 7);

    KASSERT(test_thread->kt_cancelled == 1);
    KASSERT(test_thread->kt_retval == 0);
    KASSERT(test_proc->p_status == 7);

    int status;
    do_waitpid(test_proc->p_pid, 0, &status);

    dbg(DBG_TESTPASS, "all proc_kill tests passed!\n");
}

static void * test_proc_kill_all_func(int arg1, void *arg2){

    proc_t *test_procs[NUM_PROCS];
    kthread_t *test_threads[NUM_PROCS];

    int i;
    for (i = 0; i < NUM_PROCS; i++){
        test_procs[i] = proc_create("proc_kill_all test proc");
        test_threads[i] = kthread_create(test_procs[i], sleep_function, NULL,
                                    (void *) &test_procs[i]->p_wait);

        sched_make_runnable(test_threads[i]);
    }

    yield();

    proc_kill_all();

    /* if we get here, then we didn't call do_exit() in 
     * proc_kill_all(), meaning that we must have called
     * proc_kill_all() from the init_proc
     */
    KASSERT(curproc->p_pid == 1);

    int j;
    for (j = 0; j < NUM_PROCS; j++){
        KASSERT(test_threads[j]->kt_cancelled == 1);
        KASSERT(test_threads[j]->kt_retval == 0);
        KASSERT(test_procs[j]->p_status == 0);

        int status;
        do_waitpid(test_procs[j]->p_pid, 0, &status);
    }

    return NULL;
}

static void test_proc_kill_all(){
    dbg(DBG_TEST, "testing proc_kill_all when called from init proc\n");
    test_proc_kill_all_func(NULL, NULL);

    dbg(DBG_TEST, "testing proc_kill_all when called from a different proc\n");

    proc_t *test_proc = proc_create("proc_kill_all_func caller");
    kthread_t *test_thread = kthread_create(test_proc, test_proc_kill_all_func, 
                                       NULL, NULL);

    sched_make_runnable(test_thread);

    int status;
    pid_t retpid = do_waitpid(test_proc->p_pid, 0, &status);
    KASSERT(retpid == test_proc->p_pid);

    int i;
    for (i = 0; i < NUM_PROCS; i++){
        pid_t retval = do_waitpid(-1, 0, &status);
        
        /* make sure we actually were able to wait on this pid,
         * meaning that it was properly killed in proc_kill_all
         */
        KASSERT(retval > 0);
    }

    dbg(DBG_TESTPASS, "all proc_kill_all tests passed!\n");
}

static void * lock_kmutex_func(int arg1, void *arg2){

    kmutex_t *m = (kmutex_t *) arg2;

    kmutex_lock(m);
    kmutex_unlock(m);

    return NULL;
}

static void test_normal_locking(){
    dbg(DBG_TEST, "testing normal mutex behavior\n");

    kmutex_t m;
    kmutex_init(&m);

    proc_t *kmutex_proc = proc_create("kmutex_test_proc");
    kthread_t *kmutex_thread = kthread_create(kmutex_proc, lock_kmutex_func,
                                          NULL, (void *) &m);

    sched_make_runnable(kmutex_thread);

    kmutex_lock(&m);
    
    /* let kmutex_proc attempt to lock the mutex */
    yield();

    kmutex_unlock(&m);

    /* lock and unlock the mutex with nobody on it's wait queue */
    kmutex_lock(&m);
    kmutex_unlock(&m);

    int status;
    do_waitpid(kmutex_proc->p_pid, 0, &status);

    dbg(DBG_TESTPASS, "normal kmutex tests passed!\n");
}

/* The thread executing this MUST be cancelled before it succesfully 
 * obtains the mutex. Otherwise, bad things will happen
 */
static void * cancellable_lock_kmutex(int arg1, void *arg2){

    kmutex_t *m = (kmutex_t *) arg2;

    int lock_result = kmutex_lock_cancellable(m);
    
    KASSERT(lock_result == -EINTR);
    KASSERT(m->km_holder == NULL);
    KASSERT(sched_queue_empty(&m->km_waitq));

    return NULL;
}

static void test_locking_and_cancelling(){
    dbg(DBG_TEST, "testing kmutex behavior with cancellation\n");

    kmutex_t m;
    kmutex_init(&m);

    proc_t *kmutex_proc = proc_create("kmutex_sleep_test_proc");
    kthread_t *kmutex_thread =  kthread_create(kmutex_proc, 
                                        cancellable_lock_kmutex,
                                        NULL, 
                                        (void *) &m);

    sched_make_runnable(kmutex_thread);

    kmutex_lock(&m);

    /* let kmutex_proc attempt to lock the mutex */
    yield();

    kthread_cancel(kmutex_thread, 0);

    kmutex_unlock(&m);

    int status;
    do_waitpid(kmutex_proc->p_pid, 0, &status);

    dbg(DBG_TESTPASS, "kmutex cancellation tests passed!\n");
}

static void test_kmutex(){
    test_normal_locking();
    test_locking_and_cancelling();

    dbg(DBG_TESTPASS, "kmutex tests passed!\n");
}

void run_proc_tests(){

    test_proc_create();

    proc_t *waitpid_test_proc = proc_create("waitpid_test_proc");
    kthread_t *waitpid_test_thread = kthread_create(waitpid_test_proc,
            test_do_exit_and_do_waitpid, NULL, NULL);

    sched_make_runnable(waitpid_test_thread);

    int status;
    do_waitpid(waitpid_test_proc->p_pid, 0, &status);

    test_kthread_cancel();

    test_proc_kill();
    test_proc_kill_all();

    test_kmutex();

    dbg(DBG_TESTPASS, "all proc-related tests passed!\n");
}

int proctests(kshell_t *k, int argc, char **argv){
    run_proc_tests();
    return 0;
}
