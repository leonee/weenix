#include "drivers/tty/n_tty.h"

#include "errno.h"

#include "drivers/tty/driver.h"
#include "drivers/tty/ldisc.h"
#include "drivers/tty/tty.h"

#include "mm/kmalloc.h"

#include "proc/kthread.h"

#include "util/debug.h"

#include "globals.h"

/* helpful macros */
#define EOFC            '\x4'

/*
 * This is now defined in n_tty.h"
#define TTY_BUF_SIZE    128
*/
#define ldisc_to_ntty(ldisc) \
        CONTAINER_OF(ldisc, n_tty_t, ntty_ldisc)

#define IS_BACKSPACE(c) (((c) == 0x08) || ((c) == 0x07F)) 
#define IS_NEWLINE(c) (((c) == '\n') || ((c) == '\r'))
#define IS_EOM(c) ((c) == 0x04)
#define SPACE 0x20

static void n_tty_attach(tty_ldisc_t *ldisc, tty_device_t *tty);
static void n_tty_detach(tty_ldisc_t *ldisc, tty_device_t *tty);
static int n_tty_read(tty_ldisc_t *ldisc, void *buf, int len);
static const char *n_tty_receive_char(tty_ldisc_t *ldisc, char c);
static const char *n_tty_process_char(tty_ldisc_t *ldisc, char c);

static tty_ldisc_ops_t n_tty_ops = {
        .attach       = n_tty_attach,
        .detach       = n_tty_detach,
        .read         = n_tty_read,
        .receive_char = n_tty_receive_char,
        .process_char = n_tty_process_char
};

struct n_tty {
        kmutex_t            ntty_rlock;
        ktqueue_t           ntty_rwaitq;
        char               *ntty_inbuf;
        int                 ntty_rhead;
        int                 ntty_rawtail;
        int                 ntty_ckdtail;

        tty_ldisc_t         ntty_ldisc;
};


tty_ldisc_t *
n_tty_create()
{
        n_tty_t *ntty = (n_tty_t *)kmalloc(sizeof(n_tty_t));
        if (NULL == ntty) return NULL;
        ntty->ntty_ldisc.ld_ops = &n_tty_ops;
        return &ntty->ntty_ldisc;
}

void
n_tty_destroy(tty_ldisc_t *ldisc) {
        KASSERT(NULL != ldisc);
        kfree(ldisc_to_ntty(ldisc));
}

/*
 * Initialize the fields of the n_tty_t struct, allocate any memory
 * you will need later, and set the tty_ldisc field of the tty.
 */
void
n_tty_attach(tty_ldisc_t *ldisc, tty_device_t *tty) {
    struct n_tty *n_t = ldisc_to_ntty(ldisc);

    kmutex_init(&n_t->ntty_rlock);
    sched_queue_init(&n_t->ntty_rwaitq);

    /* TODO: Is this wrong? */
    n_t->ntty_inbuf = (char *) kmalloc(TTY_BUF_SIZE);

    if (n_t->ntty_inbuf == NULL){
        panic("not enough memory for buffer\n");
    }

    n_t->ntty_rhead = 0;
    n_t->ntty_rawtail = 0;
    n_t->ntty_ckdtail = 0;

    tty->tty_ldisc = ldisc;
}

/*
 * Free any memory allocated in n_tty_attach and set the tty_ldisc
 * field of the tty.
 */
void
n_tty_detach(tty_ldisc_t *ldisc, tty_device_t *tty)
{
    struct n_tty *old_n_tty = ldisc_to_ntty(tty->tty_ldisc);
 
    kfree(old_n_tty->ntty_inbuf);

    tty->tty_ldisc = ldisc;
}

static int buf_full(struct n_tty *tty){
    return ((tty->ntty_rawtail + 1) % TTY_BUF_SIZE  == tty->ntty_rhead);
}

static int has_raw_data(struct n_tty *tty){
    return tty->ntty_ckdtail != tty->ntty_rawtail;
}

static int read_buf_empty(struct n_tty *tty){
    return ((tty->ntty_rhead == tty->ntty_ckdtail));
}

/*
 * Read a maximum of len bytes from the line discipline into buf. If
 * the buffer is empty, sleep until some characters appear. This might
 * be a long wait, so it's best to let the thread be cancellable.
 *
 * Then, read from the head of the buffer up to the tail, stopping at
 * len bytes or a newline character, and leaving the buffer partially
 * full if necessary. Return the number of bytes you read into the
 * buf.

 * In this function, you will be accessing the input buffer, which
 * could be modified by other threads. Make sure to make the
 * appropriate calls to ensure that no one else will modify the input
 * buffer when we are not expecting it.
 *
 * Remember to handle newline characters and CTRL-D, or ASCII 0x04,
 * properly.
 */
int
n_tty_read(tty_ldisc_t *ldisc, void *buf, int len)
{
    char * charbuf = (char *) buf;

    struct n_tty *tty = ldisc_to_ntty(ldisc);

    kmutex_lock(&tty->ntty_rlock);
   
    int start_pos = tty->ntty_rhead;
    int bufpos = 0;
    char last_char_read = '\0';

    /* this isn't strictly necessary, but makes the logic a bit easier */
    int chars_read = 0;

    while (chars_read < len && !IS_NEWLINE(last_char_read)){

        if (read_buf_empty(tty)){
            sched_cancellable_sleep_on(&tty->ntty_rwaitq);

            /* only happens if we're cancelled */
            if (read_buf_empty(tty)){
                KASSERT(curthr->kt_cancelled == 1);
                kmutex_unlock(&tty->ntty_rlock);
                return -EINTR;
            }
        }

        /* if we've gotten here, then there's at least one character to read */
        KASSERT(!read_buf_empty(tty));

        tty->ntty_rhead = (tty->ntty_rhead + 1) % TTY_BUF_SIZE;
        last_char_read = tty->ntty_inbuf[tty->ntty_rhead];

        if (!IS_EOM(last_char_read)){
            charbuf[bufpos] = last_char_read;
            bufpos++;
        }

        chars_read++;
    }
   
    kmutex_unlock(&tty->ntty_rlock);
    return chars_read;
}

void print_buffer(struct n_tty *tty){

    dbg(DBG_TERM, "*************************\n");

    int i;

    for (i = 0; i < TTY_BUF_SIZE; i++){
        char to_print = !IS_NEWLINE(tty->ntty_inbuf[i]) ? tty->ntty_inbuf[i] : 'N';
        int rt = tty->ntty_rawtail;
        int ct = tty->ntty_ckdtail;
        int rh = tty->ntty_rhead;

        if (rt == i && ct == i && rh == i){
            dbg(DBG_TERM, "%c(rt)(ct)(rh)\n", to_print);
        } else if (rt == i && ct == i) {
            dbg(DBG_TERM, "%c(rt)(ct)\n", to_print);
        } else if (rt == i && rh == i) {
            dbg(DBG_TERM, "%c(rt)(rh)\n", to_print);
        } else if (ct == i && rh == i) {
            dbg(DBG_TERM, "%c(ct)(rh)\n", to_print);
        } else if (rt == i) {
            dbg(DBG_TERM, "%c(rt)\n", to_print);
        } else if (ct == i) {
            dbg(DBG_TERM, "%c(ct)\n", to_print);
        } else if (rh == i) {
            dbg(DBG_TERM, "%c(rh)\n", to_print);
        } else {
            dbg(DBG_TERM, "%c\n", to_print);
        }
    } 

    dbg(DBG_TERM, "*************************\n");
}

/*
 * The tty subsystem calls this when the tty driver has received a
 * character. Now, the line discipline needs to store it in its read
 * buffer and move the read tail forward.
 *
 * Special cases to watch out for: backspaces (both ASCII characters
 * 0x08 and 0x7F should be treated as backspaces), newlines ('\r' or
 * '\n'), and full buffers.
 *
 * Return a null terminated string containing the characters which
 * need to be echoed to the screen. For a normal, printable character,
 * just the character to be echoed.
 */

/* 
 * Invariants:
 *     - raw tail points to the last character written
 *     - raw head points to next char to read
 *     - cooked tail points to location of most recent newlinke character,
 *       or NTTY_BUF_SIZE if no char has been read
 */

const char *
n_tty_receive_char(tty_ldisc_t *ldisc, char c) {
    KASSERT(TTY_BUF_SIZE > 1 && "don't be a jerk");

    struct n_tty *tty = ldisc_to_ntty(ldisc);

    const char *to_ret = n_tty_process_char(ldisc, c);

    int buffer_full = buf_full(tty);

    if (IS_BACKSPACE(c)) {
        if(has_raw_data(tty)){
            tty->ntty_rawtail = (tty->ntty_rawtail - 1) % TTY_BUF_SIZE;
        }

    } else if (buffer_full){
        /* do nothing */

    } else if (IS_NEWLINE(c)){
        int new_rawtail =  (tty->ntty_rawtail + 1) % TTY_BUF_SIZE;

        tty->ntty_rawtail = new_rawtail;
        tty->ntty_ckdtail = new_rawtail;
        
        tty->ntty_inbuf[new_rawtail] = c;

        sched_wakeup_on(&tty->ntty_rwaitq);

    } else {
        tty->ntty_rawtail = (tty->ntty_rawtail + 1) % TTY_BUF_SIZE;
        tty->ntty_inbuf[tty->ntty_rawtail] = c;
    }

    
   /* print_buffer(tty);*/
    
    return to_ret;
}

/*
 * Process a character to be written to the screen.
 *
 * The only special cases are '\r' and '\n' and backspace.
 */
const char *
n_tty_process_char(tty_ldisc_t *ldisc, char c) {
    int buffer_full = buf_full(ldisc_to_ntty(ldisc));

    char *ret_text;
    
    if (IS_BACKSPACE(c)){
        /*dbg(DBG_TERM, "received a backspace\n");*/

        if (!has_raw_data(ldisc_to_ntty(ldisc))){
            return NULL;
        }

        ret_text = kmalloc(4 * sizeof(char));

        if (ret_text == NULL){
            return NULL;
        }

        ret_text[0] = c;
        ret_text[1] = SPACE;
        ret_text[2] = c;
        ret_text[3] = '\0';

    } else if (buffer_full){
        dbg(DBG_TERM, "out of buffer space\n");
        ret_text = kmalloc(sizeof(char));

        if (ret_text == NULL){
            return NULL;
        }

        ret_text[0] = '\0';

    } else if (IS_NEWLINE(c)){
        /*dbg(DBG_TERM, "receiving a newline\n");*/
        ret_text = kmalloc(3 * sizeof(char));

        if (ret_text == NULL){
            return NULL;
        }

        ret_text[0] = '\n';
        ret_text[1] = '\r';
        ret_text[2] = '\0';

    }  else {
        /*dbg(DBG_TERM, "received a char %c/%x\n", c, c);*/
        ret_text = kmalloc(2 * sizeof(char));

        if (ret_text == NULL){
            return NULL;
        }

        ret_text[0] = c;
        ret_text[1] = '\0';
    }

    return ret_text;
}
