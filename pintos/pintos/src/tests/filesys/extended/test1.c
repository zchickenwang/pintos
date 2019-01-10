/* Test your buffer cache’s effectiveness by measuring its cache hit rate.
 First, reset the buffer cache. Open a file and read it sequentially, 
 to determine the cache hit rate for a cold cache. Then, close it, 
 re-open it, and read it sequentially again, 
 to make sure that the cache hit rate improves. */

#include <string.h>
#include <syscall.h>
#include "tests/lib.h"
#include "tests/main.h"

#define BLOCK_SECTOR_SIZE 512
#define MISS_RATE 0
#define TOTAL_CALLS 1

void
test_main (void)
{
  int old_miss_rate = 0;
  int total_miss_rate = 0;
  int old_cache_calls = 0;
  int total_cache_calls = 0;
  int zeros[BLOCK_SECTOR_SIZE];

  msg ("create(\"x...\"): %d",create ("x.txt", 0));

  int fd = open ("x.txt");
  int i = 0;
  
  memset (zeros, 0, sizeof (zeros));

  msg ("writing 60 blocks...");

  while (i < 60)
  {
    write (fd, zeros, BLOCK_SECTOR_SIZE);
    i++;
  }

  close (fd);

  old_miss_rate = cache_stats(MISS_RATE);
  old_cache_calls = cache_stats(TOTAL_CALLS);

  msg ("now reading 60 blocks...");

  fd = open ("x.txt");

  i = 0;
  while (i < 60)
  {
    read (fd, zeros, BLOCK_SECTOR_SIZE);
    i++;
  }

  close (fd);

  total_miss_rate = cache_stats(MISS_RATE);
  total_cache_calls = cache_stats(TOTAL_CALLS);

  if ((total_miss_rate * 100 / total_cache_calls)
      <= (old_miss_rate * 100 / old_cache_calls))
  {
    msg("Miss rate ratio is smaller or same than cold cache");
  } 
  else 
  {
    fail("Miss rate didnt improve");
  }

}
