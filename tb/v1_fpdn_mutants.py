#!/usr/bin/env python3
"""v1_fpdn_mutants.py -- planted-bug check of WI-FPDN (make v1-fpdn-mutants; rtl/v1/SOC.md 4.3.8, 9.17): FPSCR.DN = 0
in v1_fp_pkg, v1_fdiv, the vector unit and v1_fpu.

Each mutant copies one RTL file into build/v1_fpdn_mut/<id>/, replaces exactly one source fragment (a pattern that does
not occur exactly once is refused, so a mutant can never silently be the original), builds the testbench its probe
needs against the copy and runs it:
  unit   tb/v1_fpdn_tb --hw --vec --rand 20000   the console records through the RTL functions / divider / vector
                                                  unit, random operations against tb/sh4_fpdn.h, the FIPR / FTRV /
                                                  FSRRA captures
  core   tb/v1_fpdn_core --p2step 64 --fiprstep 4096   the console records as programs on the v1 core
A mutant is caught when the probe fails.  The unmodified build must pass both probes first (the control).  Exit 1 if
the control fails or any mutant survives.
    python3 tb/v1_fpdn_mutants.py [--jobs N] [--only ID,ID] [--keep]
"""
import argparse, os, shutil, subprocess, sys
from concurrent.futures import ThreadPoolExecutor

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "build", "v1_fpdn_mut")
NCPU = min(12, os.cpu_count() or 8)                        # compile jobs in all (the machine: 15 GB RAM)
FP, DIV, NR, VEC, FPU = ("rtl/v1/v1_fp.sv", "rtl/v1/v1_fdiv.sv", "rtl/fpu_vec/fpu_normround.sv",
                         "rtl/fpu_vec/fpu_vec_top.sv", "rtl/v1/v1_fpu.sv")
VAR = {FP: "V1_FPDN_FP", DIV: "V1_FPDN_DIV", NR: "V1_FPDN_NR", VEC: "V1_FPDN_VEC", FPU: "V1_FPDN_FPU"}

# id, file, original fragment, mutated fragment, probe, what it breaks
MUTANTS = [
    # ---- the rounder: gradual underflow and U (v1_fp.sv round_pack)
    ("dn0-flush", FP, "      if (!dn0 && below) begin", "      if (below) begin", "unit",
     "DN = 0 flushes like DN = 1: no gradual underflow"),
    ("u-exact", FP, "        o.x.u = (rb | sb) && below;", "        o.x.u = below;", "unit",
     "U on an exact denormal result (the console raises U only when inexact)"),
    ("tiny-before", FP, "        o.x.u = (rb | sb) && below;", "        o.x.u = (rb | sb);", "unit",
     "tininess before rounding: U on a value that rounds up to the minimum normal"),
    # ---- the E / NaN priority of the scalar operations (v1_fp.sv)
    ("add-sqnan-e", FP, "    if (!dbl && (ua.nan || ub.nan)) return mk(k_qnan(dbl), 1'b0, 1'b0);\n", "", "unit",
     "single FADD: a quiet NaN no longer beats E"),
    ("add-hnan-e", FP, "    if (ua.hnan || ub.hnan)       return mk(k_qnan(dbl), 1'b0, 1'b0);\n", "", "unit",
     "FADD DR: a NaN with its fraction in the high word no longer beats E"),
    ("add-qnanlo", FP, "    if (ua.den || ub.den)         return mk_e();",
     "    if ((ua.den || ub.den) && !ua.nan && !ub.nan) return mk_e();", "unit",
     "FADD DR: a low-word-only quiet NaN beats E (the console: E)"),
    ("mul-lden-early", FP, "    if ((!dbl && (ua.den || ub.den)) || ua.hden || ub.hden) return mk_e();",
     "    if (ua.den || ub.den) return mk_e();", "unit",
     "FMUL DR: a low-word-only denormal is E before the infinities (the console: inf x it = inf)"),
    ("mac-e-first", FP, "    if (pinf && uc.inf && uc.s != sp)                 return",
     "    if (pinf && uc.inf && uc.s != sp && !ua.den && !ub.den) return", "unit",
     "FMAC: E before the (denormal x inf) - inf invalid case"),
    ("div-z-e", FP, "      else if (ub.zero) begin\n        if (ua.zero)                k = mk(k_qnan(dbl), 1'b1, 1'b0);",
     "      else if (ub.zero && !ua.den) begin\n        if (ua.zero)                k = mk(k_qnan(dbl), 1'b1, 1'b0);",
     "unit", "FDIV: denormal / 0 is E instead of Z"),
    ("div-lden-zero", FP, "        else if (ua.lden)           k = mk(k_zero(s, dbl), 1'b0, 1'b0);",
     "        else if (ua.lden)           k = mk_e();", "unit",
     "FDIV DR: low-word-only denormal / 0 is E (the console: the signed zero, no cause)"),
    ("sqrt-negden", FP, "      else if (ua.s)                k = mk(k_qnan(dbl), 1'b1, 1'b0);",
     "      else if (ua.s && !ua.den)     k = mk(k_qnan(dbl), 1'b1, 1'b0);", "unit",
     "FSQRT of a negative denormal is E instead of V"),
    ("cnvsd-no-e", FP, "    if (ua.den)  return mk_e();\n    if (ua.inf)  return mk(k_inf(ua.s, 1'b1)",
     "    if (ua.inf)  return mk(k_inf(ua.s, 1'b1)", "unit", "FCNVSD of a denormal converts instead of E"),
    ("fcmp-flush", FP, "    ua = unpack(a, dbl, dn0); ub = unpack(b, dbl, dn0);\n    ma = ua.zero",
     "    ua = unpack(a, dbl); ub = unpack(b, dbl);\n    ma = ua.zero", "unit",
     "FCMP with DN = 0 compares denormals as zeros"),
    ("ftrc-e", FP, "    o.x = '0; o.e = 1'b0;\n    if (!hi[31] && nan_p)",
     "    o.x = '0; o.e = !dbl && hi[30:23] == 8'h0 && hi[22:0] != 23'h0;\n    if (!hi[31] && nan_p)", "unit",
     "FTRC raises E on a denormal (as pm 10 lists; the console does not)"),
    # ---- the divider (v1_fdiv.sv)
    ("fdiv-round-dn", DIV, "o_dbl, o_rz, o_dn0);", "o_dbl, o_rz);", "unit",
     "the divider's round stage flushes a denormal quotient with DN = 0"),
    ("fdiv-spec-dn", DIV, "    sp = fdiv_spec(a, b, sqrt, dbl, dn0);", "    sp = fdiv_spec(a, b, sqrt, dbl);", "unit",
     "the divider's special cases ignore DN = 0 (a denormal / 0 is 0 / 0)"),
    # ---- the vector unit (fpu_normround.sv, fpu_vec_top.sv)
    ("nr-no-den", NR, "      den   = dn0 && epre < 1;", "      den   = 1'b0;", "unit",
     "FIPR / FTRV with DN = 0: an underflowing sum flushes to zero"),
    ("vec-hidden", VEC, "        A_OPA:   la_n[i] = MULW'({1'b0, fva[i][30:23] != 8'd0, fva[i][22:0]});",
     "        A_OPA:   la_n[i] = MULW'({2'b01, fva[i][22:0]});", "unit",
     "FIPR: a denormal FVm element gets the hidden bit"),
    ("vec-pvalid", VEC, "          nza = ea != 0 || (dn0 && fva[i][22:0] != 0);", "          nza = ea != 0;", "unit",
     "FIPR with DN = 0: a product with a denormal FVm element is discarded"),
    ("vec-e1", VEC, "          pE[i]     <= 9'(e1(ea)) + 9'(e1(eb));", "          pE[i]     <= 9'(ea) + 9'(eb);", "unit",
     "FIPR: a denormal's product exponent uses 0 instead of 1"),
    ("vec-fsrra-e", VEC, "      d_sp_e = fr_den && !fr_sign;", "      d_sp_e = 1'b0;", "unit",
     "FSRRA of a positive denormal with DN = 0: no E"),
    # ---- the FPU integration (v1_fpu.sv): the core runs
    ("fpu-fadd-dn", FPU, "r = fadd_s(n, m, rz, dn0);", "r = fadd_s(n, m, rz);", "core",
     "the FE pipe's FADD without DN: no E, flushed results"),
    ("fpu-fcmp-dn", FPU, "cm = fcmp_s(n, m, dn0);", "cm = fcmp_s(n, m);", "core", "FCMP in the core compares denormals as zeros"),
    ("fpu-dd-e", FPU, "    dd_e[s] = dd_r[s].e;", "    dd_e[s] = 1'b0;", "core", "no E for the double forms"),
    ("fpu-div-e", FPU, "F_FDIV_FRM_FRN, F_FSQRT_FRN: begin f1_ck = 1'b1; f1_vze = {dsp.x.v, dsp.x.z, dsp.e}; f1_cx",
     "F_FDIV_FRM_FRN, F_FSQRT_FRN: begin f1_ck = 1'b1; f1_vze = {dsp.x.v, dsp.x.z, 1'b0}; f1_cx", "core",
     "no E for FDIV / FSQRT single"),
    ("fpu-div-dn", FPU, ".dbl(dv_sdbl), .rz, .dn0,", ".dbl(dv_sdbl), .rz, .dn0(1'b0),", "core",
     "the core's divider without DN: DN = 1 specials and flushed quotients"),
    ("fpu-vec-dn", FPU, ".rz, .dn0, .fva(vs_fva)", ".rz, .dn0(1'b0), .fva(vs_fva)", "core",
     "the core's vector unit without DN: FIPR denormals as zeros, no FSRRA E"),
    ("fpu-vq-te", FPU, "(en_z && vq[i].tv[0]) || vq[i].te))", "(en_z && vq[i].tv[0])))", "core",
     "FSRRA's E never traps in the core"),
    ("fpu-vres-e", FPU, "{vres_e, vres_x}", "{1'b0, vres_x}", "core", "FSRRA's E trap leaves cause 0 (the console: E)"),
    ("fpu-e-cause", FPU, "tcw_c = f1_vze[0] ? 6'b100000 :", "tcw_c = f1_vze[0] ? 6'b110000 :", "core",
     "the E trap's cause carries V too (the console: E alone)"),
    # ---- the trap effects the core probe compares in full (SOC.md 4.3.8, I053): an O / U / I enable trap reports the
    # computed cause [HW fpdn: FSQRT(1.5) + EN.I 0x00001084] -- v1_fpu's trap pass before WI-CORE-F's item 1 wrote V / Z
    ("fpu-tp-oui", FPU, "tcw_c = fs.e ? 6'b100000 : {1'b0, fs.x};",
     "tcw_c = fs.e ? 6'b100000 : {1'b0, fs.x.v, fs.x.z, 3'b000};", "core",
     "a trap pass writes V / Z only (O / U / I of an enable trap lost)"),
    ("fpu-dvtp-oui", FPU, "dv_cw = 1'b1; dv_wr_cz = dv_cz[k]; dv_wr_x = qx; dv_wr_te = dv_te[k];",
     "dv_cw = 1'b1; dv_wr_cz = dv_cz[k]; dv_wr_x = qx; dv_wr_te = dv_te[k];\n"
     "        if (dv_dst[k] == '0) begin dv_wr_x.o = 1'b0; dv_wr_x.u = 1'b0; dv_wr_x.i = 1'b0; end", "core",
     "an FDIV / FSQRT trap pass (no destination) loses the recurrence's O / U / I (FSQRT(1.5) + EN.I: cause 0)"),
]


def sh(cmd, log):
    with open(log, "a") as f:
        return subprocess.run(cmd, cwd=ROOT, stdout=f, stderr=subprocess.STDOUT).returncode


def build(tag, probe, files, jobs):
    d = os.path.join(OUT, tag)
    os.makedirs(d, exist_ok=True)
    log = os.path.join(d, "build.log")
    over = ["%s=%s" % (VAR[k], v) for k, v in files.items()]
    if probe == "unit":
        rc = sh(["make", "-s", "v1-fpdn-build", "NJOBS=%d" % jobs, "V1_FPDN_MDIR=" + os.path.join(d, "uobj")] + over, log)
        return os.path.join(d, "uobj", "tb") if rc == 0 else None
    rc = sh(["make", "-s", "v1-fpdn-core-build", "NJOBS=%d" % jobs, "V1_FPDN_CMDIR=" + os.path.join(d, "cobj")] + over, log)
    return os.path.join(d, "cobj", "tb") if rc == 0 else None


def run_probe(exe, probe, d):
    args = [exe, "--hw", "--vec", "--rand", "20000"] if probe == "unit" else [exe, "--p2step", "64", "--fiprstep", "4096"]
    log = os.path.join(d, "run.log")
    with open(log, "w") as f:
        rc = subprocess.run(args, cwd=ROOT, stdout=f, stderr=subprocess.STDOUT).returncode
    return rc, log


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--jobs", type=int, default=4)
    ap.add_argument("--only", default="")
    ap.add_argument("--keep", action="store_true")
    a = ap.parse_args()
    only = set(x for x in a.only.split(",") if x)
    per = max(2, NCPU // a.jobs)
    ok = True
    # the control: the unmodified files, both probes
    for probe in ("unit", "core"):
        exe = build("control_" + probe, probe, {}, NCPU)
        if not exe:
            print("control %s: BUILD FAILED" % probe); return 1
        rc, log = run_probe(exe, probe, os.path.join(OUT, "control_" + probe))
        print("control %-5s %s" % (probe, "pass" if rc == 0 else "FAIL (%s)" % log))
        if rc != 0: return 1
    todo = [m for m in MUTANTS if not only or m[0] in only]

    def one(m):
        mid, path, orig, mut, probe, what = m
        src = open(os.path.join(ROOT, path)).read()
        n = src.count(orig)
        if n != 1:
            return mid, "REFUSED (pattern occurs %d times)" % n, what
        d = os.path.join(OUT, mid)
        os.makedirs(d, exist_ok=True)
        dst = os.path.join(d, os.path.basename(path))
        open(dst, "w").write(src.replace(orig, mut, 1))
        exe = build(mid, probe, {path: os.path.relpath(dst, ROOT)}, per)
        if not exe:
            return mid, "caught (build fails)", what
        rc, log = run_probe(exe, probe, d)
        res = "caught" if rc != 0 else "SURVIVED"
        if not a.keep and rc != 0:
            shutil.rmtree(d, ignore_errors=True)
        return mid, res, what

    with ThreadPoolExecutor(max_workers=a.jobs) as ex:
        results = list(ex.map(one, todo))
    caught = 0
    for mid, res, what in results:
        tag = "caught" if res == "caught" else "BUILD-ERR" if res.startswith("caught") else res.split()[0]
        print("%-16s %-9s %s" % (mid, tag, what if res.startswith(("caught", "SURVIVED")) else res))
        if res == "caught": caught += 1
        else: ok = False                                   # a mutant that does not build is not a test result
    print("v1-fpdn-mutants: %d / %d caught -> %s" % (caught, len(results), "PASS" if ok else "FAIL"))
    if not a.keep:
        for t in ("control_unit", "control_core"):
            shutil.rmtree(os.path.join(OUT, t), ignore_errors=True)
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
