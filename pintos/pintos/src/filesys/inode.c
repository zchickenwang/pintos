#include "filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "filesys/cache.h"
#include "threads/malloc.h"

/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44

/* Helpers used to allocate / deallocate blocks from inode a la Unix */
bool inode_change_block (struct inode_disk *inode_disk,
    block_sector_t sector, bool add);
bool calculate_indices (int block, int *offsets, int *offset_cnt);

/* Returns cache hit/miss rate count */
int
get_cache_stats(int stats)
{
  if (stats == 0) 
    return cache_miss;
  else
    return cache_calls;
}

/* Returns the number of sectors to allocate for an inode SIZE
   bytes long. */
static inline size_t
bytes_to_sectors (off_t size)
{
  return DIV_ROUND_UP (size, BLOCK_SECTOR_SIZE);
}

/* Returns the block device sector that contains byte offset POS
   within INODE.
   Returns -1 if INODE does not contain data for a byte at offset
   POS. */
static block_sector_t
byte_to_sector (const struct inode *inode, off_t pos)
{
  ASSERT(inode != NULL);

  // Malloc structs that read off pointers in this inode
  struct inode_disk *inode_disk = malloc (sizeof (struct inode_disk));
  struct indirect_inode_disk *indirect_inode_disk =
      malloc (sizeof (struct indirect_inode_disk));
  struct doubly_indirect_inode_disk *doubly_indirect_inode_disk =
      malloc (sizeof (struct doubly_indirect_inode_disk));

  block_sector_t result = -1;
  cache_read_block (inode->sector, inode_disk);

  int offsets[2];
  int offset_cnt;
  // Finds the sector indices for an offset in this inode data
  calculate_indices (pos / BLOCK_SECTOR_SIZE, offsets, &offset_cnt);

  // Direct pointer
  if (offset_cnt == 1)
  {
    // Check if block has been allocated
    if (inode_disk->pointers[offsets[0]] == 0) {
      result = -1;
      goto done;
    }
    result = inode_disk->pointers[offsets[0]];
  }

  // Indirect pointer
  if (offset_cnt == 2)
  {
    // Check if indirect block has been allocated
    if (inode_disk->pointers[122] == 0) {
      result = -1;
      goto done;
    }
    cache_read_block (inode_disk->pointers[122], indirect_inode_disk);
    // Check if indirect's direct block has been allocated
    if (indirect_inode_disk->pointers[offsets[0]] == 0) {
      result = -1;
      goto done;
    }
    result = indirect_inode_disk->pointers[offsets[0]];
  }

  // Doubly indirect pointer
  if (offset_cnt == 3)
  {
    // Check if doubly indirect block has been allocated
    if (inode_disk->pointers[123] == 0) {
      result = -1;
      goto done;
    }
    cache_read_block (inode_disk->pointers[123], doubly_indirect_inode_disk);
    // Check if indirect block has been allocated
    if (doubly_indirect_inode_disk->pointers[offsets[0]] == 0) {
      result = -1;
      goto done;
    }
    cache_read_block (doubly_indirect_inode_disk->pointers[offsets[0]],
        indirect_inode_disk);
    // Check if direct block has been allocated
    if (indirect_inode_disk->pointers[offsets[1]] == 0) {
      result = -1;
      goto done;
    }
    result = indirect_inode_disk->pointers[offsets[1]];
  }

  // Free all structs
  done:
  free (inode_disk);
  free (indirect_inode_disk);
  free (doubly_indirect_inode_disk);
  return result;
}

/* List of open inodes, so that opening a single inode twice
   returns the same `struct inode'. */
static struct list open_inodes;

/* Initializes the inode module. */
void
inode_init (void)
{
  list_init (&open_inodes);
}

/* Initializes an inode with LENGTH bytes of data and
   writes the new inode to sector SECTOR on the file system
   device.
   Returns true if successful.
   Returns false if memory or disk allocation fails. */
bool
inode_create (block_sector_t sector, off_t length, bool is_directory)
{
  struct inode_disk *disk_inode = NULL;
  bool success = false;

  /* If this assertion fails, the inode structure is not exactly
     one sector in size, and you should fix that. */
  ASSERT (sizeof *disk_inode == BLOCK_SECTOR_SIZE);

  disk_inode = calloc (1, sizeof *disk_inode);
  if (disk_inode != NULL)
    {
      disk_inode->magic = INODE_MAGIC;
      disk_inode->is_dir = is_directory;

      if (inode_resize_file (disk_inode, length))
        {
          success = true;
          cache_write_block (sector, disk_inode);
        }
      free (disk_inode);
    }
  return success;
}

/* Reads an inode from SECTOR
   and returns a `struct inode' that contains it.
   Returns a null pointer if memory allocation fails. */
struct inode *
inode_open (block_sector_t sector)
{
  struct list_elem *e;
  struct inode *inode;

  /* Check whether this inode is already open. */
  for (e = list_begin (&open_inodes); e != list_end (&open_inodes);
       e = list_next (e))
    {
      inode = list_entry (e, struct inode, elem);
      if (inode->sector == sector)
        {
          inode_reopen (inode);
          cache_read_block (inode->sector, &inode->data);
          return inode;
        }
    }

  /* Allocate memory. */
  inode = malloc (sizeof *inode);
  if (inode == NULL)
    return NULL;

  /* Initialize. */
  list_push_front (&open_inodes, &inode->elem);
  inode->sector = sector;
  inode->open_cnt = 1;
  inode->deny_write_cnt = 0;
  inode->removed = false;
  cache_read_block (inode->sector, &inode->data);
  inode->is_dir = inode->data.is_dir;
  sema_init (&inode->alloc_sema, 1);
  return inode;
}

/* Reopens and returns INODE. */
struct inode *
inode_reopen (struct inode *inode)
{
  if (inode != NULL)
    inode->open_cnt++;
  return inode;
}

/* Returns INODE's inode number. */
block_sector_t
inode_get_inumber (const struct inode *inode)
{
  return inode->sector;
}

/* Returns whether an inode is a directory */
bool
inode_isdir (const struct inode *inode)
{
  if (inode == NULL)
    return false;
  return inode->is_dir;
}

/* Increments the open count of an inode */
void
inode_increment_open_count (struct inode *inode)
{
  inode->open_cnt++;
}

/* Closes INODE and writes it to disk.
   If this was the last reference to INODE, frees its memory.
   If INODE was also a removed inode, frees its blocks. */
void
inode_close (struct inode *inode)
{
  /* Ignore null pointer. */
  if (inode == NULL)
    return;

  /* Release resources if this was the last opener. */
  if (--inode->open_cnt == 0)
    {
      /* Remove from inode list and release lock. */
      list_remove (&inode->elem);

      /* Deallocate blocks if removed. */
      if (inode->removed)
        {
          // Deallocate all blocks pointed to by this inode!
          struct inode_disk *resize_temp =
              malloc (sizeof (struct inode_disk));
          sema_down (&inode->alloc_sema);
          cache_read_block (inode->sector, resize_temp);
          inode_resize_file(resize_temp, 0);
          free (resize_temp);
          free_map_release(inode->sector, 1);
          sema_up (&inode->alloc_sema);
        }

      free (inode);
    }
}

/* Marks INODE to be deleted when it is closed by the last caller who
   has it open. */
void
inode_remove (struct inode *inode)
{
  ASSERT (inode != NULL);
  inode->removed = true;
}

/* Returns if an inode has been removed */
bool
inode_removed (struct inode *inode)
{
  return inode->removed;
}

/* Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
   Returns the number of bytes actually read, which may be less
   than SIZE if an error occurs or end of file is reached. */
off_t
inode_read_at (struct inode *inode, void *buffer_, off_t size, off_t offset)
{
  uint8_t *buffer = buffer_;
  off_t bytes_read = 0;
  uint8_t *bounce = NULL;

  while (size > 0)
    {
      /* Disk sector to read, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset);
      // If this sector has not been allocated, we need to exit
      if (sector_idx == -1) {
        return bytes_read;
      }
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually copy out of this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
        {
          /* Read full sector directly into caller's buffer. */
          cache_read_block (sector_idx, buffer + bytes_read);
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
          cache_read_block (sector_idx, bounce);
          memcpy (buffer + bytes_read, bounce + sector_ofs, chunk_size);
        }

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
   less than SIZE if end of file is reached or an error occurs.
   (Normally a write at end of file would extend the inode, but
   growth is not yet implemented. NOW IT IS BABY! WOO!) */
off_t
inode_write_at (struct inode *inode, const void *buffer_, off_t size,
                off_t offset)
{
  const uint8_t *buffer = buffer_;
  off_t bytes_written = 0;
  uint8_t *bounce = NULL;

  if (inode->deny_write_cnt)
    return 0;

  // Check if we need to resize the file with this write
  struct inode_disk *resize_temp = malloc (sizeof (struct inode_disk));
  // Down on the allocate sema so nobody else is resizing
  sema_down (&inode->alloc_sema);
  cache_read_block (inode->sector, resize_temp);
  if (resize_temp->length < offset + size)
  {
    if (!inode_resize_file(resize_temp, offset + size)) {
      // if the resize fails, we exit
      sema_up (&inode->alloc_sema);
      free(resize_temp);
      return 0;
    }
    // update the inode disk
    cache_write_block (inode->sector, resize_temp);
  }
  sema_up (&inode->alloc_sema);
  free (resize_temp);

  while (size > 0)
    {
      /* Sector to write, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually write into this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
        {
          /* Write full sector directly to disk. */
          cache_write_block (sector_idx, (void *) buffer + bytes_written);
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
            cache_read_block (sector_idx, bounce);
          else
            memset (bounce, 0, BLOCK_SECTOR_SIZE);
          memcpy (bounce + sector_ofs, buffer + bytes_written, chunk_size);
          cache_write_block (sector_idx, bounce);
        }

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_written += chunk_size;
    }
  free (bounce);

  return bytes_written;
}

/* Helper function for resizing an inode_disk to a certain length */
bool
inode_resize_file(struct inode_disk *inode_disk, off_t length)
{
  size_t curr_blocks; // num of current blocks
  size_t new_blocks; // num of needed blocks
  off_t curr;
  off_t d_curr;

  curr_blocks = (inode_disk->length + BLOCK_SECTOR_SIZE - 1) / BLOCK_SECTOR_SIZE;
  new_blocks = (length + BLOCK_SECTOR_SIZE - 1) / BLOCK_SECTOR_SIZE;

  if (new_blocks > curr_blocks)
  {
    // allocate blocks from [curr_blocks+1, new_blocks];
    for (curr = curr_blocks; curr < new_blocks; curr++)
    {
      if (!inode_change_block (inode_disk, curr, true))
      {
        // must deallocate if failed to allocate
        for (d_curr = curr_blocks; d_curr < curr; d_curr++)
        {
          inode_change_block (inode_disk, d_curr, false);
        }
        return false;
      }
    }
    inode_disk->length = length;
    return true;
  }
  else if (curr_blocks > new_blocks)
  {
    // deallocate blocks from [new_blocks+1, curr_blocks];
    for (curr = new_blocks; curr < curr_blocks; curr++)
    {
      inode_change_block (inode_disk, curr, false);
    }
    inode_disk->length = length;
    return true;
  }
  else
  {
    inode_disk->length = length;
    return true;
  }
}

/* Helper function for allocating / deallocating a
 * sector at a certain pointer index */
bool
inode_change_block (struct inode_disk *inode_disk,
    block_sector_t block, bool add)
{
  int offsets[2];
  int offset_cnt;
  uint8_t zeros[BLOCK_SECTOR_SIZE];

  memset (zeros, 0, sizeof (zeros));
  calculate_indices (block, offsets, &offset_cnt);

  // add/remove a page for direct pointer
  if (offset_cnt == 1)
  {
    if (add)
    {
      block_sector_t next_direct;
      if (!free_map_allocate(1, &next_direct)) {
        return false;
      }
      inode_disk->pointers[offsets[0]] = next_direct;
      cache_write_block (next_direct, zeros);
      return true;
    }
    // remove the block
    else
    {
      free_map_release (inode_disk->pointers[offsets[0]], 1);
      inode_disk->pointers[offsets[0]] = 0;
    }
  }

  // add/remove a page for indirect pointer
  if (offset_cnt == 2)
  {
    block_sector_t indirect_sector = inode_disk->pointers[122];
    struct indirect_inode_disk indirect_inode_disk;

    if (add)
    {
      // do we have indirect node
      if (indirect_sector == 0)
      {
        // we need an indirect pointer block first
        if (!free_map_allocate (1, &indirect_sector)) {
          return false;
        }
        inode_disk->pointers[122] = indirect_sector;
        cache_write_block (indirect_sector, zeros);
      }
      cache_read_block (indirect_sector, &indirect_inode_disk);

      // allocate a new block
      block_sector_t next_indirect;
      if (!free_map_allocate (1, &next_indirect)) {
        return false;
      }
      // save the pointer to the new block
      indirect_inode_disk.pointers[offsets[0]] = next_indirect;
      cache_write_block (next_indirect, zeros);
      cache_write_block (indirect_sector, &indirect_inode_disk);
      return true;
    }
    // remove the block
    else
    {
      cache_read_block (indirect_sector, &indirect_inode_disk);
      free_map_release (indirect_inode_disk.pointers[offsets[0]], 1);
      if (offsets[0] == 0)
      {
        // release the indirect pointer block as well
        free_map_release (indirect_sector, 1);
        inode_disk->pointers[122] = 0;
      }
      else
      {
        indirect_inode_disk.pointers[offsets[0]] = 0;
        cache_write_block (indirect_sector, &indirect_inode_disk);
      }
      return true;
    }
  }

  // add / remove page for doubly indirect pointer
  if (offset_cnt == 3)
  {
    // structs to contain indirect / doubly indirect pointer blocks
    block_sector_t doubly_indirect_sector = inode_disk->pointers[123];
    struct doubly_indirect_inode_disk doubly_indirect_inode_disk;
    block_sector_t indirect_sector;
    struct indirect_inode_disk indirect_inode_disk;

    if (add)
    {
      // do we have doubly-indirect node
      if (doubly_indirect_sector == 0)
      {
        // if not then we need one
        if (!free_map_allocate (1, &doubly_indirect_sector)) {
          return false;
        }
        inode_disk->pointers[123] = doubly_indirect_sector;
        cache_write_block (doubly_indirect_sector, zeros);
      }
      cache_read_block (doubly_indirect_sector, &doubly_indirect_inode_disk);

      indirect_sector = doubly_indirect_inode_disk.pointers[offsets[0]];
      // do we have an indirect node
      if (indirect_sector == 0)
      {
        // if not we need one
        if (!free_map_allocate (1, &indirect_sector)) {
          return false;
        }
        doubly_indirect_inode_disk.pointers[offsets[0]] = indirect_sector;
        cache_write_block (indirect_sector, zeros);
        cache_write_block (doubly_indirect_sector, &doubly_indirect_inode_disk);
      }
      cache_read_block (indirect_sector, &indirect_inode_disk);

      block_sector_t next_doubly_indirect;
      if (!free_map_allocate (1, &next_doubly_indirect)) {
        return false;
      }
      // save pointers to the new blocks
      indirect_inode_disk.pointers[offsets[1]] = next_doubly_indirect;
      cache_write_block (next_doubly_indirect, zeros);
      cache_write_block (indirect_sector, &indirect_inode_disk);
      return true;
    }
    else
    {
      cache_read_block (doubly_indirect_sector, &doubly_indirect_inode_disk);
      indirect_sector = doubly_indirect_inode_disk.pointers[offsets[0]];
      cache_read_block (indirect_sector, &indirect_inode_disk);
      free_map_release (indirect_inode_disk.pointers[offsets[1]], 1);
      if (offsets[1] == 0)
      {
        // we can remove the indirect pointer block
        free_map_release (indirect_sector, 1);
        if (offsets[0] == 0)
        {
          // we can remove the doubly indirect pointer block
          free_map_release (doubly_indirect_sector, 1);
          inode_disk->pointers[123] = 0;
        }
        else
        {
          // otherwise just update the doubly indirect pointer block
          doubly_indirect_inode_disk.pointers[offsets[0]] = 0;
          cache_write_block (doubly_indirect_sector,
              &doubly_indirect_inode_disk);
        }
      }
      else
      {
        indirect_inode_disk.pointers[offsets[1]] = 0;
        cache_write_block (indirect_sector, &indirect_inode_disk);
      }
      return true;
    }
  }

  return false;
}

/* Helper function that calculates indices in the pointer tree
 * for the block'th sector in our inode_disk struct */
bool
calculate_indices (int block, int *offsets, int *offset_cnt)
{
  if (block < 122)
  {
    // direct pointer
    offsets[0] = block;
    *offset_cnt = 1;
    return true;
  }
  block -= 122;
  if (block < 128)
  {
    // indirect pointer
    offsets[0] = block;
    *offset_cnt = 2;
    return true;
  }
  block -= 128;
  if (block < 128*128)
  {
    // doubly indirect pointer
    offsets[0] = block / 128; // index of indirect pointer
    offsets[1] = block % 128; // index of direct pointer
    *offset_cnt = 3;
    return true;
  }
  return false;
}

/* Disables writes to INODE.
   May be called at most once per inode opener. */
void
inode_deny_write (struct inode *inode)
{
  inode->deny_write_cnt++;
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
}

/* Re-enables writes to INODE.
   Must be called once by each inode opener who has called
   inode_deny_write() on the inode, before closing the inode. */
void
inode_allow_write (struct inode *inode)
{
  ASSERT (inode->deny_write_cnt > 0);
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
  inode->deny_write_cnt--;
}

/* Returns the length, in bytes, of INODE's data. */
off_t
inode_length (const struct inode *inode)
{
  struct inode_disk i_disk;
  cache_read_block (inode->sector, &i_disk);
  cache_write_block (inode->sector, &i_disk);
  return i_disk.length;
}
