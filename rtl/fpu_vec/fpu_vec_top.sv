// fpu_vec_top.sv -- SH7091 FIPR / FSCA / FSRRA on one datapath: four Booth multipliers with input latches and
// feedback selectors, four aligners, a four-input adder and one normalise/round circuit (US 6,038,582 FIG. 4, 29,
// 34).  The three instructions are three microprograms over the same hardware:
//
//            cycle:   0 (RR)   1        2        3        4        5        6        7
//   FIPR              latch    F1 mul   F2 add   F3 nrm   result
//   FSCA              latch    step1    step2    step3    step4    F3 sin   F3 cos   (sin +6, cos +7, pitch 4)
//                                                         F2 sin   F2 cos   sin      cos
//   FSRRA             latch    step1    step2    step3    F2 add   F3 nrm   result   (+6, pitch 3)
//
// Port roles: stored constants go to port B (Booth-recoded), dx to port A (multiplicand) where it meets one (FIG. 29).
// FSCA even-chain and FSRRA feedback use port A; their next product recodes dx.
// Both are bit-exact. Every feedback shift is three bits. Every term of
// model/fsca/sh4_fsca.h / model/fsrra/fsrra_ref.h maps onto one (multiplier, step) slot below -- see README.md for the table.
//
// FSCA microprogram (V = C0 + s*T1 - T2 - s*T3 on the 2^-26 grid, s = -1 for nodes 17..31, negated for 32..63):
//   step 1  FM0: dx x dx -> 64 t^2       FM2: dx x KC -> KC dx          FM3: dx x C1F(sin) -> T1s
//   step 2  FM0: dx x (M0>>3 = 8 t^2)   FM2: dx x (M2>>3 = I1) -> J    (FM3 holds its latches)
//   step 3  FM0: (M0>>3 = A3) x C3s<<5  FM2: C1F(cos) x (M2>>3 = J)          ADD: C0s + s T1s - T2s - s T3s -> sine
//   step 4  FM0: (A3 held) x C3c<<5     FM2: C1F(sin) x (J held)   FM3: (dx held) x C1F(cos) -> T1c
//                                                                            ADD: C0c + s'T1c - T2c - s'T3c -> cosine
// FSRRA microprogram (W = C0 - T1 + T2 - T3, dx = t << 4):
//   step 1  FM0: dx x dx -> sq            FM3: dx x REC1 -> T1
//   step 2  FM2: (M0>>3 = sq) x REC2<<9 -> T2      FM0: (M0>>3 = sq, the SAME latch value) x REC3<<8 -> I'
//           REC2 / REC3 are FIXED-POINT fields (14 / 9 bits, no exponent); bit-exact on all of [1,4), both modes
//           (model/fsrra/tools/rr14_exact.cpp)
//   step 3  FM0: (M0>>3 = I) x dx(recoded) -> T3   ADD: C0 - T1 + T2 - T3
// Adder slots: slot i always takes product register M[i], except that slot 1 takes C0 for FSCA / FSRRA -- the one
// selector in front of the aligners.  The linear term therefore always sits on FM3 and FM1 is FIPR-only.
//
// Kill (rtl/v1/SOC.md 4.3.1, WI-CORE-F): every operation carries its instruction's ISN (`isn` at start) through the
// multiplier steps, F2 and F3; kill_v with ISN >= kill_isn (kill_incl) or > kill_isn drops it at whatever stage it is
// (the multiplier steps stop, its F2 / F3 slot empties, no res_valid).  The result register's own cycle is the host's
// to gate.  The host asserts the kill only in cycles with hold = 0 (a take happens in an unheld cycle).
// `st_x` is the cause of the operand special case of the operation presented with `start` (combinational, the start
// cycle): the host's operand trap check (FIPR V, FSRRA V / Z) is decided from it at issue.
//
// FPSCR.DN = 0 (`dn0`, latched with the operation; WI-FPDN, [HW] model/fipr/hw_specials, model/fsrra/hw_specials):
//   * FIPR / FTRV compute with a denormal operand as it is: no hidden bit, exponent 1, the `ea + eb <= 75` discard
//     rule unchanged, never an FPU error; the sum is rounded once at the denormal position (fpu_normround) and a zero
//     or denormal result raises U as any exponent-field-0 result does (model/fipr/sh4_fipr.h, 0 of 1M x RN / RZ);
//   * FSRRA of a positive denormal is the FPU error E (`st_e` at the start, `res_e` with its result word: the host
//     traps, the destination is not written, the cause is E alone), of a negative denormal V and the qNaN
//     (model/fsrra: sh4_fsrra_ex);
//   * FSCA has no floating-point operand.
// With dn0 = 0 (FPSCR.DN = 1) a denormal operand is a zero on every path, as before.
module fpu_vec_top
  import fpu_vec_pkg::*;
(
  input  logic        clk,
  input  logic        rst,
  input  logic        start,
  input  logic        hold,        // the host pipeline is frozen: every register holds (start is ignored)
  input  op_e         op,
  input  logic        rz,          // FPSCR.RM == RZ
  input  logic        dn0,         // FPSCR.DN == 0 (denormal operands as numbers, gradual underflow, FSRRA's E)
  input  logic [31:0] fva [4],     // FIPR FVm (port A)
  input  logic [31:0] fvb [4],     // FIPR FVn (port B, recoded)
  /* verilator lint_off UNUSEDSIGNAL */          // fpul[31:16]: FSCA takes the low 16 bits of the angle
  input  logic [31:0] fpul,        // FSCA angle (low 16 bits)
  /* verilator lint_on UNUSEDSIGNAL */
  input  logic [31:0] frn,         // FSRRA operand
  input  logic [7:0]  isn,         // ISN of the instruction a start belongs to
  input  logic        kill_v,      // kill ISN >= kill_isn (kill_incl) or > kill_isn
  input  logic [7:0]  kill_isn,
  input  logic        kill_incl,
  output logic [4:0]  st_x,        // operand special case of the start operands {V, Z, O, U, I} (0: none)
  output logic        st_e,        // ... the FPU error E (DN = 0: FSRRA of a positive denormal)
  output logic        busy,
  output logic        res_valid,
  output logic        res_second,  // FSCA: 0 = sine (FR[n]), 1 = cosine (FR[n+1])
  output logic [31:0] res,
  output logic [4:0]  res_x,       // FPSCR cause of this result word {V, Z, O, U, I} (model/*/sh4_*.h *_ex)
  output logic        res_e        // ... and its E (the word of an FSRRA whose operand was a DN = 0 denormal)
);
  `include "fpu_vec_tables.svh"

  // ================================================================== decode helpers
  // FSCA table addressing.  node = round(angle / 1024) (6 bits); r = node[3:0] addresses both 16-word ROMs directly,
  // q = node[5:4] is the quadrant.  ROM A[r] = Word(r), ROM B[r] = Word(16-r), Word(m) = the fields built from
  // sin(m*pi/32).  In an odd quadrant the roles of the two words swap and the odd terms change sign; the second half
  // turn negates the adder output.  The cosine is the sine one quadrant on: q + 1.  No comparators, no 32-m subtractor.
  /* verilator lint_off UNUSEDSIGNAL */          // q[1] is the sign's, not the swap's
  function automatic logic pass_swap(input logic [1:0] q, input logic cosine); return q[0] ^ cosine; endfunction
  /* verilator lint_on UNUSEDSIGNAL */
  function automatic logic pass_neg (input logic [1:0] q, input logic cosine); return cosine ? (q[1] ^ q[0]) : q[1]; endfunction
  function automatic logic [25:0] rom_c0(input logic sw, input logic [3:0] r); return sw ? FSCA_B_C0[r] : FSCA_A_C0[r]; endfunction
  function automatic logic [22:0] rom_s (input logic sw, input logic [3:0] r); return sw ? FSCA_B_S[r]  : FSCA_A_S[r];  endfunction
  function automatic logic [1:0]  rom_k (input logic sw, input logic [3:0] r); return sw ? FSCA_B_K[r]  : FSCA_A_K[r];  endfunction
  function automatic logic [22:0] rom_l (input logic sw, input logic [3:0] r); return sw ? FSCA_A_L[r]  : FSCA_B_L[r];  endfunction
  function automatic logic [1:0]  rom_e (input logic sw, input logic [3:0] r); return sw ? FSCA_A_E[r]  : FSCA_B_E[r];  endfunction
  function automatic logic [11:0] rom_q (input logic sw, input logic [3:0] r); return sw ? FSCA_A_Q[r]  : FSCA_B_Q[r];  endfunction

  // the coefficient ROM port for port B
  function automatic logic signed [RECW-1:0] const_of(input op_e o, input logic [1:0] sel, input logic sw, input logic [3:0] r, input logic [5:0] er);
    const_of = '0;
    if (o == OP_FSCA) begin
      unique case (sel)
        2'd0:    const_of = RECW'(FSCA_KC);
        2'd1:    const_of = RECW'(rom_l(sw, r));
        2'd3:    const_of = RECW'(rom_l(~sw, r)); // complementary linear field for T2
        default: const_of = RECW'(rom_q(sw, r)) <<< 5;
      endcase
    end else begin
      unique case (sel)
        2'd0:    const_of = RECW'(FSRRA_REC1[er]);
        2'd1:    const_of = RECW'(FSRRA_REC2[er]) <<< 9;    // 14-bit fixed-point square field
        default: const_of = RECW'(FSRRA_REC3[er]) <<< 8;    //  9-bit fixed-point cubic field
      endcase
    end
  endfunction

  // ================================================================== the microprograms
  // Sources of the multiplier latches for step `s`, evaluated in the cycle before (products of step s-1 available).
  function automatic mul_ctl_t mctl(input op_e o, input logic [2:0] s, input int i);
    mul_ctl_t c; c = '{a_sel: A_HOLD, b_sel: B_HOLD, fb_src: 2'd0, fb_sh: 3'd0, const_sel: 2'd0, tab_cos: 1'b0};
    unique case (o)
      OP_FIPR: begin c.a_sel = A_OPA; c.b_sel = B_OPB; end
      OP_FSCA: begin
        unique case (s)
          3'd1: begin c.a_sel = A_DX;
                      unique case (i) 0: c.b_sel = B_DX;
                                      1: c.a_sel = A_HOLD;                                   // FM1: FIPR only
                                      2: begin c.b_sel = B_CONST; c.const_sel = 2'd0; end
                                      default: begin c.b_sel = B_CONST; c.const_sel = 2'd1; end   // T1s, held to step 3
                      endcase end
          3'd2: begin if (i == 0) begin c.b_sel = B_FB; c.fb_src = 2'd0; c.fb_sh = 3'd3; end
                      if (i == 2) begin c.a_sel = A_FB; c.b_sel = B_DX; c.fb_src = 2'd2; c.fb_sh = 3'd3; end end
          3'd3: begin if (i == 0) begin c.a_sel = A_FB; c.fb_src = 2'd0; c.fb_sh = 3'd3; c.b_sel = B_CONST; c.const_sel = 2'd2; end
                      if (i == 2) begin c.a_sel = A_FB; c.b_sel = B_CONST; c.const_sel = 2'd3; c.fb_src = 2'd2; c.fb_sh = 3'd3; end end
          default: begin if (i == 0) begin c.b_sel = B_CONST; c.const_sel = 2'd2; c.tab_cos = 1'b1; end
                         if (i == 2) begin c.b_sel = B_CONST; c.const_sel = 2'd3; c.tab_cos = 1'b1; end
                         if (i == 3) begin c.b_sel = B_CONST; c.const_sel = 2'd1; c.tab_cos = 1'b1; end end   // T1c, dx held
        endcase end
      default: begin   // FSRRA
        unique case (s)
          3'd1: begin if (i == 0) begin c.a_sel = A_DX; c.b_sel = B_DX; end
                      if (i == 3) begin c.a_sel = A_DX; c.b_sel = B_CONST; c.const_sel = 2'd0; end end
          3'd2: begin if (i == 2) begin c.a_sel = A_FB; c.fb_src = 2'd0; c.fb_sh = 3'd3; c.b_sel = B_CONST; c.const_sel = 2'd1; end
                      if (i == 0) begin c.a_sel = A_FB; c.fb_src = 2'd0; c.fb_sh = 3'd3; c.b_sel = B_CONST; c.const_sel = 2'd2; end end
          default: begin if (i == 0) begin c.a_sel = A_FB; c.fb_src = 2'd0; c.fb_sh = 3'd3; c.b_sel = B_DX; end end
        endcase end
    endcase
    return c;
  endfunction
  function automatic logic add_go(input op_e o, input logic [2:0] s);
    unique case (o)
      OP_FIPR: add_go = (s == 3'd1);
      OP_FSCA: add_go = (s == 3'd3) || (s == 3'd4);
      default: add_go = (s == 3'd3);
    endcase
  endfunction
  function automatic logic [2:0] last_step(input op_e o);
    unique case (o) OP_FIPR: last_step = 3'd1; OP_FSCA: last_step = 3'd4; default: last_step = 3'd3; endcase
  endfunction

  // ================================================================== instruction state (loaded at cycle 0)
  op_e         op_r;
  logic        rz_r, dn0_r;
  logic [2:0]  step;                       // 0 idle, 1..4 = F1 steps
  logic signed [23:0] dx_r;                // sign expander output
  logic [3:0]  rom_r;                      // FSCA: r = node[3:0], the address of both ROMs
  logic [1:0]  quad;                       // FSCA: q = node[5:4]
  logic [5:0]  ent_r;                      // FSRRA: parity*17 + node
  logic [8:0]  emax_r;                     // FSRRA: 254 - k
  logic        sp_v;   logic [31:0] sp_r;  // pre-decided special result (NaN / inf / zero)
  logic [4:0]  sp_x;                       // and its cause
  logic        sp_e;                       // and its E
  logic [8:0]  pE [4]; logic pvalid [4], psign [4];   // FIPR per-product exponent sum, validity, sign
  logic [8:0]  fipr_emax;
  logic [7:0]  isn_r, isn_f2, isn_f3;      // the ISN of the operation in the steps / F2 / F3
  function automatic logic [7:0] e1(input logic [7:0] xe);   // the exponent a product uses (a denormal's is 1)
    return xe == 8'd0 ? 8'd1 : xe;
  endfunction
  function automatic logic kl(input logic [7:0] i);   // killed this cycle: (i - K) mod 256 < 128, and != K unless incl
    logic [7:0] d;
    d = i - kill_isn;
    return kill_v && !d[7] && (kill_incl || d != 8'd0);
  endfunction

  // ================================================================== cycle-0 decode (combinational on the inputs)
  logic [5:0]  node6;
  logic signed [9:0]  t10;
  logic signed [18:0] t19;
  logic signed [23:0] d_dx;
  logic [7:0]  fr_exp; logic [22:0] fr_man; logic fr_sign;
  logic signed [8:0] fr_e, fr_k; logic fr_par;
  logic [5:0]  d_ent_r;
  logic        d_sp_v; logic [31:0] d_sp_r; logic [4:0] d_sp_x; logic d_sp_e;
  logic        fr_den;                     // FSRRA: a denormal operand with DN = 0
  // infinite products of each sign: 0..4 (3 bits -- four infinities of one sign wrapped a 2-bit count to 0, a +0 / U
  // result instead of the infinity; WI-CORE-F's random FPU programs against model/fipr/sh4_fipr.h)
  logic [2:0]  n_pinf, n_ninf; logic n_nan, n_any, n_inv;
  always_comb begin
    // FSCA: node = (angle + 512) >> 10, t = signed low 10 bits, dx = t << 13
    node6 = fpul[15:10] + 6'(fpul[9]);                  // round to the nearest centre, wraps at the full turn
    t10  = fpul[9:0];
    // FSRRA: node = (m + 2^18) >> 19, t = signed low 19 bits, dx = t << 4, exponent parity selects the table half
    fr_sign = frn[31]; fr_exp = frn[30:23]; fr_man = frn[22:0];
    fr_e    = 9'(fr_exp) - 9'sd127;
    fr_par  = fr_e[0];
    fr_k    = fr_e >>> 1;                       // (e - parity) / 2 == floor(e / 2)
    d_ent_r = 6'((24'(fr_man) + 24'(1 << 18)) >> 19) + (fr_par ? 6'd17 : 6'd0);   // 24-bit: node 16 spans m >= 0x7c0000
    t19     = fr_man[18:0];
    d_dx    = (op == OP_FSCA) ? (24'(t10) <<< 13) : (24'(t19) <<< 4);
    // specials
    // causes {V, Z, O, U, I}: a signalling NaN has mantissa bit 22 set; an infinity or quiet NaN passing through raises
    // nothing; FSRRA of +-0 (a denormal is a zero, DN = 1) is a division by zero; with DN = 0 a denormal is E when
    // positive, V (qNaN) when negative [HW model/fsrra/hw_specials]
    d_sp_v = 1'b0; d_sp_r = 32'h0; d_sp_x = 5'b0; d_sp_e = 1'b0;
    fr_den = dn0 && fr_exp == 8'h00 && fr_man != 23'd0;
    if (op == OP_FSRRA) begin   // SH7750 manual; not exercised by the dumps
      d_sp_v = (fr_exp == 8'hff) || (fr_exp == 8'h00) || fr_sign;
      d_sp_r = (fr_exp == 8'hff) ? ((fr_man != 0 || fr_sign) ? 32'h7fbfffff : 32'h00000000)
             : (fr_exp == 8'h00 && !fr_den) ? {fr_sign, 31'h7f800000}
             : 32'h7fbfffff;
      d_sp_x = (fr_exp == 8'hff) ? {(fr_man != 0) ? fr_man[22] : fr_sign, 4'b0}
             : (fr_exp == 8'h00 && !fr_den) ? 5'b01000
             : (fr_den && !fr_sign) ? 5'b00000
             : 5'b10000;
      d_sp_e = fr_den && !fr_sign;
    end
    n_pinf = 3'd0; n_ninf = 3'd0; n_nan = 1'b0; n_any = 1'b0; n_inv = 1'b0;
    for (int i = 0; i < 4; i++) begin
      logic [7:0] ea, eb; logic [22:0] fa, fb; logic ia, ib, na, nb, za, zb;
      ea = fva[i][30:23]; eb = fvb[i][30:23]; fa = fva[i][22:0]; fb = fvb[i][22:0];
      ia = (ea == 8'hff) && (fa == 0); ib = (eb == 8'hff) && (fb == 0);
      na = (ea == 8'hff) && (fa != 0); nb = (eb == 8'hff) && (fb != 0);
      za = (ea == 0) && (!dn0 || fa == 0); zb = (eb == 0) && (!dn0 || fb == 0);   // DN = 0: a denormal is nonzero
      if (na || nb || (ia && zb) || (ib && za)) n_nan = 1'b1;
      if ((na && fa[22]) || (nb && fb[22]) || (!na && !nb && ((ia && zb) || (ib && za)))) n_inv = 1'b1;
      // a product with a NaN operand is a NaN, not an infinity: it never counts towards "infinities of both signs"
      // (model/fipr/sh4_fipr.h: `if (na || nb) { ...; continue; }`; WI-CORE-F: a quiet NaN x -inf beside a +inf product
      // raised V here -- the cause only, the qNaN result was already right)
      else if ((ia || ib) && !na && !nb) begin
        if (fva[i][31] ^ fvb[i][31]) n_ninf = n_ninf + 3'd1; else n_pinf = n_pinf + 3'd1;
      end
      else if (!za && !zb && (9'(e1(ea)) + 9'(e1(eb)) > 9'd75)) n_any = 1'b1;
    end
    if (op == OP_FIPR) begin   // model/fipr/README.md rules
      d_sp_v = n_nan || (n_pinf != 0) || (n_ninf != 0) || !n_any;
      d_sp_r = (n_nan || (n_pinf != 0 && n_ninf != 0)) ? 32'h7fbfffff
             : (n_pinf + n_ninf >= 3'd2) ? ((fva[0][31] ^ fvb[0][31]) ? 32'hff800000 : 32'h7f800000)
             : (n_pinf != 0) ? 32'h7f800000 : (n_ninf != 0) ? 32'hff800000 : 32'h00000000;
      // NaN: V when invalid (signalling operand, inf x 0, infinities of both signs); inf: none; no product: +0, U I
      d_sp_x = (n_nan || (n_pinf != 0 && n_ninf != 0)) ? {n_inv || (n_pinf != 0 && n_ninf != 0), 4'b0}
             : (n_pinf != 0 || n_ninf != 0) ? 5'b0 : 5'b00011;
    end
  end

  assign st_x = d_sp_v ? d_sp_x : 5'd0;
  assign st_e = d_sp_v && d_sp_e;

  // ================================================================== multipliers, latches, feedback selectors
  logic signed [MULW-1:0]  la [4], la_n [4];
  logic signed [RECW-1:0]  lb [4], lb_n [4];
  /* verilator lint_off UNUSEDSIGNAL */          // raw_c: the multiplier's diagnostic row sum (the product register is prod_c)
  logic signed [RAWW-1:0]  raw_c [4];
  /* verilator lint_on UNUSEDSIGNAL */
  logic signed [PRODW-1:0] prod_c [4];     // M[n]: combinational array outputs
  logic signed [PRODW-1:0] pr [4];         // product registers
  logic [2:0] s_next;

  genvar gm;
  generate
  for (gm = 0; gm < 4; gm = gm + 1) begin : g_mul
    fpu_booth_mul u_mul (.rec(lb[gm]), .mul(la[gm]), .raw(raw_c[gm]), .prod(prod_c[gm]));
  end
  endgenerate

  always_comb begin
    s_next = start ? 3'd1 : step + 3'd1;
    for (int i = 0; i < 4; i++) begin
      /* verilator lint_off UNUSEDSIGNAL */      // fb[28]: the feedback ports are 25 / 28 bits wide
      mul_ctl_t c; op_e o; logic signed [PRODW-1:0] fb; logic signed [23:0] dxv; logic sw; logic [3:0] rr; logic [1:0] qq; logic [5:0] er;
      /* verilator lint_on UNUSEDSIGNAL */
      o   = start ? op : op_r;
      c   = mctl(o, s_next, i);
      fb  = prod_c[c.fb_src] >>> c.fb_sh;                   // M[fb_src] fed back, top bits into the 25-bit port
      dxv = start ? d_dx : dx_r;
      rr  = start ? node6[3:0] : rom_r;
      qq  = start ? node6[5:4] : quad;
      sw  = pass_swap(qq, c.tab_cos);
      er  = start ? d_ent_r : ent_r;
      unique case (c.a_sel)
        A_OPA:   la_n[i] = MULW'({1'b0, fva[i][30:23] != 8'd0, fva[i][22:0]});   // a denormal: no hidden bit
        A_DX:    la_n[i] = MULW'(dxv);
        A_TAB:   la_n[i] = MULW'(rom_s(sw, rr));
        A_FB:    la_n[i] = MULW'(fb);
        default: la_n[i] = la[i];
      endcase
      unique case (c.b_sel)
        B_OPB:   lb_n[i] = RECW'({fvb[i][30:23] != 8'd0, fvb[i][22:0]});
        B_DX:    lb_n[i] = RECW'(dxv);
        B_CONST: lb_n[i] = const_of(o, c.const_sel, sw, rr, er);
        B_FB:    lb_n[i] = RECW'(fb);
        default: lb_n[i] = lb[i];
      endcase
    end
  end

  // ================================================================== F2: aligners + adder
  add_ctl_t     actl_n [4], actl_r [4];
  logic [26:0]  c0_n, c0_r;
  logic         negate_n, negate_r, second_n, f2_second, f2_v, rz_f2;
  logic [8:0]   emax_n, emax_f2;
  logic         sp_f2_v; logic [31:0] sp_f2_r; logic [4:0] sp_f2_x; logic fipr_f2, sp_f2_e, dn0_f2;
  logic signed [PRODW-1:0] p_in [4];
  logic signed [ALNW-1:0]  q [4];
  logic         cin [4];
  logic signed [SUMW-1:0]  sum_c, sum_r;

  always_comb begin
    fipr_emax = 9'd0;
    for (int i = 0; i < 4; i++) if (pvalid[i] && pE[i] > fipr_emax) fipr_emax = pE[i];
    for (int i = 0; i < 4; i++) actl_n[i] = '{src: ADD_ZERO, sh: 6'd0, inv: 1'b0};
    c0_n = '0; negate_n = 1'b0; emax_n = 9'd254; second_n = 1'b0;
    unique case (op_r)
      OP_FIPR: begin
        emax_n = fipr_emax;
        for (int i = 0; i < 4; i++) begin
          logic [9:0] d; d = 10'(fipr_emax) - 10'(pE[i]);
          actl_n[i] = '{src: pvalid[i] ? ADD_PROD : ADD_ZERO, sh: (d > 10'd63) ? 6'd63 : d[5:0], inv: psign[i]};
        end
      end
      OP_FSCA: begin
        logic cosine, sw;
        cosine = (step == 3'd4); second_n = cosine;
        sw = pass_swap(quad, cosine);                                                          // odd quadrant: words swap, odd terms flip
        actl_n[0] = '{src: ADD_PROD, sh: 6'd6,                          inv: ~sw};   // -s*T3  cubic  (FM0)
        actl_n[1] = '{src: ADD_C0,   sh: 6'd0,                          inv: 1'b0};  // +C0
        actl_n[2] = '{src: ADD_PROD, sh: 6'd7 + 6'(rom_e(~sw, rom_r)),  inv: 1'b1};  // -T2    square (FM2)
        actl_n[3] = '{src: ADD_PROD, sh: 6'd3 + 6'(rom_e(sw, rom_r)),   inv:  sw};   // +s*T1  linear (FM3)
        c0_n = 27'(rom_c0(sw, rom_r)); negate_n = pass_neg(quad, cosine);
      end
      default: begin   // FSRRA
        actl_n[0] = '{src: ADD_PROD, sh: 6'd7,                        inv: 1'b1};   // -T3 cubic  (FM0)
        actl_n[1] = '{src: ADD_C0,   sh: 6'd0,                        inv: 1'b0};   // +C0
        actl_n[2] = '{src: ADD_PROD, sh: 6'd9,                        inv: 1'b0};   // +T2 square (FM2), fixed-point field: no exponent
        actl_n[3] = '{src: ADD_PROD, sh: 6'd6 - 6'(FSRRA_E1[ent_r]),  inv: 1'b1};   // -T1 linear (FM3)
        c0_n = FSRRA_C0[ent_r]; emax_n = emax_r;
      end
    endcase
  end

  genvar ga;
  generate
  for (ga = 0; ga < 4; ga = ga + 1) begin : g_aln
    always_comb begin
      unique case (actl_r[ga].src)
        ADD_PROD: p_in[ga] = pr[ga];
        ADD_C0:   p_in[ga] = PRODW'(c0_r);
        default:  p_in[ga] = '0;
      endcase
    end
    fpu_align u_aln (.p(p_in[ga]), .sh(actl_r[ga].sh), .inv(actl_r[ga].inv), .q(q[ga]), .cin(cin[ga]));
  end
  endgenerate
  fpu_add4 u_add (.q(q), .cin(cin), .sum(sum_c));

  // ================================================================== F3: normalise / round
  logic         f3_v, f3_second, negate_f3, rz_f3, sp_f3_v;
  logic [8:0]   emax_f3;
  logic [31:0]  sp_f3_r, f_c;
  logic [4:0]   sp_f3_x;
  logic         fipr_f3, ovf_c, sp_f3_e, dn0_f3;
  fpu_normround u_nr (.sum(sum_r), .negate(negate_f3), .emax(emax_f3), .rz(rz_f3), .dn0(dn0_f3), .f(f_c), .ovf(ovf_c));

  // ================================================================== registers
  always_ff @(posedge clk) begin
    if (rst) begin
      step <= 3'd0; f2_v <= 1'b0; f3_v <= 1'b0; res_valid <= 1'b0; sp_v <= 1'b0;
    end else if (!hold) begin
      // ---- issue (cycle 0)
      if (start) begin
        op_r <= op; rz_r <= rz; dn0_r <= dn0; step <= 3'd1; dx_r <= d_dx; isn_r <= isn;
        rom_r <= node6[3:0]; quad <= node6[5:4];
        ent_r <= d_ent_r; emax_r <= 9'(9'sd254 - fr_k);
        sp_v <= d_sp_v; sp_r <= d_sp_r; sp_x <= d_sp_x; sp_e <= d_sp_e;
        for (int i = 0; i < 4; i++) begin
          logic [7:0] ea, eb;
          logic       nza, nzb;                            // nonzero finite operand (DN = 0: a denormal too)
          ea = fva[i][30:23]; eb = fvb[i][30:23];
          nza = ea != 0 || (dn0 && fva[i][22:0] != 0);
          nzb = eb != 0 || (dn0 && fvb[i][22:0] != 0);
          pE[i]     <= 9'(e1(ea)) + 9'(e1(eb));             // a denormal's exponent is 1
          pvalid[i] <= nza && nzb && (ea != 8'hff) && (eb != 8'hff) && (9'(e1(ea)) + 9'(e1(eb)) > 9'd75);
          psign[i]  <= fva[i][31] ^ fvb[i][31];
        end
      end else if (step != 3'd0) begin
        step <= (step == last_step(op_r) || kl(isn_r)) ? 3'd0 : step + 3'd1;
      end
      // ---- multiplier latches (for the next step) and product registers
      for (int i = 0; i < 4; i++) begin
        la[i] <= la_n[i]; lb[i] <= lb_n[i];
        if (step != 3'd0) pr[i] <= prod_c[i];
      end
      // ---- F1 -> F2 (for the step whose products go to the adder)
      f2_v <= (step != 3'd0) && add_go(op_r, step) && !kl(isn_r);
      isn_f2 <= isn_r; isn_f3 <= isn_f2;
      for (int i = 0; i < 4; i++) actl_r[i] <= actl_n[i];
      c0_r <= c0_n; negate_r <= negate_n; emax_f2 <= emax_n; f2_second <= second_n; rz_f2 <= rz_r; dn0_f2 <= dn0_r;
      sp_f2_v <= sp_v; sp_f2_r <= sp_r; sp_f2_x <= sp_x; sp_f2_e <= sp_e; fipr_f2 <= op_r == OP_FIPR;
      // ---- F2 -> F3
      sum_r <= sum_c; f3_v <= f2_v && !kl(isn_f2); negate_f3 <= negate_r; emax_f3 <= emax_f2; f3_second <= f2_second; rz_f3 <= rz_f2;
      sp_f3_v <= sp_f2_v; sp_f3_r <= sp_f2_r; sp_f3_x <= sp_f2_x; sp_f3_e <= sp_f2_e; fipr_f3 <= fipr_f2; dn0_f3 <= dn0_f2;
      // ---- F3 -> result
      res_valid  <= f3_v && !kl(isn_f3);
      res_second <= f3_second;
      res        <= sp_f3_v ? sp_f3_r : f_c;
      // a finite result is always inexact; FIPR adds U for a zero / denormal-range result and O on overflow
      res_x      <= sp_f3_v ? sp_f3_x : {2'b00, fipr_f3 && ovf_c, fipr_f3 && f_c[30:23] == 8'd0, 1'b1};
      res_e      <= sp_f3_v && sp_f3_e;
    end
  end
  assign busy = (step != 3'd0) || f2_v || f3_v;
endmodule
