#!/usr/bin/env python3
"""nm_boot.so: check the install journal and spawn guard.elf before loading."""
P = '/mnt/f/project/a7r3/analysis/payloads/nm_boot.c'
s = open(P, encoding='utf-8').read()

# syscall numbers + journal include
s = s.replace('#define SYS_nanosleep 162',
              '#define SYS_nanosleep 162\n#define SYS_fork 2\n'
              '#define SYS_execve 11\n#define SYS_exit 1\n'
              '#define SHA_MIN_TYPES 1\n#include "journal_min.h"', 1)

# sys1 and sys4 helpers after sys3
anchor = '''static long sys3(long nr, long a, long b, long c)'''
helper = '''static long sys1(long nr, long a)
{
    register long r7 __asm__("r7") = nr;
    register long r0 __asm__("r0") = a;
    __asm__ volatile("svc #0" : "+r"(r0) : "r"(r7) : "memory");
    return r0;
}

static long sys4(long nr, long a, long b, long c, long d)
{
    register long r7 __asm__("r7") = nr;
    register long r0 __asm__("r0") = a;
    register long r1 __asm__("r1") = b;
    register long r2 __asm__("r2") = c;
    register long r3 __asm__("r3") = d;
    __asm__ volatile("svc #0" : "+r"(r0) : "r"(r7), "r"(r1), "r"(r2), "r"(r3)
                     : "memory");
    return r0;
}

static long sys3(long nr, long a, long b, long c)'''
assert anchor in s
s = s.replace(anchor, helper, 1)

# journal scan + guard spawn, inserted before load_payload
anchor2 = '''static void load_payload(void)'''
block = '''/* Read the two journal slots; 1 when the newest record is STARTED. */
static int journal_started(void)
{
    static const char *const names[2] = { "/setting/inst/state.a",
                                          "/setting/inst/state.b" };
    u8 rec[J_BYTES];
    int k, found = 0;
    unsigned long best = 0, seq;
    u32 phase = 0, code;

    for (k = 0; k < 2; k++) {
        long fd = sys3(SYS_open, (long)names[k], 0, 0), n = 0;
        if (fd < 0)
            continue;
        while (n < J_BYTES) {
            long r = sys3(SYS_read, fd, (long)((u8 *)rec + n), J_BYTES - n);
            if (r <= 0)
                break;
            n += r;
        }
        sys2(SYS_close, fd, 0);
        if (n != J_BYTES)
            continue;
        if (j_check(rec, &seq, &phase, &code, 0, 0) != 1)
            continue;
        if (!found || seq > best) {
            best = seq;
            found = 1;
        }
    }
    if (!found)
        return 0;
    return phase == J_STARTED;
}

/* The guard runs as its own process so this constructor is never blocked. */
static void maybe_guard(void)
{
    static char *argv[] = {
        (char *)"/setting/guard.elf", (char *)"/setting/inst", (char *)"20",
        (char *)"/usr", (char *)"/usr/lib/libIMDB.so",
        (char *)"/usr/lib/libIMDB.so.retained",
        (char *)"/usr/lib/.libIMDB.standby",
        (char *)"/setting/inst/stock.so", (char *)0
    };
    static char *envp[] = { (char *)0 };
    long pid;

    if (!journal_started()) {
        out("NM_BOOT journal ok\\n", 20);
        return;
    }
    out("NM_BOOT journal STARTED -> guard\\n", 33);
    pid = sys1(SYS_fork, 0);
    if (pid == 0) {
        sys3(SYS_execve, (long)argv[0], (long)argv, (long)envp);
        sys1(SYS_exit, 127);
    }
    if (pid > 0)
        log_hex("NM_BOOT guard_pid=", (unsigned long)pid);
    else
        out("NM_BOOT fork fail\\n", 18);
}

static void load_payload(void)'''
assert anchor2 in s
s = s.replace(anchor2, block, 1)

# call it first in the loader thread
s = s.replace('''    (void)arg;
    load_payload();''', '''    (void)arg;
    maybe_guard();
    load_payload();''', 1)

open(P, 'w', encoding='utf-8').write(s)
print('nm_boot.c: journal check + guard spawn added')
