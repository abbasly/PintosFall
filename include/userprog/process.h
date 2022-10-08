#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"
#include "vm/vm.h"

tid_t process_create_initd (const char *file_name);
tid_t process_fork (const char *name, struct intr_frame *if_);
int process_exec (void *f_name);
int process_wait (tid_t);
void process_exit (void);
void process_activate (struct thread *next);
/* project2 : command argument parsing */
// void argument_stack(char **argv, int argc, struct intr_frame *_if);

// static bool load (const char *file_name, struct intr_frame *if_);
// static void __do_fork (void *);
static bool install_page (void *upage, void *kpage, bool writable);
struct thread * get_child (int pid);
struct segment {
    struct file *file;
    off_t offset;           
    size_t page_read_bytes;
    size_t page_zero_bytes;
};

#endif /* userprog/process.h */