#include "kernel.h"
#include "config.h"
#include "globals.h"
#include "errno.h"

#include "util/debug.h"
#include "util/list.h"
#include "util/string.h"
#include "util/printf.h"

#include "proc/kthread.h"
#include "proc/proc.h"
#include "proc/sched.h"
#include "proc/proc.h"

#include "mm/slab.h"
#include "mm/page.h"
#include "mm/mmobj.h"
#include "mm/mm.h"
#include "mm/mman.h"

#include "vm/vmmap.h"

#include "fs/vfs.h"
#include "fs/vfs_syscall.h"
#include "fs/vnode.h"
#include "fs/file.h"

proc_t *curproc = NULL; /* global */
static slab_allocator_t *proc_allocator = NULL;

static list_t _proc_list;
static proc_t *proc_initproc = NULL; /* Pointer to the init process (PID 1) */

void
proc_init()
{
        list_init(&_proc_list);
        proc_allocator = slab_allocator_create("proc", sizeof(proc_t));
        KASSERT(proc_allocator != NULL);
}

static pid_t next_pid = 0;

/**
 * Returns the next available PID.
 *
 * Note: Where n is the number of running processes, this algorithm is
 * worst case O(n^2). As long as PIDs never wrap around it is O(n).
 *
 * @return the next available PID
 */
static int
_proc_getid()
{
        proc_t *p;
        pid_t pid = next_pid;
        while (1) {
failed:
                list_iterate_begin(&_proc_list, p, proc_t, p_list_link) {
                        if (p->p_pid == pid) {
                                if ((pid = (pid + 1) % PROC_MAX_COUNT) == next_pid) {
                                        return -1;
                                } else {
                                        goto failed;
                                }
                        }
                } list_iterate_end();
                next_pid = (pid + 1) % PROC_MAX_COUNT;
                return pid;
        }
}

/*
 * The new process, although it isn't really running since it has no
 * threads, should be in the PROC_RUNNING state.
 *
 * dON'T FORget to set proc_initproc when you create the init
 * process. You will need to be able to reference the init process
 * when reparenting processes to the init process.
 */
proc_t *
proc_create(char *name)
{
    proc_t *p = slab_obj_alloc(proc_allocator);
    
    /* put this proc in the proc list */
    list_link_init(&p->p_list_link)

    list_insert_head(&_proc_list, &p->p_list_link); 

    p->p_pid = (pid_t) _proc_getid(); 

    list_init(&p->p_threads);
    list_init(&p->p_children);
    p->p_pproc = curproc;

    p->p_state = PROC_RUNNING;
    sched_queue_init(&p->p_wait);  
   
    p->p_pagedir = pt_create_pagedir(); 
  
    /* initialize the name */    
    char *correct_name = name ? name : "Unnamed process";

    int i;
    for (i = 0; i < PROC_NAME_LEN && name[i]; i++){
        p->p_comm[i] = name[i];
    }

    if (i < PROC_NAME_LEN){
        p->p_comm[i] = '\0';
    } else {
        p->p_comm[PROC_NAME_LEN] = '\0';
    }

    list_link_init(&p->p_child_link);
    
    if (p->p_pid != 0){
        /*     make sure we're not giving the idle proc a parent */
        KASSERT(p->p_pid != (pid_t) 0);
        
        list_insert_head(&p->p_pproc->p_children, &p->p_child_link); 

        dbg_print("pid of proc:%d\n", p->p_pid);
    }
   
    

    /* if the init proc, set the global variable */
    if (p->p_pid == (pid_t) 1){
        proc_initproc = p;
    }

    return p;
}

/**
 * Makes a process a child of the init
 * proc.
 */
static void reparent_proc(proc_t *p){
    p->p_pproc = proc_initproc;
    list_insert_tail(&proc_initproc->p_children, &p->p_list_link);
}

/**
 * Reparents an entire list of processes to the init proc
 */
/* TODO: make sure this works */
static void reparent_all_children(list_t *children){
    dbg_print("reparenting children. curproc = %s\n", curproc->p_comm);
    
    list_link_t *link = children->l_next;

    while (link && link != children){
        proc_t *p = list_item(link, proc_t, p_child_link);
        link = link->l_next;

        reparent_proc(p);
    }
}

/**
 * Cleans up as much as the process as can be done from within the
 * process. This involves:
 *    - Closing all open files (VFS)
 *    - Cleaning up VM mappings (VM)
 *    - Waking up its parent if it is waiting
 *    - Reparenting any children to the init process
 *    - Setting its status and state appropriately
 *
 * The parent will finish destroying the process within do_waitpid (make
 * sure you understand why it cannot be done here). Until the parent
 * finishes destroying it, the process is informally called a 'zombie'
 * process.
 *
 * This is also where any children of the current process should be
 * reparented to the init process (unless, of course, the current
 * process is the init process. However, the init process should not
 * have any children at the time it exits).
 *
 * Note: You do _NOT_ have to special case the idle process. It should
 * never exit this way.
 *
 * @param status the status to exit the process with
 */
void
proc_cleanup(int status)
{
    /* Reparent all the children */
    list_t *children = &curproc->p_children;

    if (!list_empty(children)){
        KASSERT(curproc != proc_initproc && "initproc still has children!!!");
        reparent_all_children(children);        
    }

    curproc->p_status = status;
    
    curproc->p_state = PROC_DEAD;
    
    sched_wakeup_on(&curproc->p_pproc->p_wait);
}

/*
 * This has nothing to do with signals and kill(1).
 *
 * Calling this on the current process is equivalent to calling
 * do_exit().
 *
 * In Weenix, this is only called from proc_kill_all.
 */
void
proc_kill(proc_t *p, int status)
{
        NOT_YET_IMPLEMENTED("PROCS: proc_kill");
}

/*
 * Remember, proc_kill on the current process will _NOT_ return.
 * Don't kill direct children of the idle process.
 *
 * In Weenix, this is only called by sys_halt.
 */
void
proc_kill_all()
{
        NOT_YET_IMPLEMENTED("PROCS: proc_kill_all");
}

proc_t *
proc_lookup(int pid)
{
        proc_t *p;
        list_iterate_begin(&_proc_list, p, proc_t, p_list_link) {
                if (p->p_pid == pid) {
                        return p;
                }
        } list_iterate_end();
        return NULL;
}

list_t *
proc_list()
{
        return &_proc_list;
}

/*
 * This function is only called from kthread_exit.
 *
 * Unless you are implementing MTP, this just means that the process
 * needs to be cleaned up and a new thread needs to be scheduled to
 * run. If you are implementing MTP, a single thread exiting does not
 * necessarily mean that the process should be exited.
 */
void
proc_thread_exited(void *retval)
{
    proc_cleanup(retval);
    curthr->kt_state = KT_EXITED;
    sched_switch();
}

/**
 * This morbid function finds a dead child,
 * of the current proc, and returns a pointer
 * to the child proc. If multiple children are dead,
 * it is unspecified which child this function returns.
 *
 * @return a pointer to a dead child of the current proc, or
 *         NULL if no such child exists
 */
static proc_t *find_dead_child(){
    list_link_t *link;
    list_t *children = &curproc->p_children;

    for (link = children->l_next; link != children; link = link->l_next){
        proc_t *p = list_item(link, proc_t, p_child_link);

        if (p->p_state == PROC_DEAD){
            return p;
        }
    }

    return NULL;
}

/* disposes of the remaining resources of a
 * cancelled child proc
 */
static void cleanup_child_proc(proc_t *p){
    KASSERT(p->p_state == PROC_DEAD && "attempting to clean up a running process\n");

    list_t *threads = &p->p_threads;
    list_link_t *link = threads->l_next;

    while (link != threads){
        kthread_t *t = list_item(link, kthread_t, kt_plink);
        link = link->l_next;
        kthread_destroy(t);
    }
 
    pt_destroy_pagedir(p->p_pagedir);
}

/**
 * A helper function for do_waitpid, which is invoked if
 * the pid argument to do_waitpid is -1. This function waits
 * for any child of the current process to exit, cleans it up,
 * puts its exit value into status, and returns its pid
 */
static pid_t do_waitpid_any(int *status){
    proc_t *dead_child = find_dead_child();

    if (dead_child == NULL){
        int wait_result = sched_cancellable_sleep_on(&curproc->p_wait);

        if (wait_result == -EINTR){
            proc_cleanup(-1);
            return -ECHILD;
        }
        
        /* we only have to do this once, because this process
         * will only wake up if a child exits 
         */
        dead_child = find_dead_child();
    }

    cleanup_child_proc(dead_child);

    status = &dead_child->p_status;
    return dead_child->p_pid;
}

/* returns true if a process with the given pid
 * is in the given list, and false otherwise
 */
static int is_child(pid_t pid, list_t *child_list){
    list_link_t *link;

    for (link = child_list->l_next; link != child_list; link = link->l_next){
        proc_t *p = list_item(link, proc_t, p_child_link);

        if (p->p_pid == pid){
            return 1;
        }
    }

    return 0;
}

/**
 * A helper function for do_waitpid, which is invoked if
 * the pid argument to do_waitpid is > 0. This function waits
 * for the child of the curproc with a pid of pid to exit, cleans it up,
 * puts its exit value into status, and returns its pid
 */
static pid_t do_waitpid_specific(pid_t pid, int *status){
    list_t *children = &curproc->p_children;
    list_link_t *link;

    proc_t *p;

    for (link = children->l_next; link != children; link = link->l_next){
        p = list_item(link, proc_t, p_child_link);

        if (p->p_pid == pid){
            break;
        }
    }

    KASSERT(p != NULL && "given proc isn't a child of curproc!!!\n");

    while (p->p_state != PROC_DEAD){
        int wait_result = sched_cancellable_sleep_on(&curproc->p_wait);

        if (wait_result == -EINTR){
            proc_cleanup(-1);
            return -ECHILD;
        }
    }

    cleanup_child_proc(p);

    status = &p->p_status;
    return p->p_pid;
}

/* If pid is -1 dispose of one of the exited children of the current
 * process and return its exit status in the status argument, or if
 * all children of this process are still running, then this function
 * blocks on its own p_wait queue until one exits.
 *
 * If pid is greater than 0 and the given pid is a child of the
 * current process then wait for the given pid to exit and dispose
 * of it.
 *
 * If the current process has no children, or the given pid is not
 * a child of the current process return -ECHILD.
 *
 * Pids other than -1 and positive numbers are not supported.
 * Options other than 0 are not supported.
 */
pid_t
do_waitpid(pid_t pid, int options, int *status)
{
    KASSERT(options == 0);

    pid_t ret_pid;

    if (pid < -1){
        ret_pid = -ECHILD;
    } else  if (list_empty(&curproc->p_children)){
        ret_pid = -ECHILD;
    } else  if (pid == (pid_t) -1){
        ret_pid = do_waitpid_any(status);      
    } else if (is_child(pid, &curproc->p_children)){
        ret_pid = do_waitpid_specific(pid, status);
    } else {
        ret_pid = -ECHILD;
    }

    return ret_pid;
}

/*
 * Cancel all threads, join with them, and exit from the current
 * thread.
 *
 * @param status the exit status of the process
 */
void
do_exit(int status)
{
    list_link_t *link;
    list_t *threads = &curproc->p_threads;

    for (link = threads->l_next; link != threads; link = link->l_next){
        kthread_t *t = list_item(link, kthread_t, kt_plink);
        if (t != curthr){
            panic("trying to cancel curproc's thread, but it isn't the\
                   curr thread\n");
            kthread_cancel(t, 0);
        }
    }

    curproc->p_status = status;

    kthread_exit(0);
}

size_t
proc_info(const void *arg, char *buf, size_t osize)
{
        const proc_t *p = (proc_t *) arg;
        size_t size = osize;
        proc_t *child;

        KASSERT(NULL != p);
        KASSERT(NULL != buf);

        iprintf(&buf, &size, "pid:          %i\n", p->p_pid);
        iprintf(&buf, &size, "name:         %s\n", p->p_comm);
        if (NULL != p->p_pproc) {
                iprintf(&buf, &size, "parent:       %i (%s)\n",
                        p->p_pproc->p_pid, p->p_pproc->p_comm);
        } else {
                iprintf(&buf, &size, "parent:       -\n");
        }

#ifdef __MTP__
        int count = 0;
        kthread_t *kthr;
        list_iterate_begin(&p->p_threads, kthr, kthread_t, kt_plink) {
                ++count;
        } list_iterate_end();
        iprintf(&buf, &size, "thread count: %i\n", count);
#endif

        if (list_empty(&p->p_children)) {
                iprintf(&buf, &size, "children:     -\n");
        } else {
                iprintf(&buf, &size, "children:\n");
        }
        list_iterate_begin(&p->p_children, child, proc_t, p_child_link) {
                iprintf(&buf, &size, "     %i (%s)\n", child->p_pid, child->p_comm);
        } list_iterate_end();

        iprintf(&buf, &size, "status:       %i\n", p->p_status);
        iprintf(&buf, &size, "state:        %i\n", p->p_state);

#ifdef __VFS__
#ifdef __GETCWD__
        if (NULL != p->p_cwd) {
                char cwd[256];
                lookup_dirpath(p->p_cwd, cwd, sizeof(cwd));
                iprintf(&buf, &size, "cwd:          %-s\n", cwd);
        } else {
                iprintf(&buf, &size, "cwd:          -\n");
        }
#endif /* __GETCWD__ */
#endif

#ifdef __VM__
        iprintf(&buf, &size, "start brk:    0x%p\n", p->p_start_brk);
        iprintf(&buf, &size, "brk:          0x%p\n", p->p_brk);
#endif

        return size;
}

size_t
proc_list_info(const void *arg, char *buf, size_t osize)
{
        size_t size = osize;
        proc_t *p;

        KASSERT(NULL == arg);
        KASSERT(NULL != buf);

#if defined(__VFS__) && defined(__GETCWD__)
        iprintf(&buf, &size, "%5s %-13s %-18s %-s\n", "PID", "NAME", "PARENT", "CWD");
#else
        iprintf(&buf, &size, "%5s %-13s %-s\n", "PID", "NAME", "PARENT");
#endif

        list_iterate_begin(&_proc_list, p, proc_t, p_list_link) {
                char parent[64];
                if (NULL != p->p_pproc) {
                        snprintf(parent, sizeof(parent),
                                 "%3i (%s)", p->p_pproc->p_pid, p->p_pproc->p_comm);
                } else {
                        snprintf(parent, sizeof(parent), "  -");
                }

#if defined(__VFS__) && defined(__GETCWD__)
                if (NULL != p->p_cwd) {
                        char cwd[256];
                        lookup_dirpath(p->p_cwd, cwd, sizeof(cwd));
                        iprintf(&buf, &size, " %3i  %-13s %-18s %-s\n",
                                p->p_pid, p->p_comm, parent, cwd);
                } else {
                        iprintf(&buf, &size, " %3i  %-13s %-18s -\n",
                                p->p_pid, p->p_comm, parent);
                }
#else
                iprintf(&buf, &size, " %3i  %-13s %-s\n",
                        p->p_pid, p->p_comm, parent);
#endif
        } list_iterate_end();
        return size;
}
