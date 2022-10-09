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

#include "filesys/filesys.h"
#include "filesys/file.h"
#include <list.h>
#include "threads/palloc.h"
#include "threads/vaddr.h"
#include "userprog/process.h"
#include "threads/synch.h"
#include "include/vm/vm.h"

void syscall_entry (void);
void syscall_handler (struct intr_frame *);

const int STDIN = 1;
const int STDOUT = 2; // extra?

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
    struct thread *cur = thread_current();
    cur->exit_status = status;
    printf("%s: exit(%d)\n", thread_name(), status);
    thread_exit();
}


void check_address(void *addr)
{
    struct thread *cur = thread_current();
    if (is_kernel_vaddr(addr) || pml4_get_page(cur->pml4, addr) == NULL)
    {
        exit(-1);
    }
}

static struct file *find_file_by_fd(int fd)
{
    struct thread *cur = thread_current();
    if (fd < 0 || fd >= FDCOUNT_LIMIT)
    {
        return NULL;
    }
    return cur->fd_table[fd];
}

int add_file_to_fdt(struct file *file)
{
    struct thread *cur = thread_current();
    struct file **fdt = cur->fd_table;


    while (cur->fd_idx < FDCOUNT_LIMIT && fdt[cur->fd_idx])
    {
        cur->fd_idx++;
    }

    if (cur->fd_idx >= FDCOUNT_LIMIT)
        return -1;

    fdt[cur->fd_idx] = file;
    return cur->fd_idx;
}

void remove_file_from_fdt(int fd)
{
    struct thread *cur = thread_current();

    // error : invalid fd
    if (fd < 0 || fd >= FDCOUNT_LIMIT)
        return;

    cur->fd_table[fd] = NULL;
}


int open(const char *file)
{
    check_address(file);
    struct file *open_file = filesys_open(file);

    if (open_file == NULL)
    {
        return -1;
    }
    // fd table에 file추가
    int fd = add_file_to_fdt(open_file);

    // fd table 가득 찼을경우
    if (fd == -1)
    {
        file_close(open_file);
    }
    return fd;
}

int filesize(int fd)
{
    struct file *open_file = find_file_by_fd(fd);
    if (open_file == NULL)
    {
        return -1;
    }
    return file_length(open_file);
}

int read(int fd, void *buffer, unsigned size)
{
    check_address(buffer);
    off_t read_byte;
    uint8_t *read_buffer = buffer;
    if (fd == 0)
    {
        char key;
        for (read_byte = 0; read_byte < size; read_byte++)
        {
            key = input_getc();
            *read_buffer++ = key;
            if (key == '\0')
            {
                break;
            }
        }
    }
    else if (fd == 1)
    {
        return -1;
    }
    else
    {
        struct file *read_file = find_file_by_fd(fd);
        if (read_file == NULL)
        {
            return -1;
        }
        lock_acquire(&filesys_lock);
        read_byte = file_read(read_file, buffer, size);
        lock_release(&filesys_lock);
    }
    return read_byte;
}

void seek(int fd, unsigned position)
{
    struct file *seek_file = find_file_by_fd(fd);
    // 0,1,2는 이미 정의되어 있음
    if (seek_file <= 2)
    {
        return;
    }
    seek_file->pos = position;
}

unsigned tell(int fd)
{
    struct file *tell_file = find_file_by_fd(fd);
    if (tell_file <= 2)
    {
        return;
    }
    return file_tell(tell_file);
}
void close(int fd)
{
    struct file *fileobj = find_file_by_fd(fd);
    if (fileobj == NULL)
    {
        return;
    }

    remove_file_from_fdt(fd);
}

int write (int fd, const void *buffer, unsigned size){
   check_address(buffer);
   struct file *f = find_file_by_fd(fd);
   int writesize;
   struct thread *cur = thread_current();

   if (f == NULL) return -1;
   if (f == STDIN) return -1;

   if (f == STDOUT){
      if (cur->stdout_count == 0) {
         NOT_REACHED();
         process_close_file(fd); ///// TODOOO
         writesize = -1;
      }
      else{
         putbuf(buffer, size);
         writesize = size;
      }
   }
   else{
      lock_acquire(&filesys_lock);
      writesize = file_write(f, buffer, size);
      lock_release(&filesys_lock);
   }
   return writesize;
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
	lock_init(&filesys_lock);
}

void syscall_handler(struct intr_frame *f UNUSED)
{
    switch (f->R.rax)
    {
    case SYS_OPEN:
        f->R.rax = open(f->R.rdi);
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
    case SYS_SEEK:
        seek(f->R.rdi, f->R.rsi);
        break;
    case SYS_TELL:
        f->R.rax = tell(f->R.rdi);
        break;
    case SYS_CLOSE:
        close(f->R.rdi);
        break;
    default:
        exit(-1);
        break;
    }
}