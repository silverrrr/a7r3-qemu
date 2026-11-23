/* Dual-slot install journal (256-byte, SHA-256 sealed), shared by the
 * publisher and the guard.  Same shape as a7r3-boot-runtime's state.a/state.b:
 * magic "A7GXST01", monotonically increasing seq, phase, code, pid, boot id,
 * old/new hashes, and a digest over the first 224 bytes.
 */
#ifndef A7R3_JOURNAL_MIN_H
#define A7R3_JOURNAL_MIN_H

#ifndef SHA_MIN_TYPES
#define SHA_MIN_TYPES
typedef unsigned char u8;
typedef unsigned int u32;
typedef unsigned long u32l;
#endif

#include "sha256_min.h"

#define J_MAGIC      "A7GXST01"
#define J_BYTES      256
#define J_SEAL_OFF   224

#define J_STARTED    1
#define J_ACCEPTED   2
#define J_FAILURE    3

static void j_memzero(void *p, u32 n) {
    u8 *b = p;
    while (n--) *b++ = 0;
}
static void j_memcopy(void *d, const void *s, u32 n) {
    u8 *a = d; const u8 *b = s;
    while (n--) *a++ = *b++;
}
static int j_memequal(const void *a, const void *b, u32 n) {
    const u8 *x = a, *y = b;
    while (n--) if (*x++ != *y++) return 0;
    return 1;
}

static u32 j_get32(const u8 *p) {
    return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}
static void j_put32(u8 *p, u32 v) {
    p[0] = (u8)v; p[1] = (u8)(v >> 8); p[2] = (u8)(v >> 16); p[3] = (u8)(v >> 24);
}

static void j_seal(u8 *rec) {
    struct sha s;
    sha_init(&s);
    sha_add(&s, rec, J_SEAL_OFF);
    sha_end(&s, rec + J_SEAL_OFF);
}

/* Fill one 256-byte slot. */
static void j_build(u8 rec[J_BYTES], unsigned long seq, u32 phase, u32 code,
                    u32 pid, const char *boot, const u8 old_sha[32],
                    const u8 new_sha[32]) {
    u32 i;
    j_memzero(rec, J_BYTES);
    j_memcopy(rec, J_MAGIC, 8);
    j_put32(rec + 8, 1);            /* abi */
    j_put32(rec + 12, J_BYTES);
    j_put32(rec + 16, (u32)seq);   /* low word */
    j_put32(rec + 20, 0);          /* high word: always 0 on this target */
    j_put32(rec + 24, phase);
    j_put32(rec + 28, code);
    j_put32(rec + 32, pid);
    for (i = 0; i < 36 && boot[i]; i++) rec[36 + i] = (u8)boot[i];
    if (old_sha) j_memcopy(rec + 72, old_sha, 32);
    if (new_sha) j_memcopy(rec + 104, new_sha, 32);
    j_seal(rec);
}

/* 1 = valid slot, 0 = absent, -1 = malformed. */
static int j_check(const u8 rec[J_BYTES], unsigned long *seq, u32 *phase,
                   u32 *code, const u8 old_sha[32], const u8 new_sha[32]) {
    u8 copy[J_BYTES];
    u8 digest[32];
    struct sha s;
    if (!j_memequal(rec, J_MAGIC, 8)) return -1;
    if (j_get32(rec + 8) != 1 || j_get32(rec + 12) != J_BYTES) return -1;
    j_memcopy(copy, rec, J_BYTES);
    j_seal(copy);
    j_memcopy(digest, copy + J_SEAL_OFF, 32);
    if (!j_memequal(digest, rec + J_SEAL_OFF, 32)) return -1;
    if (seq) *seq = (unsigned long)j_get32(rec + 16);
    if (phase) *phase = j_get32(rec + 24);
    if (code) *code = j_get32(rec + 28);
    if (old_sha) j_memcopy((void *)old_sha, rec + 72, 32);
    if (new_sha) j_memcopy((void *)new_sha, rec + 104, 32);
    return 1;
}

#endif /* A7R3_JOURNAL_MIN_H */
