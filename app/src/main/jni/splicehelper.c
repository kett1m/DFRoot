#include <syscall.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>

// argv[0] = program name
// argv[1] = file offset (decimal string)
// argv[2] = file path
// argv[3] = mode:
//   "r" — one-shot read: write 16 bytes of file content to fd 0, exit
//   "d" — daemon: loop reading (cmd, off8) from fd 0, respond on fd 2 (read)
//         or fd 1 (splice), until EOF on fd 0
//   absent — one-shot splice: splice 16 bytes of file page into fd 1, exit

#define OUT_FD  0
#define PIPE_FD 1

__attribute__((naked)) static long mysyscall1(unsigned long arg0, unsigned long nr) {
    asm volatile("mov x8, x1\nsvc 0\nret\n"
                 ::: "x0", "x1", "x2", "x3", "x4", "x5", "x6", "x7",
                     "x8", "x9", "x10", "x11", "x12", "x13", "x14", "x15",
                     "x16", "x17", "x18", "x19", "x20", "x21", "x22",
                     "x23", "x24", "x25", "x26", "x27", "x28", "x29",
                     "x30", "memory");
}

__attribute__((naked)) static long mysyscall3(
    unsigned long a0, unsigned long a1, unsigned long a2, unsigned long nr) {
    asm volatile("mov x8, x3\nsvc 0\nret\n"
                 ::: "x0", "x1", "x2", "x3", "x4", "x5", "x6", "x7",
                     "x8", "x9", "x10", "x11", "x12", "x13", "x14", "x15",
                     "x16", "x17", "x18", "x19", "x20", "x21", "x22",
                     "x23", "x24", "x25", "x26", "x27", "x28", "x29",
                     "x30", "memory");
}

__attribute__((naked)) static long mysyscall6(
    unsigned long a0, unsigned long a1, unsigned long a2,
    unsigned long a3, unsigned long a4, unsigned long a5, unsigned long nr) {
    asm volatile("mov x8, x6\nsvc 0\nret\n"
                 ::: "x0", "x1", "x2", "x3", "x4", "x5", "x6", "x7",
                     "x8", "x9", "x10", "x11", "x12", "x13", "x14", "x15",
                     "x16", "x17", "x18", "x19", "x20", "x21", "x22",
                     "x23", "x24", "x25", "x26", "x27", "x28", "x29",
                     "x30", "memory");
}

static unsigned long parse_int(char *s) {
    unsigned long val = 0;
    while (*s) { val = val * 10 + (unsigned long)(*s - '0'); s++; }
    return val;
}

static int streq(const char *a, const char *b) {
    while (*a && *b && *a == *b) { a++; b++; }
    return *a == *b;
}

void start_c(void *argblock) {
    int argc = (int)*(long *)argblock;
    char **argv = (char **)argblock + 1;

    off64_t off = (off64_t)parse_int(argv[1]);
    char *target = argv[2];
    char *mode = (argc >= 4) ? argv[3] : (char *)0;

    int file_fd = (int)mysyscall3(
        (unsigned long)AT_FDCWD,
        (unsigned long)target,
        (unsigned long)O_RDONLY,
        __NR_openat);
    if (file_fd < 0)
        mysyscall1(1, __NR_exit_group);

    if (mode && streq(mode, "r")) {
        /* Read mode: lseek to offset, read 16 bytes, write to OUT_FD */
        /* Exit codes: 0=ok, 1=read<16, 2=write<16, 3=OUT_FD not a pipe (fd sanitized) */
        mysyscall3((unsigned long)file_fd, (unsigned long)off, SEEK_SET, __NR_lseek);
        unsigned char buf[16];
        long n = mysyscall3((unsigned long)file_fd, (unsigned long)buf, 16, __NR_read);
        if (n != 16)
            mysyscall1(1, __NR_exit_group);
        if (mysyscall3(OUT_FD, 0, SEEK_CUR, __NR_lseek) != (long)-29L) /* ESPIPE: fd is a pipe */
            mysyscall1(3, __NR_exit_group);
        long w = mysyscall3(OUT_FD, (unsigned long)buf, 16, __NR_write);
        mysyscall1((unsigned long)(w == 16 ? 0 : 2), __NR_exit_group);
    }

    if (mode && streq(mode, "d")) {
        /* Daemon mode: loop reading (cmd_byte, off8_le) from OUT_FD (stdin).
         * cmd=0: lseek+read 16 bytes, write to OUT_FD
         * cmd=1: splice 16 bytes into PIPE_FD
         * Exit on EOF (read returns 0) or error. */
        for (;;) {
            unsigned char hdr[9];
            long n = mysyscall3(OUT_FD, (unsigned long)hdr, 9, __NR_read);
            if (n != 9) break;  /* EOF or short read */
            unsigned char cmd = hdr[0];
            off64_t doff = (off64_t)(
                (unsigned)hdr[1]        |
                (unsigned)hdr[2] << 8   |
                (unsigned)hdr[3] << 16  |
                (unsigned)hdr[4] << 24  |
                (unsigned long)hdr[5] << 32 |
                (unsigned long)hdr[6] << 40 |
                (unsigned long)hdr[7] << 48 |
                (unsigned long)hdr[8] << 56);

            mysyscall3((unsigned long)file_fd, (unsigned long)doff, SEEK_SET, __NR_lseek);

            if (cmd == 1) {
                /* Splice 16 bytes into PIPE_FD, then ACK on fd 2 */
                long sr = mysyscall6(
                    (unsigned long)file_fd,
                    (unsigned long)&doff,
                    PIPE_FD,
                    (unsigned long)NULL,
                    16,
                    SPLICE_F_MOVE,
                    __NR_splice);
                unsigned char ack = (sr == 16) ? 1 : 0;
                mysyscall3(2, (unsigned long)&ack, 1, __NR_write);
            } else {
                /* Read 16 bytes, write to stderr (fd 2 = data pipe) */
                unsigned char buf[16];
                long rn = mysyscall3((unsigned long)file_fd, (unsigned long)buf, 16, __NR_read);
                if (rn != 16) break;
                mysyscall3(2, (unsigned long)buf, 16, __NR_write);
            }
        }
        mysyscall1(0, __NR_exit_group);
    }

    /* One-shot splice mode: splice 16-byte page into PIPE_FD */
    long ret = mysyscall6(
        (unsigned long)file_fd,
        (unsigned long)&off,
        PIPE_FD,
        (unsigned long)NULL,
        16,
        SPLICE_F_MOVE,
        __NR_splice);
    mysyscall1((unsigned long)(ret == 16 ? 0 : 1), __NR_exit_group);
}

__attribute__((naked)) void _start() {
    asm(".extern start_c\nmov x0, sp\nb start_c\n");
}
