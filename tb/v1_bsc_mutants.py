#!/usr/bin/env python3
# v1_bsc_mutants.py -- shows that WI-BSC's testbenches can fail: each mutant plants one bug in a copy of the BIU / BSC
# RTL (rtl/v1/v1_biu.sv, rtl/v1/v1_bsc*.sv; the originals are never touched), builds the named testbench on the copy
# and requires it to report FAIL:
#   agree        tb/v1_bsc_agree_tb.sv (the stub's scenarios, every requester strobe compared every cycle)
#   unit:G       tb/v1_bsc_tb.{sv,cpp} with --only G (regs, rfsh, timing, mpx, lanes, idle, misc, random, basic,
#                pcmcia, rdyhold, manlock, breq, idlex, sdmr, model, dmaq, order, turn)
# The mutants cover the mechanisms that carry measured numbers or contract rules: the front-end depth and CAS latency
# (N_rd = 12), the critical-beat and tail accept rules, the Q split, the write retire point, the Tnop and TRWL of
# writes, the TAS hold, the BIU's order and eligibility, the wrap order, the refresh sequence and timer, the register
# size rule and keys, MPX waits, WCR1 idles, the same-Bcyc READ hazard, RDY holds, bpost_bsc; and each finding of the
# independent verification (long basic accesses, PCMCIA waits, the RDY# hand-over, locks across a manual reset, the
# BREQ# regain, idles across a refresh, the SDMR windows and areas, tb/sh4_bsc.h's functional mode); and SOC.md
# revision 3's rules (agents/reports/integration/conform-WI-BSC.md): only the CPU's Q is split, a DMAC Q's two data
# cycles on a 32-bit bus, a CPU locked read presented across a manual reset served unlocked, and the tie order that
# keeps a fill before its own write-back (the program-order test).
#   usage: tb/v1_bsc_mutants.py [--build DIR] [--verilator verilator] [--jobs N]   (make v1-bsc-mutants)
import argparse, concurrent.futures, os, shutil, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
RTL = os.path.join(ROOT, 'rtl', 'v1')
FILES = ['v1_bsc_pkg.sv', 'v1_bsc_regs.sv', 'v1_bsc_rfsh.sv', 'v1_bsc_port.sv', 'v1_bsc_sdram.sv', 'v1_bsc_mpx.sv',
         'v1_bsc_basic.sv', 'v1_bsc_arb.sv', 'v1_bsc.sv', 'v1_biu.sv']

# the C++ model: a mutant of tb/sh4_bsc.h is compiled with a copy of tb/v1_bsc_tb.cpp next to it (the copy's
# #include "sh4_bsc.h" finds the mutated header first)
TB_FILES = ['sh4_bsc.h', 'v1_bsc_tb.cpp']

# (name, target, file, old, new): `old` must occur exactly once in `file`
MUTANTS = [
    # M6-fix (WI-SOC, agents/reports/integration/M6-fix.md, M6-verify F1): a burst session's gaps are not the CPU's
    ('M6-fix: a burst session\'s gaps go to the CPU (dma_ses ignored)', 'unit:turn', 'v1_bsc_arb.sv',
     "    dw = dma_bq.v || dma_ses;", "    dw = dma_bq.v;"),
    ('M6-fix: the session also holds off the CPU\'s own locked write (a TAS.B pair deadlocks)', 'unit:turn',
     'v1_bsc_arb.sv', "    end else if (go_cmn && cg && !(dw && lk_st != 2'd1 && !cpu_turn)) begin",
     "    end else if (go_cmn && cg && !(dma_bq.v && lk_st != 2'd1 && !cpu_turn) && !(dma_ses && !cpu_turn)) begin"),
    # M6 (WI-SOC, agents/reports/integration/M6.md): the CPU's turn at a DMAC release
    ('M6: no CPU turn at a DMAC release (a DMAC stream with breaks starves an MPX read)', 'unit:turn', 'v1_bsc_arb.sv',
     "    else if (cpu_grant && dma_rel && cpu_bq.v) cpu_turn <= 1'b1;", "    else if (1'b0) cpu_turn <= 1'b1;"),
    ('M6: the CPU turn taken without the DMAC\'s release (a burst session loses the bus)', 'unit:turn', 'v1_bsc_arb.sv',
     "    else if (cpu_grant && dma_rel && cpu_bq.v) cpu_turn <= 1'b1;", "    else if (cpu_grant && cpu_bq.v) cpu_turn <= 1'b1;"),
    # M3 (WI-SOC, agents/reports/integration/M3.md): the accept rules calibrated on the console at M3
    ('M3: no write-after-line (a CPU write of another line waits for the line read\'s last beat)', 'agree',
     'v1_bsc_arb.sv', "(crit_clr && rec.sd_ln && wovl_ok(rec, cpu_bq.r.addr))", "(1'b0)"),
    ('M3: write-after-line also for the line being read', 'agree', 'v1_bsc_arb.sv',
     'p.la != a[28:5] &&', ''),
    ('M3: read-after-read at the critical beat (not one Bcyc after)', 'agree', 'v1_bsc_arb.sv',
     'acc_cpu = crit_clr && ((!cpu_crit && ovl_ok(', 'acc_cpu = crit_clr && ((ovl_ok('),
    ('M3: a refresh keeps the write-after-line state', 'unit:order', 'v1_bsc_arb.sv',
     "if (rf_go)    begin rn = brel(blen_t'(rf_len)) - brel_t'(1);   rec.sd_rd <= 1'b0; rec.sd_ln <= 1'b0; end",
     "if (rf_go)    begin rn = brel(blen_t'(rf_len)) - brel_t'(1);   rec.sd_rd <= 1'b0; end"),
    ('CAS latency 3 read as 4 (N_rd 14)', 'agree', 'v1_bsc_pkg.sv',
     "      3'd3:    return 4'd3;\n      3'd4:    return 4'd4;", "      3'd3:    return 4'd4;\n      3'd4:    return 4'd4;"),
    ('front end one Bcyc longer (DEC waits twice)', 'agree', 'v1_bsc.sv',
     "if (f_v && !f_iss) f_iss <= 1'b1;", "if (f_v && !f_iss && f_dly) f_iss <= 1'b1; f_dly <= f_v && !f_iss && !f_dly;"),
    ('no critical-beat rule: a CPU read overtakes', 'agree', 'v1_bsc_arb.sv',
     'acc_cpu = crit_clr && ((!cpu_crit && ovl_ok(', "acc_cpu = 1'b1 && ((1'b1 && ovl_ok("),
    ('no tail rule: a write inside a line read\'s tail', 'agree', 'v1_bsc_arb.sv',
     "((cpu_rd == 3'd0 && bus_ok(rec, rel, rel_d, cpu_bq.r.addr, 1'b1, 1'b0)) ||",
     "((bus_ok(rec, rel, rel_d, cpu_bq.r.addr, 1'b1, 1'b0)) ||"),
    # (M3: 'no read overlap: the next read waits for the tail' removed -- with the M3 accept one Bcyc after the
    # critical beat a CPU SDRAM read after a CPU SDRAM read is accepted in the Bcyc the bus rule gives anyway on the DC
    # configuration (READ at b3, data b3 + 3 .. b3 + 6), so disabling the overlap path changes no accept: an equivalent
    # mutant; the M3 mutant 'read-after-read at the critical beat' covers the rule's timing)
    ('Q second half without waiting for the first', 'agree', 'v1_bsc.sv',
     "if (acc_x.qh) begin\n          q2_pend <= 1'b1; q2_rdy <= 1'b0;", "if (acc_x.qh) begin\n          q2_pend <= 1'b1; q2_rdy <= 1'b1;"),
    ('write done at its WRIT only (not after the last pull)', 'agree', 'v1_bsc.sv',
     'wr_done[p] = ce.bce && wr_act[p] && (wr_pdone[p] || pl_fin[p]) &&', "wr_done[p] = ce.bce && wr_act[p] &&"),
    ('no Tnop before a row-hit write', 'agree', 'v1_bsc_sdram.sv',
     "p.off_rw  = p.rs == RS_HIT ? (x.we ? 5'd1 : 5'd0)", "p.off_rw  = p.rs == RS_HIT ? (x.we ? 5'd0 : 5'd0)"),
    ('TRWL not counted after a burst write (SQ 10, not 12)', 'agree', 'v1_bsc_sdram.sv',
     "+ {2'b0, trwl} + 6'd1;", "+ 6'd1;"),
    ('TAS hold 15 Bcyc (30 per pair)', 'agree', 'v1_bsc.sv',
     'parameter int LOCK_HOLD = 16', 'parameter int LOCK_HOLD = 15'),
    ('BIU: IC before OC on a tie', 'agree', 'v1_biu.sv',
     'BIU_OC_FILL: return 0;', 'BIU_OC_FILL: return 5;'),
    ('BIU: a read does not wait for the WTB', 'agree', 'v1_biu.sv',
     'if (wtb_p) elig[i] = 1\'b0;', 'if (1\'b0) elig[i] = 1\'b0;'),
    ('BIU: a read of the WBB line does not wait', 'agree', 'v1_biu.sv',
     "if (wbb_p && rq[i].r.addr[28:5] == rq[BIU_OC_WBB].r.addr[28:5]) elig[i] = 1'b0;", ''),
    ("BIU: no TAS pair lock (an SQ burst becomes the pair's write)", 'unit:misc', 'v1_biu.sv',
     "if (lk && i != BIU_OC_WTB) elig[i] = 1'b0;", ''),
    ('line beats in linear order (no wrap)', 'agree', 'v1_bsc_sdram.sv',
     "q = rc.x.addr[4:3] + (rc.b32 ? 2'(j[2:1]) : j[1:0]);", "q = rc.b32 ? 2'(j[2:1]) : j[1:0];"),
    ('refresh: REF right after PALL (no TPC)', 'unit:timing', 'v1_bsc_sdram.sv',
     'rlast <= banks_open ? sd_tpc(regs) : 4\'d0;', 'rlast <= banks_open ? 4\'d1 : 4\'d0;'),
    ('refresh: window TRAS + TRC one longer', 'unit:timing', 'v1_bsc_sdram.sv',
     "3'd0: nocmd <= sd_trfc(regs);", "3'd0: nocmd <= sd_trfc(regs) + 6'd1;"),
    ('refresh through the front end (phase dependent)', 'unit:timing', 'v1_bsc_arb.sv',
     "own && rel <= brel_t'(0) && !hold;", "own && rel <= -brel_t'(2) && !hold;"),
    ('refresh timer: compare only at a tick (period + 1)', 'unit:regs', 'v1_bsc_rfsh.sv',
     "wire match = cks != 3'd0 && cnt == cor;", "wire match = cks != 3'd0 && cnt == cor && tick;"),
    ('RFCR key = the 0xA5 key', 'unit:regs', 'v1_bsc_regs.sv',
     "wire key_rf = wd16[15:10] == 6'b101001;", "wire key_rf = wd16[15:8] == 8'hA5;"),
    ('size rule: any size answers', 'unit:regs', 'v1_bsc_regs.sv',
     'wire own    = sel != R_NONE && pq.size == rsz && pq.off[1:0] == 2\'b00;',
     'wire own    = sel != R_NONE;'),
    ('CMF: writing 1 clears', 'unit:regs', 'v1_bsc_rfsh.sv',
     "if (!wr_rdat[7]) cmf <= 1'b0;", "cmf <= 1'b0;"),
    ('MPX read waits for code 000 = 0 (not 1)', 'unit:mpx', 'v1_bsc_pkg.sv',
     "return (!we && c[1:0] == 2'd0) ? 2'd1 : c[1:0];", 'return c[1:0];'),
    ('MPX later-data waits ignored', 'unit:mpx', 'v1_bsc_mpx.sv',
     'if (w2) begin ph <= PH_WAIT; wc <= 4\'d1; end', ''),
    ('WCR1: no MPX minimum idle', 'unit:idle', 'v1_bsc_pkg.sv',
     "m1   = m == 4'd0 ? 4'd1 : m;", 'm1   = m;'),
    ('the same-Bcyc READ hazard ignored (DMAC pipelining truncates)', 'unit:misc', 'v1_bsc_sdram.sv',
     "if (int'(cp.cl) + int'(cp.nbus) - 1 >= first) iss_ok = 1'b0;", ''),
    ('RDY# wait does not hold the bus bookkeeping', 'unit:random', 'v1_bsc.sv',
     "wire   hold   = ce.bce && (mx_hold || bb_hold);", "wire   hold   = 1'b0;"),
    ('bpost_bsc ignored', 'unit:misc', 'v1_bsc_arb.sv',
     'go_cmn = ce.bce && !rst.por && !rst.man && !f_busy && !q2_pend && own && !bpost_bsc && !lk_hold &&',
     'go_cmn = ce.bce && !rst.por && !rst.man && !f_busy && !q2_pend && own && !lk_hold &&'),
    ('row miss: ACTV one Bcyc early (TPC - 1)', 'unit:timing', 'v1_bsc_sdram.sv',
     "p.off_act = p.rs == RS_MISS ? {1'b0, tpc} : 5'd0;", "p.off_act = p.rs == RS_MISS ? {1'b0, tpc} - 5'd1 : 5'd0;"),
    ('narrow bus: pieces not assembled (every piece delivered)', 'unit:lanes', 'v1_bsc_basic.sv',
     "pdlv  = bx.size == BZ_LINE ? ({1'b0, pa[2:0]} + bp.pb == 4'd8) : (plast && !(bx.qh && !bx.qsec));",
     "pdlv  = bx.size == BZ_LINE ? 1'b1 : (plast && !(bx.qh && !bx.qsec));"),
    ('refresh request never cleared (back-to-back refreshes)', 'unit:misc', 'v1_bsc_rfsh.sv',
     "if (take) req <= 1'b0;", ''),
    ('GPIO: active high', 'unit:misc', 'v1_bsc_port.sv',
     'assign gpio    = |(gpic & ~ioa & ~pin[15:0]);', 'assign gpio    = |(gpic & ~ioa & pin[15:0]);'),
    # the fix step's regressions (agents/reports/units/WI-BSC-fix.md): each one of the verification's findings back
    ('basic bus length 6 bits (wraps at 64 Bcyc)', 'unit:basic', 'v1_bsc.sv',
     'default:         begin iss_ok = bb_ok; hand_len = bb_len; end',
     "default:         begin iss_ok = bb_ok; hand_len = blen_t'(bb_len[5:0]); end"),
    ('PCMCIA first-data waits 6 bits (65 wraps to 1)', 'unit:pcmcia', 'v1_bsc_basic.sv',
     "p.wf  = {3'd0, w8(c)} + (pcw == 2'd0 ? 7'd0 : pcw == 2'd1 ? 7'd15 : pcw == 2'd2 ? 7'd30 : 7'd50);",
     "p.wf  = 7'(6'({3'd0, w8(c)} + (pcw == 2'd0 ? 7'd0 : pcw == 2'd1 ? 7'd15 : pcw == 2'd2 ? 7'd30 : 7'd50)));"),
    ('hand-over while a data cycle waits for RDY#', 'unit:rdyhold', 'v1_bsc.sv',
     'assign hand = ce.bce && f_v && f_iss && iss_ok && iss_bus && !hold;',
     'assign hand = ce.bce && f_v && f_iss && iss_ok && iss_bus;'),
    ('manual reset keeps a CPU lock (arbiter)', 'unit:manlock', 'v1_bsc_arb.sv',
     "if (rst.man && !lk_port) begin lk_st <= 2'd0; lk_cnt <= '0; end", ''),
    ('manual reset keeps the BIU lock', 'unit:manlock', 'v1_biu.sv',
     "if (rst.man) lk <= 1'b0;", ''),
    ('manual reset drops a DMAC lock too (splits the unit)', 'unit:manlock', 'v1_bsc_arb.sv',
     'if (rst.man && !lk_port) begin', 'if (rst.man) begin'),
    ('BREQ# inside the regain window: never owned again', 'unit:breq', 'v1_bsc_arb.sv',
     "if (breq_s || bcnt <= 6'd1) begin", "if (!breq_s && bcnt <= 6'd1) begin"),
    ('a refresh drops the WCR1 idle of the access before it', 'unit:idlex', 'v1_bsc_arb.sv',
     "if (rf_go)    begin rn = brel(blen_t'(rf_len)) - brel_t'(1);   rec.sd_rd <= 1'b0; rec.sd_ln <= 1'b0; end",
     "if (rf_go)    begin rn = brel(blen_t'(rf_len)) - brel_t'(1);   rec <= '0; end"),
    ('MPX write -> write, same area: no idle (the manual table)', 'unit:idlex', 'v1_bsc_pkg.sv',
     "if (same) return (nmpx && n_we) ? m1 : 4'd0;", "if (same) return 4'd0;"),
    ('SDMR: the whole 256 KB block decodes', 'unit:sdmr', 'v1_bsc_regs.sv',
     "wire sdmr_in = pq_sdmr.off[17:16] == 2'b00;", "wire sdmr_in = 1'b1;"),
    ('SDMR: a mode command for a non-SDRAM area', 'unit:sdmr', 'v1_bsc.sv',
     "if (sdmr_v && area_mt(regs, sdmr_a3 ? 3'd3 : 3'd2) == MT_SDRAM) begin", 'if (sdmr_v) begin'),
    ('model: tick_icyc drops the odd Icyc', 'unit:model', 'sh4_bsc.h',
     'for (uint64_t k = icyc_acc + n; k >= 2; k -= 2) bclk(rf_req);', 'for (uint64_t k = n; k >= 2; k -= 2) bclk(rf_req);'),
    ('model: functional-mode pins only sampled in bclk()', 'unit:model', 'sh4_bsc.h',
     'uint32_t pin_lv() const { return cycle ? pins : port_in; }', 'uint32_t pin_lv() const { return pins; }'),
    # SOC.md revision 3 (agents/reports/integration/conform-WI-BSC.md)
    ('a DMAC Q split into two longword cycles (66 Icyc a unit)', 'unit:dmaq', 'v1_bsc.sv',
     'acc_x.qh   = acc_r.size == BZ_Q && !acc_dma;', 'acc_x.qh   = acc_r.size == BZ_Q;'),
    ('a CPU Q not split (one 64-bit cycle)', 'unit:dmaq', 'v1_bsc.sv',
     'acc_x.qh   = acc_r.size == BZ_Q && !acc_dma;', "acc_x.qh   = 1'b0;"),
    ('32-bit SDRAM: a DMAC Q read presents only its low longword', 'unit:dmaq', 'v1_bsc_sdram.sv',
     "bo.v = j == 3'd0 || (qw && j == 3'd1);", "bo.v = j == 3'd0;"),
    ('32-bit SDRAM: a DMAC Q write masks its high longword', 'unit:dmaq', 'v1_bsc_sdram.sv',
     "if (line || j == 3'd0 || (qw && j == 3'd1)) begin", "if (line || j == 3'd0) begin"),
    ('32-bit MPX: a DMAC Q has one data cycle', 'unit:dmaq', 'v1_bsc_mpx.sv',
     "(x.size == BZ_Q && b32w) ? 4'd2 : 4'd1;", "4'd1;"),
    ('BIU: a locked read presented across rst.man keeps its lock', 'unit:manlock', 'v1_biu.sv',
     "      if (mr[i]) rqe[i].r.lock = 1'b0;\n", ''),
    ('BIU: the reset mark outlives the ready (the next TAS.B unlocked)', 'unit:manlock', 'v1_biu.sv',
     "mr[i]     <= rq[i].v && (rst.man || (mr[i] && !(push && win == 3'(i))));", "mr[i]     <= rq[i].v && (rst.man || mr[i]);"),
    ('BIU: every CPU lock dropped', 'unit:manlock', 'v1_biu.sv',
     "      if (mr[i]) rqe[i].r.lock = 1'b0;", "      rqe[i].r.lock = 1'b0;"),
    ('BIU: a write-back before its own fill on a tie', 'unit:order', 'v1_biu.sv',
     'BIU_OC_WBB:  return 3;', 'BIU_OC_WBB:  return -1;'),
]


def run_one(k, m, args):
    name, target, fname, old, new = m
    d = os.path.join(args.build, f'm{k:02d}')
    shutil.rmtree(d, ignore_errors=True)
    os.makedirs(d)
    for f in FILES:
        shutil.copy(os.path.join(RTL, f), os.path.join(d, f))
    for f in TB_FILES:
        shutil.copy(os.path.join(HERE, f), os.path.join(d, f))
    p = os.path.join(d, fname)
    src = open(p).read()
    if src.count(old) != 1:
        return (k, name, 'BROKEN', f'pattern found {src.count(old)} times')
    src = src.replace(old, new)
    if 'f_dly' in new:
        src = src.replace('  logic        f_v, f_iss;', '  logic        f_v, f_iss, f_dly;')
    open(p, 'w').write(src)
    rtl = [os.path.join(RTL, 'v1_soc_pkg.sv')] + [os.path.join(d, f) for f in FILES]
    wno = ['-Wno-WIDTH', '-Wno-UNOPTFLAT', '-Wno-DECLFILENAME', '-Wno-UNUSEDSIGNAL', '-Wno-UNUSEDPARAM', '-Wno-CASEINCOMPLETE',
           '-Wno-LATCH', '-Wno-MULTIDRIVEN', '-Wno-BLKSEQ']
    if target == 'agree':
        cmd = [args.verilator, '--binary', '--timing', '--timescale', '1ns/1ns', '-O1'] + wno + \
              ['--top-module', 'v1_bsc_agree_tb'] + rtl + \
              [os.path.join(HERE, 'v1_bsc_stub.sv'), os.path.join(HERE, 'v1_bsc_agree_tb.sv'),
               '--Mdir', os.path.join(d, 'obj'), '-o', 'tb']
        run = [os.path.join(d, 'obj', 'tb')]
        pass_mark = 'v1-bsc-agree tb: PASS'
    else:
        groups = target.split(':', 1)[1]
        cmd = [args.verilator, '--cc', '--exe', '--build', '-O1'] + wno + ['-CFLAGS', f'-O1 -I{HERE}',
               '--top-module', 'v1_bsc_tb'] + rtl + [os.path.join(HERE, 'v1_bsc_tb.sv'), os.path.join(d, 'v1_bsc_tb.cpp'),
               '--Mdir', os.path.join(d, 'obj'), '-o', 'tb']
        run = [os.path.join(d, 'obj', 'tb'), '--only', groups]
        pass_mark = 'v1-bsc tb: PASS'
    b = subprocess.run(cmd, capture_output=True, text=True)
    if b.returncode != 0:
        return (k, name, 'BROKEN', 'build failed: ' + ' | '.join((b.stderr.strip().splitlines() or ['?'])[-3:]))
    try:
        r = subprocess.run(run, capture_output=True, text=True, timeout=1500)
        out = r.stdout + r.stderr
        rc = r.returncode
    except subprocess.TimeoutExpired:
        out, rc = 'timeout', 1
    fails = [l.strip() for l in out.splitlines() if l.strip().startswith('FAIL')]
    caught = rc != 0 or pass_mark not in out
    return (k, name, 'caught' if caught else 'SURVIVED', fails[0] if fails else out.strip().splitlines()[-1:][0] if out.strip() else '')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--build', default=os.path.join(ROOT, 'build', 'v1_bsc_mut'))
    ap.add_argument('--verilator', default='verilator')
    ap.add_argument('--jobs', type=int, default=os.cpu_count() or 4)
    ap.add_argument('--only', type=int, default=-1)
    args = ap.parse_args()
    args.build = os.path.abspath(args.build)     # the testbench copy is compiled from inside Verilator's Mdir
    os.makedirs(args.build, exist_ok=True)
    todo = [(k, m) for k, m in enumerate(MUTANTS) if args.only < 0 or k == args.only]
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as ex:
        res = sorted(ex.map(lambda km: run_one(km[0], km[1], args), todo))
    bad = 0
    for k, name, st, why in res:
        print(f'  mutant {k:2d} {st:8s} {name:58s} {why[:120]}')
        bad += st != 'caught'
    print(f'v1-bsc-mutants: {len(res) - bad} / {len(res)} caught')
    print('v1-bsc-mutants: ' + ('PASS' if bad == 0 else 'FAIL'))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
