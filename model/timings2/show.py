#!/usr/bin/env python3
"""show.py <pass> [filter substring] -- print every case's curve: cycles per iteration T(n), and the penalty
P(n) = (T(n) - n) - (T(40) - 40) against the far reference, plus the event counter per iteration."""
import sys
import t2res


def main():
    pas = sys.argv[1]
    flt = sys.argv[2] if len(sys.argv) > 2 else ''
    cs = t2res.curves(t2res.load(pas))
    for c in sorted(cs):
        e = cs[c]
        m = e['meta']
        tag = f"{m['a']} | {m['b']}  [{m['mode']} {m['var']}]"
        if flt and flt not in tag:
            continue
        pts = e['pts']
        if e['status'] or 40 not in pts:
            print(f'{tag}: status {e["status"]:#x} pts {sorted(pts)}')
            continue
        s40 = pts[40][0] - 40
        ns = [n for n in range(17) if n in pts]
        pen = ' '.join(f'{pts[n][0] - n - s40:+.2f}'.replace('+0.00', ' 0') for n in ns)
        ev = ' '.join(f'{pts[n][1]:.2f}' for n in ns[:4])
        print(f'{tag}\n   T(40)={pts[40][0]:.2f}  P(0..16)= {pen}\n   event(0..3)= {ev}  event(40)={pts[40][1]:.2f}')


if __name__ == '__main__':
    main()
