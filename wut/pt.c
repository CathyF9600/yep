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
// TAILQ_HEAD(ready_queue,tcb)

#define MAX_THREADS 100
typedef struct tcb{
    int tid;
    ucontext_t context;
    int status;
    char *stack;
    int setcontext_called;
    bool has_terminated; // flag indicating implicit exit
    // struct tcb *next; // do we need this?
    TAILQ_ENTRY(tcb) entries; // pointer to the tcb's location in ready queue
} tcb_t;

// Define the ready ready_queue for threads
TAILQ_HEAD(tcb_queue_head, tcb) all_threads;
TAILQ_HEAD(ready_queue_head, tcb) ready_queue;
int num_threads;

// static struct tcb *ready_threads; // initialize the current_thread with a tcb as the first element
struct tcb *tcb0; // make the first tcb created by init global

/* create a clean up context */
static ucontext_t cleanup_ctx;
// static char *cleanup_stack;


tcb_t* find_thread_in_all(int id) {
    tcb_t* thread;
    TAILQ_FOREACH(thread, &all_threads, entries) {
        if (thread->tid == id) {
            return thread;
        }
    }
    return NULL;  // Thread with given id not found
}

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
/*
void cleanup_all(struct wait_queue_node* waiting_queue) {

    while(waiting_queue != NULL) { 
        struct wait_queue_node* tmp = waiting_queue;
        waiting_queue = waiting_queue->next;
        free(tmp);
    }
}

void cleanup(struct tcb *tcb_block) {       // remove a node from ready_queue
    if (ready_queue == NULL) {
        thread_id--;
        return;
    }
    else if (ready_queue == tcb_block) {
        ready_queue = NULL;
        thread_id--;
        return;
    }
    else {
        struct tcb* clean = ready_queue;

        while(clean->next != NULL) {
            if(clean->next == tcb_block) {
                if(clean->next->next != NULL) {
                    clean = clean->next->next;
                    while(clean != NULL) {
                        clean->tid = clean->tid - 1;
                        clean = clean->next;
                        thread_id--;
                    }
                    }
                    
                else {
                    clean = NULL;
                    thread_id--;
                }  
                return;
            }

        }
    }
}
*/

void wut_init() {
    TAILQ_INIT(&ready_queue);
    TAILQ_INIT(&all_threads);
    num_threads = 0;


    /* setup tcb0 */
    tcb0 = malloc(sizeof(struct tcb));
    tcb0->stack = new_stack();
    getcontext(&tcb0->context);
    tcb0->context.uc_stack.ss_sp = tcb0->stack;
    tcb0->context.uc_stack.ss_size = SIGSTKSZ;
    tcb0->status = 0;
    tcb0->context.uc_link = NULL;
    tcb0->tid = 0;
    tcb0->setcontext_called = 0;
    TAILQ_INSERT_TAIL(&all_threads, tcb0, entries);
    TAILQ_INSERT_TAIL(&ready_queue, tcb0, entries);
    num_threads ++;

    /* Set up cleanup context */
    getcontext(&cleanup_ctx);
    cleanup_ctx.uc_stack.ss_sp = new_stack();
    cleanup_ctx.uc_stack.ss_size = SIGSTKSZ;
    cleanup_ctx.uc_link = NULL;
    /* If a thread exits implicitly (naturally) */
    makecontext(&cleanup_ctx, (void (*)()) wut_exit, 1, 0); // 1 arg, status = 0
}


int wut_id() {
    /* Get current id */
    struct tcb *current_thread = TAILQ_FIRST(&ready_queue);
    return current_thread->tid;
}

int wut_create(void (*run)(void)) {
    // Allocate a new thread control block (tcb)
    struct tcb* new_tcb = malloc(sizeof(struct tcb));
    if (new_tcb == NULL) {
        die("malloc tcb failed");
    }

    if (num_threads == MAX_THREADS){
        die("Maximum number of threads reached.");
    }

    // Set the thread ID and context for the new tcb
    new_tcb->tid = num_threads;
    num_threads ++;
    getcontext(&new_tcb->context);
    // Allocate a new stack for the new thread
    char* stack = new_stack();
    new_tcb->stack = stack;
    new_tcb->context.uc_stack.ss_sp = stack;
    new_tcb->context.uc_stack.ss_size = SIGSTKSZ;
    new_tcb->context.uc_link = &cleanup_ctx;
    new_tcb->setcontext_called = 0;
    new_tcb->status = 0;
    makecontext(&new_tcb->context, run, 0);

    // Add the new thread to the ready ready_queue in FIFO order
    TAILQ_INSERT_TAIL(&all_threads, new_tcb, entries);
    TAILQ_INSERT_TAIL(&ready_queue, new_tcb, entries);

    // Return the new thread ID
    return new_tcb->tid;
}

int wut_yield() { // give the work to someone else and rejoin the queue
    /* Get the current thread and update its context */
    int current_id = wut_id();
    // printf("current_id",current_id);
    struct tcb *current_thread = find_thread_in_all(current_id);

    /* Put the current thread at the end of the ready queue */
    TAILQ_REMOVE(&ready_queue, current_thread, entries);
    TAILQ_INSERT_TAIL(&ready_queue, current_thread, entries);
    getcontext(&current_thread->context);

    /* Get the next thread to run */
    struct tcb *available_thread = TAILQ_FIRST(&ready_queue);
    /* Check if there are no threads to yield to */
    if (available_thread == NULL || TAILQ_EMPTY(&ready_queue)) {
        return -1;
    }

   if (current_thread->setcontext_called == 0) {
        current_thread->setcontext_called = 1;
        current_thread = available_thread;

        setcontext(&available_thread->context);
        if(setcontext(&available_thread->context) == -1){
        die("setcontext failed");
    } 
    } else {
        current_thread->setcontext_called = 0;
    }
    return 0;
}

int wut_cancel(int id) {
   /* Find the thread with id */
   struct tcb* thread_to_cancel = NULL;
    TAILQ_FOREACH(thread_to_cancel, &ready_queue, entries) {
        if (thread_to_cancel->tid == id) {
            break;
        }
    }

    /* Check if the thread was found */
    if (thread_to_cancel == NULL) {
        return -1;
    }

    /* Get the current thread */
    int current_id = wut_id();
    struct tcb *current_thread = find_thread_in_all(current_id);

    /* Check if we're trying to cancel the current thread */
    if (thread_to_cancel == current_thread) {
        return -1;
    }

    /* Remove the thread from the ready queue */
    TAILQ_REMOVE(&ready_queue, thread_to_cancel, entries);
    num_threads --;
    /* Free the stack and ucontext */
    delete_stack(thread_to_cancel->context.uc_stack.ss_sp);
    // delete_stack(thread_to_cancel->context.uc_link);
    free(thread_to_cancel);

    /* Set the thread status and maintain the thread control block */
    thread_to_cancel->status = 128;

    return 0;
}

/* Define the wut_join function */
int wut_join(int id) {
    /* cause the calling thread to wait on the thread specified by id to finish (either by
      * exiting to getting cancelled) */
    /* Get the current thread */
    int current_id = wut_id();
    struct tcb *current_thread = find_thread_in_all(current_id);

    /* Find the thread to wait on */
    struct tcb* thread_to_wait_on = NULL;
    TAILQ_FOREACH(thread_to_wait_on, &ready_queue, entries) {
        if (current_thread->tid == id) {
            /* can't wait on itself*/
            return -1;
        }

        else if (thread_to_wait_on->tid == id) {
            /* Check if the thread was found */
            if (thread_to_wait_on == NULL) {
                return -1;
            }

            /* Check if the thread is already being joined on */
            if (thread_to_wait_on->status == 129 || 128) { //128 canceled
                return -1;
            }
            
            /* Set the thread status to being joined on */
            thread_to_wait_on->status = 129;

            /* Swap context to the thread being joined on */
            TAILQ_REMOVE(&ready_queue, current_thread, entries);
            num_threads --;
            current_thread = thread_to_wait_on;
            swapcontext(&thread_to_wait_on->context, &current_thread->context);
            delete_stack(thread_to_wait_on->context.uc_stack.ss_sp);
            return thread_to_wait_on->status;
        }
    }
}

void wut_exit(int status) {
    /* Get the current thread */
    int current_id = wut_id();
    struct tcb *current_thread = find_thread_in_all(current_id);

    current_thread->status = status;
    current_thread->has_terminated = 1;
    TAILQ_REMOVE(&ready_queue, current_thread, entries);
    num_threads --;

    struct tcb *available_thread = TAILQ_FIRST(&ready_queue);
    /* Check if there are no threads to yield to */
    if (available_thread != NULL) {
        current_thread = available_thread;
        setcontext(&available_thread->context);
    }
    else {
        exit(0); // all threads have been exexcuted
    }
}

