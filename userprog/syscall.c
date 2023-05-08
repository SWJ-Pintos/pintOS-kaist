#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"
#include "threads/synch.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "userprog/process.h"
#include "console.h"
#include "threads/palloc.h"

typedef int pid_t;
#define PID_ERROR ((pid_t) -1)

struct lock filesys_lock;

void syscall_entry (void);
void syscall_handler (struct intr_frame *);

void halt (void);
void exit (int status);
tid_t fork(const char *thread_name, struct intr_frame *f);
int exec (char *file_name);
int wait (pid_t pid);
bool create(char *file , unsigned initial_size);
bool remove(char *file);
int open (const char *file);
int filesize (int fd);
int read (int fd, void *buffer, unsigned size);
int write (int fd, const void *buffer, unsigned size);
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

void
syscall_init (void) {
	write_msr(MSR_STAR, ((uint64_t)SEL_UCSEG - 0x10) << 48  |
			((uint64_t)SEL_KCSEG) << 32);
	write_msr(MSR_LSTAR, (uint64_t) syscall_entry);
	
	// project 2 for read(), write()
	lock_init(&filesys_lock);

	/* The interrupt service rountine should not serve any interrupts
	 * until the syscall_entry swaps the userland stack to the kernel
	 * mode stack. Therefore, we masked the FLAG_FL. */
	write_msr(MSR_SYSCALL_MASK,
			FLAG_IF | FLAG_TF | FLAG_DF | FLAG_IOPL | FLAG_AC | FLAG_NT);
}

/* The main system call interface */
void
syscall_handler (struct intr_frame *f UNUSED) {
	// TODO: Your implementation goes here.
	int number = f->R.rax;
	// uintptr_t stack_pointer = f->rsp;
	// check_address(stack_pointer);
	// 리턴값이 없는 함수는 rax 에 바뀐 내용을 넣어서,함수를 참조한다.

	switch(number) {
		case SYS_HALT:
			halt();
			break;
		case SYS_EXIT:
			exit(f->R.rdi);
			break;
		case SYS_FORK:
			f->R.rax = fork(f->R.rdi, f);
			break;
		case SYS_EXEC:
			f->R.rax = exec(f->R.rdi);
			break;
		case SYS_WAIT:
			f->R.rax = wait(f->R.rdi);
			break;
		case SYS_CREATE:
			f->R.rax = create(f->R.rdi, f->R.rsi);
			break;
		case SYS_REMOVE:
			f->R.rax = remove(f->R.rdi);
			break;
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
			printf ("system call!\n");
			thread_exit();
			// break;		
	}
	// printf ("system call!\n");
	// thread_exit ();
}
void
check_address (void *addr) {
	struct thread *cur = thread_current();
	if (addr == NULL || !(is_user_vaddr(addr)) || pml4_get_page(cur->pml4, addr) == NULL) {
		exit(-1);
	}
}

void 
halt(void) {
	power_off();
}

void 
exit(int status) {
	struct thread *th = thread_current();
	th->exit_status = status;
	printf("%s: exit(%d)\n", thread_name(), status);

	thread_exit();
	// return status;
}

tid_t fork(const char *thread_name, struct intr_frame *f) {
	return process_fork(thread_name, f);
}

// exec 보류
// int 
// exec(const char *cmd_line) {
// 	check_address(cmd_line);

// 	// int file_size = strlen(cmd_line)+1;
// 	char *fn_copy = palloc_get_page(PAL_ZERO);
// 	if (fn_copy == NULL) {
// 		exit(-1);
// 	}
// 	strlcpy(fn_copy, cmd_line, PGSIZE);

// 	if (process_exec(fn_copy) == -1) {
// 		exit(-1);
// 	}

// 	NOT_REACHED();
// 	return 0;
// }

int exec (char *file_name) {
	check_address(file_name);
	int file_size = strlen(file_name)+1;
	char *fn_copy = palloc_get_page(PAL_ZERO);
	if (fn_copy==NULL) {
		exit(-1);
	}
	strlcpy(fn_copy, file_name, file_size);
	if (process_exec(fn_copy)==-1) {
		return -1;
	}
	NOT_REACHED();
	return 0;
} 

int wait (pid_t pid) {
	return process_wait(pid);
}

bool 
create(char *file , unsigned initial_size) {	
	check_address(file);
	// if (file == NULL) {
	// 	exit(-1);
	// }
	return filesys_create(file, initial_size);	
}

bool 
remove(char *file) {
	check_address(file);
	// if (file == NULL) {
	// 	exit(-1);
	// }
	return filesys_remove(file);	
}

// // 현재 프로세스의 fd테이블에 파일 추가
// int add_file_to_fdt(struct file *file) {
// 	struct thread *cur = thread_current();
// 	struct file **fdt = cur->fdt;

// 	// fd의 위치가 제한 범위를 넘지 않고, fdtable의 인덱스 위치와 일치한다면
// 	while (cur->fd_idx < FDCOUNT_LIMIT && fdt[cur->fd_idx]) {
// 		cur->fd_idx++;
// 	}

// 	// fdt이 가득 찼다면
// 	if (cur->fd_idx >= FDCOUNT_LIMIT)
// 		return -1;

// 	fdt[cur->fd_idx] = file;
// 	return cur->fd_idx;
// }

int
open (const char *file) {
	check_address(file);
	if (file == NULL) {
		exit(-1);
	}
	struct file *opened_file = filesys_open(file);
	if (!opened_file){
		return -1;
	}

	int fd = process_add_file(opened_file);

	// if (fd == -1){
	// 	file_close(opened_file);
	// }
	return fd;
}

int
filesize (int fd) {
	struct file *length_file = process_get_file(fd);
	if (length_file == NULL) {
		return -1;
	}
	return file_length(length_file);
}

int
read (int fd, void *buffer, unsigned size) {
	check_address(buffer);
	lock_acquire(&filesys_lock);
	off_t read_size = 0;
	char *read_buffer = (char *)buffer;

	if (fd == 0) {
		while (read_size < size) {
			read_buffer[read_size] = input_getc();
			if (read_buffer[read_size] == '\n'){
				break;
			}
		}
		read_buffer[read_size] = '\0';
		lock_release(&filesys_lock);
		return read_size;
	}

	struct file *read_file = process_get_file(fd);
	if (read_file == NULL) {
        lock_release(&filesys_lock);
        return -1;
    }
	read_size = file_read(read_file, buffer, size);
	lock_release(&filesys_lock);
	return read_size;
}

// int read(int fd, void *buffer, unsigned size) {
// 	check_address(buffer);

// 	int read_result;
// 	char *ptr = (char *)buffer;
// 	// struct thread *cur = thread_current();
// 	struct file *file_fd = process_get_file(fd);

// 	if (fd == 0) {
// 		// read_result = i;
// 		for (int i = 0; i < size; i++)
// 		{
// 			char ch = input_getc();
// 			if (ch == '\n')
// 				break;
// 			*ptr = ch;
// 			ptr++;
// 			read_result++;
// 		}
// 	}
// 	else {
// 		if (file_fd == NULL) {
// 			return -1;
// 		}
// 		else {
// 			lock_acquire(&filesys_lock);
// 			read_result = file_read(file_fd, buffer, size);
// 			lock_release(&filesys_lock);
// 		}
// 	}
// 	return read_result;
// }

int
write (int fd, const void *buffer, unsigned size) {
	check_address(buffer);
	lock_acquire(&filesys_lock);
	int write_result;

	if (fd == 1) {
		putbuf(buffer, size);
		write_result = size;
		// write_result = sizeof((char *)buffer);
	}
	else {
		struct file *write_file = process_get_file(fd);
		// if (write_file != NULL) {
		write_result = file_write(write_file, buffer, size);
	}
	lock_release(&filesys_lock);
	return write_result;
}

void
seek (int fd, unsigned position) {
	struct file *seek_file = process_get_file(fd);
	if (seek_file <= 2){
		return ;
	}
	// seek_file->pos = position;
	file_seek(seek_file, position);
}

unsigned
tell (int fd) {
	struct file *tell_file = process_get_file(fd);
	if (tell_file <= 2){
		return ;
	}
	return file_tell(tell_file);
}

void
close (int fd) {
	// if (fd < 2)
	// 	return;
	struct file *file = process_get_file(fd);
	if (file == NULL)
		return;
	process_close_file(fd);

	file_close(file);
}