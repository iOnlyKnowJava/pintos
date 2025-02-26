#include "userprog/syscall.h"
#include "userprog/pagedir.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "vm/page.h"
#include "vm/frame.h"
#include "userprog/exception.h"

static void syscall_handler (struct intr_frame*);

/* Controls access to file system */
struct lock filesys_access;

/* Stores stack pointer of latest system call, NULL otherwise */
void* stack_ptr;

/* Constant but used to guarantee compiler optimizations do not occur */
int ZERO_CONSTANT = 0;

/* Exits current process with status exit_status. Does not return to caller. */
void exit_process (int exit_status)
{
  struct thread* curr = thread_current ();
  printf ("%s: exit(%d)\n", curr->name, exit_status); /* exit msg */

  // Matthew driving
  /* Close any opened files */
  for (int i = 0; i < MAX_FILES; i++)
    {
      if (curr->open_files[i] != NULL)
        {
          lock_acquire (&filesys_access);
          file_close (curr->open_files[i]);
          lock_release (&filesys_access);
        }
    }

  /* Allow modifications to executable file now */
  if (curr->exec_file != NULL)
    {
      lock_acquire (&filesys_access);
      file_close (curr->exec_file);
      lock_release (&filesys_access);
    }

  // Vincent driving
  /* Give exit status to parent process */
  curr->info_to_update->exit_status = exit_status;
  sema_up (&curr->info_to_update->wait_sema);

  // destroy table and free all entries
  hash_destroy (&thread_current ()->supp_page_table, &supp_entry_destroy);

  thread_exit ();
}

/* Checks if a pointer given is valid, exits with code -1 if not. If on_stack
is true, also checks if rest of bytes of the stack entry are valid. */
void validate_pointer (char* ptr, bool on_stack)
{
  // Vincent driving
  if (ptr == NULL || !is_user_vaddr (ptr))
    {
      exit_process (-1);
    }
  // Vincent driving
  if (get_entry (ptr) == NULL)
    {
      if (((unsigned) stack_ptr - (unsigned) ptr <= PUSHABYTES ||
           ptr >= stack_ptr) &&
          (unsigned) PHYS_BASE - (unsigned) ptr <= STACKLIMIT)
        {
          // Implicit stack growth
          get_frame (ptr);
        }
      else
        {
          exit_process (-1);
        }
    }
  // Matthew driving
  if (on_stack) /* Check if pointer to other bytes of stack entry also valid */
    {
      ptr += sizeof (uint32_t) - 1;
      if (!is_user_vaddr (ptr) || get_entry (ptr) == NULL)
        {
          exit_process (-1);
        }
    }
}

/* Checks if string given as ptr is valid, exits with code -1 if not. */
// Vincent driving
void validate_string (char* ptr)
{
  validate_pointer (ptr, false);
  while (*ptr != '\0')
    {
      ptr++;
      validate_pointer (ptr, false);
    }
}

/* Checks if buffer given as ptr of size size is valid,
exits with code -1 if not. */
void validate_buffer (char* ptr, unsigned size)
{
  for (unsigned i = 0; i < size; i += PGSIZE)
    {
      validate_pointer (ptr + i, false);
    }
  validate_pointer (ptr + size - 1, false);
}

/* Returns pointer to ith argument of system call. Assumes stack pointer
  not changed before calling */
void* get_arg (void* sp, int i) { return (uint32_t*) sp + i; }

/* Given a file descriptor, return the corresponding file pointer if open, or
NULL if file for file descriptor not found */
struct file* get_file (int fd)
{
  if (fd < 2 || fd > MAX_FILES + 1 ||
      thread_current ()->open_files[fd - 2] == NULL)
    {
      return NULL;
    }
  return thread_current ()->open_files[fd - 2];
}

void syscall_init (void)
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
  lock_init (&filesys_access);
  stack_ptr = NULL;
}

static void syscall_handler (struct intr_frame* f UNUSED)
{
  // Matthew driving
  struct thread* curr = thread_current ();
  stack_ptr = f->esp;
  validate_pointer (f->esp, true);
  uint32_t syscall_num = *(uint32_t*) (f->esp);
  uint32_t *ptr1 = NULL, *ptr2 = NULL, *ptr3 = NULL;

  switch (syscall_num)
    {
        case SYS_HALT: {
          shutdown_power_off ();
        }
        break;
        case SYS_EXIT: {
          // Vincent driving
          ptr1 = get_arg (f->esp, 1);
          validate_pointer (ptr1, true);
          // Vincent driving
          // Matthew driving
          int exit_status = *(int*) ptr1;
          exit_process (exit_status);
        }
        break;
        case SYS_EXEC: {
          // Vincent driving
          ptr1 = get_arg (f->esp, 1);
          validate_pointer (ptr1, true);
          validate_string (*(char**) ptr1);

          char* cmd_line = *(char**) (ptr1);
          curr->exec_success = true;
          tid_t child_tid = process_execute (cmd_line);
          if (child_tid != TID_ERROR)
            {
              // wait for child to finish loading
              sema_down (&curr->exec_sema);
              if (!curr->exec_success)
                {
                  child_tid = -1;
                }
            }

          f->eax = child_tid;
        }
        break;
        case SYS_WAIT: {
          // Matthew driving
          ptr1 = get_arg (f->esp, 1);
          validate_pointer (ptr1, true);

          tid_t tid = *(tid_t*) (ptr1);
          f->eax = process_wait (tid);
        }
        break;
        case SYS_CREATE: {
          ptr1 = get_arg (f->esp, 1);
          ptr2 = get_arg (f->esp, 2);
          validate_pointer (ptr1, true);
          validate_pointer (ptr2, true);
          validate_string (*(char**) ptr1);

          char* file = *(char**) (ptr1);
          uint32_t initial_size = *(uint32_t*) (ptr2);
          lock_acquire (&filesys_access);
          f->eax = filesys_create (file, initial_size);
          lock_release (&filesys_access);
        }
        break;
        case SYS_REMOVE: {
          // Matthew driving
          ptr1 = get_arg (f->esp, 1);
          validate_pointer (ptr1, true);
          validate_string (*(char**) ptr1);

          char* file = *(char**) (ptr1);
          lock_acquire (&filesys_access);
          f->eax = filesys_remove (file);
          lock_release (&filesys_access);
        }
        break;
        case SYS_OPEN: {
          ptr1 = get_arg (f->esp, 1);
          validate_pointer (ptr1, true);
          validate_string (*(char**) ptr1);

          // Matthew driving
          f->eax = -1;
          char* file = *(char**) (ptr1);
          struct file* file_ptr;
          lock_acquire (&filesys_access);
          file_ptr = filesys_open (file);
          lock_release (&filesys_access);
          if (file_ptr)
            {
              // store file_ptr in open_files (file descriptor is index)
              for (int i = 0; i < MAX_FILES; i++)
                {
                  // find place to store opened file
                  if (curr->open_files[i] == NULL)
                    {
                      f->eax = i + 2; // adjust for stdin and stdout
                      curr->open_files[i] = file_ptr;
                      break;
                    }
                }
            }

          // Could not store file, close opened file
          if (f->eax == -1)
            {
              lock_acquire (&filesys_access);
              file_close (file_ptr);
              lock_release (&filesys_access);
            }
        }
        break;
        case SYS_FILESIZE: {
          ptr1 = get_arg (f->esp, 1);
          validate_pointer (ptr1, true);

          int fd = *(int*) (ptr1);
          // Vincent driving
          f->eax = -1;
          struct file* file_ptr = get_file (fd);
          if (file_ptr)
            {
              lock_acquire (&filesys_access);
              f->eax = file_length (file_ptr);
              lock_release (&filesys_access);
            }
        }
        break;
        case SYS_READ: {
          ptr1 = get_arg (f->esp, 1);
          ptr2 = get_arg (f->esp, 2);
          ptr3 = get_arg (f->esp, 3);
          validate_pointer (ptr1, true);
          validate_pointer (ptr2, true);
          validate_pointer (ptr3, true);

          int fd = *(int*) (ptr1);
          char* buffer = *(char**) (ptr2);
          unsigned size = *(unsigned*) (ptr3);
          validate_buffer (buffer, size);
          if (size == 0)
            {
              f->eax = 0;
              break;
            }
          f->eax = -1;
          if (fd == 0)
            {
              // read from keyboard
              for (int i = 0; i < size; i++)
                {
                  *buffer = input_getc ();
                  buffer++;
                }
              f->eax = size;
            }
          else
            {
              struct file* file_ptr = get_file (fd);
              if (file_ptr)
                {
                  // Make sure pages swapped in and pin them
                  for (unsigned int i = 0; i < size; i += PGSIZE)
                    {
                      get_entry (buffer + i)->locked = true;
                      char c = *(buffer + i);
                      *(buffer + i) = 0;
                      // Avoid compiler optimization (barriers did not work)
                      if (*(buffer + i) == ZERO_CONSTANT)
                        *(buffer + i) = c;
                    }
                  // Deal with possibly going across page boundaries
                  get_entry (buffer + size - 1)->locked = true;
                  char c = *(buffer + size - 1);
                  *(buffer + size - 1) = 0;
                  if (*(buffer + size - 1) == ZERO_CONSTANT)
                    *(buffer + size - 1) = c;

                  lock_acquire (&filesys_access);
                  f->eax = file_read (file_ptr, buffer, size);
                  lock_release (&filesys_access);

                  // Unpin pinned pages
                  for (unsigned int i = 0; i < size; i += PGSIZE)
                    {
                      get_entry (buffer + i)->locked = false;
                    }
                  get_entry (buffer + size - 1)->locked = false;
                }
            }
        }
        break;
        case SYS_WRITE: {
          // Matthew driving
          ptr1 = get_arg (f->esp, 1);
          ptr2 = get_arg (f->esp, 2);
          ptr3 = get_arg (f->esp, 3);
          validate_pointer (ptr1, true);
          validate_pointer (ptr2, true);
          validate_pointer (ptr3, true);

          int fd = *(int*) (ptr1);
          char* buffer = *(void**) (ptr2);
          unsigned size = *(unsigned*) ptr3;
          validate_buffer (buffer, size);

          f->eax = 0;
          if (fd == 1)
            {
              // output to system console
              putbuf (buffer, size);
              f->eax = size;
            }
          else
            {
              struct file* file_ptr = get_file (fd);
              if (file_ptr)
                {
                  // Make sure page swapped in and pin them
                  for (unsigned int i = 0; i < size; i += PGSIZE)
                    {
                      get_entry (buffer + i)->locked = true;
                      char c = *(buffer + i);
                      // Avoid compiler optimizaton (barriers did not work)
                      if (c & ZERO_CONSTANT)
                        *(buffer + i) = 0;
                    }
                  // Deal with possibly going across page boundaries
                  get_entry (buffer + size - 1)->locked = true;
                  char c = *(buffer + size - 1);
                  if (c & ZERO_CONSTANT)
                    *(buffer + size - 1) = 0;

                  // Matthew driving
                  lock_acquire (&filesys_access);
                  f->eax = file_write (file_ptr, buffer, size);
                  lock_release (&filesys_access);

                  // Unpin pinned pages
                  for (unsigned int i = 0; i < size; i += PGSIZE)
                    {
                      get_entry (buffer + i)->locked = false;
                    }
                  get_entry (buffer + size - 1)->locked = false;
                }
            }
        }
        break;
        case SYS_SEEK: {
          ptr1 = get_arg (f->esp, 1);
          ptr2 = get_arg (f->esp, 2);
          validate_pointer (ptr1, true);
          validate_pointer (ptr2, true);

          int fd = *(int*) (ptr1);
          unsigned position = *(unsigned*) (ptr2);
          struct file* file_ptr = get_file (fd);
          if (file_ptr)
            {
              file_seek (file_ptr, position);
            }
        }
        break;
        case SYS_TELL: {
          // Matthew driving
          ptr1 = get_arg (f->esp, 1);
          validate_pointer (ptr1, true);

          int fd = *(int*) (ptr1);
          f->eax = 0;
          struct file* file_ptr = get_file (fd);
          if (file_ptr)
            {
              f->eax = file_tell (file_ptr);
            }
        }
        break;
        case SYS_CLOSE: {
          ptr1 = get_arg (f->esp, 1);
          validate_pointer (ptr1, true);

          int fd = *(int*) (ptr1);
          struct file* file_ptr = get_file (fd);
          if (file_ptr)
            {
              lock_acquire (&filesys_access);
              file_close (file_ptr);
              lock_release (&filesys_access);
              curr->open_files[fd - 2] = NULL;
            }
        }
        break;
    }
}