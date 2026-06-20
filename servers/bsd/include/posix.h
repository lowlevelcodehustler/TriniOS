/*
 * TriniOS BSD Compatibility Server - POSIX Definitions
 *
 * POSIX-compliant constants, types, and syscall interface.
 * Maps traditional UNIX semantics to Mach IPC operations.
 */

#ifndef POSIX_H
#define POSIX_H

#include <mach/mach.h>
#include <stdarg.h>

/* ============================================================================
 * POSIX File Descriptor Flags
 * ============================================================================ */

#define O_RDONLY      0x0000
#define O_WRONLY      0x0001
#define O_RDWR        0x0002
#define O_CREAT       0x0200
#define O_TRUNC       0x0400
#define O_APPEND      0x0008
#define O_EXCL        0x0800
#define O_NONBLOCK    0x04000

#define F_DUPFD       0
#define F_GETFD       1
#define F_SETFD       2
#define F_GETFL       3
#define F_SETFL       4

/* ============================================================================
 * Standard File Descriptors
 * ============================================================================ */

#define STDIN_FILENO  0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

/* ============================================================================
 * File Permission Bits
 * ============================================================================ */

#define S_IRWXU       0x01C0
#define S_IRUSR       0x0100
#define S_IWUSR       0x0080
#define S_IXUSR       0x0040
#define S_IRWXG       0x0038
#define S_IRGRP       0x0020
#define S_IWGRP       0x0010
#define S_IXGRP       0x0008
#define S_IRWXO       0x0007
#define S_IROTH       0x0004
#define S_IWOTH       0x0002
#define S_IXOTH       0x0001

#define S_IFMT        0xF000
#define S_IFIFO       0x1000
#define S_IFCHR       0x2000
#define S_IFDIR       0x4000
#define S_IFBLK       0x6000
#define S_IFREG       0x8000
#define S_IFLNK       0xA000
#define S_IFSOCK      0xC000

/* ============================================================================
 * POSIX Signals
 * ============================================================================ */

#define SIGHUP        1
#define SIGINT        2
#define SIGQUIT       3
#define SIGILL        4
#define SIGTRAP       5
#define SIGABRT       6
#define SIGIOT        6
#define SIGBUS        7
#define SIGFPE        8
#define SIGKILL       9
#define SIGUSR1      10
#define SIGSEGV      11
#define SIGUSR2      12
#define SIGPIPE      13
#define SIGALRM      14
#define SIGTERM      15
#define SIGSTKFLT    16
#define SIGCHLD      17
#define SIGCONT      18
#define SIGSTOP      19
#define SIGTSTP      20
#define SIGTTIN      21
#define SIGTTOU      22
#define SIGURG       23
#define SIGXCPU      24
#define SIGXFSZ      25
#define SIGVTALRM    26
#define SIGPROF      27
#define SIGWINCH     28
#define SIGIO        29
#define SIGPWR       30
#define SIGSYS       31

/* ============================================================================
 * POSIX Error Codes
 * ============================================================================ */

#define POSIX_OK              0
#define POSIX_EPERM           1
#define POSIX_ENOENT          2
#define POSIX_ESRCH           3
#define POSIX_EINTR           4
#define POSIX_EIO             5
#define POSIX_ENXIO           6
#define POSIX_E2BIG           7
#define POSIX_ENOEXEC         8
#define POSIX_EBADF           9
#define POSIX_ECHILD          10
#define POSIX_EAGAIN          11
#define POSIX_ENOMEM          12
#define POSIX_EACCES          13
#define POSIX_EFAULT          14
#define POSIX_EBUSY           16
#define POSIX_EEXIST          17
#define POSIX_EXDEV           18
#define POSIX_ENOTBLK         19
#define POSIX_EINVAL          22
#define POSIX_ENOSPC          28
#define POSIX_EISDIR          21
#define POSIX_ENOTDIR         20
#define POSIX_ELOOP           40
#define POSIX_ENAMETOOLONG    36
#define POSIX_ENOTEMPTY       39
#define POSIX_ENOSYS          38
#define POSIX_ENFILE          24
#define POSIX_EMFILE          24

/* ============================================================================
 * waitpid Status Macros
 * ============================================================================ */

#define WNOHANG       0x00000001
#define WUNTRACED     0x00000002

#define WIFEXITED(status)   (((status) & 0xFF) == 0)
#define WEXITSTATUS(status) (((status) >> 8) & 0xFF)
#define WIFSIGNALED(status) (((status) & 0x7F) != 0)
#define WTERMSIG(status)    ((status) & 0x7F)

/* ============================================================================
 * POSIX Syscall Interface
 * ============================================================================ */

/*
 * These are the C library wrappers that user-space applications call.
 * Each wrapper sends a Mach IPC message to the BSD server.
 */

/* Process management */
int  posix_fork(void);
int  posix_execve(const char *path, char *const argv[], char *const envp[]);
int  posix_waitpid(int pid, int *status, int options);
void posix_exit(int status);
int  posix_kill(int pid, int sig);
int  posix_getpid(void);
int  posix_getppid(void);

/* File I/O */
int  posix_open(const char *path, int flags, ...);
int  posix_close(int fd);
int  posix_read(int fd, void *buf, unsigned int count);
int  posix_write(int fd, const void *buf, unsigned int count);
int  posix_pipe(int pipefd[2]);
int  posix_fcntl(int fd, int cmd, ...);

/* Signal handling */
void (*posix_signal(int sig, void (*handler)(int)))(int);

/* Signal handler return value on error */
#ifndef SIG_ERR
#define SIG_ERR ((void (*)(int))(-1))
#endif

/* ============================================================================
 * Task-local storage key for BSD server port
 * ============================================================================ */

#define TLS_KEY_BSD_PORT  0

#endif /* POSIX_H */
