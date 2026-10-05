#!/usr/bin/env python3
"""v1_clk_mutants.py -- planted-bug check of the WI-CLK unit testbenches (make v1-clk-mutants).

Each mutant copies one rtl/v1 unit file into build/v1_clk_mut/<id>/, replaces exactly one source fragment (the
script refuses a pattern that does not occur exactly once, so a mutant can never silently be the original), builds
that unit's testbench against the copy and runs it.  A mutant is caught when the testbench exits non-zero.  Most
mutants are the behaviours the console measurements rule out (several are minicast's known deviations, timers-clock
10); the rest break a SOC.md contract (phases, reset kinds, codes, durations).  The script exits 1 if any survives.

    python3 tb/v1_clk_mutants.py [--jobs N] [--only ID,ID] [--cycles N]
"""
import argparse, os, shutil, subprocess, sys
from concurrent.futures import ThreadPoolExecutor

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "build", "v1_clk_mut")
TIMEOUT = 300                  # s per mutant run (the unmutated testbenches take seconds at --cycles 300000)

# unit -> (tb top, the RTL files it builds)
UNITS = {
    "cpg":  ("v1_cpg_tb",  ["v1_rstc.sv", "v1_cpg.sv", "v1_wdt.sv", "v1_tmu.sv", "v1_rtc.sv"]),
    "rstc": ("v1_rstc_tb", ["v1_rstc.sv"]),
    "wdt":  ("v1_wdt_tb",  ["v1_wdt.sv"]),
    "tmu":  ("v1_tmu_tb",  ["v1_tmu.sv"]),
    "rtc":  ("v1_rtc_tb",  ["v1_rtc.sv"]),
    "wake": ("v1_clkwake_tb", ["v1_rstc.sv", "v1_cpg.sv", "v1_wdt.sv", "v1_tmu.sv", "v1_rtc.sv", "v1_intc.sv"]),
}

# id, unit tb, file, original fragment, mutated fragment, what it breaks
MUTANTS = [
    ("cpg-phase", "cpg", "v1_cpg.sv", "wire pce_now    = clk_run && pcnt == 3'd0;",
     "wire pce_now    = clk_run && pcnt == 3'd1;",
     "pce one Icyc early: the ph = 0 phase after reset"),
    ("cpg-resync", "cpg", "v1_cpg.sv", "rs    <= 4'(RESYNC_PCE);\n                ck_on <= 1'b0;\n              end\n            S_RESYNC:",
     "rs    <= 4'(RESYNC_PCE - 1);\n                ck_on <= 1'b0;\n              end\n            S_RESYNC:",
     "the wake resynchroniser 3 Pcyc instead of 4 (M1b: the S handler would read the first TMU tick)"),
    ("cpg-frqcr-por", "cpg", "v1_cpg.sv", "if (rst.pin) frqcr <= frqcr_init(md);", "frqcr <= frqcr_init(md);",
     "a WDT power-on reset reloads FRQCR (only the pin may)"),
    ("cpg-div-stby", "cpg", "v1_cpg.sv", "if (st != S_STANDBY) dv <= dv + 12'd1;", "dv <= dv + 12'd1;",
     "the WDT divider runs in standby"),
    ("cpg-md1", "cpg", "v1_cpg.sv", "3'd1:          return 12'hE23;", "3'd1:          return 12'hE0A;", "MD mode 1 value"),
    ("cpg-ratio", "cpg", "v1_cpg.sv", "4'd2:    return 5'd12;", "4'd2:    return 5'd8;", "ratio_inc at I:B = 2 (12)"),
    ("cpg-sleep-level", "cpg", "v1_cpg.sv", "wire sleep_edge = sleep_req && !sleep_q;", "wire sleep_edge = sleep_req;",
     "a sleep_req held after the wake sleeps again"),
    ("cpg-pll2", "cpg", "v1_cpg.sv", "fw[9] && (pll1_on || bfc_chg || pll2_on)", "1'b0 && (pll1_on || bfc_chg || pll2_on)",
     "no PLL2 relock count"),
    ("cpg-stbcr2", "cpg", "v1_cpg.sv", "pr.rdata = {24'd0, dslp, 7'd0};", "pr.rdata = {24'd0, 7'd0, dslp};",
     "STBCR2.DSLP read at the wrong bit"),
    ("cpg-deep", "cpg", "v1_cpg.sv", "deep <= !stbcr[7] && dslp;", "deep <= 1'b0;", "the DMAC runs in deep sleep"),
    # WI-CLK fix step (agents/reports/units/WI-CLK-fix.md): the verifier's surviving mutants and the new rules
    ("cpg-dslp-wins", "cpg", "v1_cpg.sv", "st   <= stbcr[7] ? S_STANDBY : dslp ? S_DSLEEP : S_SLEEP;",
     "st   <= dslp ? S_DSLEEP : stbcr[7] ? S_STANDBY : S_SLEEP;", "DSLP wins over STBY (hm 9.2.4: STBY = 1 is standby)"),
    ("cpg-stby-wake", "cpg", "v1_cpg.sv", "              if (wake_stby) begin", "              if (wake_stby || wake) begin",
     "a plain wake ends standby (only wake_stby may)"),
    ("cpg-stby-noresync", "cpg", "v1_cpg.sv", "st <= sbx ? S_RESYNC : S_RUN;", "st <= S_RUN;",
     "the standby exit raises cpu_run at the WDT overflow, before v1_intc can present irq.req"),
    ("cpg-stby-status", "cpg", "v1_cpg.sv", "(st == S_RESYNC && !sbx)) status = STS_SLEEP;",
     "(st == S_RESYNC)) status = STS_SLEEP;", "STATUS not normal after the standby exit's overflow (hm 9.5.2)"),
    ("cpg-no-wpend-wait", "cpg", "v1_cpg.sv", "if ((es == 4'd0 || (es == 4'd1 && pce_now)) && !p4_wpend) begin",
     "if ((es == 4'd0 || (es == 4'd1 && pce_now))) begin",
     "the clock stops while a Pclk write is in flight (M1a: STBY <- 1 ; SLEEP sleeps instead of standby)"),
    ("cpg-entry-short", "cpg", "v1_cpg.sv", "parameter int ENTRY_PCE  = 9,", "parameter int ENTRY_PCE  = 8,",
     "the clock-stop handshake 8 Pcyc (M1b sleepwarm: J 55, below the console's interquartile range)"),
    ("cpg-entry-long", "cpg", "v1_cpg.sv", "parameter int ENTRY_PCE  = 9,", "parameter int ENTRY_PCE  = 10,",
     "the clock-stop handshake 10 Pcyc (M1b sleepwarm: S 83, above the console's interquartile range)"),
    ("cpg-entry-run", "cpg", "v1_cpg.sv", "assign cpu_run    = st == S_RUN;",
     "assign cpu_run    = st == S_RUN || st == S_ENTRY;", "the core keeps running through the clock-stop handshake"),
    ("cpg-clk-entry", "cpg", "v1_cpg.sv", "assign cpu_clk    = st == S_RUN || st == S_ENTRY || (st == S_RESYNC && ck_on);",
     "assign cpu_clk    = st == S_RUN || (st == S_RESYNC && ck_on);",
     "PMCTR stops at the sleep request, not at the clock stop (M1b sleepwarm: J 28 against the console's 50-66)"),
    ("cpg-clk-rs", "cpg", "v1_cpg.sv", "assign cpu_clk    = st == S_RUN || st == S_ENTRY || (st == S_RESYNC && ck_on);",
     "assign cpu_clk    = st == S_RUN || st == S_ENTRY || st == S_RESYNC;",
     "PMCTR counts the resync after a sleep (the clock stopped: the handler-to-return intervals are the silicon's)"),
    ("cpg-clk-rs-entry", "cpg", "v1_cpg.sv", "rs    <= 4'(RESYNC_PCE);\n                ck_on <= 1'b1;",
     "rs    <= 4'(RESYNC_PCE);\n                ck_on <= 1'b0;",
     "a wake in the clock-stop handshake resynchronises with the clock stopped (it never stopped)"),
    ("cpg-entry-nowake", "cpg", "v1_cpg.sv", "if (wake) begin\n                st    <= S_RESYNC;\n                rs    <= 4'(RESYNC_PCE);\n                ck_on <= 1'b1;",
     "if (1'b0) begin\n                st    <= S_RESYNC;\n                rs    <= 4'(RESYNC_PCE);\n                ck_on <= 1'b1;",
     "a wake in the clock-stop handshake waits for the stop"),
    ("cpg-entry-wpend-only", "cpg", "v1_cpg.sv", "if (pce_now && es != 4'd0) es <= es - 4'd1;",
     "if (es != 4'd0) es <= 4'd0;",
     "the clock stops as soon as no Pclk write is in flight (the handshake's depth lost)"),
    ("cpg-sleep-pend", "cpg", "v1_cpg.sv", "if ((st == S_RUN && sleep_edge) || st == S_ENTRY) sleep_pend <= 1'b1;",
     "if (1'b0) sleep_pend <= 1'b1;",
     "a one-cycle sleep_req in the cycle of a relocking FRQCR write is lost"),
    ("cpg-ratio-ifc", "cpg", "v1_cpg.sv", "assign ratio_inc  = ratio_of(frqcr[5:3]);",
     "assign ratio_inc  = ratio_of(frqcr[5:3] - frqcr[8:6]);", "ratio_inc depends on IFC (SOC.md 9.18: IFC = x1)"),
    ("wake-stby-noresync", "wake", "v1_cpg.sv", "st <= sbx ? S_RESYNC : S_RUN;", "st <= S_RUN;",
     "with the real v1_intc: cpu_run rises before irq.req after a standby exit"),
    ("wake-resync", "wake", "v1_cpg.sv", "rs    <= 4'(RESYNC_PCE);\n                ck_on <= 1'b0;\n              end\n            S_RESYNC:",
     "rs    <= 4'(RESYNC_PCE - 1);\n                ck_on <= 1'b0;\n              end\n            S_RESYNC:",
     "with the real v1_intc: a TMU wake costs 11 Icyc in the CPG, not 15"),
    ("rstc-hold", "rstc", "v1_rstc.sv", "hold <= 4'd8;", "hold <= 4'd7;", "reset asserted 7 Icyc after the request"),
    ("rstc-sck2", "rstc", "v1_rstc.sv", "if (!rn2 && !sk2)                              req = K_MAN;",
     "if (!rn2 && !sk2)                              req = K_POR;", "RESET with SCK2 low as a power-on reset"),
    ("rstc-140", "rstc", "v1_rstc.sv", "(kind == K_MAN && mh) ? RC_140", "(kind == K_MAN && mh) ? RC_020",
     "multiple-hit code 0x140"),
    ("rstc-merge", "rstc", "v1_rstc.sv", "always_comb merged = (act && kind > req) ? kind : req;",
     "always_comb merged = req;", "a weaker request downgrades an active reset"),
    ("wdt-early", "wdt", "v1_wdt.sv", "tap_q    <= cin;", "tap_q    <= tap;",
     "no early first count when TME is set in the tap's high half (R)"),
    ("wdt-key", "wdt", "v1_wdt.sv", "pq.wdata[15:8] == 8'h5A;", "pq.wdata[15:8] == 8'hA5;", "the WTCNT key (G)"),
    ("wdt-rate", "wdt", "v1_wdt.sv", "wire tap = div[4'(cks) + 4'd4];", "wire tap = div[4'(cks) + 4'd5];",
     "CKS rates doubled (G)"),
    ("wdt-iovf1", "wdt", "v1_wdt.sv", "if (w_csr) {tme, wtit, rsts, wovf, iovf, cks} <= pq.wdata[7:0];",
     "if (w_csr) {tme, wtit, rsts, wovf, iovf, cks} <= pq.wdata[7:0] & 8'hF7;", "IOVF not settable by a written 1 (G)"),
    ("wdt-reload", "wdt", "v1_wdt.sv", "else if (reload && settle) cnt <= sinit;", "else if (1'b0) cnt <= sinit;",
     "the second settle phase does not restart from the software value"),
    ("wdt-bytewrite", "wdt", "v1_wdt.sv", "wire w_cnt = pq.wr && s_cnt && pq.size == 2'd1 &&",
     "wire w_cnt = pq.wr && s_cnt &&", "WTCNT written by a byte / longword (G)"),
    ("tmu-period", "tmu", "v1_tmu.sv", "tcnt[n] <= tcnt[n] == 32'd0 ? tcor[n] : tcnt[n] - 32'd1;",
     "tcnt[n] <= tcnt[n] == 32'd1 ? tcor[n] : tcnt[n] - 32'd1;", "period TCOR ticks, 0 never seen (minicast; D)"),
    ("tmu-tcnt-running", "tmu", "v1_tmu.sv", "if (wr && s_k == 4'(3 * n + 3) && !str[n]) tcnt[n] <= pq.wdata;",
     "if (wr && s_k == 4'(3 * n + 3)) tcnt[n] <= pq.wdata;", "TCNT write taken while running (minicast; C / Q)"),
    ("tmu-unf1", "tmu", "v1_tmu.sv", "else if (wr && s_k == 4'(3 * n + 4) && !pq.wdata[8]) unf[n] <= 1'b0;",
     "else if (wr && s_k == 4'(3 * n + 4)) unf[n] <= pq.wdata[8];", "UNF set by a written 1 (minicast; E)"),
    ("tmu-presc-restart", "tmu", "v1_tmu.sv", "if (ce.pce && !pstop) presc <= presc + 10'd1;",
     "if (wr && s_k == 4'd1) presc <= '0; else if (ce.pce && !pstop) presc <= presc + 10'd1;",
     "the prescaler restarted by TSTR (B / Q)"),
    ("tmu-mstp", "tmu", "v1_tmu.sv", "if (pstop && tpsc[n] != 3'd6)  str[n] <= 1'b0;", "if (1'b0)  str[n] <= 1'b0;",
     "TSTR kept in module stop / standby (I)"),
    ("tmu-size", "tmu", "v1_tmu.sv", "wire acc = s_in && !mstp && pq.size == s_sz;", "wire acc = s_in && !mstp;",
     "wrong-size accesses served (minicast; E)"),
    ("tmu-nextevt", "tmu", "v1_tmu.sv", "j = (r - 64'd1 - (64'(presc) & (r - 64'd1))) + 64'(tcnt[n]) * r;",
     "j = (r - (64'(presc) & (r - 64'd1))) + 64'(tcnt[n]) * r;", "next_evt one tick late"),
    ("tmu-unf-race", "tmu", "v1_tmu.sv",
     "if (ev[n] && tcnt[n] == 32'd0)                 unf[n] <= 1'b1;\n        else if (wr && s_k == 4'(3 * n + 4) && !pq.wdata[8]) unf[n] <= 1'b0;",
     "if (wr && s_k == 4'(3 * n + 4) && !pq.wdata[8]) unf[n] <= 1'b0;\n        else if (ev[n] && tcnt[n] == 32'd0) unf[n] <= 1'b1;",
     "a UNF-clearing TCR write beats the underflow in the same Pcyc (a lost timer interrupt; spec 7.4)"),
    ("tmu-tpsc5", "tmu", "v1_tmu.sv", "if (tpsc[n] <= 3'd4)      ev[n] = str[n] && tick[tpsc[n]];",
     "if (tpsc[n] <= 3'd5)      ev[n] = str[n] && tick[3'(tpsc[n] - 3'd1)];", "TPSC 5 counts (A)"),
    ("rtc-leap", "rtc", "v1_rtc.sv", "wire        leap  = b_yr[7:0] != 8'd0 ? lp_lo == 2'd0 : lp_hi == 2'd0;",
     "wire        leap  = lp_lo == 2'd0;", "the /100 and /400 rules (tc3)"),
    ("rtc-dayge", "rtc", "v1_rtc.sv", "wire        c_mon = c_day && b_day >= {2'b0, mlen};",
     "wire        c_mon = c_day && b_day == {2'b0, mlen};", "the invalid day 02-31 (tc3)"),
    ("rtc-adj-start", "rtc", "v1_rtc.sv", "adj ? (st_n && b_sec >= 8'h30)", "adj ? (b_sec >= 8'h30)",
     "ADJ carries with START = 0 (tc3)"),
    ("rtc-af-edge", "rtc", "v1_rtc.sv", "if (match && rtcen)                               af <= 1'b1;",
     "if (match && rtcen && c1hz)                       af <= 1'b1;", "the alarm compared only at a carry (tc3)"),
    ("rtc-af-ungated", "rtc", "v1_rtc.sv", "if (match && rtcen)                               af <= 1'b1;",
     "if (match)                                        af <= 1'b1;", "AF with RTCEN = 0 (tc2 P)"),
    ("rtc-mstp", "rtc", "v1_rtc.sv", "wire       acc  = s_in && !mstp &&", "wire       acc  = s_in &&",
     "registers served while MSTP1 (H)"),
    ("rtc-cf1", "rtc", "v1_rtc.sv", "else if (wr && s_k == 4'd14)                         cf <= pq.wdata[7];",
     "else if (wr && s_k == 4'd14)                         cf <= 1'b0;", "CF not settable by a written 1 (P)"),
    ("rtc-man-rtcen", "rtc", "v1_rtc.sv", "        pes <= '0;\n      end", "        pes <= '0;\n        rtcen <= 1'b1;\n      end",
     "a manual reset sets RTCEN (hm Table 11.2 note 2: held)"),
    ("rtc-man-start", "rtc", "v1_rtc.sv", "        pes <= '0;\n      end", "        pes <= '0;\n        start <= 1'b1;\n      end",
     "a manual reset sets START (hm Table 11.2 note 2: held)"),
    ("rtc-wk", "rtc", "v1_rtc.sv", "wk  <= c_day ? (b_wk >= 3'd6 ? 3'd0 : b_wk + 3'd1) : b_wk;",
     "wk  <= c_day ? (b_wk >= 3'd7 ? 3'd0 : b_wk + 3'd1) : b_wk;", "day of week 6 -> 7 (tc3)"),
]


def run(m, cycles):
    mid, unit, fname, orig, new, what = m
    top, files = UNITS[unit]
    d = os.path.join(OUT, mid)
    shutil.rmtree(d, ignore_errors=True)
    os.makedirs(d)
    srcs = []
    for f in ["v1_soc_pkg.sv"] + files:
        src = open(os.path.join(ROOT, "rtl", "v1", f)).read()
        if f == fname:
            n = src.count(orig)
            if n != 1:
                return (mid, "BADPATTERN", f"'{orig}' occurs {n} times in {f}", what)
            src = src.replace(orig, new)
        p = os.path.join(d, f)
        open(p, "w").write(src)
        srcs.append(p)
    tbdir = os.path.join(ROOT, "tb")
    cmd = ["verilator", "--cc", "--exe", "--build", "-O1", "-Wno-WIDTH", "-Wno-UNOPTFLAT", "-Wno-DECLFILENAME",
           "-Wno-UNUSEDSIGNAL", "-Wno-UNUSEDPARAM", "-Wno-LATCH", "-j", "2", "-CFLAGS", f"-O1 -I{tbdir}",
           "--top-module", top] + srcs + [os.path.join(tbdir, top + ".sv"), os.path.join(tbdir, top + ".cpp"),
           "--Mdir", os.path.join(d, "obj"), "-o", "tb"]
    b = subprocess.run(cmd, capture_output=True, text=True)
    if b.returncode:
        return (mid, "BUILDFAIL", b.stderr[-400:], what)
    try:                                                   # a testbench must fail, never hang: a timeout is a defect
        r = subprocess.run([os.path.join(d, "obj", "tb"), "--cycles", str(cycles)], capture_output=True, text=True,
                           timeout=TIMEOUT)
    except subprocess.TimeoutExpired:
        return (mid, "TIMEOUT", f"no result after {TIMEOUT} s (a testbench loop without a bound?)", what)
    lines = [l for l in r.stdout.splitlines() if "FAIL [" in l]
    first = lines[0].strip() if lines else ""
    return (mid, "caught" if r.returncode else "SURVIVED", first[:150], what)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--jobs", type=int, default=os.cpu_count() or 4)
    ap.add_argument("--only", default="")
    ap.add_argument("--cycles", type=int, default=300000)
    a = ap.parse_args()
    ms = [m for m in MUTANTS if not a.only or m[0] in a.only.split(",")]
    with ThreadPoolExecutor(max_workers=max(1, a.jobs // 2)) as ex:
        res = list(ex.map(lambda m: run(m, a.cycles), ms))
    bad = 0
    for mid, st, first, what in res:
        print(f"{mid:18s} {st:10s} {what}")
        if first:
            print(f"{'':18s} {'':10s}   {first}")
        bad += st != "caught"
    print(f"v1-clk-mutants: {len(res) - bad} of {len(res)} planted bugs caught")
    print("v1-clk-mutants: " + ("PASS" if not bad else "FAIL"))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
