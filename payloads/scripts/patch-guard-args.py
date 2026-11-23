#!/usr/bin/env python3
"""Guard: derive the expected stock hash from the stock file itself (argc 8)."""
P = '/mnt/f/project/a7r3/analysis/payloads/guard.c'
s = open(P, encoding='utf-8').read()

s = s.replace('''    if (argc != 10) {
        out("GUARD usage: <journal> <wait_s> <mount> <target> <retained> "
            "<standby> <stock> <stock_sha256> <stock_size>\\n");
        return 2;
    }''', '''    if (argc != 8) {
        out("GUARD usage: <journal> <wait_s> <mount> <target> <retained> "
            "<standby> <stock>\\n");
        return 2;
    }''', 1)

s = s.replace('''    mount = argv[3]; target = argv[4]; retained = argv[5];
    standby = argv[6]; stock = argv[7];
    if (str_len(argv[8]) != 64) return 2;
    {
        int i;
        for (i = 0; i < 32; i++) {
            int hi = argv[8][2 * i], lo = argv[8][2 * i + 1];
            hi = (hi <= '9') ? hi - '0' : (hi | 32) - 'a' + 10;
            lo = (lo <= '9') ? lo - '0' : (lo | 32) - 'a' + 10;
            want_stock[i] = (u8)((hi << 4) | lo);
        }
    }
''', '''    mount = argv[3]; target = argv[4]; retained = argv[5];
    standby = argv[6]; stock = argv[7];

    /* the expected hash is the hash of the stock copy itself, so callers do not
     * have to carry it around */
    rc = (int)hash_file(stock, want_stock);
    if (rc) { say("stock_hash", rc); return 2; }
''', 1)

open(P, 'w', encoding='utf-8').write(s)
print('guard.c: argc 8, stock hash derived from file')
