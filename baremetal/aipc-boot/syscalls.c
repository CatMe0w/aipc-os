/*
 * Newlib syscall stubs. LVGL has its own allocator and never opens a file, so
 * these stubs only satisfy the linker and send stdout to the log.
 */

#include <errno.h>
#include <stddef.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "log.h"

extern char _heap_start[];
extern char _heap_end[];

void *_sbrk(ptrdiff_t incr)
{
    static char *heap_ptr;

    if (heap_ptr == NULL)
        heap_ptr = _heap_start;

    if (heap_ptr + incr > _heap_end) {
        errno = ENOMEM;
        return (void *)-1;
    }

    char *prev = heap_ptr;

    heap_ptr += incr;
    return prev;
}

int _write(int fd, const char *buf, int len)
{
    (void)fd;
    for (int i = 0; i < len; i++)
        log_putc(buf[i]);
    return len;
}

int _read(int fd, char *buf, int len)
{
    (void)fd; (void)buf; (void)len;
    return 0;
}

int _open(const char *name, int flags, int mode)
{
    (void)name; (void)flags; (void)mode;
    errno = ENOENT;
    return -1;
}

int _close(int fd)
{
    (void)fd;
    errno = EBADF;
    return -1;
}

int _lseek(int fd, int offset, int whence)
{
    (void)fd; (void)offset; (void)whence;
    errno = EBADF;
    return -1;
}

int _fstat(int fd, struct stat *st)
{
    (void)fd;
    st->st_mode = S_IFCHR;
    return 0;
}

int _isatty(int fd)
{
    (void)fd;
    return 1;
}

int _getpid(void)
{
    return 1;
}

int _kill(int pid, int sig)
{
    (void)pid; (void)sig;
    errno = EINVAL;
    return -1;
}

void _exit(int status)
{
    (void)status;
    for (;;)
        ;
}
