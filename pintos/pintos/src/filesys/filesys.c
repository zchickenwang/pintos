#include "filesys/filesys.h"
#include <debug.h>
#include <stdio.h>
#include <string.h>
#include "filesys/file.h"
#include "filesys/free-map.h"
#include "filesys/inode.h"
#include "filesys/directory.h"
#include "filesys/cache.h"
#include "threads/thread.h"

/* Partition that contains the file system. */
struct block *fs_device;

static void do_format (void);

/* Initializes the file system module.
   If FORMAT is true, reformats the file system. */
void
filesys_init (bool format)
{
  fs_device = block_get_role (BLOCK_FILESYS);
  if (fs_device == NULL)
    PANIC ("No file system device found, can't initialize file system.");

  cache_init ();
  inode_init ();
  free_map_init ();

  if (format)
    do_format ();

  free_map_open ();

  init_filesys = true;

  /* Assuming this function is only called once */
  thread_current ()->curr_dir = dir_open_root();
}

/* Shuts down the file system module, writing any unwritten data
   to disk. */
void
filesys_done (void)
{
  free_map_close ();
  cache_done ();
}

/* Creates a file named NAME with the given INITIAL_SIZE.
   Returns true if successful, false otherwise.
   Fails if a file named NAME already exists,
   or if internal memory allocation fails. */

bool
filesys_create (const char *name, off_t initial_size)
{
  block_sector_t inode_sector = 0;
  struct dir *dir = NULL;
  struct inode *inode = NULL;

  char filename[NAME_MAX + 1];

  bool path_finder_result = path_finder (name, &dir, filename);

  if (!path_finder_result || strcmp(filename, ".") == 0)
     return false;

  if (dir_lookup(dir, filename, &inode))
    return false;

  bool success = (dir != NULL
                  && free_map_allocate (1, &inode_sector)
                  && inode_create (inode_sector, initial_size, false)
                  && dir_add (dir, filename, inode_sector));
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
struct file *
filesys_open (const char *name)
{
  struct dir *dir = dir_open_root ();
  struct inode *inode = NULL;

  if (dir != NULL)
    dir_lookup (dir, name, &inode);
  dir_close (dir);

  return file_open (inode);
}

struct inode *
filesys_open_inode (const char *name)
{
  struct dir *dir = NULL;
  char filename[NAME_MAX + 1];
  struct inode *ref_inode;
  bool path_finder_result = path_finder(name, &dir, filename);

  if (!path_finder_result)
    return false;

  /* case where "name" is a directory that exists */
  if (strcmp (filename, ".") == 0) {
    ref_inode = dir_get_inode(dir);
    return ref_inode;

  } else {
    bool dir_lookup_result = dir_lookup (dir, filename, &ref_inode);

    if (dir_lookup_result) {
      return ref_inode;
    }
  }

  return NULL;
}

/* Deletes the file named NAME.
   Returns true if successful, false on failure.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
bool
filesys_remove (const char *name)
{
  bool success;

  struct dir *dir = NULL;
  struct dir *parent_dir = NULL;
  struct inode *sub_dir_inode = NULL;

  char filename[NAME_MAX + 1];
  char dirname[NAME_MAX + 1];
  char subdirname[NAME_MAX + 1];

  struct inode *ref_inode;
  bool has_subdir;
  bool path_finder_result = path_finder(name, &dir, filename);

  if (!path_finder_result || !dir_lookup(dir, filename, &ref_inode))
    return false;

  /* This means that we found a directory, we'll need to get the parent */
  if (strcmp(filename, ".") == 0) {

      if (dir_lookup(dir, "..", &ref_inode)) {

        /* open the parent to get a reference */
        parent_dir = dir_open(ref_inode);

        /* This will give us the dirname of the dir we're looking at currently. */
        while (strcmp(name, "") != 0) {
          get_next_part(dirname, &name);
        }

        /* Utilize dir_readdir to check if dir has directories under it. */
        has_subdir = dir_readdir (dir, subdirname);
        while (has_subdir)
          {
            if (dir_lookup (dir, subdirname, &sub_dir_inode)
                && strcmp(subdirname, ".") != 0
                && strcmp(subdirname, "..") != 0)
              {
                /* Current dir has subdirs, so we cannot remove it. */
                if (inode_isdir_check (sub_dir_inode))
                  {
                    dir_close (dir);
                    return false;
                  }
              }
            has_subdir = dir_readdir (dir, subdirname);
          }

        if (parent_dir != NULL)
            success = dir_remove (parent_dir, dirname);
        else
            success = false;

        /* Reset the offset of the parent directory, and close dirs */
        dir_reset_pos (parent_dir);
        dir_close (parent_dir);
        dir_close (dir);

    } else {
        return false;
    }

  } else {
    success = dir != NULL && dir_remove (dir, filename);
    dir_close (dir);
  }

  return success;
}

/* Formats the file system. */
static void
do_format (void)
{
  printf ("Formatting file system...");
  free_map_create ();
  if (!dir_create (ROOT_DIR_SECTOR, 16))
    PANIC ("root directory creation failed");
  free_map_close ();
  printf ("done.\n");
}

/* Extracts a file name part from *SRCP into PART, and updates *SRCP so that the
next call will return the next file name part. Returns 1 if successful, 0 at
end of string, -1 for a too-long file name part. */
int
get_next_part (char part[NAME_MAX + 1], const char **srcp)
{
  const char *src = *srcp;
  char *dst = part;

  /* Skip leading slashes. If it’s all slashes, we’re done. */
  while (*src == '/')
    src++;
  if (*src == '\0')
    return 0;

  /* Copy up to NAME_MAX character from SRC to DST. Add null terminator. */
  while (*src != '/' && *src != '\0')
    {
      if (dst < part + NAME_MAX)
        *dst++ = *src;
      else
        return -1;
      src++;
    }
  *dst = '\0';

  /* Advance source pointer. */
  *srcp = src;
  return 1;
}
/*
This function will take a path and return true if it
finds a directory corresponding to the path provided. It will be
false if this dirrectory does not exist.

If the path provided specifies a file, the desired_dir will
be populated with the base directory of the file and the filename
will have the name of the file within that directory. If the path variable
specifies a directory, then the filename will be set to ".". 
*/
bool
path_finder (const char *path, struct dir **desired_dir, char filename[NAME_MAX + 1])
{
  if (path == NULL || strlen (path) == 0)
    return false;

  if (path[0] == '\0')
    return false;

  struct dir *ref_dir = NULL;
  struct inode *ref_inode;
  struct inode *next_ref_inode;
  bool dir_lookup_result;

  char part[NAME_MAX + 1];
  strlcpy (filename, ".", 2);

  if (path[0] == '/') {
      ref_dir = dir_open_root ();
      ref_inode = dir_get_inode(ref_dir);
  } else {
      ref_dir = thread_get_dir();
      ref_inode = dir_get_inode(ref_dir);
      if (inode_removed (ref_inode))
        return false;
      inode_reopen(ref_inode);
  }

  while ((get_next_part (part, &path)) == 1) {

      /* A file was previously found, so get_next_part should NOT return 1. */
      if (strcmp (filename, ".") != 0)
        return false;

      /* make sure to open the directory */
      ref_dir = dir_open(inode_reopen(ref_inode));

      /* returns false if indoe does not exist in the passed in directory */
      dir_lookup_result = dir_lookup (ref_dir, part, &next_ref_inode);
      dir_close(ref_dir);

      /* found a directory */
      if (dir_lookup_result && inode_isdir(next_ref_inode)) {
          inode_close(ref_inode);
          ref_inode = next_ref_inode;

      /* found a file or didn't find anything */
      } else {

          /* ensuring that this is the end of the path */
          if ((get_next_part (part, &path)) == 1) {
            return false;
          }

          strlcpy(filename, part, strlen(part) + 1);
      }
  }

  *desired_dir = dir_open(ref_inode);

  /* return false upon directory not able to be opened. */
  if (desired_dir == NULL)
    return false;

  return true;

}
