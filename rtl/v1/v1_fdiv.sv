// v1_fdiv.sv -- v1 FDIV / FSQRT unit (the FPU's F3 stage): single and double, correctly rounded (RN / RZ), FPSCR.DN
// = 1 or 0 (`dn0`, WI-FPDN), fixed latency.
//
// Timing contract (ARCH.md unit B; tp_timing.svh set_div):  `start` in cycle t loads the operands; `done` is high in
// cycle t + LAT - 1 exactly (LAT = 12 FDIV, 11 FSQRT, 25 FDIV DR, 24 FSQRT DR) with r / x valid, and r / x hold
// until the next result.  The recurrence is free again in cycle t + LAT - 2 (`busy` low), which is where the timing
// proxy lets the NEXT FDIV / FSQRT issue (set_div = LAT - 2): that cycle's clock edge moves the finished remainder
// into the round / output register while the recurrence loads the new operands, so the two operations overlap by
// one cycle.  A start while busy abandons the old operation (the proxy never does that).
//
//   cycle t              : start: unpack, special cases, exponent, prescale, first partial remainder -> registers
//   t+1 .. t+LAT-3       : one radix-8 iteration per cycle (N = LAT - 3: 9 / 8 / 22 / 21 iterations)
//   t+LAT-2              : round (v1_fp_pkg::round_pack) -> output register
//   t+LAT-1              : done, r, x
//
// Progress outputs for the FPU's result pass (rtl/v1/SOC.md 4.5, WI-CORE-F).  The FPU sends the quotient to the
// register file through its own F1 / F2 / FS pass, which takes the FE dispatch slot three pipe cycles before it writes
// (FS - 3).  `rq` marks the recurrence step three divider cycles before `done` (the step with 2 iterations left, cycle
// t+LAT-4): the FPU posts the pass request there, so with no hold the pass's FS is exactly the `done` cycle (FDIV:
// dispatch steal at issue + 9, quotient written at issue + 12, the timings2 sweep's numbers).  `last` is the last
// iteration (the recurrence is free next cycle: a single-precision divide dispatched now starts in its F1 then).
// `hold` is the FPU's `hold_div` (pfz / run = 0 only): the recurrence keeps running under the MA hold `mah` [HW ci3:
// FDIV ; P4 read ; FADD(quotient) 15.0, the same as with a cache hit; 17 if the divider stopped].
//
// Kill (SOC.md 4.3.1): the operation carries its instruction's ISN (`isn` at start); kill_v with ISN >= kill_isn
// (kill_incl) or > kill_isn stops it in the recurrence or the round stage (no `done`).  An older divide is never
// stopped: it completes and writes its quotient [HW core-integration 6.1: FDIV ; trapping FADD, FR7 = 1.5].
//
// The recurrence is the textbook restoring one, three radix-2 steps chained per cycle (radix 8), shared by divide
// and square root (a 1997 unit would more likely use SRT with a redundant remainder; the digit count per cycle is
// what the latency pins, and N * 3 quotient bits cover the 24 / 53 result bits plus the round bit in every case:
// FSQRT single is exactly 8 * 3 = 24 fraction bits = 23 + round).  Fixed point with F = 72 fraction bits:
//   divide : A / D with A, D in [1, 2) (A doubled and the exponent decremented when A < D, so Q in [1, 2));
//            Q0 = 1, w0 = A - D;  step j: w' = 2w - q_j D,              q_j = [2w >= D]
//   sqrt   : x in [1, 4) (mantissa doubled for an odd exponent, which is then halved);
//            Q0 = 1, w0 = x - 1;  step j: w' = 2w - q_j (2Q + 2^-j),    q_j = [2w >= 2Q + 2^-j]
// with the invariant w_j = 2^j (A - Q_j D) resp. 2^j (x - Q_j^2), 0 <= w_j < D resp. < 2Q_j + 2^-j (< 5): every
// quantity is exact on the 2^-72 grid (operands have 52 fraction bits, 2^-j reaches 2^-66), so Q_N is the exact
// quotient / root truncated to 3N fraction bits and w_N != 0 is the sticky bit.  2^-j is a one-hot register `pos`
// shifted right per step, so each quotient bit is OR-ed in at a fixed position (no quotient shifter).
//
// Special cases (Section 10 FDIV / FSQRT; v1_fp_pkg::fdiv_spec, which the FPU's operand check shares), resolved at
// start and delivered at the same fixed time.  DN = 1: a denormal operand is a zero of its sign.
//   FDIV a / b: sNaN -> V, qNaN; qNaN -> qNaN; inf/inf, 0/0 -> V, qNaN; inf/x -> inf (x = 0 included, no Z);
//               x/inf -> 0; x/0 (x finite nonzero) -> Z, inf; 0/x -> 0; signs = xor.  Overflow / underflow via
//               round_pack (inf or max finite; tiny -> zero with U + I, or with DN = 0 the denormal).
//   FSQRT a   : sNaN -> V, qNaN; qNaN -> qNaN; +-0 -> +-0; +inf -> +inf; negative nonzero (incl. -inf) -> V, qNaN;
//               never overflows or underflows; I when inexact.
// DN = 0 (FPSCR.DN = 0, [HW tools/hw/fpdn]): a denormal operand is the FPU error E (fdiv_spec's e) except where a
// special case wins (x / 0 is Z; a negative denormal's root is V; FDIV DR's high / low word rules).  The FPU starts an
// E operation only as a D-decided trap's pass (SOC.md 4.3.8: an F1 / D operand trap otherwise kills it): it is then a
// special case here (the qNaN at the fixed time, cause 0) and the FPU's record writes E alone; so the recurrence only
// ever iterates on normal operands.  A trap pass of a normal operation runs the recurrence like any divide: its
// result's cause is the trap's (the FPU writes no destination).  A
// quotient below the normal range is rounded once on the denormal grid by round_pack's denormal shifter in the round
// stage: no extra cycle (DN = 0 latency is not measured; the same fixed latency is [P]).
module v1_fdiv
  import v1_fp_pkg::*;
(
  input  logic        clk, rst,
  input  logic        start,
  input  logic        hold,     // the divider is frozen (pfz, run = 0): every register holds (start is ignored)
  input  logic        sqrt,     // 0 FDIV a / b (a = FRn / DRn, b = FRm / DRm), 1 FSQRT a
  input  logic        dbl,      // FPSCR.PR
  input  logic        rz,       // FPSCR.RM == 1
  input  logic        dn0,      // FPSCR.DN == 0: gradual underflow (the E cases are the FPU's: fdiv_spec)
  input  logic [63:0] a, b,     // single operands in [31:0]
  input  logic [7:0]  isn,      // ISN of the instruction a start belongs to
  input  logic        kill_v,   // kill ISN >= kill_isn (kill_incl) or > kill_isn
  input  logic [7:0]  kill_isn,
  input  logic        kill_incl,
  output logic        busy,     // recurrence occupied (a start now would abandon the running operation)
  output logic        last,     // the last iteration: the recurrence is free next cycle
  output logic        rq,       // two iterations left: `done` follows three divider cycles later
  output logic        done,     // the result is in r / x this cycle
  output logic [63:0] r,        // single result in [31:0], [63:32] = 0
  output fexc_t       x
);
  localparam int F  = 72;       // fraction bits of the fixed-point recurrence
  localparam int WW = F + 5;    // remainder / trial width: 2w < 10 needs 4 integer bits, one spare

  // ---- recurrence state ---------------------------------------------------------------------------------------
  logic               act;                  // an operation is in the recurrence (iterating, or waiting to round)
  logic [4:0]         cnt;                  // iterations left
  logic [7:0]         o_isn;                // its instruction's ISN
  logic               o_sqrt, o_dbl, o_rz, o_dn0;  // latched mode of that operation
  logic               o_spec;               // special case: o_spec_r / o_spec_x are the result
  logic [63:0]        o_spec_r;
  fexc_t              o_spec_x;
  logic               o_s;                  // result sign
  logic signed [15:0] o_e;                  // result exponent (unbiased), Q in [1, 2)
  logic [WW-1:0]      w;                    // partial remainder, F fraction bits
  logic [F:0]         q;                    // quotient / root 1.F (bit F = 1.0)
  logic [F:0]         pos;                  // 2^-j of the next step (one-hot)
  logic [F:0]         dv;                   // divisor 1.F

  // ---- output register (the round stage) --------------------------------------------------------------------------
  logic               done_q;
  logic [63:0]        r_q;
  fexc_t              x_q;

  assign busy = act && cnt != 5'd0;
  assign last = act && cnt == 5'd1;
  assign rq   = act && cnt == 5'd2;
  assign done = done_q;
  // the running operation is killed this cycle ("younger or equal" = (isn - K) mod 256 < 128, SOC.md 4.3.1)
  logic [7:0] kd;
  assign kd = o_isn - kill_isn;
  wire killed = kill_v && act && !kd[7] && (kill_incl || kd != 8'd0);
  assign r    = r_q;
  assign x    = x_q;

  // ---- one radix-2 restoring step (three per cycle) ---------------------------------------------------------------
  typedef struct packed { logic [WW-1:0] w; logic [F:0] q; logic [F:0] pos; } st_t;

  function automatic st_t step(input st_t s, input logic is_sqrt, input logic [F:0] d);
    st_t           o;
    logic [WW-1:0] t, w2;
    t  = is_sqrt ? (WW'(s.q) << 1) + WW'(s.pos) : WW'(d);   // trial subtrahend: 2Q + 2^-j, or D
    w2 = s.w << 1;
    o.pos = s.pos >> 1;
    if (w2 >= t) begin o.w = w2 - t; o.q = s.q | s.pos; end
    else         begin o.w = w2;     o.q = s.q;         end
    return o;
  endfunction

  st_t cur, nx1, nx2, nx3;
  always_comb begin
    cur = '0; cur.w = w; cur.q = q; cur.pos = pos;                // (field by field: a pattern key that is also an identifier here is read as an expression by Quartus 17.0)
    nx1 = step(cur, o_sqrt, dv);
    nx2 = step(nx1, o_sqrt, dv);
    nx3 = step(nx2, o_sqrt, dv);
  end

  // ---- load: unpack, special cases, prescale (combinational on the start-cycle operands) --------------------------
  /* verilator lint_off UNUSEDSIGNAL */          // ua / ub: the recurrence reads sign, exponent and significand only
  fu_t                ua, ub;
  /* verilator lint_on UNUSEDSIGNAL */
  dspec_t             sp;
  logic               l_spec;
  logic [63:0]        l_spec_r;
  fexc_t              l_spec_x;
  logic               l_s;
  logic signed [15:0] l_e;
  logic [F:0]         l_ma, l_mb;
  logic [F+1:0]       l_x;                  // radicand in [1, 4): two integer bits
  logic [WW-1:0]      l_w;
  logic [4:0]         l_n;

  always_comb begin
    ua = unpack(a, dbl, dn0);
    ub = unpack(b, dbl, dn0);
    sp = fdiv_spec(a, b, sqrt, dbl, dn0);
    l_spec = sp.spec; l_spec_r = sp.e ? k_qnan(dbl) : sp.r; l_spec_x = sp.x;
    l_s = 1'b0; l_e = 16'sd0; l_w = '0; l_x = '0;
    l_ma = (F+1)'(ua.m) << (F - 52);
    l_mb = (F+1)'(ub.m) << (F - 52);
    // iterations: LAT - 3
    case ({dbl, sqrt})
      2'b00:   l_n = 5'd9;    // FDIV    LAT 12
      2'b01:   l_n = 5'd8;    // FSQRT   LAT 11
      2'b10:   l_n = 5'd22;   // FDIV DR LAT 25
      default: l_n = 5'd21;   // FSQRT DR LAT 24
    endcase
    // the recurrence's operands (normal numbers whenever no special case applies)
    if (sqrt) begin
      l_s = ua.s;
      l_e = ua.e >>> 1;                                    // floor(e / 2): odd e -> mantissa doubled, (e-1)/2
      l_x = ua.e[0] ? {l_ma, 1'b0} : {1'b0, l_ma};
      l_w = WW'(l_x) - WW'({1'b1, {F{1'b0}}});             // x - 1
    end else begin
      l_s = ua.s ^ ub.s;
      if (ua.m < ub.m) begin l_e = ua.e - ub.e - 16'sd1; l_w = (WW'(l_ma) << 1) - WW'(l_mb); end
      else             begin l_e = ua.e - ub.e;          l_w = WW'(l_ma) - WW'(l_mb);        end
    end
  end

  // ---- round stage: Q (1 + 3N fraction bits) + sticky (remainder != 0) -> 24 / 53 bits ----------------------------
  /* verilator lint_off UNUSEDSIGNAL */          // rnd.e: round_pack never raises E (the E cases are fdiv_spec's)
  fd_res_t rnd;
  /* verilator lint_on UNUSEDSIGNAL */
  always_comb begin
    if (o_spec) begin rnd.r = o_spec_r; rnd.x = o_spec_x; end
    else        rnd = round_pack(o_s, o_e, q[F -: 64], (q[F-64:0] != '0) || (w != '0), o_dbl, o_rz, o_dn0);
  end

  always_ff @(posedge clk) begin
    if (rst) begin
      act <= 1'b0; cnt <= 5'd0; done_q <= 1'b0; r_q <= '0; x_q <= '0;
    end else begin
      // the kill stops the operation wherever it is (a kill never comes while the divider is held: SOC.md 4.3.2 takes
      // happen in unheld cycles only); a younger start in the same cycle cannot exist (it would be killed too)
      if (killed) act <= 1'b0;
      if (!hold) begin
        done_q <= 1'b0;
        if (act && cnt == 5'd0 && !killed) begin          // round stage
          r_q <= rnd.r; x_q <= rnd.x; done_q <= 1'b1; act <= 1'b0;
        end
        if (act && cnt != 5'd0) begin                     // radix-8 iteration
          w <= nx3.w; q <= nx3.q; pos <= nx3.pos; cnt <= cnt - 5'd1;
        end
        if (start) begin                                  // load (overrides: a start while busy abandons)
          act <= 1'b1; cnt <= l_n; o_isn <= isn;
          o_sqrt <= sqrt; o_dbl <= dbl; o_rz <= rz; o_dn0 <= dn0;
          o_spec <= l_spec; o_spec_r <= l_spec_r; o_spec_x <= l_spec_x;
          o_s <= l_s; o_e <= l_e;
          w   <= l_w;
          q   <= {1'b1, {F{1'b0}}};
          pos <= {2'b01, {(F-1){1'b0}}};
          dv  <= l_mb;
        end
      end
    end
  end
endmodule
