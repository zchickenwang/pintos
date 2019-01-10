Design Document for Project 3: File System
==========================================

## Group Members

* Z Wang <zzwang@berkeley.edu>
* Ben Ben-Zour <benbz90@berkeley.edu>
* Bryce Schmidtchen <bschmidtchen@berkeley.edu>
* Yoon Kim <yoonkim@berkeley.edu>

## Task I: Buffer Cache

### Data Structures and Functions

* Data Structures
`cache.c/h`
```
static cache_entry cache[64]; // static cache array

struct cache_entry {
	block_sector_t sector;
	char data[BLOCK_SECTOR_SIZE];
	struct semaphore cache_entry_sema;
	int64_t last_use_time;
	bool dirty_bit;
			};
```

* Functions
  * `cache.c`
  * `bool cache_init ()`
  * `bool cache_read_block (block_sector_t sector, void *buffer_, off_t size, off_t offset)`
  * `bool cache_write_block (block_sector_t sector, void *buffer_, off_t size, off_t offset)`
  * `int cache_add_block (block_sector_t sector)`
  * `void cache_done ()`
  * `inode.c`
  * `inode_read_at`
  * `inode_write_at`
  * `filesys.c`
  * `filesys_init`
  * `filesys_done`

### Algorithms

* `cache.c` methods
  * `cache_init ()`
    * initialize the cache array by filling it with blank `cache_entry`'s having default values:
      * `block_sector_t` = 2^23+1 (to ensure no hits)
      * `cache_entry_sema` initialized at 1
      * `last_use_time` = 0
      * `dirty_bit` = 0
    * returns if the `malloc` was successful
  * `cache_read_block (block_sector_t sector, void *buffer_, off_t size, off_t offset)`
    * look for the given sector in the cache
    * if found, try to acquire that block's `cache_entry_sema` using `sema_try_down`
      * if the down succeeds, we can just read! update `last_use_time` to the current time, then release `cache_entry_sema`.
      * if the down fails, we need to account for the possibility that the block is currently being evicted. thus upon waking up, check if the sector number has changed. if it hasn't, we're good and read per usual. if it has, then act as if the block was not found in the first place (next bullet).
    * if not found, then we need to bring the block in!
      * call `cache_add_block (sector)`, which returns the index of the newly-added cache entry
        * `cache_add_block` also gives us ownership of that cache entry's semaphore
      * now we can execute the actual read request, updating `last_use_time`
      * finish by releasing `cache_entry_sema`
    * returns if the read was successful
  * `cache_write_block (block_sector_t sector, void *buffer_, off_t size, off_t offset)`
    * look for the given sector in the cache
    * if found, try to acquire that block's `cache_entry_sema` using `sema_try_down`
      * if the down succeeds, we can just write the changes to `data`! update `last_use_time`, set `dirty_bit` to 1, then release `cache_entry_sema`.
      * if the down fails, we need to account for the possibility that the block is currently being evicted. thus upon waking up, check if the sector number has changed. if it hasn't, we're good and write per usual. if it has, then act as if the block was not found in the first place (next bullet).
    * if not found, then we need to bring the block in!
      * call `cache_add_block (sector)`, which returns the index of the newly-added cache entry
        * `cache_add_block` also gives us ownership of that cache entry's semaphore
      * now we can execute the actual write request (updating `last_use_time`, writing to the entry's `data`, and setting its `dirty_bit` to 1)
      * finish by releasing `cache_entry_sema`
    * returns if the write was successful
  * `cache_add_block (block_sector_t sector)`
    * first find a replacement candidate via LRU
    * scan through the cache array, keeping track of the least-recently used cache entry index and its `last_use_time` (LRU means smallest `last_use_time` value)
    * once this LRU candidate is found, we `try_sema_down` on its `cache_entry_sema`.
      * if the down succeeds, great! we can start the eviction phase
      * if not, then we will wait on it. however upon waking up, we will rescan the entire cache to see if our sector has been brought in while we were waiting.
        * if it has indeed been brought in, we don't need to bring it in again.
          * just try to acquire that entry's lock like any normal read/write operation whose sector is in the cache!
        * if it has not been brought in, then we need to search for a new LRU candidate. start over at the first bullet point!
          * this is because while we were waiting, something was using this block, which means this block is no longer the LRU one in the cache
          * to cut out this scan, we can keep track of the LRU candidate during the search for the sector! if the sector is found, we can just ignore the LRU.
    * now that we have the replacement candidate, start eviction phase
    * check the entry's `dirty_bit`. if dirty, then write back the existing sector to disk using `block_write (...)`
    * then change the entry's values to:
      * `sector` = new sector being brought in
      * `data` = new sector via `block_read (...)`
      * `last_use_time` = current time via `timer_ticks ()`
      * `dirty_bit` = 0
    * returns the index of this newly-added block
  * `cache_done ()`
    * writes back all dirty blocks

* `inode.c` methods
  * `inode_read_at`
    * calls `cache_read_block` on all blocks encompassed by the read
  * `inode_write_at`
    * calls `cache_write_block` on all blocks encompassed by the write

* `filesys.c` methods
  * `filesys_init`
    * calls `cache_init`
  * `filesys_done`
    * calls `cache_done`

### Synchronization

* When two threads access a cached sector simultaneously
  * They will both access into the cache, finding the same cache entry.
  * Whoever arrives first will take the `cache_entry_sema`, and the next thread will wait on the semaphore. See ALGORITHMS for details.

* When one process is actively reading or writing data in a buffer cache block, how are other processes prevented from evicting that block?
  * In the eviction process, we use LRU algorithm to find a replacement candidate.
  * If it so happens that the candidate is currently being read (but the reading process hasn't yet updated `last_use_time`), then the evicting process will wait on its `cache_entry_sema` and let the read finish.
  * When it's done waiting, the evicting process will scan the cache again to see if its block has been brought in while it was waiting--if not, search for a new LRU candidate.

* During the eviction of a block from the cache, how are other processes prevented from attempting to access the block?
  * The thread evicting the block will hold its semaphore. Thus, any new threads trying to access the block will wait on the semaphore.
  * When waiting threads are woken up, however, the content of the block will have changed! To account for this, we perform a check immediately after `cache_entry_sema` is acquired to verify that the block sector is still the one we want (and hasn't been replaced). If the cache block has been replaced, we try to find the sector we want in the hash table again; if it's not there, we'll go through the normal process of bringing a new sector in.

* If a block is currently being loaded into the cache, how are other processes prevented from also loading it into a different cache entry?
  * Say two threads concurrently try to access a sector that's not in the cache.
  * CASE 1: ALMOST SIMULTANEOUSLY
    * If one of the threads finds a replacement candidate first, acquiring its semaphore and changing its sector number, then the second thread will find this half-brought-in block in the cache and wait on the lock (which means waiting for the first thread to bring in the new sector and finish using it). No problem here.
  * CASE 2: SIMULTANEOUSLY, AND FIRST PROC DOESN'T WAIT
    * Now if both threads enter the eviction phase together, they'll both find the same LRU replacement candidate. In this case, one will acquire the candidate's semaphore and the other will wait on it.
    * The one who waits, upon waking up, will rescan the cache and find that its block has been added! It'll down on the semaphore and use it like normal.
  * CASE 3: SIMULTANEOUSLY, AND FIRST PROCS WAITS
    * Both threads enter eviction phase together. The first find an LRU candidate, but a read request gets there just before and acquires the lock! So the first thread will wait on this lock, and only then does the read request update the `last_use_time`.
    * Now the second starts searching for a replacement candidate, but it'll find a different cache block (since the previous LRU's `last_use_time` has been updated)!
      * If this second thread doesn't wait and brings in the block right away, the first thread will wake up, rescan and find that it's block has been brought in.
      * If this second thread also waits, then both threads will eventually wake up and rescan the cache for their block, thus looping back into CASES 1/2/3.
      * It is highly unlikely that this looping will occur, so the probability that two threads repeatedly wait and rescan quickly converges to 0.

* Two threads try to evict a cached sector simultaneously (bringing in different sectors)
  * Whoever arrives at the replacement candidate first will acquire its `cache_entry_sema`.
  * The slower thread will wait on that semaphore--upon waking up, it'll scan the cache to make sure the it's sector hasn't been brought in while it was waiting. If not, it'll look for a new LRU candidate.

* How are other processes prevented from accessing the block before it is fully loaded?
  * If an entry is currently being loaded into the cache, the loading thread will own the cache block's semaphore. Any threads that want to access it will have to wait until the loading thread finishes loading and using the entry.

* All cache entries are currently in use.
  * In our LRU implementation, the incoming thread will find the LRU block and wait on its semaphore. Eventually it'll wake up, rescan, and look to bring its block into the LRU candidate at that time.
  * If the cache is still fully utilized then, this process will repeat.
  * Per instructor comment on Piazza, busy waiting will suffice for this case. What we're doing is much better (no busy waiting), so this should be covered.

### Rationale

* We spent the most time thinking through different design implementations for this case, mostly in response to the different synchronization situations listed in the previous section.
  * Array vs Hash
    * The Hash table is explicitly recommended for this project, and we tried to use it as the cache structure. However, the array proved to be simpler while still achieving concurrency requirements.
  * Hash Overview:
    * constant access time
    * but not intuitive to access (need to malloc a struct to pass into the `find` function--this seems overly complicated, and a hashmap would've suited our needs much better)
    * bad iterator--how can we iterate through the cache during LRU? according to the spec, iterators are invalidated if concurrently modified, and we can't ensure that nobody will try to access the cache during LRU candidate search (without disabling all accesses to the cache via a global lock). this is the main problem!
  * Array Overview:
    * linear access time
    * much simpler design--for instance, accesses just look for an entry which has the desired sector
    * intuitive indexing--this allows us to easily reference a specific cache entry
    * good iterator--here, scanning the cache just means going through the array, and we're okay if other entries are being modified at the same time
  * We also considered using both!
    * Perhaps an array representation with an additional hashtable to provide constant access time. This, however, seemed overly complex, and we'd have a lot of overhead in maintaining the hash alongside the actual cache.
    * Or instead, a hash representation with an array of pointers to facilitate iterating. This was cut for similar reasons.
  * We also considered using neither!
    * What if we built our own hashmap? We could then easily map sectors to cache entries, as well as implement iterating over the keys. This led to two options: hashing to 64 indexes with external chaining, or hashing directly to the sector number. The former doesn't work because we'd ultimately need a lock for each chain (complicated!), while the latter doesn't work because we'd need to initialize 2^23 blank cache entries (too much space!).

* How do we ensure concurrent accesses don't mess up the cache?
  * We considered
    * a global 'guard' lock to regulate accessing the cache (every thread must obtain the lock at first to check if its sector is in the cache--if it isn't then the thread holds the lock while bringing in the new block)
    * a global 'guard' lock on two global lists of 'blocks being brought in' and 'blocks being evicted'. every thread would first obtain the lock and check if its sector is being brought in or evicted; similarly, every thread that brings in/evicts a block needs to declare so. this would prevent the issues listed in SYNCHRONIZATION.
    * a global 'guard' lock on a hashtable of sectors currently being evicted or brought in. this way checking if a thread's block is in the eviction process would take constant time.
    * a cache entry 'guard' for adding blocks, aka another lock used by threads who are evicting a block. in this scenario, eviction operations would need both the normal and eviction lock.
    * a separate process dedicated to eviction and bringing new sectors into the cache. any time replacement is needed, we'd alert that process of the new sector that needs to be brought in. then if the eviction process is holding a lock, any read / write operations could know from checking the lock holder. but how do we create this asynchronous communication between processes? how do we implement a queue for the eviction process, and how do we add things to it?

* Clock vs LRU
  * Why we chose LRU
    * More effective.
    * Clock was annoying because how do we synchronize changing the clock hand, clock bits? We'd need to have a lock on using the clock, which means a lock on eviction. This creates more problems than it helps.
    * In order to decrement the clock bit on an entry, we'd need to know if it's being held. But how can we tell if a semaphore is held without the possibility of accidentally downing on it? This functionality is not implemented in Pintos.

* Further Optimizations
  * Including some sort of hash for constant access time into the array
  * Using a priority queue / doubly-linked list to keep track of least recently used blocks
  * Not setting the dirty bit on a write operation that doesn't actually change the data
  * Write-behind / Read-ahead



## Task II: Extensible Files

### Data Structures and Functions


* `free_map.c`
  * `struct semaphore free_map_sema; // lock for serializing access to the freemap`
  * `void free_map_init (...)`
  * `bool free_map_allocate (block_sector_t *sector_p)`
  * `void free_map_release (block_sector_t *sector_p)`

* `inode.c\inode.h`
```
struct inode_disk {
off_t length; // current size of file
	uint32_t next_sector_index; // index of next pointer to allocate
	block_sector_t direct[8]; // 8 direct pointers
	block_sector_t indirect[4]; // 4 indirect pointers
	block_sector_t doubly_indirect; // 1 doubly-indirect pointer
	unsigned magic;
	uint32_t unused[111];
};

struct indirect_inode_disk {
	block_sector_t direct[127]; // 127 direct pointers
	unsigned magic;
};

struct doubly_indirect_inode_disk {
	block_sector_t indirect[127]; // 127 indirect pointers
	unsigned magic;
};

struct inode {
	...
	struct semaphore alloc_sema; // lock for file extending
	/* REMOVE 'struct inode_disk data' */
};
```

  * `byte_to_sector (...)`
  * `inode_create (...)`
  * `inode_open (...)`
  * `inode_close (...)`
  * `inode_read_at (...)`
  * `inode_write_at (...)`

* `syscall.c\syscall.h`
  * `inumber (int fd)`

### Algorithms

* `free_map.c` methods
  * `void free_map_init (...)`
    * initialize the `free_map_sema` to 1
  * `bool free_map_allocate (block_sector_t *sector_p)`
    * only allocates one block at a time!
    * first acquire the `free_map_sema`. this prevents synchronization issues with multiple processes trying to change the freemap concurrently.
    * use existing logic to find the number of a free sector, then store it in the passed-in argument.
    * release the semaphore
  * `void free_map_release (block_sector_t *sector_p)`
    * first acquire the `free_map_sema`
    * release just the one sector specified in the argument
    * release the semaphore


* `inode.c\inode.h` methods
  * GENERAL APPROACH
    * we're following the Unix tradition of direct, indirect, and doubly-indirect pointers. Here, a pointer is just the number of a sector that contains either data or more pointers.
    * the order of the pointers follows the order of the data pages they point to.
    * regarding all the inode methods below:
      * when we fetch a sector pointed to from `inode_disk`, we may have to traverse indirect or doubly-indirect pointers. this entails finding the sector number of the indirect pointer, reading in the sector as an `indirect_inode_disk` or `doubly_indirect_inode_disk`, and then continuing the search...
        * for instance, say we're looking for the sector at index 10. we can go past the 8 direct pointers, which means looking for the 10-7 = 3rd data sector in the first indirect pointer. so we read in sector `indirect[0]`, casting it as an `indirect_inode_disk`, then fetch the data page at its third (direct) pointer.
  * `byte_to_sector (...)`
    * find the sector index by dividing offset by the sector size. then read the `inode_disk` at `sector` to get its sector pointers, and fetch the one at the index we calculated.
  * `inode_create (...)`
    * ignore the length argument--files are expanded only upon writes, so set the `inode_disk->length` field at 0. create a semaphore initialized to 1 as `inode_disk->alloc_sema`.
    * we allocate only 1 sector for the `inode_disk` itself!
  * `inode_open (...)`
    * don't read the `inode_disk` data into memory!
  * `inode_close (...)`
    * if the file needs to be removed, we'll read the `inode_disk` and call `free_map_release` on all the sectors it points to. then release the sector of the `inode_disk`.
    * free the `inode` as usual if it's `open_cnt` is now 0.
  * `inode_read_at (...)`
    * we read in the `inode_disk` to get its length and sector pointers.
    * using the offset, we calculate which sector index the read starts at. then find the sector number at that index from `inode_disk`.
    * if the read length is larger than a sector, then we repeatedly find the next sector to by traversing the pointers in `inode_disk`, using a bounce buffer for the last sector if necessary.
    * if the read offset / length go past EOF, then just put '\0's after the file ends.
  * `inode_write_at (...)`
    * read in the `inode_disk` to get its length and sector pointers.
    * first we allocate any needed sectors!
      * this way, if we run out of memory halfway through, no (irreversible) writes will have yet been made.
      * first calculate the number of sectors encompassed in the write using `size` divided by sector length. also find out the sector index at which the write begins using `offset` divided by sector length.
      * if the starting sector index + number of sectors is greater than the `next_sector_index` in `inode_disk`, then we need to allocate new sectors!
        * first down on `alloc_sema`.
          * once acquired, we need to recheck if we need new sectors. perhaps another process has just finished extending the same file!
          * to account for this, reread in the `inode_disk` and recalculate the number of new sectors needed--if this is now 0, up the semaphore and start the writing phase; otherwise continue.
        * we allocate by calling `free_map_allocate` as many times as the number of sectors needed.
	* each time, save the newly allocated sector number in `inode_disk` at the `next_sector_index`, and increment the index.
	* if we're in indirect or doubly-indirect pointer territory, then we need additional allocations for `{doubly_}indirect_inode_disk`s.
	  * for example, say `next_sector_index` is 8. the direct pointers go up to index 7, which means we need to first allocate a sector to store an `indirect_inode_disk`. We still need a new sector for the actual data, whose sector number will go at index 0 of the `indirect_inode_disk`'s pointer array.
	* be sure to save the changes made to `inode_disk`, as well as any `{doubly_}indirect_inode_disk`s by writing them to the disk!
	* now we can up the `alloc_sema`. if any processes were waiting to extend this file, they'll reread `inode_disk` and check if we just did it for them.
	* NOTE: if at any time the freemap tells us there's no more space, we need to release all the sectors that have just been allocated. we don't need to write anything to disk in this case, just exit the method. everything will look as if this write never happened.
      * since we now have all the sectors we need, just start writing the data. use the general method of going through data sectors via `inode_disk` pointers, except each time also write the modified sector back to disk.
      * be sure to update the length of the file in `inode_disk` if the write goes past the old EOF.

* `syscall.c\syscall.h` methods
  * `inumber (int fd)`
    * perform sanity checks.
    * look for the `file_data` at fd--if found, return `inode_get_inumber` on the `inode` associated with it (`file_data->file->inode`). The inumber is just the sector number of the corresponding `inode_disk`, which is unique and suggested in the spec.


### Synchronization

* Multiple files allocating space on the freemap concurrently
  * We account for this using a lock on freemap operations. Thus only 1 process can update the freemap at any given time.

* Multiple threads writing to the same file, all allocating new blocks
  * In this scenario, multiple threads have opened the same file and are all writing past its EOF, which means they all need to allocate new blocks.
  * If we allow them to each allocate space and update the `inode_disk`, then we have a bunch of allocated sectors floating around untethered!
  * Thus, we serialize file extending via a lock in the `inode`. Multiple processes will operate on the same `inode` for a given file, and so they'll have to allocate space one at a time. If a thread has allocated enough space for the ones after it, they can all just start writing.



### Rationale


* Do we allocate a single sector or a continuous chunk at a time
  * Right now `free_map_allocate` looks for a continuous chunk of sectors. We could've modified this slightly to allocate the largest possible continuous section, and also return the length of that section. This way, when we're looking to allocate n sectors, we can call the function however many times as needed to get the total sum of section lengths = n.
  * We ended up deciding to just allocate one block at a time. The bad side of this is that we may need to call the function many times, which is overhead. The good side is that it's much simpler. Furthermore, our new `inode_disk` has pointers to each sector anyway, so the 'allocate a sector, add a pointer, repeat' approach is very intuitive.

* Do we need extra structs for sectors that hold indirect / doubly-indirect pointers?
  * This is a little work that goes a long way. Whenever we take in one of these sectors from disk, our structs format them in a way that's easy to work with. Without this structure, we'd have unnecessary logic in interpreting the raw data.
  * This also makes the code more readable, and the format of the indirect sectors more well-defined. If we need to change that format, it's much easier.

* Distribution of pointers
  * What number of direct, indirect, doubly-indirect pointers do we use?
  * The key insight was that a single doubly-indirect pointer can hold 8 MB alone. However, so long as we have a single direct pointer, it's not possible to hold 8 MB without using a doubly-indirect pointer.
  * Basically, we need 1 doubly-indirect. But with that doubly-indirect, the rest don't really matter. Of course we want fast access for small files, so we put 8 direct pointers that cover 4 KB. Then 4 indirect pointers to cover what will probably be the majority of Pintos files.
  * Of course, these can be tweaked during implementation without much difficulty.


## Task III: Subdirectories

### Data Structures and Functions

* `syscall.c\syscall.h`
  * `bool chdir (const char *dir)`
  * `bool mkdir (const char *dir)`
  * `bool readdir (int fd, char *name)`
  * `bool isdir (int fd)`
  * `int inumber (int fd)`

* `thread.c`
  * `thread_init() /* Set initial thread's cwd to the root directory */`

  * `thread_create() /* copy parent's cwd into the child's */`

  * ```
    struct thread
    {
	struct dir *curr_dir; /* pointer to process’ working directory */
    	...
    };
    ```
`inode.c/.h`
  * ```
    struct inode
    {
	int open_num; /* also used to count how many processes have this directory open */
	bool is_dir; /* copied in from inode_disk (1 = T, 0 = F) */
	...
    };
    ```
  * ```
    struct inode_disk
    {
	uint32_t is_dir; /* specifies if this inode_disk is a directory */
	block_sector_t parent_directory; /* for navigating directories*/
	uint32_t unused[110] /* adding is_dir means 1 less unused */
	...
    };
    ```
  * `bool inode_create(..., uint32_t is_directory) /* Adding additional parameter */`
    * NOTE: this also means making sure any existing calls to `inode_create` intended to create files has a 0 `is_directory` value (although that should be the case by default). The same applies to `filesys_create` below.
  * `inode open (...) /* make sure to bring in is_dir from the disk */`


* `Filesys.c`
  * `bool filesys_create(..., uint32_t is_directory)`
    * `/* Adding additional parameter */`
    * `/* Changing to not just create in root, but in cwd for relative paths */`

  * `struct file *filesys_open (const char *name)`
    * `/* Changing to not just try to open from root, but from cwd for relative paths */`
    * `/* Update that it can open directories. (But do not support read\write for a directory) */`

  * `bool filesys_remove (const char *name)`
    * `/* Changing to not try to remove only from root, but from cwd for relative paths */`
    * `/* deletes directories which do not contain subdirectories (other than the root) - directories can only be deleted if they do not contain any subdirectories. They also may only be deleted if they’re not the current working directory of a different process. This will be determined by after ensuring the directory exists, we will check it’s open_cnt and ensure it is 0 for deletion. */`

  * `bool path_finder(char *path, struct dir *desired_dir, char* filename)`
    * `/* Given the path string, find the corresponding file / directory. This will utilize get_next_part() */`

* `directory.c\directory.h`
  * `#define NAME_MAX 75`
  * `bool dir_remove (...)`

### Algorithms

* `filesys.c` methods

  * `bool path_finder(char *path, struct dir *desired_dir, char* filename) `
    * This function will utilize `get_next_part(...) ` to take a path string and return the desired directory or file.
    * If the path provided is to a directory, then the `filename` will be set to  `.` and the  `desired_dir ` will reference that 	directory instance.
    * If the path provided is to a file, then the `filename` will reference the file's name and the `desired_dir ` will reference the directory that the file is within.
    * We first check if the provided path begins with `/`. If so, we use `ROOT_DIR_SECTOR` to traverse the absolute path and begin with the root inode.
    * Otherwise, we have a relative path. In this case, we use the current thread's `curr_dir` to traverse to the desired location and along with the `curr_dir` inode.
    * We create a loop that utilizes `get_next_part` to try and find the inode of our desired location. This will parse the path until we find a terminating character.
    * We call `get_next_part` on `path` and `filename` to update our `path` and `filename` and ensure the return value is `1`.
    * On our first iteration, we begin the loop with re-opening the `inode` we first received from our path (whether relative or absolute). On future interactions of the loop, this inode reference is updated to hold the next inode in the sequence.
    * We can now use `dir_lookup` to get the next inode in our sequence.
    * If this lookup is successful and the new inode is not `NULL` and is a directory, then we can close our currently referenced `dir`.
    * Now, at the end of the loop, we update the inode to be the next inode. The loop now iterates once again.
    * If the resulting inode is a directory, we ensure to set the filename to `.`
    * Now if the resulting inode is not `NULL`, we open it and set `desired_dir` to it and return true.

* `syscall.c\syscall.h` methods

  * `bool chdir (const char *dir)`
    * This function will be defined in `syscall.c`
    * Initially, we will parse the provide dir using path_finder (...)
    * If the desired directory is found, whether relative or absolute, we will have a reference to the dir. Otherwise, we return false.
    * Now, with the found dir reference from path_finder, we also want to check that we are able to successfully call `dir_lookup`. This will give us a reference to the directory's inode, as well as provide useful sanity checks.
    * If this all is successful, we must close the thread's current working directory and the new working directory. This is because the directory was opened in the process of checking and looking it up.
    * Following this, we update the current thread's `curr_dir` variable to be a reference to the directory's inode found from the `dir_lookup`. We also increment the inode's open_num count.

  * `bool mkdir (const char *dir)`
    * The logic for mkdir almost exactly resembles the creation of a file. Therefore, we can use `filesys_create` with the slight modifications to fit all of our needs.
    * The definition for `filesys_create` will be changed to contain a parameter for `is_dir`. This will be passed onto `inode_create`.
    * Furthermore, we will include our `pathfinder` function which will parse the provided path and get a reference to the location that we should be placing the new directory.  
    * All of the calls in the existing `filesys_create` will translate smoothly for directory creation.`free_map_allocate` and `inode create` will create memory as necessary.
    * `dir_add` will check for existing directories with the name provided to `mkdir`. We decided to make this function a critical section since it is possible that multiple processes could interleave their calls to `dir_add` within `mkdir` and create two directories with the same name. (See SYNCHRONIZATION)
    * To prevent duplicates we require a process to acquire the inode lock of the current directory we are adding the new directory. A process will attempt to acquire the inode lock as soon as it calls `dir_add`.
    * As already stated in the `filesys_create` function, if the creation process is unsuccessful, we release the inode sector and exit from mkdir.
    * Lastly, we close the referenced dir and return true.

  * `bool readdir (int fd, char *name)`
    * This function will be defined in `syscall.c`
    * We will then call the internal helper `get_file_data_by_fd(...)`
    * Through the `file_data` pointer returned by this function, we will be able to obtain a reference to the `file`, and thus, the `inode` and `is_dir` boolean.
    * If this `is_dir` is false, we return false.
    * Otherwise, we continue to return a call to `bool dir_readdir (struct dir *dir, char name[NAME_MAX + 1])` which will populate a passed in char array with the desired name.

  * `bool isdir (int fd)`
    * This function will be defined in `syscall.c`
    * We will then call the internal helper `get_file_data_by_fd(...)`
    * Through the `file_data` pointer returned by this function, we will be able to obtain a reference to the `file`, and thus, the `inode` and `is_dir` boolean.
    * Returns the boolean `inode->is_dir`

  * `bool dir_remove (...)`
    * Make sure that a directory can only be removed if it does not contain any subdirectories. See SYNCHRONIZATION.

  * `int inumber (int fd)`
    * See task II.

### Synchronization

* Removal of a directory that is the current working directory of a different process:
  * When a file or directory is being referenced by a process, its open_num will be a non-zero value. We will prevent other processes from removing these files or directories in filesys_remove. This will be enabled by a simple check of the open_num value upon attempt to remove.

* Creating a new directory with the same name in the same location:
  * Upon creation within mkdir, if it is found that the desired dir doesn't exists we make sure that two processes will not add the same dir name -- using the current directory inode lock `alloc_sema`. We do that by preventing two processes from calling dir_add at the same time.

* Accessing a deleted directory
  * Since processes cannot kill directories that are open by other processes, we make sure that when we try to delete we acquire the directory’s inode lock `alloc_sema` and make sure that `open_num` is 0.

* Attempting to delete already deleted directories or files.
  * Two threads might try to delete the directory at the same time. We address that by using the inode lock, when removing the directory\file.

* Deleting a directory which contains other directories:
  * This will be prevented by checking the is_dir member of all members in the directory that is trying to be deleted.
  * We considered allowing this but the process of checking all subdirectories recursively to see if they’re in use seems to take up too much time. Alternatively, we could keep a tree-like recursive aggregation of all subdirectories' `open_cnt` in each directory, but this would be too much overhead.



### Rationale

* Keep as much abstraction as possible between the syscalls and disk accessing.
  * Syscalls don't modify inodes, and or directly access disk. This is handled by calling the functions implemented in task II.

* Generalize as much possible the data structures for files and directories so that manipulation of both is similar.
  * We do that by handling both files and directories such that they both have a similar structure, and implementing inode functions such that they can modify both types.

* We decided to prevent the deletion of a directory if it is in use or if it contains directories.
  * By preventing deletion, we will have a more simple and approachable system for keeping track of the files and preventing corruption of data.


## Additional Questions

* Implementation strategy for write-behind
	* In general we want to periodically flush out dirty entries in the cache.
	* We can utilize the timer to decide when should we flush out entries.
	* In `cache_init`, we'll exec a new process dedicated to flushing out dirty entries. This process will be passed in a pointer to the cache (which will now have to be malloc'd so that all threads have access to it).
	* This new process will run a function that flushes out dirty blocks (i.e. writing them to disk), sets those entries' dirty bits to 0, and sleeps for x seconds (say x = .5, 1, 10), and repeats.
	* To make sure that dirty bits aren't set to 0 while a new write is occurring, we must ensure that we hold the cache entry's lock during write-behind.
	* We also would want to be smart about when should we write back.
	* We can either write to disk periodically every X ticks. However that might not be efficient since we don't wanna write-behind on a cache where most of its entries are not dirty.
	* In that case optimization would be keeping track of the number of dirty bits. And every X ticks would check to see if it passes some threshold. If so then spend time and write them all back to disk.
	* Also we don't want to try to acquire an entry to flush if its in use. Therefore while scanning the the cache we avoid entries that are in use.


* Implementation strategy for read-ahead:
 	* We keep a global list (queue) that keeps track of blocks to be pre-fetched.
	* The list is accessed each time a process executes a read.
	* At that point the threads next block is added to the list.
	* Then a process is executed - which his sole purpose is to fetch the blocks on the global list.
	* The list will be malloced in heap so that all threads will have access to it.
pre-fetching can optimize booting and sequential reads. So we would want to read-ahead blocks primarily when a process is reading and add the sequential block of that process to the list.
	* We want an asynchronous operation. We debated if to use an interrupt or a new process that can handle pre-fetching. We decided to go with a new process, since an interrupt would be expensive and will defeat the purpose of having read-ahead as a background job.
	* Our goal is to pre-fetch once a process is reading a file. Therefore, executing a child process when we know a read occurs, will help us optimize pre-fetching of relevant blocks.
