Final Report for Project 2: User Programs
=========================================

## Group Members
* Z Wang <zzwang@berkeley.edu>
* Yoon Kim <yoonkim@berkeley.edu>
* Bryce Schmidtchen <bschmidtchen@berkeley.edu>
* Ben Ben-Zour <benbz90@berkeley.edu>

## Changes we made since our initial design document and why we made them:

### Task 1: Argument Passing

As stated by our TA, our initial design for this task was "solid". Thus, we did not make any design changes for this task.

### Task 2: Process Control Syscalls

* Removed global list of `shared_blocks`
  * This wasn't necessary as we have access to both parent and child threads in `thread_create`. Thanks Eric for helping us find this alternative.
* Refrained from changing the signature of thread_create
  * This was a poor design decision, especially since the function already takes in an auxiliary argument for miscellaneous needs.
* Passing a pointer to a `shared_block` struct as the auxiliary argument of `thread_create` au lieu of appending it to a palloc'd page
  * The original idea was to append our pointer to the `fn_copy` string. We had some trouble with this, since the string starts out with the command line function name and arguments. It was difficult to know where to start reading off `fn_copy` when looking for our pointer.
  * As a result, we just took out `fn_copy` in process_execute, and passed in just the pointer to our "shared_block" as the aux argument.
  * Then, in `thread_create`, we palloc a page and add the function name and its arguments.
* Create `shared_block` struct in `thread_create` only for main thread. Otherwise, malloc’d in process execute.
  * The first thread needs to support waiting, since the entire system is just waiting on that thread before dying. How do we do this?
  * The design doc decision was instantiating a `shared_block` when that thread is created (since it doesn't go through `process_execute`). But, we found out this doesn't work, since the filesystem hasn't yet been initialized at that point!
  * So instead, we allotted to make a special case in thread_create for the first thread. Unlike normal threads whose `shared_blocks` are created in `process_execute`, we initialize the struct for the idle thread here in `thread_create`.
* Different user memory verification functions
  * Initially we thought one function would do as a validity check for all syscalls. As it turns out, we needed different ones for checking simply address validity, as well as the validity of filenames (strings) and buffers (arrays).

### Task 3: File Operations Syscalls

As stated by our TA, our initial design for this task was "solid", minus a minor change.
* When obtaining the lowest available file descriptor number for `int open (const char *file)`, our initial design planned on implementing an algorithm that would sort and iterate through our `file_data_list` to find the lowest `fd` that was not being used. Although correct, our TA suggested an alternative.
  * Instead, we simply abandoned this algorithm and kept track of a global `available_fd` variable which was initialized to 3. Then for every `open` syscall, we would simply increment this global variable.
  * Our TA suggested that this logic would suffice for the purposes of this project.

## Reflection
A reflection on the project – what exactly did each member do? What went well, and what could be improved?

### Ben
* Helped writing Task I and Task II and additional part.
* Worked on Task I - argument passing.
* Helped on the file system syscalls which Yoon was working on. 
* Debugging.

### Bryce
* Assisted with debugging of Task I.
* Wrote initial implementations for wait(), exec(), and other more simple syscalls.
* Wrote function for verification of user memory access.
* Cleaned up code and maintained consistent code style. 

### Yoon
* Implemented Task I with Z initially, then debugged and changed the original implementation afterwards.
  * Had difficulty in debugging, so I ran an exercise with Bryce where I explained each line of the code and he would point out any uncertainties.
  * Exercise was helpful in understanding the task from a high-level perspective, but the core issues were only found after many hours of debugging with `hex_dump`.
* Implemented Task III minus user memory verification and bad filename/buffer checks, which Z integrated.
  * While debugging, I had Ben help look at a few of the test cases that were failing. Having an extra pair of eyes helped me catch mistakes.
* Helped Z debug Task II.
* Wrote out the design spec for Task III, with help from Z.
* Discussed Task I, II, and III of the design with Z, bouncing ideas and shortcomings between each other.

### Z
* Wrote out the design spec for Task I and II, helped out with Task III.
* Wrote initial (bad) implementation of Task I, which Ben and Yoon fixed.
* Implemented Task II.
* Vast majority of my time spent debugging Task II.
* Changed a lot of the original design with Yoon.
* Integrated user memory verification into Task II and III syscalls.
* Wrote additional tests.

### What went well:
* After project 1, we had a better understanding of how to approach the design of this project, and we spent a lot of time perfecting it and considering all the relevant parts into details.
  * Most of the implementation followed the design spec verbatim, and we had a really comfortable grasp of the edge cases.
  * When we did change a crucial element of the design in Part II, we understood all the implications of those changes.
* Implementation was straight forward and debugging was easier since we had a thorough understanding and a detailed doc to back us up.
* Familiarity with gdb from Project 1 made finding specific causes of page faults much easier. In particular, hex dump was a blessing for debugging part 1.
* As always, good communication. Most of the work was done in person, so we didn't have to constantly be explaining ourselves online.
* Leveraged git well. We had clearly-defined branches and minimized the amount of merging we had to do between them.

### What could be improved:
* Should've started earlier. Had a weekend before without too much but we were a little fatigued from the weeks before. This resulted in grinding last minute and taking a slip day because of other events that came up the week the project was due.
  * This also meant abandoning multi-oom.
  * The week following the design doc submission, the intricacies of the project were still fresh in our mind. Not working much that week led to overhead in refreshing ourselves with the problems.
* Reformulating the design doc after our review session with Eric. Starting implementation, we encountered a few setbacks due to not being on the same page with what the final design should be. We had uncertainties regarding argument passing through thread_create and this led to a sub-optimal initial design decision.
* More organized approach to implementing the tasks. Because there are so many different programs and methods that could be changed, having different members start and pick up on tasks led to a lot of overhead in trying to understand what had been changed and why--to where it was easier simply to start over rather than build upon previous work.

## Student Testing Report

### Test 1

#### Provide a description of the feature your test case is supposed to test.
Verifies that calling the "remove" syscall with an invalid pointer as an argument is caught.

#### Provide an overview of how the mechanics of your test case work, as well as a qualitative description of the expected output.
Calls the "remove" syscall, passing in a pointer to an invalid memory address. The test fails on the next line, signaling the case where the process continues running. In a correct implementation, the process would be killed with exit code -1.

#### Provide the output of your own Pintos kernel when you run the test case.
* `userprog/build/tests/userprog/rem-bad-ptr.output`:
  ```c
  Copying tests/userprog/rem-bad-ptr to scratch partition...
  qemu -hda /tmp/uhkG5JWsv1.dsk -m 4 -net none -nographic -monitor null
  PiLo hda1
  Loading..........
  Kernel command line: -q -f extract run rem-bad-ptr
  Pintos booting with 4,088 kB RAM...
  382 pages available in kernel pool.
  382 pages available in user pool.
  Calibrating timer...  419,430,400 loops/s.
  hda: 5,040 sectors (2 MB), model "QM00001", serial "QEMU HARDDISK"
  hda1: 175 sectors (87 kB), Pintos OS kernel (20)
  hda2: 4,096 sectors (2 MB), Pintos file system (21)
  hda3: 101 sectors (50 kB), Pintos scratch (22)
  filesys: using hda2
  scratch: using hda3
  Formatting file system...done.
  Boot complete.
  Extracting ustar archive from scratch device into file system...
  Putting 'rem-bad-ptr' into the file system...
  Erasing ustar archive...
  Executing 'rem-bad-ptr':
  (rem-bad-ptr) begin
  rem-bad-ptr: exit(-1)
  Execution of 'rem-bad-ptr' complete.
  Timer: 67 ticks
  Thread: 0 idle ticks, 67 kernel ticks, 0 user ticks
  hda2 (filesys): 62 reads, 206 writes
  hda3 (scratch): 100 reads, 2 writes
  Console: 882 characters output
  Keyboard: 0 keys pressed
  Exception: 0 page faults
  Powering off...
  ```
* `userprog/build/tests/userprog/rem-bad-ptr.result`:
  ```c
  PASS
  ```

### Identify two non-trivial potential kernel bugs, and explain how they would have affected your output of this test case.
* *Case A*: **If our kernel** directly tried to read the argument passed in to the remove syscall **instead of** verifying its validity, **then** we'd get a page fault from trying to access an invalid address, and the test wouldn't have been able to finish properly.
* *Case B*: **If our kernel** did verify that the argument is invalid but let the thread continue running **instead of** killing it, **then** our tests would have failed at the "fail" clause on the next line, which isn't supposed to run!

### Test 2

#### Provide a description of the feature your test case is supposed to test.
Makes sure that processes don't share file descriptors.

#### Provide an overview of how the mechanics of your test case work, as well as a qualitative description of the expected output.
A parent process creates two children, and waits on both. The first child opens a file descriptor for an existing file. The second child then tries to read from the first child's open file descriptor, which should return -1 (since processes don't all use a global fd list). In an incorrect implementation, the second child would start reading from that open file.

#### Provide the output of your own Pintos kernel when you run the test case.
* `userprog/build/tests/userprog/global-fd.output`:
  ```c
  Copying tests/userprog/global-fd to scratch partition...
  Copying ../../tests/userprog/corgi.txt to scratch partition...
  Copying tests/userprog/child-fd1 to scratch partition...
  Copying tests/userprog/child-fd2 to scratch partition...
  qemu -hda /tmp/JpD1SyqAFb.dsk -m 4 -net none -nographic -monitor null
  PiLo hda1
  Loading..........
  Kernel command line: -q -f extract run global-fd
  Pintos booting with 4,088 kB RAM...
  382 pages available in kernel pool.
  382 pages available in user pool.
  Calibrating timer...  360,038,400 loops/s.
  hda: 5,040 sectors (2 MB), model "QM00001", serial "QEMU HARDDISK"
  hda1: 175 sectors (87 kB), Pintos OS kernel (20)
  hda2: 4,096 sectors (2 MB), Pintos file system (21)
  hda3: 299 sectors (149 kB), Pintos scratch (22)
  filesys: using hda2
  scratch: using hda3
  Formatting file system...done.
  Boot complete.
  Extracting ustar archive from scratch device into file system...
  Putting 'global-fd' into the file system...
  Putting 'corgi.txt' into the file system...
  Putting 'child-fd1' into the file system...
  Putting 'child-fd2' into the file system...
  Erasing ustar archive...
  Executing 'global-fd':
  (global-fd) begin
  (child-fd1) run
  child-fd1: exit(13)
  (child-fd2) run
  (child-fd2) -1
  child-fd2: exit(13)
  (global-fd) end
  global-fd: exit(0)
  Execution of 'global-fd' complete.
  Timer: 62 ticks
  Thread: 0 idle ticks, 60 kernel ticks, 2 user ticks
  hda2 (filesys): 221 reads, 608 writes
  hda3 (scratch): 298 reads, 2 writes
  Console: 1106 characters output
  Keyboard: 0 keys pressed
  Exception: 0 page faults
  Powering off...
  ```
* `userprog/build/tests/userprog/global-fd.result`:
  ```c
  PASS
  ```

#### Identify two non-trivial potential kernel bugs, and explain how they would have affected your output of this test case.
* *Case A*: **If our kernel** had a global fd list shared by all processes **instead of** giving each process its own separate fd list, **then** the second process would be able to read from any open file descriptor. This would fail the test because the return value of the read would not be -1, which is expected.
* *Case B*: **If our kernel** killed a process that tried to read from an invalid file descriptor **instead of** just returning -1, **then** the test would not have logged -1 and finished as expected. (From our design doc and review session, we understand that the expected behavior is to return -1).

#### Tell us about your experience writing tests for Pintos. What can be improved about the Pintos testing system? (There’s a lot of room for improvement.) What did you learn from writing test cases?
* It is hard to write tests that verify a specific problem. Needs a lot of thought on how to actually implement it, as oppose to detecting the problem. E.g. making sure that the system is synced. 
* We couldn't find a way to write the tests we really wanted to make. E.g. what we wrote in our design doc--testing that a process can't wait on its grandchild. But how can you pass the pid of a grandchild back up to the grandparent? Spent a lot of time on this but couldn't find a way.
* It took us some time to learn how the test suit actually works, which in it in of itself was an obstacle to overcome. Perl and ck files were particularly annoying. Are there no other simpler unit-testing frameworks for C? Maybe Pintos needs its own.. 
