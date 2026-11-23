/* Host test for journal_min.h: byte layout and round-trip of every field.
 * Builds a record exactly as the device does and re-reads it, asserting the
 * little-endian offsets (magic 0, abi 8, size 12, seq 16, phase 24, code 28,
 * pid 32, boot 36, old 72, new 104, digest 224). */
#include <stdio.h>
#include <string.h>
#include "journal_min.h"

static int fails;

static void expect(const char *what, int want, int got) {
    if (want != got) {
        printf("FAIL %-24s want=%d got=%d\n", what, want, got);
        fails++;
    } else {
        printf("ok   %-24s %d\n", what, got);
    }
}

int main(void) {
    u8 rec[J_BYTES];
    u8 old_sha[32], new_sha[32];
    unsigned long seq = 0;
    u32 phase = 0, code = 0;
    const char *boot = "00112233-4455-6677-8899-aabbccddeeff";
    int i;

    for (i = 0; i < 32; i++) {
        old_sha[i] = (u8)i;
        new_sha[i] = (u8)(0xF0 ^ i);
    }

    j_build(rec, 7, J_ACCEPTED, 3, 4242, boot, old_sha, new_sha);

    expect("magic", 1, memcmp(rec, J_MAGIC, 8) == 0);
    expect("abi==1", 1, j_get32(rec + 8) == 1);
    expect("bytes==256", 1, j_get32(rec + 12) == J_BYTES);
    expect("seq low word", 7, (int)j_get32(rec + 16));
    expect("seq high word", 0, (int)j_get32(rec + 20));
    expect("phase", J_ACCEPTED, (int)j_get32(rec + 24));
    expect("code", 3, (int)j_get32(rec + 28));
    expect("pid", 4242, (int)j_get32(rec + 32));
    expect("boot", 1, memcmp(rec + 36, boot, strlen(boot)) == 0);
    expect("old sha", 1, memcmp(rec + 72, old_sha, 32) == 0);
    expect("new sha", 1, memcmp(rec + 104, new_sha, 32) == 0);

    expect("check valid", 1, j_check(rec, &seq, &phase, &code, 0, 0));
    expect("check seq", 7, (int)seq);
    expect("check phase", J_ACCEPTED, (int)phase);
    expect("check code", 3, (int)code);

    /* a flipped byte must invalidate the seal */
    rec[40] ^= 0x01;
    expect("tamper rejected", -1, j_check(rec, 0, 0, 0, 0, 0));

    printf(fails ? "JOURNAL TEST FAILED (%d)\n" : "JOURNAL TEST OK\n", fails);
    return fails ? 1 : 0;
}
