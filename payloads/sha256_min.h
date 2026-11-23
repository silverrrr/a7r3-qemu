/* Minimal SHA-256, shared by the device publisher and host tests.
 * Freestanding: no libc, no allocation. */
#ifndef A7R3_SHA256_MIN_H
#define A7R3_SHA256_MIN_H

#ifndef SHA_MIN_TYPES
#define SHA_MIN_TYPES
typedef unsigned char u8;
typedef unsigned int u32;
typedef unsigned long u32l;
#endif

#ifndef SHA_MIN_HELPERS
#define SHA_MIN_HELPERS
static void sha_mem_set(void *d, int c, u32 n) {
    u8 *p = d;
    while (n--) *p++ = (u8)c;
}
static void sha_mem_copy(void *d, const void *s, u32 n) {
    u8 *a = d; const u8 *b = s;
    while (n--) *a++ = *b++;
}
#endif

/* --- tiny SHA-256 ------------------------------------------------------ */
struct sha {
    u32 h[8];
    u32l len;
    u8 buf[64];
    u32 used;
};
static const u32 K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1,
    0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786,
    0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
    0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
    0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a,
    0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};
#define ROR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))
static void sha_block(struct sha *s, const u8 *p) {
    u32 w[64], a, b, c, d, e, f, g, h, t1, t2;
    int i;
    for (i = 0; i < 16; i++)
        w[i] = ((u32)p[4 * i] << 24) | ((u32)p[4 * i + 1] << 16) |
               ((u32)p[4 * i + 2] << 8) | p[4 * i + 3];
    for (i = 16; i < 64; i++) {
        u32 s0 = ROR(w[i - 15], 7) ^ ROR(w[i - 15], 18) ^ (w[i - 15] >> 3);
        u32 s1 = ROR(w[i - 2], 17) ^ ROR(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    a = s->h[0]; b = s->h[1]; c = s->h[2]; d = s->h[3];
    e = s->h[4]; f = s->h[5]; g = s->h[6]; h = s->h[7];
    for (i = 0; i < 64; i++) {
        u32 S1 = ROR(e, 6) ^ ROR(e, 11) ^ ROR(e, 25);
        u32 ch = (e & f) ^ ((~e) & g);
        u32 S0 = ROR(a, 2) ^ ROR(a, 13) ^ ROR(a, 22);
        u32 mj = (a & b) ^ (a & c) ^ (b & c);
        t1 = h + S1 + ch + K[i] + w[i];
        t2 = S0 + mj;
        h = g; g = f; f = e; e = d + t1;
        d = c; c = b; b = a; a = t1 + t2;
    }
    s->h[0] += a; s->h[1] += b; s->h[2] += c; s->h[3] += d;
    s->h[4] += e; s->h[5] += f; s->h[6] += g; s->h[7] += h;
}
static void sha_init(struct sha *s) {
    s->h[0] = 0x6a09e667; s->h[1] = 0xbb67ae85; s->h[2] = 0x3c6ef372;
    s->h[3] = 0xa54ff53a; s->h[4] = 0x510e527f; s->h[5] = 0x9b05688c;
    s->h[6] = 0x1f83d9ab; s->h[7] = 0x5be0cd19;
    s->len = 0; s->used = 0;
}
static void sha_add(struct sha *s, const u8 *p, u32 n) {
    s->len += n;
    while (n) {
        u32 take = 64 - s->used;
        if (take > n) take = n;
        sha_mem_copy(s->buf + s->used, p, take);
        s->used += take; p += take; n -= take;
        if (s->used == 64) { sha_block(s, s->buf); s->used = 0; }
    }
}
static void sha_end(struct sha *s, u8 out32[32]) {
    u32l bits = s->len * 8;
    u8 pad[72];
    u32 padn, i;
    sha_mem_set(pad, 0, sizeof pad);
    pad[0] = 0x80;
    padn = (s->used < 56) ? (56 - s->used) : (120 - s->used);
    sha_add(s, pad, padn);
    for (i = 0; i < 8; i++) pad[i] = (u8)(bits >> (56 - 8 * i));
    sha_add(s, pad, 8);
    for (i = 0; i < 8; i++) {
        out32[4 * i] = (u8)(s->h[i] >> 24);
        out32[4 * i + 1] = (u8)(s->h[i] >> 16);
        out32[4 * i + 2] = (u8)(s->h[i] >> 8);
        out32[4 * i + 3] = (u8)s->h[i];
    }
}

#endif /* A7R3_SHA256_MIN_H */
