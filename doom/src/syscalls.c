/*
 * Newlib syscall stubs for AIPC DOOM bare-metal.
 *
 * Newlib provides the full C standard library; we wire it to the hardware
 * by implementing the minimal set of syscalls it delegates to the platform:
 *
 *   _sbrk   - heap growth (backed by linker-script symbols)
 *   _open   - maps any .wad filename to DDR_WAD_BASE
 *   _close  - closes the WAD fd
 *   _read   - reads bytes from the in-memory WAD
 *   _lseek  - seeks within the in-memory WAD
 *   _fstat  - reports WAD size (derived from WAD directory header)
 *   _write  - discards output for now; TODO route to UART
 *   _isatty, _kill, _getpid, _exit - minimal stubs
 */

#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

extern char _heap_start[];
extern char _heap_end[];

void *_sbrk(ptrdiff_t incr)
{
    static char *heap_ptr = NULL;
    if (heap_ptr == NULL)
        heap_ptr = _heap_start;

    char *prev = heap_ptr;
    if (heap_ptr + incr > _heap_end) {
        errno = ENOMEM;
        return (void *)-1;
    }
    heap_ptr += incr;
    return prev;
}

/* -------------------------------------------------------------------------
 * WAD virtual file
 *
 * DOOM uses fseek(fd, 0, SEEK_END) to determine the WAD size, so we derive
 * the true size from the WAD directory header rather than hard-coding it.
 *
 * WAD header layout (little-endian):
 *   [0..3]  magic       "IWAD" or "PWAD"
 *   [4..7]  num_lumps   number of directory entries
 *   [8..11] dir_offset  byte offset of the directory
 *
 * File size = dir_offset + num_lumps * 16 (each directory entry is 16 bytes).
 * ------------------------------------------------------------------------- */

#define DDR_WAD_BASE  0x30900000u
#define WAD_FD        3

static int    wad_open   = 0;
static size_t wad_offset = 0;
static size_t wad_size   = 0;

static void wad_detect_size(void)
{
    const uint8_t *hdr = (const uint8_t *)DDR_WAD_BASE;
    uint32_t num_lumps  = (uint32_t)hdr[4]  | ((uint32_t)hdr[5]  << 8)
                        | ((uint32_t)hdr[6]  << 16) | ((uint32_t)hdr[7]  << 24);
    uint32_t dir_offset = (uint32_t)hdr[8]  | ((uint32_t)hdr[9]  << 8)
                        | ((uint32_t)hdr[10] << 16) | ((uint32_t)hdr[11] << 24);
    wad_size = (size_t)dir_offset + (size_t)num_lumps * 16u;
}

int _open(const char *name, int flags, int mode)
{
    (void)flags; (void)mode;

    /* Accept any filename ending in .wad or .WAD */
    size_t len = strlen(name);
    if (len >= 4) {
        const char *ext = name + len - 4;
        if (ext[0] == '.' &&
            (ext[1] == 'w' || ext[1] == 'W') &&
            (ext[2] == 'a' || ext[2] == 'A') &&
            (ext[3] == 'd' || ext[3] == 'D'))
        {
            if (wad_open) { errno = EMFILE; return -1; }
            wad_detect_size();
            wad_open   = 1;
            wad_offset = 0;
            return WAD_FD;
        }
    }
    errno = ENOENT;
    return -1;
}

int _close(int fd)
{
    if (fd == WAD_FD) { wad_open = 0; wad_offset = 0; return 0; }
    errno = EBADF;
    return -1;
}

int _read(int fd, char *buf, int len)
{
    if (fd == WAD_FD) {
        if (wad_offset >= wad_size) return 0;
        size_t avail = wad_size - wad_offset;
        size_t n = (size_t)len < avail ? (size_t)len : avail;
        memcpy(buf, (const void *)(DDR_WAD_BASE + wad_offset), n);
        wad_offset += n;
        return (int)n;
    }
    errno = EBADF;
    return -1;
}

int _lseek(int fd, int offset, int whence)
{
    if (fd == WAD_FD) {
        size_t new_off;
        switch (whence) {
        case SEEK_SET: new_off = (size_t)offset;               break;
        case SEEK_CUR: new_off = wad_offset + (size_t)offset;  break;
        case SEEK_END: new_off = wad_size   + (size_t)offset;  break;
        default:       errno = EINVAL; return -1;
        }
        wad_offset = new_off;
        return (int)wad_offset;
    }
    errno = EBADF;
    return -1;
}

int _fstat(int fd, struct stat *st)
{
    if (fd == WAD_FD) {
        memset(st, 0, sizeof(*st));
        st->st_size  = (off_t)wad_size;
        st->st_mode  = S_IFREG | 0444;
        st->st_blksize = 512;
        return 0;
    }
    errno = EBADF;
    return -1;
}

int _write(int fd, char *buf, int len)
{
    /* TODO: route fd=1/2 to UART once UART is implemented. */
    (void)fd; (void)buf;
    return len;
}

int _isatty(int fd) { return (fd == 1 || fd == 2) ? 1 : 0; }

int _getpid(void)   { return 1; }

int _kill(int pid, int sig)
{
    (void)pid; (void)sig;
    errno = EINVAL;
    return -1;
}

void _exit(int status)
{
    (void)status;
    while (1) {}
}

/* DOOM calls M_MakeDirectory() for save-game dirs; no filesystem on bare
 * metal, so silently succeed. */
int mkdir(const char *path, mode_t mode)
{
    (void)path; (void)mode;
    return 0;
}

/* newlib reentrant wrappers require these; nothing to do on bare metal. */
int _unlink(const char *path) { (void)path; errno = ENOENT; return -1; }
int _link(const char *old, const char *new) { (void)old; (void)new; errno = ENOSYS; return -1; }
