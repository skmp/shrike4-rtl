// v1_fp.sv -- v1 FPU arithmetic (unit B): FADD / FSUB, FMUL, FMAC, FCMP, FLOAT, FTRC, FCNVSD, FCNVDS, single and
// double, as combinational functions for the core's FE pipe (F1 / F2 / FS are the core's business: it may call a
// function in one stage or split its inputs / outputs across stage registers).  FDIV / FSQRT are the iterative
// v1_fdiv module, which uses unpack() and round_pack() from here.
//
// One datapath for both precisions, as the SH7091 runs doubles through the single-precision pipe: operands unpack
// into a 53-bit significand (a single's 24 bits left-aligned), products and sums are formed exactly (106-bit product,
// 128-bit aligned adder), and ONE rounder (round_pack) rounds to 24 or 53 bits.  Semantics, all from pm.txt:
//
//  * Classes (Section 10 data_type_of): exponent 0 -> zero of that sign (DN = 1: a denormal input IS a zero, also
//    for FCMP, FTRC and the conversions); exponent all ones, fraction 0 -> infinity; fraction != 0 -> NaN, and the
//    NaN is SIGNALING when the fraction MSB is 1 (bit 22 / bit 51: the SH-4 convention, the opposite of IEEE 754-2008).
//  * NaN results (Section 6.2.2): every NaN produced is the SH-4 qNaN 0x7FBFFFFF / 0x7FF7FFFF_FFFFFFFF (input
//    payloads are never propagated).  V is raised for an sNaN input and for the invalid operations (inf - inf,
//    0 * inf, 0 / 0, inf / inf, sqrt(< 0)); a qNaN input alone raises nothing.
//  * Rounding (Section 6.4): RN = nearest-even, RZ = truncate.  Overflow: O and I, result +-inf (RN) or +-max
//    finite (RZ) with the sign of the unrounded value (6.5; I on overflow as 6.5 "Inexact: overflow, underflow or
//    rounding" says, where check_single_exception alone would not set it).
//  * Underflow with DN = 1 (check_single_exception / check_double_exception): the result is first rounded as IEEE
//    would with denormals (the host FPU in the manual's pseudo-code); if that is below the minimum normal and the
//    exact result is not zero, the result is a zero of the result's sign and U and I are raised.  So tininess is
//    "after rounding to the denormal grid": in RN an exact value in [2^-126 - 2^-150, 2^-126) rounds UP to the
//    minimum normal (I only, no U); RZ flushes everything below 2^-126.  An exact zero raises nothing.
//  * Zero signs are IEEE (RN / RZ): x + (-x) = +0, (+0) + (-0) = +0, (-0) + (-0) = -0; FMAC likewise on the exact
//    product.  (The manual's FMAC pseudo-code writes zero(n, s0^sm^sn) for 0*x + 0, contradicting its own special-case
//    table; the table is IEEE and is what this implements.)
//  * FMAC = one rounding of the exact a*b + c (ARCH.md: an assumption, the sweep measures time not FMAC values).
//    NaN priority per the FMAC pseudo-code: any sNaN -> V; a or b qNaN -> qNaN; 0 * inf -> V even when c is a qNaN;
//    c qNaN -> qNaN; (+-inf product) + (-+inf) -> V.
//
// Cause bits are the IEEE ones (fexc_t = V Z O U I); E is never produced (DN = 1).  With FPSCR.DN = 0 the core
// still calls these functions: v1 behaves as DN = 1 there (ARCH.md, documented limitation).
package v1_fp_pkg;
  typedef struct packed { logic v, z, o, u, i; } fexc_t;          // IEEE cause bits (E is not produced)
  typedef struct packed { logic [31:0] r; fexc_t x; } fs_res_t;   // single result + cause
  typedef struct packed { logic [63:0] r; fexc_t x; } fd_res_t;   // double result + cause (a single in r[31:0])

  // FCMP result.  fcmp_x(a, b) compares a with b (FCMP/GT FRm,FRn is "FRn > FRm": pass a = FRn, b = FRm).
  //   eq   : ordered and a == b              -> T of FCMP/EQ
  //   gt   : ordered and a > b               -> T of FCMP/GT
  //   v_eq : V cause of FCMP/EQ = either operand is an sNaN (fcmp_chk INVALID; a qNaN alone is UO: T = 0, no V)
  //   v_gt : V cause of FCMP/GT = either operand is any NaN (FCMP_GT calls fcmp_invalid() for INVALID and UO)
  // With a NaN operand eq = gt = 0 (fcmp_invalid() clears T when EN.V = 0, and v1 never traps).  +0 == -0, and
  // under DN = 1 every denormal compares equal to zero (and to every other denormal).
  typedef struct packed { logic eq, gt, v_eq, v_gt; } fcmp_t;

  localparam logic [31:0] QNAN_S = 32'h7FBF_FFFF;
  localparam logic [63:0] QNAN_D = 64'h7FF7_FFFF_FFFF_FFFF;

  // ---- unpacked operand ----------------------------------------------------------------------------------------
  // value = (-1)^s * m / 2^52 * 2^e for a normal number; m[52] is the hidden one; a single's 24-bit significand is
  // left-aligned ({1, f[22:0], 29'b0}) so that every later stage is precision-agnostic apart from the rounder.
  typedef struct packed {
    logic               s;
    logic signed [15:0] e;      // unbiased exponent
    logic [52:0]        m;      // significand 1.f
    logic               zero;   // +-0 and (DN = 1) +-denormal
    logic               inf;
    logic               nan;
    logic               snan;   // fraction MSB = 1
  } fu_t;

  function automatic fu_t unpack(input logic [63:0] x, input logic dbl);
    fu_t u;
    logic [10:0] ef;
    logic [51:0] f;
    if (dbl) begin ef = x[62:52];             f = x[51:0];           end
    else     begin ef = {3'b0, x[30:23]};     f = {x[22:0], 29'b0};  end
    u.s    = dbl ? x[63] : x[31];
    u.zero = ef == 11'd0;
    u.inf  = (dbl ? ef == 11'h7FF : ef == 11'h0FF) && f == '0;
    u.nan  = (dbl ? ef == 11'h7FF : ef == 11'h0FF) && f != '0;
    u.snan = u.nan && f[51];
    u.e    = $signed({5'b0, ef}) - (dbl ? 16'sd1023 : 16'sd127);
    u.m    = {1'b1, f};
    return u;
  endfunction

  // ---- constants in either format (single results in r[31:0], r[63:32] = 0) -------------------------------------
  function automatic logic [63:0] k_qnan(input logic dbl);
    return dbl ? QNAN_D : {32'h0, QNAN_S};
  endfunction
  function automatic logic [63:0] k_inf(input logic s, input logic dbl);
    return dbl ? {s, 11'h7FF, 52'h0} : {32'h0, s, 8'hFF, 23'h0};
  endfunction
  function automatic logic [63:0] k_zero(input logic s, input logic dbl);
    return dbl ? {s, 63'h0} : {32'h0, s, 31'h0};
  endfunction
  function automatic fd_res_t mk(input logic [63:0] r, input logic v, input logic z);
    fd_res_t o;
    o.r = r; o.x = '{v: v, z: z, o: 1'b0, u: 1'b0, i: 1'b0};
    return o;
  endfunction
  function automatic fs_res_t to_s(input fd_res_t d);
    fs_res_t o;
    o.r = d.r[31:0]; o.x = d.x;
    return o;
  endfunction

  // ---- the rounder ---------------------------------------------------------------------------------------------
  // Exact value = (-1)^s * (sig + st*) / 2^63 * 2^e with sig[63] = 1 (normalised) and st = OR of every bit below
  // sig[0].  Keeps 24 (single) or 53 (double) bits; round bit below them, sticky = the rest.  RN: +1 when round and
  // (sticky or lsb); RZ: truncate.  A carry out of the kept field (all ones + 1) bumps the exponent, the fraction
  // being 0 then.  Overflow: biased exponent after rounding >= 255 / 2047.  Tiny (see the header): biased exponent
  // before rounding < 1, EXCEPT exponent 0 with all 24 / 53 kept bits ones in RN: on the denormal grid (one bit
  // coarser) that value is >= min normal - half an ulp and rounds up to the minimum normal (ties go to its even
  // mantissa), which is what the host FPU of the manual's pseudo-code returns; it is inexact by construction.
  function automatic fd_res_t round_pack(input logic s, input logic signed [15:0] e, input logic [63:0] sig,
                                         input logic st, input logic dbl, input logic rz);
    fd_res_t            o;
    logic [52:0]        keep;
    logic               rb, sb, inc, carry, allones, up_to_min;
    logic [53:0]        sum;
    logic signed [15:0] eb0, eb;
    logic [51:0]        frac;
    if (dbl) begin
      keep = sig[63:11];           rb = sig[10]; sb = (|sig[9:0])  | st; eb0 = e + 16'sd1023; allones = &sig[63:11];
    end else begin
      keep = {29'h0, sig[63:40]};  rb = sig[39]; sb = (|sig[38:0]) | st; eb0 = e + 16'sd127;  allones = &sig[63:40];
    end
    inc       = !rz && rb && (sb || keep[0]);
    sum       = {1'b0, keep} + 54'(inc);
    carry     = dbl ? sum[53] : sum[24];
    eb        = eb0 + 16'(carry);
    frac      = dbl ? sum[51:0] : {29'h0, sum[22:0]};
    up_to_min = eb0 == 16'sd0 && !rz && allones;
    o.x = '0;
    if (eb0 < 16'sd1 && !up_to_min) begin                     // tiny and nonzero: flush, U + I
      o.r = k_zero(s, dbl);
      o.x.u = 1'b1; o.x.i = 1'b1;
    end else if (up_to_min) begin                              // rounds up to the minimum normal
      o.r = dbl ? {s, 11'd1, 52'h0} : {32'h0, s, 8'd1, 23'h0};
      o.x.i = 1'b1;
    end else if (eb >= (dbl ? 16'sd2047 : 16'sd255)) begin     // overflow: inf (RN) / max finite (RZ), O + I
      o.r = rz ? (dbl ? {s, 11'h7FE, {52{1'b1}}} : {32'h0, s, 8'hFE, 23'h7FFFFF}) : k_inf(s, dbl);
      o.x.o = 1'b1; o.x.i = 1'b1;
    end else begin
      o.r = dbl ? {s, eb[10:0], frac} : {32'h0, s, eb[7:0], frac[22:0]};
      o.x.i = rb | sb;
    end
    return o;
  endfunction

  // ---- normaliser: shift left until bit 127 is set, in log stages (the leading-zero count falls out) --------------
  function automatic logic [127:0] norm128(input logic [127:0] v, output logic [7:0] lz);
    logic [127:0] t;
    t = v; lz = 8'd0;
    if (t[127:64] == '0) begin t = t << 64; lz = lz + 8'd64; end
    if (t[127:96] == '0) begin t = t << 32; lz = lz + 8'd32; end
    if (t[127:112] == '0) begin t = t << 16; lz = lz + 8'd16; end
    if (t[127:120] == '0) begin t = t << 8;  lz = lz + 8'd8;  end
    if (t[127:124] == '0) begin t = t << 4;  lz = lz + 8'd4;  end
    if (t[127:126] == '0) begin t = t << 2;  lz = lz + 8'd2;  end
    if (t[127] == 1'b0)   begin t = t << 1;  lz = lz + 8'd1;  end
    return t;
  endfunction

  // ---- the adder: exact aligned sum of two nonzero finite operands ------------------------------------------------
  // Operands are (sign, exponent, 128-bit significand with the leading one at bit 126 = 1.0, bit 127 free for the
  // carry, and bits [20:0] zero -- every caller's significand is at most 106 bits wide).  The operand with the
  // smaller exponent is shifted right by the exponent difference; the bits shifted out are OR-ed into bit 0 (a
  // sticky far below every rounding position: the larger operand's bits [20:0] are zero, so the forced bit 0 moves
  // the sum by less than one unit of bit 0 and changes only bit 0 itself -- round bit and sticky are exact, also
  // for effective subtraction).  Equal exponents may give a negative difference: two's complement negate, flip the
  // sign.  Result: normalised 64-bit sig + sticky for round_pack, or zero (exact cancellation).
  typedef struct packed { logic s; logic signed [15:0] e; logic [63:0] sig; logic st; logic zero; } sum_t;

  function automatic sum_t add_core(input logic sx, input logic signed [15:0] ex, input logic [127:0] mx,
                                    input logic sy, input logic signed [15:0] ey, input logic [127:0] my);
    sum_t               o;
    logic               swap, s1, s2, neg;
    logic signed [15:0] e1, e2, d;
    logic [127:0]       m1, m2, a2, lost, mag, nrm;
    logic [128:0]       diff;
    logic [7:0]         lz;
    swap = ey > ex;
    s1 = swap ? sy : sx;  e1 = swap ? ey : ex;  m1 = swap ? my : mx;
    s2 = swap ? sx : sy;  e2 = swap ? ex : ey;  m2 = swap ? mx : my;
    d  = e1 - e2;
    if (d >= 16'sd128) begin a2 = 128'h0; lost = m2; end
    else begin a2 = m2 >> d[6:0]; lost = m2 & ~({128{1'b1}} << d[6:0]); end
    a2[0] = a2[0] | (lost != '0);
    neg = 1'b0;
    if (s1 ^ s2) begin
      diff = {1'b0, m1} - {1'b0, a2};
      neg  = diff[128];
      mag  = neg ? 128'(-diff) : diff[127:0];
    end else begin
      mag  = m1 + a2;                                          // < 2^128: both < 2^127
    end
    nrm    = norm128(mag, lz);
    o.s    = s1 ^ neg;
    o.e    = e1 + 16'sd1 - $signed({8'h0, lz});                // m1's leading one (bit 126) is 2^e1
    o.sig  = nrm[127:64];
    o.st   = nrm[63:0] != '0;
    o.zero = mag == '0;
    return o;
  endfunction

  // significand of an unpacked operand at the adder's position (leading one at bit 126)
  function automatic logic [127:0] at126(input logic [52:0] m);
    return {1'b0, m, 74'h0};
  endfunction

  // ---- FADD (FSUB = FADD with b's sign flipped, in the core) -----------------------------------------------------
  // Section 10 FADD special cases: sNaN -> V; qNaN -> qNaN; inf + -inf -> V; inf + x = inf; zero + x = x (exact,
  // register_copy); zero + zero per IEEE; otherwise the exact sum rounded once (x + (-x) = +0).
  function automatic fd_res_t fadd_x(input logic [63:0] a, input logic [63:0] b, input logic dbl, input logic rz);
    fu_t  ua, ub;
    sum_t sm;
    ua = unpack(a, dbl); ub = unpack(b, dbl);
    if (ua.snan || ub.snan)       return mk(k_qnan(dbl), 1'b1, 1'b0);
    if (ua.nan || ub.nan)         return mk(k_qnan(dbl), 1'b0, 1'b0);
    if (ua.inf && ub.inf)         return (ua.s != ub.s) ? mk(k_qnan(dbl), 1'b1, 1'b0) : mk(k_inf(ua.s, dbl), 1'b0, 1'b0);
    if (ua.inf)                   return mk(k_inf(ua.s, dbl), 1'b0, 1'b0);
    if (ub.inf)                   return mk(k_inf(ub.s, dbl), 1'b0, 1'b0);
    if (ua.zero && ub.zero)       return mk(k_zero(ua.s & ub.s, dbl), 1'b0, 1'b0);
    if (ua.zero)                  return mk(dbl ? b : {32'h0, b[31:0]}, 1'b0, 1'b0);
    if (ub.zero)                  return mk(dbl ? a : {32'h0, a[31:0]}, 1'b0, 1'b0);
    sm = add_core(ua.s, ua.e, at126(ua.m), ub.s, ub.e, at126(ub.m));
    if (sm.zero)                  return mk(k_zero(1'b0, dbl), 1'b0, 1'b0);
    return round_pack(sm.s, sm.e, sm.sig, sm.st, dbl, rz);
  endfunction

  // ---- FMUL ------------------------------------------------------------------------------------------------------
  // Section 10 FMUL special cases: sNaN -> V; qNaN -> qNaN; 0 * inf -> V; inf * x = inf, 0 * x = 0 (sign = xor).
  // The single product is 24 x 24 = 48 bits, the double 53 x 53 = 106 bits (the same array, twice as many passes
  // on the silicon); in [1, 4): one normalising shift.
  function automatic fd_res_t fmul_x(input logic [63:0] a, input logic [63:0] b, input logic dbl, input logic rz);
    fu_t                ua, ub;
    logic               s;
    logic [105:0]       p;
    logic [63:0]        sig;
    logic               st;
    logic signed [15:0] e;
    ua = unpack(a, dbl); ub = unpack(b, dbl);
    s  = ua.s ^ ub.s;
    if (ua.snan || ub.snan)                           return mk(k_qnan(dbl), 1'b1, 1'b0);
    if (ua.nan || ub.nan)                             return mk(k_qnan(dbl), 1'b0, 1'b0);
    if ((ua.inf && ub.zero) || (ua.zero && ub.inf))   return mk(k_qnan(dbl), 1'b1, 1'b0);
    if (ua.inf || ub.inf)                             return mk(k_inf(s, dbl), 1'b0, 1'b0);
    if (ua.zero || ub.zero)                           return mk(k_zero(s, dbl), 1'b0, 1'b0);
    if (dbl) p = ua.m * ub.m;
    else     p = {{24'h0, ua.m[52:29]} * {24'h0, ub.m[52:29]}, 58'h0};
    e = ua.e + ub.e;
    if (p[105]) begin sig = p[105:42]; st = p[41:0] != '0; e = e + 16'sd1; end
    else        begin sig = p[104:41]; st = p[40:0] != '0;                 end
    return round_pack(s, e, sig, st, dbl, rz);
  endfunction

  // ---- FMAC (single only): a * b + c, one rounding ---------------------------------------------------------------
  // The exact 48-bit product enters the adder as an operand (leading one at 126, bits [78:0] zero), the addend c
  // as the other; the adder's sticky alignment keeps the single rounding exact at any exponent distance.
  function automatic fs_res_t fmac_s(input logic [31:0] a, input logic [31:0] b, input logic [31:0] c, input logic rz);
    fu_t                ua, ub, uc;
    logic               sp;
    logic [47:0]        p;
    logic [127:0]       mp;
    logic signed [15:0] ep;
    sum_t               sm;
    ua = unpack({32'h0, a}, 1'b0); ub = unpack({32'h0, b}, 1'b0); uc = unpack({32'h0, c}, 1'b0);
    sp = ua.s ^ ub.s;
    if (ua.snan || ub.snan || uc.snan)                return to_s(mk(k_qnan(1'b0), 1'b1, 1'b0));
    if (ua.nan || ub.nan)                             return to_s(mk(k_qnan(1'b0), 1'b0, 1'b0));
    if ((ua.inf && ub.zero) || (ua.zero && ub.inf))   return to_s(mk(k_qnan(1'b0), 1'b1, 1'b0));
    if (uc.nan)                                       return to_s(mk(k_qnan(1'b0), 1'b0, 1'b0));
    if (ua.inf || ub.inf)                             // inf product: inf - inf is invalid
      return (uc.inf && uc.s != sp) ? to_s(mk(k_qnan(1'b0), 1'b1, 1'b0)) : to_s(mk(k_inf(sp, 1'b0), 1'b0, 1'b0));
    if (uc.inf)                                       return to_s(mk(k_inf(uc.s, 1'b0), 1'b0, 1'b0));
    if (ua.zero || ub.zero)                           // exact zero product: c, or a zero with the IEEE sum sign
      return uc.zero ? to_s(mk(k_zero(sp & uc.s, 1'b0), 1'b0, 1'b0)) : to_s(mk({32'h0, c}, 1'b0, 1'b0));
    p  = {24'h0, ua.m[52:29]} * {24'h0, ub.m[52:29]};
    ep = ua.e + ub.e;
    if (p[47]) begin mp = {1'b0, p, 79'h0};       ep = ep + 16'sd1; end
    else       begin mp = {1'b0, p[46:0], 80'h0};                   end
    if (uc.zero) begin                                // product alone (c = +-0 cannot change a nonzero value)
      sm.s = sp; sm.e = ep; sm.sig = mp[126:63]; sm.st = mp[62:0] != '0; sm.zero = 1'b0;
    end else begin
      sm = add_core(sp, ep, mp, uc.s, uc.e, at126(uc.m));
      if (sm.zero)                                    return to_s(mk(k_zero(1'b0, 1'b0), 1'b0, 1'b0));
    end
    return to_s(round_pack(sm.s, sm.e, sm.sig, sm.st, 1'b0, rz));
  endfunction

  // ---- FCMP ------------------------------------------------------------------------------------------------------
  // Ordered compare on a sign-magnitude key with zeros (and DN = 1 denormals) mapped to 0: key = s ? -mag : mag,
  // mag = the bits below the sign (infinities are the largest magnitudes).  See fcmp_t for the NaN rules.
  function automatic fcmp_t fcmp_x(input logic [63:0] a, input logic [63:0] b, input logic dbl);
    fcmp_t              o;
    fu_t                ua, ub;
    logic signed [64:0] ka, kb;
    logic [62:0]        ma, mb;
    ua = unpack(a, dbl); ub = unpack(b, dbl);
    ma = ua.zero ? 63'h0 : (dbl ? a[62:0] : {32'h0, a[30:0]});
    mb = ub.zero ? 63'h0 : (dbl ? b[62:0] : {32'h0, b[30:0]});
    ka = ua.s ? -$signed({2'b0, ma}) : $signed({2'b0, ma});
    kb = ub.s ? -$signed({2'b0, mb}) : $signed({2'b0, mb});
    o.v_eq = ua.snan || ub.snan;
    o.v_gt = ua.nan || ub.nan;
    o.eq   = !o.v_gt && ka == kb;
    o.gt   = !o.v_gt && ka > kb;
    return o;
  endfunction

  // ---- FLOAT: int32 -> float.  Single rounds (RN / RZ, I when inexact); a double is always exact. ----------------
  function automatic fd_res_t float_x(input logic [31:0] i, input logic dbl, input logic rz);
    logic [31:0]  mag;
    logic [127:0] nrm;
    logic [7:0]   lz;
    if (i == 32'h0) return mk(k_zero(1'b0, dbl), 1'b0, 1'b0);
    mag = i[31] ? 32'(-i) : i;                                 // -2^31 -> 0x80000000 as unsigned
    nrm = norm128({mag, 96'h0}, lz);
    return round_pack(i[31], 16'sd31 - $signed({8'h0, lz}), nrm[127:64], 1'b0, dbl, rz);
  endfunction

  // ---- FTRC: float -> int32, truncation, SH-4 saturation (Section 10 ftrc_*_type_of / ftrc_invalid) -------------
  // Single: sign 0 and bits > 0x7F800000 (NaN) -> V, 0x80000000; sign 0 and bits > 0x4EFFFFFF (>= 2^31, +inf)
  // -> V, 0x7FFFFFFF; sign 1 and bits > 0xCF000000 (below -2^31, -inf, negative NaNs) -> V, 0x80000000; otherwise
  // the truncated value (denormals and |x| < 1 give 0; -2^31 itself is exact, no V).  No I on a discarded fraction
  // (FTRC's possible exceptions are E and V only).
  // Double: the same with the UPPER word only, as the manual's range constants are "higher of" (P 0x41DFFFFF,
  // N 0xC1E00000); NaN = upper > 0x7FF00000 or (== and lower != 0).  That leaves negative values with upper word
  // exactly 0xC1E00000, i.e. -(2^31 + 2^11) < x <= -2^31, in range: their truncation below -2^31 (x <= -2^31 - 1)
  // does not fit an int32 and the manual's (long) cast is undefined there -- v1 saturates it to 0x80000000
  // WITHOUT V (an assumption; no measurement exists).
  function automatic fs_res_t ftrc_x(input logic [63:0] a, input logic dbl);
    fs_res_t            o;
    fu_t                ua;
    logic [31:0]        hi;
    logic               nan_p, big_p, big_n;
    logic [52:0]        mag;
    logic signed [15:0] sh;
    ua = unpack(a, dbl);
    hi = dbl ? a[63:32] : a[31:0];
    if (dbl) begin
      nan_p = hi > 32'h7FF0_0000 || (hi == 32'h7FF0_0000 && a[31:0] != 32'h0);
      big_p = hi > 32'h41DF_FFFF;
      big_n = hi > 32'hC1E0_0000;
    end else begin
      nan_p = hi > 32'h7F80_0000;
      big_p = hi > 32'h4EFF_FFFF;
      big_n = hi > 32'hCF00_0000;
    end
    o.x = '0;
    if (!hi[31] && nan_p)                     begin o.r = 32'h8000_0000; o.x.v = 1'b1; end
    else if (!hi[31] && big_p)                begin o.r = 32'h7FFF_FFFF; o.x.v = 1'b1; end
    else if (hi[31] && big_n)                 begin o.r = 32'h8000_0000; o.x.v = 1'b1; end
    else if (ua.zero || ua.e < 16'sd0)        begin o.r = 32'h0; end
    else begin                                // 0 <= e <= 31: integer part = m >> (52 - e), at most 32 bits
      sh  = 16'sd52 - ua.e;
      mag = ua.m >> sh[5:0];
      if (ua.s) o.r = (mag > 53'h8000_0000) ? 32'h8000_0000 : 32'(-mag[31:0]);
      else      o.r = mag[31:0];
    end
    return o;
  endfunction

  // ---- FCNVSD (exact) / FCNVDS (rounds, can overflow / underflow) -----------------------------------------------
  // Section 10 fcnvsd / fcnvds: sNaN -> V + qNaN of the destination format; qNaN -> qNaN; +-inf -> +-inf; zero and
  // (DN = 1) denormal -> zero of that sign.
  function automatic fd_res_t fcnvsd(input logic [31:0] a);
    fu_t               ua;
    logic signed [15:0] eb;
    ua = unpack({32'h0, a}, 1'b0);
    eb = ua.e + 16'sd1023;
    if (ua.snan) return mk(QNAN_D, 1'b1, 1'b0);
    if (ua.nan)  return mk(QNAN_D, 1'b0, 1'b0);
    if (ua.inf)  return mk(k_inf(ua.s, 1'b1), 1'b0, 1'b0);
    if (ua.zero) return mk(k_zero(ua.s, 1'b1), 1'b0, 1'b0);
    return mk({ua.s, eb[10:0], ua.m[51:0]}, 1'b0, 1'b0);
  endfunction

  function automatic fs_res_t fcnvds(input logic [63:0] a, input logic rz);
    fu_t ua;
    ua = unpack(a, 1'b1);
    if (ua.snan) return to_s(mk(k_qnan(1'b0), 1'b1, 1'b0));
    if (ua.nan)  return to_s(mk(k_qnan(1'b0), 1'b0, 1'b0));
    if (ua.inf)  return to_s(mk(k_inf(ua.s, 1'b0), 1'b0, 1'b0));
    if (ua.zero) return to_s(mk(k_zero(ua.s, 1'b0), 1'b0, 1'b0));
    return to_s(round_pack(ua.s, ua.e, {ua.m, 11'h0}, 1'b0, 1'b0, rz));
  endfunction

  // ---- the ARCH.md interface -------------------------------------------------------------------------------------
  function automatic fs_res_t fadd_s(input logic [31:0] a, input logic [31:0] b, input logic rz);   // a + b
    return to_s(fadd_x({32'h0, a}, {32'h0, b}, 1'b0, rz));
  endfunction
  function automatic fs_res_t fmul_s(input logic [31:0] a, input logic [31:0] b, input logic rz);
    return to_s(fmul_x({32'h0, a}, {32'h0, b}, 1'b0, rz));
  endfunction
  function automatic fd_res_t fadd_d(input logic [63:0] a, input logic [63:0] b, input logic rz);
    return fadd_x(a, b, 1'b1, rz);
  endfunction
  function automatic fd_res_t fmul_d(input logic [63:0] a, input logic [63:0] b, input logic rz);
    return fmul_x(a, b, 1'b1, rz);
  endfunction
  function automatic fcmp_t fcmp_s(input logic [31:0] a, input logic [31:0] b);   // a vs b (a = FRn, b = FRm)
    return fcmp_x({32'h0, a}, {32'h0, b}, 1'b0);
  endfunction
  function automatic fcmp_t fcmp_d(input logic [63:0] a, input logic [63:0] b);
    return fcmp_x(a, b, 1'b1);
  endfunction
  function automatic fs_res_t float_s(input logic [31:0] i, input logic rz);       // int32 -> single
    return to_s(float_x(i, 1'b0, rz));
  endfunction
  function automatic fd_res_t float_d(input logic [31:0] i);                       // exact
    return float_x(i, 1'b1, 1'b0);
  endfunction
  function automatic fs_res_t ftrc_s(input logic [31:0] a);                        // -> int32 in r
    return ftrc_x({32'h0, a}, 1'b0);
  endfunction
  function automatic fs_res_t ftrc_d(input logic [63:0] a);
    return ftrc_x(a, 1'b1);
  endfunction
endpackage
