#!/usr/bin/env python3
"""check.py -- the expected output of tb/bm/soc-adv/m3, the M3 verification's adversarial programs
(agents/reports/integration/M3-verify.md; its check_m3.py), the M3 gate's regression tests since M3-fix.md.

  check.py RUNDIR [--progs a,b]   RUNDIR/<prog>.log (the putchar stream) and RUNDIR/<prog>.status (soc_tb's stdout)
  check.py FILE.log --progs P     the program's own output only (the planted-bug runs of make v1-soc-mutants, whose
                                  soc_tb verdict the makefile reads)
  check.py --self RUNDIR          every pinned field perturbed alone must be flagged

Per program:
  * soc_tb's verdict: "soc_tb: PASS" (exit code 0, every testbench check -- among them tb/soc_tb_top.sv's protocol
    monitors, since M3-fix also the SDRAM command stream at the pins, the BSC strobes' bce rule, the DMAC port's hold, frz
    runs and arr_strobe, and no RTL %Error print) and "lockstep pass" -- for every program since M3-fix: ccrx_c1 (the
    IFILL order around a landed ICI, M3-verify F4) and accx_c1 (cached area 7, F5) failed lockstep before it, and rstx's
    boot stub met an open bank with its MRS (F6);
  * the monitors' summary: every violation count 0, and the SDRAM monitor saw commands and refreshes;
  * the program's "done" line, every "ok=" 1, every "errors=" / "bad=" 0 [T: the programs' model-free oracles: a shadow
    of the CPU-visible bytes (coh), a software replay of each DMA transfer (dmax), hm 4.2-4.5's cache-operation results
    (ccrx, seqx's one-entry reference cache), SOC.md 2.3's reset columns (rstx), identical images with and without
    interrupts (irqx), the new code running after ICI (smcx), SOC.md 3.1 / 4.2's access rules (accx)];
  * program-specific counts that show the program did its work (below).
"""
import re
import sys


def ge(n):
    return lambda v: int(v) >= n


def eq(x):
    return lambda v: v == x


# program -> list of (regex over the log, predicate on every match, what)
PROGS = {
    'coh':         [(r'COH total errors=(\d+)', eq('0'), 'no shadow mismatch'),
                    (r'COH phase=2 .* irqs=(\d+)', ge(5000), 'the TMU interrupt phase took >= 5000 interrupts')],
    'coh_c1':      [(r'COH total errors=(\d+)', eq('0'), 'no shadow mismatch')],
    'coh_c10002':  [(r'COH total errors=(\d+)', eq('0'), 'no shadow mismatch (8 lines on 2 OC entries)')],
    'coh_c20005':  [(r'COH total errors=(\d+)', eq('0'), 'no shadow mismatch'),
                    (r'COH rtcor=(\w+)', eq('0003'), 'the refresh storm: RTCOR 3 (a refresh every 48 Bcyc)')],
    'dmax':        [(r'DMAX summary xfers=(\d+) bad=0 mix_errors=0', ge(160), '>= 160 transfers, all replayed exactly')],
    'dmax_c10000': [(r'DMAX summary xfers=(\d+) bad=0 mix_errors=0', ge(160), '>= 160 transfers (the DMAC alone)')],
    'dmax_c20003': [(r'DMAX summary xfers=(\d+) bad=0 mix_errors=0', ge(160), '>= 160 transfers (burst mode only)')],
    'dmax_cc0007': [(r'DMAX summary xfers=(\d+) bad=0 mix_errors=0', ge(40), '>= 40 transfers under the refresh storm')],
    'ccrx':        [(r'CCRX summary cases=(\d+) bad=0', ge(230), '>= 230 cache-operation cases')],
    'ccrx_c1':     [(r'CCRX summary cases=(\d+) bad=0', ge(250), '>= 250 cases with G1 (ICI behind a cold routine)')],
    'rstx':        [(r'RSTX summary bad=(\d+)', eq('0'), 'every in-flight transaction completed'),
                    (r'RSTX boot phase=\d+ expevt=(020) ', None, '11 CPU-only resets (EXPEVT 0x020, SOC.md 2.3)')],
    'irqx':        [(r'IRQX compare .* ok=(\d)', eq('1'), 'identical images with and without interrupts'),
                    (r'IRQX run=1 .* irqs=(\d+)', ge(1000), '>= 1000 interrupts in the second run')],
    'accx':        [(r'ACCX summary cases=(\d+) bad=0', ge(30), '>= 30 access-rule cases')],
    'accx_c1':     [(r'ACCX summary cases=(\d+) bad=0', ge(33), '>= 33 cases with cached area 7 through P1 and P0'),
                    (r'ACCX (a7 p1 st then ld \(cached\) got=55aa55aa) want=55aa55aa ok=1', None,
                     '[S 4.2, C16] cached area 7 holds the stored line'),
                    (r'ACCX (ora0 st then ld got=99999999) want=99999999 ok=1', None, '[S 4.2] P0 area 7, ORA = 0')],
    'seqx':        [(r'SEQX summary seqs=(\d+) loads=\d+ bad=0', ge(1600), '>= 1600 back-to-back sequences')],
    'smcx':        [(r'SMCX summary cases=(\d+) bad=0', ge(36), '>= 36 self-modifying-code cases')],
}
COUNT = {'rstx': 11}                     # a spec with predicate None: the number of matches it needs (default 1)
DONE = {'coh': 'COH done', 'dmax': 'DMAX done', 'ccrx': 'CCRX done', 'rstx': 'RSTX done', 'irqx': 'IRQX done',
        'accx': 'ACCX done', 'seqx': 'SEQX done', 'smcx': 'SMCX done'}
MON = re.compile(r'^soc_tb: monitors: (\d+) SDRAM commands and (\d+) refreshes checked; violations MA (\d+) xl (\d+) '
                 r'P4 (\d+) fetch (\d+) bce (\d+) dmahs (\d+) frz (\d+) arr (\d+) SDRAM (\d+)$', re.M)


def base(p):
    return p.split('_c')[0]


def log_fields(p, log):
    """(field, ok, text) of the program's own output"""
    out = [('done', DONE[base(p)] in log, DONE[base(p)])]
    oks = re.findall(r'\bok=(\d)', log)
    out.append(('ok=', all(o == '1' for o in oks), f'{len(oks)} ok= fields, {oks.count("0")} zero'))
    bads = re.findall(r'\b(?:errors|bad)=(\d+)', log)
    out.append(('bad=', all(b == '0' for b in bads), f'{len(bads)} errors= / bad= fields'))
    for rx, fn, what in PROGS[p]:
        mm = re.findall(rx, log)
        if fn is None:
            n = COUNT.get(base(p), 1) if rx.startswith(r'RSTX boot') else 1
            out.append((rx, len(mm) >= n, f'{what}: {len(mm)} matches'))
        else:
            out.append((rx, bool(mm) and all(fn(x) for x in mm), f'{what}: {mm[:3]}'))
    return out


def status_fields(status):
    out = [('pass', bool(re.search(r'^soc_tb: PASS$', status, re.M)), 'soc_tb: PASS'),
           ('lockstep', bool(re.search(r'lockstep pass', status)), 'lockstep pass')]
    m = MON.findall(status)
    ok = bool(m) and int(m[-1][0]) > 0 and int(m[-1][1]) > 0 and all(int(x) == 0 for x in m[-1][2:])
    out.append(('monitors', ok, f'monitor summary {m[-1] if m else "missing"}'))
    e = re.findall(r'^soc_tb: RTL prints: (\d+) %Error', status, re.M)
    out.append(('rtl-prints', all(x == '0' for x in e), 'no RTL %Error print'))
    return out


def load(path):
    try:
        return open(path, errors='replace').read()
    except OSError:
        return None


def main():
    a = sys.argv[1:]
    self_test = '--self' in a
    if self_test:
        a.remove('--self')
    progs = list(PROGS)
    if '--progs' in a:
        i = a.index('--progs')
        progs = a[i + 1].split(',')
        del a[i:i + 2]
    if not a:
        print(__doc__)
        return 2
    single = a[0].endswith('.log')
    nf = nbad = 0
    for p in progs:
        if p not in PROGS:
            print(f'{p}: no such program')
            nbad += 1
            continue
        log = load(a[0] if single else f'{a[0]}/{p}.log')
        st = '' if single else load(f'{a[0]}/{p}.status')
        if log is None or st is None:
            print(f'{p:12s} MISSING')
            nbad += 1
            continue
        v = log_fields(p, log) + ([] if single else status_fields(st))
        bad = [x for x in v if not x[1]]
        nf += len(v)
        if not self_test:
            nbad += len(bad)
            print(f'{p:12s} {"PASS" if not bad else "FAIL"}  {len(v)} fields' +
                  ''.join(f'\n    FAIL {b[0]}: {b[2]}' for b in bad))
            continue
        # --self: every field's evidence perturbed alone must flip that field
        pert = [
            ('pass', lambda l, s: (l, s.replace('soc_tb: PASS', 'soc_tb: FAIL'))),
            ('lockstep', lambda l, s: (l, s.replace('lockstep pass', 'lockstep fail'))),
            ('monitors', lambda l, s: (l, re.sub(r' SDRAM 0$', ' SDRAM 1', s, flags=re.M))),
            ('rtl-prints', lambda l, s: (l, s + '\nsoc_tb: RTL prints: 1 %Error, 0 %Warning\n')),
            ('done', lambda l, s: (l.replace(' done', ' dxne'), s)),
            ('ok=', lambda l, s: (l.replace('ok=1', 'ok=0', 1), s)),
            ('bad=', lambda l, s: (re.sub(r'\b(errors|bad)=0', r'\1=3', l, count=1), s)),
        ]
        for name, f in pert:
            l2, s2 = f(log, st)
            if (l2, s2) == (log, st):
                continue
            v2 = dict((x[0], x[1]) for x in log_fields(p, l2) + ([] if single else status_fields(s2)))
            if v2.get(name, True):
                print(f'{p}: --self: perturbing {name} was not flagged')
                nbad += 1
        for rx, fn, what in PROGS[p]:                  # each program-specific count below its bound
            m = re.search(rx, log)
            if not m:
                continue
            l2 = log[:m.start(1)] + ('1' if fn is not None and m.group(1).isdigit() and m.group(1) != '1' else 'X') + \
                log[m.end(1):]
            if fn is None:
                l2 = re.sub(rx, 'removed', log)
            if dict((x[0], x[1]) for x in log_fields(p, l2)).get(rx, True):
                print(f'{p}: --self: perturbing {what!r} was not flagged')
                nbad += 1
    if self_test:
        print(f'soc-adv/m3 check.py --self: {"PASS" if nbad == 0 else "FAIL"} ({len(progs)} programs)')
    else:
        print(f'soc-adv/m3 check.py: {"PASS" if nbad == 0 else "FAIL"} ({len(progs)} programs, {nf} fields, {nbad} failing)')
    return 1 if nbad else 0


if __name__ == '__main__':
    sys.exit(main())
