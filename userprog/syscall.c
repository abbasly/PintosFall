#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"

/* 추가해준 헤더 파일들 */
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

/* syscall functions */
void halt (void);
void exit (int status);
bool create(const char *file, unsigned initial_size);
bool remove(const char *file);
int open(const char *file);
int filesize(int fd);
int read(int fd, void *buffer, unsigned size);
int write(int fd, const void *buffer, unsigned size);
int _write (int fd UNUSED, const void *buffer, unsigned size);
void seek(int fd, unsigned position);
unsigned tell(int fd);
void close(int fd);
tid_t fork (const char *thread_name);
int exec (const char *file_name);
int dup2(int oldfd, int newfd);

/* syscall helper functions */
void check_address(const uint64_t*);
static struct file *process_get_file(int fd);
int process_add_file(struct file *file);
void process_close_file(int fd);

/* Project2-extra */
const int STDIN = 1;
const int STDOUT = 2;

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
   /* LOCK INIT 추가*/
   lock_init(&filesys_lock);
}

/* helper functions letsgo ! */
void check_address(const uint64_t *uaddr){
	struct thread *cur = thread_current();
#ifndef VM
    /* is_user_vaddr : 포인터가 가리키는 주소가 유저영역의 주소인지 확인
     * pml4_get_page : 유저 가상 주소에 연결된 물리 주소를 반환하는 함수. 
                       만약 포인터가 가리키는 주소가 매핑되지 않은 영역이면 NULL을 반환함 */
   /* 잘못된 접근인 경우, 프로세스 종료 */
	if (uaddr == NULL || !(is_user_vaddr(uaddr)) || pml4_get_page(cur->pml4, uaddr) == NULL)
	{
		exit(-1);
	}
#else
	if (uaddr == NULL || !(is_user_vaddr(uaddr)) || spt_find_page(&cur->spt, uaddr) == NULL)
	{
		exit(-1);
	}
#endif
}


/* 현재 쓰레드의 FDT테이블에서 첫번째 빈공간을 찾아 파일 객체를 추가해주는 함수 */
int process_add_file(struct file *f){
   struct thread *curr = thread_current(); 
   struct file **curr_fd_table = curr->file_descriptor_table;
   for (int idx = curr->fdidx; idx < FDCOUNT_LIMIT; idx++){ // 현재 fdidx의 위치부터 FDCOUNT_LI 
      if(curr_fd_table[idx] == NULL){
         curr_fd_table[idx] = f;
         curr->fdidx = idx; // fd의 최대값 + 1
         return curr->fdidx;
      }
   }
   curr->fdidx = FDCOUNT_LIMIT;
   return -1;
}

struct file *process_get_file (int fd){
   if (fd < 0 || fd >= FDCOUNT_LIMIT)
      return NULL;
   struct file *f = thread_current()->file_descriptor_table[fd];
   return f;
}

/* revove the file(corresponding to fd) from the FDT of current process */
void process_close_file(int fd){
   if (fd < 0 || fd > FDCOUNT_LIMIT)
      return NULL;
   thread_current()->file_descriptor_table[fd] = NULL;
}

/* helper functions gooooooooooood job */

/* The main system call interface */
void
syscall_handler (struct intr_frame *f UNUSED) {
   // TODO: Your implementation goes here.
   int syscall_num = f->R.rax; // rax: system call number
   switch(syscall_num){
      case SYS_HALT:                   /* Halt the operating system. */
         halt();
         break;
      case SYS_EXIT:                   /* Terminate this process. */
         exit(f->R.rdi);
         break;    
      case SYS_FORK:   ;                /* Clone current process. */
         // 유저프로그램의 실행 정보를 intr_frame에 저장해줌
         // 시스템콜 핸들러로 넘어온 f에 저장되어 있음
         struct thread *curr = thread_current();                 
         memcpy(&curr->parent_if, f, sizeof(struct intr_frame));
         f->R.rax = fork(f->R.rdi);
         break;
      case SYS_EXEC:                   /* Switch current process. */
         if (exec(f->R.rdi) == -1)
            exit(-1);
         break;
      case SYS_WAIT:                   /* Wait for a child process to die. */
         f->R.rax = wait(f->R.rdi);
         break;
      case SYS_CREATE:                 /* Create a file. */
         f->R.rax = create(f->R.rdi, f->R.rsi);
         break;
      case SYS_REMOVE:                 /* Delete a file. */
         f->R.rax = remove(f->R.rdi);
         break;
      case SYS_OPEN:                   /* Open a file. */
         f->R.rax = open(f->R.rdi);
         break;
      case SYS_FILESIZE:               /* Obtain a file's size. */
         f->R.rax = filesize(f->R.rdi);
         break;
      case SYS_READ:                   /* Read from a file. */
         f->R.rax = read(f->R.rdi, f->R.rsi, f->R.rdx);
         break;
      case SYS_WRITE:                  /* Write to a file. */
         f->R.rax = write(f->R.rdi, f->R.rsi, f->R.rdx);
         break;
      case SYS_SEEK:                   /* Change position in a file. */
         seek(f->R.rdi, f->R.rsi);
         break;
      case SYS_TELL:                   /* Report current position in a file. */
         f->R.rax = tell(f->R.rdi);
         break;
      case SYS_CLOSE:                /* Close a file. */
         close(f->R.rdi);
         break;
      case SYS_DUP2:
         f->R.rax = dup2(f->R.rdi, f->R.rsi);
         break;
      default:                   /* call thread_exit() ? */
         exit(-1);
         break;
   }
   // printf ("system call!\n");
   // thread_exit ();
}


/* Pintos를 종료시키는 시스템 콜*/
void halt(void){
   power_off(); // init.c의 power_off 활용
}

/* 현재 실행중인 프로세스를 종료시키는 시스템 콜 */
void exit(int status){
   struct thread *curr = thread_current(); // 실행 중인 스레드 구조체 가져오기
   curr->exit_status = status;
   /* status == 0 : 정상 종료 */
   printf("%s: exit(%d)\n", thread_name(), status); // 프로그램이 정상적으로 종료되었는지 확인.
   thread_exit(); // 스레드 종료
}

/* Clone current process. */
tid_t fork (const char *thread_name){
   /* create new process, which is the clone of current process with the name THREAD_NAME*/
   // 커널영역에서 실행중
   struct thread *curr = thread_current(); // 부모 쓰레드
   return process_fork(thread_name, &curr->parent_if);
   /* must return pid of the child process */
}

int exec (const char *file){
   check_address(file);
   int size = strlen(file) + 1; // 마지막 null 문자를 포함해서 + 1
   char *fn_copy = palloc_get_page(PAL_ZERO); // page 할당을 하는데, 페이지 전체를 0으로 초기화
   
   if(fn_copy==NULL)
      exit(-1);

   strlcpy(fn_copy, file, size);
   if (process_exec(fn_copy) == -1)
      return -1;

   NOT_REACHED();
   return 0;
}

/* Wait for a child process to die. */
int wait(tid_t pid){
   process_wait(pid);
}

 /* 파일을 생성하는 시스템 콜 */
bool create(const char *file, unsigned initial_size){
   check_address(file); // 포인터가 가리키는 주소가 유저영역의 주소인지 확인
   return filesys_create(file, initial_size); // 파일 이름 & 크기에 해당하는 파일 생성
}

 /* Delete a file. */
bool remove(const char *file){
   check_address(file); // 포인터가 가리키는 주소가 유저영역의 주소인지 확인
   return filesys_remove(file); // 파일 이름에 해당하는 파일을 제거
}
/* 파일을 열 때 사용하는 시스템콜*/
int open (const char *file){
   /* 인자로 들어오는 file = 파일의 이름 및 경로 정보 */

   check_address(file);
   lock_acquire(&filesys_lock); // 파일을 접근하는 동안 다른 곳에서 쓰면 안되므로 lock
   struct file *f = filesys_open(file); // 열고자 하는 파일의 객체 정보를 받아오기
   if (f == NULL)
      return -1;
   int fd = process_add_file(f); // 파일 객체를 가리키는 포인터를 FDT에 추가하고, FDT내의 해당 파일이 위치한 fdidx를 리턴
   if (fd == -1)
      file_close(f);
   lock_release(&filesys_lock);
   return fd; // 추가된 파일 객체의 fd 반환
}
/* 파일의 크기를 알려주는 시스템콜 */
int filesize (int fd){
   struct file *f = process_get_file(fd); // fd를 이용해서 파일 객체 검색
   if (f == NULL) return -1;
   return file_length(f);
}
/* 해당 파일로부터 값을 읽고, 버퍼에 넣는 시스템콜 */
int read (int fd, void *buffer, unsigned size){
   check_address(buffer);
   unsigned char *buf = buffer;
   int readsize;
   struct thread *curr = thread_current();

   struct file *f = process_get_file(fd);

   if (f == NULL) return -1;
   // if (fd < 0 || fd>= FDCOUNT_LIMIT) return NULL;
   if (f == STDOUT) return -1;
   
   if (f == STDIN){
      if (curr->stdin_count == 0){
         NOT_REACHED();
         process_close_file(fd);
         readsize = -1;
      }
      else {
         // fd가 0일 경우 키보드 입력을 받아온다
         for (readsize = 0; readsize < size; readsize++){
            char c = input_getc();
            *buf++ = c;
            if (c == '\0')
               break;
         }
      }
   }
   else{
      lock_acquire(&filesys_lock); // 파일에 동시접근 일어날 수 있으므로 lock 사용
      readsize = file_read(f, buffer, size);
      lock_release(&filesys_lock);
   }
   return readsize;
}

/* 데이터를 기록하는 시스템 콜 */
int write (int fd, const void *buffer, unsigned size){
   check_address(buffer);
   struct file *f = process_get_file(fd);
   int writesize;
   struct thread *cur = thread_current();

   if (f == NULL) return -1;
   if (f == STDIN) return -1;

   if (f == STDOUT){
      if (cur->stdout_count == 0) {
         NOT_REACHED();
         process_close_file(fd);
         writesize = -1;
      }
      else{
         putbuf(buffer, size);// buffer에 들은 size만큼을, 한 번의 호출로 작성해준다.
         writesize = size;
      }
   }
   else{
      lock_acquire(&filesys_lock); // 파일에 동시접근 일어날 수 있으므로 lock 사용
      writesize = file_write(f, buffer, size);
      lock_release(&filesys_lock);
   }
   return writesize;
}

/* 파일의 pos를 변경해주는 시스템콜 */
void seek (int fd, unsigned position){
   struct file *f = process_get_file(fd); // fdt에서 파일 객체 찾아오기
   if (f > 2)
      file_seek(f, position);
}
/* 해당 파일의 pos를 반환해주는 시스템콜 */
unsigned tell (int fd){
   struct file *f = process_get_file(fd);
   if (fd < 2)
      return;
   return file_tell(f);
}

/* 열린 파일을 닫는 시스템콜 */
void close (int fd){
	// if(fd < 2) return;
	struct file *f = process_get_file(fd);

	if(f == NULL)
		return;
	struct thread *curr = thread_current();

	if(fd==0 || f==STDIN)
		curr->stdin_count--;
	else if(fd==1 || f==STDOUT)
		curr->stdout_count--;

	process_close_file(fd);
	if(fd <= 1 || f <= 2){
		return;
	}

	if(f->dup_count == 0){
		file_close(f);
	}
	else{
		f->dup_count--;
	}
}

/* 식별자 테이블 엔트리의 이전 내용을 덮어써서 식별자 테이블 엔트리 oldfd를 newfd로 복사 */
int dup2(int oldfd, int newfd)
{
	struct file *file_fd = process_get_file(oldfd);

	if (file_fd == NULL) {
		return -1;
	} 

	if (oldfd == newfd) { // oldfd == newfd라면 복제하지 않고 newfd 리턴
		return newfd; 
	}
	
	struct thread *cur = thread_current();
	struct file **fdt = cur->file_descriptor_table;

	if (file_fd == STDIN) {
      cur->stdin_count++;
   }
   else if (file_fd == STDOUT) {
      cur->stdout_count++;
   }
   else {
      file_fd->dup_count++;
   }
   
   close(newfd);
   fdt[newfd] = file_fd;
   return newfd;
}