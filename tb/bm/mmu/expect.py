#!/usr/bin/env python3
"""expect.py -- expected values of the bare-metal MMU probe port (tb/bm/mmu/mmubm.c), read from the console logs.

Writes expect.json to stdout.  Every value comes from tools/hw/mmu/run{1,2,3}.log (mmutest{,2,3}.elf on the console,
rtl/v1/spec/mmu.md section 6); a few are the difference of two logged numbers (a PR against its branch address, noted in
"source").  mmubm.c records each value under (name, idx); id = (fnv1a32(name) & 0xFFFFFF) | (idx << 24).

Fields per record:
  mask      bits compared.  0 for values the console printed from KOS state (register garbage, foreign memory,
            unrelated data after AT 1 -> 0).  0xFFFFFDFF for data array 1 reads: bit 9 reads undefined on the silicon
            and 0 on the RTL (mmu 2.2).
  requires  what else must be present for the value to be comparable, beyond the MMU and the core:
              fetch   the fetch unit's run-ahead (v1_fetch, WI-CORE-B; mmu 3.5 R1)
              ic      the instruction cache (cold / warm lines, the forced miss after an array write)
              pmc     the performance counters' event counts
              timing  a cycle count (the console's, with its caches warm)
              sq      the store queues' transfer to memory
              oc      the operand cache and its address array
              pipeline  the cycle a register write lands relative to a following access (mmu 7.4: the MMUCR AT
                      write is seen from the load 3 instructions later); a functional model sees it at once
              open    a console value the RTL does not reproduce, recorded with its evidence and not compared at any
                      level until the named open question closes (M2, agents/reports/integration/M2.md)
  Levels (the milestone checks, rtl/v1/SOC.md 8.3): M2 on v1_soc IDEAL_MEM = 1, TRANSLATE = 1 (imx: the MMU with a warm
  ideal I-cache) skips ic, oc, sq, open; M3 and later (the real caches) skip open only.

Usage:  expect.py > expect.json                         generate
        expect.py check <dump.bin> [--skip tag,tag,...]  compare a result block (the bytes at physical 0x0C0F0000
                                                        after the program halted) with expect.json; records whose
                                                        requires meet --skip are not compared
        expect.py self <dump.bin> [--skip tag,tag,...]   the check's self-test on a block that passes: every compared
                                                        record perturbed alone must be flagged, and a missing magic,
                                                        an unexpected exception, a missing record
"""
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
HW = os.path.normpath(os.path.join(HERE, "..", "..", "..", "tools", "hw", "mmu"))
DA1 = 0xFFFFFDFF
FULL = 0xFFFFFFFF


def fnv(s):
    h = 2166136261
    for c in s.encode():
        h ^= c
        h = (h * 16777619) & 0xFFFFFFFF
    return h


RECS = []
SEEN = set()


def rec(name, idx, val, mask=FULL, requires=(), source=""):
    assert 0 <= idx < 256, (name, idx)
    key = (name, idx)
    assert key not in SEEN, key
    SEEN.add(key)
    RECS.append({"name": name, "idx": idx, "id": "0x%08x" % ((fnv(name) & 0xFFFFFF) | (idx << 24)),
                 "expect": "0x%08x" % (val & 0xFFFFFFFF), "mask": "0x%08x" % mask, "requires": list(requires),
                 "source": source})


def lines(name):
    with open(os.path.join(HW, name)) as f:
        return f.read().splitlines()


def find(ls, prefix, n=1):
    out = [l for l in ls if l.startswith(prefix)]
    assert len(out) >= n, (prefix, len(out))
    return out


def hexes(s):
    return [int(x, 16) for x in re.findall(r"\b[0-9a-f]{8}\b", s)]


def pack(chars):
    assert len(chars) == 4, chars
    return sum(ord(c) << (8 * i) for i, c in enumerate(chars))


def main():
    r1, r2, r3 = lines("run1.log"), lines("run2.log"), lines("run3.log")

    # ---------------------------------------------------------------- A
    l = find(r1, "A0 initial")[0]
    m = re.search(r"PTEH (\w+) PTEL (\w+) PTEA (\w+) TTB (\w+) TEA (\w+) MMUCR (\w+)", l)
    for i in range(6):
        rec("A0", i, int(m.group(i + 1), 16), source="run1 A0 (KOS state; the RTL's power-on value is 0 [P])")
    for i, reg in enumerate(["PTEH", "PTEL", "PTEA", "TTB ", "TEA "]):
        l = find(r1, "A1 " + reg)[0]
        v = re.findall(r"rd ([0-9a-f]{8})", l)
        for j in range(3):
            rec("A1", 3 * i + j, int(v[j], 16), source="run1 " + l[:8])
    v = re.findall(r"rd ([0-9a-f]{8})", find(r1, "A2 MMUCR")[0])
    for j in range(3):
        rec("A2", j, int(v[j], 16), source="run1 A2")
    for tag, nm in (("A3 TI  before", "A3b"), ("A3 TI  after", "A3a")):
        v = re.findall(r"(?:aa|da1|da2|MMUCR) ([0-9a-f]{8})", find(r1, tag)[0])
        for i, x in enumerate(v):
            rec(nm, i, int(x, 16), DA1 if i in (1, 4) else FULL, source="run1 " + tag)

    # ---------------------------------------------------------------- B
    v = []
    for tag in ("B1 ITLB1 aa=", "B1 ITLB1 da2=", "B2 UTLB37 aa=", "B2 UTLB37 da2="):
        v += [(w, int(x, 16)) for w, x in re.findall(r"\b(aa|da1|da2) ([0-9a-f]{8})", find(r1, tag)[0])]
    assert len(v) == 24, len(v)
    for i, (w, x) in enumerate(v):
        rec("B12", i, x, DA1 if w == "da1" else FULL, source="run1 B1 / B2")
    for e in range(4):
        v = re.findall(r"\) ([0-9a-f]{8})", find(r1, "B3 ITLB%d" % e)[0])
        for j in range(4):
            rec("B3", 4 * e + j, int(v[j], 16), source="run1 B3")
    m = re.search(r"e<<8: (\d+) mismatches", find(r1, "B4 UTLB decode")[0])
    rec("B4", 0, int(m.group(1)), source="run1 B4")
    idx = [i for i, l in enumerate(r1) if l.startswith("B5 associative")]
    assert len(idx) == 2
    for at, i0 in enumerate(idx):
        for c in range(8):
            l = r1[i0 + 1 + c]
            m = re.search(r"UTLB20 aa (\w+) da1 (\w+)  ITLB1 aa (\w+) da1 (\w+)  urc (\d+)->(\d+)", l)
            vals = [int(m.group(k), 16) for k in range(1, 5)] + [int(m.group(5)), int(m.group(6))]
            for j, x in enumerate(vals):
                rec("B5", at * 64 + c * 8 + j, x, DA1 if j in (1, 3) else FULL, source="run1 B5 AT=%d case %c" % (at, 97 + c))

    # ---------------------------------------------------------------- C
    for t, tag in enumerate(("C1 LDTLB", "C2 LDTLB")):
        l = find(r1, tag)[0]
        v = re.findall(r"(?:aa|da1|da2|MMUCR) ([0-9a-f]{8})", l.split("->")[1])
        for j in range(4):
            rec("C", 4 * t + j, int(v[j], 16), DA1 if j == 1 else FULL, source="run1 " + tag[:2])

    # ---------------------------------------------------------------- D
    i0 = next(i for i, l in enumerate(r1) if l.startswith("D1 URC after each step"))
    k = 0
    for l in r1[i0 + 1:]:
        m = re.match(r"  (.+?)\s+urc\s+(\d+)(?:  exc (\w+))?$", l)
        if not m:
            break
        ev = {None: 0, "040": 0x040, "060": 0x060, "0c0": 0x0C0, "0e0": 0x0E0}[m.group(3)]
        rec("D1u", k, int(m.group(2)), source="run1 D1 " + m.group(1).strip())
        rec("D1e", k, ev, source="run1 D1 " + m.group(1).strip())
        k += 1
    assert k == 38, k
    for t, l in enumerate(find(r1, "D2 URB", 6)):
        m = re.match(r"D2 URB\s+\d+ URC\s+\d+ :\s+(\d+) ->((?: \d+)+)", l)
        vals = [int(m.group(1))] + [int(x) for x in m.group(2).split()]
        assert len(vals) == 9
        for i, x in enumerate(vals):
            rec("D2", 9 * t + i, x, source="run1 D2 #%d" % t)

    # ---------------------------------------------------------------- E
    i0 = next(i for i, l in enumerate(r1) if l.startswith("E1 fetch page"))
    items = re.findall(r"p(\d):([0-9a-f]{2})\[(.{4})\](X?)", r1[i0 + 1])
    assert len(items) == 13
    for i, (_, lr, pg, x) in enumerate(items):
        rec("E1", 3 * i, int(lr, 16), source="run1 E1 #%d" % i)
        rec("E1", 3 * i + 1, pack(pg), source="run1 E1 #%d pages %s" % (i, pg))
        rec("E1", 3 * i + 2, 1 if x else 0, source="run1 E1 #%d" % i)
    for t, l in enumerate(find(r1, "E1b ", 7)):
        m = re.match(r"E1b lrui\(fill\)=(\w+) itlb\[(.{4})\] set \w+ -> fetch p4: lrui (\w+) itlb \[(.) (.) (.) (.)\]( EXC)?", l)
        rec("E1b", 5 * t, int(m.group(1), 16), source="run1 E1b #%d" % t)
        rec("E1b", 5 * t + 1, pack(m.group(2)), source="run1 E1b #%d" % t)
        rec("E1b", 5 * t + 2, int(m.group(3), 16), source="run1 E1b #%d" % t)
        rec("E1b", 5 * t + 3, pack("".join(m.group(k) for k in range(4, 8))), source="run1 E1b #%d" % t)
        rec("E1b", 5 * t + 4, 1 if m.group(8) else 0, source="run1 E1b #%d" % t)
    m = re.match(r"E1c after p3 \[(.{4})\]; LRUI:=0, fetch p0 -> itlb \[(.) (.) (.) (.)\] lrui (\w+)", find(r1, "E1c")[0])
    rec("E1c", 0, pack(m.group(1)), source="run1 E1c")
    rec("E1c", 1, pack("".join(m.group(k) for k in range(2, 6))), source="run1 E1c")
    rec("E1c", 2, int(m.group(6), 16), source="run1 E1c")
    l = find(r1, "E2 4K")[0]
    v = re.findall(r"(?:aa|da1|da2|TEA|PTEH) ([0-9a-f]{8})", l.split(":", 1)[1])
    n = int(re.search(r"exc (\d+)", l).group(1))
    for j, x in enumerate([int(y, 16) for y in v] + [n]):
        rec("E2", j, x, DA1 if j in (1, 4) else FULL, source="run1 E2 4K")
    l = find(r1, "E2 1M")[0]
    v = re.findall(r"(?:aa|da1) ([0-9a-f]{8})", l.split(":", 1)[1])
    n = int(re.search(r"exc (\d+)", l).group(1))
    for j, x in enumerate([int(y, 16) for y in v] + [n]):
        rec("E2", 9 + j, x, DA1 if j == 1 else FULL, source="run1 E2 1M")
    for var, l in enumerate(find(r1, "E4 rts;nop", 4)):
        m = re.search(r"count (\d+), itlb \[(.) (.) (.) (.)\], exc (\d+) ev (\w+)", l)
        rec("E4", 4 * var, int(m.group(1)), requires=("pmc",), source="run1 E4 #%d (PMC 0x07 / 0x03)" % var)
        rec("E4", 4 * var + 1, pack("".join(m.group(k) for k in range(2, 6))), requires=("fetch",),
            source="run1 E4 #%d" % var)
        rec("E4", 4 * var + 2, int(m.group(6)), source="run1 E4 #%d" % var)
        rec("E4", 4 * var + 3, int(m.group(7), 16), source="run1 E4 #%d" % var)
    for var, tag in enumerate(("E4c", "E4d")):
        l = find(r1, tag)[0]
        m = re.search(r"exc (\d+) ev (\w+) tea (\w+) pteh (\w+) spc-rts ([+-]\d+) pr-ret ([+-]\d+)", l)
        b = 8 * var
        rec("E4c", b, int(m.group(1)), source="run1 " + tag)
        rec("E4c", b + 1, int(m.group(2), 16), source="run1 " + tag)
        rec("E4c", b + 2, int(m.group(3), 16), source="run1 " + tag)
        rec("E4c", b + 3, int(m.group(4), 16), source="run1 " + tag)
        rec("E4c", b + 4, int(m.group(5)), source="run1 " + tag)
        rec("E4c", b + 5, int(m.group(6)), source="run1 " + tag)
        rec("E4c", b + 6, 0, 0, requires=("pmc",), source="run1 %s: the ITLB-miss count was cut off in the log" % tag)

    # ---------------------------------------------------------------- F
    i0 = next(i for i, l in enumerate(r1) if l.startswith("F  (dp0"))
    rows = []
    for l in r1[i0 + 1:]:
        m = re.match(r"  (\S.*?)\s+exc=(\d+) ev=(\w+) tea=(\w+) pteh=(\w+) spc([+-]\d+) ret=(\w+)", l)
        if m:
            rows.append(m)
        elif l.startswith("    PR at the exception"):
            continue
        else:
            break
    assert len(rows) == 52, len(rows)
    # ret is the probe's R0 (or the value its wrapper returns); it is KOS register garbage for these probes
    garbage = ("st32", "st8", "tst.b", "tas.b", "ocbi", "ocbp", "ocbwb", "pref", "bra;", "jsr;")
    for k, m in enumerate(rows):
        nm = m.group(1)
        src = "run1 F " + nm
        rec("Fn", k, int(m.group(2)), source=src)
        rec("Fev", k, int(m.group(3), 16), source=src)
        rec("Ftea", k, int(m.group(4), 16), source=src)
        rec("Fpteh", k, int(m.group(5), 16), source=src)
        rec("Fspc", k, int(m.group(6)), source=src)
        g = nm.startswith(garbage) and not nm.startswith("st faults")
        rec("Fret", k, int(m.group(7), 16), 0 if g else FULL, source=src + (" (ret: KOS register garbage)" if g else ""))
    l = find(r1, "    PR at the exception")[0]
    pr = int(re.search(r"exception (\w+)", l).group(1), 16)
    rec("Fjsrpr", 0, pr - (0x8C015B26 + 4), source="run1 F: PR %08x - (_p_jsr_ds_at 8c015b26 + 4), mmutest.elf" % pr)
    l = find(r1, "  PTEH preset 00000300")[0]
    m = re.search(r"PTEH (\w+) TEA (\w+)$", l)
    rec("Fx", 0, int(m.group(1), 16), source="run1 F PTEH preset 300")
    rec("Fx", 1, int(m.group(2), 16), source="run1 F PTEH preset 300")
    l = find(r1, "  AT0 st16 P1 misaligned")[0]
    m = re.search(r"exc (\d+) ev (\w+) TEA (\w+) PTEH (\w+)", l)
    for j in range(4):
        rec("Fx", 2 + j, int(m.group(j + 1), 16 if j else 10), source="run1 F AT0 st16")
    l = find(r1, "  mac.l dp0,unmapped:")[0]
    m = re.search(r"r4 (\w+) r5 (\w+) \(in \w+ \w+\) \| mac.l unmapped,dp0: r4 (\w+) r5 (\w+)", l)
    for j in range(4):
        rec("Fmac", j, int(m.group(j + 1), 16), source="run1 F mac.l registers")

    # ---------------------------------------------------------------- G
    rows = [re.match(r"  G (.+?)\s+va (\w+) got (\w+) phys (\w+)\s+(==|!=)?\s*exc (\d+) ev (\w+)", l)
            for l in r1 if l.startswith("  G ") and " va " in l]
    assert len(rows) == 25, len(rows)
    for k, m in enumerate(rows):
        src = "run1 G " + m.group(1)
        va, got, n = int(m.group(2), 16), int(m.group(3), 16), int(m.group(6))
        # got: the data read; comparable when it came from the data buffer (PA 0c0b0000-0c0bffff) or is the -1 of a fault
        ok = n == 1 or (0x0C0B0000 <= (got & 0xFFFF0000) | 0x0C000000 and got >> 16 == 0xD000 and va != 0x6C0FFFFC)
        rec("Gva", k, va, source=src)
        # "64K PPN|fc00: +100" reads PA 0c0b0100 through a C = 0 page after F's cacheable store of 12345678 there:
        # the console's copy-back OC still held that line, so the uncached read saw the old memory word
        oc = m.group(1).startswith("64K PPN|fc00")
        rec("Ggot", k, got, FULL if ok else 0, requires=("oc",) if oc else (),
            source=src + ("" if ok else " (memory outside the data buffer)") +
            (" (the line F stored 12345678 to was dirty in the copy-back OC)" if oc else ""))
        rec("Gn", k, n, source=src)
        rec("Gev", k, int(m.group(7), 16), source=src)
    rec("Gpteh", 0, int(re.search(r"ASID=12: (\w+)", find(r1, "  G PTEH after")[0]).group(1), 16), source="run1 G PTEH")
    m = re.search(r"\[3f00000c\] (\w+) \(TEA written \w+\) exc (\d+); \[3f000010\] (\w+) exc (\d+) \(MMUCR (\w+)\)",
                  find(r1, "  G area7")[0])
    for j, x in enumerate([int(m.group(1), 16), int(m.group(2)), int(m.group(3), 16), int(m.group(4)), int(m.group(5), 16)]):
        rec("Ga7", j, x, source="run1 G area 7")

    # ---------------------------------------------------------------- S
    rows = [l for l in r1 if re.match(r"S (no SQ|1M )", l)]
    assert len(rows) == 5
    for c, l in enumerate(rows):
        m = re.search(r"st x8: exc (\d+) ev (\w+) tea (\w+) \| pref: exc (\d+) ev (\w+) \| urc (\d+)->(\d+)->(\d+) \| mem:((?: \w+)+)", l)
        vals = [int(m.group(1)), int(m.group(2), 16), int(m.group(3), 16), int(m.group(4)), int(m.group(5), 16),
                int(m.group(6)), int(m.group(7)), int(m.group(8))]
        for j, x in enumerate(vals):
            rec("S", 16 * c + j, x, source="run1 S #%d" % c)
        mem = [int(x, 16) for x in m.group(9).split()]
        assert len(mem) == 7
        for j, x in enumerate(mem):
            rec("Smem", 8 * c + j, x, requires=("sq",), source="run1 S #%d memory" % c)

    # ---------------------------------------------------------------- T
    l = find(r1, "T1 jsr")[0]
    m = re.search(r"ITLB hit min (\d+) max (\d+) \| ITLB miss \(UTLB idx 0\) min (\d+) max \d+ \(itlb-miss events (\d+) / 32\)"
                  r" \| UTLB idx 63 min (\d+) max \d+ \(events (\d+) / 32\)", l)
    t = [int(m.group(k)) for k in range(1, 7)]
    for j, x in enumerate(t):
        req = ("pmc",) if j in (3, 5) else ("timing",)
        rec("T1", j, x, requires=req, source="run1 T1")
    v = re.findall(r"(\d+)(?: \||\s+cycles)", find(r1, "T2 256")[0].split(":", 1)[1])
    assert len(v) == 4, v
    for j, x in enumerate(v):
        rec("T2", j, int(x), requires=("timing",), source="run1 T2")

    # ---------------------------------------------------------------- U
    rows = [re.match(r"  U (.+?)\s+exc=(\d+) ev=(\w+) tea=(\w+) spc-pc([+-]\d+)", l) for l in r1 if l.startswith("  U ") and "exc=" in l]
    assert len(rows) == 12, len(rows)
    for k, m in enumerate(rows):
        src = "run1 U " + m.group(1)
        rec("Un", k, int(m.group(2)), source=src)
        rec("Uev", k, int(m.group(3), 16), source=src)
        fetch_p1 = m.group(1).startswith("fetch P1")
        rec("Utea", k, int(m.group(4), 16), 0 if fetch_p1 else FULL, source=src + (" (tea = _p_dummy: Udummy)" if fetch_p1 else ""))
        rec("Uspc", k, int(m.group(5)), source=src)
        if fetch_p1:
            rec("Udummy", 0, int(m.group(4), 16) - 0x8C015B38, source="run1 U fetch P1: tea - _p_dummy 8c015b38 (mmutest.elf)")
    m = re.search(r"e(\d+) da1 (\w+)", find(r1, "  U ITLB entry")[0])
    rec("Ui10", 0, int(m.group(1)), source="run1 U ITLB entry")
    rec("Ui10", 1, int(m.group(2), 16), DA1, source="run1 U ITLB entry")

    # ---------------------------------------------------------------- run2: R1 R2 R3 R4 R5 R6 R7
    rows = [re.match(r"  pos (\w+): cold \[(.{4})\] (\w+) (X?)\s*warm \[(.{4})\] (\w+) (X?)", l) for l in r2 if l.startswith("  pos ")]
    assert len(rows) == 14
    for i, m in enumerate(rows):
        src = "run2 R1 pos %s" % m.group(1)
        rec("R1c", 3 * i, pack(m.group(2)), requires=("fetch", "ic"), source=src + " cold")
        rec("R1c", 3 * i + 1, int(m.group(3), 16), requires=("fetch", "ic"), source=src + " cold")
        rec("R1c", 3 * i + 2, 1 if m.group(4) else 0, source=src + " cold")
        rec("R1w", 3 * i, pack(m.group(5)), requires=("fetch",), source=src + " warm")
        rec("R1w", 3 * i + 1, int(m.group(6), 16), requires=("fetch",), source=src + " warm")
        rec("R1w", 3 * i + 2, 1 if m.group(7) else 0, source=src + " warm")
    l1, l2 = find(r2, "R2 call 'rts;nop'")[0], find(r2, "R2 call 8 nops")[0]
    m = re.search(r"min (\d+) max (\d+) \| .*?min (\d+) max (\d+) \[itlb (.{4})\] \| .*?min (\d+) max (\d+)", l1)
    vals = [int(m.group(1)), int(m.group(2)), int(m.group(3)), int(m.group(4)), pack(m.group(5)), int(m.group(6)), int(m.group(7))]
    # [5] "p6 not in UTLB" 13: the speculative ITLB miss that also misses the UTLB costs 2 on the first call after the
    # setup and 4 on every later one [HW tools/hw/mmu/r5warm Q: 11 then 13 x 15 in mmutest3 V's order, 13 x 16 in this
    # R2 order, both repeated], and mmutest3 V counts 2..3 IC misses and 6 IC-miss freeze cycles per call for it: the
    # I-cache's state (M2)
    reqs = [("timing",), ("timing",), ("timing", "ic"), ("timing", "ic"), ("fetch",), ("timing", "ic"), ("timing",)]
    masks = [FULL, 0, FULL, 0, FULL, FULL, 0]
    for j in range(7):
        rec("R2", j, vals[j], masks[j], requires=reqs[j], source="run2 R2 rts" + (" (max: noise)" if not masks[j] else ""))
    m = re.search(r"min (\d+) max (\d+) \[itlb (.{4})\] \| p12 dropped min (\d+) max (\d+) \| p11 dropped min (\d+) max (\d+)", l2)
    vals = [int(m.group(1)), int(m.group(2)), pack(m.group(3)), int(m.group(4)), int(m.group(5)), int(m.group(6)), int(m.group(7))]
    reqs = [("timing",), ("timing",), (), ("timing", "ic"), ("timing", "ic"), ("timing", "ic"), ("timing", "ic")]
    masks = [FULL, 0, FULL, FULL, 0, FULL, 0]
    for j in range(7):
        rec("R2", 7 + j, vals[j], masks[j], requires=reqs[j], source="run2 R2 straight line" + (" (max: noise)" if not masks[j] else ""))
    br = re.search(r"bsr (\w+) jsr (\w+) jsr\(ill\) (\w+)", find(r2, "R3 branch addresses")[0])
    bsr, jsr, jsri = (int(br.group(k), 16) for k in (1, 2, 3))
    rows = [l for l in r2 if l.startswith("R3 ") and "exc " in l]
    assert len(rows) == 5
    at = [bsr, jsr, jsr, jsri, jsr]
    for k, l in enumerate(rows):
        m = re.search(r"exc (\d+) ev (\w+) spc-branch ([+-]\d+) PR (\w+)", l)
        spc = (at[k] + int(m.group(3))) & 0xFFFFFFFF
        src = "run2 " + l[:34].strip()
        rec("R3n", k, int(m.group(1)), source=src)
        rec("R3ev", k, int(m.group(2), 16), source=src)
        rec("R3spc", k, int(m.group(3)), 0 if k == 4 else FULL, source=src + " (spc - branch)")
        rec("R3pr", k, int(m.group(4), 16) - (at[k] + 4), source=src + " (PR - (branch + 4), branch from 'R3 branch addresses')")
        rec("R3abs", k, spc, FULL if k == 4 else 0, source=src + " (the absolute SPC: the jsr target VA(CODE(15)))")
    v = re.findall(r" (\d+)(?=  |$)", find(r2, "R4 URC:")[0].split(":", 1)[1])
    assert len(v) == 8, v
    for j, x in enumerate(v):
        rec("R4", j, int(x), source="run2 R4")
    # R5jsr n0 / n1 (000 / 040 here) depend on the I-cache's state [HW tools/hw/mmu/r5warm J, M2]: with the probe's
    # lines cold and the slot in the next line both are 040, cold with the slot in the JSR's line 000 / 040, warm both
    # 000 (n2 is 040 in every state); this run's order left each probe in its own state.  The RTL fetches the JSR target after the MMUCR store has landed whatever n <= 2 is (040): open (SOC.md 9.21)
    for tag, nm, req in (("R5 MMUCR AT 0->1, n instr, load", "R5on", ("pipeline",)), ("R5 MMUCR AT 1->0", "R5off", ()),
                         ("R5 MMUCR AT 0->1, n instr, jsr", "R5jsr", ("fetch", "pipeline")),
                         ("R6 LDTLB", "R6l", ()), ("R6 UTLB aa write", "R6u", ())):
        v = re.findall(r"n(\d+):(\w+)", find(r2, tag)[0])
        for n, x in v:
            rq = req + ("open",) if nm == "R5jsr" and int(n) <= 1 else req
            rec(nm, int(n), int(x, 16), requires=rq,
                source="run2 " + tag[:24] + (" (I-cache state dependent: r5warm J, SOC.md 9.21)" if "open" in rq else ""))
    m = re.search(r"exc (\d+) ev (\w+); then mapped \+ PREF: exc (\d+) ev (\w+); memory:((?: \w+)+)", find(r2, "R7 SQ")[0])
    for j, x in enumerate([int(m.group(1)), int(m.group(2), 16), int(m.group(3)), int(m.group(4), 16)]):
        rec("R7", j, x, source="run2 R7")
    for j, x in enumerate(m.group(5).split()):
        rec("R7mem", j, int(x, 16), requires=("sq",), source="run2 R7 memory")

    # ---------------------------------------------------------------- run3: V P M
    rows = [l for l in r3 if l.startswith("V ")]
    assert len(rows) == 11
    for vi, l in enumerate(rows):
        src = "run3 " + l[:44].strip()
        m = re.search(r"cyc (\d+)\.\.(\d+)", l)
        # the minimum over the 16 calls of a cached (C = 1) page is the MMU's own cost with the I-cache warm (the ITLB
        # array write that drops an entry forces its IC miss onto the NOPs before t_call's timed window): 9 / 16 / 15 /
        # 11 / 13 / 20 (M2); a C = 0 page adds the uncached fetch, the I-cache's (M3)
        rec("Vcyc", 2 * vi, int(m.group(1)), requires=("timing", "ic") if "C=0" in l else ("timing",), source=src + " (min)")
        rec("Vcyc", 2 * vi + 1, int(m.group(2)), 0, requires=("timing", "ic"), source=src + " (max: noise)")
        for mi, ev in enumerate(("ICmiss", "ICfillcyc", "frzICmiss", "ifetch")):
            m = re.search(ev + r" (\d+)\.\.(\d+)", l)
            rec("Vev", 8 * vi + 2 * mi, int(m.group(1)), requires=("pmc", "ic"), source=src + " " + ev + " min")
            rec("Vev", 8 * vi + 2 * mi + 1, int(m.group(2)), requires=("pmc", "ic"), source=src + " " + ev + " max")
    l = find(r3, "P OC address array")[0]
    v = re.findall(r"(?:purged|P0|P1) ([0-9a-f]{8})", l)
    assert len(v) == 6, v
    for j, x in enumerate(v):
        # 0: the index held another line before the purge; 5: another line's tag at the index (the unmapped PREF left
        # no line of its own), both KOS state
        rec("P", j, int(x, 16), 0 if j in (0, 5) else FULL, requires=("oc",), source="run3 P #%d" % j)
    m = re.findall(r"n(\d): (\w+) exc (\d+)", find(r3, "M AT 1->0")[0])
    for n, x, e in m:
        n = int(n)
        rec("Mv", n, int(x, 16), FULL if n == 3 else 0,
            source="run3 M n%d" % n + ("" if n == 3 else " (unrelated data on the silicon, not modelled: mmu 9 Q6)"))
        rec("Mn", n, int(e), source="run3 M n%d" % n)
    rec("END", 0, int(re.search(r"MMUCR (\w+)", find(r1, "mmutest: done")[0]).group(1), 16), source="run1 done")

    unexpected = [int(re.search(r"unexpected exceptions (\d+)", find(r, p)[0]).group(1))
                  for r, p in ((r1, "mmutest: done"), (r2, "mmutest2: done"), (r3, "mmutest3: done"))]
    ids = [r["id"] for r in RECS]
    assert len(set(ids)) == len(ids), "id collision"
    out = {
        "program": "tb/bm/mmu (build/bm_mmu/mmubm.bin, linked at 0x8C010000, entry 0x8C010000)",
        "results_pa": "0x0c0f0000",
        "layout": "u32 magic 0x4d4d5542, u32 count, u32 unexpected, u32 interrupts, then count x {u32 id, u32 value}",
        "id": "(fnv1a32(name) & 0xffffff) | (idx << 24)",
        "expect_unexpected": sum(unexpected),
        "expect_interrupts": 0,
        "count": len(RECS),
        "records": RECS,
    }
    json.dump(out, sys.stdout, indent=1)
    sys.stdout.write("\n")


def check(dump, skip):
    with open(os.path.join(HERE, "expect.json")) as f:
        exp = json.load(f)
    return report(compare(exp, open(dump, "rb").read(), skip), skip)


def compare(exp, data, skip):
    """(bad lines, records in the block, compared, skipped) of one result block against expect.json"""
    import struct
    magic, count, unexp, irqs = struct.unpack_from("<4I", data, 0)
    bad = []
    if magic != 0x4D4D5542:
        bad.append("magic %08x: the program did not finish" % magic)
    if unexp != exp["expect_unexpected"] or irqs != exp["expect_interrupts"]:
        bad.append("unexpected exceptions %d, interrupts %d" % (unexp, irqs))
    got = {}
    for i in range(count):
        rid, val = struct.unpack_from("<2I", data, 16 + 8 * i)
        if rid in got:
            bad.append("record id %08x twice" % rid)
        got[rid] = val
    ncmp = nskip = 0
    for r in exp["records"]:
        rid = int(r["id"], 16)
        if rid not in got:
            bad.append("%s[%d]: missing" % (r["name"], r["idx"]))
            continue
        v = got.pop(rid)
        mask = int(r["mask"], 16)
        if mask == 0 or set(r["requires"]) & skip:
            nskip += 1
            continue
        ncmp += 1
        if (v ^ int(r["expect"], 16)) & mask:
            bad.append("%s[%d] = %08x, console %s (mask %s; %s)" % (r["name"], r["idx"], v, r["expect"], r["mask"], r["source"]))
    for rid in got:
        bad.append("record id %08x not in expect.json" % rid)
    return bad, count, ncmp, nskip


def report(res, skip):
    bad, count, ncmp, nskip = res
    for b in bad[:60]:
        print("  " + b)
    print("tb/bm/mmu check: %d records, %d compared, %d skipped (mask 0 or requires %s), %d mismatches -> %s"
          % (count, ncmp, nskip, ",".join(sorted(skip)) or "-", len(bad), "FAIL" if bad else "PASS"))
    return 1 if bad else 0


def self_test(dump, skip):
    """the check must pass on `dump` and flag every compared record perturbed alone (its lowest compared bit
    flipped), naming that record; also a missing magic, an unexpected exception and a missing record (M2)"""
    import struct
    with open(os.path.join(HERE, "expect.json")) as f:
        exp = json.load(f)
    data = bytearray(open(dump, "rb").read())
    if compare(exp, bytes(data), skip)[0]:
        print("tb/bm/mmu self: the unperturbed block fails its own check")
        return 1
    count = struct.unpack_from("<I", data, 4)[0]
    where = {}
    for i in range(count):
        rid = struct.unpack_from("<I", data, 16 + 8 * i)[0]
        where[rid] = 16 + 8 * i + 4
    n = missed = 0
    for r in exp["records"]:
        mask = int(r["mask"], 16)
        if mask == 0 or set(r["requires"]) & skip:
            continue
        off = where[int(r["id"], 16)]
        bit = mask & -mask
        v = struct.unpack_from("<I", data, off)[0]
        struct.pack_into("<I", data, off, v ^ bit)
        bad = compare(exp, bytes(data), skip)[0]
        struct.pack_into("<I", data, off, v)
        n += 1
        if not (len(bad) == 1 and bad[0].startswith("%s[%d] =" % (r["name"], r["idx"]))):
            missed += 1
            if missed <= 10:
                print("  not flagged alone: %s[%d]" % (r["name"], r["idx"]))
    struct.pack_into("<I", data, 0, 0)                                     # no magic: the program did not finish
    ok_magic = bool(compare(exp, bytes(data), skip)[0])
    struct.pack_into("<I", data, 0, 0x4D4D5542)
    struct.pack_into("<I", data, 8, 1)                                     # an unexpected exception
    ok_unexp = bool(compare(exp, bytes(data), skip)[0])
    struct.pack_into("<I", data, 8, exp["expect_unexpected"])
    struct.pack_into("<I", data, 4, count - 1)                             # the last record missing
    ok_count = bool(compare(exp, bytes(data), skip)[0])
    ok = not missed and ok_magic and ok_unexp and ok_count
    print("tb/bm/mmu self: %d compared records perturbed, %d not flagged alone; magic %s, unexpected exception %s, "
          "missing record %s -> %s" % (n, missed, "flagged" if ok_magic else "MISSED", "flagged" if ok_unexp else "MISSED",
                                       "flagged" if ok_count else "MISSED", "PASS" if ok else "FAIL"))
    return 0 if ok else 1


if __name__ == "__main__":
    if len(sys.argv) > 1 and sys.argv[1] in ("check", "self"):
        sk = set()
        if "--skip" in sys.argv:
            sk = set(sys.argv[sys.argv.index("--skip") + 1].split(","))
        sys.exit((check if sys.argv[1] == "check" else self_test)(sys.argv[2], sk))
    main()
