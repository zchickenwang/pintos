Design Document for Project 1: Threads
======================================

## Group Members

* Z Wang <zzwang@berkeley.edu>
* Ben Ben-Zour <benbz90@berkeley.edu>
* Bryce Schmidtchen <bschmidtchen@berkeley.edu>
* Yoon Kim <yoonkim@berkeley.edu>

## Alarm Clock

### Data Structures & Functions
 - `thread.c\h`:
   - `void thread_yield()` once a thread is ready appends to ready queue. 
   - pushes threads to sleep.
- `timer.c\h`:
   - `void timer_sleep()`: updates the sleeping threads, calls `yield_thread()` once they are ready. 
   - `struct list Sleeping_threads_queue`: global variable that keeps track of sleeping threads, contains a pointer to a thread and the number of ticks left to wait.
 
### Algorithm 
- `thread.c`: Add threads to the sleeping list. 
- `timer.c\h`:
  - `void timer_sleep()`:
   - Every time interrupt:
      a. Update the sleeping threads - we do that by keeping track of the time a thread should wake up from the start time. 
      b. If a thread is ready, yield_thread() and pop from queue (even multiples threads that are ready).
- Runtime Complexity:
  - O(logn) to add a new thread to the list.
  - O(1) we only need to check the first thread, and see if his time is up, since the queue is sorted by earliest end time.
  - O(1) to remove a thread from the queue.
  - No need to sort the array, since its first in first out. (priority doesn't  play a role here).
- Space Complexity: 
  - O(n) to maintain a list. 
  
 ### Synchronization
 - Potential sync problems could occur when we update the sleeping queue. However we are taking care of it by using the external timer interrupt which means that we update only through the timer interrupt which pauses the current thread run. The interrupt itself is external to our thread scheduling therefore it shouldn’t mess with the sleeping queue updates. 
- Another issue might be saving the ticks in each thread, but since we are using a global queue, then keeping track and\or accidently modifying it by a thread would not be handled in any thread. 
- If a thread is both blocked\blocking and sleeping then that thread will continue to wait until it is ready. The alarm clock should be simple and should not care about that case, and therefore a tick count would continue as if no block is set.

### Rationale
- Main Idea: We want to implement a module that can put threads to sleep, and wake them up after a certain amount of ticks.
When would we wake a thread up, once the end sleep time is up we will push the thread to the ready queue.
 - During brainstorming one idea stood out - using locks\semaphores to prevent race conditions and block threads from getting pushed to the ready queue. However we realized that implementing the alarm clock in that fashion is first of all complex. Secondly, keeping track of locked threads can actually cause race conditions and\or unpredictable behavior, which is in fact what we are trying to prevent. 
- Second idea was handling sleeping threads through the scheduler thread. This concept is easier to implement and keep track. However similarly to threads the scheduler may cause race conditions and modify sleeping threads unpredictably.
Therefore, after realizing that we need to centralize our main focus for this part was to be able to keep track of the sleeping threads through the timer interrupt while keeping a layer of abstraction.
- The main concepts that drive this design is:  
  - Abstraction:
    - Keep track of sleeping threads in a global variable inside  `timer.c`
    - Update and yield threads inside `timer.c` .
    - The queue can only be modified internally, except for pushing new threads to it - The schedule handler only pushes threads to the queue, and therefore will not mess with current sleeping threads. 
  - Simple: 
    - Simple operations. Count ticks, update, yield. 

## Priority Scheduler

### Data Structures & Functions
* `thread.c` / `thread.h`:
    * In `struct thread`:
        * `size_t curr_priority;` // to be added
            * The priority value used for schedule that takes into account donation.
        * `struct list acquired_locks;` // to be added
            * Useful reference for releasing and other lock operations.
        * `size_t max_lock_priority;`
            * Used for priority donation.
        * `lock * lock_to_acquire;`
            * Assisting in priority donation.
    * `init_thread()` // to be edited
        * Initializing new thread members.
    * `next_thread_to_run()`
        * Edited to take into consideration maximum priority when specifying the next thread to run.
* `synch.c` / `synch.h`:
    * `sema_up();`
        * Must be edited to unblock threads with the highest priority.
    * In `struct lock`:
        * `size_t max_seeking_thread_priority;` 
            * Useful for referencing.
        * `thread * holding_thread;`
            * Assisting in priority donation.
        * `lock_init();` 
            * Initialize new lock members.
        * `lock_acquire();`
            * Needing modification for priority donation.
        * `lock_try_acquire();`
            * Needing modification for priority donation.
        * `lock_release();`
            * Needing modification for priority donation.
### Algorithm 
* **Handling the choosing of the next running thread:**
    * The priority will be taken into account within `next_thread_to_run()` by finding the maximum priority element within the ready list.
* **Modification of** `acquired_locks` **and** `waiting_threads` **lists:**
    * When locks are acquired or released and threads are unblocked or block, these lists will be updated accordingly by removing or adding members.
* **Selecting which thread to unblock:**
    * This will additionally be as simple as using default list functions to find the maximum priority thread and unblocking it in `sema_up()` when it is ready to be unblocked. 
* **Priority Donation**:
    * In our implementation of priority scheduling, we associate a priority with each lock. This priority is defined as the seeking thread with the highest priority.
    * Additionally, we associate a priority with each thread, which is the lock priority value of the highest priority lock that is currently references.
    * When the priority of a thread changes due to donation, we must be diligent to ensure that these changes are recognized across the locations that this thread is referenced.
    * **Handling nested donation:**
        * This will be partially implemented by utilizing the `lock_to_aquire` and `holding_thread` references.
            * When a thread has received a beneficial priority, it may seek to acquire a lock and when doing so, it allowed to pass its own priority onto the thread which holds this desired lock.
            * Once a thread that donated its priority receives its desired lock, it revokes the priority which it donated to other threads.
            * The above requirements can be satisfied by traversing the  lock_to_aquire and holding_thread references for the respective locks and threads.
        * With this design, it will most likely be unnecessary to limit the donation recursion depth.
* **Thread and Lock list updates:**
    * In `sema_down()`, we must ensure that a thread is added to the list of waiting threads when it requests to acquire that lock.
    * Once a thread successfully acquires a lock, the thread must be removed from the list that it is referenced within that lock. It may now add this lock to its own list of locks that it holds.
    * During a thread’s releasal of a lock, it must be removed from its held locks. Additionally, we may pass this lock to a thread which desires to acquire it. 

### Synchronization
* **Priority Scheduling:**
    * Firstly, the Pintos lists’ lack of thread safeness can lead to potential synchronization problem. Threads that are expected to be in the ready queue and are about to be scheduled could be corrupted. 
    * Additionally, 
* **Priority Donation:**
    * Potential sync problems can be avoided when base and current priorities are updated. For example, a thread may attempt to update its own priority and at the same time, another thread may attempt to donate its priority to the prior thread. This should be handled by the double priority variables and the systems which handle the updating of the priority. 
    
   * Firstly, the Pintos lists’ lack of thread safeness can lead to potential synchronization problem. Threads that are expected to be in the ready queue and are about to be scheduled could be corrupted. This may have to be tracked.

   * Additionally, the transfer of lock information between threads of varying priorities can lead to race conditions. 

   * Priority Scheduling is not affected by synchronization problems since the functions which scheduling relies on, next_thread_to_run() and sema_up(), are safe from interrupts.

    * Priority Donation:
      Potential sync problems can be avoided when base and current priorities are updated. For example, a thread may attempt to update its own priority and at the same time, another thread may attempt to donate its priority to the prior thread. This should be handled by the double priority variables and the systems which handle the updating of the priority. 

### Rationale

* **Ready_list:**
    * This maximum priority members within this list are scheduled and dealt with by simply using list_max(). This method simplifies the       scheduling of these threads without having to have a more complex data structures for ordering prioritized threads. As long as the       prioritized are assigned and read correctly, finding the maximum in linear time will return the thread we need.  
* **Priority Donation:**
    * lock_to_aquire and holding_thread:
      By having references of the locks that are being sought to acquire and what threads are being held by a lock, we will be able to         traverse these references in order to determine where to transfer priority. 
* **max_seeking_thread_priority and max_lock_priority:**
    * When a lock is released or acquired, we will need to modify priorities across the threads. By maintaining these references, these      updates will be much easier.
    * Overall, these design decisions for the priority scheduler are centered around enabling robust thread and lock communication while allowing our priority donation system to be fault proof.

## Multi-level Feedback Queue Scheduler (MLFQS)

### Data Structures

* __Constants__
    ```c
    #define NICE_MIN -20 // Minimum niceness
    #define NICE_MAX 20 // Maximum niceness
    ```
* __Global Variables__
    ```c
    static fixed_point_t load_avg // Current load average
    static uint8_t next_priority_list // Index of list containing highest-priority thread in ready_queue
    static int ready_thread_count // Number of threads in ready_queue
    ```
* __Ready Queue__
    ```c
    // Multi-level Queue is a list of 64 lists
    static struct list ready_queue;
    
    // Each list represents a priority level (0...63)
    static struct list priority_0;
    ...
    static struct list priority_63;
    
    // Each list element represents a thread
    struct thread_list_elem
      {
        struct thread t;
        struct list_elem elem;
      };
    ```
* __Blocked Queue___
    ```c
    // List of blocked threads. Contains 'thread_list_elem' from above
    static struct list blocked_threads;
    ```
* __Thread Struct__ 
    ```c
    struct thread
      {
        ...
        int nice; // Current niceness
        fixed_point_t recent_cpu; // Current recent CPU
      };
    ```
* __List Struct__
    ```c
    struct list
         {
           struct list_elem head;      /* List head. */
           struct list_elem tail;      /* List tail. */
        int size;                   /* List size. */
         };
    ```
* __Existing Functions__
    * `thread_tick`
        Calls functions that update thread priorities, `load_avg`, and `recent_cpu`.
    * `next_thread_to_run`
        Chooses thread with highest priority. If multiple, perform RR.
    * `thread_block`
        Moves thread into blocked threads list.
    * `thread_unblock`
        Inserts thread back into ready queue.
    * `thread_init`
        Initializes ready, blocked queues and `load_avg`, `next_priority_list` values.
    * `thread_create`
        Initializes new attributes in thread struct. Ignores priority argument, adds niceness argument.
    * `thread_set_priority`
        Ignores attempts to manually set priority.
    * `thread_set_nice`
        Sets niceness and calculates priority, yielding if necessary.
    * `thread_get_nice`
        Returns current thread's niceness.
    * `thread_yield`
        Taking out the logic that pushes the current thread to the end of ready list.
    * `thread_exit`
        Removes from `ready_queue` and decrements `ready_thread_count` if necessary.
* __New Functions__
    * `void update_load_avg (void)`
        Updates `load_avg`.
    * `void update_recent_cpu (void)`
        Updates `recent_cpu` for all threads.
    * `void update_priority (void)`
        Updates `priority` for all threads.

### Algorithms
* __Choosing next thread to run__
    * Our `next_priority_list` value tells us which list in `ready_queue` to look at. We return the first item in that list. 
    * If that list is empty, then we perform binary search on the set of smaller priority lists to find the highest-priority nonempty list in `ready_queue`, setting its index as the new `next_priority_list` value (this takes log(`next_priority_list`-1) complexity) 
    * Before choosing the thread, we verify it still exists. If not, continue searching.
    * If `ready_thread_count` is 0 or `ready_queue` contains no threads, then run the idle thread. (Addresses *)
* __Updating Priorities__
    * This is called at the beginning of `schedule()` on every 4th tick. Before we can know which thread should run next, we need to recalculate priorities. 
    * To do so, we run through the `ready_queue` (starting at the `next_priority_list`'th list), applying the given priority formula. Each thread then gets appended onto the end of the list corresponding to its new priority (looping through the original list's size so no thread gets updated twice). 
    * In the end, we also recalcuate the current thread's priority, and add it back into the end of its corresponding list (this ensures RR if there are multiple threads in that list). During this process, we update the `size` values for each priority list, thus avoiding the linear-time default size algorithm.
    * All the while, we also keep track of the highest priority any updated thread has taken, and this becomes the new `next_priority_list` value. We also count the total number of ready threads, and this becomes the new `ready_thread_count` value.
    * We also update the priorities of threads in the blocked list. This is because when they get added back into the `ready_queue`, their priority values must be up-to-date.
    * Assuming constant atomic operations, this process takes linear time complexity w.r.t. the number of all threads.
* __Updating load_avg, recent_cpu values__
    * Both these attributes are recalculated every second, or 100 ticks. We use the given formulas on `load_avg`, then `recent_cpu`. Furthermore, we increment the `recent_cpu` of the current thread at each tick.
* __Initializing threading system__
    * Initialize ready and blocked queues. 64 lists get initialized and placed into the `ready_queue`. We also set `load_avg`, `next_priority_list`, and `ready_thread_count` at 0.
* __Creating a new thread__
    * Upon creation, we copy the given niceness and call `update_priority()`. The thread then gets unblocked (see unblocking below).
* __Blocking & Unblocking a thread__
    * If a thread gets blocked, we remove it from its list in `ready_queue` and place it in `blocked_threads`, decrementing `ready_thread_count`. `thread_yield()` is called.*
    * If a thread gets unblocked, we place it into the `ready_queue`, updating `next_priority_list` if necessary and incrementing `ready_thread_count`. Call `thread_yield()` to run new highest_priority thread.
* __Setting niceness__
    * Sets nice attribute of current thread. We recalculate `recent_cpu` and `priority`. If the priority decreases, turn off interrupts and call `thread_yield()` to run new highest_priority thread.*

*There's a special case after thread blocking, setting niceness, completing, sleeping, exiting, or somehow getting deleted on a tick that doesn't correspond with updating priorities. If the thread in question is the sole member of the priority list indicated in `next_priority_list`, then `next_priority_list` will be invalid. We address this in 'Choosing next thread to run'.

** Operations involving real numbers--calculating `priority`, `load_avg`, and `recent_cpu`--involve using floating point operations given in `fixed-point.h`.


### Synchronization
All of our method logic occurs in a context where interrupts are turned off (this is a tradeoff discussed in 'Rationale'). Any shared data, including the lists within `ready_queue`, therefore is accessed only by one thread at a time. Before selecting a thread to run, we check that it hasn't been deallocated or otherwise removed.


### Rationale
* How many queues
    * More queues is better! If we only had one large `ready_list`, then sorting the list would take `nlog(n)` time (alternatively we can perform binary search each time we select a new thread to run, which adds up to about the same complexity). This adds significant overhead to `timer_interrupt`, which we know is costly. Compare that with having k lists, where we would still like to keep them sorted, taking `k(n/k log(n/k))` time. Because `nlog(n)` grows monotonically at an increasing rate, increasing `k` gives us better time complexity.
    * At the other extreme, as we have designed, we have a list for each priority level. This means that the lists do not need to be sorted, as threads within a list just run in RR. Furthermore, updating priorities simply means appending each thread to its corresponding priority list (without having to maintain sorted order via binary insertion).
    * On the downside, we have to create 64 lists. We believe this spatial increase is worth the corresponding simplicity and time-efficiency of our algorithms.
* 'Avoid disabling interrupts'
    * This was by far the most challenging aspect of this problem. Including all the priority-updating logic within the non-interrupt environment of `thread_tick` seemed to be going against everything the design doc suggested.
    * However we tried different solutions, eventually coming back to the assertion that determining priorities is too critical a function to be allowed to be interrupted. After all, updated priorities are essential to selecting the next thread to run, and they cannot afford to be delayed.
    * One alternative was factoring out the update priority logic to a new thread, which is then given the highest priority and placed in the `ready_queue`. This way, we allow the current thread to finish its `TIME_SLICE`, while also allowing interrupts when the priorities get updated. The issue here is that by the time our new thread is chosen to run, it's values will be lagged behind the true current priorities. Furthermore, if the updated priorities say that the current thread should no longer be running, then allowing it to complete its 4 ticks seems wrong. Moreover, if an interrupt were to occur when priorities are being updated, they would lag even further behind the true current values, and perhaps its execution could be stayed off indefinitely. Addressing this problem would make our logic incredibly complex (removing the out-of-date update priority thread and creating new ones that make up for the lost time).
    * Another thought was to use semaphores, as was suggested in Piazza. Devin talked with us about the producer-consumer model that might be leveraged. In that case, the producer could be updating priorities, which `next_thread_to_run` would wait on. However, this runs into a similar issue as the previous idea, where interrupts can cause a lag in priorities. Further, assuming we don't encounter interrupts, then time-wise this is exactly the same as our current implementation.
    * Another use of semaphores would be to add all ready threads as waiters to a semaphore that's controlled by the `update_priority()` method. After calculating priorities, it would wake up the highest-priority waiter. This did not appear to solve any of the forementioned problems.
    * In our minds, the key optimization is reducing the amount of time spent with interrupts disabled. The next thought was--perhaps it's not the arithmetic itself that takes up inordinate time, but other operations like function calls (hence the idea of using semaphores). However, this turned out to be somewhat of a dead end as well.
    * Before arriving at our implementation decision, we tested how long it would take to perform these priority updates. Starting with 100 threads, the first priority calculation took ~.575 ticks. This seems pretty large, but in practice threads will not likely be instantiated all at once, and so this time will be relatively spread out. After this initial calculation, all further updates took on average ~.01 ticks, which felt to us an acceptable cost, especially considering that any other implementation would also have its own non-zero time cost.
* Complexity
    * We've added a relatively small space complexity with the overhead of 64 priority lists (discussed above), as well as attributes like `size` within each list, and global vars like `ready_thread_count`. In doing so, we avoid costly operations like linear-time size algorithms.
    * Other time complexities are included in the 'Algorithms' section.

## Additional Questions 

### 1. 
- We want to see a case where priority donation should take precedence. Create a test where we have 3 threads. A simple test would be to create 3 threads with different priorities. Lets call them H(high), M(medium), L(low). We would also acquire a semaphore. Thread H would create a semaphore and initialize it to 0, and pass it to L, to wait until L is complete. Since there is no priority donation instead of L getting high priority in order to release the H thread, M would be the thread picked. 
- With priority donation - H donates priority to L which then is processed first, then H, and lastly M.
- Without priority donation - M first, then L, and lastly H
- Pseudocode for test
  1. Create 3 threads - H, M, L
  2. `sema_init(sema, 0)` - Initialize semaphore through H with value 0.
  3. Pass to thread L. 
  4. `sema_down()` with H\Wait to `sema_up()` from L (order doesn’t matter).
  5. Test what thread was sent to CPU. 
  6. If L H M then test passes, otherwise Test fails.
- **Update (After Design Review):** See `priority-donate-sema.c` for the correct test.
### 2. 
timer ticks | R(A) | R(B) | R(C) | P(A) | P(B) | P(C) | thread to run
------------|------|------|------|------|------|------|--------------
 0          |   0  |   0  | 0    |  63  | 61   |59    |A
 4          |   4  |   0  | 0    | 62   | 61   |59    |A
 8          |   8  |   0  | 0    |  61  | 61   |59    |B
12          |   8  |   4  | 0    |  61  | 60   |59    |A
16          |  12  |   4  | 0    |  60  | 60   |59    |B
20          |  12  |   8  | 0    |  60  | 59   |59    |A
24          |  16  |   8  | 0    |  59  | 59   |59    |C
28          |  16  |   8  | 4    |  59  | 59   |58    |B
32          |  16  |   12 | 4    |  59  | 58   |58    |A
36          |  20  |   12 | 4    |  58  | 58   |58    |C

### 3.
At some point the priorities were equal for several threads. To break ties in this case we used the thread the had the least recent_cpu but in general there could be an arbitrary tie breaker (say ascending order of id’s). The reason it doesn’t matter is since we will be using a “round robin” method where we rotate highest priorities every 4 ticks.
