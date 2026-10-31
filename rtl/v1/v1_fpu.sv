// v1_fpu.sv -- the SH7091 FPU of the v1 core: registers, pipes, sequencers and the FPU half of the D-stage interlock.
//
// Registers: FR (2 banks x 16, physical index bank * 16 + i), FPUL, FPSCR, and MACH / MACL (the integer multiplier is
// the FPU's multiplier array).  Every register file is write-through: a value written in cycle c is read in cycle c.
//
// Units, and the timing each one's structure implies (c = issue cycle of the producer; "D x" = a consumer may issue in
// cycle x; the numbers are the SH7091's, measured by the timings2 sweep):
//   * FE pipe F1 F2 FS (FADD FSUB FMUL FMAC FLOAT FTRC FCMP, FRCHG / FSCHG passes).  Operands are read in D and taken
//     at F1 through a bypass from the integer LS pipe (FP moves are known in EX, FP loads at the end of MA); the FE
//     pipe itself has no bypass: its result reaches readers only through the register file at FS.  So an FE result is
//     read at D c+3 (F1 F2 FS), an FP move's at D c (a follower of the same pair) or later, an FP load's at D c+2.
//     An FP store takes its data at the start of EX, one stage later than D: it may issue at c+2 (the FE pass is in
//     FS when the store is in EX).  STS FPUL,Rn takes FPUL in MA: FTRC -> STS FPUL,Rn at c+1.
//     FCMP's T goes to the CPU at the end of F1 (T readers at D c+2); its flags reach the architectural SR at FS
//     (STC SR at c+3).  The cause bits reach FPSCR one cycle after FS: LDS / STS FPSCR at c+4.
//   * double precision (PR = 1): the op holds F1 (and the array) for L1 passes, then sends the lower word, and one
//     cycle later the upper word, to F2: FADD / FSUB / FMUL DR L1 = 6 (words at FS c+7 / c+8), FLOAT / FCNVSD DR
//     L1 = 2 (c+3 / c+4), FTRC / FCNVDS DR L1 = 2 (FPUL at c+4), FDIV / FSQRT DR L1 = 2 (setup, then the divider).
//     Its operands are read from the register file only (no bypass from the LS pipe: an FP move or load is read at
//     S, c+3).  While the op is in F1 a comparator on its destination pair holds late readers (FP stores, STS FPUL)
//     and, until its last pass, later writers; FE ops and the array wait for F1.  FADD / FSUB / FMUL DR re-read their
//     operands in their first F1 pass, so a writer of a source waits one cycle (D c+2).
//   * divider (F3, v1_fdiv): FDIV / FSQRT single start at F1 with the bypassed operands, DR start in D.  The quotient
//     reaches the register file through a result pass of its own: the recurrence posts a pass request three divider
//     cycles before the quotient is rounded (v1_fdiv `rq`); the request takes the FPU dispatch slot (no FE instruction
//     issues in that cycle: the "steal") in the first cycle the FE pipe is not held, and the pass then runs F1 F2 FS
//     like any FE pass and writes FR at FS (a double's upper word one pass, one cycle, later).  With no hold the pass
//     is granted when it is posted: FS at c+12 / c+11 (single, FS - 3 = the steal at c+9 / c+8) and c+24 / c+23 (DR,
//     then the upper word), v1's numbers.  The next divide may start when the recurrence frees (D c+10 / c+9 /
//     c+23 / c+22: the divider's own `busy` / `last`) and a destination record is free.  Late readers and later
//     writers wait for the destination until the final pass has left F1 (FS - 1); readers until its FS.
//   * vector unit (rtl/fpu_vec: FIPR 1 array pass, FSRRA 3, FSCA 4; FTRV = four FIPR passes): operands from the
//     register file in D; element results at FS c+4 (FIPR), c+6 (FSRRA, FSCA sine), c+7 (FSCA cosine), c+4..c+7
//     (FTRV rows).  The array is busy c+1..c+p (another vector op, a multiply, FRCHG / FSCHG wait: D c+p); each pass
//     then uses F1's adder, so FE arithmetic waits for the last one (D c+p+1).  A later writer of the destination waits
//     until the last FS - 2 (the write order); FTRV locks its destination FV group against LS FP accesses the same way
//     (the compare ignores the bank bit and, for FE writers, covers only DR(4n)).
//   * multiplier: MUL / DMUL / MAC write MACH:MACL at FS c+6 (operand stages, one F2 accumulate, FS); CLRMAC and
//     LDS MACx enter two stages later (FS c+5).  The array stages hold the FE issue port while the FS of a multiply is
//     three or more cycles away (FE at c+4 after MUL, c+3 after CLRMAC); STS MACx,Rn samples MACH / MACL in its MA
//     stage (c+4 / c+3), STS.L MACx in EX (c+5 / c+4).  Multiplies flow in order through the same stages and never
//     wait for each other.
//   * FPSCR mode: FRCHG / FSCHG flip the decoder's FR / SZ at issue (a follower of the same pair cannot use it);
//     LDS FPSCR loads it at S, so every FPU-decoded instruction waits until then (D c+4).  The FPU decoder claims the
//     whole F opcode group before it knows the mode, so a code that is illegal in the old mode waits too.
//   * result tags: the divider and the double sequencer can start the next operation while the previous one is still
//     rounding / sending its words to FS, so each keeps two destination records (the running operation and the one
//     finishing); a write-back always uses its own record.
//   * FPSCR cause: every cause writer takes a tag at issue from a counter of cause-writer issues.  The divider, the
//     double sequencer and the vector unit finish out of program order with the FE pipe, so a completion rewrites
//     the cause field only while its tag is still the counter's value (no younger cause writer has issued); the
//     flags always OR.  The field thus ends with the youngest instruction's cause, as in program order.  The vector
//     unit reports each result word's cause (fpu_vec_top res_x: the measured rules of model/*/sh4_*.h); FTRV's later
//     rows OR into the field that its first row rewrote.
//   * pairing: besides RAW / WAW between the leader's results and the follower, a follower that writes a source word
//     of FADD / FSUB / FMUL DR cannot pair with it (the re-read in the first F1 pass).
//
// The two holds (rtl/v1/SOC.md 3.2.2, 4.5; WI-CORE-F).  `hold_pipe` (pfz | mah | run = 0) holds the FE pipe F1 / F2 /
// FS, the double sequencer, the vector array passes, the FTRV row sequencer, the multiplier stages, every age / record
// counter of those and the register writes; `hold_div` (pfz | run = 0) holds only the divider recurrence and its own
// bookkeeping (the request post, the quotient capture).  So under the MA hold `mah` the recurrence runs on, its result
// pass request waits for the FE pipe -- granted the cycle after the hold ends (the arbiter's registered view of the
// MA wait, below) -- and the pass then takes F1 F2 FS: [HW ci3] FDIV ; P4 read (2 mah cycles) ; FADD(quotient) 15.0
// (the request comes after the hold: FS c+12, on time); with an uncached read (14 / 15 mah cycles by Bclk phase)
// 24.1 = FS at the ack + 4, the loop settling on the even length of 23 / 24; FDIV before the uncached read with no
// consumer 20.1 (the next FDIV reads its FR7 at that FS).  v1's frozen-age divider bookkeeping gives 17 / 30 there,
// a pass granted in the ack cycle 15 / 22.  With hold_pipe = hold_div (no `mah`) the FPU is cycle-identical to v1's
// single `frz` (make v1).
//
// Kill (SOC.md 4.3.1, core-integration 3.5.2).  Every record carries its instruction's ISN: the F1 / F2 / FS latches,
// the double sequencer's records, the divider's records and its recurrence (v1_fdiv), the vector records and the
// operation in each vector stage (fpu_vec_top), the FTRV row sequencer, the multiplier records, FCMP DR's pending
// compare pass.  kill_v with ISN >= kill_isn (kill_incl) or > kill_isn removes them all in that cycle (their writes
// in that cycle are gated too), so nothing a killed instruction started ever writes FR / FPUL / FPSCR / MACx or T.
// Older work completes (an FDIV in flight writes its quotient after a younger FADD trapped [HW ci run 1]).  A killed
// FRCHG / FSCHG flips the decoder mode back: every flip rides its F1 / F2 / FS pass, and the kill XORs the killed
// passes' flips out of FPSCR [HW ci2: a faulting load paired with FRCHG or FSCHG leaves FPSCR 0x00040000].  The cause
// tag counter is repaired: it counts up once per cause writer, so it goes back by the number of killed cause writers
// (each instruction has one owner record of its tag), and the youngest surviving writer owns the field again.
//
// FPU exceptions (SOC.md 4.3.2 / 4.3.8, core-integration 3.5.5, exceptions-intc 3.8).  No new issue comparator.
// Trap effects (SOC.md 4.3.8) [HW tools/hw/fpdn: FSQRT(1.5) + EN.I leaves 0x00001084, FMUL.D max x 1.5 + EN.I
// 0x00085094, an exact FADD + EN.I 0x00040080, sNaN + EN.V 0x00050840]: the destination is never written; FPSCR's
// cause and flags are what the untrapped execution writes -- cause <- the computed {V, Z, O, U, I}, flags |= them --
// except for an E trap, whose cause is E alone with the flags unchanged.
//   * `trap_d`: decided at D from the opcode and the enables: an enabled O / U / I that the instruction can raise, and
//     FTRV's V [HW model/timing, exceptions-intc run 1]; plus FCMP DR's V, from its register-file operands [P: FCMP DR
//     reads the register file only, so its operands at D are its F1 operands; its compare pass is a sequencer step
//     with no EX micro-op to convert].  The core issues EXC0 instead of the instruction; the instruction still
//     dispatches here as a trap pass (di_v with trap_d), which is the instruction's own execution with its
//     destination cleared: it computes the operation on the path an untrapped one takes and writes only the cause.
//     A single FE op's pass computes at F1 (bypassed operands) and writes the cause at FS; a double's (and FCMP DR's)
//     cause is computed at D from the register file (the double sequencer's value functions) and rides one F1 F2 FS
//     pass; FDIV / FSQRT (single and DR) run the recurrence -- the divider record and its result pass(es), the double
//     sequencer's setup passes for DR -- and the result pass writes the cause (so a trap pass occupies the divider as
//     the untrapped divide does [P: the handler's first FDIV / STS FPSCR after a trapped FDIV issues when it would
//     after the same FDIV untrapped; not measured]); a vector trap pass runs its array passes, its results write the
//     cause (model/fipr, model/fsca, model/fsrra: "the destination is left unchanged while cause and flag are still
//     updated", measured).  Every trap pass holds the FPSCR readers (LDS / STS FPSCR: fscr_busy) until its cause is
//     written, like any cause writer.
//   * `trap_f1`: V / Z / E on the operands, checked where the instruction first is in F1 (the cycle after issue):
//     single FE ops on the bypassed operands [HW ci2: a sNaN loaded by the FMOV.S right before FADD traps as fast as
//     one from the register file]; doubles and the vector ops (FIPR V, FSRRA V / Z) on the register-file operands they
//     read at D, registered with their record.  E (FPSCR.DN = 0, a denormal operand, never masked) as the console
//     raises it [HW tools/hw/fpdn, SOC.md 9.17; WI-FPDN]: the value functions' `e` (v1_fp_pkg: FADD FSUB FMUL FMAC
//     FCNVSD FCNVDS, after their NaN / invalid checks; FTRC and FCMP never), fdiv_spec's for FDIV / FSQRT, and the
//     vector unit's st_e for FSRRA.  DN = 0 reaches every value function, the divider and the vector unit (`dn0`).
//     The core converts the instruction's own micro-op (in EX) into EXC0 in place and kills
//     everything younger; this FPU then drops the trapping instruction's own work (a killed kill_incl = 0 kill naming
//     trap_f1_isn is the taken trap) and writes its cause in the take cycle: the operation's computed cause, which
//     for a V / Z trap is V / Z alone (an invalid operation's qNaN and a division by zero's infinity are exact), or E
//     alone; a vector op runs on without a destination and writes its cause at its results.
// The trapping instruction keeps its cause tag and writes its cause at the trap; an older completion that lands
// later only ORs its flags [HW ci2: FDIV 1/3 in flight, younger FADD V trap -> 0x00050844, FR9 = 0x3EAAAAAB].
//
// Commit-log records (SOC.md 6.3; the core puts them into its tap, v1_core.tap.rec[CT_CORE_N]).  `frec` carries one
// record per architectural write this FPU makes in the cycle, with the writing instruction's ISN: CK_FPR (idx = the
// physical FR, bank * 16 + i), CK_SYS SY_FPUL / SY_MACH / SY_MACL, and CK_SYS SY_FPSCR for the mode writers -- LDS /
// LDS.L FPSCR at S (the whole value) and FRCHG / FSCHG when their pass leaves FS unkilled (the value after the flip
// in program order: the flips of younger passes still in F1 / F2 taken out).  A cause writer's cause / flag update
// has no record (SOC.md 6.3: an FPSCR change of the cause / flag fields alone needs none; an older divide's flags land
// after younger writers); nor does a trap pass, a killed record or the reset / backdoor.  A record is emitted in the
// cycle its register is written (an unheld cycle).  Six slots (the FPU's share of CT_CORE_N), fixed by source (at most
// this many writes per cycle, by structure; `rec_ovf_err` checks it for the testbenches):
//   frec[0..1]  the LS pipe at S: one micro-op per cycle writes FPU state (LS never pairs with LS; a CO issues alone):
//               a 64-bit move's pair (two FPR), or FPUL, or FPSCR
//   frec[2..3]  the FE side, in write-through order: FS (an FPR / FPUL word, or FRCHG / FSCHG's FPSCR), the double
//               sequencer's word, a divider result pass's word, a vector result word.  At most two per cycle: an FE
//               op's FS never meets the others (a double holds F1 and the array until its words are out; a vector op
//               holds F1's adder; a result pass takes the dispatch slot, no FE op issues then), a double never meets
//               a vector result (PR = 1 against PR = 0: the mode changes only through LDS FPSCR, which waits for both)
//   frec[4], [5] MACH, MACL of the multiplier write (one per cycle: MUL / MAC are CO k = 2, CLRMAC / LDS enter two
//               stages later), as the instruction writes them: MUL.L / MULS.W / MULU.W / LDS MACL MACL, LDS MACH MACH,
//               DMULx / MACx / CLRMAC both
module v1_fpu import tp_pkg::*, v1_pkg::*, v1_fp_pkg::*, v1_mul_pkg::*; (
  input  logic        clk,
  input  logic        rst,
  input  logic        hold_pipe,              // pfz | mah | run = 0: the FE pipe, vector, multiplier, double sequencer
  input  logic        hold_div,               // pfz | run = 0: the divider recurrence
  input  logic        dbg_we,
  input  logic [7:0]  dbg_addr,
  input  logic [31:0] dbg_wdata,
  input  logic [7:0]  dbg_raddr,
  output logic [31:0] dbg_rdata,
  output logic        dbg_hit,
  // D stage
  input  logic        s_v [2],
  input  dec_t        s_d [2],
  input  ictl_t       s_c [2],
  input  logic [15:0] s_op [2],
  output logic [2:0]  md_dec,
  output logic        fhz [2],
  output logic [7:0]  fwhy [2],
  output logic        fdep,
  output logic        trap_d [2],             // D-decided FPU trap of the slot (enables x opcode; FCMP DR's V)
  // the integer pipe's latches (LS-pipe FP results, MAC micro-ops)
  input  uop_t        ex [2],
  input  uop_t        ma [2],
  input  uop_t        sl [2],
  input  uop_t        ex_res [2],
  // dispatch (this cycle's issues; di_v with trap_d is a trap pass)
  input  logic        di_v [2],
  input  logic [7:0]  di_isn [2],
  input  logic [15:0] di_op [2],
  input  dec_t        di_d [2],
  input  ictl_t       di_c [2],
  input  logic [2:0]  di_md [2],
  input  logic        di_fcmpd,
  input  logic [15:0] di_fcmpd_op,
  input  logic [2:0]  di_fcmpd_md,
  // the kill (SOC.md 4.3.1): ISN >= kill_isn (kill_incl) or > kill_isn; asserted only in cycles with hold_pipe = 0
  input  logic        kill_v,
  input  logic [7:0]  kill_isn,
  input  logic        kill_incl,
  // the F1 operand trap: the instruction whose micro-op is in EX lane trap_f1_lane converts in place
  output logic        trap_f1,
  output logic        trap_f1_lane,
  output logic [7:0]  trap_f1_isn,
  // the commit-log records of this cycle's register writes (the head; SOC.md 6.3; v1_core's tap slots 10-15)
  output v1_soc_pkg::commit_rec_t [5:0] frec,
  // multiplier operands from the integer pipe
  input  logic        mac_ld_a_v, mac_ld_b_v, def_ld_v,
  input  logic [31:0] mac_ld_a, mac_ld_b, def_ld,
  // SR.S as each EX micro-op sees it, in program order: the saturation mode of a MAC is the S it executes under,
  // not the S at its MACH / MACL write (a SETS / CLRS right behind it does not change it; [HW] tools/hw/mac-s)
  input  logic [1:0]  ex_s,
  // register reads (write-through)
  output logic [31:0] fr_now [32],
  output logic [31:0] fpul_now, fpscr_now, mach_now, macl_now,
  // T to the CPU
  output logic        t_busy, flag_busy,
  output logic        t_we, t_val, t_lane,
  output logic        idle
);
  // ============================================================================================================
  // registers
  logic [31:0] fr [32];
  logic [31:0] fpul, fpscr, mach, macl;
  logic        rst_q;                           // rst last cycle: a reset's first cycle loads FPSCR's reset value
  assign md_dec = {fpscr[21], fpscr[20], fpscr[19]};
  wire rz   = fpscr[0];
  wire dn0  = !fpscr[18];                       // FPSCR.DN = 0: denormals as numbers, gradual underflow, E (WI-FPDN)
  wire en_v = fpscr[11];                        // enables: V 11, Z 10, O 9, U 8, I 7
  wire en_z = fpscr[10];
  localparam logic [31:0] FPSCR_RESET = 32'h0004_0001;   // hm 6.4 (DN = 1, RM = RZ); exceptions-intc 3.16

  // ============================================================================================================
  // the kill
  function automatic logic yng(input logic [7:0] i, input logic [7:0] k, input logic incl);   // (i - k) mod 256 < 128
    logic [7:0] d;
    d = i - k;
    return !d[7] && (incl || d != 8'd0);
  endfunction
  // killed by the core's kill (the owner count of the cause tag, the write gating)
  function automatic logic kx(input logic [7:0] i);
    return kill_v && yng(i, kill_isn, kill_incl);
  endfunction
  // record removed this cycle: the core's kill, and a taken F1 trap's own FPU work (`ftk`)
  logic ftk;
  function automatic logic kl(input logic [7:0] i);
    return kill_v && yng(i, kill_isn, kill_incl || ftk);
  endfunction

  // ============================================================================================================
  // FE pipe latches
  typedef struct packed {
    logic        v;
    logic        lane;
    form_e       form;
    logic [15:0] op;
    logic [2:0]  md;
    logic [4:0]  rn, rm;                // physical FRn / FRm
    logic [31:0] n, m, z;               // FRn, FRm, FR0 values read in D
    logic [31:0] ul;                    // FPUL read in D
    logic [4:0]  cz;                    // cause tag (see cz_ctr)
    logic [7:0]  isn;
    logic        own;                   // this entry owns its instruction's cause tag (the tag repair's count)
    logic        tp;                    // a trap pass (D-decided trap): writes the trap's cause at FS, nothing else (an
                                        // FDIV / FSQRT: starts the divider, whose record writes it)
    logic        tpv;                   // ... with the cause computed at D (doubles, FCMP DR): tpx
    logic [5:0]  tpx;                   // {E, V, Z, O, U, I}: E alone, or the computed cause
    logic        fcd;                   // FCMP DR's compare pass (its V was decided at D)
    logic [1:0]  mfl;                   // {FR, SZ} decoder-mode flip this pass carries (FRCHG / FSCHG)
  } f1_t;
  typedef struct packed {
    logic        v;
    logic [31:0] wm;                    // FR words written at FS
    logic [31:0] we, wo;
    logic        uw;
    logic [31:0] uv;
    logic        cw;                    // cause bits committed at FS (and: the entry owns its cause tag)
    fexc_t       x;
    logic        fcmp;                  // an FCMP pass (flags reach SR at FS)
    logic [4:0]  cz;
    logic [7:0]  isn;
    logic [1:0]  mfl;
    logic        tp;                    // a trap pass: at FS the trap's cause (x, or E alone) through the trap write
    logic        e;                     // ... its E
  } fp_t;
  f1_t f1;
  /* verilator lint_off UNUSEDSIGNAL */          // fs.fcmp: FCMP's flag interlock is taken at F2 (flag_busy)
  fp_t f2, fs;
  /* verilator lint_on UNUSEDSIGNAL */

  // FPSCR cause ownership.  Every instruction that writes the cause field takes a tag at issue from a counter of such
  // issues; the divider, the double sequencer and the vector unit finish out of program order with the FE pipe, so a
  // completion rewrites the cause only while no younger cause writer has issued (its tag is still the counter's
  // value) -- the field then holds the youngest instruction's cause, as in program order.  The flags always OR.
  // 5 bits: at most one cause writer issues per cycle and none is in flight for 32 cycles (FDIV DR: 25).
  // A kill takes the counter back by the number of killed cause writers (cz_eff: the value every compare of the kill
  // cycle uses, so an older completion in that cycle already owns the field again).
  logic [4:0] cz_ctr, cz_eff, cz_nk;
  logic       cz_is [2];                // this slot issues a cause writer (a trap pass too: it writes the trap's cause)
  always_comb for (int s = 0; s < 2; s++)
    cz_is[s] = di_v[s] && di_c[s].fe && di_d[s].form != F_FRCHG && di_d[s].form != F_FSCHG;
  wire [4:0] cz_new = cz_ctr + 5'd1;    // the tag of this cycle's cause writer
  // the youngest cause writer that has completed (its tag and the cause field it wrote or would have written)
  logic [4:0] czs_tag;
  logic [5:0] czs_c;
  logic       czs_v;
  // tag a is younger than or as young as tag b (tags count up from cz_ctr's past values)
  function automatic logic cz_yeq(input logic [4:0] a, input logic [4:0] b);
    return 5'(cz_ctr - a) <= 5'(cz_ctr - b);
  endfunction
  // one cause write: the field (a rewrite, or an OR for FTRV's later rows) if the writer is the youngest, the flags,
  // and the record of the youngest completed writer
  task automatic cz_event(inout logic [31:0] f, inout logic [4:0] st, inout logic [5:0] sc, inout logic sv,
                          input logic [4:0] t, input logic [5:0] c, input logic head);
    if (t == cz_eff) begin
      if (head) f[17:12] = c; else f[16:12] = f[16:12] | c[4:0];
    end
    if (!c[5]) f[6:2] = f[6:2] | c[4:0];                  // (an E trap sets no flag)
    if (head) begin
      if (!sv || cz_yeq(t, st)) begin st = t; sc = c; sv = 1'b1; end
    end else if (sv && st == t) sc[4:0] = sc[4:0] | c[4:0];
  endtask

  // double-precision sequencer
  // [0] the operation in its F1 passes, [1] the previous one whose result words are still in F2 / FS when the next
  // double issues (a double issues once F1 is free, at age L1 of the previous one; that one retires at L1 + 2)
  logic        db_on  [2];
  logic [4:0]  db_age [2];              // cycles since issue (1 = first F1 pass)
  logic [2:0]  db_l1  [2];
  logic [31:0] db_dst [2], db_src [2];  // destination pair / source words
  logic        db_fpul[2];              // result goes to FPUL
  logic [63:0] db_res [2];
  fexc_t       db_x   [2];
  logic        db_e   [2];              // E (DN = 0 denormal operand) of the operand check
  logic        db_div [2];              // FDIV / FSQRT DR: the divider writes the result
  logic [4:0]  db_cz  [2];
  logic [7:0]  db_isn [2];
  logic        db_lane[2];
  // divider: two destination records (the operation in the recurrence and the previous one, whose result passes may
  // still be pending or in flight), fixed slots, dv_old = the older when both are on
  logic        dv_on  [2];
  logic        dv_dbl [2];
  logic [31:0] dv_dst [2];
  logic [4:0]  dv_cz  [2];
  logic [7:0]  dv_isn [2];
  logic        dv_st  [2];              // the operation has started in the recurrence (single: at F1; DR: at D)
  logic        dv_pst [2];              // its result pass request is posted (v1_fdiv rq)
  logic        dv_cap [2];              // its result is captured (v1_fdiv done)
  logic [63:0] dv_res [2];
  fexc_t       dv_xr  [2];
  logic        dv_te  [2];              // a trap pass (destination 0) whose operand check is E: its cause is E alone
                                        // (single: set at the start in F1; DR: at D)
  logic [2:0]  ps_lo  [2], ps_hi [2];   // result pass of the lower / single (upper) word: 0 not granted, 1 F1, 2 F2,
                                        // 3 FS (written this cycle), 4 written
  logic        dv_old;
  logic        dv_start, dv_sqrt, dv_sdbl, dv_busy, dv_last, dv_rq, dv_done;
  logic [7:0]  dv_sisn;
  logic [63:0] dv_a, dv_b, dv_r;
  fexc_t       dv_x;
  // vector unit: in-flight records (FTRV is four FIPR passes).  An FTRV's four records live 6 cycles and the array
  // takes a new operation every p cycles, so at most 10 are live: FTRV at x - 6, FIPR at x - 2 and x - 1, FTRV at x
  // (8 dropped the last row's record of that sequence: its word was never written -- WI-CORE-F's random programs)
  localparam int VN = 12;
  typedef struct packed {
    logic        v;
    logic [1:0]  op;                    // 0 FIPR, 1 FSCA, 2 FSRRA, 3 FTRV row
    logic [4:0]  age;
    logic [4:0]  fs;                    // FS of this result
    logic [4:0]  last;                  // last FS of the instruction (WAW release = last - 2)
    logic [4:0]  p;                     // array passes of the instruction
    logic [31:0] dst;                   // words this record writes (0: a trap pass / a trapped op: cause only)
    logic [31:0] idst;                  // the instruction's whole destination
    logic        head;                  // first record of the instruction (holds the array / F1 / FPSCR tags)
    logic        ftrv;
    logic [1:0]  grp;
    logic        sec;                   // FSCA cosine record
    logic [4:0]  cz;
    logic [7:0]  isn;
    logic        lane;
    logic [1:0]  tv;                    // {V, Z} of the operand check (FIPR V, FSRRA V / Z), for the F1 trap
    logic        te;                    // E of the operand check (FSRRA with DN = 0), for the F1 trap
  } vr_t;
  vr_t vq [VN];
  // multiplier: in-flight writers of MACH / MACL
  localparam int MN = 4;
  typedef struct packed {
    logic        v;
    logic [2:0]  eta;                   // cycles until FS
    mul_op_e     op;
    logic        def;                   // CLRMAC / LDS MACx
    logic        defh;                  // LDS MACH (else MACL / CLRMAC)
    logic        clr;
    logic [31:0] a, b;
    logic        av, bv;
    logic        s;                     // SR.S of a MAC, taken at its first micro-op's EX (ex_s)
    logic [7:0]  isn;
  } mr_t;
  mr_t mq [MN];
  // FCMP DR: its tag's owner between micro-op 0 (the issue) and the compare pass (micro-op 1)
  logic        fcd_v;
  logic [7:0]  fcd_isn;
  logic        fcd_lane;

  // ============================================================================================================
  // write-through register reads.  (A killed record is never at its write stage in its kill cycle: a take kills what
  // issued at or after the excepting instruction, at most 3 cycles before, while the earliest write is at FS, c + 3,
  // and every hold holds both; `kill_wr_err` below checks it for the testbenches.)
  logic [31:0] fr_w [32];
  logic [31:0] fpul_w, fpscr_w, mach_w, macl_w;
  logic [31:0] db_wr_m;  logic db_wr_u;  logic [63:0] db_wr_v;
  logic [31:0] dv_wr_m;  logic [31:0] dv_wr_v;
  logic [31:0] vc_wr_m;  logic [31:0] vc_wr_v;
  logic        mc_we;    logic [63:0] mc_v;
  always_comb begin
    for (int w = 0; w < 32; w++) fr_w[w] = fr[w];
    fpul_w = fpul; fpscr_w = fpscr; mach_w = mach; macl_w = macl;
    // LS pipe (S)
    for (int l = 0; l < 2; l++) if (sl[l].v) begin
      for (int w = 0; w < 32; w++) if (sl[l].fwm[w]) fr_w[w] = w[0] ? sl[l].fwo : sl[l].fwe;
      if (sl[l].uw) fpul_w = sl[l].uv;
      if (sl[l].fscw) fpscr_w = sl[l].fscv;
    end
    // FE pipe (FS)
    if (fs.v) begin
      for (int w = 0; w < 32; w++) if (fs.wm[w]) fr_w[w] = w[0] ? fs.wo : fs.we;
      if (fs.uw) fpul_w = fs.uv;
    end
    // double sequencer, divider result passes, vector unit results of this cycle
    for (int w = 0; w < 32; w++) begin
      if (db_wr_m[w]) fr_w[w] = w[0] ? db_wr_v[31:0] : db_wr_v[63:32];
      if (dv_wr_m[w]) fr_w[w] = dv_wr_v;
      if (vc_wr_m[w]) fr_w[w] = vc_wr_v;
    end
    if (db_wr_u) fpul_w = db_wr_v[31:0];
    if (mc_we) begin mach_w = mc_v[63:32]; macl_w = mc_v[31:0]; end
  end
  always_comb for (int w = 0; w < 32; w++) fr_now[w] = fr_w[w];
  assign fpul_now = fpul_w;
  // FPSCR as the CPU side reads it (STS FPSCR,Rn in MA, STS.L FPSCR in EX): the decoder's FR / SZ flip of a younger
  // FRCHG / FSCHG is not architectural until its pass has left F1.  (Its only reader that can see it: STS FPSCR,Rn
  // issued one cycle before the FRCHG, whose MA is the FRCHG's F1 cycle; an older FRCHG's pass is past F1 by then.)
  // [program order, tb/sh4_iss.h; the timings2 sweep's STS FPSCR ; FRCHG loop (record 115656) never stores the value]
  assign fpscr_now = f1.v ? fpscr_w ^ (32'(f1.mfl) << 20) : fpscr_w;
  assign mach_now = mach_w;
  assign macl_now = macl_w;

  // ============================================================================================================
  // F1: operands through the bypass from the LS pipe, then the arithmetic
  function automatic logic [31:0] f1_byp(input logic [4:0] w, input logic [31:0] v, input logic lane1);
    logic [31:0] x;
    x = v;
    for (int l = 0; l < 2; l++) if (sl[l].v && sl[l].fwm[w]) x = w[0] ? sl[l].fwo : sl[l].fwe;
    for (int l = 0; l < 2; l++) if (ma[l].v && ma[l].fwm[w] && ma[l].fk != FP_LOAD) x = w[0] ? ma[l].fwo : ma[l].fwe;
    if (lane1 && ex[0].v && ex_res[0].fwm[w] && ex_res[0].fk != FP_LOAD) x = w[0] ? ex_res[0].fwo : ex_res[0].fwe;
    return x;
  endfunction
  function automatic logic [31:0] f1_bypu(input logic [31:0] v, input logic lane1);
    logic [31:0] x;
    x = v;
    for (int l = 0; l < 2; l++) if (sl[l].v && sl[l].uw) x = sl[l].uv;
    for (int l = 0; l < 2; l++) if (ma[l].v && ma[l].uw && ma[l].fk != FP_LOAD) x = ma[l].uv;
    if (lane1 && ex[0].v && ex_res[0].uw && ex_res[0].fk != FP_LOAD) x = ex_res[0].uv;
    return x;
  endfunction
  // operand class for FCMP DR's D-stage V check (the other checks are v1_fp_pkg's: the functions' e, fdiv_spec)
  /* verilator lint_off UNUSEDSIGNAL */          // the class test ignores the sign bit
  function automatic logic nan_d(input logic [63:0] a); return a[62:52] == 11'h7FF && a[51:0] != 52'd0; endfunction
  /* verilator lint_on UNUSEDSIGNAL */

  fp_t   f1_out;
  logic  f1_t_we, f1_t_val;
  logic  dv_s_start;                    // single FDIV / FSQRT start (in F1)
  logic [31:0] dv_s_a, dv_s_b;
  logic  f1_ck;                         // the F1 entry's form has operand checks (V / Z / E)
  logic [2:0] f1_vze;                   // {V, Z, E} of the F1 operands
  fexc_t f1_cx;                         // the F1 operation's computed cause (a taken F1 trap writes it): the value
                                        // function's, FDIV / FSQRT's special case (fdiv_spec: a V / Z trap is one)
  always_comb begin
    logic [31:0] n, m, z, ul;
    fs_res_t r;
    fcmp_t cm;
    /* verilator lint_off UNUSEDSIGNAL */        // dsp: the operand check reads the special case's causes only
    dspec_t dsp;
    /* verilator lint_on UNUSEDSIGNAL */
    f1_out = '0; f1_t_we = 1'b0; f1_t_val = 1'b0; dv_s_start = 1'b0; dv_s_a = '0; dv_s_b = '0;
    f1_ck = 1'b0; f1_vze = '0; f1_cx = '0;
    n = f1_byp(f1.rn, f1.n, f1.lane);
    m = f1_byp(f1.rm, f1.m, f1.lane);
    z = f1_byp({f1.md[2], 4'd0}, f1.z, f1.lane);
    ul = f1_bypu(f1.ul, f1.lane);
    r = '0;
    if (f1.v) begin
      f1_out.v = 1'b1;
      f1_out.cw = 1'b1;
      f1_out.cz = f1.cz;
      f1_out.isn = f1.isn;
      f1_out.mfl = f1.mfl;
      unique case (f1.form)
        F_FADD_FRM_FRN: begin r = fadd_s(n, m, rz, dn0); f1_out.wm = 32'h1 << f1.rn; end
        F_FSUB_FRM_FRN: begin r = fadd_s(n, {~m[31], m[30:0]}, rz, dn0); f1_out.wm = 32'h1 << f1.rn; end
        F_FMUL_FRM_FRN: begin r = fmul_s(n, m, rz, dn0); f1_out.wm = 32'h1 << f1.rn; end
        F_FMAC_FR0_FRM_FRN: begin r = fmac_s(z, m, n, rz, dn0); f1_out.wm = 32'h1 << f1.rn; end
        F_FLOAT_FPUL_FRN: begin r = float_s(ul, rz); f1_out.wm = 32'h1 << f1.rn; end
        F_FTRC_FRM_FPUL: begin r = ftrc_s(n); f1_out.uw = 1'b1; f1_out.uv = r.r; end   // FRm is op[11:8]
        F_FCMP_EQ_FRM_FRN, F_FCMP_GT_FRM_FRN: begin
          cm = fcmp_s(n, m, dn0);
          f1_t_we = !f1.tp;
          f1_t_val = f1.form == F_FCMP_EQ_FRM_FRN ? cm.eq : cm.gt;
          r.x = '0; r.x.v = f1.form == F_FCMP_EQ_FRM_FRN ? cm.v_eq : cm.v_gt;
          f1_out.fcmp = 1'b1;
        end
        F_FCMP_EQ_DRM_DRN, F_FCMP_GT_DRM_DRN: if (!f1.tp) begin
          logic [63:0] dn, dm;
          dn = {fr_w[{f1.md[2], f1.op[11:9], 1'b0}], fr_w[{f1.md[2], f1.op[11:9], 1'b1}]};
          dm = {fr_w[{f1.md[2], f1.op[7:5], 1'b0}], fr_w[{f1.md[2], f1.op[7:5], 1'b1}]};
          cm = fcmp_d(dn, dm, dn0);
          f1_t_we = 1'b1;
          f1_t_val = f1.form == F_FCMP_EQ_DRM_DRN ? cm.eq : cm.gt;
          r.x = '0; r.x.v = f1.form == F_FCMP_EQ_DRM_DRN ? cm.v_eq : cm.v_gt;
          f1_out.fcmp = 1'b1;
        end
        F_FDIV_FRM_FRN, F_FSQRT_FRN: begin
          // the divider writes the result and the cause; a trap pass runs the recurrence as well (SOC.md 4.3.8: its
          // cause is the quotient's), its record has no destination
          dv_s_start = 1'b1; dv_s_a = n; dv_s_b = m;
          f1_out.v = 1'b0;
        end
        F_FRCHG, F_FSCHG: f1_out.cw = 1'b0;   // no arithmetic: the pass only orders FPSCR
        default: ;
      endcase
      f1_out.x = r.x;
      f1_out.we = r.r; f1_out.wo = r.r;
      // the operand checks (exceptions-intc 3.8; pm 10 possible exceptions): V from the arithmetic's own invalid-
      // operand rule, V / Z of FDIV / FSQRT from the divider's special cases (fdiv_spec), E (DN = 0) as the console
      // raises it [HW tools/hw/fpdn]: the value function's e, fdiv_spec's e; never for FTRC / FCMP
      dsp = fdiv_spec({32'd0, n}, {32'd0, m}, f1.form == F_FSQRT_FRN, 1'b0, dn0);
      unique case (f1.form)
        F_FADD_FRM_FRN, F_FSUB_FRM_FRN, F_FMUL_FRM_FRN, F_FMAC_FR0_FRM_FRN: begin
          f1_ck = 1'b1; f1_vze = {r.x.v, 1'b0, r.e}; f1_cx = r.x;
        end
        F_FTRC_FRM_FPUL, F_FCMP_EQ_FRM_FRN, F_FCMP_GT_FRM_FRN: begin f1_ck = 1'b1; f1_vze = {r.x.v, 2'b00}; f1_cx = r.x; end
        F_FDIV_FRM_FRN, F_FSQRT_FRN: begin f1_ck = 1'b1; f1_vze = {dsp.x.v, dsp.x.z, dsp.e}; f1_cx = dsp.x; end
        default: ;
      endcase
      if (!dn0) f1_vze[0] = 1'b0;
      // a trap pass writes nothing but the trap's cause, at FS (t + 3, with EXC0 at S: every older instruction has
      // passed its commit point by then, so an older fault found later -- a lane-0 partner's at its MA -- kills it on
      // the way; a cause written at F1 could not be taken back): the operation's computed cause {V, Z, O, U, I} (r.x on
      // the bypassed operands; a double's / FCMP DR's from D, tpx), or E alone.  (FDIV / FSQRT: the divider's record.)
      if (f1.tp && f1.form != F_FDIV_FRM_FRN && f1.form != F_FSQRT_FRN) begin
        f1_out = '0;
        f1_out.v = 1'b1; f1_out.cw = 1'b1; f1_out.tp = 1'b1; f1_out.cz = f1.cz; f1_out.isn = f1.isn;
        if (f1.tpv) begin f1_out.e = f1.tpx[5]; f1_out.x = f1.tpx[4:0]; end
        else begin f1_out.e = f1_vze[0]; f1_out.x = r.x; end
      end
    end
  end

  // ============================================================================================================
  // the F1 operand trap (SOC.md 4.3.2): where the instruction first is in F1, the cycle after its issue
  logic       f1_trap, db_trap, vq_trap;
  logic       db_tk;                    // the record index of the trapping double
  always_comb begin
    f1_trap = f1.v && !f1.tp && !f1.fcd && f1_ck &&
              (f1_vze[0] || (en_v && f1_vze[2]) || (en_z && f1_vze[1]));
    db_trap = 1'b0; db_tk = 1'b0;
    for (int k = 0; k < 2; k++)
      if (db_on[k] && db_age[k] == 5'd1 && (db_e[k] || (en_v && db_x[k].v) || (en_z && db_x[k].z))) begin
        db_trap = 1'b1; db_tk = k[0];
      end
    vq_trap = 1'b0;
    for (int i = 0; i < VN; i++)
      if (vq[i].v && vq[i].head && vq[i].age == 5'd1 && ((en_v && vq[i].tv[1]) || (en_z && vq[i].tv[0]) || vq[i].te))
        vq_trap = 1'b1;
    trap_f1 = f1_trap || db_trap || vq_trap;
    trap_f1_lane = f1.lane; trap_f1_isn = f1.isn;
    if (db_trap) begin trap_f1_lane = db_lane[db_tk]; trap_f1_isn = db_isn[db_tk]; end
    for (int i = 0; i < VN; i++)
      if (vq_trap && vq[i].v && vq[i].head && vq[i].age == 5'd1) begin
        trap_f1_lane = vq[i].lane; trap_f1_isn = vq[i].isn;
      end
  end
  // the core took this trap: its kill names the trapping instruction, excluding it (kill > K)
  assign ftk = trap_f1 && kill_v && !kill_incl && kill_isn == trap_f1_isn;
  // FCMP's T: never for a trapping FCMP, nor a killed one (SOC.md 1.3.2)
  assign t_we = f1_t_we && !f1_trap && !kl(f1.isn);
  assign t_val = f1_t_val;
  assign t_lane = f1.lane;
  // the trap's cause write (SOC.md 4.3.8: the computed cause, flags |= it; E: E alone, flags kept): a taken F1 trap of a
  // scalar op at the end of its F1 cycle (the take: nothing older can fault any more, SOC.md 4.3.2), a trap pass at
  // its FS (below); a divider trap pass writes through its result pass (dv_cw)
  logic       tcw_v;
  logic [5:0] tcw_c;                    // {E, V, Z, O, U, I}
  logic [4:0] tcw_cz;
  always_comb begin
    tcw_v = 1'b0; tcw_c = '0; tcw_cz = '0;
    if (ftk && f1_trap) begin tcw_v = 1'b1; tcw_c = f1_vze[0] ? 6'b100000 : {1'b0, f1_cx}; tcw_cz = f1.cz; end
    else if (ftk && db_trap) begin
      tcw_v = 1'b1; tcw_c = db_e[db_tk] ? 6'b100000 : {1'b0, db_x[db_tk]}; tcw_cz = db_cz[db_tk];
    end
    if (fs.v && fs.tp) begin tcw_v = 1'b1; tcw_c = fs.e ? 6'b100000 : {1'b0, fs.x}; tcw_cz = fs.cz; end
  end

  // ============================================================================================================
  // this cycle's writes of the double sequencer, the divider, the vector unit and the multiplier
  logic        vres_valid;
  /* verilator lint_off UNUSEDSIGNAL */          // vres_second: the vq records know which word is due
  logic        vres_second;
  /* verilator lint_on UNUSEDSIGNAL */
  logic [31:0] vres;
  logic [4:0]  vres_x;                  // the result word's cause {V, Z, O, U, I}
  logic        vres_e;                  // ... its E (FSRRA of a DN = 0 positive denormal)
  logic        vc_live;                 // a live record is due for this result
  logic        vc_head;                 // it is the instruction's first result word (FTRV: row 0)
  logic [4:0]  vc_cz;
  logic        dv_cw;                   // a divider pass writes its cause this cycle (the lower / single word's FS)
  logic [4:0]  dv_wr_cz;
  fexc_t       dv_wr_x;
  logic        dv_wr_te;                // ... a trap pass's E: its cause is E alone
  // the writers' ISNs (the commit-log records) and which of MACH / MACL the multiplier write is architecturally
  logic [7:0]  db_wr_isn, dv_wr_isn, vc_isn, mc_isn;
  logic        mc_h, mc_l;
  always_comb begin
    logic [63:0] q;
    fexc_t qx;
    q = '0; qx = '0;
    db_wr_m = '0; db_wr_u = 1'b0; db_wr_v = '0; db_wr_isn = '0;
    for (int k = 0; k < 2; k++) if (db_on[k] && !db_div[k]) begin
      if (db_fpul[k]) begin
        if (db_age[k] == 5'(db_l1[k]) + 5'd2) begin db_wr_u = 1'b1; db_wr_v = db_res[k]; db_wr_isn = db_isn[k]; end
      end else begin
        if (db_age[k] == 5'(db_l1[k]) + 5'd1) begin    // lower word: FR(2n+1)
          db_wr_m = db_dst[k] & 32'hAAAA_AAAA; db_wr_v = db_res[k]; db_wr_isn = db_isn[k];
        end
        if (db_age[k] == 5'(db_l1[k]) + 5'd2) begin    // upper word: FR(2n)
          db_wr_m = db_dst[k] & 32'h5555_5555; db_wr_v = db_res[k]; db_wr_isn = db_isn[k];
        end
      end
    end
    // the divider's result passes at FS: the quotient captured at `done`, or `done` itself this cycle
    dv_wr_m = '0; dv_wr_v = '0; dv_cw = 1'b0; dv_wr_cz = '0; dv_wr_x = '0; dv_wr_te = 1'b0; dv_wr_isn = '0;
    for (int k = 0; k < 2; k++) if (dv_on[k]) begin
      q = dv_cap[k] ? dv_res[k] : dv_r;
      qx = dv_cap[k] ? dv_xr[k] : dv_x;
      if (ps_lo[k] == 3'd3) begin
        dv_wr_m = dv_dbl[k] ? dv_dst[k] & 32'hAAAA_AAAA : dv_dst[k]; dv_wr_v = q[31:0]; dv_wr_isn = dv_isn[k];
        dv_cw = 1'b1; dv_wr_cz = dv_cz[k]; dv_wr_x = qx; dv_wr_te = dv_te[k];
      end
      if (dv_dbl[k] && ps_hi[k] == 3'd3) begin
        dv_wr_m = dv_dst[k] & 32'h5555_5555; dv_wr_v = q[63:32]; dv_wr_isn = dv_isn[k];
      end
    end
    vc_wr_m = '0; vc_wr_v = vres; vc_head = 1'b0; vc_cz = '0; vc_live = 1'b0; vc_isn = '0;
    if (vres_valid) begin
      // the oldest record whose result is due
      for (int i = VN - 1; i >= 0; i--)
        if (vq[i].v && vq[i].age == vq[i].fs) begin
          vc_wr_m = vq[i].dst; vc_head = vq[i].head; vc_cz = vq[i].cz; vc_live = 1'b1; vc_isn = vq[i].isn;
        end
    end
    mc_we = 1'b0; mc_v = {mach, macl}; mc_isn = '0; mc_h = 1'b0; mc_l = 1'b0;
    for (int i = 0; i < MN; i++) begin
      if (mq[i].v && mq[i].eta == 3'd0) begin
        mc_we = 1'b1; mc_isn = mq[i].isn;
        if (mq[i].clr) mc_v = 64'd0;
        else if (mq[i].def) mc_v = mq[i].defh ? {mq[i].a, macl} : {mach, mq[i].a};
        else mc_v = v1_mul(mq[i].op, mq[i].a, mq[i].b, mq[i].s, {mach, macl});
        // the architectural writes: MUL.L / MULS.W / MULU.W and LDS MACL write MACL, LDS MACH MACH, the rest both
        mc_h = mq[i].clr || (mq[i].def ? mq[i].defh : !(mq[i].op == M_MULL || mq[i].op == M_MULSW || mq[i].op == M_MULUW));
        mc_l = mq[i].clr || !(mq[i].def && mq[i].defh);
      end
    end
  end

  // ============================================================================================================
  // divider (the recurrence under hold_div) and its result-pass bookkeeping
  v1_fdiv u_div (
    .clk, .rst, .start(dv_start), .hold(hold_div), .sqrt(dv_sqrt), .dbl(dv_sdbl), .rz, .dn0,
    .a(dv_a), .b(dv_b), .isn(dv_sisn), .kill_v, .kill_isn, .kill_incl(kill_incl || ftk),
    .busy(dv_busy), .last(dv_last), .rq(dv_rq), .done(dv_done), .r(dv_r), .x(dv_x)
  );
  // the running record (started, request not posted: at most one) posts its request at the divider's rq step; the
  // result goes to the oldest started record not captured yet (the recurrence finishes in order)
  logic       dv_post, dv_post_k, dv_cap_v, dv_cap_k;
  logic       dv_req [2];               // record k has a pass request pending this cycle
  logic       dv_gnt, dv_gk, dv_ghi;    // a pass is granted this cycle: record, upper word
  logic       steal;                    // a pass request takes this cycle's FE dispatch slot
  logic       mahq;                     // last cycle was held by the MA wait alone (hold_pipe without hold_div)
  always_comb begin
    dv_post = 1'b0; dv_post_k = 1'b0; dv_cap_v = 1'b0; dv_cap_k = 1'b0;
    for (int k = 0; k < 2; k++)
      if (dv_on[k] && dv_st[k] && !dv_pst[k] && !kl(dv_isn[k]) && dv_rq && !hold_div) begin
        dv_post = 1'b1; dv_post_k = k[0];
      end
    for (int j = 0; j < 2; j++) begin
      logic k;
      k = j == 0 ? dv_old : !dv_old;           // the older first
      if (!dv_cap_v && dv_on[k] && dv_st[k] && !dv_cap[k] && dv_done && !hold_div) begin dv_cap_v = 1'b1; dv_cap_k = k; end
    end
    for (int k = 0; k < 2; k++)
      dv_req[k] = dv_on[k] && !kl(dv_isn[k]) && (dv_pst[k] || (dv_post && dv_post_k == k[0])) &&
                  (ps_lo[k] == 3'd0 || (dv_dbl[k] && ps_lo[k] != 3'd0 && ps_hi[k] == 3'd0));
    steal = dv_req[0] || dv_req[1];
    dv_gk = dv_req[dv_old] ? dv_old : !dv_old;
    dv_ghi = ps_lo[dv_gk] != 3'd0;
    // the grant: the FE pipe moves this cycle, and the dispatch arbiter's registered view of the MA wait is clear --
    // the pass is granted the cycle after a `mah` hold ends, not in the ack cycle, like the fetch's view of the MA
    // unit's hold (WI-CORE-B, [HW ldCCN / stP4]) [HW ci3: with the uncached read's hold of 14 / 15 cycles by Bclk
    // phase, which the six P2 rows without an FPU op fix (empty 18, nop8 22, nop16 26, ldcsr 24, stcsr 20, use 20),
    // p2_fdiv comes out 24 (22 if granted in the ack cycle) and p2_fdiv2 20]
    dv_gnt = steal && !hold_pipe && !mahq;
  end
  always_ff @(posedge clk) mahq <= !rst && hold_pipe && !hold_div;

  // vector unit
  logic        vs_start;
  fpu_vec_pkg::op_e vs_op;
  logic [31:0] vs_fva [4], vs_fvb [4], vs_fpul, vs_frn;
  logic [7:0]  vs_isn;
  /* verilator lint_off UNUSEDSIGNAL */          // vs_stx[2:0] (O / U / I): only V and Z are start-operand traps
  logic [4:0]  vs_stx;                  // the start operands' special-case cause (the operand check at issue)
  /* verilator lint_on UNUSEDSIGNAL */
  logic        vs_ste;                  // ... the FPU error E (FSRRA, DN = 0)
  /* verilator lint_off UNUSEDSIGNAL */
  logic        vs_busy;                 // (the records keep the array's occupancy)
  /* verilator lint_on UNUSEDSIGNAL */
  fpu_vec_top u_vec (
    .clk, .rst, .start(vs_start), .hold(hold_pipe), .op(vs_op), .rz, .dn0, .fva(vs_fva), .fvb(vs_fvb), .fpul(vs_fpul),
    .frn(vs_frn), .isn(vs_isn), .kill_v, .kill_isn, .kill_incl, .st_x(vs_stx), .st_e(vs_ste),
    .busy(vs_busy), .res_valid(vres_valid), .res_second(vres_second), .res(vres), .res_x(vres_x), .res_e(vres_e)
  );

  // ============================================================================================================
  // hazards of the D slots against the FPU state and the LS pipe
  //   fwhy bits: 0 FR / FPUL RAW, 1 WAW / WAR, 2 FPSCR, 3 multiplier, 4 F1 / array busy, 5 divider, 6 dispatch steal,
  //              7 FTRV group lock
  function automatic logic [31:0] pairw(input logic [31:0] m);   // widen a word mask to its DR pairs
    return m | ((m & 32'h5555_5555) << 1) | ((m & 32'hAAAA_AAAA) >> 1);
  endfunction
  logic [31:0] pend_fe, pend_rf, pend_st, pend_wr, pend_war, grp_lock;
  logic        u_pend_fe, u_pend_rf, u_pend_st, u_pend_ma, u_pend_wr;
  logic        fscr_busy, dec_lock, mul_fe_busy, mul_st_busy, f1_busy_nx, arr_busy_nx, div_busy_nx, div_busy_d;
  logic        vec_f1_busy;
  always_comb begin
    logic [31:0] m, lo, up;
    /* verilator lint_off UNUSEDSIGNAL */        // the hazard logic reads the control fields it needs
    ictl_t ec;
    /* verilator lint_on UNUSEDSIGNAL */
    m = '0; lo = '0; up = '0; ec = '0;
    pend_fe = '0; pend_rf = '0; pend_st = '0; pend_wr = '0; pend_war = '0; grp_lock = '0;
    u_pend_fe = 1'b0; u_pend_rf = 1'b0; u_pend_st = 1'b0; u_pend_ma = 1'b0; u_pend_wr = 1'b0;
    fscr_busy = 1'b0; dec_lock = 1'b0; mul_fe_busy = 1'b0; mul_st_busy = 1'b0;
    f1_busy_nx = 1'b0; arr_busy_nx = 1'b0; div_busy_nx = 1'b0; div_busy_d = 1'b0; vec_f1_busy = 1'b0;
    t_busy = 1'b0; flag_busy = 1'b0;
    // FE pipe: F1 and F2 results are not in the register file yet (no bypass out of the FE pipe)
    if (f1.v) begin
      m = f1_out.wm;
      pend_fe |= m; pend_rf |= m; pend_st |= m;           // a store at D would capture in EX = this pass's F2
      if (f1_out.uw) begin u_pend_fe = 1'b1; u_pend_rf = 1'b1; u_pend_st = 1'b1; end
      fscr_busy = 1'b1;
      if (f1.form == F_FCMP_EQ_FRM_FRN || f1.form == F_FCMP_GT_FRM_FRN || f1.form == F_FCMP_EQ_DRM_DRN ||
          f1.form == F_FCMP_GT_DRM_DRN) begin t_busy = 1'b1; flag_busy = 1'b1; end
    end
    if (f2.v) begin
      pend_fe |= f2.wm; pend_rf |= f2.wm;
      if (f2.uw) begin u_pend_fe = 1'b1; u_pend_rf = 1'b1; end
      fscr_busy = 1'b1;
      if (f2.fcmp) flag_busy = 1'b1;
    end
    if (fs.v) fscr_busy = 1'b1;                         // the cause bits commit one cycle after FS
    if (di_fcmpd_pending) begin t_busy = 1'b1; flag_busy = 1'b1; fscr_busy = 1'b1; end
    // double sequencer: its pair in F1 (and the words not yet at FS)
    for (int k = 0; k < 2; k++) if (db_on[k]) begin
      lo = db_dst[k] & (32'h5555_5555 << 1);
      up = db_dst[k] & 32'h5555_5555;
      if (!db_fpul[k]) begin
        if (db_age[k] <= 5'(db_l1[k])) begin pend_fe |= lo; pend_rf |= lo; end
        if (db_age[k] <= 5'(db_l1[k]) + 5'd1) begin pend_fe |= up; pend_rf |= up; end
        if (db_age[k] <= 5'(db_l1[k])) pend_st |= db_dst[k];       // late readers: the pair is in F1
        if (db_age[k] < 5'(db_l1[k])) pend_wr |= db_dst[k];        // later writers: until the last pass
      end else begin
        if (db_age[k] <= 5'(db_l1[k]) + 5'd1) begin u_pend_fe = 1'b1; u_pend_rf = 1'b1; end
        if (db_age[k] <= 5'(db_l1[k])) begin u_pend_st = 1'b1; u_pend_ma = 1'b1; end
        if (db_age[k] < 5'(db_l1[k])) u_pend_wr = 1'b1;
      end
      if (db_age[k] == 5'd1 && db_src[k] != 0) pend_war |= db_src[k];   // re-read of the sources in the first F1 pass
      if (db_age[k] < 5'(db_l1[k])) begin f1_busy_nx = 1'b1; arr_busy_nx = 1'b1; end
      if (db_age[k] <= 5'(db_l1[k]) + 5'd2) fscr_busy = 1'b1;
    end
    // divider: from its records and their result passes -- a reader of the destination waits until the word's pass is
    // at FS (the write-through), a late reader (FP store, STS FPUL) and a later writer until the final pass has left
    // F1 (FS - 1); FPSCR readers until the final pass has written
    for (int k = 0; k < 2; k++) if (dv_on[k]) begin
      lo = dv_dbl[k] ? dv_dst[k] & (32'h5555_5555 << 1) : dv_dst[k];
      if (ps_lo[k] < 3'd3) begin pend_fe |= lo; pend_rf |= lo; end
      if (dv_dbl[k] && ps_hi[k] < 3'd3) begin pend_fe |= dv_dst[k] & 32'h5555_5555; pend_rf |= dv_dst[k] & 32'h5555_5555; end
      if ((dv_dbl[k] ? ps_hi[k] : ps_lo[k]) < 3'd2) begin pend_st |= dv_dst[k]; pend_wr |= dv_dst[k]; end
      fscr_busy = 1'b1;
    end
    // the next divide: the recurrence must be free when it starts (a single at its F1 next cycle, a double at D), no
    // single start may be waiting in F1, and a destination record must be free
    div_busy_nx = (f1.v && (f1.form == F_FDIV_FRM_FRN || f1.form == F_FSQRT_FRN)) ||
                  (dv_busy && !dv_last) || (dv_on[0] && dv_on[1]);
    div_busy_d = div_busy_nx || dv_busy;
    // vector unit
    for (int i = 0; i < VN; i++) if (vq[i].v) begin
      if (vq[i].age < vq[i].fs) begin pend_fe |= vq[i].dst; pend_rf |= vq[i].dst; end
      if (vq[i].age < vq[i].fs - 5'd1) pend_st |= vq[i].dst;
      if (vq[i].age < vq[i].last - 5'd2) pend_wr |= vq[i].ftrv ? vq[i].idst & ftrv_fe_mask(vq[i].idst) : vq[i].idst;
      if (vq[i].head) begin
        if (vq[i].age < vq[i].p) arr_busy_nx = 1'b1;           // the array, one pass per cycle
        if (vq[i].age < vq[i].p + 5'd1) vec_f1_busy = 1'b1;    // the passes' adder slots in F1
        if (vq[i].age <= vq[i].last) fscr_busy = 1'b1;
        if (vq[i].ftrv && vq[i].age < 5'd5) grp_lock |= grp_mask(vq[i].grp);
      end
    end
    // multiplier: stages that hold the FE issue port / have not reached FS for STS
    for (int i = 0; i < MN; i++) if (mq[i].v) begin
      if (mq[i].eta >= 3'd3) mul_fe_busy = 1'b1;
      if (mq[i].eta >= 3'd2) mul_st_busy = 1'b1;
    end
    for (int l = 0; l < 2; l++) if (ex[l].v && ex[l].step == 3'd0) begin
      ec = v1_ictl(ex[l].form);
      if (ec.mul || ec.macdef) begin mul_fe_busy = 1'b1; mul_st_busy = 1'b1; end
    end
    // LS pipe: FP loads not yet at the end of MA; anything not yet written at S for RF-only readers
    for (int l = 0; l < 2; l++) begin
      if (ex[l].v) begin
        if (ex[l].fk == FP_LOAD) begin pend_fe |= ex[l].fwm; pend_st |= ex[l].fwm; if (ex[l].uw) begin
          u_pend_fe = 1'b1; u_pend_st = 1'b1; u_pend_ma = 1'b1; end end
        pend_rf |= ex[l].fwm; if (ex[l].uw) u_pend_rf = 1'b1;
        if (ex[l].fscw) dec_lock = 1'b1;
      end
      if (ma[l].v) begin
        pend_rf |= ma[l].fwm; if (ma[l].uw) u_pend_rf = 1'b1;
        if (ma[l].fscw) dec_lock = 1'b1;
      end
      if (sl[l].v && sl[l].fscw) dec_lock = 1'b1;       // the decoder's mode is loaded from FPSCR at the end of S
    end
    f1_busy_nx = f1_busy_nx || vec_f1_busy;
  end
  function automatic logic [31:0] grp_mask(input logic [1:0] g);  // FV group in both banks (bank bit not compared)
    return (32'hf << {g, 2'b00}) | (32'hf << {1'b1, g, 2'b00});
  endfunction
  function automatic logic [31:0] ftrv_fe_mask(input logic [31:0] idst);   // FE writers compare DR(4n) only
    logic [31:0] m;
    m = '0;
    for (int g = 0; g < 8; g++) if (idst[4 * g]) m |= 32'h3 << (4 * g);
    return m;
  endfunction
  logic       di_fcmpd_pending;

  // per-slot hazards
  always_comb begin
    for (int s = 0; s < 2; s++) begin
      logic [31:0] rd, rf_, st, wr, acc;
      logic ur_d, ur_rf, ur_st, ur_ma, uw_;
      /* verilator lint_off UNUSEDSIGNAL */        // the hazard logic reads the control fields it needs
      ictl_t cc;
      /* verilator lint_on UNUSEDSIGNAL */
      cc = s_c[s];
      rd  = cc.frs == FR_D  ? s_d[s].fs : 32'd0;
      rf_ = cc.frs == FR_RF ? s_d[s].fs : 32'd0;
      st  = s_d[s].fd;
      wr  = s_d[s].fw;
      acc = s_d[s].fs | s_d[s].fd | s_d[s].fw;
      ur_d = cc.urd == FR_D; ur_rf = cc.urd == FR_RF; ur_st = cc.urd == FR_EX; ur_ma = cc.urd == FR_MA;
      uw_ = cc.wr_fpul;
      fwhy[s] = '0;
      fwhy[s][0] = |(rd & pend_fe) || |(rf_ & pend_rf) || |(st & pend_st) ||
                   (ur_d && u_pend_fe) || (ur_rf && u_pend_rf) || (ur_st && u_pend_st) || (ur_ma && u_pend_ma);
      fwhy[s][1] = |(wr & (pend_wr | pend_war)) || (uw_ && u_pend_wr);
      // the FPU decoder claims the whole F group by opcode, before the mode is known: a code that is illegal in the
      // stale mode waits for the new one like any other FPU instruction
      fwhy[s][2] = ((cc.fpu_dec || s_op[s][15:12] == 4'hF) && dec_lock) || (cc.fscr_rd && fscr_busy);
      fwhy[s][3] = (cc.fe && mul_fe_busy) || (cc.mac_rd == 2'd1 && mul_fe_busy) || (cc.mac_rd == 2'd2 && mul_st_busy);
      fwhy[s][4] = (cc.fe_arith && f1_busy_nx) || ((cc.arr || cc.dbl) && arr_busy_nx);
      fwhy[s][5] = cc.div && (cc.dbl ? div_busy_d : div_busy_nx);
      fwhy[s][6] = cc.fe && steal;
      fwhy[s][7] = cc.fp_ls && |(acc & grp_lock);
      fhz[s] = s_v[s] && |fwhy[s];
    end
  end

  // the follower against the leader (same cycle)
  always_comb begin
    /* verilator lint_off UNUSEDSIGNAL */        // the hazard logic reads the control fields it needs
    ictl_t c0, c1;
    /* verilator lint_on UNUSEDSIGNAL */
    logic [31:0] w0, rd1, rf1, st1, w1;
    c0 = s_c[0]; c1 = s_c[1];
    w0 = s_d[0].fw;
    rd1 = c1.frs == FR_D ? s_d[1].fs : 32'd0;
    rf1 = c1.frs == FR_RF ? s_d[1].fs : 32'd0;
    st1 = s_d[1].fd;
    w1 = s_d[1].fw;
    fdep = 1'b0;
    // FR / FPUL results of the leader, by how they are produced
    unique case (c0.fprod)
      FP_MOV:  if (|(w0 & rf1) || |(w0 & st1) || (c0.wr_fpul && (c1.urd == FR_RF || c1.urd == FR_MA || c1.urd == FR_EX)))
                 fdep = 1'b1;                           // a move is bypassed only to D-readers
      FP_LOAD, FP_XFER: if (|(w0 & (rd1 | rf1 | st1)) || (c0.wr_fpul && c1.urd != FR_NONE)) fdep = 1'b1;
      FP_FE:   if (|(w0 & (rd1 | rf1 | st1 | w1)) || (c0.wr_fpul && (c1.urd != FR_NONE || c1.wr_fpul))) fdep = 1'b1;
      default: ;
    endcase
    // double arithmetic re-reads its source pairs in its first F1 pass (pend_war once it is in flight): a follower that
    // writes one of those words cannot go with it
    if ((s_d[0].form == F_FADD_DRM_DRN || s_d[0].form == F_FSUB_DRM_DRN || s_d[0].form == F_FMUL_DRM_DRN) &&
        |(s_d[0].fs & w1)) fdep = 1'b1;
    // FRCHG / FSCHG: the follower would decode with the old mode
    if (c0.fscr_mode && (c1.fpu_dec || s_op[1][15:12] == 4'hF)) fdep = 1'b1;
    // FTRV re-reads XMTRX in its later passes; locks its group against LS accesses
    if (s_d[0].form == F_FTRV_XMTRX_FVN) begin
      if (|(w1 & (md_dec[2] ? 32'h0000_ffff : 32'hffff_0000))) fdep = 1'b1;
      if (c1.fp_ls && |((s_d[1].fs | s_d[1].fd | s_d[1].fw) & grp_mask(s_op[0][11:10]))) fdep = 1'b1;
    end
    // FLDS FRm,FPUL; FTRV with FRm in FTRV's group: the group comparator also looks at the older slot's op[11:8]
    if (s_d[0].form == F_FLDS_FRM_FPUL && s_d[1].form == F_FTRV_XMTRX_FVN && s_op[0][11:10] == s_op[1][11:10])
      fdep = 1'b1;
  end

  // ============================================================================================================
  // the D-decided trap (SOC.md 4.3.2, 4.3.8): an enabled O / U / I the opcode can raise, FTRV's V (exceptions-intc 3.8
  // [HW]); FCMP DR's V from its register-file operands [P, see the head]
  always_comb for (int s = 0; s < 2; s++) begin
    logic [63:0] dn, dm;
    logic        fv;
    dn = {fr_w[{md_dec[2], s_op[s][11:9], 1'b0}], fr_w[{md_dec[2], s_op[s][11:9], 1'b1}]};
    dm = {fr_w[{md_dec[2], s_op[s][7:5], 1'b0}], fr_w[{md_dec[2], s_op[s][7:5], 1'b1}]};
    fv = s_d[s].form == F_FCMP_EQ_DRM_DRN ? (nan_d(dn) && dn[51]) || (nan_d(dm) && dm[51])
       : s_d[s].form == F_FCMP_GT_DRM_DRN ? nan_d(dn) || nan_d(dm) : 1'b0;
    trap_d[s] = (|(s_c[s].fpx_oui & fpscr_w[9:7])) || (s_c[s].fpx_vd && fpscr_w[11]) || (fv && fpscr_w[11]);
  end

  // the double-precision arithmetic of a dispatch (D: operands from the register file), with its operand check
  fd_res_t    dd_r [2];
  logic [2:0] dd_l1 [2];
  logic       dd_fpul [2], dd_e [2];
  always_comb for (int s = 0; s < 2; s++) begin
    logic [63:0] dn, dm;
    logic [4:0] pn, pm;
    fs_res_t rs;
    /* verilator lint_off UNUSEDSIGNAL */        // dsp: the operand check reads the special case's causes only
    dspec_t dsp;
    /* verilator lint_on UNUSEDSIGNAL */
    rs = '0;
    pn = {di_md[s][2], di_op[s][11:9], 1'b0};
    pm = {di_md[s][2], di_op[s][7:5], 1'b0};
    dn = {fr_w[pn], fr_w[pn + 1]};
    dm = {fr_w[pm], fr_w[pm + 1]};
    dd_r[s] = '0; dd_l1[s] = 3'd2; dd_fpul[s] = 1'b0; dd_e[s] = 1'b0;
    dsp = fdiv_spec(dn, dm, di_d[s].form == F_FSQRT_DRN, 1'b1, dn0);
    unique case (di_d[s].form)
      F_FADD_DRM_DRN: begin dd_r[s] = fadd_d(dn, dm, rz, dn0); dd_l1[s] = 3'd6; end
      F_FSUB_DRM_DRN: begin dd_r[s] = fadd_d(dn, {~dm[63], dm[62:0]}, rz, dn0); dd_l1[s] = 3'd6; end
      F_FMUL_DRM_DRN: begin dd_r[s] = fmul_d(dn, dm, rz, dn0); dd_l1[s] = 3'd6; end
      F_FLOAT_FPUL_DRN: dd_r[s] = float_d(fpul_w);
      F_FCNVSD_FPUL_DRN: dd_r[s] = fcnvsd(fpul_w, dn0);
      F_FTRC_DRM_FPUL: begin rs = ftrc_d(dn); dd_r[s].r = {32'd0, rs.r}; dd_r[s].x = rs.x; dd_fpul[s] = 1'b1; end
      F_FCNVDS_DRM_FPUL: begin
        rs = fcnvds(dn, rz, dn0); dd_r[s].r = {32'd0, rs.r}; dd_r[s].x = rs.x; dd_r[s].e = rs.e; dd_fpul[s] = 1'b1;
      end
      F_FDIV_DRM_DRN, F_FSQRT_DRN: begin dd_r[s].x.v = dsp.x.v; dd_r[s].x.z = dsp.x.z; dd_r[s].e = dsp.e; end
      F_FCMP_EQ_DRM_DRN: dd_r[s].x.v = (nan_d(dn) && dn[51]) || (nan_d(dm) && dm[51]);
      F_FCMP_GT_DRM_DRN: dd_r[s].x.v = nan_d(dn) || nan_d(dm);
      default: ;
    endcase
    dd_e[s] = dd_r[s].e;                             // E [HW fpdn]: never for FTRC / FLOAT / FCMP DR
  end

  // ============================================================================================================
  // the tag repair's count: killed owners of a cause tag (one per instruction), the taken trap's own excluded
  always_comb begin
    cz_nk = '0;
    if (f1.v && f1.own && kx(f1.isn)) cz_nk = cz_nk + 5'd1;
    if (f2.v && f2.cw && kx(f2.isn)) cz_nk = cz_nk + 5'd1;
    if (fs.v && fs.cw && kx(fs.isn)) cz_nk = cz_nk + 5'd1;
    for (int k = 0; k < 2; k++) begin
      if (db_on[k] && !db_div[k] && kx(db_isn[k])) cz_nk = cz_nk + 5'd1;
      if (dv_on[k] && kx(dv_isn[k])) cz_nk = cz_nk + 5'd1;
    end
    for (int i = 0; i < VN; i++) if (vq[i].v && vq[i].head && kx(vq[i].isn)) cz_nk = cz_nk + 5'd1;
    if (fcd_v && kx(fcd_isn)) cz_nk = cz_nk + 5'd1;
    cz_eff = kill_v ? cz_ctr - cz_nk : cz_ctr;
  end

  // ============================================================================================================
  // dispatch and state
  always_ff @(posedge clk) begin
    rst_q <= rst;
    if (rst) begin
      cz_ctr <= '0;
      f1 <= '0; f2 <= '0; fs <= '0; db_on[0] <= 1'b0; db_on[1] <= 1'b0; dv_on[0] <= 1'b0; dv_on[1] <= 1'b0;
      for (int i = 0; i < VN; i++) vq[i] <= '0;
      for (int i = 0; i < MN; i++) mq[i] <= '0;
      di_fcmpd_pending <= 1'b0; fcd_v <= 1'b0; czs_v <= 1'b0;
      // FPSCR's reset value in a reset's first cycle (every reset kind, the CPU-only one of SR.BL = 1 included:
      // SOC.md 2.3); the backdoor (the core-only harnesses' register load) writes after it
      if (!rst_q) fpscr <= FPSCR_RESET;
      if (dbg_we) begin
        if (dbg_addr >= 8'd64 && dbg_addr < 8'd96) fr[5'(dbg_addr - 8'd64)] <= dbg_wdata;
        else unique case (dbg_addr)
          8'd31: mach <= dbg_wdata;
          8'd32: macl <= dbg_wdata;
          8'd35: fpscr <= dbg_wdata;
          8'd36: fpul <= dbg_wdata;
          default: ;
        endcase
      end
    end else begin
      // ---- the divider's own bookkeeping, in recurrence time (hold_div): the request post and the quotient capture
      if (dv_post) dv_pst[dv_post_k] <= 1'b1;
      if (dv_cap_v) begin dv_cap[dv_cap_k] <= 1'b1; dv_res[dv_cap_k] <= dv_r; dv_xr[dv_cap_k] <= dv_x; end
      if (!hold_pipe) begin
        // ---- register writes (the write-through values)
        for (int w = 0; w < 32; w++) fr[w] <= fr_w[w];
        fpul <= fpul_w;
        mach <= mach_w; macl <= macl_w;
        begin
          logic [31:0] f;
          logic [4:0]  st;
          logic [5:0]  sc;
          logic        sv;
          f = fpscr_w; st = czs_tag; sc = czs_c; sv = czs_v;
          // an LDS FPSCR at S loads the whole field (after every older cause writer: it waits for them, fscr_busy):
          // no earlier writer's cause may come back after it
          for (int l = 0; l < 2; l++) if (sl[l].v && sl[l].fscw) sv = 1'b0;
          // The cause field is rewritten only by the youngest cause writer issued so far (tag == cz_eff); flags always
          // OR (bits: cause E 17 V 16 Z 15 O 14 U 13 I 12, flag V 6 .. I 2).  A kill takes the tag counter back to the
          // youngest surviving writer; if that one has completed already its rewrite was skipped while a younger (now
          // killed) writer held the counter: the record of the youngest completed writer (czs_*) brings it back.
          if (kill_v && sv && st == cz_eff) f[17:12] = sc;
          // cause committed at FS
          if (fs.v && fs.cw && !fs.tp)
            cz_event(f, st, sc, sv, fs.cz, {1'b0, fs.x.v, fs.x.z, fs.x.o, fs.x.u, fs.x.i}, 1'b1);
          // vector results: the first word of an instruction rewrites the cause, FTRV's later rows OR into it
          if (vres_valid && vc_live) cz_event(f, st, sc, sv, vc_cz, {vres_e, vres_x}, vc_head);
          // the divider's result pass: the cause with the lower / single word, at its FS
          // (a trap pass: the same cause, or E alone -- SOC.md 4.3.8)
          if (dv_cw) cz_event(f, st, sc, sv, dv_wr_cz, dv_wr_te ? 6'b100000 : {1'b0, dv_wr_x}, 1'b1);
          for (int k = 0; k < 2; k++)
            if (db_on[k] && !db_div[k] && db_age[k] == 5'(db_l1[k]) + (db_fpul[k] ? 5'd2 : 5'd1))
              cz_event(f, st, sc, sv, db_cz[k], {1'b0, db_x[k].v, db_x[k].z, db_x[k].o, db_x[k].u, db_x[k].i}, 1'b1);
          // the trap's cause (the youngest cause writer): cause <- the computed V / Z / O / U / I, flags |= them; or E
          // alone, flags kept [HW fpdn: 0x00001084, 0x00085094, 0x00040080, 0x00050840; ci2 0x00048420]
          if (tcw_v) cz_event(f, st, sc, sv, tcw_cz, tcw_c, 1'b1);
          czs_tag <= st; czs_c <= sc; czs_v <= sv;
          // FRCHG / FSCHG flip the mode the decoder uses at issue ...
          for (int s = 0; s < 2; s++) if (di_v[s]) begin
            if (di_d[s].form == F_FRCHG) f[21] = ~f[21];
            if (di_d[s].form == F_FSCHG) f[20] = ~f[20];
          end
          // ... and a killed one flips it back (its pass is in F1 / F2 / FS)
          if (f1.v && kl(f1.isn)) f[21:20] = f[21:20] ^ f1.mfl;
          if (f2.v && kl(f2.isn)) f[21:20] = f[21:20] ^ f2.mfl;
          if (fs.v && kl(fs.isn)) f[21:20] = f[21:20] ^ fs.mfl;
          fpscr <= f;
        end
        // ---- FE pipe
        fs <= (f2.v && !kl(f2.isn)) ? f2 : '0;
        f2 <= (f1.v && !kl(f1.isn)) ? f1_out : '0;
        f1 <= '0;
        di_fcmpd_pending <= di_fcmpd;
        if (kill_v) cz_ctr <= cz_eff;
        else if (cz_is[0] || cz_is[1]) cz_ctr <= cz_new;
        for (int s = 0; s < 2; s++)
          if (di_v[s] && di_c[s].fe && ((!di_c[s].vec && !di_c[s].dbl) || (trap_d[s] && !di_c[s].vec && !di_c[s].div))) begin
            f1_t f;
            f = '0;
            f.v = 1'b1; f.lane = s[0]; f.form = di_d[s].form; f.op = di_op[s]; f.md = di_md[s];
            f.rn = {di_md[s][2], di_op[s][11:8]}; f.rm = {di_md[s][2], di_op[s][7:4]};
            f.n = fr_w[f.rn]; f.m = fr_w[f.rm]; f.z = fr_w[{di_md[s][2], 4'd0}]; f.ul = fpul_w; f.cz = cz_new;
            f.isn = di_isn[s];
            // the tag's owner: a cause writer that is not a divide (whose record owns it, a trap pass's too)
            f.own = cz_is[s] && !di_c[s].div;
            f.tp = trap_d[s];
            // a double / FCMP DR trap pass carries its cause from D (the double sequencer's value functions): E
            // alone, or the computed cause
            f.tpv = trap_d[s] && di_c[s].dbl;
            f.tpx = dd_e[s] ? 6'b100000 : {1'b0, dd_r[s].x};
            f.mfl = {di_d[s].form == F_FRCHG, di_d[s].form == F_FSCHG};
            f1 <= f;
          end
        if (di_fcmpd) begin                                // FCMP DR: the compare pass, micro-op 1
          f1_t f;
          f = '0;
          f.v = 1'b1; f.lane = fcd_lane; f.op = di_fcmpd_op; f.md = di_fcmpd_md;
          f.form = di_fcmpd_op[3:0] == 4'h4 ? F_FCMP_EQ_DRM_DRN : F_FCMP_GT_DRM_DRN;
          f.cz = cz_ctr;                                   // tagged when FCMP DR issued (D is held since)
          f.isn = fcd_isn; f.own = 1'b1; f.fcd = 1'b1;
          f1 <= f;
          fcd_v <= 1'b0;
        end
        for (int s = 0; s < 2; s++)
          if (di_v[s] && !trap_d[s] && (di_d[s].form == F_FCMP_EQ_DRM_DRN || di_d[s].form == F_FCMP_GT_DRM_DRN)) begin
            fcd_v <= 1'b1; fcd_isn <= di_isn[s]; fcd_lane <= s[0];
          end
        if (fcd_v && kl(fcd_isn)) fcd_v <= 1'b0;
        // ---- double-precision sequencer
        for (int k = 0; k < 2; k++) if (db_on[k]) begin
          db_age[k] <= db_age[k] + 5'd1;
          if (db_age[k] == 5'(db_l1[k]) + 5'd2 || kl(db_isn[k])) db_on[k] <= 1'b0;
        end
        // (FDIV / FSQRT DR's trap pass too: its setup passes, then the recurrence; no destination, no F1 check)
        for (int s = 0; s < 2; s++) if (di_v[s] && (!trap_d[s] || di_c[s].div) && di_c[s].dbl && di_c[s].fe &&
                                        di_d[s].form != F_FCMP_EQ_DRM_DRN && di_d[s].form != F_FCMP_GT_DRM_DRN) begin
          // the previous operation's tag moves on with its result words to F2 / FS
          db_on[1] <= db_on[0] && db_age[0] != 5'(db_l1[0]) + 5'd2; db_age[1] <= db_age[0] + 5'd1; db_l1[1] <= db_l1[0];
          db_dst[1] <= db_dst[0]; db_src[1] <= db_src[0]; db_fpul[1] <= db_fpul[0]; db_res[1] <= db_res[0];
          db_x[1] <= db_x[0]; db_e[1] <= db_e[0]; db_div[1] <= db_div[0]; db_cz[1] <= db_cz[0];
          db_isn[1] <= db_isn[0]; db_lane[1] <= db_lane[0];
          db_on[0] <= 1'b1; db_age[0] <= 5'd1; db_dst[0] <= trap_d[s] ? '0 : di_d[s].fw; db_fpul[0] <= dd_fpul[s];
          db_div[0] <= di_c[s].div; db_cz[0] <= cz_new; db_isn[0] <= di_isn[s]; db_lane[0] <= s[0];
          db_l1[0] <= dd_l1[s]; db_res[0] <= dd_r[s].r;
          db_x[0] <= trap_d[s] ? '0 : dd_r[s].x; db_e[0] <= !trap_d[s] && dd_e[s];
          db_src[0] <= (di_d[s].form == F_FADD_DRM_DRN || di_d[s].form == F_FSUB_DRM_DRN ||
                        di_d[s].form == F_FMUL_DRM_DRN) ? di_d[s].fs : 32'd0;
        end
        // ---- divider records: the result passes move with the FE pipe; a pass is granted here (the steal)
        for (int k = 0; k < 2; k++) if (dv_on[k]) begin
          if (ps_lo[k] != 3'd0 && ps_lo[k] != 3'd4) ps_lo[k] <= ps_lo[k] + 3'd1;
          if (ps_hi[k] != 3'd0 && ps_hi[k] != 3'd4) ps_hi[k] <= ps_hi[k] + 3'd1;
          if ((dv_dbl[k] ? ps_hi[k] : ps_lo[k]) == 3'd3 || kl(dv_isn[k])) dv_on[k] <= 1'b0;   // the final word written
        end
        if (dv_gnt) begin
          if (dv_ghi) ps_hi[dv_gk] <= 3'd1; else ps_lo[dv_gk] <= 3'd1;
        end
        // a single divide starts at F1: its record is the one with its ISN (a trap pass's E: its F1 operand check)
        if (dv_start && !dv_sdbl)
          for (int k = 0; k < 2; k++) if (dv_on[k] && dv_isn[k] == f1.isn) begin
            dv_st[k] <= 1'b1; dv_te[k] <= f1.tp && f1_vze[0];
          end
        // a divide's record, a trap pass's too (SOC.md 4.3.8: no destination; the recurrence gives its cause)
        for (int s = 0; s < 2; s++) if (di_v[s] && di_c[s].div) begin
          logic j;
          j = dv_on[0] ? 1'b1 : 1'b0;                      // a free record (div_busy_nx guarantees one)
          dv_on[j] <= 1'b1; dv_dbl[j] <= di_c[s].dbl; dv_dst[j] <= trap_d[s] ? '0 : di_d[s].fw; dv_cz[j] <= cz_new;
          dv_te[j] <= trap_d[s] && di_c[s].dbl && dd_e[s];
          dv_isn[j] <= di_isn[s]; dv_st[j] <= di_c[s].dbl; dv_pst[j] <= 1'b0; dv_cap[j] <= 1'b0;
          ps_lo[j] <= 3'd0; ps_hi[j] <= 3'd0;
          dv_old <= dv_on[!j] ? !j : j;
        end
        // ---- vector unit records
        for (int i = 0; i < VN; i++) if (vq[i].v) begin
          vq[i].age <= vq[i].age + 5'd1;
          if ((vq[i].age >= vq[i].last && vq[i].age >= vq[i].fs) || kx(vq[i].isn)) vq[i].v <= 1'b0;
          // a taken F1 trap of a vector op: it runs on without its destination (cause at its results)
          if (ftk && vq_trap && vq[i].isn == kill_isn) begin vq[i].dst <= '0; vq[i].idst <= '0; end
        end
        for (int s = 0; s < 2; s++) if (di_v[s] && di_c[s].vec) begin
          int k;
          k = 0;
          for (int j = 0; j < 4; j++) begin
            vr_t r;
            r = '0;
            r.v = 1'b1; r.age = 5'd1; r.head = j == 0; r.idst = di_d[s].fw; r.grp = di_op[s][11:10]; r.cz = cz_new;
            r.isn = di_isn[s]; r.lane = s[0];
            unique case (di_d[s].form)
              F_FIPR_FVM_FVN: begin
                r.op = 2'd0; r.fs = 5'd4; r.last = 5'd4; r.p = 5'd1; r.dst = di_d[s].fw; r.tv = {vs_stx[4], 1'b0};
              end
              F_FSRRA_FRN: begin
                r.op = 2'd2; r.fs = 5'd6; r.last = 5'd6; r.p = 5'd3; r.dst = di_d[s].fw; r.tv = vs_stx[4:3];
                r.te = vs_ste;
              end
              F_FSCA_FPUL_DRN: begin
                r.op = 2'd1; r.fs = 5'(6 + j); r.last = 5'd7; r.p = 5'd4; r.sec = j == 1;
                r.dst = di_d[s].fw & (j == 0 ? 32'h5555_5555 : 32'hAAAA_AAAA);
              end
              default: begin                               // FTRV: row j writes element j
                r.op = 2'd3; r.fs = 5'(4 + j); r.last = 5'd7; r.p = 5'd4; r.ftrv = 1'b1;
                r.dst = 32'h1 << ({di_md[s][2], di_op[s][11:10], 2'b00} + 5'(j));
              end
            endcase
            if (trap_d[s]) begin r.dst = '0; r.idst = '0; r.tv = '0; r.te = 1'b0; end   // a trap pass: the cause only
            if (j == 0 || (di_d[s].form == F_FSCA_FPUL_DRN && j == 1) || di_d[s].form == F_FTRV_XMTRX_FVN) begin
              int q;
              q = vfree(k);
              if (q < VN) vq[q] <= r;
              k = k + 1;
            end
          end
        end
        // ---- multiplier
        for (int i = 0; i < MN; i++) if (mq[i].v) begin
          if (mq[i].eta == 3'd0 || kl(mq[i].isn)) mq[i].v <= 1'b0;
          else mq[i].eta <= mq[i].eta - 3'd1;
          if (mac_ld_a_v && !mq[i].av && !mq[i].def) begin mq[i].a <= mac_ld_a; mq[i].av <= 1'b1; end
          if (mac_ld_b_v && mq[i].av && !mq[i].bv && !mq[i].def) begin mq[i].b <= mac_ld_b; mq[i].bv <= 1'b1; end
          if (def_ld_v && mq[i].def && !mq[i].av) begin mq[i].a <= def_ld; mq[i].av <= 1'b1; end
        end
        for (int l = 0; l < 2; l++) if (ex[l].v && ex[l].step == 3'd0 && !kl(ex[l].isn)) begin
          mr_t r;
          logic go;
          r = '0; go = 1'b1;
          r.v = 1'b1; r.isn = ex[l].isn; r.s = ex_s[l];
          unique case (ex[l].form)
            F_MUL_L_RM_RN:   begin r.op = M_MULL;  r.eta = 3'd4; r.a = ex_res[l].b; r.b = ex_res[l].a; r.av = 1; r.bv = 1; end
            F_MULS_W_RM_RN:  begin r.op = M_MULSW; r.eta = 3'd4; r.a = ex_res[l].b; r.b = ex_res[l].a; r.av = 1; r.bv = 1; end
            F_MULU_W_RM_RN:  begin r.op = M_MULUW; r.eta = 3'd4; r.a = ex_res[l].b; r.b = ex_res[l].a; r.av = 1; r.bv = 1; end
            F_DMULS_L_RM_RN: begin r.op = M_DMULS; r.eta = 3'd4; r.a = ex_res[l].b; r.b = ex_res[l].a; r.av = 1; r.bv = 1; end
            F_DMULU_L_RM_RN: begin r.op = M_DMULU; r.eta = 3'd4; r.a = ex_res[l].b; r.b = ex_res[l].a; r.av = 1; r.bv = 1; end
            F_MAC_L_AT_RM_INC_AT_RN_INC: begin r.op = M_MACL; r.eta = 3'd4; end
            F_MAC_W_AT_RM_INC_AT_RN_INC: begin r.op = M_MACW; r.eta = 3'd4; end
            F_CLRMAC:        begin r.def = 1; r.clr = 1; r.eta = 3'd3; end
            F_LDS_RM_MACH:   begin r.def = 1; r.defh = 1; r.eta = 3'd3; r.a = ex_res[l].b; r.av = 1; end   // Rm = op[11:8]
            F_LDS_RM_MACL:   begin r.def = 1; r.eta = 3'd3; r.a = ex_res[l].b; r.av = 1; end
            F_LDS_L_AT_RM_INC_MACH: begin r.def = 1; r.defh = 1; r.eta = 3'd3; end
            F_LDS_L_AT_RM_INC_MACL: begin r.def = 1; r.eta = 3'd3; end
            default: go = 1'b0;
          endcase
          if (go) begin
            int q;
            q = mfree();
            if (q < MN) mq[q] <= r;
          end
        end
      end
    end
  end

  // vector unit start: the D-stage dispatch (operands from the register file) and FTRV's three following rows
  logic [1:0]  ftrv_row;
  logic        ftrv_on;
  logic [7:0]  ftrv_isn;
  logic [31:0] ftrv_x [16];
  logic [31:0] ftrv_v [4];
  always_comb begin
    vs_start = 1'b0; vs_op = fpu_vec_pkg::OP_FIPR; vs_fpul = fpul_w; vs_frn = '0; vs_isn = ftrv_isn;
    for (int j = 0; j < 4; j++) begin vs_fva[j] = '0; vs_fvb[j] = '0; end
    if (ftrv_on && !hold_pipe && !kl(ftrv_isn)) begin
      vs_start = 1'b1;
      for (int j = 0; j < 4; j++) begin vs_fva[j] = ftrv_v[j]; vs_fvb[j] = ftrv_x[4 * j + int'(ftrv_row)]; end
    end
    for (int s = 0; s < 2; s++) if (di_v[s] && di_c[s].vec) begin
      logic [4:0] b;
      b = {di_md[s][2], 4'd0};
      vs_start = 1'b1; vs_isn = di_isn[s];
      unique case (di_d[s].form)
        F_FIPR_FVM_FVN: begin
          vs_op = fpu_vec_pkg::OP_FIPR;
          for (int j = 0; j < 4; j++) begin
            vs_fva[j] = fr_w[b + {1'b0, di_op[s][9:8], 2'b00} + 5'(j)];
            vs_fvb[j] = fr_w[b + {1'b0, di_op[s][11:10], 2'b00} + 5'(j)];
          end
        end
        F_FSCA_FPUL_DRN: vs_op = fpu_vec_pkg::OP_FSCA;
        F_FSRRA_FRN: begin vs_op = fpu_vec_pkg::OP_FSRRA; vs_frn = fr_w[b + {1'b0, di_op[s][11:8]}]; end
        default: begin                                   // FTRV row 0
          vs_op = fpu_vec_pkg::OP_FIPR;
          for (int j = 0; j < 4; j++) begin
            vs_fva[j] = fr_w[b + {1'b0, di_op[s][11:10], 2'b00} + 5'(j)];
            vs_fvb[j] = fr_w[(b ^ 5'd16) + 5'(4 * j)];
          end
        end
      endcase
    end
  end
  always_ff @(posedge clk) begin
    if (rst) begin ftrv_on <= 1'b0; ftrv_row <= '0; end
    else if (!hold_pipe) begin
      if (ftrv_on) begin
        ftrv_row <= ftrv_row + 2'd1;
        if (ftrv_row == 2'd3 || kl(ftrv_isn)) ftrv_on <= 1'b0;
      end
      for (int s = 0; s < 2; s++) if (di_v[s] && di_d[s].form == F_FTRV_XMTRX_FVN) begin
        logic [4:0] b;
        b = {di_md[s][2], 4'd0};
        ftrv_on <= 1'b1; ftrv_row <= 2'd1; ftrv_isn <= di_isn[s];
        for (int j = 0; j < 16; j++) ftrv_x[j] <= fr_w[(b ^ 5'd16) + 5'(j)];
        for (int j = 0; j < 4; j++) ftrv_v[j] <= fr_w[b + {1'b0, di_op[s][11:10], 2'b00} + 5'(j)];
      end
    end
  end

  // divider start: single in F1 (bypassed operands; not while F1 is held, not an F1-trapping or killed one), double in
  // D; a D-decided trap's pass starts it too (SOC.md 4.3.8)
  always_comb begin
    logic [4:0] pn, pm;
    pn = '0; pm = '0;
    dv_start = 1'b0; dv_sqrt = 1'b0; dv_sdbl = 1'b0; dv_a = '0; dv_b = '0; dv_sisn = f1.isn;
    if (dv_s_start && !hold_pipe && !f1_trap && !kl(f1.isn)) begin
      dv_start = 1'b1; dv_sqrt = f1.form == F_FSQRT_FRN; dv_a = {32'd0, dv_s_a}; dv_b = {32'd0, dv_s_b};
    end
    for (int s = 0; s < 2; s++) if (di_v[s] && di_c[s].div && di_c[s].dbl) begin
      pn = {di_md[s][2], di_op[s][11:9], 1'b0};
      pm = {di_md[s][2], di_op[s][7:5], 1'b0};
      dv_start = 1'b1; dv_sdbl = 1'b1; dv_sqrt = di_d[s].form == F_FSQRT_DRN; dv_sisn = di_isn[s];
      dv_a = {fr_w[pn], fr_w[pn + 1]}; dv_b = {fr_w[pm], fr_w[pm + 1]};
    end
  end

  // free slots
  function automatic int vfree(input int k);
    int n;
    n = 0;
    for (int i = 0; i < VN; i++) if (!vq[i].v || (vq[i].age >= vq[i].last && vq[i].age >= vq[i].fs)) begin
      if (n == k) return i;
      n++;
    end
    return VN;
  endfunction
  function automatic int mfree();
    for (int i = 0; i < MN; i++) if (!mq[i].v || mq[i].eta == 3'd0) return i;
    return MN;
  endfunction

  always_comb begin
    idle = !f1.v && !f2.v && !fs.v && !db_on[0] && !db_on[1] && !dv_on[0] && !dv_on[1] && !ftrv_on && !fcd_v;
    for (int i = 0; i < VN; i++) if (vq[i].v) idle = 1'b0;
    for (int i = 0; i < MN; i++) if (mq[i].v) idle = 1'b0;
  end

  always_comb begin
    dbg_hit = 1'b1;
    dbg_rdata = '0;
    if (dbg_raddr >= 8'd64 && dbg_raddr < 8'd96) dbg_rdata = fr[5'(dbg_raddr - 8'd64)];
    else unique case (dbg_raddr)
      8'd31: dbg_rdata = mach;
      8'd32: dbg_rdata = macl;
      8'd35: dbg_rdata = fpscr;
      8'd36: dbg_rdata = fpul;
      default: dbg_hit = 1'b0;
    endcase
  end

  // ============================================================================================================
  // the commit-log records (the head; SOC.md 6.3): this cycle's register writes, each with its writer's ISN, in the
  // cycle the write lands (an unheld one); the slots are fixed by source
  function automatic v1_soc_pkg::commit_rec_t mkrec(input v1_soc_pkg::ck_e k, input logic [7:0] idx,
                                                    input logic [7:0] isn, input logic [31:0] d);
    v1_soc_pkg::commit_rec_t r;
    r = '0;
    r.v = 1'b1; r.kind = k; r.isn = isn; r.idx = idx; r.data = {32'd0, d};
    return r;
  endfunction
  localparam logic [7:0] IX_FPUL = 8'(v1_soc_pkg::SY_FPUL), IX_FPSCR = 8'(v1_soc_pkg::SY_FPSCR),
                         IX_MACH = 8'(v1_soc_pkg::SY_MACH), IX_MACL = 8'(v1_soc_pkg::SY_MACL);
  logic rec_ovf;                        // a source wrote more than its slots hold (rec_ovf_err)
  always_comb begin
    logic [5:0]  ns, nf, nm;            // writes of the S stage, the FE side, the multiplier
    logic [1:0]  yfl;                   // the {FR, SZ} flips of the younger passes in F1 / F2 (already applied)
    logic [31:0] fscr_after;            // FPSCR after the FRCHG / FSCHG at FS, in program order
    frec = '0; ns = '0; nf = '0; nm = '0;
    yfl = (f1.v ? f1.mfl : 2'b00) ^ (f2.v ? f2.mfl : 2'b00);
    fscr_after = fpscr_w ^ (32'(yfl) << 20);
    if (!rst && !hold_pipe) begin
      // the LS pipe at S
      for (int l = 0; l < 2; l++) if (sl[l].v) begin
        for (int w = 0; w < 32; w++) if (sl[l].fwm[w]) begin
          if (ns < 6'd2) frec[ns[0]] = mkrec(v1_soc_pkg::CK_FPR, 8'(w), sl[l].isn, w[0] ? sl[l].fwo : sl[l].fwe);
          ns = ns + 6'd1;
        end
        if (sl[l].uw) begin
          if (ns < 6'd2) frec[ns[0]] = mkrec(v1_soc_pkg::CK_SYS, IX_FPUL, sl[l].isn, sl[l].uv);
          ns = ns + 6'd1;
        end
        if (sl[l].fscw) begin
          if (ns < 6'd2) frec[ns[0]] = mkrec(v1_soc_pkg::CK_SYS, IX_FPSCR, sl[l].isn, sl[l].fscv);
          ns = ns + 6'd1;
        end
      end
      // the FE side, in the write-through order: FS, the double sequencer, the divider's result pass, the vector unit
      if (fs.v) begin
        for (int w = 0; w < 32; w++) if (fs.wm[w]) begin
          if (nf < 6'd2) frec[2 + 32'(nf[0])] = mkrec(v1_soc_pkg::CK_FPR, 8'(w), fs.isn, w[0] ? fs.wo : fs.we);
          nf = nf + 6'd1;
        end
        if (fs.uw) begin
          if (nf < 6'd2) frec[2 + 32'(nf[0])] = mkrec(v1_soc_pkg::CK_SYS, IX_FPUL, fs.isn, fs.uv);
          nf = nf + 6'd1;
        end
        if (fs.mfl != 2'b00 && !kl(fs.isn)) begin
          if (nf < 6'd2) frec[2 + 32'(nf[0])] = mkrec(v1_soc_pkg::CK_SYS, IX_FPSCR, fs.isn, fscr_after);
          nf = nf + 6'd1;
        end
      end
      for (int w = 0; w < 32; w++) if (db_wr_m[w]) begin
        if (nf < 6'd2) frec[2 + 32'(nf[0])] = mkrec(v1_soc_pkg::CK_FPR, 8'(w), db_wr_isn, w[0] ? db_wr_v[31:0] : db_wr_v[63:32]);
        nf = nf + 6'd1;
      end
      if (db_wr_u) begin
        if (nf < 6'd2) frec[2 + 32'(nf[0])] = mkrec(v1_soc_pkg::CK_SYS, IX_FPUL, db_wr_isn, db_wr_v[31:0]);
        nf = nf + 6'd1;
      end
      for (int w = 0; w < 32; w++) if (dv_wr_m[w]) begin
        if (nf < 6'd2) frec[2 + 32'(nf[0])] = mkrec(v1_soc_pkg::CK_FPR, 8'(w), dv_wr_isn, dv_wr_v);
        nf = nf + 6'd1;
      end
      for (int w = 0; w < 32; w++) if (vc_wr_m[w]) begin
        if (nf < 6'd2) frec[2 + 32'(nf[0])] = mkrec(v1_soc_pkg::CK_FPR, 8'(w), vc_isn, vc_wr_v);
        nf = nf + 6'd1;
      end
      // the multiplier (the write of the record at eta 0)
      for (int i = 0; i < MN; i++) if (mq[i].v && mq[i].eta == 3'd0) nm = nm + 6'd1;
      if (mc_we && mc_h) frec[4] = mkrec(v1_soc_pkg::CK_SYS, IX_MACH, mc_isn, mc_v[63:32]);
      if (mc_we && mc_l) frec[5] = mkrec(v1_soc_pkg::CK_SYS, IX_MACL, mc_isn, mc_v[31:0]);
    end
    rec_ovf = ns > 6'd2 || nf > 6'd2 || nm > 6'd1;
  end

  // testbench checks (read hierarchically; no hardware):
  //   pass_err     a result pass reaches FS before its quotient exists (SOC.md 4.5: the request is posted three divider
  //                cycles before `done`, and the divider never lags the FE pipe: hold_div implies hold_pipe)
  //   kill_wr_err  a record is killed in the cycle it writes a register (see the write-through above)
  //   kill_hold_err the kill arrives in a held cycle (the core takes only in unheld cycles)
  //   vec_orph_err an array result no record is due for (a pass started for a dropped record: a killed op's)
  //   rec_ovf_err  more register writes in a cycle than the commit-log slots of their source (the head's structure)
  /* verilator lint_off UNUSEDSIGNAL */
  logic pass_err, kill_wr_err, kill_hold_err, vec_orph_err, rec_ovf_err;
  /* verilator lint_on UNUSEDSIGNAL */
  always_comb begin
    pass_err = 1'b0;
    for (int k = 0; k < 2; k++)
      if (dv_on[k] && (ps_lo[k] == 3'd3 || (dv_dbl[k] && ps_hi[k] == 3'd3)) && !dv_cap[k] && !dv_done) pass_err = 1'b1;
    kill_wr_err = 1'b0;
    for (int l = 0; l < 2; l++)
      if (sl[l].v && (sl[l].fwm != 0 || sl[l].uw || sl[l].fscw) && kx(sl[l].isn)) kill_wr_err = 1'b1;
    if (fs.v && (fs.wm != 0 || fs.uw || fs.cw) && kx(fs.isn)) kill_wr_err = 1'b1;
    for (int k = 0; k < 2; k++) begin
      if (db_on[k] && !db_div[k] && db_age[k] > 5'(db_l1[k]) && kx(db_isn[k])) kill_wr_err = 1'b1;
      if (dv_on[k] && (ps_lo[k] == 3'd3 || ps_hi[k] == 3'd3) && kx(dv_isn[k])) kill_wr_err = 1'b1;
    end
    for (int i = 0; i < VN; i++) if (vq[i].v && vq[i].age == vq[i].fs && vres_valid && kx(vq[i].isn)) kill_wr_err = 1'b1;
    for (int i = 0; i < MN; i++) if (mq[i].v && mq[i].eta == 3'd0 && kx(mq[i].isn)) kill_wr_err = 1'b1;
    kill_hold_err = kill_v && hold_pipe;
    vec_orph_err = vres_valid && !vc_live;
    rec_ovf_err = rec_ovf;
  end
endmodule
