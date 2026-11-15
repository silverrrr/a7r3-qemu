/* nm_probe.c -- minimal dlopen() test library.
 *
 * Loaded by nm_boot.so to answer one question: can this firmware dlopen() a
 * freestanding .so at run time at all?  It only prints and returns.
 */
typedef unsigned long usize;

#include <stddef.h>
#include <stdint.h>

#define SYS_write 4
#define SYS_open 5
#define SYS_close 6
#define O_TRUNC_CREAT 0x241

static long sys2(long nr, long a, long b)
{
    register long r7 __asm__("r7") = nr;
    register long r0 __asm__("r0") = a;
    register long r1 __asm__("r1") = b;
    __asm__ volatile("svc #0" : "+r"(r0) : "r"(r7), "r"(r1) : "memory");
    return r0;
}

static long sys3(long nr, long a, long b, long c)
{
    register long r7 __asm__("r7") = nr;
    register long r0 __asm__("r0") = a;
    register long r1 __asm__("r1") = b;
    register long r2 __asm__("r2") = c;
    __asm__ volatile("svc #0" : "+r"(r0) : "r"(r7), "r"(r1), "r"(r2) : "memory");
    return r0;
}

__attribute__((constructor)) static void probe_init(void)
{
    static const char msg[] = "NM_PROBE alive\n";
    long fd;
    sys3(SYS_write, 1, (long)msg, (long)sizeof(msg) - 1);
    fd = sys3(SYS_open, (long)"/tmp/nm_probe.log", O_TRUNC_CREAT, 0644);
    if (fd >= 0) {
        sys3(SYS_write, fd, (long)msg, (long)sizeof(msg) - 1);
        sys2(SYS_close, fd, 0);
    }
}
