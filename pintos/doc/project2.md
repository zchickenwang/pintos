Design Document for Project 2: User Programs
============================================

## Group Members

* Z Wang <zzwang@berkeley.edu>
* Ben Ben-Zour <benbz90@berkeley.edu>
* Bryce Schmidtchen <bschmidtchen@berkeley.edu>
* Yoon Kim <yoonkim@berkeley.edu>

## 1.1 Argument Passing

### Data Structures and Functions
* `tid_t process_execute(const char *file_name)`
  * We'll add a sanity check to make sure `file_name` isn't null.
* `static void start_process(void *file_name_)`
  * Here, we add functionality for parsing the `file_name` and placing arguments onto the new thread stack.

### Algorithms

* The main changes occur in `start_process`:
  * The argument we receive from `process_execute` is a string containing the executable file name and its arguments.
  * Here we split the string by spaces using `strtok_r`, passing the first argument into `load` (which tries to open the executable).
  * If `load` is successful, hooray! We have a pointer to the new program stack in `&if_.esp`.
    * We split the arguments by spaces and add each argument onto the stack, keeping track of pointers to those arguments and the total count.
    * Then we place padding to round the stack pointer down to a multiple of 4.
    * Then we add pointers to the arguments onto the stack in right-to-left order, starting with a null pointer ending.
    * Add argv, which is a pointer to the the arg[0] pointer.
    * Add argc, which is the number of arguments.
    * Add a fake return address (ra = 0, as suggested in the spec).
  * If ‘load’ is unsuccessful, the default code frees the page.

### Synchronization

* We don't see any synchronization issues here. Even if the thread starting the program gets preempted, another thread won't interfere with the preempted-thread's stack.

### Rationale

 * We're following the guidance laid out in the design spec. We discussed implementing the argument parsing and stack placement in different places:
   * `process_execute` wasn't chosen as finding the stack pointer is nontrivial. We wanted to populate the stack closer in the pipeline to where it's being created.
   * `load` or `setup_stack` wasn't chosen as we would need to pass down all the program arguments. This also disrupts the logical modularity of those functions.


## 1.2 Process Control Syscalls

### Data Structures and Functions

**thread.h:**
```c
// similar to wait_status struct defined in our discussion section
struct shared_block
{
	// used by the parent to search for a given tid in children
	tid_t tid;

	// used by the parent to wait on this child to load
	struct semaphore thread_loaded;

	// used by the parent to wait on this child to finish
	struct semaphore thread_finished;

	// used to ensure synchronization of reference count modifications
	struct semaphore ref_cnt_sema;

	// stores exit value if the parent waits after this child has died
	int exit_status;

	// the number of threads that still need this struct
	int reference_count;

	// necessary in order to make a list of process_node's
	struct list_elem elem;
};
```
```c
// global list of all shared_block structs
struct list all_shared_blocks;
```

```c
struct thread
{
	...
	// a pointer to this thread's shared_block.
 	struct shared_block *shared;

 	// pointers to the shared_block's of this thread's children
 	struct list children;
	...
};
```

**userprog/syscall.h**, **userprog/syscall.c:**
```c
/* Here we check the syscall no. and any additional arguments,
 then call the corresponding handler. */
static void syscall_handler (struct intr_frame *f UNUSED)

int practice(int i) // see algorithms

void halt(void) // see algorithms

void exit(int status) // see algorithms

pid_t exec(const char* cmd_line) // see algorithms

int wait(pid_t pid) // see algorithms
```

**threads/thread.c:**
```c
// Passes along the new pointer to init_thread.
tid_t thread_create (... struct shared_block* shared)


/* * Initializes the all_shared_blocks list.
   * Creates a shared_block for this thread so that `main` can wait on it.
   	- Initial values are the same as in process_execute below.
	- Place the block on all_shared_blocks. */
void thread_init (void)

/* * Sets this thread's shared_block field to the passed-in pointer,
     while setting the tid value inside to the current tid.
   * Initializes this thread's children list. */
static void init_thread(... struct shared_block* shared)

```


**userprog/process.c:**
```c
/* * Before calling `thread_create`, we `malloc` space for a `shared_block`
     and initialize the semaphores and int values inside of it:
	- default `exit_status` is -1
	- initial `reference_count` is 2
	- `thread_loaded` and `thread_finished` semaphores initialized to 0
	- `ref_cnt_sema` initialized to 1
   * We add the new `shared_block` to `all_shared_blocks`.
   * Then pass a pointer to this struct to `thread_create`
     while downing on the `thread_loaded` semaphore. */
tid_t process_execute (const char *file_name)

/* * Once the user program is loaded (successfully or unsuccessfully),
     we'll up the `thread_loaded` semaphore to let
     process_execute finish. */
static void start_process (void *file_name_)

void process_exit (void) // see exit in algorithms

int process_wait (tid_t child_tid UNUSED) // see wait in algorithms
```

### Algorithms

* General Pattern
  1. The user process calls syscall which invokes syscall_handler.
  2. The syscall handler checks the stack pointer is valid, and reads the syscall number. Depending on the number, it'll parse      handler-specific arguments using the following criteria:
     * checking that they aren't null pointers
     * using `is_user_vaddr` to make sure arguments point to user space
     * using `pagedir_get_page` to make sure the beginning and end of the arguments are in mapped memory pages
     * checking that they match the handler spec (e.g. `practice` should take an int).
     * if any of these fail, we exit with status -1.
  3. Then call that handler using the parsed args.
  4. Place return value in eax.

* `int practice (int i)`:
  * Increment the input arg and return that value.

* `void halt (void)`:
  * Calls `shutdown_power_off` and kills Pintos (RIP).

* `void exit (int status)`:
  * Check this thread's `shared` block (where it is the child). Obtain `ref_cnt_sema` -- if the count is still 2, then we update `exit_status`.
  * Call `thread_exit`, which in turn calls `process_exit`. In `process_exit`:
    * For the current thread's `shared_block`:
      * We first down on its `ref_cnt_sema` to make sure the other thread isn't changing the reference count.
      * Once obtained, decrement `reference_count`.
      * If the count is now 0, we free the block.
      * Otherwise, up the `ref_cnt_sema`. If we're still holding `thread_loaded` sema, we need to up it. This covers the case that the thread errors out while loading, where we must unblock the `exec` call. Also up the `thread_finished` semaphore.
    * For each of the current thread's `children` blocks:
      * We obtain `ref_cnt_sema`, decrement the count, free if it's now 0, and release the semaphore otherwise.
  * Finally, print out the given log statement for tests to pass.

* `pid_t exec (const char *cmd_line)`:
  * Pass `cmd_line` in a call to `process_execute`, which parses and starts the command.
  * By the time we get back the `tid_t` return value from `process_execute`, the thread will have finished loading
    * `process_execute` downs on the `thread_loaded` semaphore, which is only upped after `load` in `start_process` (see data structures and functions, synchronization).
  * Find the `shared_block` in `all_shared_blocks` with matching `tid` and add a pointer to that thread's `shared_block` in the current thread's `children` list.
  * Set eax to the returned `tid_t` value (either a valid tid or `TID_ERROR`).

* `int wait (pid_t pid)`:
  * Calls `process_wait`--all logic below belongs in `process_wait`.
  * Read through this thread's children--if the passed-in `pid` does not belong to a child, return -1 immediately.
  * Otherwise check the child's `shared_block`, downing on its `thread_finished` semaphore. (If the child has already finished, there'll be no blocking).
  * When the child thread exits and ups the semaphore, we'll read the exit value and place it in eax.
  * Free the space. Since the child is already dead, and the parent has waited once, we guarantee their `shared_block` is no longer needed.
  * Remove the child from the current thread's `children` list; this ensures that if `wait` is called again on the same `pid`, we won't find it in `children` and so will return -1 immediately.

### Synchronization

* `exec` must wait for the executable to be loaded before returning
We ensure this via the `thread_loaded` semaphore. `process_execute` downs on the semaphore. `start_process` ups it after finishing it's call to `load` (if `load` fails, then the thread will exit and up the semaphore anyway.) Thus when we return to `exec`, we'll already know if the program has been successfully loaded.
`wait`: normal case
Parent calls wait on a living child, downing the `thread_finished` semaphore. Child later exits (always going through `process_exit`), which ups the semaphore and wakes up the parent. The parent free's the `shared_struct`.
* `wait`: failure on exec (child initialization)
If the child fails to load, it will call `thread_exit()`. Since the `reference_count` at that point is 1 (the parent hasn't returned from `process_execute`), the child will decrement the count and free the space before exiting. The parent won't find this child's `shared_block`.
* `wait`: child exits before parent calls wait
The child will up the semaphore to 1, fill in `exit_status` if needed, and decrement `reference_count` to 1.
The parent calls `wait`--decrements the semaphore (returning immediately), reads the exit value, and frees the `shared_block`.
* `wait`: parent exits before child exits
The parent decrements `reference_count` to 1. When the child exits, it'll free the space.
* `wait`: child exits at the same time as parent calls wait
The key here is the order of instructions. The parent downs the semaphore before anything else, which ensures it can read `exit_status` only after the child has upped the semaphore, by which time the child has already written its exit value.
* `wait`: child exits at the same time as parent exits
Here, both threads can be decrementing their shared `reference_count` concurrently. We ensure synchronization via the `ref_cnt_sema`.
* `wait`: on multiple children
Our current logic is robust to sequential `wait` calls.
* all other cases should be covered in previous sections.

### Rationale

* Most of our debate revolved around `wait` and `exit`. Here are the iterations of our design and their shortcomings:
  * Place a semaphore and exit status in child thread; parent uses them on `wait`.
    * If the child thread exits before the parent calls wait, the child thread struct will be gone.
  * Place a list of children tid's, semaphores, and exit status' in the parent thread; the child finds its parent in `exit` and updates the value. Alternatively, just keeping a list of pointers to the children thread structs (and keeping the semaphore and exit status inside each child thread).
    * We debated having children->parent or parent->children pointers. Either way, we're adding linear to quadratic complexity to searching in `exit` or `wait` (depending on how many children each parent has).
    * Further, the list would be removed when the parent dies. We'd need to add logic in the child to make sure it doesn't dereference a null pointer.
    * If the default exit value is -1, how can we tell if a thread has actually exited? Having a pointer to an exited thread could be null or valid if the thread is in a 'dying' state. We'd need to check for both cases.
  * Use a global table of tid's, semaphores, and exit status'.
    * Global vars are worrisome.
    * If we were using a list, synchronization would need to be carefully monitored between concurrent reads and writes to the list. Having a global lock would slow things down and introduce more logic complexity.
  * Use `malloc` to create shared memory between parent and child.
    * We debating the necessity of having a reference count. Perhaps we can just deallocate the structs when the parent dies? However, the child then doesn't know whether its pointer to the `malloc`'d space is still valid!
    * Do we need a lock for `process_node`, or disable interrupts when the child calls `exit`? This would add complexity, so we found a way to avoid synchronization issues via ordering of operations.

## File Operation Syscalls

### Data Structures and Functions

**syscall.c:**
```c
/* lock for all file system syscalls
   (initialized to 1 for mutually exclusive file operations) */
struct semaphore file_system_sema;
```

**threads/thread.h:**
```c
struct thread
{
	...
	// a pointer to this thread's executable file
	struct file *executable;

	// a list containing file data's for the current thread
	struct list file_data_list;
	...
};
```

```c
struct file_data {
	// file descriptor number
	int fd;

	// file name
	char *file_name;

	// pointer to the file struct
	struct file *file_p;

	// needed for use as element in file_data_list
	struct list_elem elem;
};`
```

**threads/thread.c:**
* `tid_t thread_create (...)`
  * Here we initialize the `file_data_list`
* `userprog/process.c`:
  * `bool load (...)`
    * If we load the file successfully, we call `file_deny_write` using the returned `file *`. This changes the corresponding `inode`'s `deny_write_cnt`, so that no other processes that open this executable can write to it.
    * We take out the line in `load` that closes the file, and save the `file *` in the current thread's `executable` field.
    * `void process_exit (void)`
    * Before a process exits, we call `close` on all it's open file descriptors.
    * If it's holding the `file_system_sema`, we up it.
    * We also need to allow writes back to the running file. To do this, we'll call `file_allow_write` on the current file's `executable`, and then close the file.

**userprog/syscall.h**, **userprog/syscall.c:**
* `bool create (const char *file, unsigned initial size)`
* `bool remove (const char *file)`
* `int open (const char *file)`
* `int filesize (int fd)`
* `int read (int fd, void *buffer, unsigned size)`
* `int write (int fd, const void *buffer, unsigned size)`
* `void seek (int fd, unsigned position)`
* `unsigned tell (int fd)`
* `void close (int fd)`
* `void syscall_init (void)`
  * We initialize the `file_system_sema` to 1.

### Algorithms

* **General Pattern**
  * We perform sanity checks on the arguments using the same reasoning as in Part 2 syscalls: i.e. make sure pointers and their objects live on mapped pages in the user space. Then pass those arguments to the appropriate handler (determined by the syscall no).
  * Before performing the operation, we down on the `file_system_sema`. After finishing and before returning, we up the semaphore.
* `bool create (const char *file, unsigned initial size)`:
  * Call `filesys_create(...)` to create the file, and return its result.
* `bool remove (const char *file)`:
  * Call `filesys_remove(...)` to remove the file.
  * In the current thread's `file_data_list`, remove the element corresponding to the given filename (if it exists).
  * Return the result of `filesys_remove`.
* `int open (const char *file)`:
  * Call `filesys_open(...)` and obtain its result, which will be a pointer to the file element.
  * If its result is false, return -1.
  * Otherwise, perform the following:
    * `list_sort(...)` the `file_data_list` by each `file_data->fd`, and iterate through the list to find the lowest available `fd`.
      * If all the `fd`s are unassigned, then the lowest available `fd` will be 3.
      * See rationale on how we find the lowest available `fd`.
    * Then, add a new `file_data` element to the current thread’s `file_data_list`.
    * With the newly created `file_data` element, assign the following to the `file_data` element’s variables:
      * Previously found lowest available `fd`
      * Filename
      * Pointer to the file element from the result of `filesys_open(...)`
    * Finally, return the assigned `fd`.
* `int filesize (int fd)`:
  * Call `file_length` using the `file_p` pointer in the `file_data` element with a matching `fd` number.
  * If no match is found, terminate with exit code -1, i.e., call `thread_exit()`.
  * Otherwise, return the result of `file_length`.
* `int read (int fd, void *buffer, unsigned size)`:
  * If the passed in `fd` argument is 0, call `input_getc()` to read from the keyboard and return 1.
  * Otherwise, call `file_read` using the `buffer`, `size`, and `file_p` pointer in the `file_data` element with a matching `fd` number.
    * If no match, then return -1.
    * Otherwise, return the result of `file_read`.
* `int write (int fd, const void *buffer, unsigned size)`:
  * If the passed in `fd` argument is 1, then write `size` bytes of the buffer to console with `putbuf()`. If the buffer `size` is greater than a 500 bytes, we'll break it up.
  * Otherwise, call `file_write` using the `buffer`, `size`, and `file_p` pointer in the `file_data` element with a matching `fd` number.
    * If no match is found, then terminate with exit code -1, i.e., call `thread_exit()`.
    * Otherwise, return the result of `file_write`.
* `void seek (int fd, unsigned position)`:
  * Find the matching `file_data` element with a matching `fd` number from the current thread’s `file_data_list`.
  * If no match is found, then terminate with exit code -1, i.e., call `thread_exit()`.
  * Otherwise, call `file_seek` using `position` and the `file_p` pointer in the `file_data` element with a matching `fd` number.
  * Return the result of `file_seek`.
* `unsigned tell (int fd)`:
  * Find the matching `file_data` element with a matching `fd` number from the current thread’s `file_data_list`.
  * If no matching `fd` is found, then terminate with exit code -1, i.e., call `thread_exit()`.
  * Otherwise, call `file_tell` using the `file_p` pointer in the `file_data` element with a matching `fd` number.
  * Return the result of `file_tell`.
* `void close (int fd)`:
  * Find the `file_p` pointer associated with the given `fd` by traversing `file_data_list`.
    * If no matching `fd` is found, then terminate with exit code -1, i.e., call `thread_exit()`.
    * Otherwise, pass that pointer in a call to `file_close`.
  * In the current thread's `file_data_list`, remove the `file_data` element from the list.
    * This way, if the file is ever opened again, we add a new `file_data` element with a new fd, filename, and file element that has a new file position.
  * Return the result of `file_close`.

### Synchronization

* Since we're using a global lock on all file system operations, we can guarantee that only one thread will be able to touch the file system at a time. If a thread exits while holding the lock, we release it.

### Rationale

* We weren't sure about return values for some of the operations in the case of an error (e.g. fd not found, file system call not successful). When the spec doesn't specify what should be done, we extend the behavior enforced by the test suite by terminating with exit code -1 by calling `thread_exit()`.
  * Specifically, the `read-bad-fd.c` and `write-bad-fd.c` tests enforce the constraint that an invalid fd must “fail silently or terminate the process with exit code -1.”
  * However, there are no invalid fd tests for `filesize`, `seek`, `tell`, and `close`.
  * Alternatively, we could terminate silently, or follow the spec’s behavior for `open` and `read` and return -1.
* The spec mentions that when a thread removes a file, only threads that have opened that file will be able to access it. From looking through the codebase and Eric Hou's answer on Piazza, we believe this functionality is already implemented. Our algorithms support this, as calling open on a removed file will return an error.
* How do we find the next available file descriptor when `open` is called?
  * The solution in algorithms is sorting the `file_data_list` on `fd`, then traversing the list to find the lowest available fd number. For example, if we encounter 3, then 5 in the list, we'll use 4 as the next file descriptor. If there are no such gaps (no files have been closed), then we take the highest used fd number and increment it. This requires linear time search.
  * Alternatively, we considered keeping a separate sorted list of available fd numbers. The list would be initialized as 3. Every time we call `open`, we pop off the first (lowest) number in the list. If the list is then empty, we add back the popped number + 1. Whenever we close a file descriptor, we add it's number back into the list in a sorted fashion. This way gaps are filled before we expand the range of fd number's. This takes linear time w.r.t. the number of available file descriptors.
  * Another option is keeping a bitmap of available fd numbers. This is similar to the second option, and may be more simple.
* Currently running files cannot be written to.
  * Please see data structures and functions: `load` and `process_exit`.
* Where to put the lock?
  * We decided to put the global lock in `syscall.c`. We considered placing the global lock at the filesystem / file levels. However, that would require checking between both of them since `filesys` doesn't implement all operations. Further, all file system syscalls have to go through syscall anyway.
* File seeking past EOF
  * This is already accounted for in the `file` methods.

## Additional Questions

**1. Take a look at the Project 2 test suite in pintos/src/tests/userprog. Some of the test cases will intentionally provide invalid pointers as syscall arguments, in order to test whether your implementation safely handles the reading and writing of user process memory. Please identify a test case that uses an invalid stack pointer ($esp) when making a syscall. Provide the name of the test and explain how the test works. (Your explanation should be very specific: use line numbers and the actual names of variables when explaining the test case.)**
  * This test is placing an kernel space address into the stack pointer. Thus when a syscall is made, attempting to read the arguments on the stack will result in a page fault. Our implementation fixes this by checking first and foremost in `syscall_handler` the validity of the stack pointer and arguments on the stack.
  * `sc-bad-sp.c`
    * **Variables:**
      * `%esp` - stack pointer
      * `$.-(64*1024*1024)` - the address of subtracting 64MB from the program counter.
      * `movl a1, a2`- copy value from a1, to a2.
      * `int $0x30` - software interrupt, in this case calling the syscall handler which is at address `$0x30`
    * **Line 18 Breakdown:** `asm volatile ("movl $.-(64*1024*1024), %esp; int $0x30");`
      * `$.-(64*1024*1024)` means subtract 64MB from the pc (program counter), so in this context we are copying this value onto the stack pointer.
      * We then call the syscall interrupt, which at that point will check the stack pointer which is invalid, thus process should be terminated.

**2. Please identify a test case that uses a valid stack pointer when making a syscall, but the stack pointer is too close to a page boundary, so some of the syscall arguments are located in invalid memory. (Your implementation should kill the user process in this case.) Provide the name of the test and explain how the test works. (Your explanation should be very specific: use line numbers and the actual names of variables when explaining the test case.)**
* In this test, we move the stack pointer all the way to 4 bytes below PHYS_BASE. The syscall number for exit takes up those 4 bytes, but when the syscall handler tries to read the first argument (exit status), it fails due to entering kernel space.
* `sc-bad-args.c`
  * **Variables:**
    * `movl a1, a2`- copy value from a1, to a2.
    * `$0xbffffffc` - an address 4 bytes below PHYS_BASE
    * `esp` - stack pointer
    * `%0` - dummy variable in which the input operand fills in
    * `int $0x30` - syscall interrupt
    * `"i" (SYS_EXIT)` - input operand of value SYS_EXIT
  * **Line 14 breakdown:**
    * `"movl $0xbffffffc, %%esp;` - copy the valid address `0xbffffffc`, which is very close to PHYS_BASE (top of the user’s stack)
    * `"movl %0, (%%esp);` - uses the parameter “i” specified at the end of the asm instruction and replaces %0 with it. This puts the address of the SYS_EXIT syscall on the stack.
    * `int $0x30"` - invokes a syscall.
    * `: : "i" (SYS_EXIT));` - an input variable that passes an integer of value SYS_EXIT

**3. Identify one part of the project requirements which is not fully tested by the existing test suite. Explain what kind of test needs to be added to the test suite, in order to provide coverage for that part of the project. (There are multiple good answers for this question.)**
  * We suggest an additional test to the wait syscall: the case where we call wait on a grandchild’s pid. In this case, we will be     checking how the parent handles a pid that is valid but not one of its children. As stated in the design spec, a thread doesn't inherit its children's children.
  * Specifically, the test will run thread A. A will exec() thread B, and B will exec() thread C. All threads will run through some  meaningless loops to prevent exiting. Then A tries to wait on thread C--we expect a return value of -1.


### GDB
**1. Set a breakpoint at process_execute and continue to that point. What is the name and address of the thread running this function? What other threads are present in pintos at this time? Copy their struct threads. (Hint: for the last part dumplist &all_list thread allelem may be useful.)**
  * `thread running - “main”, address - 0xc000e000`:
    * {tid = 1, status = THREAD_RUNNING, name = "main", '\000' <repeats 11 times>, stack = 0xc000ee0c "\210", <incomplete sequence \357>, priority = 31, nice = 0, recent_cpu = {f = 0}, allelem = {prev = 0xc0035d70 <all_list>, next = 0xc0104028}, curr_priority = 31, acquired_locks = {head = {prev = 0x0, next = 0xc000e03c}, tail= {prev = 0xc000e034, next = 0x0}}, lock_to_acquire = 0x0, wake_up_time = 76, elem = {prev = 0xc0035d80 <ready_list>, next = 0xc0035d88 <ready_list+8>}, pagedir = 0x0, magic = 3446325067}
  * The only other thread present is the “idle” thread, and the `address - 0xc0104000`
    * {tid = 2, status = THREAD_BLOCKED, name = "idle", '\000' <repeats 11 times>, stack = 0xc0104f34 "", priority = 0, nice = 0, recent_cpu = {f = 0}, allelem = {prev = 0xc000e028, next = 0xc0035d78 <all_list+8>}, curr_priority = 0, acquired_locks = {head = {prev = 0x0, next = 0xc010403c}, tail = {prev = 0xc0104034, next = 0x0}}, lock_to_acquire = 0x0, wake_up_time = 0, elem = {prev = 0xc0035d80 <ready_list>, next = 0xc0035d88 <ready_list+8>}, pagedir = 0x0, magic = 3446325067}

**2. What is the backtrace for the current thread? Copy the backtrace from gdb as your answer and also copy down the line of c code corresponding to each function call.**
* `#0  process_execute (file_name=file_name@entry=0xc0007d50 "args-none") at ../../userprog/process.c:32`
  * `process_execute (const char *file_name)`
* `#1  0xc002025e in run_task (argv=0xc0035c0c <argv+12>) at ../../threads/init.c:288`
  * `process_wait (process_execute (task));`
* `#2  0xc00208e4 in run_actions (argv=0xc0035c0c <argv+12>) at ../../threads/init.c:340`
  * `a->function (argv);`
* `#3  main () at ../../threads/init.c:133`
  * `run_actions (argv);`

**3. Set a breakpoint at start_process and continue to that point. What is the name and address of the thread running this function? What other threads are present in pintos at this time? Copy their struct threads.**
* current running thread
  * name = "args-none\000\000\000\000\000\000", address = 0xc010a000
    * `{tid = 3, status = THREAD_RUNNING, name = "args-none\000\000\000\000\000\000",
       stack = 0xc010afd4 "", priority = 31, nice = 0, recent_cpu = {f = 0},
       allelem = {prev = 0xc0104028, next = 0xc0035d78 <all_list+8>}, curr_priority = 31,
       acquired_locks = {head = {prev = 0x0, next = 0xc010a03c}, tail = {prev = 0xc010a034, next= 0x0}},
       lock_to_acquire = 0x0, wake_up_time = 0, elem = {prev = 0xc0035d80 <ready_list>,next = 0xc000e050},
       pagedir = 0x0, magic = 3446325067}`
* other threads:
  * name = “main” address = 0xc000e000
    * `{tid = 1, status = THREAD_READY, name = "main", '\000' <repeats 11 times>,
      stack = 0xc000eebc "u\252\002\300", priority = 31, nice = 0, recent_cpu = {f = 0},
      allelem = {prev = 0xc0035d70 <all_list>, next = 0xc0104028}, curr_priority = 31,
      acquired_locks = {head = {prev = 0x0, next = 0xc000e03c}, tail = {prev = 0xc000e034, next = 0x0}},
      lock_to_acquire = 0x0, wake_up_time = 76, elem = {prev = 0xc0035d80 <ready_list>, next = 0xc0035d88 <ready_list+8>},
      pagedir = 0x0,magic = 3446325067}`
  * name = “idle” address = 0xc0104000
    * `{tid = 2, status = THREAD_BLOCKED, name = "idle", '\000' <repeats 11 times>, stack = 0xc0104f34 "",
       priority = 0, nice = 0, recent_cpu = {f =0}, allelem = {prev = 0xc000e028, next = 0xc010a028},
       curr_priority = 0, acquired_locks ={head = {prev = 0x0, next = 0xc010403c}, tail = {prev = 0xc0104034, next = 0x0}},
       lock_to_acquire = 0x0, wake_up_time = 0, elem = {prev = 0xc0035d80 <ready_list>, next = 0xc0035d88 <ready_list+8>},
       pagedir = 0x0, magic = 3446325067}`

**4. Where is the thread running start_process created? Copy down this line of code.**
* Start_process is called from kernel_thread /threads/thread.c line 604.
  That line invokes function → which is the arg that start_process got passed from process_execute when
  thread_create was called in userprog/process.c line 45.
    * `#0  start_process (file_name_=0xc0109000) at ../../userprog/process.c:55`
    * `#1  0xc002175f in kernel_thread (function=0xc002aa75 <start_process>, aux=0xc0109000) at .
          ./../threads/thread.c:604`
    * `#2  0x00000000 in ?? ()`

**5. Continue one more time. The userprogram should cause a page fault and thus cause the page fault handler to be executed. Please find out what line of our user program caused the page fault. Don’t worry if it’s just an hex address. (Hint: btpagefault may be useful)**
* `0x0804870c`

**6. The reason why btpagefault returns an hex address is because pintos-gdb build/kernel.o only loads in the symbols from the kernel. The instruction that caused the page fault is in our user program so we have to load these symbols into gdb. To do this use `loadusersymbols build/tests/userprog/args-none`. Now do `btpagefault` again and copy down the results.**
* `_start (argc=<error reading variable: can't compute CFA for this frame>,
  argv=<error reading variable: can't compute CFA for this frame>) at ../../lib/user/entry.c:9`

**7. Why did our user program page fault on this line?**
* Currently we are not handling moving the stack pointer down when we need to pass arguments in setup stack.
  In GDB, the stack pointer is  still at 0xc0000000. Therefore, when we call intr_exit the stack pointer tries
  to read starting at PHYS_BASE up, outside of the user stack--thus we get a page fault.

>>>>>>> de0537f55fb36a9bc7a5e0ab85cafafc66a85ebe
