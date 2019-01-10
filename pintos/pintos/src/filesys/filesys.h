#ifndef FILESYS_FILESYS_H
#define FILESYS_FILESYS_H

#include <stdbool.h>
#include "filesys/off_t.h"
#include "filesys/directory.h"

/* Sectors of system file inodes. */
#define FREE_MAP_SECTOR 0       /* Free map file inode sector. */
#define ROOT_DIR_SECTOR 1       /* Root directory file inode sector. */

static bool init_filesys = false;

/* Block device that contains the file system. */
struct block *fs_device;

void filesys_init (bool format);
void filesys_done (void);
bool filesys_create (const char *name, off_t initial_size);
struct inode *filesys_open_inode (const char *name);
struct file *filesys_open (const char *name);
bool filesys_remove (const char *name);
int get_next_part (char part[NAME_MAX + 1], const char **srcp);
bool path_finder (const char *path, struct dir **desired_dir, char filename[NAME_MAX +1]);

#endif /* filesys/filesys.h */
