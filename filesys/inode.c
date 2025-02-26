#include "filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "threads/malloc.h"

/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44

/* Returns the number of sectors to allocate for an inode SIZE
   bytes long. */
static inline size_t bytes_to_sectors (off_t size)
{
  return DIV_ROUND_UP (size, BLOCK_SECTOR_SIZE);
}

// Vincent driving
/* Fills given sector with provided val */
inline void set_block_val (block_sector_t sector, block_sector_t val)
{
  block_sector_t buffer[INDICES_PER_BLOCK];
  ASSERT (sizeof (buffer) == BLOCK_SECTOR_SIZE);
  for (int i = 0; i < INDICES_PER_BLOCK; i++)
    {
      buffer[i] = val;
    }
  block_write (fs_device, sector, buffer);
}

/* Returns the block device sector that contains byte offset POS
   within INODE.
   Returns -1 if INODE does not contain data for a byte at offset
   POS. */
static block_sector_t byte_to_sector (const struct inode *inode, off_t pos,
                                      bool allocate)
{
  ASSERT (inode != NULL);
  if (pos >= MX_FILE_LEN)
    {
      return -1;
    }
  // Matthew driving
  // direct indices
  pos /= BLOCK_SECTOR_SIZE;
  if (pos < NUM_DIRECT_INDICES)
    {
      if (inode->data.direct_pointers[pos] == UNALLOCATED_SECTOR && allocate)
        {
          if (free_map_allocate (1, &inode->data.direct_pointers[pos]))
            {
              set_block_val (inode->data.direct_pointers[pos], 0);
            }
        }
      return inode->data.direct_pointers[pos];
    }
  pos -= NUM_DIRECT_INDICES;

  // level one pointer
  if (pos < INDICES_PER_BLOCK)
    {
      if (inode->data.levelone_pointer == UNALLOCATED_SECTOR)
        {
          if (!allocate ||
              !free_map_allocate (1, &inode->data.levelone_pointer))
            {
              return UNALLOCATED_SECTOR;
            }
          set_block_val (inode->data.levelone_pointer, UNALLOCATED_SECTOR);
        }
      block_sector_t buffer[INDICES_PER_BLOCK];
      block_read (fs_device, inode->data.levelone_pointer, buffer);
      // Matthew driving
      if (buffer[pos] == UNALLOCATED_SECTOR && allocate)
        {
          if (free_map_allocate (1, &buffer[pos]))
            {
              set_block_val (buffer[pos], 0);
              block_write (fs_device, inode->data.levelone_pointer, buffer);
            }
        }
      return buffer[pos];
    }
  pos -= INDICES_PER_BLOCK;

  // level two pointer
  if (inode->data.leveltwo_pointer == UNALLOCATED_SECTOR)
    {
      if (!allocate || !free_map_allocate (1, &inode->data.leveltwo_pointer))
        {
          return UNALLOCATED_SECTOR;
        }
      set_block_val (inode->data.leveltwo_pointer, UNALLOCATED_SECTOR);
    }
  block_sector_t buffer[INDICES_PER_BLOCK];
  // Vincent driving
  block_read (fs_device, inode->data.leveltwo_pointer, buffer);
  off_t lvl_2_idx = pos / INDICES_PER_BLOCK;
  off_t lvl_1_idx = pos % INDICES_PER_BLOCK;
  if (buffer[lvl_2_idx] == UNALLOCATED_SECTOR)
    {
      if (!allocate || !free_map_allocate (1, &buffer[lvl_2_idx]))
        {
          return UNALLOCATED_SECTOR;
        }
      block_write (fs_device, inode->data.leveltwo_pointer, buffer);
      set_block_val (buffer[lvl_2_idx], UNALLOCATED_SECTOR);
    }
  block_sector_t save_sector = buffer[lvl_2_idx];
  block_read (fs_device, buffer[lvl_2_idx], buffer);
  if (buffer[lvl_1_idx] == UNALLOCATED_SECTOR && allocate)
    {
      if (free_map_allocate (1, &buffer[lvl_1_idx]))
        {
          set_block_val (buffer[lvl_1_idx], 0);
          block_write (fs_device, save_sector, buffer);
        }
    }
  return buffer[lvl_1_idx];
}

/* List of open inodes, so that opening a single inode twice
   returns the same `struct inode'. */
static struct list open_inodes;

static struct lock inode_list_access;

/* Initializes the inode module. */
void inode_init (void)
{
  list_init (&open_inodes);
  lock_init (&inode_list_access);
}

/* Releases sectors held by sector, given that sector is a level lvl index */
void free_index (block_sector_t sector, uint32_t lvl)
{
  if (sector == UNALLOCATED_SECTOR)
    {
      return;
    }
  if (lvl == 0)
    {
      free_map_release (sector, 1);
      return;
    }
  block_sector_t buffer[INDICES_PER_BLOCK];
  block_read (fs_device, sector, buffer);
  for (int i = 0; i < INDICES_PER_BLOCK; i++)
    {
      if (buffer[i] != UNALLOCATED_SECTOR)
        free_index (buffer[i], lvl - 1);
    }
  free_map_release (sector, 1);
}

/* Allocates an indirect index of level lvl that will store sectors
sectors. If sectors is greater than what can be stored by the index,
as much is allocated as possible. If allocation fails, returns
UNALLOCATED_SECTOR */
block_sector_t create_index (size_t sectors, uint32_t lvl)
{
  // Vincent driving
  if (sectors == 0)
    {
      return UNALLOCATED_SECTOR;
    }
  block_sector_t retval = UNALLOCATED_SECTOR;

  free_map_allocate (1, &retval);
  if (lvl == 0 || retval == UNALLOCATED_SECTOR)
    {
      return retval;
    }
  // Matthew driving
  uint32_t sectors_in_index = 1;
  for (int i = 0; i < lvl - 1; i++)
    {
      sectors_in_index *= INDICES_PER_BLOCK;
    }
  block_sector_t buffer[INDICES_PER_BLOCK];
  bool good = true;
  for (int i = 0; i < INDICES_PER_BLOCK; i++)
    {
      buffer[i] = UNALLOCATED_SECTOR;
      if (sectors && good)
        {
          uint32_t remove =
              sectors >= sectors_in_index ? sectors_in_index : sectors;
          buffer[i] = create_index (remove, lvl - 1);
          // Matthew driving
          if (buffer[i] == UNALLOCATED_SECTOR)
            {
              good = false;
            }

          sectors -= remove;
        }
    }
  block_write (fs_device, retval, buffer);
  if (!good)
    {
      free_index (retval, lvl);
      retval = UNALLOCATED_SECTOR;
    }
  return retval;
}

/* Initializes an inode with LENGTH bytes of data and
   writes the new inode to sector SECTOR on the file system
   device.
   Returns true if successful.
   Returns false if memory or disk allocation fails. */
bool inode_create (block_sector_t sector, off_t length)
{
  struct inode_disk *disk_inode = NULL;
  ASSERT (length >= 0);
  ASSERT (length < MX_FILE_LEN);

  /* If this assertion fails, the inode structure is not exactly
     one sector in size, and you should fix that. */
  ASSERT (sizeof *disk_inode == BLOCK_SECTOR_SIZE);
  disk_inode = calloc (1, sizeof *disk_inode);
  if (disk_inode != NULL)
    {
      disk_inode->is_directory = false;
      size_t sectors = bytes_to_sectors (length);
      disk_inode->magic = INODE_MAGIC;
      disk_inode->length = length;

      // Initialize everything to unallocated
      for (int i = 0; i < NUM_DIRECT_INDICES; i++)
        {
          disk_inode->direct_pointers[i] = UNALLOCATED_SECTOR;
        }
      disk_inode->levelone_pointer = disk_inode->leveltwo_pointer =
          UNALLOCATED_SECTOR;

      // Fill in direct indices
      for (int i = 0; i < NUM_DIRECT_INDICES && sectors; i++)
        {
          disk_inode->direct_pointers[i] = create_index (sectors, 0);
          if (disk_inode->direct_pointers[i] == UNALLOCATED_SECTOR)
            {
              goto FAIL_ALLOCATION;
            }
          sectors--;
        }

      // First layer indirect index
      if (sectors)
        {
          disk_inode->levelone_pointer = create_index (sectors, 1);
          if (disk_inode->levelone_pointer == UNALLOCATED_SECTOR)
            {
              goto FAIL_ALLOCATION;
            }
          sectors -= sectors > INDICES_PER_BLOCK ? INDICES_PER_BLOCK : sectors;
        }
      // Second layer indirect index
      if (sectors)
        {
          disk_inode->leveltwo_pointer = create_index (sectors, 2);
          if (disk_inode->leveltwo_pointer == UNALLOCATED_SECTOR)
            {
              goto FAIL_ALLOCATION;
            }
        }
      block_write (fs_device, sector, disk_inode);
      free (disk_inode);
      return true;

    FAIL_ALLOCATION:
      for (int i = 0; i < NUM_DIRECT_INDICES; i++)
        {
          free_index (disk_inode->direct_pointers[i], 0);
        }
      free_index (disk_inode->levelone_pointer, 1);
      free_index (disk_inode->leveltwo_pointer, 2);

      free (disk_inode);
    }
  return false;
}

/* Reads an inode from SECTOR
   and returns a `struct inode' that contains it.
   Returns a null pointer if memory allocation fails. */
struct inode *inode_open (block_sector_t sector)
{
  struct list_elem *e;
  struct inode *inode;

  lock_acquire (&inode_list_access);
  /* Check whether this inode is already open. */
  for (e = list_begin (&open_inodes); e != list_end (&open_inodes);
       e = list_next (e))
    {
      inode = list_entry (e, struct inode, elem);
      if (inode->sector == sector)
        {
          lock_release (&inode_list_access);
          // Wait for inode to be read in if not read in yet
          lock_acquire (&inode->block_op_wait);
          lock_release (&inode->block_op_wait);
          inode_reopen (inode);
          return inode;
        }
    }

  /* Allocate memory. */
  inode = malloc (sizeof *inode);
  if (inode == NULL)
    {
      lock_release (&inode_list_access);
      return NULL;
    }

  /* Initialize. */
  list_push_front (&open_inodes, &inode->elem);
  lock_init (&inode->block_op_wait);
  // Force other threads trying to get inode to wait for read to finish
  lock_acquire (&inode->block_op_wait);
  // Avoid holding global lock shared by all inodes for too long
  lock_release (&inode_list_access);

  inode->sector = sector;
  inode->open_cnt = 1;
  inode->deny_write_cnt = 0;
  inode->removed = false;
  lock_init (&inode->extend_write_lock);
  lock_init (&inode->dir_lock);
  lock_init (&inode->op_lock);
  block_read (fs_device, inode->sector, &inode->data);
  lock_release (&inode->block_op_wait);
  return inode;
}

/* Reopens and returns INODE. */
struct inode *inode_reopen (struct inode *inode)
{
  if (inode != NULL)
    {
      lock_acquire (&inode->op_lock);
      inode->open_cnt++;
      lock_release (&inode->op_lock);
    }
  return inode;
}

/* Returns INODE's inode number. */
block_sector_t inode_get_inumber (const struct inode *inode)
{
  return inode->sector;
}

/* Closes INODE and writes it to disk.
   If this was the last reference to INODE, frees its memory.
   If INODE was also a removed inode, frees its blocks. */
void inode_close (struct inode *inode)
{
  /* Ignore null pointer. */
  if (inode == NULL)
    return;

  if (!inode->removed)
    {
      block_write (fs_device, inode->sector, &inode->data);
    }
  lock_acquire (&inode_list_access);
  /* Release resources if this was the last opener. */
  lock_acquire (&inode->op_lock);
  if (--inode->open_cnt == 0)
    {
      lock_release (&inode->op_lock);
      /* Remove from inode list and release lock. */
      list_remove (&inode->elem);
      lock_release (&inode_list_access);
      /* Deallocate blocks if removed. */
      if (inode->removed)
        {
          free_map_release (inode->sector, 1);

          // Matthew driving
          for (int i = 0; i < NUM_DIRECT_INDICES; i++)
            {
              if (inode->data.direct_pointers[i] != UNALLOCATED_SECTOR)
                free_map_release (inode->data.direct_pointers[i], 1);
            }
          free_index (inode->data.levelone_pointer, 1);
          free_index (inode->data.leveltwo_pointer, 2);
        }
      free (inode);
    }
  else
    {
      lock_release (&inode->op_lock);
      lock_release (&inode_list_access);
    }
}

/* Marks INODE to be deleted when it is closed by the last caller who
   has it open. */
void inode_remove (struct inode *inode)
{
  ASSERT (inode != NULL);
  inode->removed = true;
}

/* Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
   Returns the number of bytes actually read, which may be less
   than SIZE if an error occurs or end of file is reached. */
off_t inode_read_at (struct inode *inode, void *buffer_, off_t size,
                     off_t offset)
{
  uint8_t *buffer = buffer_;
  off_t bytes_read = 0;
  uint8_t *bounce = NULL;

  while (size > 0)
    {
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually copy out of this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      // Vincent driving
      /* Disk sector to read, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset, false);
      // Vincent driving
      if (sector_idx == UNALLOCATED_SECTOR)
        {
          // Block not allocated, just put zeros
          for (int i = 0; i < chunk_size; i++)
            {
              *(buffer + bytes_read + i) = 0;
            }
          goto CONTINUE_LOOP;
        }
      if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
        {
          /* Read full sector directly into caller's buffer. */
          block_read (fs_device, sector_idx, buffer + bytes_read);
        }
      else
        {
          /* Read sector into bounce buffer, then partially copy
             into caller's buffer. */
          if (bounce == NULL)
            {
              bounce = malloc (BLOCK_SECTOR_SIZE);
              if (bounce == NULL)
                break;
            }
          block_read (fs_device, sector_idx, bounce);
          memcpy (buffer + bytes_read, bounce + sector_ofs, chunk_size);
        }

    CONTINUE_LOOP:

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_read += chunk_size;
    }
  free (bounce);

  return bytes_read;
}

/* Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
   Returns the number of bytes actually written, which may be
   less than SIZE if an error occurs. */
off_t inode_write_at (struct inode *inode, const void *buffer_, off_t size,
                      off_t offset)
{
  const uint8_t *buffer = buffer_;
  off_t bytes_written = 0;
  uint8_t *bounce = NULL;

  if (inode->deny_write_cnt)
    return 0;

  // Vincent driving
  bool acquired_lock = false;
  // Grow file
  if (inode->data.length < size + offset)
    {
      acquired_lock = true;
      lock_acquire (&inode->extend_write_lock);
    }

  while (size > 0)
    {
      /* Sector to write, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset, true);

      if (sector_idx == UNALLOCATED_SECTOR)
        {
          // Couldn't allocate a sector to write to
          break;
        }

      int sector_ofs = offset % BLOCK_SECTOR_SIZE;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;

      /* Number of bytes to actually write into this sector. */
      int chunk_size = size < sector_left ? size : sector_left;
      if (chunk_size <= 0)
        break;

      if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
        {
          /* Write full sector directly to disk. */
          block_write (fs_device, sector_idx, buffer + bytes_written);
        }
      else
        {
          /* We need a bounce buffer. */
          if (bounce == NULL)
            {
              bounce = malloc (BLOCK_SECTOR_SIZE);
              if (bounce == NULL)
                break;
            }

          /* If the sector contains data before or after the chunk
             we're writing, then we need to read in the sector
             first.  Otherwise we start with a sector of all zeros. */
          if (sector_ofs > 0 || chunk_size < sector_left)
            block_read (fs_device, sector_idx, bounce);
          else
            memset (bounce, 0, BLOCK_SECTOR_SIZE);
          memcpy (bounce + sector_ofs, buffer + bytes_written, chunk_size);
          block_write (fs_device, sector_idx, bounce);
        }

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_written += chunk_size;
    }
  if (inode->data.length < offset)
    {
      inode->data.length = offset;
    }
  if (acquired_lock)
    lock_release (&inode->extend_write_lock);
  free (bounce);
  return bytes_written;
}

/* Disables writes to INODE.
   May be called at most once per inode opener. */
void inode_deny_write (struct inode *inode)
{
  lock_acquire (&inode->op_lock);
  inode->deny_write_cnt++;
  lock_release (&inode->op_lock);
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
}

/* Re-enables writes to INODE.
   Must be called once by each inode opener who has called
   inode_deny_write() on the inode, before closing the inode. */
void inode_allow_write (struct inode *inode)
{
  ASSERT (inode->deny_write_cnt > 0);
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
  lock_acquire (&inode->op_lock);
  inode->deny_write_cnt--;
  lock_release (&inode->op_lock);
}

/* Returns the length, in bytes, of INODE's data. */
off_t inode_length (const struct inode *inode) { return inode->data.length; }