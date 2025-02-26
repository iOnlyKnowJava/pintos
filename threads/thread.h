#ifndef THREADS_THREAD_H
#define THREADS_THREAD_H

#include <debug.h>
#include <list.h>
#include <stdint.h>
#include "threads/synch.h"
#include "filesys/directory.h"

/* States in a thread's life cycle. */
enum thread_status
{
  THREAD_RUNNING, /* Running thread. */
  THREAD_READY,   /* Not running but ready to run. */
  THREAD_BLOCKED, /* Waiting for an event to trigger. */
  THREAD_DYING    /* About to be destroyed. */
};

/* Thread identifier type.
   You can redefine this to whatever type you like. */
typedef int tid_t;
#define TID_ERROR ((tid_t) - 1) /* Error value for tid_t. */

/* Thread priorities. */
#define PRI_MIN 0      /* Lowest priority. */
#define PRI_DEFAULT 31 /* Default priority. */
#define PRI_MAX 63     /* Highest priority. */

#define MX_OPEN_FILES 128 /* Max # of open files a process can have at once */
#define FD_START_VAL 2    /* First non-special file descriptor value */

struct thread;

/* Struct to help with wait and exit by keeping track of child process's
 * information */
struct child_info
{
  tid_t tid;                     // tid of child process
  struct semaphore wait_sema;    // Allows process to wait on child exit
  struct semaphore control_sema; // Controls access to this struct
  struct list_elem elem;         // List element for children_infos list
  int exit_status;               // Exit code of child process.
  bool other_exited;             // True if either child or parent has exited
};

/* Method to initialize struct child_info. */
void child_info_init (struct child_info *info);

/* A kernel thread or user process.

   Each thread structure is stored in its own 4 kB page.  The
   thread structure itself sits at the very bottom of the page
   (at offset 0).  The rest of the page is reserved for the
   thread's kernel stack, which grows downward from the top of
   the page (at offset 4 kB).  Here's an illustration:

        4 kB +---------------------------------+
             |          kernel stack           |
             |                |                |
             |                |                |
             |                V                |
             |         grows downward          |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             +---------------------------------+
             |              magic              |
             |                :                |
             |                :                |
             |               name              |
             |              status             |
        0 kB +---------------------------------+

   The upshot of this is twofold:

      1. First, `struct thread' must not be allowed to grow too
         big.  If it does, then there will not be enough room for
         the kernel stack.  Our base `struct thread' is only a
         few bytes in size.  It probably should stay well under 1
         kB.

      2. Second, kernel stacks must not be allowed to grow too
         large.  If a stack overflows, it will corrupt the thread
         state.  Thus, kernel functions should not allocate large
         structures or arrays as non-static local variables.  Use
         dynamic allocation with malloc() or palloc_get_page()
         instead.

   The first symptom of either of these problems will probably be
   an assertion failure in thread_current(), which checks that
   the `magic' member of the running thread's `struct thread' is
   set to THREAD_MAGIC.  Stack overflow will normally change this
   value, triggering the assertion.  (So don't add elements below
   THREAD_MAGIC.)
*/
/* The `elem' member has a dual purpose.  It can be an element in
   the run queue (thread.c), or it can be an element in a
   semaphore wait list (synch.c).  It can be used these two ways
   only because they are mutually exclusive: only a thread in the
   ready state is on the run queue, whereas only a thread in the
   blocked state is on a semaphore wait list. */
struct thread
{
  /* Owned by thread.c. */

  tid_t tid;                  /* Thread identifier. */
  enum thread_status status;  /* Thread state. */
  char name[16];              /* Name (for debugging purposes). */
  uint8_t *stack;             /* Saved stack pointer. */
  int priority;               /* Priority. */
  struct list locks;          /* List to store locks thread holds. */
  struct lock *locker;        /* Lock which thread is waiting on. */
  int donated_priority;       /* Priority donated by this lock to its holder. */
  struct list_elem allelem;   /* List element for all threads list. */
  struct list children_infos; /* List to store thread's children_infos */

  /* Points to where child process should tell parent its
     exit value and stop parent's wait. */
  struct child_info *info_to_update;

  /* Semaphore so parent process can wait for child process to be created*/
  struct semaphore exec_sema;
  bool exec_success;     /* Indicates if exec call was a success */
  struct thread *parent; /* Parent process of this process */

  /* Pointer to process's own executable, denies modifications to executable
   * until process exits */
  struct file *exec_file;

  struct file *open_files[MX_OPEN_FILES]; /* Contains process's open files */

  /* Shared between thread.c and synch.c. */
  struct list_elem elem; /* List element. */
#ifndef USERPROG
#define USERPROG
#endif
#ifdef USERPROG
  /* Owned by userprog/process.c. */
  uint32_t *pagedir; /* Page directory. */
#endif

  struct dir *curr_directory; /* Process's current directory */

  /* Owned by thread.c. */
  unsigned magic; /* Detects stack overflow. */
};

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
extern bool thread_mlfqs;

void thread_init (void);
void thread_start (void);

void thread_tick (void);
void thread_print_stats (void);

typedef void thread_func (void *aux);
tid_t thread_create (const char *name, int priority, thread_func *, void *);

void thread_block (void);
void thread_unblock (struct thread *);

struct thread *thread_current (void);
tid_t thread_tid (void);
const char *thread_name (void);

void thread_exit (void) NO_RETURN;
void thread_yield (void);

/* Performs some operation on thread t, given auxiliary data AUX. */
typedef void thread_action_func (struct thread *t, void *aux);
void thread_foreach (thread_action_func *, void *);

int thread_get_priority (void);
void thread_set_priority (int);

int thread_get_nice (void);
void thread_set_nice (int);
int thread_get_recent_cpu (void);
int thread_get_load_avg (void);

int max (int a, int b); /* Returns maximum of the two int parameters. */

/* Comparator to sort threads by greater priority */
bool priority_cmp (const struct list_elem *a, const struct list_elem *b,
                   void *x);
#endif /* threads/thread.h */
