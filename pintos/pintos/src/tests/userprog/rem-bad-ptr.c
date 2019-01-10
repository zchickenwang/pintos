/* Passes an invalid pointer to the remove system call.
   The process must be terminated with -1 exit code. */

#include <syscall.h>
#include "tests/lib.h"
#include "tests/main.h"

void
test_main (void)
{
  msg ("remove(0x20101234): %d", remove ((char *) 0x20101234));
  fail ("should have called exit(-1)");
}
