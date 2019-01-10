/* Child process run by global-fd.
 * Tries to read from a file descriptor opened
 * by another process. This should fail and return
 * -1 bytes read. */

#include <stdio.h>
#include "tests/lib.h"

const char *test_name = "child-fd2";

// The other process opens a file in fd number 3.
const int FD = 3;

int
main (void)
{
  char buffer[16];

  msg ("run");
  int num_bytes_read = read (FD, buffer, sizeof(buffer));
  msg ("%d", num_bytes_read);  // Should be -1.
  return 13;
}

