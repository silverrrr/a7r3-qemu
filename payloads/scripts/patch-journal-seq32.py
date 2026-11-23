#!/usr/bin/env python3
"""Journal: store seq as an explicit little-endian u32 pair (no 64-bit shifts),
which is what a 32-bit target can round-trip deterministically."""
P = '/mnt/f/project/a7r3/analysis/payloads/journal_min.h'
s = open(P, encoding='utf-8').read()

old_build = '''    j_put64(rec + 16, seq);'''
new_build = '''    j_put32(rec + 16, (u32)seq);   /* low word */
    j_put32(rec + 20, 0);          /* high word: always 0 on this target */'''
assert old_build in s
s = s.replace(old_build, new_build, 1)

old_check = '''    if (seq) *seq = j_get64(rec + 16);'''
new_check = '''    if (seq) *seq = (unsigned long)j_get32(rec + 16);'''
assert old_check in s
s = s.replace(old_check, new_check, 1)

# drop the now-unused 64-bit helpers
s = s.replace('''static unsigned long j_get64(const u8 *p) {
    return (unsigned long)j_get32(p) | ((unsigned long)j_get32(p + 4) << 32);
}
static void j_put64(u8 *p, unsigned long v) {
    j_put32(p, (u32)v);
    j_put32(p + 4, (u32)(v >> 32));
}
''', '', 1)

open(P, 'w', encoding='utf-8').write(s)
print('journal_min.h: seq stored as explicit u32 low/high words')
