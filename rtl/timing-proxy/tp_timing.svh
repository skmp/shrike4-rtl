// tp_timing.svh -- per-form timing control of the proxy (included in tp_pkg): the decoder's control ROM.
//
// Every number is measured on the console by the timings2 sweep (model/timings2; derivation in README.md) or bounded
// by it where its loops cannot see the exact value; the manual's table was vocabulary only.  The free choices are
// marked in-line "hidden:" with the range the sweep allows.  Latencies are issue-to-issue distances counted from the
// producer's FIRST issue cycle: a consumer may issue L cycles later (0 = it may issue as the second instruction of
// the same pair).
//
// How tp_core uses them.  It keeps no latency counters for registers: results are destination tags in the pipeline
// stages (EX / MA, E1 / E2, F1 / F2, the pass schedule, the divider), so the latency of a result to each reader kind
// follows from the producer kind (move, load, FE op, vector / double op, divide) and, for vector and double
// operations, the register-file write time FS of each word (f_rf, u_rf), whose passes it schedules.  The per-reader
// columns below (f_rd / f_st / f_wr, u_*) are those same measured latencies as each reader kind sees them; the core
// reads them only as "non-zero", for the same-cycle pairing of a follower with its leader.  Structural holds (set_*,
// lat_mac, lat_fpscr_*, sx window) load small counters directly.

typedef enum logic [2:0] {C_MT = 3'd0, C_EX = 3'd1, C_LS = 3'd2, C_FE = 3'd3, C_CO = 3'd4, C_BR = 3'd5} cls_e;

// FPUL read path of a consumer
typedef enum logic [2:0] {U_NONE = 3'd0, U_D = 3'd1, U_RF = 3'd2, U_ST = 3'd3, U_STS = 3'd4} ukind_e;

typedef struct packed {
  cls_e        cls;          // issue class: MT pairs with all but CO; EX / LS / FE not with themselves; CO alone;
                             // BR (PC-relative branches) like MT, one branch per pair
  logic [2:0]  k;            // D-stage cycles (issue cycles); > 1 never pairs
  // control transfer
  logic        br_pc;        // BT BF BT/S BF/S BRA BSR: target from the D-stage adder, redirect in the issue cycle
  logic        br_reg;       // JMP JSR BRAF BSRF RTS RTE: redirect in the last D cycle
  logic        br_dly;       // has a delay slot
  logic        refetch;      // LDC(.L) SR: the next instruction is refetched after the last D cycle
  // memory pipe (operand cache and UTLB, the MA stage's resources)
  logic        mem1;         // uses them in its first issue cycle: every load / store / prefetch / cache-block op,
                             // the CO loads and single-cycle CO stores, LDTLB; not STC.L / STS.L PR (store in cycle 2)
  logic        oc_busy;      // OCBWB: holds the operand cache one more cycle; a mem1 instruction issued in the next
                             // cycle freezes the pipeline for a cycle when it reaches MA
  logic [1:0]  frz_n;        // cycles of whole-pipeline freeze after the issue cycle (tag / UTLB write): LDTLB,
                             // OCBI, OCBP 2
  // GPR results (roles gw / gwl / gwu of tp_decode): gwl (a load, value from MA) is always 2, gwu 1
  logic [2:0]  lat_gw;       // 0 MOV Rm,Rn; 1 ALU; 3 STS FPUL / FPSCR / MACH / MACL,Rn (value from write-back);
                             // k for STC / STS PR (hidden: any 1..k, D is busy until k)
  logic        shad;         // SHAD / SHLD (read their shift amount in the cycle an older integer load spends in MA)
  // T and the SR flags: T is on the integer flag bypass the next cycle; an FCMP is a cycle late (it is in F1)
  logic        rd_t;         // reads T over the flag bypass (EX T users, conditional branches)
  logic        wr_t;
  logic        wr_flags;     // writes T / S / Q / M: STC SR / STC.L SR read the architectural SR, written at the
                             // end of MA of the writer's last issue cycle (latency k + 2)
  logic        stc_sr;       // STC SR / STC.L SR
  // system (control) registers: coarse SX interlock
  logic        sx_w;         // writer: SSR SPC DBR VBR GBR Rn_BANK S PR (incl. BSR / JSR / BSRF)
  logic        sx_r;         // reader: STC*, STS PR*, RTS, RTE, CLRS, SETS, @(R0,GBR) ops, @(disp,GBR) MOVs
  // FPU
  logic        fpu_dec;      // decoded by the FPU (reads the FPSCR mode): FE group, FP moves, LDS / STS FPSCR
  logic        fe;           // FE group: held while the multiplier's busy count is > 2, and by the divider's
                             // FE-issue inhibit (the cycle before each of its result passes)
  logic        fe_arith;     // FE arithmetic (FADD..FTRC, FCMP, FDIV, FSQRT, doubles): held by the set_fe hold
  logic        arr;          // multiplier-array class (FIPR FTRV FSCA FSRRA, MUL/DMUL/MAC, CLRMAC, LDS MACx,
                             // FRCHG, FSCHG): held by the set_arr hold
  logic        rf_rd;        // FR operands read from the register file only (vector ops, doubles)
  ukind_e      fpul_rd;
  logic [2:0]  fw_width;     // destination width in words (1, 2, 4): position of a word = index % width
  logic [3:0][4:0] f_rd;     // per destination position: to bypass readers (FE ops, FP moves)
  logic [3:0][4:0] f_st;     //   to FP store data (the late capture of the FE result bus)
  logic [3:0][4:0] f_rf;     //   to register-file readers = FS, the word's register-file write
  logic [3:0][4:0] f_wr;     //   to a later writer of the word (write order)
  logic [1:0]  f_war;        // 2: a later writer of one of this instruction's source words waits a cycle (the
                             // sources are read in F1)
  logic [4:0]  u_rd, u_st, u_sts, u_rf, u_wr;   // FPUL result by reader kind (D, store, STS FPUL,Rn, RF) / WAW
  logic [4:0]  lat_fpscr_c;  // FPSCR cause / flags to LDS / STS FPSCR (0: not written): written a cycle after the
                             // last FS
  logic [2:0]  lat_fpscr_m;  // FPSCR mode (FR / SZ / PR) to every FPU-decoded instruction (0: not written)
  logic        rd_fpscr_c;   // LDS / LDS.L / STS / STS.L FPSCR
  // integer multiplier / MACH:MACL (one busy counter; the multiplier is the FPU's array)
  logic [2:0]  lat_mac;      // 6 multiply, 5 CLRMAC / LDS MACx (0: not a MAC writer)
  logic [1:0]  mac_rd;       // 1 STS MACx,Rn (may issue at busy <= 2), 2 STS.L MACx (busy <= 1)
  // FPU dispatch holds this instruction sets (issue distances; 0 = none)
  logic [4:0]  set_arr;      // array class may issue at this distance (the vector sequencer's passes)
  logic [4:0]  set_fe;       // FE arithmetic may issue at this distance: one pass longer after a vector op, whose
                             // last pass shares the FE adder / FS write port
  logic [4:0]  set_div;      // the divider's release: next FDIV / FSQRT.  Its result passes come the cycle(s)
                             // before (one; two for PR = 1, lower word first) on the divider's own result path
  logic        ftrv;         // FTRV: group lock and XMTRX anti-dependency
  logic        fp_ls;        // an LS-pipe FP access (FP moves / loads / stores / FLDS / FSTS / FABS / FNEG)
} tc_t;

function automatic logic [3:0][4:0] v4(input int a, input int b, input int c, input int d);
  return {5'(d), 5'(c), 5'(b), 5'(a)};
endfunction

function automatic tc_t tp_timing(input form_e f);
  tc_t t;
  t = '0;
  t.k = 3'd1;
  t.fw_width = 3'd1;
  unique case (f)
    // ---- integer MT ------------------------------------------------------------------------------------------
    F_MOV_RM_RN: begin t.cls = C_MT; t.lat_gw = 3'd0; end
    F_NOP: t.cls = C_MT;
    F_CMP_EQ_IMM_R0, F_CMP_EQ_RM_RN, F_CMP_HS_RM_RN, F_CMP_GE_RM_RN, F_CMP_HI_RM_RN, F_CMP_GT_RM_RN, F_CMP_PZ_RN,
    F_CMP_PL_RN, F_CMP_STR_RM_RN, F_TST_RM_RN, F_TST_IMM_R0, F_CLRT, F_SETT: begin
      t.cls = C_MT; t.wr_t = 1; t.wr_flags = 1;
    end
    // ---- integer EX ------------------------------------------------------------------------------------------
    F_MOV_IMM_RN, F_MOVA_AT_D_PC_R0, F_SWAP_B_RM_RN, F_SWAP_W_RM_RN, F_XTRCT_RM_RN, F_ADD_RM_RN, F_ADD_IMM_RN,
    F_EXTS_B_RM_RN, F_EXTS_W_RM_RN, F_EXTU_B_RM_RN, F_EXTU_W_RM_RN, F_NEG_RM_RN, F_SUB_RM_RN, F_AND_RM_RN,
    F_AND_IMM_R0, F_NOT_RM_RN, F_OR_RM_RN, F_OR_IMM_R0, F_XOR_RM_RN, F_XOR_IMM_R0, F_SHLL2_RN, F_SHLR2_RN,
    F_SHLL8_RN, F_SHLR8_RN, F_SHLL16_RN, F_SHLR16_RN: begin t.cls = C_EX; t.lat_gw = 3'd1; end
    F_SHAD_RM_RN, F_SHLD_RM_RN: begin t.cls = C_EX; t.lat_gw = 3'd1; t.shad = 1; end
    F_MOVT_RN: begin t.cls = C_EX; t.lat_gw = 3'd1; t.rd_t = 1; end
    F_ADDV_RM_RN, F_SUBV_RM_RN, F_DIV0S_RM_RN, F_DIV0U, F_DT_RN, F_ROTL_RN, F_ROTR_RN, F_SHAL_RN, F_SHAR_RN,
    F_SHLL_RN, F_SHLR_RN: begin
      t.cls = C_EX; t.lat_gw = 3'd1; t.wr_t = 1; t.wr_flags = 1;
    end
    F_ADDC_RM_RN, F_SUBC_RM_RN, F_NEGC_RM_RN, F_DIV1_RM_RN, F_ROTCL_RN, F_ROTCR_RN: begin
      t.cls = C_EX; t.lat_gw = 3'd1; t.rd_t = 1; t.wr_t = 1; t.wr_flags = 1;
    end
    // ---- integer LS ------------------------------------------------------------------------------------------
    F_MOV_W_AT_D_PC_RN, F_MOV_L_AT_D_PC_RN, F_MOV_B_AT_RM_RN, F_MOV_W_AT_RM_RN, F_MOV_L_AT_RM_RN,
    F_MOV_B_AT_RM_INC_RN, F_MOV_W_AT_RM_INC_RN, F_MOV_L_AT_RM_INC_RN, F_MOV_B_AT_D_RM_R0, F_MOV_W_AT_D_RM_R0,
    F_MOV_L_AT_D_RM_RN, F_MOV_B_AT_R0_RM_RN, F_MOV_W_AT_R0_RM_RN, F_MOV_L_AT_R0_RM_RN: begin
      t.cls = C_LS; t.mem1 = 1;
    end
    F_MOV_B_AT_D_GBR_R0, F_MOV_W_AT_D_GBR_R0, F_MOV_L_AT_D_GBR_R0: begin
      t.cls = C_LS; t.mem1 = 1; t.sx_r = 1;
    end
    F_MOV_B_R0_AT_D_GBR, F_MOV_W_R0_AT_D_GBR, F_MOV_L_R0_AT_D_GBR: begin t.cls = C_LS; t.mem1 = 1; t.sx_r = 1; end
    F_MOV_B_RM_AT_RN, F_MOV_W_RM_AT_RN, F_MOV_L_RM_AT_RN, F_MOV_B_RM_AT_PRE_RN, F_MOV_W_RM_AT_PRE_RN,
    F_MOV_L_RM_AT_PRE_RN, F_MOV_B_R0_AT_D_RN, F_MOV_W_R0_AT_D_RN, F_MOV_L_RM_AT_D_RN, F_MOV_B_RM_AT_R0_RN,
    F_MOV_W_RM_AT_R0_RN, F_MOV_L_RM_AT_R0_RN, F_PREF_AT_RN, F_MOVCA_L_R0_AT_RN: begin t.cls = C_LS; t.mem1 = 1; end
    // ---- read-modify-write on @(R0,GBR): CO, 3 / 4 D cycles ----------------------------------------------------
    F_TST_B_IMM_AT_R0_GBR: begin     // T after its 3 D cycles (hidden: any T latency 1..3)
      t.cls = C_CO; t.k = 3'd3; t.mem1 = 1; t.sx_r = 1; t.wr_t = 1; t.wr_flags = 1;
    end
    F_AND_B_IMM_AT_R0_GBR, F_OR_B_IMM_AT_R0_GBR, F_XOR_B_IMM_AT_R0_GBR: begin
      t.cls = C_CO; t.k = 3'd4; t.mem1 = 1; t.sx_r = 1;
    end
    // ---- branches ----------------------------------------------------------------------------------------------
    F_BT, F_BF: begin t.cls = C_BR; t.br_pc = 1; t.rd_t = 1; end
    F_BT_S, F_BF_S: begin t.cls = C_BR; t.br_pc = 1; t.br_dly = 1; t.rd_t = 1; end
    F_BRA_LABEL: begin t.cls = C_BR; t.br_pc = 1; t.br_dly = 1; end
    F_BSR_LABEL: begin t.cls = C_BR; t.br_pc = 1; t.br_dly = 1; t.sx_w = 1; end
    F_JMP_AT_RN, F_BRAF_RN: begin t.cls = C_CO; t.k = 3'd2; t.br_reg = 1; t.br_dly = 1; end
    F_JSR_AT_RN, F_BSRF_RN: begin t.cls = C_CO; t.k = 3'd2; t.br_reg = 1; t.br_dly = 1; t.sx_w = 1; end
    F_RTS: begin t.cls = C_CO; t.k = 3'd2; t.br_reg = 1; t.br_dly = 1; t.sx_r = 1; end
    F_RTE: begin                     // T (from SSR) after its 5 D cycles: its slot reads it at + 5 (bounded: <= 5)
      t.cls = C_CO; t.k = 3'd5; t.br_reg = 1; t.br_dly = 1; t.sx_r = 1; t.wr_t = 1;
    end
    // ---- control / system registers ---------------------------------------------------------------------------
    F_CLRS, F_SETS: begin t.cls = C_CO; t.sx_w = 1; t.sx_r = 1; t.wr_flags = 1; end
    // LDC SR: T after the 4 D cycles and the refetch (bounded: <= 5); it does not open the SX window, LDC GBR does
    // (hidden: either, for both -- their own D cycles cover the window)
    F_LDC_RM_SR: begin t.cls = C_CO; t.k = 3'd4; t.refetch = 1; t.wr_t = 1; end
    F_LDC_L_AT_RM_INC_SR: begin t.cls = C_CO; t.k = 3'd4; t.mem1 = 1; t.refetch = 1; t.wr_t = 1; end
    F_LDC_RM_GBR: begin t.cls = C_CO; t.k = 3'd3; t.sx_w = 1; end
    F_LDC_L_AT_RM_INC_GBR: begin t.cls = C_CO; t.k = 3'd3; t.mem1 = 1; t.sx_w = 1; end
    F_LDC_RM_VBR, F_LDC_RM_SSR, F_LDC_RM_SPC, F_LDC_RM_DBR, F_LDC_RM_RN_BANK: begin t.cls = C_CO; t.sx_w = 1; end
    F_LDC_L_AT_RM_INC_VBR, F_LDC_L_AT_RM_INC_SSR, F_LDC_L_AT_RM_INC_SPC, F_LDC_L_AT_RM_INC_DBR,
    F_LDC_L_AT_RM_INC_RN_BANK: begin t.cls = C_CO; t.mem1 = 1; t.sx_w = 1; end
    F_LDS_RM_PR: begin t.cls = C_CO; t.k = 3'd2; t.sx_w = 1; end
    F_LDS_L_AT_RM_INC_PR: begin t.cls = C_CO; t.k = 3'd2; t.mem1 = 1; t.sx_w = 1; end
    // STC / STS PR,Rn result: lat_gw = k (hidden: any 1..k)
    F_STC_SR_RN: begin t.cls = C_CO; t.k = 3'd2; t.sx_r = 1; t.stc_sr = 1; t.lat_gw = 3'd2; end
    F_STC_GBR_RN, F_STC_VBR_RN, F_STC_SSR_RN, F_STC_SPC_RN, F_STC_DBR_RN, F_STC_RM_BANK_RN, F_STS_PR_RN: begin
      t.cls = C_CO; t.k = 3'd2; t.sx_r = 1; t.lat_gw = 3'd2;
    end
    F_STC_SGR_RN: begin t.cls = C_CO; t.k = 3'd3; t.sx_r = 1; t.lat_gw = 3'd3; end
    F_STC_L_SR_AT_PRE_RN: begin t.cls = C_CO; t.k = 3'd2; t.sx_r = 1; t.stc_sr = 1; end
    F_STC_L_GBR_AT_PRE_RN, F_STC_L_VBR_AT_PRE_RN, F_STC_L_SSR_AT_PRE_RN, F_STC_L_SPC_AT_PRE_RN,
    F_STC_L_DBR_AT_PRE_RN, F_STC_L_RM_BANK_AT_PRE_RN, F_STS_L_PR_AT_PRE_RN: begin
      t.cls = C_CO; t.k = 3'd2; t.sx_r = 1;
    end
    F_STC_L_SGR_AT_PRE_RN: begin t.cls = C_CO; t.k = 3'd3; t.sx_r = 1; end
    // LDTLB: one issue cycle, then the UTLB write freezes the whole pipeline for 2 cycles (as OCBI / OCBP do)
    F_LDTLB: begin t.cls = C_CO; t.mem1 = 1; t.frz_n = 2'd2; end
    // ---- multiplier and MACH / MACL ---------------------------------------------------------------------------
    F_MUL_L_RM_RN, F_MULS_W_RM_RN, F_MULU_W_RM_RN, F_DMULS_L_RM_RN, F_DMULU_L_RM_RN: begin
      t.cls = C_CO; t.k = 3'd2; t.arr = 1; t.lat_mac = 3'd6;
    end
    F_MAC_L_AT_RM_INC_AT_RN_INC, F_MAC_W_AT_RM_INC_AT_RN_INC: begin
      t.cls = C_CO; t.k = 3'd2; t.mem1 = 1; t.arr = 1; t.lat_mac = 3'd6;
    end
    F_CLRMAC, F_LDS_RM_MACH, F_LDS_RM_MACL: begin t.cls = C_CO; t.arr = 1; t.lat_mac = 3'd5; end
    F_LDS_L_AT_RM_INC_MACH, F_LDS_L_AT_RM_INC_MACL: begin t.cls = C_CO; t.mem1 = 1; t.arr = 1; t.lat_mac = 3'd5; end
    F_STS_MACH_RN, F_STS_MACL_RN: begin t.cls = C_CO; t.mac_rd = 2'd1; t.lat_gw = 3'd3; end
    F_STS_L_MACH_AT_PRE_RN, F_STS_L_MACL_AT_PRE_RN: begin t.cls = C_CO; t.mem1 = 1; t.mac_rd = 2'd2; end
    // ---- cache-block operations (validated where the console measured them deterministically) ------------------
    // OCBI / OCBP: an LS instruction that pairs as usual, then the tag write freezes the whole pipeline (fetch
    // included) for 2 cycles; OCBWB: plain LS, but a mem1 instruction issued right behind it costs a 1-cycle freeze.
    // Their bus-side time (a dirty line written back, a store after OCBP) varies and is not modelled.
    F_OCBI_AT_RN, F_OCBP_AT_RN: begin t.cls = C_LS; t.mem1 = 1; t.frz_n = 2'd2; end
    F_OCBWB_AT_RN: begin t.cls = C_LS; t.mem1 = 1; t.oc_busy = 1; end
    // ---- out of scope: a bus-locked read-modify-write whose time the external bus sets (23.3 cycles on average);
    //      the manual's issue cost
    F_TAS_B_AT_RN: begin
      t.cls = C_CO; t.k = 3'd5; t.mem1 = 1; t.wr_t = 1; t.wr_flags = 1;
    end
    // ---- FP moves, loads and stores (LS pipe) ------------------------------------------------------------------
    // "mov" results: bypassed to FE / move readers at once, in the register file 3 cycles after issue; to stores
    // f_st 0 (bounded: <= 1, an LS store never pairs with an LS move)
    F_FLDI0_FRN, F_FLDI1_FRN, F_FMOV_FRM_FRN, F_FABS_FRN, F_FNEG_FRN, F_FABS_DRN, F_FNEG_DRN: begin
      t.cls = C_LS; t.fpu_dec = 1; t.fp_ls = 1; t.f_rf = v4(3, 3, 3, 3);
    end
    F_FMOV_DRM_DRN, F_FMOV_DRM_XDN, F_FMOV_XDM_DRN, F_FMOV_XDM_XDN: begin
      t.cls = C_LS; t.fpu_dec = 1; t.fp_ls = 1; t.fw_width = 3'd2; t.f_rf = v4(3, 3, 3, 3);
    end
    F_FSTS_FPUL_FRN: begin t.cls = C_LS; t.fpu_dec = 1; t.fp_ls = 1; t.fpul_rd = U_D; t.f_rf = v4(3, 3, 3, 3); end
    F_FMOV_S_AT_RM_FRN, F_FMOV_S_AT_R0_RM_FRN, F_FMOV_S_AT_RM_INC_FRN: begin
      t.cls = C_LS; t.mem1 = 1; t.fpu_dec = 1; t.fp_ls = 1;
      t.f_rd = v4(2, 2, 2, 2); t.f_st = v4(2, 2, 2, 2); t.f_rf = v4(3, 3, 3, 3);
    end
    F_FMOV_AT_RM_DRN, F_FMOV_AT_R0_RM_DRN, F_FMOV_AT_RM_INC_DRN, F_FMOV_AT_RM_XDN, F_FMOV_AT_RM_INC_XDN,
    F_FMOV_AT_R0_RM_XDN: begin
      t.cls = C_LS; t.mem1 = 1; t.fpu_dec = 1; t.fp_ls = 1; t.fw_width = 3'd2;
      t.f_rd = v4(2, 2, 2, 2); t.f_st = v4(2, 2, 2, 2); t.f_rf = v4(3, 3, 3, 3);
    end
    F_FMOV_S_FRM_AT_RN, F_FMOV_S_FRM_AT_PRE_RN, F_FMOV_S_FRM_AT_R0_RN, F_FMOV_DRM_AT_RN, F_FMOV_DRM_AT_PRE_RN,
    F_FMOV_DRM_AT_R0_RN, F_FMOV_XDM_AT_RN, F_FMOV_XDM_AT_PRE_RN, F_FMOV_XDM_AT_R0_RN: begin
      t.cls = C_LS; t.mem1 = 1; t.fpu_dec = 1; t.fp_ls = 1;
    end
    // FLDS / LDS Rm,FPUL to STS FPUL,Rn / STS.L FPUL: 0 (bounded: <= 1, LS / LS and LS / CO never pair)
    F_FLDS_FRM_FPUL: begin t.cls = C_LS; t.fpu_dec = 1; t.fp_ls = 1; t.u_rf = 5'd3; end
    // ---- FPUL / FPSCR transfers ----------------------------------------------------------------------------------
    F_LDS_RM_FPUL: begin t.cls = C_LS; t.u_rd = 5'd1; t.u_rf = 5'd3; end
    F_LDS_L_AT_RM_INC_FPUL: begin
      t.cls = C_CO; t.mem1 = 1; t.u_rd = 5'd2; t.u_st = 5'd2; t.u_sts = 5'd2; t.u_rf = 5'd3;
    end
    F_STS_FPUL_RN: begin t.cls = C_LS; t.fpul_rd = U_STS; t.lat_gw = 3'd3; end
    F_STS_L_FPUL_AT_PRE_RN: begin t.cls = C_CO; t.mem1 = 1; t.fpul_rd = U_ST; end
    F_LDS_RM_FPSCR: begin t.cls = C_CO; t.fpu_dec = 1; t.rd_fpscr_c = 1; t.lat_fpscr_m = 3'd4; end
    F_LDS_L_AT_RM_INC_FPSCR: begin t.cls = C_CO; t.mem1 = 1; t.fpu_dec = 1; t.rd_fpscr_c = 1; t.lat_fpscr_m = 3'd4; end
    F_STS_FPSCR_RN: begin t.cls = C_CO; t.fpu_dec = 1; t.rd_fpscr_c = 1; t.lat_gw = 3'd3; end
    F_STS_L_FPSCR_AT_PRE_RN: begin t.cls = C_CO; t.mem1 = 1; t.fpu_dec = 1; t.rd_fpscr_c = 1; end
    // ---- FE single precision ---------------------------------------------------------------------------------
    F_FADD_FRM_FRN, F_FSUB_FRM_FRN, F_FMUL_FRM_FRN, F_FMAC_FR0_FRM_FRN: begin
      t.cls = C_FE; t.fpu_dec = 1; t.fe = 1; t.fe_arith = 1; t.lat_fpscr_c = 5'd4;
      t.f_rd = v4(3, 3, 3, 3); t.f_st = v4(2, 2, 2, 2); t.f_rf = v4(3, 3, 3, 3); t.f_wr = v4(1, 1, 1, 1);
    end
    F_FLOAT_FPUL_FRN: begin
      t.cls = C_FE; t.fpu_dec = 1; t.fe = 1; t.fe_arith = 1; t.lat_fpscr_c = 5'd4; t.fpul_rd = U_D;
      t.f_rd = v4(3, 3, 3, 3); t.f_st = v4(2, 2, 2, 2); t.f_rf = v4(3, 3, 3, 3); t.f_wr = v4(1, 1, 1, 1);
    end
    F_FTRC_FRM_FPUL: begin
      t.cls = C_FE; t.fpu_dec = 1; t.fe = 1; t.fe_arith = 1; t.lat_fpscr_c = 5'd4;
      t.u_rd = 5'd3; t.u_st = 5'd2; t.u_sts = 5'd1; t.u_rf = 5'd3; t.u_wr = 5'd1;
    end
    F_FCMP_EQ_FRM_FRN, F_FCMP_GT_FRM_FRN: begin
      t.cls = C_FE; t.fpu_dec = 1; t.fe = 1; t.fe_arith = 1; t.lat_fpscr_c = 5'd4;
      t.wr_t = 1; t.wr_flags = 1;
    end
    // divides: the result pass is in the divider's rounding stage at set_div, written at set_div + 1: f_rd = f_rf =
    // set_div + 2, stores and later writers set_div + 1; the FE-issue inhibit falls the cycle before (set_div - 1)
    F_FDIV_FRM_FRN: begin
      t.cls = C_FE; t.fpu_dec = 1; t.fe = 1; t.fe_arith = 1; t.lat_fpscr_c = 5'd13;
      t.f_rd = v4(12, 12, 12, 12); t.f_st = v4(11, 11, 11, 11); t.f_rf = v4(12, 12, 12, 12); t.f_wr = v4(11, 11, 11, 11);
      t.set_div = 5'd10;
    end
    F_FSQRT_FRN: begin
      t.cls = C_FE; t.fpu_dec = 1; t.fe = 1; t.fe_arith = 1; t.lat_fpscr_c = 5'd12;
      t.f_rd = v4(11, 11, 11, 11); t.f_st = v4(10, 10, 10, 10); t.f_rf = v4(11, 11, 11, 11); t.f_wr = v4(10, 10, 10, 10);
      t.set_div = 5'd9;
    end
    F_FRCHG, F_FSCHG: begin
      t.cls = C_FE; t.fpu_dec = 1; t.fe = 1; t.arr = 1; t.lat_fpscr_m = 3'd1; t.lat_fpscr_c = 5'd4;
    end
    // ---- vector unit (FIPR 1 array pass, FSRRA 3, FSCA 4, FTRV 4); operands from the register file -------------
    F_FIPR_FVM_FVN: begin
      t.cls = C_FE; t.fpu_dec = 1; t.fe = 1; t.arr = 1; t.rf_rd = 1; t.lat_fpscr_c = 5'd5;
      t.f_rd = v4(4, 4, 4, 4); t.f_st = v4(3, 3, 3, 3); t.f_rf = v4(4, 4, 4, 4); t.f_wr = v4(2, 2, 2, 2);
      t.set_arr = 5'd1; t.set_fe = 5'd2;
    end
    F_FSRRA_FRN: begin
      t.cls = C_FE; t.fpu_dec = 1; t.fe = 1; t.arr = 1; t.rf_rd = 1; t.lat_fpscr_c = 5'd7;
      t.f_rd = v4(6, 6, 6, 6); t.f_st = v4(5, 5, 5, 5); t.f_rf = v4(6, 6, 6, 6); t.f_wr = v4(4, 4, 4, 4);
      t.set_arr = 5'd3; t.set_fe = 5'd4;
    end
    F_FSCA_FPUL_DRN: begin
      t.cls = C_FE; t.fpu_dec = 1; t.fe = 1; t.arr = 1; t.rf_rd = 1; t.fpul_rd = U_RF; t.lat_fpscr_c = 5'd8;
      t.fw_width = 3'd2;
      t.f_rd = v4(6, 7, 6, 7); t.f_st = v4(5, 6, 5, 6); t.f_rf = v4(6, 7, 6, 7); t.f_wr = v4(5, 5, 5, 5);
      t.set_arr = 5'd4; t.set_fe = 5'd5;
    end
    F_FTRV_XMTRX_FVN: begin
      t.cls = C_FE; t.fpu_dec = 1; t.fe = 1; t.arr = 1; t.rf_rd = 1; t.lat_fpscr_c = 5'd8; t.ftrv = 1;
      t.fw_width = 3'd4;
      // element 0 (hidden: behind the group lock, which holds every LS access to the group until 5); write order:
      // the tag covers DR(4n) only (tp_core, the H3 rule)
      t.f_rd = v4(4, 5, 6, 7); t.f_st = v4(3, 4, 5, 6); t.f_rf = v4(4, 5, 6, 7); t.f_wr = v4(5, 5, 5, 5);
      t.set_arr = 5'd4; t.set_fe = 5'd5;
    end
    // ---- double precision (PR = 1): operands from the register file; position 0 = FR(2n), the upper word ------
    F_FADD_DRM_DRN, F_FSUB_DRM_DRN, F_FMUL_DRM_DRN: begin
      t.cls = C_FE; t.fpu_dec = 1; t.fe = 1; t.fe_arith = 1; t.rf_rd = 1; t.lat_fpscr_c = 5'd9; t.fw_width = 3'd2;
      t.f_rd = v4(8, 7, 8, 7); t.f_st = v4(7, 7, 7, 7); t.f_rf = v4(8, 7, 8, 7); t.f_wr = v4(6, 6, 6, 6);
      t.f_war = 2'd2; t.set_arr = 5'd6; t.set_fe = 5'd6;
    end
    F_FLOAT_FPUL_DRN, F_FCNVSD_FPUL_DRN: begin
      t.cls = C_FE; t.fpu_dec = 1; t.fe = 1; t.fe_arith = 1; t.rf_rd = 1; t.fpul_rd = U_RF; t.lat_fpscr_c = 5'd5;
      t.fw_width = 3'd2;
      t.f_rd = v4(4, 3, 4, 3); t.f_st = v4(3, 3, 3, 3); t.f_rf = v4(4, 3, 4, 3); t.f_wr = v4(2, 2, 2, 2);
      t.set_arr = 5'd2; t.set_fe = 5'd2;
    end
    F_FTRC_DRM_FPUL, F_FCNVDS_DRM_FPUL: begin
      t.cls = C_FE; t.fpu_dec = 1; t.fe = 1; t.fe_arith = 1; t.rf_rd = 1; t.lat_fpscr_c = 5'd5;
      t.u_rd = 5'd4; t.u_st = 5'd3; t.u_sts = 5'd3; t.u_rf = 5'd4; t.u_wr = 5'd2;
      t.set_arr = 5'd2; t.set_fe = 5'd2;
    end
    F_FDIV_DRM_DRN: begin
      t.cls = C_FE; t.fpu_dec = 1; t.fe = 1; t.fe_arith = 1; t.rf_rd = 1; t.lat_fpscr_c = 5'd26; t.fw_width = 3'd2;
      t.f_rd = v4(25, 24, 25, 24); t.f_st = v4(24, 24, 24, 24); t.f_rf = v4(25, 24, 25, 24); t.f_wr = v4(24, 24, 24, 24);
      t.set_arr = 5'd2; t.set_fe = 5'd2; t.set_div = 5'd23;   // FE inhibit at 21 / 22 (hidden: n stops at 16)
    end
    F_FSQRT_DRN: begin
      t.cls = C_FE; t.fpu_dec = 1; t.fe = 1; t.fe_arith = 1; t.rf_rd = 1; t.lat_fpscr_c = 5'd25; t.fw_width = 3'd2;
      t.f_rd = v4(24, 23, 24, 23); t.f_st = v4(23, 23, 23, 23); t.f_rf = v4(24, 23, 24, 23); t.f_wr = v4(23, 23, 23, 23);
      t.set_arr = 5'd2; t.set_fe = 5'd2; t.set_div = 5'd22;   // FE inhibit at 20 / 21 (hidden: n stops at 16)
    end
    F_FCMP_EQ_DRM_DRN, F_FCMP_GT_DRM_DRN: begin
      t.cls = C_CO; t.k = 3'd2; t.fpu_dec = 1; t.fe = 1; t.fe_arith = 1; t.rf_rd = 1; t.lat_fpscr_c = 5'd5;
      t.wr_t = 1; t.wr_flags = 1;
    end
    default: t.cls = C_CO;      // F_NONE: an unknown opcode issues alone
  endcase
  return t;
endfunction
