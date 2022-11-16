#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"
#include "userprog/process.h"
#include "threads/vaddr.h"
#include "userprog/process.h"
#include "threads/synch.h"
#include "include/vm/vm.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#include <list.h>
#include "threads/palloc.h"
#include "include/vm/vm.h"


void syscall_entry (void);
void syscall_handler (struct intr_frame *);

static struct file *fd_to_file(int fd);
void valid_adress(uaddr);
void halt(void);
void exit(int status);
bool create(const char *file, unsigned initial_size);
bool remove(const char *file);
int open(const char *file);
int filesize(int fd);
int read(int fd, void *buffer, unsigned size);
int write(int fd, const void *buffer, unsigned size);
void seek(int fd, unsigned position);
unsigned tell(int fd);
void close(int fd);

/* System call.
 *
 * Previously system call services was handled by the interrupt handler
 * (e.g. int 0x80 in linux). However, in x86-64, the manufacturer supplies
 * efficient path for requesting the system call, the `syscall` instruction.
 *
 * The syscall instruction works by reading the values from the the Model
 * Specific Register (MSR). For the details, see the manual. */

#define MSR_STAR 0xc0000081         /* Segment selector msr */
#define MSR_LSTAR 0xc0000082        /* Long mode SYSCALL target */
#define MSR_SYSCALL_MASK 0xc0000084 /* Mask for the eflags */

void exit(int status)
{
    thread_current()->exit_status = status;
    printf("%s: exit(%d)\n", thread_name(), status);
    thread_exit();
}


void valid_adress(void *addr)
{

	#ifndef VM
	if (addr == NULL || !(is_user_vaddr(addr)) || !pml4_get_page(thread_current()->pml4, addr))
	{
		exit(-1);
	}
	#else
	if (addr == NULL || !(is_user_vaddr(addr))){
		exit(-1);
	}
	#endif
}

static struct file *fd_to_file(int fd)
{
    // wrong file descriptor
    if(fd<0) return NULL;
    if(fd >= FD_LIMIT) return NULL;

    return thread_current()->file_desc_table[fd];
}

int add_file_to_fdt(struct file *file)
{
    struct thread *curr = thread_current();
    struct file **fd_table = curr->file_desc_table;


    while (fd_table[curr->fd_idx] && curr->fd_idx < FD_LIMIT)
    {
        curr->fd_idx = curr->fd_idx+1;
    }

    if (curr->fd_idx < FD_LIMIT){} else return -1;

    fd_table[curr->fd_idx] = file;
    return curr->fd_idx;
}

void remove_file_from_fdt(int fd)
{
    // wrong file descriptor
    if(fd<0) return NULL;
    if(fd >= FD_LIMIT) return NULL;

    thread_current()->file_desc_table[fd] = NULL;
}


int open(const char *file)
{
    valid_adress(file);
    struct file *open_file = filesys_open(file);

    if(open_file !=NULL){
        // put the file to fd table
        int fd = add_file_to_fdt(open_file);
        
        // if the fd table is full
        if (fd == -1)
        {
            file_close(open_file);
        }
    return fd;} else return -1;
}

int filesize(int fd)
{
    struct file *open_file = fd_to_file(fd);
    if (open_file != NULL){
        return file_length(open_file);
    }
    return -1;
}

int read(int fd, void *buffer, unsigned size)
{
    valid_adress(buffer);
    struct thread *cur = thread_current();

	#ifdef VM
		/* project3 - writable이 true가 아닌데, buffer에 쓰려고하면 안됨. */
		struct page* p = spt_find_page(&cur->spt, buffer);
		if (p && !p->writable) exit(-1);
	#endif
    off_t bytes_read;
    uint8_t *read_buffer = buffer;
    char key;
    struct file *read_file = fd_to_file(fd);
    switch (fd)
    {
    case 1:
        return -1;
        break;
    case 0:
        for (bytes_read = 0; bytes_read < size; bytes_read++)
        {
            key = input_getc();
            *read_buffer++ = key;
            if (key == '\0')
            {
                break;
            }
        }
    default:
        if (read_file == NULL)
        {
            return -1;
        }
        lock_acquire(&file_lock);
        bytes_read = file_read(read_file, buffer, size);
        lock_release(&file_lock);
        break;
    }
    return bytes_read;
}

void seek(int fd, unsigned pos)
{
    struct file *file_sk = fd_to_file(fd);
    // 0,1, and 2 are already defined
    if (file_sk < 3)
    return;
    file_sk->pos = pos;
}

unsigned tell(int fd)
{
    struct file *tell_file = fd_to_file(fd);
    if (tell_file >= 3){
        return file_tell(tell_file);
    }
    return;
}

void close(int fd)
{
    struct file *file = fd_to_file(fd);
    if (file != NULL){
        remove_file_from_fdt(fd);
    }
    return;
}

int write (int fd, const void *buffer, unsigned size){
    valid_adress(buffer);
    struct thread *curr = thread_current();
    struct file *file = fd_to_file(fd);
    int write_size;

    if (file == NULL) return -1;
    if (file == 1) return -1; // stdin

    if (file == 2){ // stdout
        if(curr->stdout_count != 0){
            putbuf(buffer, size);
            write_size = size;
        }
        else{
            NOT_REACHED();
            close(fd);
            write_size = -1;
        }
   }
   else{
      lock_acquire(&file_lock);
      write_size = file_write(file, buffer, size);
      lock_release(&file_lock);
   }
   return write_size;
}


void halt(void)
{
    power_off();
}

bool create(const char *file, unsigned init_size)
{
    valid_adress(file);
    bool ret = filesys_create(file, init_size);
    return ret;
}

bool remove(const char *file)
{
    valid_adress(file);
    bool ret = filesys_remove(file);
    return ret;
}

int exec(char *file)
{
    valid_adress(file);
    int file_size = strlen(file) + 1;
    char *fn_copy = palloc_get_page(PAL_ZERO);
    if (fn_copy != NULL){
        strlcpy(fn_copy, file, file_size);
        if (process_exec(fn_copy) != -1){
            NOT_REACHED();
            return 0;
                }
        return -1;
    }
    exit(-1);
}

tid_t fork(const char *thread_name, struct intr_frame *f)
{
    return process_fork(thread_name, f);
}


void
syscall_init (void) {
	write_msr(MSR_STAR, ((uint64_t)SEL_UCSEG - 0x10) << 48  |
			((uint64_t)SEL_KCSEG) << 32);
	write_msr(MSR_LSTAR, (uint64_t) syscall_entry);

	/* The interrupt service rountine should not serve any interrupts
	 * until the syscall_entry swaps the userland stack to the kernel
	 * mode stack. Therefore, we masked the FLAG_FL. */
	write_msr(MSR_SYSCALL_MASK,
			FLAG_IF | FLAG_TF | FLAG_DF | FLAG_IOPL | FLAG_AC | FLAG_NT);
	lock_init(&file_lock);
}

void syscall_handler(struct intr_frame *f UNUSED)
{
    struct thread*t = thread_current();
	t->rsp = f->rsp;
    switch (f->R.rax) // system call number
    {
    case SYS_HALT:
        halt();
        break;
    case SYS_EXIT:
        exit(f->R.rdi);
        break;
    case SYS_FILESIZE:
        f->R.rax = filesize(f->R.rdi);
        break;
    case SYS_READ:
        f->R.rax = read(f->R.rdi, f->R.rsi, f->R.rdx);
        break;
    case SYS_WRITE:
        f->R.rax = write(f->R.rdi, f->R.rsi, f->R.rdx);
        break;
    case SYS_FORK:
        f->R.rax = fork(f->R.rdi, f);
        break;
    case SYS_EXEC:
        if (exec(f->R.rdi) == -1)
        {
            exit(-1);
        }
        break;
    case SYS_CLOSE:
        close(f->R.rdi);
        break;
    case SYS_WAIT:
        f->R.rax = process_wait(f->R.rdi);
        break;
    case SYS_REMOVE:
        f->R.rax = remove(f->R.rdi);
        break;
    case SYS_OPEN:
        f->R.rax = open(f->R.rdi);
        break;
    case SYS_CREATE:
        f->R.rax = create(f->R.rdi, f->R.rsi);
        break;
    case SYS_SEEK:
        seek(f->R.rdi, f->R.rsi);
        break;
    case SYS_TELL:
        f->R.rax = tell(f->R.rdi);
        break;
    default:
        exit(-1);
        break;
    }
}