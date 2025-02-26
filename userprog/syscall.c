#include "userprog/syscall.h"
#include "userprog/pagedir.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "filesys/free-map.h"
static void syscall_handler (struct intr_frame*);

/* Exits current process with status exit_status. Does not return to caller. */
void exit_process (int exit_status)
{
  struct thread* curr = thread_current ();
  printf ("%s: exit(%d)\n", curr->name, exit_status); /* exit msg */

  /* Close any opened files */
  for (int i = 0; i < MX_OPEN_FILES; i++)
    {
      if (curr->open_files[i] != NULL)
        {
          file_close (curr->open_files[i]);
        }
    }

  /* Allow modifications to executable file now */
  if (curr->exec_file != NULL)
    {
      file_close (curr->exec_file);
    }

  // Vincent driving
  /* Give exit status to parent process */
  curr->info_to_update->exit_status = exit_status;
  sema_up (&curr->info_to_update->wait_sema);
  dir_close (thread_current ()->curr_directory);
  thread_exit ();
}

/* Checks if a pointer given is valid, exits with code -1 if not. If on_stack
is true, also checks if rest of bytes of the stack entry are valid. */
void validate_pointer (char* ptr, bool on_stack)
{
  // Vincent driving
  if (ptr == NULL || !is_user_vaddr (ptr) ||
      pagedir_get_page (thread_current ()->pagedir, ptr) == NULL)
    {
      exit_process (-1);
    }
  // Matthew driving
  if (on_stack) /* Check if pointer to other bytes of stack entry also valid */
    {
      ptr += sizeof (uint32_t) - 1;
      if (!is_user_vaddr (ptr) ||
          pagedir_get_page (thread_current ()->pagedir, ptr) == NULL)
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
  if (fd < FD_START_VAL || fd >= FD_START_VAL + MX_OPEN_FILES ||
      thread_current ()->open_files[fd - FD_START_VAL] == NULL)
    {
      return NULL;
    }
  return thread_current ()->open_files[fd - FD_START_VAL];
}

void syscall_init (void)
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void syscall_handler (struct intr_frame* f UNUSED)
{
  // Matthew driving
  struct thread* curr = thread_current ();
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

          char* cmd_line = *(char**) (ptr1);
          validate_string (cmd_line);
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

          char* file = *(char**) (ptr1);
          validate_string (file);
          uint32_t initial_size = *(uint32_t*) (ptr2);
          f->eax = filesys_create (file, initial_size, false);
        }
        break;
        case SYS_REMOVE: {
          // Matthew driving
          ptr1 = get_arg (f->esp, 1);
          validate_pointer (ptr1, true);
          validate_string (*(char**) ptr1);

          char* file = *(char**) (ptr1);
          f->eax = filesys_remove (file);
        }
        break;
        case SYS_OPEN: {
          ptr1 = get_arg (f->esp, 1);
          validate_pointer (ptr1, true);
          validate_string (*(char**) ptr1);

          // Matthew driving
          f->eax = -1;
          char* file = *(char**) (ptr1);
          struct file* file_ptr = NULL;
          file_ptr = filesys_open (file);
          if (file_ptr)
            {
              // store file_ptr in open_files (file descriptor is index)
              for (int i = 0; i < MX_OPEN_FILES; i++)
                {
                  // find place to store opened file
                  if (curr->open_files[i] == NULL)
                    {
                      f->eax = i + FD_START_VAL; // adjust for stdin and stdout
                      curr->open_files[i] = file_ptr;
                      break;
                    }
                }
            }

          // Could not store file, close opened file
          if (f->eax == -1)
            {
              file_close (file_ptr);
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
              f->eax = file_length (file_ptr);
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
                  f->eax = file_read (file_ptr, buffer, size);
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
          void* buffer = *(void**) (ptr2);
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
                  // Matthew driving
                  if (file_ptr->inode->data.is_directory)
                    {
                      f->eax = -1;
                    }
                  else
                    {
                      f->eax = file_write (file_ptr, buffer, size);
                    }
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
              file_close (file_ptr);
              curr->open_files[fd - FD_START_VAL] = NULL;
            }
        }
        break;
        // Matthew driving
        case SYS_CHDIR: {
          ptr1 = get_arg (f->esp, 1);
          validate_pointer (ptr1, true);

          char* dir = *(char**) (ptr1);
          validate_string (dir);

          char* last = NULL;
          struct dir* tempdir = get_dir (dir, &last);

          struct inode* temp = NULL;
          f->eax = false;
          // Find new directory and switch current directory to it
          if (tempdir && dir_lookup (tempdir, last, &temp) &&
              temp->data.is_directory)
            {
              // Matthew driving
              struct dir* newdir = dir_open (temp);
              if (newdir != NULL)
                {
                  dir_close (thread_current ()->curr_directory);
                  thread_current ()->curr_directory = newdir;
                  f->eax = true;
                }
            }
          else
            {
              inode_close (temp);
            }
          dir_close (tempdir);
        }
        break;
        case SYS_MKDIR: {
          ptr1 = get_arg (f->esp, 1);
          validate_pointer (ptr1, true);
          char* dir = *(char**) (ptr1);
          validate_string (dir);
          f->eax = filesys_create (dir, 0, true);
        }
        break;
        case SYS_READDIR: {
          ptr1 = get_arg (f->esp, 1);
          ptr2 = get_arg (f->esp, 2);
          validate_pointer (ptr1, true);
          validate_pointer (ptr2, true);

          int fd = *(int*) (ptr1);
          char* name = *(char**) (ptr2);
          validate_string (name);
          struct file* file_ptr = get_file (fd);
          f->eax = false;
          if (file_ptr && file_ptr->inode->data.is_directory)
            {
              struct dir* tempdir = dir_open (inode_reopen (file_ptr->inode));
              if (tempdir != NULL)
                {
                  // Use struct file's pos as pos to start searching
                  tempdir->pos = file_ptr->dir_pos;
                  f->eax = dir_readdir (tempdir, name);
                  file_ptr->dir_pos = tempdir->pos;
                  dir_close (tempdir);
                }
            }
        }
        break;
        case SYS_ISDIR: {
          ptr1 = get_arg (f->esp, 1);
          validate_pointer (ptr1, true);

          int fd = *(int*) (ptr1);
          struct file* file_ptr = get_file (fd);
          f->eax = file_ptr && file_ptr->inode->data.is_directory;
        }
        break;
        case SYS_INUMBER: {
          ptr1 = get_arg (f->esp, 1);
          validate_pointer (ptr1, true);
          // Vincent driving
          int fd = *(int*) (ptr1);
          struct file* file_ptr = get_file (fd);
          f->eax = -1;
          if (file_ptr)
            {
              f->eax = file_ptr->inode->sector;
            }
        }
        break;
    }
}