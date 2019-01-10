/* Child process run by global-fd.
 * This child opens a file. */

#include <stdio.h>
#include "tests/lib.h"

const char *test_name = "child-fd1";

int
main (void)
{
  msg ("run");
  open ("corgi.txt");
  return 13;
}
