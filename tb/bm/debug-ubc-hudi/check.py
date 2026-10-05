#!/usr/bin/env python3
"""check.py -- compare a bare-metal run's console output with expected/<prog>.json (mkexpected.py).

  check.py <output log> <expected.json> [--classes functional,timing[,cache]] [--quiet]
  check.py --self <expected.json> [--classes ...]     self-test: the console's own lines pass, and every perturbation
                                                       (one compared token changed, one line dropped) is caught

Lines are found by their key (the shortest unique token prefix) in log order; every token is compared except the
entry's "ignore" tokens; "var" tokens that differ are reported as variable, not failed; "cache" tokens (they need the
caches and the bus: M3, mkexpected.py) are compared only when --classes names "cache", and otherwise reported as
deferred.  Exit 0 when every selected line is present and equal.
"""
import json
import sys


def compare(out, doc, classes, quiet, report=True):
    """(equal, mismatched, missing, variable) of the log lines `out` against the expected document."""
    pos = 0
    ok = bad = miss = varn = 0
    defer = 'cache' not in classes
    for e in doc['lines']:
        if e['class'] not in classes:
            continue
        key = e['key']
        j = pos
        while j < len(out) and out[j].split()[:len(key)] != key:
            j += 1
        if j == len(out):
            miss += 1
            if report:
                print('MISSING  [%s] %s' % (e['class'], e['text']))
            continue
        pos = j + 1
        got, want = out[j].split(), e['text'].split()
        diffs, vd, dd = [], [], []
        for i in range(max(len(got), len(want))):
            g = got[i] if i < len(got) else '<none>'
            w = want[i] if i < len(want) else '<none>'
            if g == w or i in e['ignore']:
                continue
            if i in e['var']:
                vd.append('%s->%s' % (w, g))
            elif defer and i in e.get('cache', []):
                dd.append('%s->%s' % (w, g))
            else:
                diffs.append('%s->%s' % (w, g))
        if dd and report and not quiet and not diffs:
            print('deferred [cache] %s: %s' % (' '.join(key), ' '.join(dd)))
        if diffs:
            bad += 1
            if report:
                print('MISMATCH [%s] %s\n         got: %s\n         diff (expected->got): %s' %
                      (e['class'], e['text'], out[j], ' '.join(diffs)))
        else:
            ok += 1
            if vd:
                varn += 1
                if report and not quiet:
                    print('variable [%s] %s: %s' % (e['class'], ' '.join(key), ' '.join(vd)))
    return ok, bad, miss, varn


def perturb(tok):
    """a different token of the same shape (a changed digit / letter)"""
    for i in range(len(tok) - 1, -1, -1):
        c = tok[i]
        if c.isdigit():
            return tok[:i] + ('1' if c != '1' else '2') + tok[i + 1:]
        if c.isalpha():
            return tok[:i] + ('x' if c != 'x' else 'y') + tok[i + 1:]
    return tok + '!'


def selftest(doc, classes):
    sel = [e for e in doc['lines'] if e['class'] in classes]
    base = [e['text'] for e in doc['lines']]
    ok, bad, miss, _ = compare(base, doc, classes, True, report=False)
    fails = 0
    if bad or miss or ok != len(sel):
        print('self: FAIL: the console lines do not satisfy their own entries (%d equal of %d)' % (ok, len(sel)))
        fails += 1
    caught = tried = 0
    for n, e in enumerate(doc['lines']):
        if e['class'] not in classes:
            continue
        toks = e['text'].split()
        cand = [i for i in range(len(e['key']), len(toks)) if i not in e['ignore'] and i not in e['var'] and
                ('cache' in classes or i not in e.get('cache', []))]
        if cand:                                   # change one compared token of this line
            i = cand[len(cand) // 2]
            t = list(toks)
            t[i] = perturb(t[i])
            log = list(base)
            log[n] = ' '.join(t)
            tried += 1
            r = compare(log, doc, classes, True, report=False)
            caught += r[1] >= 1
        log = base[:n] + base[n + 1:]               # drop the line
        tried += 1
        r = compare(log, doc, classes, True, report=False)
        caught += r[2] >= 1
    print('self %s (%s): %d lines pass on the console text; %d / %d perturbations caught' %
          (doc['program'], ','.join(classes), ok, caught, tried))
    return 0 if not fails and caught == tried else 1


def main():
    a = sys.argv[1:]
    classes = ['functional', 'timing']
    quiet = '--quiet' in a
    if '--classes' in a:
        classes = a[a.index('--classes') + 1].split(',')
    if '--self' in a:
        doc = json.load(open(a[a.index('--self') + 1]))
        sys.exit(selftest(doc, classes))
    if len(a) < 2:
        sys.exit(__doc__)
    log, exp = a[0], a[1]
    out = [l.rstrip() for l in open(log, errors='replace').read().splitlines()]
    doc = json.load(open(exp))
    ok, bad, miss, varn = compare(out, doc, classes, quiet)
    print('check %s (%s): %d equal, %d mismatched, %d missing, %d with varying console values' %
          (doc['program'], ','.join(classes), ok, bad, miss, varn))
    sys.exit(1 if bad or miss else 0)


if __name__ == '__main__':
    main()
