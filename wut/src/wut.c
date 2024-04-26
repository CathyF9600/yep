#include "wut.h"

#include <assert.h> // assert
#include <errno.h> // errno
#include <stddef.h> // NULL
#include <stdio.h> // perror
#include <stdlib.h> // reallocarray
#include <sys/mman.h> // mmap, munmap
#include <sys/signal.h> // SIGSTKSZ
#include <sys/queue.h> // TAILQ_*
#include <ucontext.h> // getcontext, makecontext, setcontext, swapcontext
#include <valgrind/valgrind.h> // VALGRIND_STACK_REGISTER
#include <string.h>
#include <stdbool.h>

// #define STACK_SIZE SIGSTKSZ
/* Define a queue of TCBs for threads */
typedef struct tcb{
    int tid;
    ucontext_t *context;
    int status;
    int init; // whether it's init or not
    int waiter_id; //id_waiting_on_us // id of the thread that is waiting on this thread
    int waiting_id; // id_waiting_on
    bool has_exited; // flag indicating implicit exit
    struct list_entry* queue_entry;
    TAILQ_ENTRY(tcb) entries; // pointer to the tcb's location in ready queue
} tcb_t;
TAILQ_HEAD(tcb_queue_head, tcb);
static struct tcb_queue_head tcb_queue_head;

/* Define a queue of threads */
struct list_entry {
    int id;
    TAILQ_ENTRY(list_entry) entries;
};
TAILQ_HEAD(list_head, list_entry);
static struct list_head list_head;

// vars
struct tcb *tcb0; // make the first tcb created by init global
int num_threads;

/* create a clean up context */
static ucontext_t *cleanup_ctx;
// static char *cleanup_stack;

static void die(const char* message) {
    int err = errno;
    perror(message);
    exit(err);
}

static char* new_stack(void) {
    char* stack = mmap(
        NULL,
        SIGSTKSZ,
        PROT_READ | PROT_WRITE | PROT_EXEC,
        MAP_ANONYMOUS | MAP_PRIVATE,
        -1,
        0
    );
    if (stack == MAP_FAILED) {
        die("mmap stack failed");
    }
    VALGRIND_STACK_REGISTER(stack, stack + SIGSTKSZ);
    return stack;
}

static void delete_stack(char* stack) {
    if (munmap(stack, SIGSTKSZ) == -1) {
        die("munmap stack failed");
    }
}

static void _exit_cleanup(void) {
    /* Get the thread that just finished */
    struct list_entry* thread_finished = TAILQ_FIRST(&list_head);
    /* Remove the finished thread from the queue */
    TAILQ_REMOVE(&list_head, thread_finished, entries);
    int id = thread_finished->id;
    struct tcb *entry;
    /* Find the tcb for the same thread */
    TAILQ_FOREACH(entry, &tcb_queue_head, entries) {
        if (entry->tid == id){ break; }
    }
    /* Update status */
    entry->has_exited = 1;
    /* Check if the thread is waited on; if so, add the calling thread to the back of the queue */
    if (entry->waiter_id != -1) { //waiter_id stores the waiter's id
        struct tcb *caller_thread;
        TAILQ_FOREACH(caller_thread, &tcb_queue_head, entries) {
            if (caller_thread->tid == entry->waiter_id) {
                // insert the waiter back to the queue
                TAILQ_INSERT_TAIL(&list_head, caller_thread->queue_entry, entries);
                break;
            }
        }
    }
    /* If the thread queue is empty, clean up tcb queue's stack then exit(0) */
    if (TAILQ_EMPTY(&list_head)) {
        static struct tcb* each;
        TAILQ_FOREACH(each, &tcb_queue_head, entries) {
            TAILQ_REMOVE(&tcb_queue_head, each, entries);
            if(each->init == 0) {
                delete_stack(each->context->uc_stack.ss_sp);
            }
            free(each->context);
            free(each->queue_entry);
            free(each);
        }
        exit(0);
    }
    
    /* If the queue is not empty, switch to the next thread in the queue */
    struct list_entry *next_thread = TAILQ_FIRST(&list_head);
    struct tcb *next_tcb;
    TAILQ_FOREACH(next_tcb, &tcb_queue_head, entries) {
        // printf(" %d", e->id);
        if(next_tcb->tid == next_thread->id){
            break; //do not reset the other thread's "id waiting on status" yet. That should only finish reset once the thread regains control
        }
    }
    int err = setcontext(next_tcb->context);
    if(err == -1) {
        die("switch context failed in cleanup.");
    }
}
    

void wut_init() {
    /* Set up list entries */
    TAILQ_INIT(&list_head);
    assert(TAILQ_EMPTY(&list_head));
    struct list_entry* e0 = (struct list_entry*) malloc(sizeof(struct list_entry));
    e0->id = 0;
    TAILQ_INSERT_TAIL(&list_head, e0, entries);

    /* setup tcb0 */
    TAILQ_INIT(&tcb_queue_head);
    assert(TAILQ_EMPTY(&tcb_queue_head));
    struct tcb* tcb0 = (struct tcb*) malloc(sizeof(struct tcb));
    // tcb0->stack = new_stack();
    tcb0->context = (ucontext_t*)malloc(sizeof(ucontext_t));
    getcontext(tcb0->context);
    tcb0->status = 0;
    // tcb0->context->uc_link = NULL; // if context is a pointer
    tcb0->tid = 0;
    tcb0->queue_entry = e0;
    tcb0->waiter_id = -1;
    tcb0->waiting_id = -1;
    tcb0->has_exited = 0;
    tcb0->init = 1;
    TAILQ_INSERT_TAIL(&tcb_queue_head, tcb0, entries);

    /* Set up cleanup context */
    cleanup_ctx = (ucontext_t*)malloc(sizeof(ucontext_t));
    getcontext(cleanup_ctx);
    cleanup_ctx->uc_stack.ss_sp = new_stack();
    cleanup_ctx->uc_stack.ss_size = SIGSTKSZ;
    /* If a thread exits implicitly (naturally) */
    makecontext(cleanup_ctx, _exit_cleanup, 0); // 1 arg, status = 0
}


int wut_id() {
    /* Get current id */
    struct list_entry *first_e = TAILQ_FIRST(&list_head);
    if (first_e == NULL) {
        die("List is empty.");
    }
    return first_e->id;
}

int wut_create(void (*run)(void)) {
    int thread_id = 0;
    // Allocate a new thread control block (tcb)
    struct tcb* entry;
    /* Get the lowest available id */
    TAILQ_FOREACH(entry, &tcb_queue_head, entries) {
        if(entry->tid > thread_id){
            break;
        }
        thread_id++;
    }

    /* Set up list entry */
    struct list_entry* e = (struct list_entry*)malloc(sizeof(struct list_entry));
    e->id = thread_id;
    TAILQ_INSERT_TAIL(&list_head, e, entries);
    struct tcb* new_tcb = malloc(sizeof(struct tcb));
    if (new_tcb == NULL) {
        die("malloc tcb failed");
    }
    /* Set up tcb */
    new_tcb->tid = thread_id;
    new_tcb->context = malloc(sizeof(ucontext_t));
    getcontext(new_tcb->context);
    // Allocate a new stack for the new thread
    char* stack = new_stack();
    new_tcb->context->uc_stack.ss_sp = stack;
    new_tcb->context->uc_stack.ss_size = SIGSTKSZ;
    new_tcb->context->uc_link = cleanup_ctx;
    makecontext(new_tcb->context, run, 0);
    new_tcb->status = 0;
    new_tcb->queue_entry = e;
    new_tcb->waiter_id = -1;
    new_tcb->waiting_id = -1;
    new_tcb->has_exited = 0;
    new_tcb->init = 0;
    // Add the new thread to the tcb_queue_head
    if (entry == NULL){ //
        TAILQ_INSERT_TAIL(&tcb_queue_head, new_tcb, entries);
    }
    else{
        TAILQ_INSERT_BEFORE(entry, new_tcb, entries);
    }

    // Return the new thread ID
    return thread_id;
}

int wut_yield() { // give the work to someone else and rejoin the queue
    /* Get the current thread and update its context */
    /* the current thread is in FIFO queue since it's still running */
    struct list_entry* cur_thread = TAILQ_FIRST(&list_head);
    TAILQ_REMOVE(&list_head, cur_thread, entries);
    int cur_id = cur_thread->id;
    if(TAILQ_EMPTY(&list_head)){
        return -1; 
    }
    /* get current tcb */
    struct tcb* cur_tcb;
    TAILQ_FOREACH(cur_tcb, &tcb_queue_head, entries) {
        if(cur_tcb->tid == cur_id){
            break;
        }
    }
    /* Insert the current thread to the back of the FIFO queue */
    TAILQ_INSERT_TAIL(&list_head, cur_thread, entries);

    /* Get the next thread to run */
    struct list_entry *available_thread = TAILQ_FIRST(&list_head);
    struct tcb* nxt_tcb;
    TAILQ_FOREACH(nxt_tcb, &tcb_queue_head, entries) {
        if(nxt_tcb->tid == available_thread->id){
            break;
        }
    }
    int err = swapcontext(cur_tcb->context, nxt_tcb->context);
    if(err == -1){
        die("failed to swap context");
    }
    return 0;
}

int wut_cancel(int id) {
    /* Find the thread with id */
    struct tcb* thread_to_cancel = NULL;
    TAILQ_FOREACH(thread_to_cancel, &tcb_queue_head, entries) {
        if (thread_to_cancel->tid == id) {
            break;
        }
    }

    /* Check if we're trying to cancel the current thread */
    if (id == wut_id()) {
        return -1;
    }

    /* Check if the thread was found */
    if (thread_to_cancel == NULL) {
        return -1;
    }
    /* Cannot cancel on an exited thread */
    if (thread_to_cancel->has_exited == 1){
        return -1;
    }
    /* Update status */
    thread_to_cancel->has_exited = 1;

    /* Remove the thread from the FIFO queue */
    struct list_entry *e;
    TAILQ_FOREACH(e, &list_head, entries) {
        if (e->id == id) {
            break;
        }
    }
    if (e) {
        TAILQ_REMOVE(&list_head, thread_to_cancel->queue_entry, entries);
    }

    /* Check if another thread was waiting on this thread */
    if (thread_to_cancel->waiter_id != -1){
        struct tcb* waiter_thread = NULL;
        TAILQ_FOREACH(waiter_thread, &tcb_queue_head, entries) {
            if (waiter_thread->tid == thread_to_cancel->waiter_id) {
                /* Insert the waiter back to FIFO queue, resume */
                TAILQ_INSERT_TAIL(&list_head, waiter_thread->queue_entry, entries);
                break;
            }
        }
    }

    /* Check if the cancelled thread was waiting on another thread */
    if (thread_to_cancel->waiting_id != -1){
        struct tcb* waited_thread = NULL;
        TAILQ_FOREACH(waited_thread, &tcb_queue_head, entries) {
            if (waited_thread->tid == thread_to_cancel->waiting_id) {
                /* No longer waiting; update status */
                thread_to_cancel->waiting_id = -1;
                waited_thread->waiter_id = -1;
                break;
            }
        }
    }

    /* Free the stack and ucontext */
    if (thread_to_cancel->init == 0) {
        delete_stack(thread_to_cancel->context->uc_stack.ss_sp);
    }
    free(thread_to_cancel->context);
    thread_to_cancel->context = NULL;
    /* Set the thread status and maintain the thread control block */
    thread_to_cancel->status = 128;
    return 0;
}

/* Define the wut_join function */
int wut_join(int id) {
    /* cause the calling thread to wait on the thread specified by id to finish (either by
      * exiting to getting cancelled) */
    /* Check if the thread you're waiting on exists */
    struct tcb *waited;
    int found = 0;
    TAILQ_FOREACH(waited, &tcb_queue_head, entries) {
        if (waited->tid == id) {
            found = 1;
            break;
        }
    }
    if (found != 1) {
        return -1;
    }

    /* Check if the waited thread is not the current thread */
    if (id == wut_id()) {
        return -1;
    }

    /* Check if the waited thread doesn't already have another thread waiting on it */
    if (waited->waiter_id != -1) {
        return -1;
    }
    
    /* Check if the waited thread is currently running (in FIFO queue) */
    int running = 0;
    struct list_entry* entry;
    TAILQ_FOREACH(entry, &list_head, entries) {
        if (entry->id == waited->tid) {
            running = 1;
            break;
        }
    }
    /* Check if the waited thread is waiting on another thread -> not in the queue but hasn't exited */
    if (running != 1 && waited->has_exited == 0) {
        return -1;
    }

    /* Find the current thread in tcb queue */
    /* note: the current one might not be in FIFO queue if it's canceled or waiting on another thread */
    struct tcb *cur_tcb;
    TAILQ_FOREACH(cur_tcb, &tcb_queue_head, entries) {
        if (cur_tcb->tid == wut_id()) {
            break;
        }
    }

    /* Update tcb with join relationship */
    cur_tcb->waiting_id = id;
    waited->waiter_id = wut_id();
    /* If the waited thread hasn't exited */
    if (waited->has_exited == 0) {
        /* Remove the waiter from FIFO */
        TAILQ_REMOVE(&list_head, cur_tcb->queue_entry, entries);
        /* Switch to the next thread to execute in FIFO */
        struct list_entry *nxt_e = TAILQ_FIRST(&list_head);
        struct tcb *nxt_tcb;
        TAILQ_FOREACH(nxt_tcb, &tcb_queue_head, entries) {
            if(nxt_tcb->tid == nxt_e->id){
                break;
            }
        }
        int er = swapcontext(cur_tcb->context, nxt_tcb->context);
        if (er == -1) {die("swapcontext failed.");}
    }
    //waited thread has exited explicitly
    /* Get the new current id IMPORTANT */
    int curr_id = wut_id();
    TAILQ_FOREACH(cur_tcb, &tcb_queue_head, entries) {
        if(cur_tcb->tid == curr_id){
            break; 
        }
    }
    /* Update status */
    cur_tcb->waiting_id = -1;
    waited->waiter_id = -1;
    /* Remove the exited thread from tcb queue */
    TAILQ_REMOVE(&tcb_queue_head, waited, entries);
    if (waited->init == 0) {
        if (waited->context != NULL){
            delete_stack(waited->context->uc_stack.ss_sp);
        }
    }
    if (waited->context != NULL){
        free(waited->context);
    }
    waited->context = NULL;
    free(waited->queue_entry);
    waited->queue_entry = NULL;
    int status_of_waited_thread = waited->status;
    free(waited);
    return status_of_waited_thread;
    
    
}

void wut_exit(int status) {
    status &= 0xFF;
    struct list_entry* thread_exited = TAILQ_FIRST(&list_head);
    /* Remove the exited thread from FIFO queue */
    TAILQ_REMOVE(&list_head, thread_exited, entries);
    /* Find exited thread in tcb queue */
    struct tcb *current_thread;
    TAILQ_FOREACH(current_thread, &tcb_queue_head, entries) {
        if(current_thread->tid == thread_exited->id){
            break;
        }
    }
    current_thread->status = status;
    current_thread->has_exited = 1;

    /* Check if there is another thread waiting on this exiting thread */
    if (current_thread->waiter_id != -1){
        struct tcb *waiter_thread;
        TAILQ_FOREACH(waiter_thread, &tcb_queue_head, entries) {
            if(waiter_thread->tid == current_thread->waiter_id){
                /* Insert the waiter back to FIFO */
                TAILQ_INSERT_TAIL(&list_head, waiter_thread->queue_entry, entries);
                break;
            }
        }
    }
    /* If the exiting process is the final process */
    if (TAILQ_EMPTY(&list_head)){
        // TAILQ_INSERT_TAIL(&list_head, thread_exited, entries);
        int err = setcontext(cleanup_ctx);
        if (err == -1) {
            die("fail to cleanup after the last thread.");
        }
    }
    /* If the exiting process does not have uc_link -> clean up manually */
    if (current_thread->context->uc_link == NULL){
        ucontext_t* cleanup_ctx = malloc(sizeof(ucontext_t));
        getcontext(cleanup_ctx);
        cleanup_ctx->uc_stack.ss_sp = new_stack();
        cleanup_ctx->uc_stack.ss_size = SIGSTKSZ;
        makecontext(cleanup_ctx, _exit_cleanup, 0);
        setcontext(cleanup_ctx);
    }
    /* Get the next thread in FIFO */
    struct list_entry *available_thread = TAILQ_FIRST(&list_head);
    struct tcb *nxt_thread;
    TAILQ_FOREACH(nxt_thread, &tcb_queue_head, entries) {
        if(nxt_thread->tid == available_thread->id){
            break;
        }
    }
    int ret = setcontext(nxt_thread->context);
    if (ret == -1) {
        die("setcontext unsuccessful.");
    }
}
