#!/usr/bin/env python3
"""v1_fetchx_mutants.py -- planted bugs in WI-CORE-B's fetch (rtl/v1/v1_fetch.sv) and the core's fetch hooks
(rtl/v1/v1_core.sv), each of which must be caught (make v1-fetchx-mutants; rtl/v1/SOC.md 4.1); and planted bugs in
rtl/v1/v1_icache.sv (WI-CACHE's file, mutated only in the copy) that only the IC-in-the-loop run (`--ic`, SOC.md 8.3 M3,
8.4 WI-CORE-B (1)) can catch, to show that the option tests the real chain.

Each mutant copies the one file it changes into build/v1_fetchx_mut/<id>/, replaces exactly one fragment (a pattern
that does not occur exactly once is refused, so a mutant can never silently be the original), builds the testbench its
probe needs against the copy and runs the probe:
  fx          tb/v1_fetchx_tb's directed rows (the ISS, the handshake / hold monitors, the rows' own checks)
  fxrand      500 random programs under random fetch timing against the ISS
  fxic        300 random programs on the real v1_mmu -> v1_icache -> tb/v1_bsc_stub.sv chain (--ic): the ISS, the
              monitors, every acked word equal to memory, the stub's handshake check
  counts      the console's debug-ubc-hudi probes ubcbrk / ubcevt on v1-exc's harness, tb/v1_fetchx_counts.py
  exc:A,B     tb/v1_exc_tb's directed rows A, B (--only)
  bm:PROG     a soc-design / core-exc console probe on v1-exc's harness, check.py --level fetch
The unmodified sources must pass every probe first (the control).  Exit 1 if the control fails or a mutant survives.

    python3 tb/v1_fetchx_mutants.py [--jobs N] [--only ID,ID] [--keep]
"""
import argparse, os, shutil, subprocess, sys
from concurrent.futures import ThreadPoolExecutor

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "build", "v1_fetchx_mut")
CORE = ["rtl/v1/v1_core.sv", "rtl/v1/v1_fdiv.sv", "rtl/v1/v1_fetch.sv", "rtl/v1/v1_fpu.sv", "rtl/v1/v1_ideal_mem.sv"]
BM = os.path.join(ROOT, "build", "bm")
F, C, IC = "rtl/v1/v1_fetch.sv", "rtl/v1/v1_core.sv", "rtl/v1/v1_icache.sv"
MAXJ = 12                                      # the machine's build-parallelism cap (all concurrent builds together)

# Equivalent, so not listed: WI-CORE-A's d1-rte re-aimed at the fetch MD (`assign f_md = sr_rd[30]`): the RTE slot's
# word is always requested -- its MD latched with the request -- before RTE's SR write lands (the fetch asks for the
# word after the RTE's as soon as the RTE is at D, and every hold freezes the RTE with the fetch); the behaviour stays
# checked by the rows rte_slot_late_md (v1-fetchx) and vx_fetch_rte_slot_p1_user (v1-exc).
# Equivalent under this fetch, so not listed: v1_icache acking its kept uncached word for any request
# (`if (ur.va == ifq.va[31:2]) unc_ack` -> `unc_ack`): the fetch presents its busy request again, unchanged, until the
# ack (SOC.md 3.2.1; the tb's handshake monitor), so no other request meets a kept word; only a reset in flight
# reaches the drop, and the random programs make none (fxic survived: 300 programs, conform-WI-CORE-B).
# id, file, original fragment, mutated fragment, probe, what it breaks
MUTANTS = [
    ("room-after", F, "room = (IQN - int'(cnt)) >= 2 || rd_any;", "room = (IQN - int'(after_pop)) >= 2 || rd_any;", "counts",
     "v1's rule: room judged after this cycle's issues (ld / st 6 fetches, console 5)"),
    ("no-rdreq", F, "room = (IQN - int'(cnt)) >= 2 || rd_any;", "room = (IQN - int'(cnt)) >= 2;", "counts",
     "a redirect cycle with a full queue requests nothing (ex8 7, br 12, ldcgbr 4: console 8 13 5)"),
    ("hold-ma-early", F, "pres = want && !hold && !(hold_ma && hma_q) && !own_odd && !own_usr;",
     "pres = want && !hold && !hold_ma && !own_odd && !own_usr;", "counts",
     "the MA unit's hold stops the request in its first cycle (ldCCN / stP4 3 fetches, console 4)"),
    ("iqn4", F, "parameter int IQN = 6 ", "parameter int IQN = 4 ", "counts",
     "v1's 4-halfword queue (X rows 0x06 short; the UBC ADD-settle row breaks at 9 / 10)"),
    ("no-busy-wait", F, "wait_rq = (pres && !ifr.ack && ifr.busy) || (!pres && rq_v && rq_busy);", "wait_rq = 1'b0;", "fx",
     "a redirect abandons an uncached word / ITLB refill on the bus (mmu V 6, entry F, the handshake monitor)"),
    ("poison-no-stop", F, "if (wfx != FX_NONE) stop <= 1'b1;             // a poisoned word",
     "if (1'b0) stop <= 1'b1;             // a poisoned word", "fx", "fetch runs on past a poisoned word"),
    ("own-odd-off", F, "own_odd = fpc_r[0];", "own_odd = 1'b0;", "fx", "a branch to an odd address is fetched, not poisoned"),
    ("own-usr-off", F, "own_usr = !md_q && fpc_r[31];", "own_usr = 1'b0;", "fx",
     "a user-mode fetch at or above 0x80000000 goes to memory (SOC.md 4.1 item 2: no ireq)"),
    ("odd-pre", F, "ubc_q = '{v: (pres && ifr.ack) || (want && !hold && !(hold_ma && hma_q) && !own_odd && own_usr),",
     "ubc_q = '{v: (pres && ifr.ack) || (want && !hold && !(hold_ma && hma_q) && (own_odd || own_usr)),",
     "exc:vx_fetch_odd_no_pre", "an odd target is presented to the UBC and takes its even halfword's tag (re-homed WI-CORE-A "
     "d1-odd-pre: 0x1E0 instead of 0x0E0)"),
    ("skip-redirects", C, "end else if (iss0 && br_taken[0] && !br_skip[0]) begin", "end else if (iss0 && br_taken[0]) begin",
     "fx", "a taken BT / BF disp 0 redirects like any branch (btt / bfshort 6 cycles, console 5)"),
    ("skip-no-nullify", F, "skip_now = skip && int'(cnt) > int'(pop);", "skip_now = 1'b0;", "fx",
     "the skip form does not nullify a queued fall-through"),
    ("slot-pend-both", F, "hw_n = (fpc_r[1] || fpc_r[0] || slot_only || slot_now || own_usr) ? 2'd1 : 2'd2;",
     "hw_n = (fpc_r[1] || fpc_r[0] || slot_only || own_usr) ? 2'd1 : 2'd2;", "fxrand",
     "a delay slot fetched in its branch's redirect cycle brings the halfword after it too"),
    ("pre-armed-off", F, "if (ubc_pre_armed) begin", "if (1'b0) begin", "fx",
     "the pre-armed hold is off: a BSR issues before its slot's pre-execution break is seen"),
    ("claim-window-off", F,
     "end else if (pres && ifr.ack && ifr.fx == FX_NONE && !rp_v && k >= int'(cnt) && k - int'(cnt) < int'(hw_n)) begin",
     "end else if (1'b0) begin", "exc:c1j_ld1_irq,c1j_ld2_irq,c1j_st1_irq,c1j_pc1_irq,c1j_ldxld1_irq,c1j_pref1_irq",
     "the claim window stops at the queue (v1's 'fetched at t': c1j 1 / 2 after X do not claim)"),
    ("q-dep-off", C, "3'(!c[0].br_dly && q2_mem && !q_dep[0])", "3'(!c[0].br_dly && q2_mem)", "bm:c1b",
     "a follower-candidate take's claimant that reads the issuing leader's result claims (c1b d0: 10, console 8)"),
    ("tgt-off", F, "hw[h].tgt = tgt_next && h == 0;", "hw[h].tgt = 1'b0;", "counts",
     "the first instruction after a redirect pairs as a follower"),
    ("starve-off", C, "wire f_starve = !q_v[0] && (f_busy_wait || f_starve_q);", "wire f_starve = 1'b0;", "fx",
     "no interrupt boundary while the window is empty and the fetch waits"),
    ("excsave-early", C, "if (exc_sv && !frz && !f_busy_wait) ex_in[0]", "if (exc_sv && !frz) ex_in[0]", "fx",
     "EXC_SAVE issues before the vector's fetch when the redirect waits for busy"),
    ("hold-ignored", F, "pres = want && !hold && !(hold_ma && hma_q) && !own_odd && !own_usr;",
     "pres = want && !(hold_ma && hma_q) && !own_odd && !own_usr;", "fx",
     "fetch runs during an early hold (the entry's fetch stop, asleep)"),
    ("miss-replay-off", F, "ev_fetch = first || miss_d1 || (pres && ifr.ack && rq_v && rq_missed);", "ev_fetch = first;",
     "fx", "0x06 counts a miss once (console: + 2 per miss, icm)"),
    # the IC in the loop (--ic): the fetch's busy wait against the real v1_icache, and v1_icache bugs the scripted C++
    # model cannot show (the fetch sees a wrong word or the handshake breaks)
    ("no-busy-wait-ic", F, "wait_rq = (pres && !ifr.ack && ifr.busy) || (!pres && rq_v && rq_busy);", "wait_rq = 1'b0;",
     "fxic", "a redirect abandons v1_icache's uncached word on the bus (the handshake monitor, SOC.md 3.2.1)"),
    ("ic-byp-any-q", IC, "if (lm && fb_v && fb_q == q) begin fl_ack = 1'b1; qd = fb_d; end",
     "if (lm && fb_v) begin fl_ack = 1'b1; qd = fb_d; end", "fxic",
     "the fill's beat bypass serves its quadword to a fetch of another quadword of the line"),
    ("ic-unc-lane", IC, "ur.d   <= ur.pa[2] ? bs_unc.rd_data[63:32] : bs_unc.rd_data[31:0];",
     "ur.d   <= bs_unc.rd_data[31:0];", "fxic", "an uncached word at PA[2] = 1 takes the low longword of the beat"),
    ("ic-no-arrived-wait", IC, "else if (lm && fl.arrived[q]) fl_ack = 1'b1;", "else if (lm) fl_ack = 1'b1;", "fxic",
     "a fetch of the filling line is acked before its quadword has arrived (the array's old contents)"),
    ("ic-other-line", IC, "else if (lm && fl.arrived[q]) fl_ack = 1'b1;", "else if (fl.arrived[q]) fl_ack = 1'b1;",
     "fxic", "a fetch of another line is acked from the filling line's arrived quadwords"),
]


def sh(cmd, log):
    with open(log, "w") as f:
        return subprocess.run(cmd, cwd=ROOT, stdout=f, stderr=subprocess.STDOUT).returncode


def build(kind, tag, core, jobs, icu=IC):
    d = os.path.join(OUT, tag)
    os.makedirs(d, exist_ok=True)
    mdir = os.path.join(d, "obj_" + kind)
    log = os.path.join(d, "build_%s.log" % kind)
    if kind == "fx":
        cmd = ["make", "-s", "v1-fetchx-build", "NJOBS=%d" % jobs, "V1_FX_MDIR=" + mdir, "V1_FX_CORE=" + " ".join(core),
               "V1_FX_ICU=" + icu]
    else:
        cmd = ["make", "-s", "v1-exc-build", "NJOBS=%d" % jobs, "V1_EXC_MDIR=" + mdir, "V1_EXC_CORE=" + " ".join(core)]
    return os.path.join(mdir, "tb") if sh(cmd, log) == 0 else None


def needs(probe):
    return "fx" if probe in ("fx", "fxrand", "fxic") else "exc"


def run_probe(exe, probe, d):
    log = os.path.join(d, probe.replace(":", "_").replace(",", "+")[:60] + ".log")
    if probe == "fx":
        return sh([exe], log)
    if probe == "fxrand":
        return sh([exe, "--rand", "500", "--seed", "11"], log)
    if probe == "fxic":
        return sh([exe, "--rand", "300", "--seed", "21", "--ic"], log)
    if probe.startswith("exc:"):
        return sh([exe, "--only", probe[4:]], log)
    if probe == "counts":
        outs = []
        for p in ("ubcbrk", "ubcevt"):
            o = os.path.join(d, p + ".out")
            rc = sh([exe, "--bm", os.path.join(BM, "debug-ubc-hudi", p + ".bin"), "--out", o, "--maxcyc", "40000000"],
                    log + "." + p)
            if rc:
                return rc
            outs.append(o)
        return sh(["python3", "tb/v1_fetchx_counts.py"] + outs, log)
    prog = probe.split(":")[1]
    suite = "core-exc" if prog in ("c1i", "c1k") else "soc-design"
    o = os.path.join(d, prog + ".out")
    rc = sh([exe, "--bm", os.path.join(BM, suite, prog + ".bin"), "--out", o, "--maxcyc", "20000000"], log)
    if rc:
        return rc
    return sh(["python3", "tb/bm/%s/check.py" % suite, o, "--level", "fetch", "--prog", prog], log + ".check")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--jobs", type=int, default=6)
    ap.add_argument("--only", default="")
    ap.add_argument("--keep", action="store_true")
    a = ap.parse_args()
    only = set(x for x in a.only.split(",") if x)
    muts = [m for m in MUTANTS if not only or m[0] in only]
    ncpu = min(MAXJ, os.cpu_count() or 4)
    bj = max(1, ncpu // max(1, a.jobs))
    os.makedirs(OUT, exist_ok=True)
    for suite in ("soc-design", "core-exc", "debug-ubc-hudi"):
        if subprocess.run(["make", "-s", "-C", "tb/bm/" + suite], cwd=ROOT, stdout=subprocess.DEVNULL).returncode:
            print("tb/bm/%s: build FAILED" % suite)
            return 1
    probes = sorted(set(m[4] for m in muts))
    exe = {k: build(k, "control", CORE, ncpu) for k in sorted(set(needs(p) for p in probes))}
    if None in exe.values():
        print("control: build FAILED (%s)" % os.path.join(OUT, "control"))
        return 1
    ok = True
    for p in probes:
        rc = run_probe(exe[needs(p)], p, os.path.join(OUT, "control"))
        print("control  probe %-24s %s" % (p[:24], "pass" if rc == 0 else "FAIL (the unmodified sources must pass)"))
        ok &= rc == 0
    if not ok:
        return 1

    def one(m):
        mid, f, old, new, probe, why = m
        text = open(os.path.join(ROOT, f)).read()
        if text.count(old) != 1:
            return (mid, "BAD PATTERN (occurs %d times)" % text.count(old), why, probe)
        d = os.path.join(OUT, mid)
        os.makedirs(d, exist_ok=True)
        dst = os.path.join(d, os.path.basename(f))
        open(dst, "w").write(text.replace(old, new))
        core = [os.path.relpath(dst, ROOT) if x == f else x for x in CORE]
        e = build(needs(probe), mid, core, bj, os.path.relpath(dst, ROOT) if f == IC else IC)
        if not e:
            return (mid, "BUILD FAILED", why, probe)
        rc = run_probe(e, probe, d)
        return (mid, "caught" if rc != 0 else "SURVIVED", why, probe)

    with ThreadPoolExecutor(max_workers=a.jobs) as ex:
        res = list(ex.map(one, muts))
    bad = 0
    for mid, st, why, probe in res:
        print("%-17s %-8s probe %-10s %s" % (mid, st, probe[:10], why))
        bad += st != "caught"
    print("v1-fetchx-mutants: %d / %d caught" % (len(res) - bad, len(res)))
    if not bad and not a.keep:
        shutil.rmtree(OUT, ignore_errors=True)
    print("v1-fetchx-mutants: %s" % ("PASS" if not bad else "FAIL"))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
