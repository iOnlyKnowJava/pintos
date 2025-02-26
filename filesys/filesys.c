#include "threads/thread.h"
#include "filesys/filesys.h"
#include <debug.h>
#include <stdio.h>
#include <string.h>
#include "filesys/file.h"
#include "filesys/free-map.h"
#include "filesys/inode.h"
#include "filesys/directory.h"

/* Constant for "." string */
const char *dot_string = ".";

/* Partition that contains the file system. */
struct block *fs_device;

static void do_format (void);

/* Initializes the file system module.
   If FORMAT is true, reformats the file system. */
void filesys_init (bool format)
{
  fs_device = block_get_role (BLOCK_FILESYS);
  if (fs_device == NULL)
    PANIC ("No file system device found, can't initialize file system.");

  inode_init ();
  free_map_init ();

  if (format)
    do_format ();

  free_map_open ();
  // Indicate that root is a directory
  struct inode *root = inode_open (ROOT_DIR_SECTOR);
  root->data.is_directory = true;
  thread_current ()->curr_directory = dir_open (root);
}

/* Shuts down the file system module, writing any unwritten data
   to disk. */
void filesys_done (void) { free_map_close (); }

// Matthew driving
/* Parse the given path, returning the directory that it lives in and setting
 * last_term to its file name. */
struct dir *get_dir (const char *s, char **last_term)
{
  // Invalid string
  if (*s == '\0')
    {
      return NULL;
    }
  // Preserve s contents by making copy
  size_t len = strlen (s) + 1;
  char path[len];
  memcpy (path, s, len);

  // Absolute vs relative path
  struct dir *curr = (path[0] == '/') ?
                         dir_open_root () :
                         dir_reopen (thread_current ()->curr_directory);
  if (curr == NULL)
    return NULL;

  char *token = NULL;
  char *next = NULL;
  char *save_ptr = NULL;
  // Matthew driving
  // Deals with case of s = "/"
  *last_term = (*s == '/' ? dot_string : s);
  for (token = strtok_r (path, "/", &save_ptr); token != NULL; token = next)
    {
      next = strtok_r (NULL, "/", &save_ptr);
      *last_term = s + ((uint32_t) token - (uint32_t) path);
      if (next == NULL)
        {
          break;
        }
      struct inode *temp = NULL;
      bool success = dir_lookup (curr, token, &temp);
      dir_close (curr);
      // Vincent driving
      if (success && temp->data.is_directory)
        {
          curr = dir_open (temp);
          if (curr == NULL)
            return NULL;
        }
      else
        {
          inode_close (temp);
          return NULL;
        }
    }
  return curr;
}

/* Creates a file named NAME with the given INITIAL_SIZE.
   Returns true if successful, false otherwise.
   Fails if a file named NAME already exists,
   or if internal memory allocation fails. */
bool filesys_create (const char *name, off_t initial_size, bool is_dir)
{

  block_sector_t inode_sector = 0;
  char *filename = NULL;
  struct dir *dir = get_dir (name, &filename);
  bool success = (dir != NULL && free_map_allocate (1, &inode_sector) &&
                  (is_dir ? dir_create (inode_sector, initial_size) :
                            inode_create (inode_sector, initial_size)));
  if (success)
    {
      if (is_dir)
        {
          // try to add parent entry to child directory
          struct dir *child_dir = dir_open (inode_open (inode_sector));
          success = child_dir != NULL &&
                    dir_add (child_dir, "..", dir->inode->sector) &&
                    dir_add (dir, filename, inode_sector);
          dir_close (child_dir);
        }
      else
        {
          // just add file to dir
          success = dir_add (dir, filename, inode_sector);
        }
    }
  if (!success && inode_sector != 0)
    free_map_release (inode_sector, 1);
  dir_close (dir);
  return success;
}

/* Opens the file with the given NAME.
   Returns the new file if successful or a null pointer
   otherwise.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
struct file *filesys_open (const char *name)
{
  char *filename = NULL;
  struct dir *dir = get_dir (name, &filename);
  struct inode *inode = NULL;

  if (dir != NULL)
    dir_lookup (dir, filename, &inode);
  dir_close (dir);

  return file_open (inode);
}

/* Deletes the file named NAME.
   Returns true if successful, false on failure.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
bool filesys_remove (const char *name)
{
  char *filename = NULL;
  struct dir *dir = get_dir (name, &filename);
  if (strcmp (filename, ".") == 0 || strcmp (filename, "..") == 0)
    return false;
  bool success = dir != NULL && dir_remove (dir, filename);
  dir_close (dir);

  return success;
}

/* Formats the file system. */
static void do_format (void)
{
  printf ("Formatting file system...");
  free_map_create ();
  if (!dir_create (ROOT_DIR_SECTOR, 16))
    PANIC ("root directory creation failed");
  free_map_close ();
  printf ("done.\n");
}
