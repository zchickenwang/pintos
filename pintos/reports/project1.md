Final Report for Project 1: Threads
===================================

## Group Members

* Z Wang <zzwang@berkeley.edu>
* Ben Ben-Zour <benbz90@berkeley.edu>
* Bryce Schmidtchen <bschmidtchen@berkeley.edu>
* Yoon Kim <yoonkim@berkeley.edu>

## Changes we made since our initial design document and why we made them:
Although the foundations of our design was solid, we had more insight on how to improve our design after our design review meeting with our TA. After we started implementing our code, we encountered some difficulties in the our design. Thus, we had to adjust and improve our initial approach. Key ideas that were adjusted as we implemented the code were:

### Alarm Clock

* Our initial thought was keeping a list of all threads and counting their ticks until they need to wake up. However after discussing the idea with Eric, he suggested that for efficiency we should only keep track of end time,
  * **Solution:**
  * we stored the wake up time as part of the thread struct.
  * **Why we made this change:**
  * this made our lives easier and the implementation more efficient. That way we remove the need to update every thread on each timer interrupt.
### Priority Scheduler
We addressed the main takeaways from our design review meeting:
* “Storing the `max_seeking_thread_priority` inside the lock struct will likely cause some errors.”
    * “This value is redundant and equals the lock holder's effective priority.”
    * “Storing the same value in two different places might lead to synchronization problems and thus, this minor optimization is probably not worth it.”
    * **Solution:** We ditched this initial idea entirely and instead calculated a lock holder’s effective priority based on the highest effective priority among the locks held. That being said, we added an effective priority (`curr_priority`) variable in `struct lock`.
    * **Why we made this change:** As stated above, this variable was redundant and could have led to synchronization issues. Furthermore, adding effective priority to locks simplified both priority donation and recalculating effective priority upon releasing a lock, if multiple donations have occurred. We will describe this in more detail below.
* “Locks already have a holder value!”
    * **Solution:** Simple fix, we did not add a `thread * holding_thread` variable to `struct lock`.
* “When a thread releases a lock, it does not necessarily go back down to its original priority. If a thread holds more than one lock, it can receive more than one donation which may become relevant if a thread releases a lock. Thus, every thread needs to keep track of all of its donations, not just the highest one.”
    * “The two options for this are generally as follows:”
    * “You can keep track of every lock a thread holds.”
    * “You can keep track of every thread a thread receives donations from.”
    * **Solution:** One option that Eric suggested was that we keep track of every lock a thread holds; however, we have already stated this idea in our initial design via `struct list acquired_locks` in `struct thread`. Nevertheless, we continued with this initial idea, with the added on effective priority on each lock in this list to account for a thread’s effective priority change upon releasing a lock.
    * **Why we made this change:** Having a list of acquired locks along with an effective priority on each lock greatly simplified our implementation in recalculating a thread’s new effective priority upon releasing a lock.
      * For instance, say that we have a thread that holds three locks, and all locks were desired by other threads that have higher priority.
      * Those other threads would donate their priorities to the thread holding the three locks, and the donated effective priority would be stored in each lock.
      * If the original thread released its highest effective priority lock, we could simply recalculate the new effective priority of the thread by first removing the lock from  the `acquired_locks` list, and then calling `list_max` on it.
      * If the original thread released its last lock, then we would simply update its effective priority back to its base priority.
* “Keep in mind that you will need to introduce a new `list_elem` in either locks or threads.”
    * **Solution:** Simple fix, we introduced `list_elem` in both `struct thread` and `struct lock` to call `list_max` with various comparator functions. These comparators compared locks and threads based on effective priority.
    * **Why we made this change:** These comparators simplified our implementation by allowing us to call `list_max` on lists of locks, threads, and even `semaphore_elem`s, instead of keeping the lists sorted.
* “I think using list_max is a very smart idea as it allows you to avoid the complexity of constantly sorting the various lists.”

Other changes that weren’t discussed during or after our design review included the following:
* **Change:** We added `thread_yield()` in `thread_create(..)`, `thread_set_priority(..)`, `sema_up(..)`, and `lock_release(..)` to enforce the constraint that a thread must immediately yield the CPU if it no longer has the highest effective priority.
    * **Why we made this change:** These function calls resulted in cases where a thread was created or unblocked, meaning that the currently running thread may not necessarily have the highest effective priority. Thus, yielding the thread in these functions would satisfy this constraint.
* **Change:** A lock’s effective priority is set to the base priority of the holding thread, and only updated upon priority donation.
    * **Why we made this change:** This change allowed us to easily handle nested priority donation, which essentially recursed from the donating thread down to the lowest holding thread of a lock. While traversing the lock and thread path via `lock_to_acquire` and `lock->holder`, we updated the lock’s effective priority to be that of the donating thread’s.

### Multi-level Feedback Queue Scheduler (MLFQS)

* Regarding maintaining 64 queues to keep track of similar priorities: after discussing with Eric, he highly recommended that we shouldn’t.
  * **Solution:**
  * We listened and as a result found the highest priority thread via linear search in a single list, taking a hit on runtime but greatly simplifying space complexity. 
  * **Why we made this change:**
  * Overall was great for simplifying the concepts to a more manageable bite.

## Reflection
A reflection on the project – what exactly did each member do? What went well, and what could be improved?

## Yoon

### What I did
  * Sketched out the basic implementation of the priority scheduler against the provided test cases with Bryce, which led to some improvements from the initial design, mainly adding effective priorities to locks themselves.
  * Worked together and heavily discussed our implementation approach with Bryce before actual implementation, to solidify our understanding and to simplify our work in the long run.
  * Implemented the approach to managing effective priority for threads with multiple locks by placing effective priority on the locks themselves, and updating them appropriately upon priority donation.
  * Implemented `donate_priority()` to handle nested priority donation.
  * Implemented `lock_acquire(..)` and `lock_release(..)` with Bryce to handle nested priority donation and to account for the new effective priority of a thread upon releasing a lock involving donations.
  * Observed test case behavior of `priority-donate-chain.c` to detect flaws in our design and approach through the entirety of the process.
  * Added appropriate `thread_yield()` calls to various functions to satisfy the constraint of a thread immediately yielding the CPU to a higher effective priority thread.

### What went well
  * Thoroughly discussing our plan of action and sketching out the test cases before implementing the code allowed us to get a nearly working implementation in the first try with only a few bugs.
  * Extensively checking our implementation of the priority scheduler with `priority-donate-chain.c` has helped avoid any major pitfalls. In the end, our overall design was on point, with only minor issues that arose while debugging. These minor issues mainly included `thread_set_priority(..)` logic and missing `thread_yield()`s.
  * Adding comment blocks on the more complicated logic, such as priority donation, has helped immensely with debugging.
  * We made sure to keep our code modular with various helper functions that eased implementation.
  * Version control via branches and communication before merging left us with minimal merge conflicts. In fact, merging our divded parts in the end was not as bad as we expected, since we places respective flags to separate the priority scheduler and MLFQS. 

### What could be improved
  * Making sure that all the modular parts of the code were working as intended via unit tests could have been helpful. Faulty logic in `thread_set_priority(..)` caused behavior that was difficult to debug. Even without unit tests, we could have logically went through each part to ensure it had the desired behavior.
  * Before scrapping an idea or approach, we should have fully tested our approach against the test cases. We slightly deviated away from our original approach since debugging got overwhelming. In the end, it turned out that our original approach was indeed correct, with only a few minor issues.
  * Getting early familiarity with GDB and Pintos overall could have helped. Nevertheless, we managed to become familiar with them by the end, and GDB has been a great help in terms of unforseen behavior.
  * We definitely could have started earlier, which could have helped avoid long days of implementation filled with diminishing returns.

## Bryce

### What I did
  * I initially focused on overall design of the priority scheduler. This began with comprehensively running through examples with Yoon and understanding the minute details of how priority inversion behaved.
  * Worked together and heavily discussed our implementation approach with Yoon before actual implementation, to solidify our understanding and to simplify our work in the long run. Next, I added the design’s necessary variables and modifications to structs.
  * Implemented `lock_acquire(..)` and `lock_release(..)` with Yoon to handle nested priority donation and to account for the new effective priority of a thread upon releasing a lock involving donations.
  * Added appropriate `thread_yield()` calls to various functions to satisfy the constraint of a thread immediately yielding the CPU to a higher effective priority thread.
  * I dealt with the list_elem and list struct as well as the comparators that were necessary to add for our list_max usage. This was utilized throughout this step of the project, most notably in next_thread_to_run.

### What went well
* **Priority Scheduler (Task 2):**
 * Our initial design really persisted through the implementation process and there were minimal things in need of editing in the end.
 * We also managed our code well through clean development practices and diligent management of branches, commits, merges, and more.

### What could be improved
* I think the main thing would be overall time management and planning for our progress. We were fortunate to have finalized our implementations near the end of the week but a lone bug could have easily led to our detriment.

## Ben

### What I did
* As a group we went over all the specs and divided tasks. Our initial thought was to separate the design into 4 small pieces and later form groups of two. 
* Initially focused on designing task 1 - the alarm clock, and the additional question. After thoroughly understanding the ins and outs of the task in hand I was able to join forces with Z and work on task 3 which was more involved. 
* Implemented and designed task 1 - `timer_sleep(...)`, `timer_interrupt(..)` and its data structures. 
Helped implementing task 3 along with Z. 
* Implementing the updates such as `thread_update_priorities(...)`, `thread_update_load_avg(...)`, `thread_update_recent_cpu(...)`, and helping with the structure of the priority scheduling. 
* Most of the work was done trying to debug failed tests. 

### What went well
* After discussing the design and understanding what could be most efficient. Implementation was straight forward and pretty easy. 
* For the third task we encountered some more difficulties, with an emphasis on syntax and the functionality of each data structure (ie linked list), however since our design was solid it was only a matter of familiarity until we were able to figure out all the bugs. 
* Overall:
  * Division of labor worked well, and we were very thoughtful to each member's time constraint and schedule.
  * Communication was key and made the project successful.
  * The experience definitely improved our team work, debugging, and design skills.

### What could be improved

* Early start on the actual implementation could've prevented much pain in the final days of the assignment.
* Creating more tests and walking through them would have improved unforeseen obstacles we encountered along the way.  
* Getting better familiarity with the tools and pintos was key, and we should've emphasized on thoroughly understanding the language and debugging tools.  

## Z

### What I did
* Focused mainly on Part III--revising our design and planning out a lower-level wireframe of how we'd need to change existing functions, as well as breaking up the MLFQS operations into modular logical chunks which allowed Ben to help me with specific functions without worrying about how they fit in at the moment. 
* I implemented the thread creation, update, scheduling, and interrupt functions.
* I spent most of my time debugging for the tests... a lot of mostly-fruitless time as the issue turned out to be a simple typo--using 'elem' (thus traversing the ready_list) au lieu of 'allelem' for updating priorities of all threads. See improved.
* After finishing the parts, I helped mark which pieces of the logic belonged to MLFQS (while Yoon and Bryce marked out which belonged to Priority Donation), in order to merge successfully and pass all tests.

### What went well
* Gained a very deep understanding of how MLFQS works (mostly in planning out our original design which included 64 priority lists and indexes for binary search, although we ultimately followed a simplified version).
* Refactoring made leaps for readability, as well as allowing the implementation to be easily extended to include more sophisticated scheduling features.
* Ben did a great job with leveraging fixed_point_t ops for the calculations. (While debugging, I ended up checking the code many times over--extremely frustrating but useful in a deep understanding of how fixed points are implemented in Pintos.) We did a great job coordinating on debugging, and talking through possible error points was useful and therapeutic. In the process, we became very familiar with native Pintos and cgdb, which should prove useful in the future.
* Talking with Yoon and Bryce about potential pitfalls in Priority Donation was very interesting, as the logic for Part II was more complex.
* Many hours together with the others led to smooth coordination.
* Ben finished his part really quickly and was very eager and helpful with building and debugging MLFQS.

### What could be improved
* **Don't get stuck on stupid bugs!** Spent way too much time looking for errors in the wrong places. The logic for MLFQS is pretty basic so I would've really liked to finish earlier and help out on Part II.
* Simplify design from the beginning. For this project, that would've made a lot more clear how Parts II and III could be blended together.
* Keep track of modified methods for each Part to make the final merge easier.
