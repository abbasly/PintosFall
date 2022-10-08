// #ifndef USERPROG_SYSCALL_H
// #define USERPROG_SYSCALL_H

// #include "userprog/process.h"

// void syscall_init (void);
// /* project2 : system call */
// void check_address(void *addr);
// // struct lock filesys_lock;
// void close (int fd);

// #endif /* userprog/syscall.h */
#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#include "threads/synch.h"

void syscall_init (void);

struct lock filesys_lock;

#endif /* userprog/syscall.h */