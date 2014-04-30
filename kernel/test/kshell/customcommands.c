#include "errno.h"
#include "util/debug.h"

#include "api/exec.h"

#include "fs/open.h"
#include "fs/fcntl.h"

#include "test/kshell/io.h"

#include "proc/proc.h"
#include "proc/kthread.h"

#ifdef __VM__
static void * exec_func(int arg1, void *arg2){
    char **argv = (char **) arg2;

    argv[arg1] = NULL;

    char *func_to_run = argv[1];

    /* open stdin, stdout, stderr */
    KASSERT(do_open("/dev/tty0", O_RDONLY) == 0);
    KASSERT(do_open("/dev/tty0", O_WRONLY) == 1);
    KASSERT(do_open("/dev/tty0", O_WRONLY) == 2);
 
    char *empty_args[1] = {NULL};
    char *empty_envp[1] = {NULL};
    kernel_execve(func_to_run, argv, empty_envp);

    panic("returned when you weren't supposed to!");

    return NULL;
}

int kshell_exec(kshell_t *ksh, int argc, char **argv){
    KASSERT(NULL != ksh);
    KASSERT(NULL != argv);

    if (argc < 2){
        kprintf(ksh, "Usage: exec <command>\n");
        return 1;
    }

    proc_t *execproc = proc_create("exec_proc");

    kthread_t *execthread = kthread_create(execproc, exec_func, argc, argv);

    sched_make_runnable(execthread);

    int status;
    do_waitpid(execproc->p_pid, 0, &status);

    return 0;
}
#endif
