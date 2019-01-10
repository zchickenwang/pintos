#include <debug.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <list.h>
#include <round.h>

#include "filesys/cache.h"
#include "filesys/inode.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "filesys/free-map.h"
#include "devices/timer.h"
#include "threads/malloc.h"
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"

#include "userprog/pagedir.h"
#include "userprog/process.h"
#include "devices/shutdown.h"


static struct cache_entry cache[64]; /* static cache array */
struct semaphore global_cache_sema; /* guard semaphore for the cache */

/* Finds index if sector is in cache, if not return -1. */
int
find_entry(block_sector_t sector) 
{
	int i = 0;
	int retVal = -1;
	while (i < 64)
	{
		if (cache[i].sector == sector) 
		{
			retVal = i;
			break;
		}
		i++;
	}
	return retVal;
}

/* Iterates through cache and finds index of LRU,
 * return that entry's index. */
int
find_entry_to_replace(void)
{
	int LRU = timer_ticks ();
	int index = -1; 

	int i = 0;
	while (i < 64)
	{
		if (cache[i].last_use_time < LRU) {
			LRU = cache[i].last_use_time;
			index = i;
		}

		i++;
	}
	return index;
}


void
cache_init (void)
{
	sema_init(&global_cache_sema, 1); // Initialize the global semaphore

	cache_calls = 0;
	cache_miss = 0;

	/* Initialize all cache entries */
	int i = 0;
	while (i < 64)
	{
		sema_init(&cache[i].cache_entry_sema, 1);
		cache[i].sector = 8388609; // 2^23+1
		cache[i].data = (char *) malloc(BLOCK_SECTOR_SIZE);
		cache[i].last_use_time = 0; 
		cache[i].dirty_bit = 0;
		i++;
	}
}

/* Reads from a sector--brings into the cache if not already present. */
bool
cache_read_block (block_sector_t sector, void *buffer_)
{
	uint8_t *buffer = buffer_;

	cache_calls++;

	sema_down(&global_cache_sema);

	/* find block index in the cache */
	int entry_index = find_entry(sector);

	/* if sector is not in cache,
	need to bring it to the cache */ 
	if(entry_index == -1)
	{
		cache_miss++;
		entry_index = cache_add_block(sector);
	}
	sema_up(&global_cache_sema);

	/*  try to acquire that block */
	sema_down(&cache[entry_index].cache_entry_sema);

	memcpy(buffer,cache[entry_index].data,BLOCK_SECTOR_SIZE); // copy data 
	cache[entry_index].last_use_time = timer_ticks (); // update recently used
	sema_up(&cache[entry_index].cache_entry_sema);

	return true;
}

/* Writes to a sector--brings into the cache if not already present. */
bool
cache_write_block (block_sector_t sector, void *buffer_)
{
	sema_down(&global_cache_sema);

	/* find block index in the cache */
	int entry_index = find_entry(sector);

	cache_calls++;

	/* if sector is not in cache,
	need to bring it to the cache */ 
	if (entry_index == -1)
	{
		cache_miss++;
		entry_index = cache_add_block(sector);
	}
	sema_up(&global_cache_sema);

	/*  try to acquire that block */
	sema_down(&cache[entry_index].cache_entry_sema);
	memcpy(cache[entry_index].data, buffer_, BLOCK_SECTOR_SIZE); // write to data 
	cache[entry_index].last_use_time = timer_ticks (); // update recently used
	cache[entry_index].dirty_bit = 1; // set dirty bit
	sema_up(&cache[entry_index].cache_entry_sema);

	return true;
}

/* Helper function which brings a sector into cache */
int
cache_add_block (block_sector_t sector)
{
	/* keep in mind that we have the global lock during this method call */ 

	// find the LRU entry to replace 
	int index = find_entry_to_replace ();

	// make sure nobody else already brought this sector
	int entry_exists = find_entry(sector);
	if( entry_exists != -1)
	{
		return entry_exists;
	}

	sema_down(&cache[index].cache_entry_sema);

	// if entry is dirty write-back
	if(cache[index].dirty_bit == 1)
	{
		// write the existing sector! not new !!
		block_write (fs_device, cache[index].sector, cache[index].data);
	}
	cache[index].sector = sector;

	block_read (fs_device, sector, cache[index].data);

	cache[index].last_use_time = timer_ticks ();
	cache[index].dirty_bit = 0;

	sema_up(&cache[index].cache_entry_sema);

	return index;
}

/* Free cache's allocated memory and delete the data */
void
cache_done (void)
{
	int i = 0;
	while (i < 64)
	{
		if (cache[i].dirty_bit == 1) 
		{
			block_write (fs_device, cache[i].sector, cache[i].data);	
		}
		free(cache[i].data);
		cache[i].last_use_time = timer_ticks ();
		cache[i].dirty_bit = 0;

		i++;
	}
	
	cache_miss = 0;
	cache_calls = 0;
}