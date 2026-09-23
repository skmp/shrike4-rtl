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
//   * divider (F3, v1_fdiv): FDIV / FSQRT single start at F1 with the bypassed operands, the result is written when
//     the recurrence finishes (FS at c+12 / c+11); DR start in D and write the lower word at c+24 / c+23, the upper one
//     cycle later.  The next divide may start when the recurrence frees (D c+10 / c+9 / c+23 / c+22).  The result
//     pass takes the FPU dispatch the cycle before it enters F1 (FS - 3), so no FE instruction can issue then.
//     Late readers and later writers wait for the destination until the final pass has left F1 (FS - 1).
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
//     finishing); a write-back always uses the record whose age says its result is due.
//   * FPSCR cause: every cause writer takes a tag at issue from a counter of cause-writer issues.  The divider, the
//     double sequencer and the vector unit finish out of program order with the FE pipe, so a completion rewrites
//     the cause field only while its tag is still the counter's value (no younger cause writer has issued); the
//     flags always OR.  The field thus ends with the youngest instruction's cause, as in program order.  The vector
//     unit reports each result word's cause (fpu_vec_top res_x: the measured rules of model/*/sh4_*.h); FTRV's later
//     rows OR into the field that its first row rewrote.
//   * pairing: besides RAW / WAW between the leader's results and the follower, a follower that writes a source word
//     of FADD / FSUB / FMUL DR cannot pair with it (the re-read in the first F1 pass).
module v1_fpu import tp_pkg::*; import v1_pkg::*; import v1_fp_pkg::*; import v1_mul_pkg::*; (
  input  logic        clk,
  input  logic        rst,
  input  logic        frz,
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
  // the integer pipe's latches (LS-pipe FP results, MAC micro-ops)
  input  uop_t        ex [2],
  input  uop_t        ma [2],
  input  uop_t        sl [2],
  input  uop_t        ex_res [2],
  // dispatch (this cycle's issues)
  input  logic        di_v [2],
  input  logic [15:0] di_op [2],
  input  dec_t        di_d [2],
  input  ictl_t       di_c [2],
  input  logic [2:0]  di_md [2],
  input  logic        di_fcmpd,
  input  logic [15:0] di_fcmpd_op,
  input  logic [2:0]  di_fcmpd_md,
  // multiplier operands from the integer pipe
  input  logic        mac_ld_a_v, mac_ld_b_v, def_ld_v,
  input  logic [31:0] mac_ld_a, mac_ld_b, def_ld,
  input  logic        sr_s,
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
  assign md_dec = {fpscr[21], fpscr[20], fpscr[19]};
  wire rz = fpscr[0];

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
  } f1_t;
  typedef struct packed {
    logic        v;
    logic [31:0] wm;                    // FR words written at FS
    logic [31:0] we, wo;
    logic        uw;
    logic [31:0] uv;
    logic        cw;                    // cause bits committed at FS
    fexc_t       x;
    logic        fcmp;                  // an FCMP pass (flags reach SR at FS)
    logic [4:0]  cz;
  } fp_t;
  f1_t f1;
  fp_t f2, fs;

  // FPSCR cause ownership.  Every instruction that writes the cause field takes a tag at issue from a counter of such
  // issues; the divider, the double sequencer and the vector unit finish out of program order with the FE pipe, so a
  // completion rewrites the cause only while no younger cause writer has issued (its tag is still the counter's
  // value) -- the field then holds the youngest instruction's cause, as in program order.  The flags always OR.
  // 5 bits: at most one cause writer issues per cycle and none is in flight for 32 cycles (FDIV DR: 25).
  logic [4:0] cz_ctr;
  logic       cz_is [2];                // this slot issues a cause writer
  always_comb for (int s = 0; s < 2; s++)
    cz_is[s] = di_v[s] && di_c[s].fe && di_d[s].form != F_FRCHG && di_d[s].form != F_FSCHG;
  wire [4:0] cz_new = cz_ctr + 5'd1;    // the tag of this cycle's cause writer

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
  logic        db_div [2];              // FDIV / FSQRT DR: the divider writes the result
  logic [4:0]  db_cz  [2];
  // divider
  // destination tags: [0] the operation in the recurrence, [1] the previous one, still rounding / writing back when
  // the next divide starts (the recurrence frees at LAT - 2, the result is written at LAT - 1 / LAT)
  logic        dv_on  [2];
  logic [4:0]  dv_age [2];
  logic [4:0]  dv_w   [2];              // FS of the (upper) result word: 12 / 11 / 25 / 24
  logic        dv_dbl [2];
  logic [31:0] dv_dst [2];
  logic [4:0]  dv_cz  [2];
  logic [63:0] dv_hres;                 // the result held one more cycle for the upper double word
  logic        dv_start, dv_sqrt, dv_sdbl, dv_busy, dv_done;
  logic [63:0] dv_a, dv_b, dv_r;
  fexc_t       dv_x;
  // vector unit: in-flight records (FTRV is four FIPR passes)
  localparam int VN = 8;
  typedef struct packed {
    logic        v;
    logic [1:0]  op;                    // 0 FIPR, 1 FSCA, 2 FSRRA, 3 FTRV row
    logic [4:0]  age;
    logic [4:0]  fs;                    // FS of this result
    logic [4:0]  last;                  // last FS of the instruction (WAW release = last - 2)
    logic [4:0]  p;                     // array passes of the instruction
    logic [31:0] dst;                   // words this record writes
    logic [31:0] idst;                  // the instruction's whole destination
    logic        head;                  // first record of the instruction (holds the array / F1 / FPSCR tags)
    logic        ftrv;
    logic [1:0]  grp;
    logic        sec;                   // FSCA cosine record
    logic [4:0]  cz;
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
  } mr_t;
  mr_t mq [MN];

  // ============================================================================================================
  // write-through register reads
  logic [31:0] fr_w [32];
  logic [31:0] fpul_w, fpscr_w, mach_w, macl_w;
  // this cycle's writes, collected below
  logic [31:0] wr_ls_m, wr_ls_e, wr_ls_o;     // LS-pipe S-stage FR writes (one instruction per cycle)
  logic        wr_ls_u; logic [31:0] wr_ls_uv;
  logic        wr_ls_fs; logic [31:0] wr_ls_fsv;
  logic [31:0] wr_fe_m [4]; logic [31:0] wr_fe_v [4][32];
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
    // double sequencer, divider, vector unit results of this cycle
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
  assign fpscr_now = fpscr_w;
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

  fp_t   f1_out;
  logic  f1_t_we, f1_t_val;
  logic  dv_s_start;                    // single FDIV / FSQRT start (in F1)
  logic [31:0] dv_s_a, dv_s_b;
  always_comb begin
    logic [31:0] n, m, z, ul;
    fs_res_t r;
    fcmp_t cm;
    f1_out = '0; f1_t_we = 1'b0; f1_t_val = 1'b0; dv_s_start = 1'b0; dv_s_a = '0; dv_s_b = '0;
    n = f1_byp(f1.rn, f1.n, f1.lane);
    m = f1_byp(f1.rm, f1.m, f1.lane);
    z = f1_byp({f1.md[2], 4'd0}, f1.z, f1.lane);
    ul = f1_bypu(f1.ul, f1.lane);
    r = '0;
    if (f1.v) begin
      f1_out.v = 1'b1;
      f1_out.cw = 1'b1;
      f1_out.cz = f1.cz;
      unique case (f1.form)
        F_FADD_FRM_FRN: begin r = fadd_s(n, m, rz); f1_out.wm = 32'h1 << f1.rn; end
        F_FSUB_FRM_FRN: begin r = fadd_s(n, {~m[31], m[30:0]}, rz); f1_out.wm = 32'h1 << f1.rn; end
        F_FMUL_FRM_FRN: begin r = fmul_s(n, m, rz); f1_out.wm = 32'h1 << f1.rn; end
        F_FMAC_FR0_FRM_FRN: begin r = fmac_s(z, m, n, rz); f1_out.wm = 32'h1 << f1.rn; end
        F_FLOAT_FPUL_FRN: begin r = float_s(ul, rz); f1_out.wm = 32'h1 << f1.rn; end
        F_FTRC_FRM_FPUL: begin r = ftrc_s(n); f1_out.uw = 1'b1; f1_out.uv = r.r; end   // FRm is op[11:8]
        F_FCMP_EQ_FRM_FRN, F_FCMP_GT_FRM_FRN: begin
          cm = fcmp_s(n, m);
          f1_t_we = 1'b1;
          f1_t_val = f1.form == F_FCMP_EQ_FRM_FRN ? cm.eq : cm.gt;
          r.x = '0; r.x.v = f1.form == F_FCMP_EQ_FRM_FRN ? cm.v_eq : cm.v_gt;
          f1_out.fcmp = 1'b1;
        end
        F_FCMP_EQ_DRM_DRN, F_FCMP_GT_DRM_DRN: begin
          logic [63:0] dn, dm;
          dn = {fr_w[{f1.md[2], f1.op[11:9], 1'b0}], fr_w[{f1.md[2], f1.op[11:9], 1'b1}]};
          dm = {fr_w[{f1.md[2], f1.op[7:5], 1'b0}], fr_w[{f1.md[2], f1.op[7:5], 1'b1}]};
          cm = fcmp_d(dn, dm);
          f1_t_we = 1'b1;
          f1_t_val = f1.form == F_FCMP_EQ_DRM_DRN ? cm.eq : cm.gt;
          r.x = '0; r.x.v = f1.form == F_FCMP_EQ_DRM_DRN ? cm.v_eq : cm.v_gt;
          f1_out.fcmp = 1'b1;
        end
        F_FDIV_FRM_FRN, F_FSQRT_FRN: begin
          dv_s_start = 1'b1; dv_s_a = n; dv_s_b = m;
          f1_out.v = 1'b0;               // the divider writes the result and the cause
        end
        F_FRCHG, F_FSCHG: f1_out.cw = 1'b0;   // no arithmetic: the pass only orders FPSCR
        default: ;
      endcase
      f1_out.x = r.x;
      f1_out.we = r.r; f1_out.wo = r.r;
    end
  end
  assign t_we = f1_t_we;
  assign t_val = f1_t_val;
  assign t_lane = f1.lane;

  // ============================================================================================================
  // this cycle's writes of the double sequencer, the divider, the vector unit and the multiplier
  logic [31:0] db_wr_m;  logic db_wr_u;  logic [63:0] db_wr_v;
  logic [31:0] dv_wr_m;  logic [31:0] dv_wr_v;
  logic [31:0] vc_wr_m;  logic [31:0] vc_wr_v;
  logic        mc_we;    logic [63:0] mc_v;
  logic        vres_valid, vres_second;
  logic [31:0] vres;
  logic [4:0]  vres_x;                  // the result word's cause {V, Z, O, U, I}
  logic        vc_head;                 // it is the instruction's first result word (FTRV: row 0)
  logic [4:0]  vc_cz;
  logic [4:0]  dv_wr_cz;                // tag of the divider result written this cycle
  always_comb begin
    db_wr_m = '0; db_wr_u = 1'b0; db_wr_v = '0;
    for (int k = 0; k < 2; k++) if (db_on[k] && !db_div[k]) begin
      if (db_fpul[k]) begin
        if (db_age[k] == 5'(db_l1[k]) + 5'd2) begin db_wr_u = 1'b1; db_wr_v = db_res[k]; end
      end else begin
        if (db_age[k] == 5'(db_l1[k]) + 5'd1) begin db_wr_m = db_dst[k] & 32'hAAAA_AAAA; db_wr_v = db_res[k]; end  // lower word: FR(2n+1)
        if (db_age[k] == 5'(db_l1[k]) + 5'd2) begin db_wr_m = db_dst[k] & 32'h5555_5555; db_wr_v = db_res[k]; end  // upper word: FR(2n)
      end
    end
    // the tag whose result is due this cycle: lower / single word with `done`, the upper double word one cycle later
    dv_wr_m = '0; dv_wr_v = '0; dv_wr_cz = '0;
    for (int k = 0; k < 2; k++) if (dv_on[k]) begin
      if (dv_age[k] == (dv_dbl[k] ? dv_w[k] - 5'd1 : dv_w[k])) begin
        dv_wr_cz = dv_cz[k];
        dv_wr_m = dv_dbl[k] ? dv_dst[k] & (32'h5555_5555 << 1) : dv_dst[k]; dv_wr_v = dv_r[31:0];
      end else if (dv_dbl[k] && dv_age[k] == dv_w[k]) begin
        dv_wr_m = dv_dst[k] & 32'h5555_5555; dv_wr_v = dv_hres[63:32];
      end
    end
    vc_wr_m = '0; vc_wr_v = vres; vc_head = 1'b0; vc_cz = '0;
    if (vres_valid) begin
      // the oldest record whose result is due
      for (int i = VN - 1; i >= 0; i--) if (vq[i].v && vq[i].age == vq[i].fs) begin vc_wr_m = vq[i].dst; vc_head = vq[i].head; vc_cz = vq[i].cz; end
    end
    mc_we = 1'b0; mc_v = {mach, macl};
    for (int i = 0; i < MN; i++) begin
      if (mq[i].v && mq[i].eta == 3'd0) begin
        mc_we = 1'b1;
        if (mq[i].clr) mc_v = 64'd0;
        else if (mq[i].def) mc_v = mq[i].defh ? {mq[i].a, macl} : {mach, mq[i].a};
        else mc_v = v1_mul(mq[i].op, mq[i].a, mq[i].b, sr_s, {mach, macl});
      end
    end
  end

  // ============================================================================================================
  // divider
  v1_fdiv u_div (
    .clk, .rst, .start(dv_start), .hold(frz), .sqrt(dv_sqrt), .dbl(dv_sdbl), .rz,
    .a(dv_a), .b(dv_b), .busy(dv_busy), .done(dv_done), .r(dv_r), .x(dv_x)
  );
  // vector unit
  logic        vs_start;
  fpu_vec_pkg::op_e vs_op;
  logic [31:0] vs_fva [4], vs_fvb [4], vs_fpul, vs_frn;
  logic        vs_busy;
  fpu_vec_top u_vec (
    .clk, .rst, .start(vs_start), .hold(frz), .op(vs_op), .rz, .fva(vs_fva), .fvb(vs_fvb), .fpul(vs_fpul), .frn(vs_frn),
    .busy(vs_busy), .res_valid(vres_valid), .res_second(vres_second), .res(vres), .res_x(vres_x)
  );

  // ============================================================================================================
  // hazards of the D slots against the FPU state and the LS pipe
  //   fwhy bits: 0 FR / FPUL RAW, 1 WAW / WAR, 2 FPSCR, 3 multiplier, 4 F1 / array busy, 5 divider, 6 dispatch steal,
  //              7 FTRV group lock
  function automatic logic [31:0] pairw(input logic [31:0] m);   // widen a word mask to its DR pairs
    return m | ((m & 32'h5555_5555) << 1) | ((m & 32'hAAAA_AAAA) >> 1);
  endfunction
  logic [31:0] pend_fe, pend_f1, pend_rf, pend_st, pend_wr, pend_war, grp_lock;
  logic        u_pend_fe, u_pend_rf, u_pend_st, u_pend_ma, u_pend_wr;
  logic        fscr_busy, dec_lock, mul_fe_busy, mul_st_busy, f1_busy_nx, arr_busy_nx, div_busy_nx, steal;
  logic        vec_f1_busy;
  always_comb begin
    pend_fe = '0; pend_f1 = '0; pend_rf = '0; pend_st = '0; pend_wr = '0; pend_war = '0; grp_lock = '0;
    u_pend_fe = 1'b0; u_pend_rf = 1'b0; u_pend_st = 1'b0; u_pend_ma = 1'b0; u_pend_wr = 1'b0;
    fscr_busy = 1'b0; dec_lock = 1'b0; mul_fe_busy = 1'b0; mul_st_busy = 1'b0;
    f1_busy_nx = 1'b0; arr_busy_nx = 1'b0; div_busy_nx = 1'b0; steal = 1'b0; vec_f1_busy = 1'b0;
    t_busy = 1'b0; flag_busy = 1'b0;
    // FE pipe: F1 and F2 results are not in the register file yet (no bypass out of the FE pipe)
    if (f1.v) begin
      logic [31:0] m;
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
      logic [31:0] lo, up;
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
    // divider: the destination until its final pass has left F1 / reached FS
    for (int k = 0; k < 2; k++) if (dv_on[k]) begin
      logic [4:0] wlo;
      wlo = dv_dbl[k] ? dv_w[k] - 5'd1 : dv_w[k];
      if (dv_age[k] < wlo) begin pend_fe |= dv_dbl[k] ? dv_dst[k] & (32'h5555_5555 << 1) : dv_dst[k];
                                 pend_rf |= dv_dbl[k] ? dv_dst[k] & (32'h5555_5555 << 1) : dv_dst[k]; end
      if (dv_dbl[k] && dv_age[k] < dv_w[k]) begin pend_fe |= dv_dst[k] & 32'h5555_5555; pend_rf |= dv_dst[k] & 32'h5555_5555; end
      if (dv_age[k] < dv_w[k] - 5'd1) begin pend_st |= dv_dst[k]; pend_wr |= dv_dst[k]; end
      if (dv_age[k] <= dv_w[k]) fscr_busy = 1'b1;
      // the result pass takes the FPU dispatch the cycle before F1 (FS - 3), per result word
      if (dv_age[k] == wlo - 5'd3 || (dv_dbl[k] && dv_age[k] == dv_w[k] - 5'd3)) steal = 1'b1;
    end
    // the next divide may start when the recurrence frees
    if (dv_busy_left > 5'd1) div_busy_nx = 1'b1;
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
      ictl_t ec;
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
  logic [4:0] dv_busy_left;             // cycles until the recurrence is free
  logic       di_fcmpd_pending;

  // per-slot hazards
  always_comb begin
    for (int s = 0; s < 2; s++) begin
      logic [31:0] rd, rf_, st, wr, acc;
      logic ur_d, ur_rf, ur_st, ur_ma, uw_;
      ictl_t cc;
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
      fwhy[s][5] = cc.div && div_busy_nx;
      fwhy[s][6] = cc.fe && steal;
      fwhy[s][7] = cc.fp_ls && |(acc & grp_lock);
      fhz[s] = s_v[s] && |fwhy[s];
    end
  end

  // the follower against the leader (same cycle)
  always_comb begin
    ictl_t c0, c1;
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
  // dispatch and state
  always_ff @(posedge clk) begin
    if (rst) begin
      cz_ctr <= '0;
      f1 <= '0; f2 <= '0; fs <= '0; db_on[0] <= 1'b0; db_on[1] <= 1'b0; dv_on[0] <= 1'b0; dv_on[1] <= 1'b0;
      for (int i = 0; i < VN; i++) vq[i] <= '0;
      for (int i = 0; i < MN; i++) mq[i] <= '0;
      di_fcmpd_pending <= 1'b0; dv_busy_left <= '0;
      if (dbg_we) begin
        if (dbg_addr >= 8'd64 && dbg_addr < 8'd96) fr[dbg_addr - 8'd64] <= dbg_wdata;
        else unique case (dbg_addr)
          8'd31: mach <= dbg_wdata;
          8'd32: macl <= dbg_wdata;
          8'd35: fpscr <= dbg_wdata;
          8'd36: fpul <= dbg_wdata;
          default: ;
        endcase
      end
    end else if (!frz) begin
      // ---- register writes (the write-through values)
      for (int w = 0; w < 32; w++) fr[w] <= fr_w[w];
      fpul <= fpul_w;
      mach <= mach_w; macl <= macl_w;
      begin
        logic [31:0] f;
        f = fpscr_w;
        // cause committed at FS: cause <- x, flag |= x (bits: cause V 16 Z 15 O 14 U 13 I 12, flag V 6 .. I 2)
        // (the cause is rewritten only by the youngest cause writer issued so far: tag == cz_ctr)
        if (fs.v && fs.cw) begin
          if (fs.cz == cz_ctr) f[17:12] = {1'b0, fs.x.v, fs.x.z, fs.x.o, fs.x.u, fs.x.i};
          f[6:2] = f[6:2] | {fs.x.v, fs.x.z, fs.x.o, fs.x.u, fs.x.i};
        end
        // vector results: the first word of an instruction rewrites the cause, FTRV's later rows OR into it
        if (vres_valid) begin
          if (vc_cz == cz_ctr) begin
            if (vc_head) f[17:12] = {1'b0, vres_x}; else f[16:12] = f[16:12] | vres_x;
          end
          f[6:2] = f[6:2] | vres_x;
        end
        if (dv_done) begin
          if (dv_wr_cz == cz_ctr) f[17:12] = {1'b0, dv_x.v, dv_x.z, dv_x.o, dv_x.u, dv_x.i};
          f[6:2] = f[6:2] | {dv_x.v, dv_x.z, dv_x.o, dv_x.u, dv_x.i};
        end
        for (int k = 0; k < 2; k++)
          if (db_on[k] && !db_div[k] && db_age[k] == 5'(db_l1[k]) + (db_fpul[k] ? 5'd2 : 5'd1)) begin
            if (db_cz[k] == cz_ctr) f[17:12] = {1'b0, db_x[k].v, db_x[k].z, db_x[k].o, db_x[k].u, db_x[k].i};
            f[6:2] = f[6:2] | {db_x[k].v, db_x[k].z, db_x[k].o, db_x[k].u, db_x[k].i};
          end
        // FRCHG / FSCHG flip the mode the decoder uses at issue
        for (int s = 0; s < 2; s++) if (di_v[s]) begin
          if (di_d[s].form == F_FRCHG) f[21] = ~f[21];
          if (di_d[s].form == F_FSCHG) f[20] = ~f[20];
        end
        fpscr <= f;
      end
      // ---- FE pipe
      fs <= f2;
      f2 <= f1_out;
      f1 <= '0;
      di_fcmpd_pending <= di_fcmpd;
      if (cz_is[0] || cz_is[1]) cz_ctr <= cz_new;
      for (int s = 0; s < 2; s++) if (di_v[s] && di_c[s].fe && !di_c[s].vec && !di_c[s].dbl) begin
        f1_t f;
        f = '0;
        f.v = 1'b1; f.lane = s[0]; f.form = di_d[s].form; f.op = di_op[s]; f.md = di_md[s];
        f.rn = {di_md[s][2], di_op[s][11:8]}; f.rm = {di_md[s][2], di_op[s][7:4]};
        f.n = fr_w[f.rn]; f.m = fr_w[f.rm]; f.z = fr_w[{di_md[s][2], 4'd0}]; f.ul = fpul_w; f.cz = cz_new;
        f1 <= f;
      end
      if (di_fcmpd) begin                                // FCMP DR: the compare pass, micro-op 1
        f1_t f;
        f = '0;
        f.v = 1'b1; f.lane = 1'b0; f.op = di_fcmpd_op; f.md = di_fcmpd_md;
        f.form = di_fcmpd_op[3:0] == 4'h4 ? F_FCMP_EQ_DRM_DRN : F_FCMP_GT_DRM_DRN;
        f.cz = cz_ctr;                                   // tagged when FCMP DR issued (D is held since)
        f1 <= f;
      end
      // ---- double-precision sequencer
      for (int k = 0; k < 2; k++) if (db_on[k]) begin
        db_age[k] <= db_age[k] + 5'd1;
        if (db_age[k] == 5'(db_l1[k]) + 5'd2) db_on[k] <= 1'b0;
      end
      for (int s = 0; s < 2; s++) if (di_v[s] && di_c[s].dbl && di_c[s].fe && di_d[s].form != F_FCMP_EQ_DRM_DRN &&
                                      di_d[s].form != F_FCMP_GT_DRM_DRN) begin
        logic [63:0] dn, dm;
        logic [4:0] pn, pm;
        fd_res_t r;
        fs_res_t rs;
        pn = {di_md[s][2], di_op[s][11:9], 1'b0};
        pm = {di_md[s][2], di_op[s][7:5], 1'b0};
        dn = {fr_w[pn], fr_w[pn + 1]};
        dm = {fr_w[pm], fr_w[pm + 1]};
        // the previous operation's tag moves on with its result words to F2 / FS
        db_on[1] <= db_on[0] && db_age[0] != 5'(db_l1[0]) + 5'd2; db_age[1] <= db_age[0] + 5'd1; db_l1[1] <= db_l1[0];
        db_dst[1] <= db_dst[0]; db_src[1] <= db_src[0]; db_fpul[1] <= db_fpul[0]; db_res[1] <= db_res[0];
        db_x[1] <= db_x[0]; db_div[1] <= db_div[0]; db_cz[1] <= db_cz[0];
        db_on[0] <= 1'b1; db_age[0] <= 5'd1; db_dst[0] <= di_d[s].fw; db_fpul[0] <= 1'b0; db_div[0] <= 1'b0; db_src[0] <= 32'd0;
        db_cz[0] <= cz_new;
        r = '0;
        unique case (di_d[s].form)
          F_FADD_DRM_DRN: begin r = fadd_d(dn, dm, rz); db_l1[0] <= 3'd6; db_src[0] <= di_d[s].fs; end
          F_FSUB_DRM_DRN: begin r = fadd_d(dn, {~dm[63], dm[62:0]}, rz); db_l1[0] <= 3'd6; db_src[0] <= di_d[s].fs; end
          F_FMUL_DRM_DRN: begin r = fmul_d(dn, dm, rz); db_l1[0] <= 3'd6; db_src[0] <= di_d[s].fs; end
          F_FLOAT_FPUL_DRN: begin r = float_d(fpul_w); db_l1[0] <= 3'd2; end
          F_FCNVSD_FPUL_DRN: begin r = fcnvsd(fpul_w); db_l1[0] <= 3'd2; end
          F_FTRC_DRM_FPUL: begin rs = ftrc_d(dn); r.r = {32'd0, rs.r}; r.x = rs.x; db_l1[0] <= 3'd2; db_fpul[0] <= 1'b1; end
          F_FCNVDS_DRM_FPUL: begin rs = fcnvds(dn, rz); r.r = {32'd0, rs.r}; r.x = rs.x; db_l1[0] <= 3'd2; db_fpul[0] <= 1'b1; end
          F_FDIV_DRM_DRN, F_FSQRT_DRN: begin db_l1[0] <= 3'd2; db_div[0] <= 1'b1; end
          default: ;
        endcase
        db_res[0] <= r.r; db_x[0] <= r.x;
      end
      // ---- divider
      for (int k = 0; k < 2; k++) if (dv_on[k]) begin
        dv_age[k] <= dv_age[k] + 5'd1;
        if (dv_age[k] == dv_w[k]) dv_on[k] <= 1'b0;
      end
      if (dv_busy_left != 0) dv_busy_left <= dv_busy_left - 5'd1;
      if (dv_done) dv_hres <= dv_r;
      for (int s = 0; s < 2; s++) if (di_v[s] && di_c[s].div) begin
        logic dbl;
        dbl = di_c[s].dbl;
        // the running operation's tag moves on with it to the round / write-back side
        dv_on[1] <= dv_on[0] && dv_age[0] != dv_w[0]; dv_age[1] <= dv_age[0] + 5'd1;
        dv_w[1] <= dv_w[0]; dv_dbl[1] <= dv_dbl[0]; dv_dst[1] <= dv_dst[0]; dv_cz[1] <= dv_cz[0];
        dv_on[0] <= 1'b1; dv_age[0] <= 5'd1; dv_dbl[0] <= dbl; dv_dst[0] <= di_d[s].fw; dv_cz[0] <= cz_new;
        dv_w[0] <= dbl ? (di_d[s].form == F_FDIV_DRM_DRN ? 5'd25 : 5'd24) : (di_d[s].form == F_FDIV_FRM_FRN ? 5'd12 : 5'd11);
        // the recurrence is busy until start + LAT - 2 (single starts in F1, double in D)
        dv_busy_left <= dbl ? (di_d[s].form == F_FDIV_DRM_DRN ? 5'd23 : 5'd22) : (di_d[s].form == F_FDIV_FRM_FRN ? 5'd10 : 5'd9);
      end
      // ---- vector unit records
      for (int i = 0; i < VN; i++) if (vq[i].v) begin
        vq[i].age <= vq[i].age + 5'd1;
        if (vq[i].age >= vq[i].last && vq[i].age >= vq[i].fs) vq[i].v <= 1'b0;
      end
      for (int s = 0; s < 2; s++) if (di_v[s] && di_c[s].vec) begin
        int k;
        k = 0;
        for (int j = 0; j < 4; j++) begin
          vr_t r;
          r = '0;
          r.v = 1'b1; r.age = 5'd1; r.head = j == 0; r.idst = di_d[s].fw; r.grp = di_op[s][11:10]; r.cz = cz_new;
          unique case (di_d[s].form)
            F_FIPR_FVM_FVN: begin r.op = 2'd0; r.fs = 5'd4; r.last = 5'd4; r.p = 5'd1; r.dst = di_d[s].fw; end
            F_FSRRA_FRN:    begin r.op = 2'd2; r.fs = 5'd6; r.last = 5'd6; r.p = 5'd3; r.dst = di_d[s].fw; end
            F_FSCA_FPUL_DRN: begin
              r.op = 2'd1; r.fs = 5'(6 + j); r.last = 5'd7; r.p = 5'd4; r.sec = j == 1;
              r.dst = di_d[s].fw & (j == 0 ? 32'h5555_5555 : 32'hAAAA_AAAA);
            end
            default: begin                               // FTRV: row j writes element j
              r.op = 2'd3; r.fs = 5'(4 + j); r.last = 5'd7; r.p = 5'd4; r.ftrv = 1'b1;
              r.dst = 32'h1 << ({di_md[s][2], di_op[s][11:10], 2'b00} + 5'(j));
            end
          endcase
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
        if (mq[i].eta == 3'd0) mq[i].v <= 1'b0;
        else mq[i].eta <= mq[i].eta - 3'd1;
        if (mac_ld_a_v && !mq[i].av && !mq[i].def) begin mq[i].a <= mac_ld_a; mq[i].av <= 1'b1; end
        if (mac_ld_b_v && mq[i].av && !mq[i].bv && !mq[i].def) begin mq[i].b <= mac_ld_b; mq[i].bv <= 1'b1; end
        if (def_ld_v && mq[i].def && !mq[i].av) begin mq[i].a <= def_ld; mq[i].av <= 1'b1; end
      end
      for (int l = 0; l < 2; l++) if (ex[l].v && ex[l].step == 3'd0) begin
        mr_t r;
        logic go;
        r = '0; go = 1'b1;
        r.v = 1'b1;
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

  // vector unit start: the D-stage dispatch (operands from the register file) and FTRV's three following rows
  logic [1:0]  ftrv_row;
  logic        ftrv_on;
  logic [31:0] ftrv_x [16];
  logic [31:0] ftrv_v [4];
  always_comb begin
    vs_start = 1'b0; vs_op = fpu_vec_pkg::OP_FIPR; vs_fpul = fpul_w; vs_frn = '0;
    for (int j = 0; j < 4; j++) begin vs_fva[j] = '0; vs_fvb[j] = '0; end
    if (ftrv_on && !frz) begin
      vs_start = 1'b1;
      for (int j = 0; j < 4; j++) begin vs_fva[j] = ftrv_v[j]; vs_fvb[j] = ftrv_x[4 * j + int'(ftrv_row)]; end
    end
    for (int s = 0; s < 2; s++) if (di_v[s] && di_c[s].vec) begin
      logic [4:0] b;
      b = {di_md[s][2], 4'd0};
      vs_start = 1'b1;
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
    else if (!frz) begin
      if (ftrv_on) begin
        ftrv_row <= ftrv_row + 2'd1;
        if (ftrv_row == 2'd3) ftrv_on <= 1'b0;
      end
      for (int s = 0; s < 2; s++) if (di_v[s] && di_d[s].form == F_FTRV_XMTRX_FVN) begin
        logic [4:0] b;
        b = {di_md[s][2], 4'd0};
        ftrv_on <= 1'b1; ftrv_row <= 2'd1;
        for (int j = 0; j < 16; j++) ftrv_x[j] <= fr_w[(b ^ 5'd16) + 5'(j)];
        for (int j = 0; j < 4; j++) ftrv_v[j] <= fr_w[b + {1'b0, di_op[s][11:10], 2'b00} + 5'(j)];
      end
    end
  end

  // divider start: single in F1 (bypassed operands), double in D
  always_comb begin
    dv_start = 1'b0; dv_sqrt = 1'b0; dv_sdbl = 1'b0; dv_a = '0; dv_b = '0;
    if (dv_s_start) begin
      dv_start = 1'b1; dv_sqrt = f1.form == F_FSQRT_FRN; dv_a = {32'd0, dv_s_a}; dv_b = {32'd0, dv_s_b};
    end
    for (int s = 0; s < 2; s++) if (di_v[s] && di_c[s].div && di_c[s].dbl) begin
      logic [4:0] pn, pm;
      pn = {di_md[s][2], di_op[s][11:9], 1'b0};
      pm = {di_md[s][2], di_op[s][7:5], 1'b0};
      dv_start = 1'b1; dv_sdbl = 1'b1; dv_sqrt = di_d[s].form == F_FSQRT_DRN;
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
    idle = !f1.v && !f2.v && !fs.v && !db_on[0] && !db_on[1] && !dv_on[0] && !dv_on[1] && !ftrv_on;
    for (int i = 0; i < VN; i++) if (vq[i].v) idle = 1'b0;
    for (int i = 0; i < MN; i++) if (mq[i].v) idle = 1'b0;
  end

  always_comb begin
    dbg_hit = 1'b1;
    dbg_rdata = '0;
    if (dbg_raddr >= 8'd64 && dbg_raddr < 8'd96) dbg_rdata = fr[dbg_raddr - 8'd64];
    else unique case (dbg_raddr)
      8'd31: dbg_rdata = mach;
      8'd32: dbg_rdata = macl;
      8'd35: dbg_rdata = fpscr;
      8'd36: dbg_rdata = fpul;
      default: dbg_hit = 1'b0;
    endcase
  end
endmodule
