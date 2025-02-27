#include <stdint.h>
#include "vm/page.h"
#include "threads/thread.h"
#include "threads/vaddr.h"

extern struct list frame_list;

extern struct semaphore frame_list_access;

extern struct list free_sectors;

extern struct lock free_sector_access;

/* Returns a hash value for page p. Hashes pages based on address */
unsigned page_hash (const struct hash_elem *p_, void *aux UNUSED)
{
  const struct supp_entry *p = hash_entry (p_, struct supp_entry, elem);
  return hash_bytes (&p->address, sizeof p->address);
}

/* Comparator for keys of supp_page_table, used as hash_less_func */
bool supp_entry_cmp (const struct hash_elem *a, const struct hash_elem *b,
                     void *aux UNUSED)
{
  return hash_entry (a, struct supp_entry, elem)->address <
         hash_entry (b, struct supp_entry, elem)->address;
}

/* Initializes the supplemental page table */
void supp_page_table_init ()
{
  hash_init (&thread_current ()->supp_page_table, &page_hash, &supp_entry_cmp,
             NULL);
}

/* Initializes a single supplemental page table entry */
void supp_entry_init (struct supp_entry *entry)
{
  entry->in_filesys = entry->in_swap = entry->locked = entry->in_frame = false;
  lock_init (&entry->page_access);
  entry->address = entry->swap_sector = entry->phys_frame = NULL;
  entry->file_offset = entry->file_read_bytes = 0;
  entry->writable = true;
}

/* Frees the supplemental page table entry and makes swap sector or frame it
 * occupies available, used as hash_action_func for hash_destroy */
void supp_entry_destroy (struct hash_elem *elem, void *aux UNUSED)
{
  // Matthew drove here
  struct supp_entry *entry = hash_entry (elem, struct supp_entry, elem);
  lock_acquire (&entry->page_access);
  entry->locked = true;
  lock_release (&entry->page_access);

  // Remove page from frame if in a frame
  lock_acquire (&frame_list_access);
  if (entry->in_frame)
    {
      list_remove (&entry->phys_frame->elem);
      lock_release (&frame_list_access);
      free (entry->phys_frame);
    }
  else
    {
      lock_release (&frame_list_access);
    }

  // Remove from swap space if in swap space
  if (entry->in_swap)
    {
      lock_acquire (&free_sector_access);
      list_push_back (&free_sectors, &entry->swap_sector->elem);
      lock_release (&free_sector_access);
    }
  free (entry);
}

/* Returns corresponding supplemental entry of address, or NULL if address has
 * no mapping */
struct supp_entry *get_entry (const void *address)
{
  struct supp_entry temp;
  // Vincent driving
  temp.address = (void *) ((unsigned) address & ~PGMASK);
  struct hash_elem *elem =
      hash_find (&thread_current ()->supp_page_table, &temp.elem);
  if (elem == NULL)
    {
      return NULL;
    }
  return hash_entry (elem, struct supp_entry, elem);
}