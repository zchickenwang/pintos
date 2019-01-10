/* Test your buffer cache’s ability to coalesce writes
 * to the same sector. Each block device keeps a read_cnt
 * counter and a write_cnt counter. Write a large file
 * byte-by-byte (make the total file size at least 64KB,
 * which is twice the maximum allowed buffer cache size).
 * Then, read it in byte-by-byte. The total number of
 * device writes should be on the order of 128
 * (because 64KB is 128 blocks). */

#include <string.h>
#include <syscall.h>
#include "tests/lib.h"
#include "tests/main.h"

void
test_main (void)
{
  unsigned long long write_cnt;
  char filler[1];

  msg ("create (\"y...\"): %d", create ("y.txt", 0));

  int fd = open ("y.txt");
  int i = 0;

  memset (filler, 0, sizeof (filler));

  msg ("writing 64 KB...");

  while (i < 64000)
  {
    write (fd, filler, sizeof (filler));
    i++;
  }

  close (fd);

  msg ("now reading 64 KB...");

  fd = open ("y.txt");

  i = 0;
  while (i < 64000)
  {
    read (fd, filler, sizeof (filler));
    i++;
  }

  close (fd);

  write_cnt = wrtcnt ();

  if (write_cnt >= 100 && write_cnt < 1000)
  {
    msg ("Number of device writes is on the order of 128");
  }
  else
  {
    fail ("Number of device writes must be on the order of 128 but is instead %d", (int) write_cnt);
  }
}