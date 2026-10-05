#!/usr/bin/env python3
# v1_scif_mutants.py -- planted-bug check of the serial unit testbenches (WI-SER; make v1-scif-mutants).
#
# Each mutant is one textual change to a copy of the serial RTL in build/v1_scif_mut/<name>/ (the real sources are
# never touched).  Every mutant is built and run twice: with the model co-simulation (tb/scif_model.h), and with
# --no-cosim, i.e. the directed checks alone (the spec formulas and the console-log replays).  A mutant is "killed"
# when the testbench fails; the target passes only when every mutant is killed by the directed checks (and so also
# with the co-simulation).
import os, shutil, subprocess, sys, threading
from concurrent.futures import ThreadPoolExecutor

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, 'build', 'v1_scif_mut')
RTL = ['v1_soc_pkg.sv', 'v1_scif_brg.sv', 'v1_scif_fifo.sv', 'v1_scif_tx.sv', 'v1_scif_rx.sv', 'v1_scif.sv', 'v1_sci.sv']

# name, tb (scif / sci), file, old text, new text, what the mutation breaks
MUTANTS = [
    ('pre5', 'scif', 'v1_scif_tx.sv', "if (pos == 8'd5) begin", "if (pos == 8'd4) begin",
     'idle-to-start delay 5 ticks instead of 6 (B0 + 23 / 159 / 166)'),
    ('pop_at_load', 'scif', 'v1_scif.sv', "t_pop_rq = pop_pend && tick;", "t_pop_rq = tx_load;",
     'the TX FIFO pops at S + 16 instead of S + 17'),
    ('tdfe_le', 'scif', 'v1_scif.sv', "|| t_nx < ttrg;", "|| t_nx <= ttrg;",
     "TDFE on 'count <= trigger' (the manual's wording) instead of the measured 'fewer than'"),
    ('dr239', 'scif', 'v1_scif.sv', "(drt_n == 8'd240 && r_nx", "(drt_n == 8'd239 && r_nx",
     'DR after 239 ticks instead of 240'),
    ('no_latch_dr', 'scif', 'v1_scif.sv', "c_dr   = !pq.wdata[0] && l_dr;", "c_dr   = !pq.wdata[0];",
     'DR clears without a prior read of 1'),
    ('no_dma_rx_clear', 'scif', 'v1_scif.sv', "dma_rx   = acc_rd && ri == 4'd5 && pq.dma;", "dma_rx   = 1'b0;",
     'no DMAC auto-clear of RDF / DR'),
    ('orer_on_fer', 'scif', 'v1_scif.sv', "if (rx_fer) er_set = 1'b1; else orer_set = 1'b1;", "orer_set = 1'b1;",
     'a 17th byte with FER sets ORER instead of ER'),
    ('no_break', 'scif', 'v1_scif_rx.sv', "brk = 1'b1; st_n = S_BRK;", "brk = 1'b0; st_n = S_BRK;",
     'break detection never sets BRK'),
    ('loop_after_port', 'scif', 'v1_scif.sv', "rx_in  = loop_a ? tx_line : rxd_s[1];", "rx_in  = loop_a ? txd_o : rxd_s[1];",
     'the loopback taken after the TxD2 port mux'),
    ('sptr_one_flop', 'scif', 'v1_scif.sv', "rtsio, rts_s[1], ctsio, cts_s[1]", "rtsio, rts_s[0], ctsio, cts_s[0]",
     'SCSPTR2 pin reads through one Pphi stage instead of two'),
    ('no_size_rule', 'scif', 'v1_scif.sv', "match  = slot && pq.off[1:0] == 2'd0 && pq.size == (IS_BYTE[ri] ? 2'd0 : 2'd1);",
     "match  = slot && pq.off[1:0] == 2'd0;", 'registers answer any access size'),
    ('rts14', 'scif', 'v1_scif.sv', "(fcr[3] && rcnt >= 5'd15)", "(fcr[3] && rcnt >= 5'd14)",
     'RTS2 at R >= 14'),
    ('tend_busy', 'scif', 'v1_scif.sv', "(tx_idle_nx && t_nx == 5'd0)", "(t_nx == 5'd0)",
     'TEND set while the last frame is still in the shift register'),
    ('fifo_full17', 'scif', 'v1_scif.sv', "t_push = acc_wr && ri == 4'd3 && !fcr_a[2] && tcnt != 5'd16;",
     "t_push = acc_wr && ri == 4'd3 && !fcr_a[2];", 'a write to a full TX FIFO is not ignored'),
    ('sci_tdre_te', 'sci', 'v1_sci.sv', "tdre_n = tdre_c || tdre_set || !te_a;", "tdre_n = tdre_c || tdre_set;",
     'TDRE not fixed at 1 while TE = 0'),
    ('sci_sptr_mstp', 'sci', 'v1_sci.sv', "    if (ires) begin\n      {eio,", "    if (mres) begin\n      {eio,",
     'SCSPTR1 re-initialised by module standby'),
    ('sci_t1513', 'sci', 'v1_sci.sv', "orer_set = 1'b1; fer_set = rx_fer; per_set = rx_per;", "orer_set = 1'b1;",
     'hm Table 15.13: ORER without FER / PER'),
    ('sci_card_tend', 'sci', 'v1_sci.sv', "ct_t_n == HALF && ct_k == 4'd12", "ct_t_n == HALF && ct_k == 4'd11",
     'smart card TEND at 11.5 etu instead of 12.5 (GM = 0)'),
    ('sci_sync_nocont', 'sci', 'v1_sci.sv', "sy_tsr_n = tdr_a; tdre_set = 1'b1; sy_cont_n = 1'b1;",
     "sy_tsr_n = tdr_a; tdre_set = 1'b1; sy_cont_n = 1'b0;", 'clocked synchronous: no back-to-back continuation'),
    ('sci_mpie', 'sci', 'v1_sci.sv', "if (smr_a[2] && rx_mpb) scr_n[3] = 1'b0;", "if (1'b0) scr_n[3] = 1'b0;",
     'MPB = 1 does not clear MPIE'),
    ('no_dma_tx_clear', 'scif', 'v1_scif.sv', "dma_tx   = acc_wr && ri == 4'd3 && pq.dma;", "dma_tx   = 1'b0;",
     'no DMAC auto-clear of TDFE / TEND (the DMAC would fill the FIFO to 16, run 1 S17 holds it at 8)'),
    ('tap_8bit', 'scif', 'v1_scif_tx.sv', "assign sh_o = f_chr ? {1'b0, sh[6:0]} : sh;", "assign sh_o = sh;",
     'the tap reports the unsent bit 7 of a 7-bit frame (SOC.md 7.4 cross-check)'),
    ('sci_stby_noinit', 'sci', 'v1_sci.sv', "assign mres  = ires | mstp | stby;", "assign mres  = ires | mstp;",
     'standby does not initialise the SCI (SOC.md 2.3)'),
    ('sci_dma_tdr', 'sci', 'v1_sci.sv', "dma_tdr = acc_wr && ri == 3'd3 && pq.dma;", "dma_tdr = 1'b0;",
     'no DMAC auto-clear of TDRE / TEND on an SCTDR1 write'),
    ('sci_dma_rdr', 'sci', 'v1_sci.sv', "dma_rdr = acc_rd && ri == 3'd5 && pq.dma;", "dma_rdr = 1'b0;",
     'no DMAC auto-clear of RDRF on an SCRDR1 read'),
    # WI-SER-fix (the verifier's findings 1, 2 and 4, and the [P] choice of finding 3)
    ('tfrst_poppend', 'scif', 'v1_scif.sv', "pop_pend_n = !fcr_a[2] && ((pop_pend && !tick) || tx_load);",
     "pop_pend_n = (pop_pend && !tick) || tx_load;",
     'TFRST does not cancel the pending TX pop: a byte written after it is dropped at S + 17 (timing X1)'),
    ('er_nolatch', 'scif', 'v1_scif.sv', "c_er   = !pq.wdata[7] && l_er;", "c_er   = !pq.wdata[7];",
     'ER clears without a prior read of 1'),
    ('tend_nolatch', 'scif', 'v1_scif.sv', "c_tend = !pq.wdata[6] && l_tend;", "c_tend = !pq.wdata[6];",
     'TEND clears without a prior read of 1'),
    ('tdfe_nolatch', 'scif', 'v1_scif.sv', "c_tdfe = !pq.wdata[5] && l_tdfe;", "c_tdfe = !pq.wdata[5];",
     'TDFE clears without a prior read of 1'),
    ('brk_nolatch', 'scif', 'v1_scif.sv', "c_brk  = !pq.wdata[4] && l_brk;", "c_brk  = !pq.wdata[4];",
     'BRK clears without a prior read of 1'),
    ('rdf_nolatch', 'scif', 'v1_scif.sv', "c_rdf  = !pq.wdata[1] && l_rdf;", "c_rdf  = !pq.wdata[1];",
     'RDF clears without a prior read of 1'),
    ('orer_nolatch', 'scif', 'v1_scif.sv', "c_orer = !pq.wdata[0] && l_orer;", "c_orer = !pq.wdata[0];",
     'ORER (SCLSR2) clears without a prior read of 1'),
    ('dr_latch_w1', 'scif', 'v1_scif.sv', "l_dr_n   = l_dr   && pq.wdata[0];", "l_dr_n   = 1'b0;",
     '[P] a write of 1 disarms the DR read-1 latch (serial 3.4; run 3 H3 did not isolate it)'),
    ('brk_any_fer', 'scif', 'v1_scif_rx.sv', "done = 1'b1; arm_n = BRK_EN && !rxd && data == 8'd0;",
     "done = 1'b1; arm_n = BRK_EN && !rxd;",
     '[P] break detection arms on any framing-error frame (the BRK bit text), not serial 3.3.5 (uart mid-frame break)'),
    ('sci_tdre_nolatch', 'sci', 'v1_sci.sv', "c_tdre = !pq.wdata[7] && l_tdre;", "c_tdre = !pq.wdata[7];",
     'SCI TDRE clears without a prior read of 1'),
    ('sci_rdrf_nolatch', 'sci', 'v1_sci.sv', "c_rdrf = !pq.wdata[6] && l_rdrf;", "c_rdrf = !pq.wdata[6];",
     'SCI RDRF clears without a prior read of 1'),
    ('sci_orer_nolatch', 'sci', 'v1_sci.sv', "c_orer = !pq.wdata[5] && l_orer;", "c_orer = !pq.wdata[5];",
     'SCI ORER clears without a prior read of 1'),
    ('sci_fer_nolatch', 'sci', 'v1_sci.sv', "c_fer  = !pq.wdata[4] && l_fer;", "c_fer  = !pq.wdata[4];",
     'SCI FER clears without a prior read of 1'),
    ('sci_per_nolatch', 'sci', 'v1_sci.sv', "c_per  = !pq.wdata[3] && l_per;", "c_per  = !pq.wdata[3];",
     'SCI PER clears without a prior read of 1'),
]


def build(m):
    name, tb, f, old, new, _ = m
    d = os.path.join(OUT, name)
    shutil.rmtree(d, ignore_errors=True)
    os.makedirs(os.path.join(d, 'rtl'))
    for r in RTL:
        shutil.copy(os.path.join(ROOT, 'rtl', 'v1', r), os.path.join(d, 'rtl', r))
    p = os.path.join(d, 'rtl', f)
    s = open(p).read()
    if s.count(old) != 1:
        return name, 'mutation text not found exactly once'
    open(p, 'w').write(s.replace(old, new))
    sub = ['v1_scif_brg.sv', 'v1_scif_fifo.sv', 'v1_scif_tx.sv', 'v1_scif_rx.sv', 'v1_scif.sv' if tb == 'scif' else 'v1_sci.sv']
    srcs = [os.path.join(d, 'rtl', x) for x in ['v1_soc_pkg.sv'] + sub]
    top = 'v1_%s_tb_top' % tb
    cmd = ['verilator', '--cc', '--exe', '--build', '-j', '4', '-Wno-WIDTH', '-Wno-UNOPTFLAT', '-Wno-DECLFILENAME',
           '-Wno-UNUSEDSIGNAL', '-Wno-UNUSEDPARAM', '-CFLAGS', '-O1 -I%s/tb' % ROOT, '--top-module', top] + srcs + \
          [os.path.join(ROOT, 'tb', top + '.sv'), os.path.join(ROOT, 'tb', 'v1_%s_tb.cpp' % tb), '--Mdir',
           os.path.join(d, 'obj'), '-o', 'tb']
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode:
        return name, 'build failed: ' + r.stderr[-400:]
    return name, None


def run(m, cosim):
    # The testbench's output is streamed, never collected: a mutant that trips the checker can print a FAIL line on
    # nearly every one of the 1M random cycles (gigabytes), and capture_output once took this script to 12 GB and the
    # machine down.  The first FAIL line decides the mutant (killed), so the run is stopped right there.
    name, tb = m[0], m[1]
    exe = os.path.join(OUT, name, 'obj', 'tb')
    args = [exe, '--rand-cycles', '1000000'] + ([] if cosim else ['--no-cosim'])
    p = subprocess.Popen(args, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True)
    watchdog = threading.Timer(900, p.kill)
    watchdog.start()
    first = ''
    try:
        for line in p.stdout:
            if line.startswith('FAIL'):
                first = line[:150]
                p.kill()
                break
    finally:
        p.stdout.close()
        p.wait()
        watchdog.cancel()
    return bool(first) or p.returncode != 0, first


def main():
    os.makedirs(OUT, exist_ok=True)
    jobs = int(os.environ.get('NJOBS', os.cpu_count() or 4))
    with ThreadPoolExecutor(max(1, jobs // 4)) as ex:
        built = dict(ex.map(build, MUTANTS))
    bad = 0
    with ThreadPoolExecutor(jobs) as ex:
        res = {m[0]: (ex.submit(run, m, True), ex.submit(run, m, False)) for m in MUTANTS if not built[m[0]]}
    for m in MUTANTS:
        name, tb, _, _, _, what = m
        if built[name]:
            print('  %-16s %-4s ERROR %s' % (name, tb, built[name])); bad += 1; continue
        (k1, _), (k2, why) = res[name][0].result(), res[name][1].result()
        ok = k1 and k2
        bad += not ok
        print('  %-16s %-4s cosim %-9s directed %-9s %s' % (name, tb, 'killed' if k1 else 'SURVIVED',
                                                              'killed' if k2 else 'SURVIVED', what))
        if why:
            print('  %16s      first directed failure: %s' % ('', why.split(': ', 1)[-1]))
    print('v1-scif-mutants: %d mutants, %d survived or broken' % (len(MUTANTS), bad))
    print('v1-scif-mutants: %s' % ('FAIL' if bad else 'PASS'))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
