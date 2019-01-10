#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H
#include <stdbool.h>

typedef int pid_t;
#define PID_ERROR ((pid_t) -1)

void syscall_init (void);

void exit(int status);

pid_t exec(const char* cmd_line);

int wait(pid_t pid);

void syscall_init (void);

/* Helper methods for file data. */
struct file_data *get_file_data_by_fd (int fd);
void remove_file_data_by_name (const char *file);

/* File System Calls. */
bool create (const char *file, unsigned initial_size);
bool remove (const char *file);
int open (const char *file);
int filesize (int fd);
int read (int fd, void *buffer, unsigned size);
int write (int fd, const void *buffer, unsigned size);
void seek (int fd, unsigned position);
unsigned tell (int fd);
void close (int fd);

bool chdir (const char *dir);
bool mkdir (const char *dir);
bool readdir (int fd, char *name);
bool isdir (int fd);
int inumber (int fd);

int cache_stats (int stats);
unsigned long long get_num_writes (void);
#endif /* userprog/syscall.h */
