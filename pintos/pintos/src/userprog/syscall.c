#include "userprog/syscall.h"
#include <stdio.h>
#include <stdlib.h>
#include <syscall-nr.h>
#include <string.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/malloc.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#include "userprog/process.h"
#include "devices/shutdown.h"
#include "devices/input.h"
#include "devices/block.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "filesys/directory.h"
#include "filesys/inode.h"

static void syscall_handler (struct intr_frame *);
int user_mem_access_verification(uint32_t* arg, int num_of_args);
bool is_valid_buffer(uint32_t buf, uint32_t size);
bool is_valid_filename(const char *file);

/* Global, lowest available file descriptor number. */
static int available_fd;

/* Print out exit code, for testing purposes. */
void print_exit_code (int code);

void
syscall_init (void)
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");

  /* Initialize the global, lowest available fd number. */
  available_fd = 3;
}

int
user_mem_access_verification (uint32_t *args, int index)
{
  /* Handle validity of user memory access */
  if (!is_user_vaddr (&args[index]))
    {
      return 0;
    }

  /* Ensure that desired address is mapped. */
  if (pagedir_get_page (thread_current ()->pagedir, &args[index]) == NULL)
    {
      return 0;
    }

  return 1;
}

/* Ensures that the buffer being used is in
paged memory and user accessible memory */
bool
is_valid_buffer (uint32_t buf, uint32_t size)
{
  return
      is_user_vaddr((void *)buf)
      && pagedir_get_page (thread_current ()->pagedir, (void *)buf) != NULL
      && is_user_vaddr((void *)buf+size)
      && pagedir_get_page (thread_current ()->pagedir, (void *)buf+size) != NULL;
}

/* Ensures that the filename being used is valid by the
requirements of user paging and user memory space */
bool
is_valid_filename (const char *file)
{
  if (!is_user_vaddr (file))
    {
      return false;
    }
  char *filename = pagedir_get_page (thread_current ()->pagedir, file);
  if (filename == NULL)
    {
      return false;
    }
  return is_user_vaddr (file + strlen (filename)+1)
      && pagedir_get_page (thread_current ()->pagedir, file + strlen (filename)+1) != NULL;
}

/* Executes the command provided by cmd_line if it is valid */
pid_t
exec (const char* cmd_line)
{
  tid_t exec_tid = process_execute (cmd_line);

  if (exec_tid == TID_ERROR || !thread_current()->shared->load_success)
    return -1;
  else
    return exec_tid;
}

int
wait (pid_t pid)
{
  return process_wait(pid);
}

/* Get the `file_data` list element with a matching `fd`
   from the current thread's `file_data_list`. */
struct file_data *
get_file_data_by_fd (int fd)
{
  struct list_elem *e;

  for (e = list_begin (&thread_current ()->file_data_list);
       e != list_end (&thread_current ()->file_data_list);
       e = list_next (e))
    {
      struct file_data *file_data = list_entry (e, struct file_data, elem);

      /* Return the matching file data. */
      if (file_data->fd == fd)
      {
        return file_data;
      }
    }

  /* Return NULL if no matching fd. */
  return NULL;
}

/* Creates a new file called `file` initially `initial_size`
   bytes in size. Returns true if successful, false otherwise. */
bool
create (const char *file, unsigned initial_size)
{
  bool result;
  result = (file == NULL || strlen (file) > 14) ? false : filesys_create (file, initial_size);
  return result;
}

/* Deletes the file called `file`. Returns true if successful,
   false otherwise. */
bool
remove (const char *file)
{
  bool result;
  result = filesys_remove (file);
  return result;
}

/* Opens the file called file. Returns a nonnegative integer fd,
   or -1 if the file could not be opened. */
int
open (const char *file)
{
  struct inode *inode;
  struct file *file_p;
  struct dir *dir_p;
  struct file_data *file_data;
  int fd;

  /* If open failed, return -1. */
  if (file == NULL || (inode = filesys_open_inode (file)) == NULL)
    {
      return -1;
    }

  file_data = malloc (sizeof (struct file_data));
  if (inode_isdir_check(inode))
    {
      if ((dir_p = dir_open (inode)) == NULL)
        return -1;
      file_data->dir_p = dir_p;
      file_data->is_directory = true;
    }
  else
    {
      if ((file_p = file_open (inode)) == NULL)
        return -1;
      file_data->file_name = file;
      file_data->file_p = file_p;
      file_data->is_directory = false;
    }

  /* Assign the lowest available fd, and increment after. */
  fd = available_fd++;

  /* Allocate a new `file_data` element, assign its parameters,
    and push it onto the current thread's `file_data_list`. */
  file_data->fd = fd;
  list_push_back (&thread_current ()->file_data_list, &file_data->elem);

  return fd;
}

/* Returns the size, in bytes, of the file open as `fd`. */
int
filesize (int fd)
{
  struct file_data *file_data;
  int file_size;
  file_data = get_file_data_by_fd (fd);

  /* If no `file_data` match is found, return -1. */
  if (file_data == NULL)
    {
      return -1;
    }

  file_size = file_length (file_data->file_p);
  return file_size;
}

/* Reads `size` bytes from the file open as `fd` into `buffer`.
   Returns the number of bytes actually read, or -1 if the file
   could not be read. */
int
read (int fd, void *buffer, unsigned size)
{
  /* If fd = 0, read from the keyboard. */
  if (fd == STDIN_FILENO)
    {
      uint8_t key;
      key = input_getc ();
      memset (buffer, key, size);
      return 1;
    }

  /* Otherwise, call `file_read` with the corresponding file. */
  struct file_data *file_data;
  int bytes_read;
  file_data = get_file_data_by_fd (fd);

  /* If no `file_data` match is found, return -1. */
  if (file_data == NULL || file_data->is_directory)
    {
      return -1;
    }

  bytes_read = file_read (file_data->file_p, buffer, size);
  return bytes_read;
}

/* Writes `size` bytes from `buffer` to the open file `fd`.
   Returns the number of bytes actually written. */
int
write (int fd, const void *buffer, unsigned size)
{
  /* Write to the console. */
  if (fd == STDOUT_FILENO)
    {
      putbuf(buffer, (size_t) size);
      return size;
    }

  struct file_data *file_data;
  int bytes_written;
  file_data = get_file_data_by_fd (fd);

  /* If no `file_data` match is found OR if it's a directory, return -1. */
  if (file_data == NULL || file_data->is_directory)
    {
      return -1;
    }

  bytes_written = file_write (file_data->file_p, buffer, size);
  return bytes_written;
}

/* Changes the next byte to be read or written in open file `fd`
   to `position`, expressed in bytes from the beginning of the file. */
void
seek (int fd, unsigned position)
{
  struct file_data *file_data;
  file_data = get_file_data_by_fd (fd);

  /* If no `file_data` match is found, do nothing. */
  if (file_data == NULL)
    {
      return;
    }

  file_seek (file_data->file_p, position);
}

/* Returns the position of the next byte to be read or written
   in open file fd, expressed in bytes from the beginning of file. */
unsigned
tell (int fd)
{
  struct file_data *file_data;
  unsigned byte_position;
  file_data = get_file_data_by_fd (fd);

  /* If no `file_data` match is found, exit thread. */
  if (file_data == NULL)
    {
      thread_exit ();
    }

  byte_position = file_tell (file_data->file_p);
  return byte_position;
}

/* Closes file descriptor `fd`. */
void
close (int fd)
{
  struct file_data *file_data;
  file_data = get_file_data_by_fd (fd);

  /* If no `file_data` match is found, exit thread. */
  if (file_data == NULL)
    {
      print_exit_code (-1);
      thread_exit ();
    }

  /* Close the file and remove the corresponding `file_data`
     element from the current thread's `file_data_list`. */
  if (file_data->is_directory)
    dir_close (file_data->dir_p);
  else
    file_close (file_data->file_p);
}

/* Returns the inode number of the inode associated with fd,
 which may represent an ordinary file or a directory. */
int
inumber (int fd) 
{
  struct file_data *file_data;
  file_data = get_file_data_by_fd (fd);
  /* If no `file_data` match is found, exit thread. */
  if (file_data == NULL)
    {
      print_exit_code (-1);
      return -1;
    }

  if (file_data->is_directory)
    return inode_get_inumber (dir_get_inode (file_data->dir_p));  

  /* inode_get_inumber on the inode associated with it */
  return inode_get_inumber(file_get_inode (file_data->file_p));
}

void
print_exit_code (int code)
{
  char *save_ptr;
  char *name = strtok_r ((char *) &thread_current ()->name, " ", &save_ptr);
  printf ("%s: exit(%d)\n", name, code);
}

/* Changes the current working directory of the process to dir,
 which may be relative or absolute. Returns true if successful,
 false on failure. */
bool
chdir (const char *dir)
{
  struct dir* desired_dir = NULL;
  char filename[NAME_MAX + 1];
  bool path_finder_result = path_finder(dir, &desired_dir, filename);

  //assuming positive if it exists
  if (path_finder_result && (strcmp (filename, ".") == 0)) {
    thread_current ()->curr_dir = desired_dir;
  } else {
    return false;
  }

  return true;
}

/* Creates the directory named dir, which may be rel- ative or absolute.
 Returns true if successful, false on failure. */
bool
mkdir (const char *dir)
{
  bool create_result = dir_allocate (dir, 32);
  if (!create_result)
    return false;
  return true;
}

/* Reads a directory entry from file descriptor fd, which must represent a directory.
 If successful, stores the null-terminated file name in name, which must have room
 for READDIR MAX LEN + 1 bytes, and returns true. If no entries are left in the
 directory, returns false. */
bool
readdir (int fd, char *name)
{
  struct file_data *file_data;
  file_data = get_file_data_by_fd (fd);

  /* If no `file_data` match is found, return -1. */
  if (file_data == NULL)
    return false;

  bool result = dir_readdir (file_data->dir_p, name);
  while (result && (strcmp (name, ".") == 0 || strcmp (name, "..") == 0))
    result = dir_readdir (file_data->dir_p, name);

  return result;
}

/* Returns true if fd represents a directory,
  false if it represents an ordinary file. */
bool
isdir (int fd)
{
  struct file_data *file_data;
  file_data = get_file_data_by_fd (fd);

  /* If no `file_data` match is found, return -1. */
  if (file_data == NULL)
    {
      return false;
    }

  return file_data->is_directory;
}

/* Get the cache stats and reset for proj3 additional tests. */ 
int
cache_stats (int stats) 
{
  return get_cache_stats (stats);
}

/* Get block write count for proj3 additional tests. */
unsigned long long
get_num_writes (void)
{
  return get_write_cnt ();
}

static void
syscall_handler (struct intr_frame *f UNUSED)
{
  bool bool_result;
  unsigned unsigned_result;
  int int_result;
  int exit_stat;

  uint32_t *args = ((uint32_t *) f->esp);
  if (!user_mem_access_verification(args, 0))
    {
      print_exit_code (-1);
      thread_exit ();
    }

  /* Call the appropriate handler for the given syscall. */
  switch (args[0])
    {
      case SYS_EXIT:
        if (!user_mem_access_verification(args, 1))
          {
            print_exit_code(-1);
            thread_exit();
          }
        f->eax = args[1];
        thread_current()->shared->exit_status = args[1];
        print_exit_code(args[1]);
        thread_exit();
        break;

      case SYS_WAIT:
        exit_stat = wait(args[1]);
        f->eax = exit_stat;
        break;

      case SYS_EXEC:
        if (!is_valid_filename((char *)args[1]))
          {
            print_exit_code(-1);
            thread_exit();
          }
        tid_t exec_tid;
        exec_tid = exec((char *) args[1]);
        f->eax = exec_tid;
        break;

      case SYS_HALT:
        shutdown_power_off();
        break;

      case SYS_CREATE:
        if (!is_valid_filename((char *)args[1]))
          {
            f->eax = false;
            print_exit_code(-1);
            thread_exit();
          }
        bool_result = create((char *) args[1], args[2]);
        f->eax = bool_result;
        break;

      case SYS_REMOVE:
        if (!is_valid_filename((char *)args[1]))
          {
            f->eax = false;
            print_exit_code(-1);
            thread_exit();
          }
        bool_result = remove((char *) args[1]);
        f->eax = bool_result;
        break;

      case SYS_OPEN:
        if (!is_valid_filename((char *) args[1]))
          {
            f->eax = -1;
            print_exit_code(-1);
            thread_exit();
          }
        int_result = open((char *) args[1]);
        f->eax = int_result;
        break;

      case SYS_FILESIZE:
        int_result = filesize(args[1]);
        f->eax = int_result;
        break;

      case SYS_READ:
        if (!is_valid_buffer(args[2], args[3]))
          {
            f->eax = -1;
            print_exit_code(-1);
            thread_exit();
          }
        int_result = read(args[1], (void *) args[2], args[3]);
        f->eax = int_result;
        break;

      case SYS_WRITE:
        if (!is_valid_buffer(args[2], args[3]))
          {
            f->eax = -1;
            print_exit_code(-1);
            thread_exit();
          }
        int_result = write(args[1], (void *) args[2], args[3]);
        f->eax = int_result;
        break;

      case SYS_SEEK:
        seek(args[1], args[2]);
        break;

      case SYS_TELL:
        unsigned_result = tell(args[1]);
        f->eax = unsigned_result;
        break;

      case SYS_CLOSE:
        close(args[1]);
        break;

      case SYS_PRACTICE:
        f->eax = args[1] + 1;
        break;

      case SYS_MKDIR:
        bool_result = mkdir ((char *) args[1]);
        f->eax = bool_result;
        break;

      case SYS_CHDIR:
        bool_result = chdir ((char *) args[1]);
        f->eax = bool_result;
        break;

      case SYS_READDIR:
        bool_result = readdir ((int) args[1], (char *) args[2]);
        f->eax = bool_result;
        break;

      case SYS_ISDIR:
        int_result = isdir ((int) args[1]);
        f->eax = int_result;
        break;

      case SYS_INUMBER:
        int_result = inumber ((int) args[1]);
        f->eax = int_result;
        break;

      case CACHE_STATS:
        unsigned_result = cache_stats(args[1]);
        f->eax = unsigned_result;
        break;

      case SYS_WRTCNT:
        f->eax = get_num_writes ();
        break;

      default:
          thread_exit ();
    }
}
