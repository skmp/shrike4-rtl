#!/usr/bin/env python3
"""v1_dbg_mutants.py -- WI-DBG's planted-bug check: every important check of the four debug-unit testbenches must be
able to fail.  Each mutant copies one RTL file into build/v1_dbg_mut/<name>/, plants one bug (a literal source
replacement that must apply exactly once), rebuilds that unit's testbench against the copy (the make fragments take
V1_<UNIT>_RTL / V1_<UNIT>_OBJ) and runs it; the mutant is "caught" when the testbench exits non-zero.

  make v1-dbg-mutants        (runs every mutant in parallel; exit 1 if any mutant survives)
  tb/v1_dbg_mutants.py [--jobs N] [--keep] [name ...]
"""
import concurrent.futures as cf
import os
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, 'build', 'v1_dbg_mut')

# (name, unit make prefix, target, rtl file, old, new, what must catch it)
MUTANTS = [
    ('perfctr_one_event_reg', 'PERFCTR', 'v1-perfctr', 'v1_perfctr.sv',
     'ev_q <= ev; ev_qq <= ev_q;', 'ev_q <= ev; ev_qq <= ev;', 'timing: 0 3 6 9 / A.3 (two event registers)'),
    ('perfctr_clr_while_running', 'PERFCTR', 'v1-perfctr', 'v1_perfctr.sv',
     "if (pq_ccn.wdata[13] && !running[n]) cnt[n] <= '0;", "if (pq_ccn.wdata[13]) cnt[n] <= '0;",
     'control: E023 while running must not clear'),
    ('perfctr_mask', 'PERFCTR', 'v1-perfctr', 'v1_perfctr.sv',
     "16'hC13F;", "16'hE13F;", 'regs: PMCR read-back 0xC13F'),
    ('perfctr_decode_1xx', 'PERFCTR', 'v1-perfctr', 'v1_perfctr.sv',
     "3'b1??:  t_val = cnt[1][31:0];", "3'b100:  t_val = cnt[1][31:0];", 'regs: a[4:2] = 1xx -> PMCTR2L'),
    ('perfctr_write_lands_early', 'PERFCTR', 'v1-perfctr', 'v1_perfctr.sv',
     "running[n] = pmcr[n][15] && pmcr[n][14];",
     "running[n] = (c_wr && c_sel == 1'(n)) ? (c_wd[15] && c_wd[14]) : (pmcr[n][15] && pmcr[n][14]);",
     'timing: a PMCR write lands at the end of its strobe'),
    # M2-fix (M2-verify F6, [HW tools/hw/mmu/ptehubc]): PTEH.ASID reaches the fetch side through the copy's delay and
    # the EX side at its landing
    ('ubc_fetch_live_asid', 'UBC', 'v1-ubc', 'v1_ubc.sv',
     'assign fasid = fasid_d[IF_DLY - 1];', 'assign fasid = asid;',
     'settle-asid: a pre-execution break k <= 12 NOPs after PTEH.ASID 1 -> 2 compares the old ASID'),
    ('ubc_ex_copy_asid', 'UBC', 'v1-ubc', 'v1_ubc.sv',
     'opm.a = opq.v && match(ch[0], 1\'b0, opq.wr, opq.size, opq.va, asid);',
     'opm.a = opq.v && match(ch[0], 1\'b0, opq.wr, opq.size, opq.va, fasid);',
     'settle-asid: an operand break k >= 3 NOPs after PTEH.ASID 1 -> 2 compares the new ASID'),
    ('ubc_if_dly_2', 'UBC', 'v1-ubc', 'v1_ubc.sv',
     'parameter int IF_DLY = 3', 'parameter int IF_DLY = 2', 'settle (9.5): NOP k = 12 must not break'),
    ('ubc_if_dly_4', 'UBC', 'v1-ubc', 'v1_ubc.sv',
     'parameter int IF_DLY = 3', 'parameter int IF_DLY = 4', 'settle (9.5): NOP k = 13 must break'),
    ('ubc_no_asid', 'UBC', 'v1-ubc', 'v1_ubc.sv',
     '&& (c.bamr[2] || c.basr == as);', '&& 1\'b1;', 'breaks: asid_mismatch / asid_mism80'),
    ('ubc_seq_not_cleared', 'UBC', 'v1-ubc', 'v1_ubc.sv',
     "if (seq) a_seen <= 1'b0;                        // the sequence completed", '',
     'random: b_live after a completed sequence'),
    ('ubc_byte_not_replicated', 'UBC', 'v1-ubc', 'v1_ubc.sv',
     "b16 = {16'd0, dq.data[7:0], dq.data[7:0]};", "b16 = {24'd0, dq.data[7:0]};", 'random: byte data compare'),
    ('ubc_evb_ignores_seq', 'UBC', 'v1-ubc', 'v1_ubc.sv',
     'pmc_ev[PMC_UBC_B] = run && hit.hit_b && b_live;', 'pmc_ev[PMC_UBC_B] = run && hit.hit_b;',
     'breaks: seq_Bonly_thenAB evB = 1'),
    ('ubc_brcr_bits', 'UBC', 'v1-ubc', 'v1_ubc.sv',
     "assign brcr = {cmfa, cmfb, 3'd0, pcba, 2'd0, dbeb, pcbb, 2'd0, seq, 2'd0, ubde};",
     "assign brcr = {cmfa, cmfb, 3'd0, pcbb, 2'd0, dbeb, pcba, 2'd0, seq, 2'd0, ubde};", 'regs: BRCR 0408 read-back'),
    ('ubc_seq_write_clears', 'UBC', 'v1-ubc', 'v1_ubc.sv',
     "if (q_wr && q_reg == R_BRCR && !q_wm[3]) a_seen <= 1'b0;", "if (q_wr && q_reg == R_BRCR) a_seen <= 1'b0;",
     'seqrace 3 / random: only SEQ = 0 clears "A seen"'),
    ('ubc_seq_race_hit_wins', 'UBC', 'v1-ubc', 'v1_ubc.sv',
     "if (q_wr && q_reg == R_BRCR && !q_wm[3]) a_seen <= 1'b0;",
     "if (q_wr && q_reg == R_BRCR && !q_wm[3] && !(run && hit.hit_a && seq)) a_seen <= 1'b0;",
     'seqrace 1: a SEQ = 0 write beats a same-cycle A half'),
    ('ubc_seq_zero_no_clear', 'UBC', 'v1-ubc', 'v1_ubc.sv',
     "if (q_wr && q_reg == R_BRCR && !q_wm[3]) a_seen <= 1'b0;", '',
     'seqrace 2: writing SEQ = 0 clears "A seen"'),
    ('ubc_cmf_write_wins', 'UBC', 'v1-ubc', 'v1_ubc.sv',
     "        if (hit.hit_a && !seq) cmfa <= 1'b1;\n", "        if (hit.hit_a && !seq && !(q_wr && q_reg == R_BRCR)) cmfa <= 1'b1;\n",
     'random: a hardware CMF set beats a BRCR write'),
    ('ubc_fetch_live_regs', 'UBC', 'v1-ubc', 'v1_ubc.sv',
     "ift.a[h] = ifq.v && match(fch[0], 1'b1, 1'b0, 2'd1, hva, fasid);",
     "ift.a[h] = ifq.v && match(ch[0], 1'b1, 1'b0, 2'd1, hva, fasid);", 'settle: the fetch side uses the copy'),
    ('ubc_operand_ignores_size', 'UBC', 'v1-ubc', 'v1_ubc.sv',
     "&& (sz == 3'b000 || sz == sz_code(fetch ? 2'd1 : size))", "&& 1'b1", 'breaks: pcA_pre_szL, opA_rd_W_onL'),
    ('ubc_dbeb_nodata', 'UBC', 'v1-ubc', 'v1_ubc.sv',
     '&& !(dbeb && opq.nodata);', ';', 'random: DBEB with an access without data'),
    ('perfctr_ratio_ignored', 'PERFCTR', 'v1-perfctr', 'v1_perfctr.sv',
     "pmcr[n][8] ? {43'd0, ratio_inc} : 48'd1;", "48'd1;", 'control: PMCLK adds ratio_inc per event (x12)'),
    ('perfctr_pmen_or_pmst', 'PERFCTR', 'v1-perfctr', 'v1_perfctr.sv',
     'running[n] = pmcr[n][15] && pmcr[n][14];', 'running[n] = pmcr[n][15] || pmcr[n][14];',
     'control: 4023 / 8023 count nothing'),
    ('perfctr_run_ignored', 'PERFCTR', 'v1-perfctr', 'v1_perfctr.sv',
     'if (run && running[n]) cnt[n] <= cnt[n] + inc[n];', 'if (running[n]) cnt[n] <= cnt[n] + inc[n];',
     'control: the counters hold while asleep (run = 0)'),
    ('perfctr_manual_reset_clears', 'PERFCTR', 'v1-perfctr', 'v1_perfctr.sv',
     'if (rst.por) begin', 'if (rst.por || rst.man) begin', 'control: a manual reset holds PMCR / PMCTR'),
    ('perfctr_pmcr_any_size', 'PERFCTR', 'v1-perfctr', 'v1_perfctr.sv',
     "assign c_ok  = pq_ccn.size == 2'd1;", "assign c_ok  = 1'b1;", 'regs: PMCR r32 / w32 / w8 (size rule)'),
    ('ubc_bam_mask_9', 'UBC', 'v1-ubc', 'v1_ubc.sv',
     "3'b001:  return 32'h0000_03FF;", "3'b001:  return 32'h0000_01FF;", 'breaks / random: BAM 001 masks bits 9:0'),
    ('ubc_quad_low_half_only', 'UBC', 'v1-ubc', 'v1_ubc.sv',
     'default: d_mb = dmatch32(dq.data[31:0], bdrb, bdmrb) || dmatch32(dq.data[63:32], bdrb, bdmrb);',
     'default: d_mb = dmatch32(dq.data[31:0], bdrb, bdmrb);', 'kinds / random: a quad matches on either half'),
    ('ubc_fetch_word_compare', 'UBC', 'v1-ubc', 'v1_ubc.sv',
     "if (fetch) cmp = 32'hFFFF_FFFF;", '', 'breaks: pcA_odd (a fetch compares bit 0)'),
    ('ubc_manual_reset_clears', 'UBC', 'v1-ubc', 'v1_ubc.sv',
     'if (rst.por) begin', 'if (rst.por || rst.man) begin', 'regs: a manual reset holds every UBC register'),
    ('ubc_run_ignored', 'UBC', 'v1-ubc', 'v1_ubc.sv',
     'pmc_ev[PMC_UBC_A] = run && hit.hit_a;', 'pmc_ev[PMC_UBC_A] = hit.hit_a;', 'random: events only while run'),
    ('ubc_seq_sets_cmfa', 'UBC', 'v1-ubc', 'v1_ubc.sv',
     "if (hit.hit_a && !seq) cmfa <= 1'b1;", "if (hit.hit_a) cmfa <= 1'b1;", 'breaks: seq_AB BRCR 4009 (no CMFA)'),
    ('ubc_pre_armed_post', 'UBC', 'v1-ubc', 'v1_ubc.sv',
     'return id0 && rw0 && !pcb;', 'return id0 && rw0;', 'kinds: ctl_pre_armed is 0 for a post break'),
    ('ccn_id_pvr_value', 'CCN_ID', 'v1-ccn-id', 'v1_ccn_id.sv',
     "parameter logic [31:0] PVR = 32'h040205C1", "parameter logic [31:0] PVR = 32'h040205C0", 'D: PVR 040205C1'),
    ('hudi_irq_not_tied', 'HUDI', 'v1-hudi', 'v1_hudi.sv',
     "assign irq = 1'b0;", 'assign irq = pq.wr;', 'every strobe: irq = rst_req = 0'),
    ('ccn_id_size_rule', 'CCN_ID', 'v1-ccn-id', 'v1_ccn_id.sv',
     "if (pq.rd && pq.size == 2'd2) pr.rdata = PVR;", 'if (pq.rd) pr.rdata = PVR;', 'S: PVR r16 / r8 read 0'),
    ('hudi_sddrl_half', 'HUDI', 'v1-hudi', 'v1_hudi.sv',
     "pr.rdata = {16'd0, SDDR_INIT[15:0]};", "pr.rdata = {16'd0, SDDR_INIT[31:16]};", 'I: SDDRL r16+2 a3ab'),
    ('hudi_sdir_span', 'HUDI', 'v1-hudi', 'v1_hudi.sv',
     "if (o[17:1] == 17'd0) begin", "if (o[17:2] == 16'd0) begin", 'span: SDIR +2 / +3 are holes (hit = 0)'),
    ('hudi_sdir_r32', 'HUDI', 'v1-hudi', 'v1_hudi.sv',
     "if (pq.rd && pq.size == 2'd1 && o[1:0] == 2'd0) pr.rdata = 32'h0000FFFF;",
     "if (pq.rd) pr.rdata = 32'h0000FFFF;", 'I: SDIR r32 reads 0'),
]


def run(m, keep):
    name, unit, target, rtl, old, new, why = m
    d = os.path.join(OUT, name)
    os.makedirs(d, exist_ok=True)
    src = open(os.path.join(ROOT, 'rtl', 'v1', rtl)).read()
    if src.count(old) != 1:
        return name, why, 'BROKEN (the replacement does not apply exactly once: the RTL changed, update the mutant)'
    dst = os.path.join(d, rtl)
    open(dst, 'w').write(src.replace(old, new))
    obj = os.path.join('build', 'v1_dbg_mut', name, 'obj')
    cmd = ['make', '-s', target, 'V1_%s_RTL=%s' % (unit, os.path.relpath(dst, ROOT)), 'V1_%s_OBJ=%s' % (unit, obj),
           'V1_%s_N=300000' % unit, 'NJOBS=4']
    p = subprocess.run(cmd, cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    log = p.stdout
    open(os.path.join(d, 'log.txt'), 'w').write(log)
    if not keep:
        shutil.rmtree(os.path.join(ROOT, obj), ignore_errors=True)
    if 'PASS' in log.splitlines()[-1] if log.strip() else False:
        return name, why, 'MISSED (the testbench passed)'
    if ': FAIL' in log:
        first = next((l for l in log.splitlines() if l.startswith('FAIL')), '')
        return name, why, 'caught: ' + first[:110]
    return name, why, 'BROKEN (build or run error, see %s)' % os.path.relpath(os.path.join(d, 'log.txt'), ROOT)


def main():
    args = sys.argv[1:]
    jobs, keep = 6, False
    if '--jobs' in args:
        i = args.index('--jobs'); jobs = int(args[i + 1]); del args[i:i + 2]
    if '--keep' in args:
        keep = True; args.remove('--keep')
    ms = [m for m in MUTANTS if not args or m[0] in args]
    with cf.ThreadPoolExecutor(jobs) as ex:
        res = list(ex.map(lambda m: run(m, keep), ms))
    bad = 0
    for name, why, r in res:
        print('mutant %-28s %-52s %s' % (name, why, r))
        bad += not r.startswith('caught')
    print('v1-dbg-mutants: %d / %d planted bugs caught' % (len(res) - bad, len(res)))
    print('v1-dbg-mutants: %s' % ('PASS' if not bad else 'FAIL'))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
