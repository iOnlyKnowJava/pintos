#ifndef FILESYS_INODE_H
#define FILESYS_INODE_H

#include <stdbool.h>
#include "filesys/off_t.h"
#include "devices/block.h"
#include <list.h>
#include "threads/synch.h"

/* Value that represents that sector not allocated */
#define UNALLOCATED_SECTOR (block_sector_t) - 1

#define NUM_DIRECT_INDICES                                                     \
  ((BLOCK_SECTOR_SIZE - sizeof (bool) - sizeof (off_t) - sizeof (unsigned) -   \
    sizeof (block_sector_t) - sizeof (block_sector_t)) /                       \
   sizeof (block_sector_t))

#define INDICES_PER_BLOCK (BLOCK_SECTOR_SIZE / sizeof (block_sector_t))

#define MX_FILE_LEN                                                            \
  (NUM_DIRECT_INDICES + (INDICES_PER_BLOCK + 1) * INDICES_PER_BLOCK) *         \
      BLOCK_SECTOR_SIZE

struct bitmap;

void inode_init (void);
bool inode_create (block_sector_t, off_t);
struct inode *inode_open (block_sector_t);
struct inode *inode_reopen (struct inode *);
block_sector_t inode_get_inumber (const struct inode *);
void inode_close (struct inode *);
void inode_remove (struct inode *);
off_t inode_read_at (struct inode *, void *, off_t size, off_t offset);
off_t inode_write_at (struct inode *, const void *, off_t size, off_t offset);
void inode_deny_write (struct inode *);
void inode_allow_write (struct inode *);
off_t inode_length (const struct inode *);

/* On-disk inode.
   Must be exactly BLOCK_SECTOR_SIZE bytes long. */
struct inode_disk
{
  bool is_directory; /* True if this inode is a directory*/
  off_t length;      /* File size in bytes. */
  block_sector_t direct_pointers[NUM_DIRECT_INDICES];
  block_sector_t levelone_pointer;
  block_sector_t leveltwo_pointer;
  unsigned magic; /* Magic number. */
};

/* In-memory inode. */
struct inode
{
  struct list_elem elem;         /* Element in inode list. */
  struct lock extend_write_lock; /* Lock for writes that extend inode length */
  struct lock dir_lock;          /* Lock for directory operations if needed */
  struct lock block_op_wait; /* Allows for waiting for inode's data to be read
                                in from block */
  struct lock op_lock;    /* Ensures modifications to variables is atomic */
  block_sector_t sector;  /* Sector number of disk location. */
  int open_cnt;           /* Number of openers. */
  bool removed;           /* True if deleted, false otherwise. */
  int deny_write_cnt;     /* 0: writes ok, >0: deny writes. */
  struct inode_disk data; /* Inode content. */
};

void free_index (block_sector_t sector, uint32_t lvl);
#endif /* filesys/inode.h */
