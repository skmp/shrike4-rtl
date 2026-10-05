#!/usr/bin/env python3
# check.py -- compare a serial probe's output with its expected/<prog>.json (WI-SER; README.md).
#
#   check.py <output log> expected/<prog>.json [--classes functional,trace,diff,env] [--quiet]
#
# The expected lines are the console's, with the `rtl` line in place of each `diff` / `env` entry (a null `rtl` is a
# line the RTL does not print).  The output is aligned to them by line shape (digits masked), then compared token by
# token (the tokenizer of mkexpected.py):
#   functional / diff / env   every token exactly
#   trace                     every token except the timing ones (`t`)
#   timing                    the timing tokens within `tol`, the others exactly
# The classes named by --classes gate the exit code (default: functional,trace,diff,env -- what a functional replay
# must reproduce; v1_soc adds timing, and cache with the I-cache, rtl/v1/SOC.md 8.3 M3); the others are reported.  A
# line of a gating class that is missing or extra fails.  Exit 0 when every gating line matches.
# Poll-phase rules (mkexpected.py): in a `poll1` block a trace row that lasts one poll (the next row starts where it
# ends) is optional on both sides, and the block's "N events" count is compared without those rows; an `alt` line may
# match one of its alternatives instead; an E1 line (`e1pair` / `e1tend`) is checked per repetition against the set
# of the console's repetitions (the start inside their range widened by the tolerance, the frame's or TEND's interval
# within the tolerance of one of theirs); an E2 / E4 line (`erange`, M3-fix) the same way per series.
# --kos (M5, WI-SOC; SOC.md 8.3 M5): the console's own ELF unmodified under KOS on the board with the dcload stand-in
# (soc_tb --dcload): what misses because its console value carries a cold first pass (class `cache`, M1b / M3) or a poll
# phase the testbench does not reproduce (KOS_DOC's sections) is reported "documented" instead of failing -- token-
# precise and bounded (M5-fix, M5-verify F2); every other gated line must match:
#   * a `cache` line that is not a poll trace (S5's costs; S15's FIFO dump): its `t` tokens free, each within
#     console / 2 - tol .. 2 x console + tol, every other token exact (the FIFO dump has none: all of it compared);
#   * a poll trace of KOS_DOC's sections (the indented rows under the section's header; S15's rows are class `cache`):
#     the rows as a whole, by register -- the sequence of values each field takes (SCFDR2's transmit and receive counts
#     separately, SCFSR2, SCSPTR2, the line-status word; repeats collapsed) must equal the console's.  Which values
#     share one poll row, and how many rows a change takes, is the poll phase: a poll whose two reads (SCFDR2, then
#     SCFSR2) straddle a byte's arrival shows the new flags with the old count, one row more or less.  The trace's end
#     time is bounded as a `t` token (2 x), its row count by 2 x, and a header's "N events" must be the run's own row
#     count (the console's N is compared against the console's rows the same way).
import argparse, difflib, json, re, sys

KOS_DOC = {
    ('scif_probe', 'S6c'): 'phase: the TEND-clearing poll trace splits into one event more on rm (M3: "serial phase '
                           'effects seen only on rm (an S6c split)"; the grafted run and this one alike)',
    ('scif_probe', 'S15'): 'cache: the MCE + loop-back flow-control poll trace ran cold on the console (class cache, M1b / '
                           'M3): its row times and the poll each count change lands in are the cold pass\'s',
    ('scif_probe2', 'E6'): 'phase: a poll whose two reads (SCFDR2, then SCFSR2) straddle a byte\'s arrival shows the new '
                           'flags with the old count -- a one-poll row (0200/0062 between 0200/0060 and 0201/0062) the '
                           'console\'s poll phase did not catch (M5 gate run; batch 1 and M3\'s graft matched)',
}

SEP = re.compile(r'[\s\[\]\(\):,|/+]+|(?<=\d)-(?=\s*\d)')


def tokens(line):
    return [x for x in SEP.split(line) if x]


def shape(line):
    return re.sub(r'\d+', '#', line.strip())


TRACE_ROW = re.compile(r'^\s+(\d+)-\s*(\d+) fdr ')
EVENTS = re.compile(r': (\d+) events, ')


def poll1_filter(lines, text, sects, firsts, retext):
    """drop the one-poll trace rows of the `sects` blocks (a row whose next row starts where it ends), except a block's
    first row (its initial state) unless the block's header is in `firsts`; a dropped row's start becomes the next
    row's (the change began in its window: retext(item, new text, widened by) makes that item).  Return the kept items
    and, per kept "N events" header, the number of rows its block lost (index into the kept list)"""
    keep, lost, sect, head, first, carry = [], {}, '', None, False, None
    for i, x in enumerate(lines):
        s = text(x)
        if not s.startswith(' '):
            sect = s.split(' ')[0].rstrip(':')
        m = TRACE_ROW.match(s)
        if not m:
            head = len(keep) if (sect in sects and EVENTS.search(s)) else None
            first = sect in sects and any(s.startswith(f) for f in firsts)
            keep.append(x)
            carry = None
            continue
        is_first = i == 0 or not TRACE_ROW.match(text(lines[i - 1]))
        if carry is not None:                                    # the rows dropped before this one began the change
            lo, wid = carry
            x = retext(x, s[:m.start(1)] + ' ' * max(0, len(m.group(1)) - len(lo)) + lo + s[m.end(1):], wid)
            s = text(x)
            m = TRACE_ROW.match(s)
            carry = None
        if sect in sects and i + 1 < len(lines) and (not is_first or first):
            n = TRACE_ROW.match(text(lines[i + 1]))
            if n and n.group(1) == m.group(2):
                if head is not None:
                    lost[head] = lost.get(head, 0) + 1
                carry = (m.group(1), int(m.group(2)) - int(m.group(1)))
                continue
        keep.append(x)
    return keep, lost


def trace_rows(lines):
    """(M5-fix, --kos) a poll trace's rows: [{'t0', 't1', field: value}] from the named form ('  a- b fdr X fsr X sptr X
    lsr X' / 'fed N rd N') or the packed one ('  a-b:FDR/FSR a-b:FDR/FSR ...'); None when a line is neither"""
    rows = []
    for s in lines:
        tk = tokens(s)
        if len(tk) >= 4 and tk[2] == 'fdr':
            r = {'t0': tk[0], 't1': tk[1]}
            r.update(zip(tk[2::2], tk[3::2]))
            rows.append(r)
        elif tk and len(tk) % 4 == 0 and all(x.isdigit() for x in tk[0::4] + tk[1::4]):
            rows += [{'t0': tk[k], 't1': tk[k + 1], 'fdr': tk[k + 2], 'fsr': tk[k + 3]} for k in range(0, len(tk), 4)]
        else:
            return None
    return rows


def field_seqs(rows):
    """each register field's sequence of values, repeats collapsed (SCFDR2: transmit / receive counts apart)"""
    out = {}
    for r in rows:
        for k, v in r.items():
            if k in ('t0', 't1', 'fed', 'rd'):                 # times and the program's own counters: the poll phase
                continue
            parts = [(k + '.tx', v[:2]), (k + '.rx', v[2:])] if k == 'fdr' else [(k, v)]
            for kk, vv in parts:
                q = out.setdefault(kk, [])
                if not q or q[-1] != vv:
                    q.append(vv)
    return out


def minus_events(s, k):
    """the header line with its event count lowered by k"""
    if not k:
        return s
    return EVENTS.sub(lambda m: ': %d events, ' % (int(m.group(1)) - k), s, count=1)


def main():
    ap = argparse.ArgumentParser(description='compare a serial probe log with expected/<prog>.json')
    ap.add_argument('log')
    ap.add_argument('expected')
    ap.add_argument('--classes', default='functional,trace,diff,env')
    ap.add_argument('--quiet', action='store_true')
    ap.add_argument('--kos', action='store_true', help='M5: the unmodified console ELF under KOS (KOS_DOC above)')
    ap.add_argument('--trace-times', action='store_true',
                    help='also compare the trace rows\' Icyc columns (they carry the poll phase and the free-running '
                         'base-tick phase at each write: reported, not gated, by default)')
    a = ap.parse_args()
    gate, quiet, trace_times = set(a.classes.split(',')), a.quiet, a.trace_times
    doc = json.load(open(a.expected))
    want = []
    for e in doc['lines']:
        if e['class'] in ('diff', 'env'):
            if e['rtl'] is None:
                continue
            want.append(dict(e, text=e['rtl']))
        else:
            want.append(e)
    got = [l.rstrip() for l in open(a.log, errors='replace').read().splitlines()]
    # the program's own lines only (a SoC log may carry more around them)
    head = doc['lines'][0]['text']
    if head in got:
        got = got[got.index(head):]
    tail = [i for i, l in enumerate(got) if l == doc['program'] + ' done']
    if tail:
        got = got[:tail[0] + 1]
    # the poll1 blocks: one-poll rows out of both sides, the event counts of their headers lowered to match
    sects, firsts = set(), set()
    sect = ''
    for e in want:
        if not e['text'].startswith(' '):
            sect = e['text'].split(' ')[0].rstrip(':')
        if e.get('poll1'):
            sects.add(sect)
        if e.get('poll1_first'):
            firsts.add(e['text'].split(':')[0])
    if sects:
        def rw(e, t, wid):                                   # a merged row: its time tolerances widened by the drop
            return dict(e, text=t, tol=[x + wid for x in e['tol']])
        want, lw = poll1_filter(want, lambda e: e['text'], sects, firsts, rw)
        want = [dict(e, text=minus_events(e['text'], lw.get(i, 0))) for i, e in enumerate(want)]
        got, lg = poll1_filter(got, lambda g: g, sects, firsts, lambda g, t, wid: t)
        got = [minus_events(g, lg.get(i, 0)) for i, g in enumerate(got)]

    stats = {}
    fails = []
    documented = []
    sect_of, cur = [], ''                                        # each expected line's section (its header's name)
    for e in want:
        if not e['text'].startswith(' '):
            cur = e['text'].split(' ')[0].rstrip(':')
        sect_of.append(cur)

    def cache_doc(e, g):
        """(--kos) a `cache` line that missed: its t tokens within their bound, every other token exact"""
        if g is None or 't' not in e:
            return False
        te, tg = tokens(e['text']), tokens(g)
        if len(te) != len(tg):
            return False
        tt = dict(zip(e['t'], e['tol']))
        for i, (x, y) in enumerate(zip(te, tg)):
            if i in tt:
                if not (x.isdigit() and y.isdigit() and int(x) / 2 - tt[i] <= int(y) <= 2 * int(x) + tt[i]):
                    return False
            elif x != y:
                return False
        return True

    def note(cls, ok, e, g, i=None):
        s = stats.setdefault(cls, [0, 0, 0])
        s[0] += 1
        s[1 if ok else 2] += 1
        if not ok and cls in gate:
            if a.kos and cls == 'cache' and cache_doc(e, g):
                documented.append((e, g, 'cache: a cold first pass on the console (class cache, M1b / M3); its times '
                                   'only, within 2 x'))
                return
            fails.append((e, g))

    def e1(e, g, gprev):
        # E1 per repetition against the set of the console's (mkexpected.py `e1`): numbers from token 6 (pair: lo1 hi1
        # lo2 hi2 per repetition) or 1 (TEND: lo hi, its interval from the repetition's T->0 on the line before)
        te, tg = tokens(e['text']), tokens(g)
        pair = e['mode'] == 'e1pair'
        b, w = (6, 4) if pair else (1, 2)
        if len(te) != len(tg) or not all(x.isdigit() for x in tg[b:]):
            return False
        if te[:b] != tg[:b]:
            return False
        tol = max(e['tol']) if e['tol'] else 0
        bit = int(te[5]) if pair else int(tokens(e['pair'])[5])
        tick = max(tol, bit // 16)                           # the free-running base tick: the start's phase range
        n = (len(te) - b) // w
        cs = [int(te[b + w * k]) for k in range(n)]
        gs = [int(tg[b + w * k]) for k in range(n)]
        if pair:
            ci = [int(te[b + w * k + 2]) - cs[k] for k in range(n)]
            gi = [int(tg[b + w * k + 2]) - gs[k] for k in range(n)]
        else:
            pe, pg = tokens(e['pair']), tokens(gprev or '')
            if len(pe) != len(pg) or len(pg) < 6 + 4 * n:
                return False
            ci = [cs[k] - int(pe[6 + 4 * k]) for k in range(n)]
            gi = [gs[k] - int(pg[6 + 4 * k]) for k in range(n)]
        itol = tol if pair else tick                         # TEND - T->0: the console's own spread is a base tick
        for k in range(n):
            if not (min(cs) - tick <= gs[k] <= max(cs) + tick):
                return False
            if not any(abs(gi[k] - c) <= itol for c in ci):
                return False
        return True

    def erange(e, g):
        # E2 / E4 per repetition against the set of the console's (mkexpected.py `erange`): the (lo, hi) pairs of the
        # line in two series -- E2 'blocks' (4 R->1, then 4 TEND), E4 'inter' (R push, DR per repetition) --; every lo
        # inside the console's range of its series widened by max(tol, bit / 16), every width (hi - lo) within tol of
        # the console's, the second series' interval from the first within that of one console repetition
        te, tg = tokens(e['text']), tokens(g)
        if len(te) != len(tg) or [x for x in te if not x.isdigit()] != [x for x in tg if not x.isdigit()]:
            return False
        tol = max(e['tol']) if e['tol'] else 0
        tick = max(tol, e['erange']['bit'] // 16)
        ne = [int(te[i]) for i in e['t']]
        ng = [int(tg[i]) for i in e['t']]
        pe, pg = list(zip(ne[0::2], ne[1::2])), list(zip(ng[0::2], ng[1::2]))
        n = len(pe) // 2
        if e['erange']['layout'] == 'blocks':
            ser = lambda p: (p[:n], p[n:])
        else:
            ser = lambda p: (p[0::2], p[1::2])
        (ea, eb), (ga, gb) = ser(pe), ser(pg)
        for es, gs in ((ea, ga), (eb, gb)):
            cs = [x[0] for x in es]
            for (lo, hi), (clo, chi) in zip(gs, es):
                if not (min(cs) - tick <= lo <= max(cs) + tick) or abs((hi - lo) - (chi - clo)) > tol:
                    return False
        ci = [b[0] - a[0] for a, b in zip(ea, eb)]
        return all(any(abs((b[0] - a[0]) - c) <= tick for c in ci) for a, b in zip(ga, gb))

    def compare(e, g, gprev=None):
        if e.get('mode') in ('e1pair', 'e1tend') and 'timing' in gate:
            return e1(e, g, gprev)
        if e.get('mode') == 'erange' and 'timing' in gate:
            return erange(e, g)
        if e.get('alt') and any(compare(dict(e, text=a, alt=None), g) for a in e['alt']):
            return True
        cls = e['class']
        te, tg = tokens(e['text']), tokens(g)
        if len(te) != len(tg):
            return False
        tt = dict(zip(e['t'], e['tol']))
        for i, (a, b) in enumerate(zip(te, tg)):
            if i in tt:
                if cls in ('functional', 'diff', 'env'):
                    continue                                  # a time inside a functional line: not compared here
                if cls == 'trace' and not trace_times:
                    continue
                if not (a.isdigit() and b.isdigit() and abs(int(a) - int(b)) <= tt[i]):
                    return False
            elif a != b:
                return False
        return True

    def extra(i, g):
        # an output line with no expected line: a row more in a poll trace belongs to that trace's class
        near = [want[k]['class'] for k in (i - 1, i) if 0 <= k < len(want)]
        cls = 'cache' if 'cache' in near else 'timing' if 'timing' in near else 'trace' if 'trace' in near else 'functional'
        note(cls, False, {'class': cls, 'text': '(no expected line)', 'n': 0}, g, i - 1)

    if a.kos:                                                # KOS_DOC's poll traces, by register (see the usage)
        def block(texts, sect):
            h = next((k for k, s in enumerate(texts) if not s.startswith(' ') and
                      s.split(' ')[0].rstrip(':') == sect), None)
            if h is None:
                return None, []
            k = h + 1
            while k < len(texts) and texts[k].startswith(' '):
                k += 1
            return h, list(range(h + 1, k))

        for (prog, sect), why in KOS_DOC.items():
            if prog != doc['program']:
                continue
            wh, wi = block([e['text'] for e in want], sect)
            gh, gi = block(got, sect)
            if wh is None or not wi:
                continue
            cls = want[wi[0]]['class']
            if cls not in gate:
                continue
            same = gh is not None and len(gi) == len(wi) and all(compare(want[x], got[y]) for x, y in zip(wi, gi))
            wr = trace_rows([want[x]['text'] for x in wi])
            gr = trace_rows([got[y] for y in gi]) if gh is not None else None
            head_n = lambda s: (EVENTS.search(s) and int(EVENTS.search(s).group(1)))
            bad = []
            if not same:
                if wr is None or gr is None or not gr:
                    bad.append('the trace rows do not parse')
                else:
                    ws, gs = field_seqs(wr), field_seqs(gr)
                    for k in sorted(set(ws) | set(gs)):
                        if ws.get(k) != gs.get(k):
                            bad.append('%s went %s, console %s' % (k, ' '.join(gs.get(k, [])), ' '.join(ws.get(k, []))))
                    tol = max(max(want[x]['tol'] or [0]) for x in wi)
                    if not (0 <= int(gr[-1]['t1']) <= 2 * int(wr[-1]['t1']) + tol):
                        bad.append('the trace ends at %s, console %s (bound 2 x + %d)' % (gr[-1]['t1'], wr[-1]['t1'], tol))
                    if not len(wr) / 2 <= len(gr) <= 2 * len(wr):
                        bad.append('%d rows, console %d (bound 2 x)' % (len(gr), len(wr)))
            if gh is not None and head_n(want[wh]['text']) is not None and head_n(got[gh]) is not None and \
                    head_n(got[gh]) != head_n(want[wh]['text']):
                if gr is None or head_n(got[gh]) != len(gr):
                    bad.append('the header counts %d events, its rows %s' % (head_n(got[gh]), len(gr) if gr else '?'))
                    same = False
                elif not bad:
                    got[gh] = EVENTS.sub(': %d events, ' % head_n(want[wh]['text']), got[gh], count=1)
                    documented.append((want[wh], got[gh], why + ' (the header\'s event count: the rows\')'))
            st = stats.setdefault(cls, [0, 0, 0])
            st[0] += len(wi)
            st[1 if same else 2] += len(wi)
            if not same and not bad:
                documented.append(({'class': cls, 'n': want[wi[0]]['n'], 'text': '%s: %d poll-trace rows (the run: %d)'
                                    % (sect, len(wr), len(gr))}, None, why + '; every register\'s value sequence equal'))
            elif bad:
                fails.append(({'class': cls, 'n': want[wi[0]]['n'], 'text': '%s poll trace, by register: %s'
                               % (sect, '; '.join(bad))}, ' | '.join(got[y].strip() for y in gi)[:150] if gi else None))
            gset = set(gi)
            want = [e for x, e in enumerate(want) if x not in set(wi)]
            sect_of = [s for x, s in enumerate(sect_of) if x not in set(wi)]
            got = [g for y, g in enumerate(got) if y not in gset]

    sm = difflib.SequenceMatcher(None, [shape(e['text']) for e in want], [shape(g) for g in got], autojunk=False)
    for op, i1, i2, j1, j2 in sm.get_opcodes():
        if op in ('equal', 'replace'):
            for k in range(max(i2 - i1, j2 - j1)):
                e = want[i1 + k] if i1 + k < i2 else None
                g = got[j1 + k] if j1 + k < j2 else None
                if e is None:
                    extra(i2, g)
                elif g is None:
                    note(e['class'], False, e, None, i1 + k)
                else:
                    note(e['class'], compare(e, g, got[j1 + k - 1] if j1 + k > 0 else None), e, g, i1 + k)
        elif op == 'delete':
            for k, e in enumerate(want[i1:i2]):
                note(e['class'], False, e, None, i1 + k)
        elif op == 'insert':
            for g in got[j1:j2]:
                extra(i1, g)

    name = doc['program']
    summ = ' '.join('%s %d/%d' % (c, s[1], s[0]) for c, s in sorted(stats.items()))
    ok = not fails
    for e, g, why in documented:
        print('  documented (--kos): %-8s console %3d: %s -- %s' % (e['class'], e['n'], e['text'][:90], why))
    if a.kos:
        print('%s: %d lines / traces documented (--kos: class cache, KOS_DOC sections)' % (name, len(documented)))
    print('%s: %s  [gate: %s] %s' % (name, summ, ','.join(sorted(gate)), 'PASS' if ok else 'FAIL'))
    if not quiet or not ok:
        for e, g in fails[:40]:
            print('  %-10s console %3d: %s' % (e['class'], e['n'], e['text'][:150]))
            if e['class'] in ('diff', 'env') and e.get('why'):
                print('  %10s   (%s)' % ('', e['why'][:140]))
            print('  %-10s      got: %s' % ('', g[:150] if g is not None else '(missing)'))
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
