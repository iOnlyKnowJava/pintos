#include <list.h>
#include <string.h>
#include <stdint.h>
#include "devices/block.h"
#include "vm/frame.h"
#include "threads/palloc.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "userprog/syscall.h"
#include "userprog/process.h"
#include "vm/page.h"
#include "threads/vaddr.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "userprog/pagedir.h"

extern struct lock filesys_access;

struct list frame_list; // Lists all frames of all processes

struct lock frame_list_access; // Controls access to frame_list

struct list free_sectors; // Lists all free sectors of swap space

struct lock free_sector_access; // Controls access to free_sectors list

struct block* swap_block; // Pointer to the block device for swap space

// Allows threads to wait for available frames to appear
extern struct semaphore frame_wait;

#ifndef BLOCK_STRUCT_DEFINED
#define BLOCK_STRUCT_DEFINED
/* Copy of struct block from devices/block.c so program compiles */
struct block
{
  struct list_elem list_elem; /* Element in all_blocks. */

  char name[16];        /* Block device name. */
  enum block_type type; /* Type of block device. */
  block_sector_t size;  /* Size in sectors. */

  const struct block_operations* ops; /* Driver operations. */
  void* aux;                          /* Extra data owned by driver. */

  unsigned long long read_cnt;  /* Number of sectors read. */
  unsigned long long write_cnt; /* Number of sectors written. */
};
#endif

/* Initializes the supplemental frame table */
void frametable_init ()
{
  list_init (&frame_list);
  lock_init (&frame_list_access);
  list_init (&free_sectors);
  lock_init (&free_sector_access);
  // Vincent driving
  // Initialize free sectors list
  swap_block = block_get_role (BLOCK_SWAP);
  // Create list of free sectors in swap space
  for (block_sector_t i = 0; i < swap_block->size - PGSIZE / BLOCK_SECTOR_SIZE;
       i += PGSIZE / BLOCK_SECTOR_SIZE)
    // Use PGSIZE/BLOCK_SECTOR_SIZE to account for PGSIZE > BLOCK_SECTOR_SIZE
    {
      struct block_sector* free_block = malloc (sizeof (struct block_sector));
      if (free_block == NULL)
        {
          exit_process (-1);
        }
      free_block->sector = i;
      list_push_back (&free_sectors, &free_block->elem);
    }
}

/* Sets the the page user_addr belongs to as valid.
Returns the supplemental information added to the supplemental page table, or
returns the existing supplemental info if the address was already valid  */
struct supp_entry* get_frame (void* user_addr)
{
  // Vincent driving

  // Matthew driving
  struct supp_entry* info = malloc (sizeof (struct supp_entry));
  if (!info)
    {
      exit_process (-1);
    }
  supp_entry_init (info);
  info->address = (void*) ((unsigned) user_addr & ~PGMASK);
  struct supp_entry* old =
      hash_insert (&thread_current ()->supp_page_table, &info->elem);
  if (old)
    {
      // User virtual address already mapped
      free (info);
      return old;
    }
  return info;
}

/* Swaps the page insert_page in, getting the information needed from the swap
space or file system, and also evicts a page to the swap space if necessary */
void swap_frame (struct supp_entry* insert_page)
{
  /* Make sure no other process messing with insert_page's information */
  lock_acquire (&insert_page->page_access);

  /* No new processes can access this page, so we can release lock */
  lock_release (&insert_page->page_access);
  struct data_frame* frame_data = malloc (sizeof (struct data_frame));
  if (!frame_data)
    {
      exit_process (-1);
    }
  frame_data->frame_supp = insert_page;
  insert_page->phys_frame = frame_data;
  frame_data->owner = thread_current ();

TRY_ACQUIRE_FRAME:
  frame_data->frame = palloc_get_page (PAL_USER | PAL_ZERO);

  // Matthew driving

  if (!frame_data->frame) // Need to swap out a frame
    {
      // Get a victim frame to swap out
      lock_acquire (&frame_list_access);
      size_t frame_list_size = list_size (&frame_list);
      struct data_frame* evict_tgt = NULL;
      // Vincent driving
      for (int i = 0; i < frame_list_size << 1; i++)
        {
          struct data_frame* front_frame = list_entry (
              list_pop_front (&frame_list), struct data_frame, elem);
          if (front_frame->frame_supp->locked)
            {
              list_push_back (&frame_list, &front_frame->elem);
            }
          else if (pagedir_is_accessed (front_frame->owner->pagedir,
                                        front_frame->frame_supp->address))
            {
              // Clock algorithm
              pagedir_set_accessed (front_frame->owner->pagedir,
                                    front_frame->frame_supp->address, false);
              list_push_back (&frame_list, &front_frame->elem);
            }
          else
            {
              lock_acquire (&front_frame->frame_supp->page_access);

              // Synchronize with possible destruction of front_frame
              if (front_frame->frame_supp->locked)
                {
                  list_push_back (&frame_list, &front_frame->elem);
                  continue;
                }
              evict_tgt = front_frame;
              break;
            }
        }
      lock_release (&frame_list_access);
      if (evict_tgt == NULL)
        {
          // Vincent driving
          /* No suitable evict target found, wait for frames to become available
          and try again once more frames available*/
          sema_down (&frame_wait);
          goto TRY_ACQUIRE_FRAME;
        }

      // Clear page mapping of evicted page
      pagedir_clear_page (evict_tgt->owner->pagedir,
                          evict_tgt->frame_supp->address);

      // Do nothing about evicted info if unmodified and stored in a file
      if (evict_tgt->frame_supp->in_filesys &&
          !pagedir_is_dirty (evict_tgt->owner->pagedir,
                             evict_tgt->frame_supp->address))
        {
          evict_tgt->frame_supp->in_frame = false;
        }
      else
        {
          // Matthew driving
          // Get a sector in swap space to write to
          lock_acquire (&free_sector_access);
          // Matthew driving
          if (list_empty (&free_sectors))
            {
              lock_release (&free_sector_access);
              free (frame_data);
              PANIC ("Not enough space"); // Not enough swap space
            }
          struct block_sector* free_sector = list_entry (
              list_pop_front (&free_sectors), struct block_sector, elem);
          lock_release (&free_sector_access);
          // Write victim to swap
          for (int i = 0; i < PGSIZE / BLOCK_SECTOR_SIZE; i++)
            {
              block_write (swap_block, free_sector->sector + i,
                           (char*) evict_tgt->frame + BLOCK_SECTOR_SIZE * i);
            }
          // Update supp_entry of victim
          evict_tgt->frame_supp->swap_sector = free_sector;
          evict_tgt->frame_supp->in_swap = true;
          evict_tgt->frame_supp->in_filesys = false;
          evict_tgt->frame_supp->in_frame = false;
          evict_tgt->frame_supp->phys_frame = NULL;
        }

      lock_release (&evict_tgt->frame_supp->page_access);

      // Take control of now available frame
      frame_data->frame = evict_tgt->frame;
      free (evict_tgt);
    }

  // Swap in from swap space
  if (insert_page->in_swap)
    {
      ASSERT (!insert_page->in_filesys);
      // Write to frame from swap sector
      for (int i = 0; i < PGSIZE / BLOCK_SECTOR_SIZE; i++)
        {
          block_read (swap_block, insert_page->swap_sector->sector + i,
                      (char*) frame_data->frame + BLOCK_SECTOR_SIZE * i);
        }

      // Free up swap sector
      insert_page->in_swap = false;
      lock_acquire (&free_sector_access);
      list_push_back (&free_sectors, &insert_page->swap_sector->elem);
      lock_release (&free_sector_access);
      insert_page->swap_sector = NULL;
    }

  /* Info stored in file */
  if (insert_page->in_filesys)
    {
      ASSERT (!insert_page->in_swap);
      if (insert_page->file_read_bytes)
        {
          file_seek (thread_current ()->exec_file, insert_page->file_offset);

          lock_acquire (&filesys_access);
          /* Load this page from file */
          if (file_read (thread_current ()->exec_file, frame_data->frame,
                         insert_page->file_read_bytes) !=
              insert_page->file_read_bytes)
            {
              lock_release (&filesys_access);
              free (frame_data);
              exit_process (-1);
            }
          lock_release (&filesys_access);
        }
    }

  /* Add the page to the process's address space. */
  if (!install_page (insert_page->address, frame_data->frame,
                     insert_page->writable))
    {
      free (frame_data);
      exit_process (-1);
    }
  // Vincent driving
  insert_page->in_frame = true;
  lock_acquire (&frame_list_access);
  list_push_back (&frame_list, &frame_data->elem);
  lock_release (&frame_list_access);
  sema_up(&frame_wait);
}