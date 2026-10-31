//
// v1_fpu_timing - standalone synthesis / timing harness around v1_fpu (rtl/v1/v1_fpu.sv): the FPU of v1_core -- the FR
// register file and FPSCR, the FADD / FMUL / FCMP pipes and the double-precision sequencer, the FIPR / FSCA / FSRRA /
// FTRV datapath (fpu_vec_top), the FDIV / FSQRT unit (v1_fdiv), the integer multiplier's MACH / MACL records, the
// FPU-side issue hazards and F1 traps.
//
// Pattern (polly2-rtl's quartus/timming_tests harnesses): every input bit from an HPS-written input register bank (so
// the fitter cannot fold the unit against constant inputs); every output captured RAW (no logic before the flops:
// pure unit timing); the captured vector XOR-folded to one `digest` pin the next cycle (the fold sits off the capture).
//
// Build: cd quartus/timing/v1_fpu && quartus_sh --flow compile v1_fpu
// Fmax:  output_files/v1_fpu.sta.rpt
//
module v1_fpu_timing import tp_pkg::*, v1_pkg::*, v1_soc_pkg::*; (
    input             clk,
    input             reset,
    input             wr_en,
    input      [12:0] wr_addr,
    input      [31:0] wr_data,
    output reg        digest
);
    // ---- input register bank, flattened (offsets below; every two-lane port as [0], [1])
    localparam integer W_DEC  = $bits(dec_t);
    localparam integer W_ICTL = $bits(ictl_t);
    localparam integer W_UOP  = $bits(uop_t);
    localparam integer O_SD   = 0;                       // s_d[2]
    localparam integer O_SC   = O_SD + 2 * W_DEC;        // s_c[2]
    localparam integer O_SOP  = O_SC + 2 * W_ICTL;       // s_op[2]
    localparam integer O_EX   = O_SOP + 2 * 16;          // ex[2]
    localparam integer O_MA   = O_EX + 2 * W_UOP;        // ma[2]
    localparam integer O_SL   = O_MA + 2 * W_UOP;        // sl[2]
    localparam integer O_EXR  = O_SL + 2 * W_UOP;        // ex_res[2]
    localparam integer O_DISN = O_EXR + 2 * W_UOP;       // di_isn[2]
    localparam integer O_DOP  = O_DISN + 2 * 8;          // di_op[2]
    localparam integer O_DD   = O_DOP + 2 * 16;          // di_d[2]
    localparam integer O_DC   = O_DD + 2 * W_DEC;        // di_c[2]
    localparam integer O_DMD  = O_DC + 2 * W_ICTL;       // di_md[2]
    localparam integer O_FOP  = O_DMD + 2 * 3;           // di_fcmpd_op
    localparam integer O_FMD  = O_FOP + 16;              // di_fcmpd_md
    localparam integer O_DBA  = O_FMD + 3;               // dbg_addr
    localparam integer O_DBW  = O_DBA + 8;               // dbg_wdata
    localparam integer O_DBR  = O_DBW + 32;              // dbg_raddr
    localparam integer O_KISN = O_DBR + 8;               // kill_isn
    localparam integer O_MLA  = O_KISN + 8;              // mac_ld_a
    localparam integer O_MLB  = O_MLA + 32;              // mac_ld_b
    localparam integer O_DEF  = O_MLB + 32;              // def_ld
    localparam integer O_EXS  = O_DEF + 32;              // ex_s
    localparam integer O_BITS = O_EXS + 2;               // 0 rst, 1 hold_pipe, 2 hold_div, 3 dbg_we, 4 / 5 s_v[0 / 1],
                                                         // 6 / 7 di_v[0 / 1], 8 di_fcmpd, 9 kill_v, 10 kill_incl,
                                                         // 11 mac_ld_a_v, 12 mac_ld_b_v, 13 def_ld_v
    localparam integer IN_W   = O_BITS + 14;
    localparam integer NREG   = (IN_W + 31) / 32;
    reg [31:0] in_reg [0:NREG-1];
    always @(posedge clk) begin
        if (reset) begin
            for (int i = 0; i < NREG; i++) in_reg[i] <= 32'd0;
        end else if (wr_en && wr_addr < NREG) begin
            in_reg[wr_addr] <= wr_data;
        end
    end
    reg [NREG*32-1:0] in_flat;
    always @* for (int i = 0; i < NREG; i++) in_flat[i*32 +: 32] = in_reg[i];

    logic        q_s_v [2], q_di_v [2];
    dec_t        q_s_d [2], q_di_d [2];
    ictl_t       q_s_c [2], q_di_c [2];
    logic [15:0] q_s_op [2], q_di_op [2];
    uop_t        q_ex [2], q_ma [2], q_sl [2], q_ex_res [2];
    logic [7:0]  q_di_isn [2];
    logic [2:0]  q_di_md [2];
    always @* begin
        for (int k = 0; k < 2; k++) begin
            q_s_v[k]    = in_flat[O_BITS + 4 + k];
            q_di_v[k]   = in_flat[O_BITS + 6 + k];
            q_s_d[k]    = dec_t'(in_flat[O_SD + k * W_DEC +: W_DEC]);
            q_di_d[k]   = dec_t'(in_flat[O_DD + k * W_DEC +: W_DEC]);
            q_s_c[k]    = ictl_t'(in_flat[O_SC + k * W_ICTL +: W_ICTL]);
            q_di_c[k]   = ictl_t'(in_flat[O_DC + k * W_ICTL +: W_ICTL]);
            q_s_op[k]   = in_flat[O_SOP + k * 16 +: 16];
            q_di_op[k]  = in_flat[O_DOP + k * 16 +: 16];
            q_ex[k]     = uop_t'(in_flat[O_EX + k * W_UOP +: W_UOP]);
            q_ma[k]     = uop_t'(in_flat[O_MA + k * W_UOP +: W_UOP]);
            q_sl[k]     = uop_t'(in_flat[O_SL + k * W_UOP +: W_UOP]);
            q_ex_res[k] = uop_t'(in_flat[O_EXR + k * W_UOP +: W_UOP]);
            q_di_isn[k] = in_flat[O_DISN + k * 8 +: 8];
            q_di_md[k]  = in_flat[O_DMD + k * 3 +: 3];
        end
    end

    // ---- DUT
    logic [31:0]       d_dbg_rdata, d_fpul_now, d_fpscr_now, d_mach_now, d_macl_now;
    logic              d_dbg_hit, d_fdep, d_trap_f1, d_trap_f1_lane;
    logic              d_t_busy, d_flag_busy, d_t_we, d_t_val, d_t_lane, d_idle;
    logic [2:0]        d_md_dec;
    logic              d_fhz [2], d_trap_d [2];
    logic [7:0]        d_fwhy [2];
    logic [7:0]        d_trap_f1_isn;
    commit_rec_t [5:0] d_frec;
    logic [31:0]       d_fr_now [32];
    v1_fpu u_dut (
        .clk(clk), .rst(in_flat[O_BITS + 0]), .hold_pipe(in_flat[O_BITS + 1]), .hold_div(in_flat[O_BITS + 2]),
        .dbg_we(in_flat[O_BITS + 3]), .dbg_addr(in_flat[O_DBA +: 8]), .dbg_wdata(in_flat[O_DBW +: 32]),
        .dbg_raddr(in_flat[O_DBR +: 8]), .dbg_rdata(d_dbg_rdata), .dbg_hit(d_dbg_hit),
        .s_v(q_s_v), .s_d(q_s_d), .s_c(q_s_c), .s_op(q_s_op),
        .md_dec(d_md_dec), .fhz(d_fhz), .fwhy(d_fwhy), .fdep(d_fdep), .trap_d(d_trap_d),
        .ex(q_ex), .ma(q_ma), .sl(q_sl), .ex_res(q_ex_res),
        .di_v(q_di_v), .di_isn(q_di_isn), .di_op(q_di_op), .di_d(q_di_d), .di_c(q_di_c), .di_md(q_di_md),
        .di_fcmpd(in_flat[O_BITS + 8]), .di_fcmpd_op(in_flat[O_FOP +: 16]), .di_fcmpd_md(in_flat[O_FMD +: 3]),
        .kill_v(in_flat[O_BITS + 9]), .kill_isn(in_flat[O_KISN +: 8]), .kill_incl(in_flat[O_BITS + 10]),
        .trap_f1(d_trap_f1), .trap_f1_lane(d_trap_f1_lane), .trap_f1_isn(d_trap_f1_isn), .frec(d_frec),
        .mac_ld_a_v(in_flat[O_BITS + 11]), .mac_ld_b_v(in_flat[O_BITS + 12]), .def_ld_v(in_flat[O_BITS + 13]),
        .mac_ld_a(in_flat[O_MLA +: 32]), .mac_ld_b(in_flat[O_MLB +: 32]), .def_ld(in_flat[O_DEF +: 32]),
        .ex_s(in_flat[O_EXS +: 2]),
        .fr_now(d_fr_now), .fpul_now(d_fpul_now), .fpscr_now(d_fpscr_now), .mach_now(d_mach_now),
        .macl_now(d_macl_now),
        .t_busy(d_t_busy), .flag_busy(d_flag_busy), .t_we(d_t_we), .t_val(d_t_val), .t_lane(d_t_lane), .idle(d_idle)
    );

    // ---- RAW capture, then the next cycle's XOR fold to one pin
    reg [31:0]         cap_fr [32];
    reg [159:0]        cap_words;                        // dbg_rdata, fpul_now, fpscr_now, mach_now, macl_now
    commit_rec_t [5:0] cap_frec;
    reg [40:0]         cap_misc;
    always @(posedge clk) begin
        if (reset) begin
            for (int k = 0; k < 32; k++) cap_fr[k] <= '0;
            cap_words <= '0; cap_frec <= '0; cap_misc <= '0;
        end else begin
            for (int k = 0; k < 32; k++) cap_fr[k] <= d_fr_now[k];
            cap_words <= {d_dbg_rdata, d_fpul_now, d_fpscr_now, d_mach_now, d_macl_now};
            cap_frec  <= d_frec;
            cap_misc  <= {d_dbg_hit, d_fdep, d_trap_f1, d_trap_f1_lane, d_t_busy, d_flag_busy, d_t_we,
                          d_t_val, d_t_lane, d_idle, d_md_dec, d_fhz[0], d_fhz[1], d_trap_d[0], d_trap_d[1],
                          d_fwhy[0], d_fwhy[1], d_trap_f1_isn};
        end
    end
    reg [31:0] fr_fold;
    always @* begin
        fr_fold = '0;
        for (int k = 0; k < 32; k++) fr_fold = fr_fold ^ cap_fr[k];
    end
    always @(posedge clk) begin
        if (reset) digest <= 1'b0;
        else       digest <= ^{fr_fold, cap_words, cap_frec, cap_misc};
    end
endmodule
