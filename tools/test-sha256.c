/* Host-side vector test for sha256_min.h. */
#include <stdio.h>
#include <string.h>
#include "sha256_min.h"

static void hex(const u8 *h, char out[65]) {
    static const char *d = "0123456789abcdef";
    int i;
    for (i = 0; i < 32; i++) {
        out[2 * i] = d[h[i] >> 4];
        out[2 * i + 1] = d[h[i] & 15];
    }
    out[64] = 0;
}

static int check(const char *msg, unsigned n, const char *want) {
    struct sha s;
    u8 h[32];
    char got[65];
    sha_init(&s);
    sha_add(&s, (const u8 *)msg, n);
    sha_end(&s, h);
    hex(h, got);
    if (strcmp(got, want) != 0) {
        printf("FAIL len=%u got=%s want=%s\n", n, got, want);
        return 1;
    }
    printf("ok   len=%u %s\n", n, got);
    return 0;
}

int main(void) {
    static char big[1000];
    int bad = 0;
    bad += check("", 0,
        "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    bad += check("abc", 3,
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    bad += check("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq",
        56,
        "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    memset(big, 'a', sizeof big);
    bad += check(big, (unsigned)sizeof big,
        "41edece42d63e8d9bf515a9ba6932e1c20cbc9f5a5d134645adb5db1b9737ea3");
    printf(bad ? "SHA256 TEST FAILED\n" : "SHA256 TEST OK\n");
    return bad ? 1 : 0;
}
