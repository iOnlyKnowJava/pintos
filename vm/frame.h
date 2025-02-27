#ifndef VM_FRAME_H
#define VM_FRAME_H

#include <list.h>
#include <stdint.h>
#include "threads/thread.h"
#include <hash.h>
#include "devices/block.h"

/* Represents a system frame */
struct data_frame
{
  struct list_elem elem;
  void* frame; // Address from user pool in kernel virtual address space
  struct thread* owner;          // Thread that currently occupies this frame
  struct supp_entry* frame_supp; // Supplementary info of this frame
};

// Vincent driving
/* Represents a sector of the swap space of size PGSIZE bytes */
struct block_sector
{
  struct list_elem elem;

  /* Sector number that this block_sector starts at.
  The space this block_sector represents extends to PGSIZE bytes,
  and must therefore span multiple consecutive sectors */
  block_sector_t sector;
};

/* Initializes the supplemental frame table */
void frametable_init (void);

/* Sets the the page user_addr belongs to as valid.
Returns the supplemental information added to the supplemental page table, or
returns the existing supplemental info if the address was already valid  */
struct supp_entry* get_frame (void* user_addr);

/* Swaps the frame insert_frame in, getting the information needed from the swap
space or file system, and also evicts a page to the swap space if necessary */
void swap_frame (struct supp_entry* insert_page);

#endif /* vm/frame.h */