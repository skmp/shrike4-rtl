#!/usr/bin/env python3
"""v1_mutants.py -- planted-bug check of WI-CORE-A step A1's mechanisms (make v1-mutants; rtl/v1/SOC.md 3.2.2, 4.2, 5).

Each mutant copies one rtl/v1 file into build/v1_mut/<id>/, replaces exactly one source fragment (the script refuses a
pattern that does not occur exactly once, so a mutant can never silently be the original), builds the `make v1`
testbench against the copy (make v1-build with V1_TB_CORE / V1_TB_MDIR overridden) and runs the probe the mutant
names: a handful of sweep records through tb/v1_tb.cpp, timing and final state against the console and the ISS, or
final state only with the MA hold (--slow) or clock-enable (--run-gap) knob; or, for the probe "unit", the ideal
memory's own unit testbench (make v1-ideal-mem-build with V1_IM_SRC / V1_IM_MDIR).  A mutant is caught when the
testbench exits non-zero.  The unmodified build must pass every probe first (the control).  The script exits 1 if the control
fails or any mutant survives.

    python3 tb/v1_mutants.py [--jobs N] [--only ID,ID] [--keep]
"""
import argparse, os, shutil, subprocess, sys
from concurrent.futures import ThreadPoolExecutor

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "build", "v1_mut")
VEC = ["--vec", "build/v1/vectors.bin", "--t2", "model/timings2"]
CORE = ["rtl/v1/v1_core.sv", "rtl/v1/v1_fdiv.sv", "rtl/v1/v1_fetch.sv", "rtl/v1/v1_fpu.sv", "rtl/v1/v1_ideal_mem.sv"]

# the probes (tb/v1_tb.cpp arguments)
LDTLB = "85338,85339,85340,85341,85342,85343,85886,86744,87244,87744,88244"   # SOC.md 5: LDTLB x cache ops
# OCBWB before a load / store / OCBI / PREF / LDTLB / NOP, OCBI / OCBP / PREF / MOVCA.L before a NOP, OCBI before a
# store, a plain store and a plain load
CACHE = "87397,87437,87747,87750,87744,87746,86746,87246,88246,85888,86437,4397,6650"
# the six OCBI records the V-clear comparator reproduces (SOC.md 3.3.1 C1, 5): OCBI ; MOV.x R0,@(disp,GBR), same line
OCBI6 = "86478,86479,86481,86482,86484,86485"
PROBES = {
    "ldtlb":  ["--rid", LDTLB],
    "ocbi6":  ["--rid", OCBI6],
    "cache":  ["--rid", CACHE],
    "slow":   ["--shard", "0/400", "--func-only", "--slow", "3"],
    "gap":    ["--shard", "1/400", "--func-only", "--run-gap", "3"],
    "unit":   ["--n", "60000"],                   # tb/v1_ideal_mem_tb.cpp
}

# id, file, original fragment, mutated fragment, probe, what it breaks
MUTANTS = [
    ("lt-ex", "v1_core.sv",
     "wire lt_ma = ma[0].v && ma[0].form == F_LDTLB;", "wire lt_ma = ex[0].v && ex[0].form == F_LDTLB;", "ldtlb",
     "LDTLB's freeze in EX (v1's tfrz position): it overlaps an older OCBI / OCBP's miss path (SOC.md 5: 86744 / 87244)"),
    ("lt-1", "v1_core.sv",
     "assign lt_frz = lt_ma && lt_n != 2'd2;", "assign lt_frz = lt_ma && lt_n != 2'd1;", "ldtlb",
     "LDTLB freezes 1 cycle instead of 2"),
    ("coll-off", "v1_core.sv",
     "oc_coll = lreq.v && lrsp.ack && lreq.kind == DK_OCBWB && use_ex;", "oc_coll = 1'b0;", "cache",
     "no OCBWB port collision"),
    ("coll-ldtlb", "v1_core.sv",
     "if (ex[l].v && (ex_res[l].dk != DK_NONE || ex[l].form == F_LDTLB)) use_ex = 1'b1;",
     "if (ex[l].v && ex_res[l].dk != DK_NONE) use_ex = 1'b1;", "ldtlb",
     "LDTLB in EX does not collide with an OCBWB's second port cycle (87744)"),
    ("coll-any", "v1_core.sv",
     "oc_coll = lreq.v && lrsp.ack && lreq.kind == DK_OCBWB && use_ex;",
     "oc_coll = lreq.v && lrsp.ack && (lreq.kind == DK_OCBWB || lreq.kind == DK_OCBI) && use_ex;", "cache",
     "an OCBI also keeps the port a second cycle"),
    ("madone-off", "v1_core.sv",
     "if (frz && ma_acc[l] && lreq.v && lrsp.ack) ma[l].ma_done <= 1'b1;", "if (1'b0) ma[l].ma_done <= 1'b1;", "cache",
     "an ack taken while the pipeline holds is not latched: the request is presented again (the contract monitor)"),
    ("madone-run", "v1_core.sv",
     "    end else begin\n      // ---- the MA port (SOC.md 3.2.2 rule 2).",
     "    end else if (run) begin\n      // ---- the MA port (SOC.md 3.2.2 rule 2).", "gap",
     "the ack latch under the run enable: an ack while run = 0 is lost"),
    ("rdq-off", "v1_core.sv",
     "if (ma[l].ma_done) ma_rd = ma_rdq;", "if (1'b0) ma_rd = ma_rdq;", "gap",
     "a latched ack's data is not used when the micro-op leaves MA later"),
    ("mah-off", "v1_core.sv",
     "assign mah = lreq.v && !lrsp.ack && !lrsp.frz;", "assign mah = 1'b0;", "slow",
     "no MA hold: the pipeline runs on while an access waits"),
    ("im-miss1", "v1_ideal_mem.sv",
     "localparam logic [6:0] MISS_N = 7'd2;", "localparam logic [6:0] MISS_N = 7'd1;", "cache",
     "the operand cache's miss path 1 cycle instead of 2"),
    ("im-ocbi-keep", "v1_ideal_mem.sv",
     "        DK_OCBI, DK_OCBP, DK_TAS_PURGE:\n", "        DK_OCBP, DK_TAS_PURGE:\n", "cache",
     "OCBI leaves the line valid (its steady state becomes a hit)"),
    ("im-st-miss", "v1_ideal_mem.sv",
     "DK_ST, DK_RMW_WR, DK_TAS_WR, DK_MOVCA:           need_bus = !cach || ocq.x.wt || !hit;",
     "DK_ST, DK_RMW_WR, DK_TAS_WR, DK_MOVCA:           need_bus = 1'b1;", "cache",
     "a store to a present copy-back line takes the miss path"),
    ("im-ocbwb-inv", "v1_ideal_mem.sv",
     "          if (hit) lu[idx] <= 1'b0;", "          if (hit) begin lu[idx] <= 1'b0; lv[idx] <= 1'b0; end", "cache",
     "OCBWB invalidates the line"),
    # the ideal memory's unit testbench
    ("u-miss1", "v1_ideal_mem.sv",
     "localparam logic [6:0] MISS_N = 7'd2;", "localparam logic [6:0] MISS_N = 7'd1;", "unit", "miss path 1 cycle"),
    ("u-frz-ack", "v1_ideal_mem.sv",
     "ocr.frz   = ocq.v && !vch && need_bus && wcnt < MISS_N;", "ocr.frz   = ocq.v && !vch && need_bus && wcnt <= MISS_N;",
     "unit", "frz also in the ack cycle"),
    # the V-clear comparator (conformance step, SOC.md 3.3.1 C1 / 5)
    ("vc-off", "v1_ideal_mem.sv",
     "vch     = ocq.v && cach && vc_v && vc_idx == idx;", "vch     = 1'b0;", "ocbi6",
     "no V-clear comparator: the store right behind a same-line OCBI hit compares in MA1 (the six records 1 short)"),
    ("u-vc-off", "v1_ideal_mem.sv",
     "vch     = ocq.v && cach && vc_v && vc_idx == idx;", "vch     = 1'b0;", "unit",
     "no V-clear comparator (the unit model holds the same-entry access after an invalidating hit)"),
    ("u-vc-idx", "v1_ideal_mem.sv",
     "vch     = ocq.v && cach && vc_v && vc_idx == idx;", "vch     = ocq.v && cach && vc_v;", "unit",
     "the comparator holds an access to any entry after an invalidating hit"),
    ("u-vc-ocbwb", "v1_ideal_mem.sv",
     "(ocq.kind == DK_OCBI || ocq.kind == DK_OCBP || ocq.kind == DK_TAS_PURGE);",
     "(ocq.kind == DK_OCBI || ocq.kind == DK_OCBP || ocq.kind == DK_TAS_PURGE || ocq.kind == DK_OCBWB);", "unit",
     "an OCBWB hit (no V write) also arms the comparator"),
    ("u-vc-count", "v1_ideal_mem.sv",
     "else if (!vch) wcnt <= wcnt + 7'd1;", "else wcnt <= wcnt + 7'd1;", "unit",
     "the comparator's cycle counts as the held access's first miss-path cycle"),
    ("u-ocram-idx", "v1_ideal_mem.sv",
     "q_extract(ocram[{ocq.va[13], ocq.va[11:3]}], ocq.va[2:0])", "q_extract(ocram[{ocq.va[12], ocq.va[11:3]}], ocq.va[2:0])",
     "unit", "OC RAM read index VA[12] instead of VA[13]"),
    ("u-a7-data", "v1_ideal_mem.sv",
     "ocq.x.rg == RG_A7    ? 64'd0 : im_dm_rdata;", "im_dm_rdata;", "unit", "an area-7 load returns memory, not 0"),
    ("u-por-keep", "v1_ideal_mem.sv",
     "    if (rst.por) begin\n      lv <= '0;", "    if (1'b0) begin\n      lv <= '0;", "unit",
     "a power-on reset keeps the lines valid"),
    ("u-cpu-clear", "v1_ideal_mem.sv",
     "    if (rst.por) begin\n      lv <= '0;", "    if (rst.cpu) begin\n      lv <= '0;", "unit",
     "a CPU-only / manual reset invalidates the lines"),
    ("u-wt-alloc", "v1_ideal_mem.sv",
     "if (!ocq.x.wt) begin lv[idx] <= 1'b1;", "if (1'b1) begin lv[idx] <= 1'b1;", "unit",
     "a write-through store allocates"),
    ("u-unc-fill", "v1_ideal_mem.sv",
     "end else if (oc_ack && ocq.x.rg != RG_OCRAM && cach) begin", "end else if (oc_ack && ocq.x.rg != RG_OCRAM) begin",
     "unit", "an uncached access changes the line state"),
    ("u-fetch-poison", "v1_ideal_mem.sv",
     "im_if_req  = ifq.v && !if_poison && !if_stall && if_hit;", "im_if_req  = ifq.v && !if_stall && if_hit;", "unit",
     "a poisoned fetch reads memory"),
    # M2 (agents/reports/integration/M2.md): the fetch follows v1_mmu's answer as v1_icache does (SOC.md 3.2.1)
    ("u-fetch-stall", "v1_ideal_mem.sv",
     "if_res     = ifq.v && (if_poison || (!if_stall && if_hit));", "if_res     = ifq.v && (if_poison || if_hit || if_stall);",
     "unit", "a fetch is acked while an ITLB refill owns it (the refill's cost vanishes: mmu T1 16 -> 9)"),
    ("u-fetch-busy", "v1_ideal_mem.sv",
     "ifr.busy   = if_stall;", "ifr.busy   = 1'b0;", "unit",
     "no busy during an ITLB refill: a redirect abandons the refill (mmu V: the speculative refill's 6 lost)"),
    ("u-fetch-va", "v1_ideal_mem.sv",
     "im_if_addr = {ifq.va[31:29], if_pa[28:2], 2'b00};", "im_if_addr = {ifq.va[31:2], 2'b00};", "unit",
     "a translated fetch is read at its VA, not the ITLB's PA"),
    ("u-sq-slow", "v1_ideal_mem.sv",
     "sq_ack    = sqq.v && sq_wcnt == (in_slow(sqq.va) ? {1'b0, slow_n} : 7'd0);", "sq_ack    = sqq.v;", "unit",
     "the SQ port ignores the slow knob"),
    ("u-wr-early", "v1_ideal_mem.sv",
     "im_dm_req   = oc_ack && (rd_k || wr_k) && ocq.x.rg == RG_MEM;",
     "im_dm_req   = ocq.v && (rd_k || wr_k) && ocq.x.rg == RG_MEM;", "unit",
     "the memory access happens in every cycle of the request, not in its ack cycle"),
    ("u-kill-count", "v1_ideal_mem.sv",
     "if (rst.cpu || !ocq.v || oc_ack) wcnt <= 7'd0;", "if (oc_ack) wcnt <= 7'd0;", "unit",
     "a withdrawn request's waiting count survives"),
]


def sh(cmd, log):
    with open(log, "w") as f:
        return subprocess.run(cmd, cwd=ROOT, stdout=f, stderr=subprocess.STDOUT).returncode


def build(tag, core, jobs, unit=False):
    mdir = os.path.join(OUT, tag, "obj_unit" if unit else "obj")
    log = os.path.join(OUT, tag, "build_unit.log" if unit else "build.log")
    os.makedirs(os.path.dirname(log), exist_ok=True)
    if unit:
        im = [f for f in core if os.path.basename(f) == "v1_ideal_mem.sv"][0]
        cmd = ["make", "-s", "v1-ideal-mem-build", "NJOBS=%d" % jobs, "V1_IM_MDIR=" + mdir, "V1_IM_SRC=" + im]
    else:
        cmd = ["make", "-s", "v1-build", "NJOBS=%d" % jobs, "V1_TB_MDIR=" + mdir, "V1_TB_CORE=" + " ".join(core)]
    rc = sh(cmd, log)
    return os.path.join(mdir, "tb") if rc == 0 else None


def run_probe(exe, probe, log):
    return sh([exe] + ([] if probe == "unit" else VEC) + PROBES[probe], log)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--jobs", type=int, default=4, help="mutants built and run at the same time")
    ap.add_argument("--only", default="")
    ap.add_argument("--keep", action="store_true", help="keep build/v1_mut (default: removed when every check passes)")
    a = ap.parse_args()
    only = set(x for x in a.only.split(",") if x)
    muts = [m for m in MUTANTS if not only or m[0] in only]
    ncpu = min(12, os.cpu_count() or 4)          # the machine's build-parallelism cap (12)
    bj = max(1, ncpu // max(1, a.jobs))
    os.makedirs(OUT, exist_ok=True)

    # the control: the unmodified sources pass every probe the mutants use
    probes = sorted(set(m[4] for m in muts))
    exe = build("control", CORE, ncpu) if any(p != "unit" for p in probes) else ""
    uexe = build("control", CORE, ncpu, unit=True) if "unit" in probes else ""
    if exe is None or uexe is None:
        print("control: build FAILED (%s)" % os.path.join(OUT, "control"))
        return 1
    ctl_ok = True
    for p in probes:
        rc = run_probe(uexe if p == "unit" else exe, p, os.path.join(OUT, "control", p + ".log"))
        print("control  probe %-6s %s" % (p, "pass" if rc == 0 else "FAIL (the unmodified core must pass)"))
        ctl_ok &= rc == 0
    if not ctl_ok:
        return 1

    def one(m):
        mid, fname, old, new, probe, why = m
        src = os.path.join(ROOT, "rtl", "v1", fname)
        text = open(src).read()
        if text.count(old) != 1:
            return (mid, "BAD PATTERN (occurs %d times)" % text.count(old), why, probe)
        d = os.path.join(OUT, mid)
        os.makedirs(d, exist_ok=True)
        dst = os.path.join(d, fname)
        open(dst, "w").write(text.replace(old, new))
        core = [os.path.relpath(dst, ROOT) if os.path.basename(f) == fname else f for f in CORE]
        exe = build(mid, core, bj, unit=probe == "unit")
        if not exe:
            return (mid, "BUILD FAILED", why, probe)
        rc = run_probe(exe, probe, os.path.join(d, probe + ".log"))
        return (mid, "caught" if rc != 0 else "SURVIVED", why, probe)

    with ThreadPoolExecutor(max_workers=a.jobs) as ex:
        res = list(ex.map(one, muts))
    bad = 0
    for mid, st, why, probe in res:
        print("%-13s %-8s probe %-6s %s" % (mid, st, probe, why))
        bad += st != "caught"
    print("v1-mutants: %d / %d caught" % (len(res) - bad, len(res)))
    if not bad and not a.keep:
        shutil.rmtree(OUT, ignore_errors=True)
    print("v1-mutants: %s" % ("PASS" if not bad else "FAIL"))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
