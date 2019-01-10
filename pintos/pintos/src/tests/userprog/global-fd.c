/* Executes and waits for two children.
 * The first opens a file. The second tries
 * to read from this file descriptor (just opened
 * by the first process). This should give the
 * second process -1 bytes read, which is logged
 * and checked (see global-fd.ck). */

#include <syscall.h>
#include "tests/lib.h"
#include "tests/main.h"

void
test_main (void)
{
  wait (exec ("child-fd1"));
  wait (exec ("child-fd2"));
}
