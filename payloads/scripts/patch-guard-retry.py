#!/usr/bin/env python3
"""Guard: narrowed single retry (only ACCEPTED -> STARTED across new boots)."""
P = '/mnt/f/project/a7r3/analysis/payloads/guard.c'
s = open(P, encoding='utf-8').read()

# replace get_seq() with a two-record reader
old_start = s.index('static unsigned long get_seq(')
old_end = s.index('static int write_slot(')
new = '''/* Read both slots.  Returns the newest seq; rec[0]/rec[1] keep the raw records
 * so the caller can inspect phase, seq and boot id of both. */
struct slot {
    int valid;
    unsigned long seq;
    u32 phase;
    char boot[37];
};

static void slot_boot(const u8 *rec, char out[37]) {
    int i;
    for (i = 0; i < 36; i++) out[i] = (char)rec[36 + i];
    out[36] = 0;
}

static unsigned long read_slots(const char *dir, struct slot s[2]) {
    char path[256];
    u8 rec[J_BYTES];
    unsigned long best = 0;
    int k;
    for (k = 0; k < 2; k++) {
        s[k].valid = 0;
        s[k].seq = 0;
        s[k].phase = 0;
        s[k].boot[0] = 0;
        join(path, sizeof path, dir, k ? "/state.b" : "/state.a");
        if (read_all(path, rec, J_BYTES) != J_BYTES) continue;
        if (j_check(rec, &s[k].seq, &s[k].phase, 0, 0, 0) != 1) continue;
        s[k].valid = 1;
        slot_boot(rec, s[k].boot);
        if (s[k].seq > best) best = s[k].seq;
    }
    return best;
}

static int boot_equal(const char *a, const char *b) {
    int i;
    for (i = 0; i < 36; i++) {
        if (a[i] != b[i]) return 0;
        if (!a[i]) return 1;
    }
    return 1;
}

static void current_boot(char out[37]) {
    int i;
    for (i = 0; i < 36; i++) out[i] = 0;
    if (read_all("/proc/sys/kernel/random/boot_id", out, 37) < 0) {
        for (i = 0; i < 36; i++) out[i] = '0';
    }
    for (i = 0; i < 36; i++)
        if (out[i] == '\\n' || out[i] == 0) { out[i] = 0; break; }
}

'''
s = s[:old_start] + new + s[old_end:]

# run(): use the slot structs, decide retry vs immediate rollback
old = '''    seq = get_seq(jdir, &found, &phase);
    if (!found) { say("no_journal", 0); return 0; }
    say(phase == J_ACCEPTED ? "accepted" : phase == J_FAILURE ? "failure"
                                                              : "started", 0);
    if (phase == J_ACCEPTED || phase == J_FAILURE) return 0;
'''
new = '''    {
        struct slot s[2];
        struct slot *latest, *prev = 0;
        char now_boot[37];
        int retry_ok = 0;

        seq = read_slots(jdir, s);
        if (s[0].valid && s[1].valid) {
            latest = (s[0].seq >= s[1].seq) ? &s[0] : &s[1];
            prev = (latest == &s[0]) ? &s[1] : &s[0];
        } else if (s[0].valid) {
            latest = &s[0];
        } else if (s[1].valid) {
            latest = &s[1];
        } else {
            say("no_journal", 0);
            return 0;
        }
        found = 1;
        phase = latest->phase;
        say(phase == J_ACCEPTED ? "accepted" : phase == J_FAILURE ? "failure"
                                                                  : "started", 0);
        if (phase == J_ACCEPTED || phase == J_FAILURE) return 0;

        /* Narrowed retry: only if the previous record was ACCEPTED, the newest
         * STARTED is its direct successor, and all three boot ids differ. */
        if (prev && prev->valid && prev->phase == J_ACCEPTED &&
            latest->seq == prev->seq + 1) {
            current_boot(now_boot);
            retry_ok = !boot_equal(prev->boot, latest->boot) &&
                       !boot_equal(now_boot, latest->boot) &&
                       !boot_equal(now_boot, prev->boot);
        }
        say(retry_ok ? "retry_allowed" : "retry_denied", 0);
        if (!retry_ok)
            wait_max = 0;   /* fail-closed: roll back right away */
    }
'''
assert old in s, 'run() block not found'
s = s.replace(old, new, 1)

s = s.replace('    int found = 0, waited = 0, wait_max, want_mode = 0;',
              '    int found = 0, waited = 0, wait_max, want_mode = 0;', 1)

open(P, 'w', encoding='utf-8').write(s)
print('guard.c: narrowed retry implemented')
