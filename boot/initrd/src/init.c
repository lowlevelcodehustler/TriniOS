/*
 * TriniOS - /sbin/init (PID 1)
 * 
 * Initial process that bootstraps the system:
 * - Sets up mount points
 * * - Creates device nodes
 * - Starts BSD compatibility server
 * - Launches the shell
 *
 * This runs inside the initrd (RAM-based rootfs) before any
 * persistent storage is available.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>

/* Configuration */
#define SHELL_PATH        "/bin/sh"
#define BSD_SERVER_PATH   "/sbin/bsd_server"
#define MAX_CHILDREN      32
#define INIT_LOG_TAG      "[init]  "

/* Forward declarations */
static int setup_mount_points(void);
static int create_device_nodes(void);
static int start_bsd_server(void);
static int launch_shell(void);
static void child_reaper(int sig);
static void reap_children(void);
static void log_init(const char *msg);

/* Track child processes for proper cleanup */
static pid_t child_pids[MAX_CHILDREN];
static int child_count = 0;

/*
 * log_init - Print a log message with the init tag
 * Uses write() for safety in early boot (before stdio is fully initialized)
 */
static void log_init(const char *msg)
{
    const char *tag = INIT_LOG_TAG;
    write(1, tag, strlen(tag));
    write(1, msg, strlen(msg));
    write(1, "\n", 1);
}

/*
 * child_reaper - Signal handler for SIGCHLD
 */
static void child_reaper(int sig)
{
    (void)sig;
    reap_children();
}

/*
 * reap_children - Wait for all exited children (non-blocking)
 */
static void reap_children(void)
{
    int status;
    pid_t pid;
    
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        char buf[64];
        snprintf(buf, sizeof(buf), "Child %d exited", pid);
        log_init(buf);
        
        /* Remove from tracked children */
        for (int i = 0; i < child_count; i++) {
            if (child_pids[i] == pid) {
                child_pids[i] = child_pids[--child_count];
                break;
            }
        }
    }
}

/*
 * setup_mount_points - Create directory structure for filesystems
 */
static int setup_mount_points(void)
{
    int ret = 0;
    
    /* Essential directories for the root filesystem */
    struct dir_entry {
        const char *path;
        mode_t mode;
    };
    
    struct dir_entry dirs[] = {
        { "/dev",        0755 },
        { "/dev/pts",    0755 },
        { "/proc",       0555 },
        { "/sys",        0555 },
        { "/tmp",        1777 },    /* sticky bit */
        { "/var",        0755 },
        { "/var/log",    0755 },
        { "/var/run",    0755 },
        { "/var/tmp",    1777 },    /* sticky bit */
        { "/etc",        0755 },
        { NULL,          0 }
    };
    
    for (int i = 0; dirs[i].path != NULL; i++) {
        log_init("Creating: ");
        log_init(dirs[i].path);
        
        if (mkdir(dirs[i].path, dirs[i].mode) != 0) {
            if (errno != EEXIST) {
                char buf[128];
                snprintf(buf, sizeof(buf), "WARNING: mkdir %s failed: %s", 
                         dirs[i].path, strerror(errno));
                log_init(buf);
                ret = 1;
            }
        }
    }
    
    return ret;
}

/*
 * create_device_nodes - Create essential device nodes
 * 
 * In a full implementation, these would be proper mknod calls
 * with the correct major/minor numbers for the Mach kernel's
 * device driver framework.
 * 
 * Major/minor numbers follow Linux convention for compatibility:
 * null:     1,3    zero:     1,5
 * random:   1,8    urandom:  1,9
 * console:  5,1    tty:      5,0
 * mem:      1,1    kmem:     1,2
 */
static int create_device_nodes(void)
{
    int ret = 0;
    
    struct dev_entry {
        const char *path;
        mode_t mode;
        int major;
        int minor;
    };
    
    struct dev_entry devs[] = {
        { "/dev/null",     S_IFCHR | 0666, 1, 3 },
        { "/dev/zero",     S_IFCHR | 0666, 1, 5 },
        { "/dev/random",   S_IFCHR | 0666, 1, 8 },
        { "/dev/urandom",  S_IFCHR | 0666, 1, 9 },
        { "/dev/console",  S_IFCHR | 0600, 5, 1 },
        { "/dev/tty",      S_IFCHR | 0666, 5, 0 },
        { NULL,            0, 0, 0 }
    };
    
    for (int i = 0; devs[i].path != NULL; i++) {
        /* Pack major/minor into dev_t */
        unsigned dev = (devs[i].major << 8) | devs[i].minor;
        
        if (mknod(devs[i].path, devs[i].mode, dev) != 0) {
            if (errno != EEXIST) {
                char buf[128];
                snprintf(buf, sizeof(buf), 
                         "WARNING: mknod %s failed: %s", devs[i].path, strerror(errno));
                log_init(buf);
                ret = 1;
            }
        }
    }
    
    /* Open /dev/console for stdin/stdout/stderr */
    int console_fd = open("/dev/console", O_RDWR);
    if (console_fd < 0) {
        /* Fallback: /dev/null if console not available */
        console_fd = open("/dev/null", O_RDWR);
    }
    
    if (console_fd >= 0) {
        dup2(console_fd, 0);
        dup2(console_fd, 1);
        dup2(console_fd, 2);
        close(console_fd);
    }
    
    return ret;
}

/*
 * start_bsd_server - Fork and start the BSD compatibility server
 * 
 * The BSD server is a Mach user-space server that provides POSIX
 * semantics (fork, exec, pipe, signals) over Mach IPC.
 */
static int start_bsd_server(void)
{
    pid_t pid = fork();
    
    if (pid < 0) {
        log_init("ERROR: fork() failed for BSD server");
        log_init(strerror(errno));
        return -1;
    }
    
    if (pid == 0) {
        /* Child: exec the BSD server */
        execl(BSD_SERVER_PATH, "bsd_server", NULL);
        
        /* If execl returns, it failed */
        log_init("ERROR: execl() failed for BSD server");
        log_init(strerror(errno));
        _exit(127);
    }
    
    /* Parent: record child PID */
    if (child_count < MAX_CHILDREN) {
        child_pids[child_count++] = pid;
    }
    
    char buf[64];
    snprintf(buf, sizeof(buf), "BSD server started (PID %d)", pid);
    log_init(buf);
    
    return 0;
}

/*
 * launch_shell - Fork and exec the TriniOS shell
 */
static int launch_shell(void)
{
    pid_t pid = fork();
    
    if (pid < 0) {
        log_init("ERROR: fork() failed for shell");
        log_init(strerror(errno));
        return -1;
    }
    
    if (pid == 0) {
        /* Child: exec the shell */
        
        /* Set up environment */
        putenv("HOME=/");
        putenv("PATH=/bin:/sbin:/usr/bin:/usr/sbin");
        putenv("SHELL=/bin/sh");
        putenv("TERM=xterm");
        putenv("USER=root");
        putenv("HOSTNAME=trini");
        putenv("LANG=en_US.UTF-8");
        putenv("TRINIOS_VERSION=0.1");
        
        execl(SHELL_PATH, "sh", "-i", NULL);
        
        /* If execl returns, it failed */
        log_init("ERROR: execl() failed for shell");
        log_init(strerror(errno));
        _exit(127);
    }
    
    /* Parent: record child PID and wait */
    if (child_count < MAX_CHILDREN) {
        child_pids[child_count++] = pid;
    }
    
    /* Wait for shell to exit */
    int status = 0;
    waitpid(pid, &status, 0);
    
    if (WIFEXITED(status)) {
        char buf[64];
        snprintf(buf, sizeof(buf), "Shell exited with status %d", WEXITSTATUS(status));
        log_init(buf);
        return WEXITSTATUS(status);
    }
    
    if (WIFSIGNALED(status)) {
        char buf[64];
        snprintf(buf, sizeof(buf), "Shell killed by signal %d", WTERMSIG(status));
        log_init(buf);
        return 128 + WTERMSIG(status);
    }
    
    return 0;
}

/*
 * main - Entry point for PID 1
 */
int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;
    
    log_init("TriniOS init starting (PID 1)");
    
    /* Step 1: Set up signal handlers */
    signal(SIGHUP, SIG_IGN);
    signal(SIGCHLD, child_reaper);
    log_init("Signal handlers configured");
    
    /* Step 2: Create essential mount points */
    log_init("Setting up mount points...");
    if (setup_mount_points() != 0) {
        log_init("WARNING: Failed to set up all mount points");
    }
    
    /* Step 3: Create device nodes */
    log_init("Creating device nodes...");
    if (create_device_nodes() != 0) {
        log_init("WARNING: Failed to create all device nodes");
    }
    
    /* Step 4: Start the BSD compatibility server */
    log_init("Starting BSD compatibility server...");
    if (start_bsd_server() != 0) {
        log_init("ERROR: BSD server failed to start");
        goto emergency_shell;
    }
    
    log_init("BSD server is ready");
    
    /* Step 5: Launch the shell */
    log_init("Launching shell...");
    if (launch_shell() == 0) {
        log_init("Shell exited normally");
    } else {
        log_init("WARNING: Shell exited with error");
    }
    
    /* If we reach here, the shell exited - drop to emergency loop */
emergency_shell:
    log_init("Entering emergency mode - shell loop");
    
    while (1) {
        log_init("Restarting shell...");
        if (launch_shell() != 0) {
            sleep(1);
        }
    }
    
    /* Should never reach here */
    log_init("Init exiting - system halt");
    return 0;
}