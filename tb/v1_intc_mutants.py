#!/usr/bin/env python3
# v1_intc_mutants.py -- shows that WI-EXC's unit testbench (tb/v1_intc_tb.{sv,cpp}) can fail: each mutant plants one
# bug in a copy of rtl/v1/v1_intc.sv or rtl/v1/v1_excregs.sv (the original files are never touched), builds the
# testbench on the copy and requires it to report FAIL.  The mutants cover the checks that carry measured facts:
# the IMASK synchroniser (depth, clock, output register), the raise compare (SOC.md 3.4: the output register's level
# against the Iclk input register's IMASK), the resolver's tie order and IPR fields, the register masks, the IRL noise
# canceller, the NMI rules, standby sampling and the exception-register routing.
#   usage: tb/v1_intc_mutants.py [--build DIR] [--verilator verilator] [--jobs N]   (make v1-intc-mutants)
import argparse, concurrent.futures, os, shutil, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
RTL = os.path.join(ROOT, 'rtl', 'v1')

# (name, file, old text, new text[, more (file, old, new) edits]): each `old` must occur exactly once
MUTANTS = [
    ('imask: parity flipped (the Iclk register moved from the input to the output: still 9 / 10)', 'v1_intc.sv',
     'im1 <= im_i; im2 <= im1;', 'im1 <= sr_imask; im2 <= im1;',
     [('v1_intc.sv', 'rq <= r2_req; rc <= r2_code;', 'rq0 <= r2_req; rq <= rq0; rc <= r2_code;'),
      ('v1_intc.sv', 'logic        r2_req, rq, rq_live, mai_mask;', 'logic        r2_req, rq, rq0, rq_live, mai_mask;')]),
    ('imask: no Iclk input register (8 / 9)', 'v1_intc.sv',
     'im1 <= im_i; im2 <= im1;', 'im1 <= sr_imask; im2 <= im1;'),
    ('imask: first synchroniser stage on pce (4 phases)', 'v1_intc.sv',
     'im1 <= im_i; im2 <= im1;', 'if (ce.pce) im1 <= im_i; im2 <= im1;'),
    ('imask: one more synchroniser stage (11 / 12)', 'v1_intc.sv',
     'im3 <= im2;                                            // R1',
     'im3 <= im2x; im2x <= im2;                                            // R1'),
    ('output: no Iclk output register', 'v1_intc.sv',
     'assign rq_live = rq && rl > im_i;', 'assign rq_live = r2_req && r2_lvl > im_i;'),
    ('resolver: ties to the lower order (and on-chip over IRL)', 'v1_intc.sv',
     "if (s[i] && !(no_iti && i == 2) && lv > bl)", "if (s[i] && !(no_iti && i == 2) && lv >= bl)"),
    ('resolver: ITI on IPRB[11:8]', 'v1_intc.sv',
     '2:                  return b[15:12];', '2:                  return b[11:8];'),
    ('ICR bit 0 not stored', 'v1_intc.sv', 'icr_b0 <= pq.wdata[0];', 'icr_b0 <= 1\'b0;'),
    ('IRL: two-sample noise canceller', 'v1_intc.sv',
     'assign irl_st = ~(irl2 ^ irl_h1) & ~(irl_h1 ^ irl_h2);', 'assign irl_st = ~(irl2 ^ irl_h1);'),
    ('IRL: IRLM = 1 filtered as one vector (a toggling IRL3 holds back IRL0)', 'v1_intc.sv',
     'if (icr_irlm)          irl_f <= (irl_f & ~irl_st) | (irl2 & irl_st);', 'if (icr_irlm && &irl_st) irl_f <= irl2;'),
    ('NMI: nmi_ack keeps NMIB', 'v1_intc.sv',
     "if (nmi_ack) icr_nmib <= 1'b0;", "if (1'b0) icr_nmib <= 1'b0;"),
    ('NMI: no NMIE blind window', 'v1_intc.sv', "nblank <= 3'd6;", "nblank <= 3'd0;"),
    ('NMI: MAI = 1 in standby still latches a pin transition', 'v1_intc.sv',
     "nblank == 3'd0 && !(stby && icr_mai);", "nblank == 3'd0;"),
    ('NMI: an NMIB write wins over nmi_ack', 'v1_intc.sv',
     "if (wr16 && sel_icr) begin", "if (nmi_ack) icr_nmib <= 1'b0;\n      if (wr16 && sel_icr) begin",
     [('v1_intc.sv', "      if (nmi_ack) icr_nmib <= 1'b0;\n    end", "    end")]),
    ('NMI: BL ignored', 'v1_intc.sv',
     'irq.req  = ns ? (!sr_bl || icr_nmib || asleep || stby) : rq_live;',
     'irq.req  = ns ? 1\'b1 : rq_live;'),
    ('MAI ignored', 'v1_intc.sv', 'assign mai_mask = icr_mai && !n2;', "assign mai_mask = 1'b0;"),
    ('standby: IRL sampled every cycle', 'v1_intc.sv',
     'assign irl_ce = stby ? rtc_tick : ce.bce;', "assign irl_ce = stby ? 1'b1 : ce.bce;"),
    ('wake ignores IMASK', 'v1_intc.sv', 'wake     = rq_live || ns;', "wake     = r1_lvl != 4'd0 || ns;"),
    ('wake one cycle early (from R2, in the cycle after a bce)', 'v1_intc.sv', 'wake     = rq_live || ns;',
     'wake     = r2_req || ns;'),
    ('wake: not masked by an IMASK raise (the synchronised request only)', 'v1_intc.sv', 'wake     = rq_live || ns;',
     'wake     = rq || ns;'),
    ('imask: a delayed raise (through the synchroniser like the lowering, s + 9 / 10: the pre-I014 v1_intc)',
     'v1_intc.sv', 'assign rq_live = rq && rl > im_i;', 'assign rq_live = rq;'),
    ('imask: the raise compare on the live sr_imask (no input register: gone in s, not s + 1)', 'v1_intc.sv',
     'assign rq_live = rq && rl > im_i;', 'assign rq_live = rq && rl > sr_imask;'),
    ('imask: the raise compare against the synchronised mask (im3: gone at s + 5 .. 8)', 'v1_intc.sv',
     'assign rq_live = rq && rl > im_i;', 'assign rq_live = rq && rl > im3;'),
    ('imask: the raise compare\'s level one stage early (R1\'s, not the output register\'s)', 'v1_intc.sv',
     'rl <= r2_lvl;', 'rl <= r1_lvl;'),
    ('code held while req (the package wording)', 'v1_intc.sv', 'rq <= r2_req; rc <= r2_code;',
     'rq <= r2_req; if (!rq) rc <= r2_code;'),
    ('excregs: an interrupt also writes EXPEVT', 'v1_excregs.sv',
     'assign xw_exp = excw.v && !excw.intr;', 'assign xw_exp = excw.v;'),
    ('excregs: an excw pulse drops a bus write to another register', 'v1_excregs.sv',
     'else if (wr32 && sel_tra)  tra    <= pq.wdata[9:2];', 'else if (wr32 && sel_tra && !excw.v) tra <= pq.wdata[9:2];'),
    ('excregs: a bus write beats excw on the same register', 'v1_excregs.sv',
     'if (xw_int)                intevt <= excw.code;', 'if (xw_int && !(wr32 && sel_int)) intevt <= excw.code;'),
    ('excregs: TRA mask 0x3FF', 'v1_excregs.sv',
     "if (sel_tra) pr.rdata = {22'd0, tra, 2'b00};", "if (sel_tra) pr.rdata = {22'd0, tra, pq.wdata[1:0] | 2'b11};"),
    ('excregs: manual reset EXPEVT 000', 'v1_excregs.sv', 'RC_020:  return EXP_MANUAL;', 'RC_020:  return EXP_POR;'),
]


def run_one(k, m, args):
    name, fname, old, new = m[:4]
    edits = [(fname, old, new)] + (list(m[4]) if len(m) > 4 else [])
    d = os.path.join(args.build, f'm{k:02d}')
    shutil.rmtree(d, ignore_errors=True)
    os.makedirs(d)
    for f in ('v1_intc.sv', 'v1_excregs.sv'):
        shutil.copy(os.path.join(RTL, f), os.path.join(d, f))
    for fn, o, n in edits:
        p = os.path.join(d, fn)
        src = open(p).read()
        if src.count(o) != 1:
            return (k, name, 'BROKEN', f'pattern found {src.count(o)} times')
        src = src.replace(o, n)
        if 'im2x' in n:
            src = src.replace('logic [3:0]  im_i, im1, im2, im3, r1_lvl, r2_lvl, rl;',
                              'logic [3:0]  im_i, im1, im2, im2x, im3, r1_lvl, r2_lvl, rl;')
        open(p, 'w').write(src)
    cmd = [args.verilator, '--cc', '--exe', '--build', '-O1', '-Wno-WIDTH', '-Wno-UNOPTFLAT', '-Wno-DECLFILENAME',
           '-Wno-UNUSEDSIGNAL', '-Wno-UNUSEDPARAM', '-CFLAGS', f'-O1 -I{HERE}', '--top-module', 'v1_intc_tb',
           os.path.join(RTL, 'v1_soc_pkg.sv'), os.path.join(d, 'v1_intc.sv'), os.path.join(d, 'v1_excregs.sv'),
           os.path.join(HERE, 'v1_intc_tb.sv'), os.path.join(HERE, 'v1_intc_tb.cpp'), '--Mdir', os.path.join(d, 'obj'),
           '-o', 'tb']
    b = subprocess.run(cmd, capture_output=True, text=True)
    if b.returncode != 0:
        return (k, name, 'BROKEN', 'build failed: ' + (b.stderr.strip().splitlines() or ['?'])[-1])
    r = subprocess.run([os.path.join(d, 'obj', 'tb')], capture_output=True, text=True)
    fails = [l.strip() for l in r.stdout.splitlines() if l.strip().startswith('FAIL:')]
    caught = r.returncode != 0 and 'v1-intc: FAIL' in r.stdout
    return (k, name, 'caught' if caught else 'SURVIVED', fails[0] if fails else '')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--build', default=os.path.join(ROOT, 'build', 'v1_intc_mut'))
    ap.add_argument('--verilator', default='verilator')
    ap.add_argument('--jobs', type=int, default=os.cpu_count() or 4)
    args = ap.parse_args()
    os.makedirs(args.build, exist_ok=True)
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as ex:
        res = sorted(ex.map(lambda km: run_one(km[0], km[1], args), enumerate(MUTANTS)))
    bad = 0
    for k, name, st, why in res:
        print(f'  mutant {k:2d} {st:8s} {name:55s} {why[:110]}')
        bad += st != 'caught'
    print(f'v1-intc-mutants: {len(res) - bad} / {len(res)} caught')
    print('v1-intc-mutants: ' + ('PASS' if bad == 0 else 'FAIL'))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
