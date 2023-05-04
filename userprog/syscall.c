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
struct lock filesys_lock;

void syscall_entry (void);
void syscall_handler (struct intr_frame *);
bool create(char *file , unsigned initial_size);
bool remove(char *file);
unsigned tell (int fd);
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
syscall_handler (struct intr_frame *f ) {
	// TODO: Your implementation goes here.
	int number = f->R.rax;
	uintptr_t stack_pointer = f->rsp;
	check_address(stack_pointer);
	// 리턴값이 없는 함수는 rax 에 바뀐 내용을 넣어서,함수를 참조한다.

	switch(number) {
		case SYS_HALT:
			halt();
			break;
		case SYS_EXIT:
			exit(f->R.rdi);
			break;
		case SYS_EXEC:
			// exec(f->R.rdi);
			break;
		case SYS_WAIT:
			// wait();
			break;
		case SYS_CREATE:
			create(f->R.rdi, f->R.rsi);
			break;
		case SYS_REMOVE:
			remove(f->R.rdi);
			break;
		case SYS_OPEN:
			open(f->R.rdi);
			break;
		case SYS_FILESIZE:
			filesize(f->R.rdi);
			break;
		case SYS_READ:
			read(f->R.rdi, f->R.rsi, f->R.rdx);
			break;
		case SYS_WRITE:
			write(f->R.rdi, f->R.rsi, f->R.rdx);
			break;
		case SYS_SEEK:
			seek(f->R.rdi, f->R.rsi);
			break;
		case SYS_TELL:
			tell(f->R.rdi);
			break;
		case SYS_CLOSE:
			close(f->R.rdi);
			break;
		default:
			thread_exit ();		
	}
	// printf ("system call!\n");
	// thread_exit ();
}
void
check_address (void *addr) {
	if (is_kernel_vaddr(addr)) {
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
	printf("%s: exit(%d)\n", th->name, th->exit_status);

	thread_exit();
	return status;
}
// exec 보류
// int 
// exec(const char *cmd_line) {
	// int pid = process_create_initd(cmd_line);
// 	if (pid) {
// 		return 0;
// 	}
// 	else {
		
// 		exit(-1);
// 	}

// }
bool 
create(char *file , unsigned initial_size) {	
	if (file == NULL) {
		exit(-1);
	}
	return filesys_create(file, initial_size);	
}

bool 
remove(char *file) {	
	if (file == NULL) {
		exit(-1);
	}
	return filesys_remove(file);	
}

int
open (const char *file) {
	struct thread *th = thread_current();
	int next = th->next_fd;
	if (file == NULL) {
		exit(-1);
	}
	filesys_open(file);
	*(th->fdt+next) = file;
	th->next_fd =+ 1;
	return th->fdt+next;
}

int
filesize (int fd) {
	struct file *length_file = process_get_file(fd);
	if (length_file == NULL) {
		return -1;
	}
	int length = file_length(length_file);
	return length;
}

int
read (int fd, void *buffer, unsigned size) {
	lock_acquire(&filesys_lock);
	struct file *read_file = process_get_file(fd);
	if ( fd==0 ) {
		buffer = input_getc();
	}
	else {
		//return bytes_read;
		file_read(read_file, buffer, size);
	}
	lock_release(&filesys_lock); 
}

int
write (int fd, const void *buffer, unsigned size) {
	lock_acquire(&filesys_lock);
	struct file *write_file = process_get_file(fd);
	if (fd == 1) {
		putbuf(buffer, size);
	}
	else {
		//return bytes_written;
		file_write(write_file, buffer, size);
	}
	lock_release(&filesys_lock);
}

void
seek (int fd, unsigned position) {
	struct file *seek_file = process_get_file(fd);
	file_seek(seek_file, position);
}

unsigned
tell (int fd) {
	struct file *tell_file = process_get_file(fd);
	return file_tell(tell_file);
}

void
close (int fd) {
	process_close_file(fd);
}
