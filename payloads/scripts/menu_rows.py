#!/usr/bin/env python3
"""Detect rows, the highlighted row and the active tab in an A7R3 frame.

Works for the settings list and for modal dialogs:
  * ignore the tab bar (y < 70) and the footer (y > 434);
  * ignore centered header text (median text x > 250);
  * rows are left-aligned text bands with height >= 6;
  * the selected row is found by matching the orange highlight bar centre.
"""
import sys
from PIL import Image


def rows_and_selection(path):
    img = Image.open(path).convert("RGB")
    w, h = img.size
    px = img.load()
    scan = []
    for y in range(70, 470):
        xs = []
        for x in range(6, w - 6, 2):
            r, g, b = px[x, y]
            if r > 85 and g > 85 and b > 85 and abs(r - g) < 30 and abs(g - b) < 30:
                xs.append(x)
        scan.append((y, xs))
    bands = []
    cur = None
    for y, xs in scan:
        if len(xs) >= 5:
            if cur is None:
                cur = [y, y, list(xs)]
            else:
                cur[1] = y
                cur[2].extend(xs)
        else:
            if cur is not None and cur[1] - cur[0] >= 6:
                bands.append(cur)
            cur = None
    if cur is not None and cur[1] - cur[0] >= 6:
        bands.append(cur)
    rows = []
    for a, b, xs in bands:
        if b > 434:
            continue
        xs.sort()
        if xs[len(xs) // 2] > 250:
            continue
        rows.append((a, b))
    best_y, best_n = 0, 0
    for y in range(40, 470):
        n = 0
        for x in range(0, w, 4):
            r, g, b = px[x, y]
            if r > 100 and 25 <= g <= 110 and b < 80:
                n += 1
        if n > best_n:
            best_y, best_n = y, n
    sel_ys = []
    for y in range(40, 470):
        n = 0
        for x in range(0, w, 4):
            r, g, b = px[x, y]
            if r > 100 and 25 <= g <= 110 and b < 80:
                n += 1
        if n >= max(3, best_n * 0.4):
            sel_ys.append(y)
    sel_center = (sel_ys[0] + sel_ys[-1]) // 2 if sel_ys else best_y
    sel = sel_ys[0] if sel_ys else best_y
    sel_idx = None
    best_dist = None
    for i, (a, b) in enumerate(rows):
        d = abs((a + b) // 2 - sel_center)
        if best_dist is None or d < best_dist:
            best_dist = d
            sel_idx = i
    if best_dist is not None and best_dist > 70:
        sel_idx = None
    return rows, sel, sel_idx


def _tab_cells(path):
    img = Image.open(path).convert("RGB")
    w, h = img.size
    px = img.load()
    cells = 7
    cw = w // cells
    means = []
    for i in range(cells):
        x0, x1 = i * cw, (i + 1) * cw
        r = g = b = n = 0
        for y in range(8, 58):
            for x in range(x0 + 4, x1 - 4, 2):
                pr, pg, pb = px[x, y]
                r += pr
                g += pg
                b += pb
                n += 1
        means.append((r // n, g // n, b // n) if n else (0, 0, 0))
    return means


def detect_tab(path):
    """Return (index, focus_on_tab_bar) or (None, False).

    The active category has a coloured box (yellow/blue/purple/grey depending on
    the icon); when the tab bar itself has focus the focused icon gets a light
    grey/white box.  Both score high on `saturation + brightness` while the
    other cells stay dark.
    """
    means = _tab_cells(path)
    best, best_score = None, 0
    for i, (r, g, b) in enumerate(means):
        sat = max(r, g, b) - min(r, g, b)
        mean = (r + g + b) // 3
        score = sat + mean
        if score > best_score and score > 100:
            best, best_score = i, score
    if best is None:
        return None, False
    r, g, b = means[best]
    sat = max(r, g, b) - min(r, g, b)
    return best, sat <= 20


def classify(path, x, y):
    """Decide what a click at (x, y) should do.

    Returns one of:
      {"kind": "enter"}                     nothing to select -> confirm/dismiss
      {"kind": "move", "delta": n, ...}     move the selection by n rows
      {"kind": "tab", "from": i, "to": j}   switch category tab
      {"kind": "none"}
    """
    if y < 62:
        rows, sel, sel_idx = rows_and_selection(path)
        cur, on_bar = detect_tab(path)
        target = min(6, max(0, x * 7 // 640))
        if cur is None or target == cur:
            return {"kind": "none", "tab": cur}
        # from the list: UP (row index + 1) times reaches the tab bar
        ups = 0 if on_bar else ((sel_idx + 1) if sel_idx is not None else 1)
        return {"kind": "tab", "from": cur, "to": target,
                "delta": target - cur, "ups": ups}
    if 62 <= y <= 128:
        # header row: page arrows at the right edge (◀ 2/7 ▶)
        if x >= 605:
            return {"kind": "page", "delta": 1}
        if 540 <= x <= 585:
            return {"kind": "page", "delta": -1}
        return {"kind": "none", "where": "header"}
    rows, sel, sel_idx = rows_and_selection(path)
    if y > 434:
        # bottom bar / outside the list: a click there confirms or dismisses
        return {"kind": "enter"}
    if len(rows) < 2 or sel_idx is None:
        return {"kind": "enter"}
    target = min(range(len(rows)),
                 key=lambda i: abs((rows[i][0] + rows[i][1]) // 2 - y))
    delta = target - sel_idx
    if delta == 0:
        return {"kind": "enter", "row": target}
    return {"kind": "move", "delta": delta, "from": sel_idx, "to": target,
            "rows": len(rows)}


if __name__ == "__main__":
    bands, sel, sel_idx = rows_and_selection(sys.argv[1])
    print("highlight y:", sel, "selected index:", sel_idx)
    for i, (a, b) in enumerate(bands):
        print("  row %d: %d..%d center %d" % (i, a, b, (a + b) // 2))
    if len(sys.argv) > 2:
        print("tab:", detect_tab(sys.argv[1]))
