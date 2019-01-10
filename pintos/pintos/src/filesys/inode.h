#ifndef FILESYS_INODE_H
#define FILESYS_INODE_H

#include <stdbool.h>
#include "filesys/off_t.h"
#include "devices/block.h"
#include "threads/synch.h"
#include <list.h>

struct bitmap;

/* On-disk inode.
   Must be exactly BLOCK_SECTOR_SIZE bytes long. */
struct inode_disk
  {
    off_t length;                       /* File size in bytes. */
    uint32_t next_sector_index;         /* index of next pointer to allocate */
    block_sector_t pointers[124];       /* 122 direct pointers,
                                         * 1 indirect, 1 doubly-indirect */
    uint32_t is_dir;                    /* is this inode_disk a directory */
    unsigned magic;                     /* Magic number. */
  };

/* In-memory inode. */
struct inode
  {
    struct list_elem elem;              /* Element in inode list. */
    block_sector_t sector;              /* Sector number of disk location. */
    int open_cnt;                       /* Number of openers. */
    bool removed;                       /* True if deleted, false otherwise. */
    int deny_write_cnt;                 /* 0: writes ok, >0: deny writes. */
    struct semaphore alloc_sema;        /* lock for file extending */
    bool is_dir;                        /* Copied in from inode_disk (1=T, 0=F). */
    struct inode_disk data;             /* Inode content. */
  };

/* On-disk indirect inode. */
struct indirect_inode_disk
  {
    block_sector_t pointers[128];       /* 128 direct pointers */
  };

/* On-disk doubly-indirect inode. */
struct doubly_indirect_inode_disk
  {
    block_sector_t pointers[128];       /* 128 indirect pointers */
  };

void inode_init (void);
bool inode_create (block_sector_t, off_t, bool is_directory);
struct inode *inode_open (block_sector_t);
struct inode *inode_reopen (struct inode *);
block_sector_t inode_get_inumber (const struct inode *);
bool inode_isdir(const struct inode *);
int inode_sub_dir_num (const struct inode *inode);
void inode_increment_sub_dir_count(struct inode *inode);
void inode_decrement_sub_dir_count(struct inode *inode);
void inode_increment_open_count(struct inode *);
void inode_close (struct inode *);
void inode_remove (struct inode *);
bool inode_removed (struct inode *inode);
off_t inode_read_at (struct inode *, void *, off_t size, off_t offset);
off_t inode_write_at (struct inode *, const void *, off_t size, off_t offset);
void inode_deny_write (struct inode *);
void inode_allow_write (struct inode *);
off_t inode_length (const struct inode *);
bool inode_resize_file (struct inode_disk *, off_t length);
int get_cache_stats (int stats);

#endif /* filesys/inode.h */
