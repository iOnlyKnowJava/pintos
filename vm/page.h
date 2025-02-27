#ifndef VM_PAGE_H
#define VM_PAGE_H

#include <stdint.h>
#include "threads/thread.h"
#include "devices/block.h"
#include "vm/frame.h"
#include <hash.h>
#include "filesys/off_t.h"

/* An entry in the supplemental page table. */
struct supp_entry
{
  void* address; // User virtual address this supp_entry stores information on
  struct block_sector* swap_sector; // Stores sector where swapped out page is
  struct data_frame* phys_frame;    // Stores frame that this page is in
  bool in_filesys; // True if page stored in file system, false otherwise
  bool in_swap;    // True if page stored in swap space, false otherwise
  bool in_frame;   // True if mapped to a frame, false otherwise
  bool writable;   // True if page is writable, false otherwise
  bool locked;     // True if page is pinned, false otherwise
  uint32_t file_read_bytes; // Contains number of bytes to read from file
  off_t file_offset;        // Contains offset in file for info this page stores
  struct lock page_access; // Lock to control access to this page
  struct hash_elem elem;
};

void supp_page_table_init (void);
void supp_entry_init (struct supp_entry* entry);
struct supp_entry* get_entry (const void* address);
void supp_entry_destroy (struct hash_elem* elem, void* aux UNUSED);

#endif /* vm/page.h */