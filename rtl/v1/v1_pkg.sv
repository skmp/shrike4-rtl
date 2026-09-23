// v1_pkg.sv -- types and the per-form structural control of the v1 core (see README.md).
//
// The control word says WHAT an instruction does in the pipeline: which pipe it goes down, how many D-stage
// micro-operations it is sequenced as, which stage produces each of its results, which stage reads each operand,
// which units / stages it occupies.  It contains no latency: every delay the core shows is the consequence of those
// facts meeting in the pipeline latches (README.md derives each measured latency from them).
package v1_pkg;
  import tp_pkg::*;

  // issue class of the pairing logic (pipes): MT fits either integer pipe; EX the ALU pipe; LS the load/store pipe;
  // FE the FPU dispatch port; CO takes every port (issues alone); BR the branch unit (MT-like, one branch per pair)
  typedef enum logic [2:0] {K_MT = 3'd0, K_EX = 3'd1, K_LS = 3'd2, K_FE = 3'd3, K_CO = 3'd4, K_BR = 3'd5} kls_e;

  // where a GPR result is produced (the D stage compares its sources with the destination tags whose result is not
  // on a bypass path yet)
  typedef enum logic [1:0] {GR_EX = 2'd0,     // end of EX: ALU, address update, MOV, STC (bypass MA->EX)
                            GR_MA = 2'd1,     // end of MA: load data (bypass S->EX)
                            GR_S  = 2'd2      // S: a value that crosses from the FPU / MAC side (no bypass, RF only)
                           } gres_e;

  // FP register / FPUL read path of a consumer
  typedef enum logic [2:0] {FR_NONE = 3'd0,
                            FR_D    = 3'd1,   // D-stage read + bypass from the LS pipe (FE ops, FP moves, FLOAT/FSTS FPUL)
                            FR_RF   = 3'd2,   // register file only (vector ops, double precision, FSCA FPUL, FLOAT DR)
                            FR_EX   = 3'd3,   // captured in EX (store data, STS.L FPUL)
                            FR_MA   = 3'd4    // captured in MA (STS FPUL,Rn)
                           } frrd_e;

  // FP results by producer structure
  typedef enum logic [2:0] {FP_NONE = 3'd0,
                            FP_MOV  = 3'd1,   // LS pipe, value known in D (register copy / constant / sign logic)
                            FP_LOAD = 3'd2,   // LS pipe, value at the end of MA (FP load, LDS.L FPUL)
                            FP_XFER = 3'd3,   // LS pipe, value from the CPU EX stage (LDS Rm,FPUL)
                            FP_FE   = 3'd4    // FPU pipe (F1 F2 FS, sequencers), written at FS
                           } fprod_e;

  // multi-cycle sequences (the D stage issues one micro-op per cycle)
  typedef enum logic [3:0] {SQ_1 = 4'd0, SQ_MUL, SQ_MAC, SQ_SX2, SQ_SX3, SQ_LDCGBR, SQ_LDCSR, SQ_JMP, SQ_RTE,
                            SQ_TSTB, SQ_BOPB, SQ_TASB, SQ_FCMPD} seq_e;

  typedef struct packed {
    kls_e        cls;
    logic [2:0]  k;           // D-stage micro-operations
    seq_e        seq;
    // control transfer
    logic        br_pc;       // PC-relative branch (target from the D-stage adder)
    logic        br_reg;      // JMP JSR BRAF BSRF RTS RTE (target from a register, known in EX of micro-op 0)
    logic        br_dly;
    logic        br_cond;     // BT BF BT/S BF/S
    logic        br_t;        // taken when T = 1 (BT, BT/S)
    logic        refetch;     // LDC SR / LDC.L SR
    logic        sleep;
    logic        trap;        // TRAPA / unknown opcode (halts the core with an error)
    // CPU
    gres_e       gres;        // stage of the GPR results in gw (gwl is GR_MA, gwu GR_EX)
    logic        mov;         // MOV Rm,Rn: a follower may take the source value in D
    logic        gld;         // integer load into a GPR: its data is aligned by the barrel shifter in S
    logic        shad;        // SHAD / SHLD: use the barrel shifter in EX
    logic        rd_t;        // reads T in D (EX T users, conditional branches)
    logic        wr_t;        // writes T
    logic        t_fpu;       // T comes from the FPU (FCMP): end of F1, no bypass to D
    logic        wr_flags;    // writes T / S / Q / M (architectural SR at S)
    logic        stc_sr;      // reads the architectural SR (STC SR / STC.L SR)
    logic        sx_w;        // system-register write (tag rides the first micro-op)
    logic        sx_r;        // system-register read in D (leader slot only)
    // FPU
    logic        fpu_dec;     // decoded by the FPU: needs the FPSCR mode
    logic        fe;          // FE dispatch (FPU issue port)
    logic        fe_arith;    // enters F1 (FE arithmetic single or double)
    logic        arr;         // needs the array slot (vector ops, MAC writers, FRCHG / FSCHG)
    logic        fp_ls;       // an LS-pipe FP register access
    fprod_e      fprod;       // how the FR / FPUL result is produced
    frrd_e       frs;         // read path of the FR operands (fs)
    frrd_e       urd;         // read path of FPUL
    logic        wr_fpul;
    logic        fscr_rd;     // LDS / STS FPSCR: wait for every FPU op to have committed its cause bits
    logic        fscr_mode;   // writes the FPSCR mode the decoder uses (LDS FPSCR, FRCHG, FSCHG)
    logic        dbl;         // double-precision FE op (F1 multi-pass)
    logic        div;         // FDIV / FSQRT
    logic        vec;         // FIPR FTRV FSCA FSRRA
    logic        mul;         // MUL / DMUL / MAC (multiplier from stage 5 before FS)
    logic        macdef;      // CLRMAC / LDS / LDS.L MACx (enter the multiplier 4 stages before FS)
    logic [1:0]  mac_rd;      // 1 STS MACx,Rn (reads in MA), 2 STS.L MACx (reads in EX)
    // operand cache (the MA stage's cache / UTLB port; hits only, v1 has no cache arrays)
    logic        oc_use;      // uses the port without a data transfer: PREF, OCBI, OCBP, OCBWB (loads / stores are
                              // seen from their micro-op's mrd / mwr)
    logic        tag_wr;      // OCBI / OCBP / LDTLB: the tag (UTLB) write holds the whole pipeline for 2 cycles
    logic        oc_hold;     // OCBWB: keeps the port one cycle longer (an access right behind it waits a cycle)
  } ictl_t;

  function automatic ictl_t v1_ictl(input form_e f);
    ictl_t c;
    c = '0;
    c.k = 3'd1;
    unique case (f)
      F_MOV_RM_RN: begin c.cls = K_MT; c.mov = 1; end
      F_NOP: c.cls = K_MT;
      F_CMP_EQ_IMM_R0, F_CMP_EQ_RM_RN, F_CMP_HS_RM_RN, F_CMP_GE_RM_RN, F_CMP_HI_RM_RN, F_CMP_GT_RM_RN, F_CMP_PZ_RN,
      F_CMP_PL_RN, F_CMP_STR_RM_RN, F_TST_RM_RN, F_TST_IMM_R0, F_CLRT, F_SETT: begin
        c.cls = K_MT; c.wr_t = 1; c.wr_flags = 1;
      end
      F_MOV_IMM_RN, F_MOVA_AT_D_PC_R0, F_SWAP_B_RM_RN, F_SWAP_W_RM_RN, F_XTRCT_RM_RN, F_ADD_RM_RN, F_ADD_IMM_RN,
      F_EXTS_B_RM_RN, F_EXTS_W_RM_RN, F_EXTU_B_RM_RN, F_EXTU_W_RM_RN, F_NEG_RM_RN, F_SUB_RM_RN, F_AND_RM_RN,
      F_AND_IMM_R0, F_NOT_RM_RN, F_OR_RM_RN, F_OR_IMM_R0, F_XOR_RM_RN, F_XOR_IMM_R0, F_SHLL2_RN, F_SHLR2_RN,
      F_SHLL8_RN, F_SHLR8_RN, F_SHLL16_RN, F_SHLR16_RN: c.cls = K_EX;
      F_SHAD_RM_RN, F_SHLD_RM_RN: begin c.cls = K_EX; c.shad = 1; end
      F_MOVT_RN: begin c.cls = K_EX; c.rd_t = 1; end
      F_ADDV_RM_RN, F_SUBV_RM_RN, F_DIV0S_RM_RN, F_DIV0U, F_DT_RN, F_ROTL_RN, F_ROTR_RN, F_SHAL_RN, F_SHAR_RN,
      F_SHLL_RN, F_SHLR_RN: begin c.cls = K_EX; c.wr_t = 1; c.wr_flags = 1; end
      F_ADDC_RM_RN, F_SUBC_RM_RN, F_NEGC_RM_RN, F_DIV1_RM_RN, F_ROTCL_RN, F_ROTCR_RN: begin
        c.cls = K_EX; c.rd_t = 1; c.wr_t = 1; c.wr_flags = 1;
      end
      F_MOV_W_AT_D_PC_RN, F_MOV_L_AT_D_PC_RN, F_MOV_B_AT_RM_RN, F_MOV_W_AT_RM_RN, F_MOV_L_AT_RM_RN,
      F_MOV_B_AT_RM_INC_RN, F_MOV_W_AT_RM_INC_RN, F_MOV_L_AT_RM_INC_RN, F_MOV_B_AT_D_RM_R0, F_MOV_W_AT_D_RM_R0,
      F_MOV_L_AT_D_RM_RN, F_MOV_B_AT_R0_RM_RN, F_MOV_W_AT_R0_RM_RN, F_MOV_L_AT_R0_RM_RN: begin
        c.cls = K_LS; c.gld = 1;
      end
      F_MOV_B_AT_D_GBR_R0, F_MOV_W_AT_D_GBR_R0, F_MOV_L_AT_D_GBR_R0: begin c.cls = K_LS; c.gld = 1; c.sx_r = 1; end
      F_MOV_B_R0_AT_D_GBR, F_MOV_W_R0_AT_D_GBR, F_MOV_L_R0_AT_D_GBR: begin c.cls = K_LS; c.sx_r = 1; end
      F_MOV_B_RM_AT_RN, F_MOV_W_RM_AT_RN, F_MOV_L_RM_AT_RN, F_MOV_B_RM_AT_PRE_RN, F_MOV_W_RM_AT_PRE_RN,
      F_MOV_L_RM_AT_PRE_RN, F_MOV_B_R0_AT_D_RN, F_MOV_W_R0_AT_D_RN, F_MOV_L_RM_AT_D_RN, F_MOV_B_RM_AT_R0_RN,
      F_MOV_W_RM_AT_R0_RN, F_MOV_L_RM_AT_R0_RN, F_MOVCA_L_R0_AT_RN: c.cls = K_LS;
      F_PREF_AT_RN: begin c.cls = K_LS; c.oc_use = 1; end
      F_OCBWB_AT_RN: begin c.cls = K_LS; c.oc_use = 1; c.oc_hold = 1; end
      F_OCBI_AT_RN, F_OCBP_AT_RN: begin c.cls = K_LS; c.oc_use = 1; c.tag_wr = 1; end
      F_TST_B_IMM_AT_R0_GBR: begin c.cls = K_CO; c.k = 3'd3; c.seq = SQ_TSTB; c.sx_r = 1; c.wr_t = 1; c.wr_flags = 1; end
      F_AND_B_IMM_AT_R0_GBR, F_OR_B_IMM_AT_R0_GBR, F_XOR_B_IMM_AT_R0_GBR: begin
        c.cls = K_CO; c.k = 3'd4; c.seq = SQ_BOPB; c.sx_r = 1;
      end
      F_TAS_B_AT_RN: begin c.cls = K_CO; c.k = 3'd5; c.seq = SQ_TASB; c.wr_t = 1; c.wr_flags = 1; end
      // branches
      F_BT: begin c.cls = K_BR; c.br_pc = 1; c.br_cond = 1; c.br_t = 1; c.rd_t = 1; end
      F_BF: begin c.cls = K_BR; c.br_pc = 1; c.br_cond = 1; c.rd_t = 1; end
      F_BT_S: begin c.cls = K_BR; c.br_pc = 1; c.br_cond = 1; c.br_t = 1; c.br_dly = 1; c.rd_t = 1; end
      F_BF_S: begin c.cls = K_BR; c.br_pc = 1; c.br_cond = 1; c.br_dly = 1; c.rd_t = 1; end
      F_BRA_LABEL: begin c.cls = K_BR; c.br_pc = 1; c.br_dly = 1; end
      F_BSR_LABEL: begin c.cls = K_BR; c.br_pc = 1; c.br_dly = 1; c.sx_w = 1; end
      F_JMP_AT_RN, F_BRAF_RN: begin c.cls = K_CO; c.k = 3'd2; c.seq = SQ_JMP; c.br_reg = 1; c.br_dly = 1; end
      F_JSR_AT_RN, F_BSRF_RN: begin c.cls = K_CO; c.k = 3'd2; c.seq = SQ_JMP; c.br_reg = 1; c.br_dly = 1; c.sx_w = 1; end
      F_RTS: begin c.cls = K_CO; c.k = 3'd2; c.seq = SQ_JMP; c.br_reg = 1; c.br_dly = 1; c.sx_r = 1; end
      F_RTE: begin
        c.cls = K_CO; c.k = 3'd5; c.seq = SQ_RTE; c.br_reg = 1; c.br_dly = 1; c.sx_r = 1; c.wr_t = 1; c.wr_flags = 1;
      end
      // control / system registers
      F_CLRS, F_SETS: begin c.cls = K_CO; c.sx_w = 1; c.sx_r = 1; c.wr_flags = 1; end
      F_LDC_RM_SR, F_LDC_L_AT_RM_INC_SR: begin
        c.cls = K_CO; c.k = 3'd4; c.seq = SQ_LDCSR; c.refetch = 1; c.wr_t = 1; c.wr_flags = 1;
      end
      F_LDC_RM_GBR, F_LDC_L_AT_RM_INC_GBR: begin c.cls = K_CO; c.k = 3'd3; c.seq = SQ_LDCGBR; c.sx_w = 1; end
      F_LDC_RM_VBR, F_LDC_RM_SSR, F_LDC_RM_SPC, F_LDC_RM_DBR, F_LDC_RM_RN_BANK, F_LDC_L_AT_RM_INC_VBR,
      F_LDC_L_AT_RM_INC_SSR, F_LDC_L_AT_RM_INC_SPC, F_LDC_L_AT_RM_INC_DBR, F_LDC_L_AT_RM_INC_RN_BANK: begin
        c.cls = K_CO; c.sx_w = 1;
      end
      F_LDS_RM_PR, F_LDS_L_AT_RM_INC_PR: begin c.cls = K_CO; c.k = 3'd2; c.seq = SQ_SX2; c.sx_w = 1; end
      F_STC_SR_RN, F_STC_L_SR_AT_PRE_RN: begin c.cls = K_CO; c.k = 3'd2; c.seq = SQ_SX2; c.sx_r = 1; c.stc_sr = 1; end
      F_STC_GBR_RN, F_STC_VBR_RN, F_STC_SSR_RN, F_STC_SPC_RN, F_STC_DBR_RN, F_STC_RM_BANK_RN, F_STS_PR_RN,
      F_STC_L_GBR_AT_PRE_RN, F_STC_L_VBR_AT_PRE_RN, F_STC_L_SSR_AT_PRE_RN, F_STC_L_SPC_AT_PRE_RN,
      F_STC_L_DBR_AT_PRE_RN, F_STC_L_RM_BANK_AT_PRE_RN, F_STS_L_PR_AT_PRE_RN: begin
        c.cls = K_CO; c.k = 3'd2; c.seq = SQ_SX2; c.sx_r = 1;
      end
      F_STC_SGR_RN, F_STC_L_SGR_AT_PRE_RN: begin c.cls = K_CO; c.k = 3'd3; c.seq = SQ_SX3; c.sx_r = 1; end
      F_LDTLB: begin c.cls = K_CO; c.oc_use = 1; c.tag_wr = 1; end        // the UTLB write freezes like a tag write
      // multiplier and MACH / MACL
      F_MUL_L_RM_RN, F_MULS_W_RM_RN, F_MULU_W_RM_RN, F_DMULS_L_RM_RN, F_DMULU_L_RM_RN: begin
        c.cls = K_CO; c.k = 3'd2; c.seq = SQ_MUL; c.arr = 1; c.mul = 1;
      end
      F_MAC_L_AT_RM_INC_AT_RN_INC, F_MAC_W_AT_RM_INC_AT_RN_INC: begin
        c.cls = K_CO; c.k = 3'd2; c.seq = SQ_MAC; c.arr = 1; c.mul = 1;
      end
      F_CLRMAC, F_LDS_RM_MACH, F_LDS_RM_MACL, F_LDS_L_AT_RM_INC_MACH, F_LDS_L_AT_RM_INC_MACL: begin
        c.cls = K_CO; c.arr = 1; c.macdef = 1;
      end
      F_STS_MACH_RN, F_STS_MACL_RN: begin c.cls = K_CO; c.mac_rd = 2'd1; c.gres = GR_S; end
      F_STS_L_MACH_AT_PRE_RN, F_STS_L_MACL_AT_PRE_RN: begin c.cls = K_CO; c.mac_rd = 2'd2; end
      // FP moves, loads, stores (LS pipe)
      F_FLDI0_FRN, F_FLDI1_FRN, F_FMOV_FRM_FRN, F_FABS_FRN, F_FNEG_FRN, F_FMOV_XDM_XDN, F_FABS_DRN, F_FNEG_DRN,
      F_FMOV_DRM_DRN, F_FMOV_DRM_XDN, F_FMOV_XDM_DRN: begin
        c.cls = K_LS; c.fpu_dec = 1; c.fp_ls = 1; c.fprod = FP_MOV; c.frs = FR_D;
      end
      F_FSTS_FPUL_FRN: begin c.cls = K_LS; c.fpu_dec = 1; c.fp_ls = 1; c.fprod = FP_MOV; c.urd = FR_D; end
      F_FMOV_S_AT_RM_FRN, F_FMOV_S_AT_R0_RM_FRN, F_FMOV_S_AT_RM_INC_FRN, F_FMOV_AT_RM_DRN, F_FMOV_AT_R0_RM_DRN,
      F_FMOV_AT_RM_INC_DRN, F_FMOV_AT_RM_XDN, F_FMOV_AT_RM_INC_XDN, F_FMOV_AT_R0_RM_XDN: begin
        c.cls = K_LS; c.fpu_dec = 1; c.fp_ls = 1; c.fprod = FP_LOAD;
      end
      F_FMOV_S_FRM_AT_RN, F_FMOV_S_FRM_AT_PRE_RN, F_FMOV_S_FRM_AT_R0_RN, F_FMOV_DRM_AT_RN, F_FMOV_DRM_AT_PRE_RN,
      F_FMOV_DRM_AT_R0_RN, F_FMOV_XDM_AT_RN, F_FMOV_XDM_AT_PRE_RN, F_FMOV_XDM_AT_R0_RN: begin
        c.cls = K_LS; c.fpu_dec = 1; c.fp_ls = 1; c.frs = FR_EX;
      end
      F_FLDS_FRM_FPUL: begin c.cls = K_LS; c.fpu_dec = 1; c.fp_ls = 1; c.frs = FR_D; c.fprod = FP_MOV; c.wr_fpul = 1; end
      // FPUL / FPSCR transfers
      F_LDS_RM_FPUL: begin c.cls = K_LS; c.fprod = FP_XFER; c.wr_fpul = 1; end
      F_LDS_L_AT_RM_INC_FPUL: begin c.cls = K_CO; c.fprod = FP_LOAD; c.wr_fpul = 1; end
      F_STS_FPUL_RN: begin c.cls = K_LS; c.urd = FR_MA; c.gres = GR_S; end
      F_STS_L_FPUL_AT_PRE_RN: begin c.cls = K_CO; c.urd = FR_EX; end
      F_LDS_RM_FPSCR, F_LDS_L_AT_RM_INC_FPSCR: begin c.cls = K_CO; c.fpu_dec = 1; c.fscr_rd = 1; c.fscr_mode = 1; end
      F_STS_FPSCR_RN: begin c.cls = K_CO; c.fpu_dec = 1; c.fscr_rd = 1; c.gres = GR_S; end
      F_STS_L_FPSCR_AT_PRE_RN: begin c.cls = K_CO; c.fpu_dec = 1; c.fscr_rd = 1; end
      // FE single precision
      F_FADD_FRM_FRN, F_FSUB_FRM_FRN, F_FMUL_FRM_FRN, F_FMAC_FR0_FRM_FRN, F_FCMP_EQ_FRM_FRN, F_FCMP_GT_FRM_FRN: begin
        c.cls = K_FE; c.fpu_dec = 1; c.fe = 1; c.fe_arith = 1; c.fprod = FP_FE; c.frs = FR_D;
        if (f == F_FCMP_EQ_FRM_FRN || f == F_FCMP_GT_FRM_FRN) begin c.wr_t = 1; c.t_fpu = 1; c.wr_flags = 1; c.fprod = FP_NONE; end
      end
      F_FLOAT_FPUL_FRN: begin c.cls = K_FE; c.fpu_dec = 1; c.fe = 1; c.fe_arith = 1; c.fprod = FP_FE; c.urd = FR_D; end
      F_FTRC_FRM_FPUL: begin
        c.cls = K_FE; c.fpu_dec = 1; c.fe = 1; c.fe_arith = 1; c.fprod = FP_FE; c.frs = FR_D; c.wr_fpul = 1;
      end
      F_FDIV_FRM_FRN, F_FSQRT_FRN: begin
        c.cls = K_FE; c.fpu_dec = 1; c.fe = 1; c.fe_arith = 1; c.fprod = FP_FE; c.frs = FR_D; c.div = 1;
      end
      F_FRCHG, F_FSCHG: begin c.cls = K_FE; c.fpu_dec = 1; c.fe = 1; c.arr = 1; c.fscr_mode = 1; end
      F_FIPR_FVM_FVN, F_FTRV_XMTRX_FVN, F_FSRRA_FRN: begin
        c.cls = K_FE; c.fpu_dec = 1; c.fe = 1; c.arr = 1; c.vec = 1; c.fprod = FP_FE; c.frs = FR_RF;
      end
      F_FSCA_FPUL_DRN: begin
        c.cls = K_FE; c.fpu_dec = 1; c.fe = 1; c.arr = 1; c.vec = 1; c.fprod = FP_FE; c.urd = FR_RF;
      end
      // double precision
      F_FADD_DRM_DRN, F_FSUB_DRM_DRN, F_FMUL_DRM_DRN: begin
        c.cls = K_FE; c.fpu_dec = 1; c.fe = 1; c.fe_arith = 1; c.dbl = 1; c.fprod = FP_FE; c.frs = FR_RF;
      end
      F_FLOAT_FPUL_DRN, F_FCNVSD_FPUL_DRN: begin
        c.cls = K_FE; c.fpu_dec = 1; c.fe = 1; c.fe_arith = 1; c.dbl = 1; c.fprod = FP_FE; c.urd = FR_RF;
      end
      F_FTRC_DRM_FPUL, F_FCNVDS_DRM_FPUL: begin
        c.cls = K_FE; c.fpu_dec = 1; c.fe = 1; c.fe_arith = 1; c.dbl = 1; c.fprod = FP_FE; c.frs = FR_RF; c.wr_fpul = 1;
      end
      F_FDIV_DRM_DRN, F_FSQRT_DRN: begin
        c.cls = K_FE; c.fpu_dec = 1; c.fe = 1; c.fe_arith = 1; c.dbl = 1; c.div = 1; c.fprod = FP_FE; c.frs = FR_RF;
      end
      F_FCMP_EQ_DRM_DRN, F_FCMP_GT_DRM_DRN: begin
        c.cls = K_CO; c.k = 3'd2; c.seq = SQ_FCMPD; c.fpu_dec = 1; c.fe = 1; c.fe_arith = 1; c.dbl = 1; c.frs = FR_RF;
        c.wr_t = 1; c.t_fpu = 1; c.wr_flags = 1;
      end
      default: begin c.cls = K_CO; c.trap = 1; end
    endcase
    // the result stage of the "other destination" GPR role
    if (f == F_STC_SR_RN || f == F_STC_GBR_RN || f == F_STC_VBR_RN || f == F_STC_SSR_RN || f == F_STC_SPC_RN ||
        f == F_STC_DBR_RN || f == F_STC_RM_BANK_RN || f == F_STS_PR_RN || f == F_STC_SGR_RN)
      c.gres = GR_EX;
    return c;
  endfunction

  // physical GPR number: R0-R7 of bank 0 are 0-7, of bank 1 8-15, R8-R15 are 16-23.  rb = SR.MD & SR.RB.
  function automatic logic [4:0] gphys(input logic [3:0] r, input logic rb);
    return r[3] ? {2'b10, r[2:0]} : {1'b0, rb, r[2:0]};
  endfunction
  function automatic logic [23:0] gmask(input logic [15:0] m, input logic rb);
    logic [23:0] p;
    p = '0;
    for (int i = 0; i < 16; i++) if (m[i]) p[gphys(4'(i), rb)] = 1'b1;
    return p;
  endfunction
  // STC.L / STS.L PR: stores in its last micro-op
  function automatic logic stcl_form(input form_e f);
    return f == F_STC_L_SR_AT_PRE_RN || f == F_STC_L_GBR_AT_PRE_RN || f == F_STC_L_VBR_AT_PRE_RN ||
           f == F_STC_L_SSR_AT_PRE_RN || f == F_STC_L_SPC_AT_PRE_RN || f == F_STC_L_SGR_AT_PRE_RN ||
           f == F_STC_L_DBR_AT_PRE_RN || f == F_STC_L_RM_BANK_AT_PRE_RN || f == F_STS_L_PR_AT_PRE_RN;
  endfunction

  // the other bank of R0-R7 (LDC / STC Rn_BANK)
  function automatic logic [4:0] gbank(input logic [2:0] r, input logic rb);
    return {1'b0, !rb, r};
  endfunction

  // ---- one micro-operation in an integer pipe latch (EX, MA, S; two lanes: 0 = older)
  // The operand fields are the instruction's register fields read in D (a = R[op[7:4]], b = R[op[11:8]], c = R0),
  // corrected in EX by the bypass; each stage uses what the form needs.  Results carry the stage they appear in: w1 is
  // produced in EX (or MA for the FPU / MAC transfers, gres = GR_S), w2 is load data (end of MA).
  typedef struct packed {
    logic        v;
    logic [31:0] pc;
    logic [15:0] op;
    form_e       form;
    logic [2:0]  step;
    logic        lane;        // issue lane of the instruction (0 = older in its issue cycle)
    logic        rb;          // SR.MD & SR.RB at D (bank of R0-R7)
    logic [2:0]  md;          // FPSCR FR / SZ / PR at D
    // hazard tags this micro-op carries
    logic        flagop;      // writes the architectural SR flags at S
    logic        sxw;         // system-register write tag
    logic        gld;         // integer GPR load (aligned by the shifter in S)
    // operands
    logic [4:0]  pa, pb, pc0; // physical register numbers of a, b, c
    logic [31:0] a, b, c;
    logic [31:0] cr;          // system register read in D (STC / STS / RTS / RTE / bank register)
    logic        tin, qin, min, sin;   // flags read in D (T with the EX bypass)
    // results
    logic        w1;  gres_e w1k;  logic [4:0] w1r;  logic [31:0] w1v;
    logic        w2;  logic [4:0] w2r;  logic [31:0] w2v;
    logic        tw;  logic tv;   logic qmw; logic qv, mv;   logic sw; logic sv;
    // memory access (in MA)
    logic        mrd, mwr;    logic [1:0] msz;  logic msx;  logic [31:0] maddr;  logic [63:0] mwd;
    // system-register write at S
    logic [3:0]  crw;         // CR_* below
    logic [31:0] crv;
    logic [4:0]  bkr;         // CR_BANK: the other-bank register (physical GPR number) that crv goes to
    // LS-pipe FP results (written at S through the FPU's LS port)
    fprod_e      fk;          // how the value is produced (MOV known in EX, LOAD at the end of MA, XFER from EX)
    logic [31:0] fwm;         // FR words written (physical: bank * 16 + i)
    logic [31:0] fwe, fwo;    // value of the even / odd word (a single word uses its own parity)
    logic        uw;          // FPUL written
    logic [31:0] uv;
    logic        fscw;        // FPSCR written (LDS / LDS.L FPSCR)
    logic [31:0] fscv;
  } uop_t;

  localparam logic [3:0] CR_NONE = 4'd0, CR_SR = 4'd1, CR_GBR = 4'd2, CR_VBR = 4'd3, CR_SSR = 4'd4, CR_SPC = 4'd5,
                         CR_DBR = 4'd6, CR_PR = 4'd7, CR_BANK = 4'd8;
endpackage
