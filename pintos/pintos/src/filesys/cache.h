#ifndef FILESYS_CACHE_H
#define FILESYS_CACHE_H

#include <stdbool.h>
#include <stddef.h>
#include "threads/synch.h"
#include "filesys/off_t.h"
#include "devices/block.h"

/* Used for additional test 1 */
int cache_miss;
int cache_calls;

/* Each entry in the cache */
struct cache_entry 
{
	block_sector_t sector;  // sector of cache entry
	char *data;							// data in sector
	struct semaphore cache_entry_sema;  // semaphore for r/w operations
	int64_t last_use_time;  // used for LRU search
	bool dirty_bit;				  // used for write-back
};


void cache_init (void);
bool cache_read_block (block_sector_t sector, void *buffer_);
bool cache_write_block (block_sector_t sector, void *buffer_);
int cache_add_block (block_sector_t sector);
void cache_done (void);

/* helpers */

int find_entry(block_sector_t sector);
int find_entry_to_replace(void);

#endif /* filesys/cache.h */