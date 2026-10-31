//
// v1_itlb_timing - standalone synthesis / timing harness around v1_itlb (rtl/v1/v1_itlb.sv): the 4-entry fully
// associative ITLB -- the fetch lookup (f_*), the cross lookup of an array translation (x_*), the refill from the UTLB,
// the address / data array writes and the indexed read.
//
// Pattern (polly2-rtl's quartus/timming_tests harnesses): every input bit from an HPS-written input register bank (so
// the fitter cannot fold the unit against constant inputs); every output captured RAW (no logic before the flops:
// pure unit timing); the captured vector XOR-folded to one `digest` pin the next cycle (the fold sits off the capture).
//
// Build: cd quartus/timing/v1_itlb && quartus_sh --flow compile v1_itlb
// Fmax:  output_files/v1_itlb.sta.rpt
//
module v1_itlb_timing import v1_mmu_pkg::*; (
    input             clk,
    input             reset,
    input             wr_en,
    input      [12:0] wr_addr,
    input      [31:0] wr_data,
    output reg        digest
);
    // ---- input register bank, flattened (offsets below)
    localparam integer W_E    = $bits(itlb_e_t);
    localparam integer O_FVA  = 0;                       // f_va
    localparam integer O_XVA  = O_FVA + 32;              // x_va
    localparam integer O_AWD  = O_XVA + 32;              // aw_data
    localparam integer O_RFV  = O_AWD + 32;              // rf_val
    localparam integer O_ASID = O_RFV + W_E;             // asid
    localparam integer O_RFI  = O_ASID + 8;              // rf_idx
    localparam integer O_AWA  = O_RFI + 2;               // aw_arr
    localparam integer O_AWI  = O_AWA + 2;               // aw_idx
    localparam integer O_RI   = O_AWI + 2;               // r_idx
    localparam integer O_BITS = O_RI + 2;                // 0 clr_v, 1 sv, 2 f_md, 3 x_md, 4 rf_we, 5 aw_we, 6 as_we,
                                                         // 7 as_v, 8 ti
    localparam integer IN_W   = O_BITS + 9;
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

    itlb_e_t q_rf_val;
    always @* q_rf_val = itlb_e_t'(in_flat[O_RFV +: W_E]);

    // ---- DUT
    logic       d_f_any, d_f_multi, d_x_any, d_x_multi;
    logic [1:0] d_f_idx;
    itlb_e_t    d_f_ent, d_x_ent, d_r_ent;
    v1_itlb u_dut (
        .clk(clk), .clr_v(in_flat[O_BITS + 0]), .asid(in_flat[O_ASID +: 8]), .sv(in_flat[O_BITS + 1]),
        .f_va(in_flat[O_FVA +: 32]), .f_md(in_flat[O_BITS + 2]), .f_any(d_f_any), .f_multi(d_f_multi),
        .f_idx(d_f_idx), .f_ent(d_f_ent), .x_va(in_flat[O_XVA +: 32]), .x_md(in_flat[O_BITS + 3]), .x_any(d_x_any),
        .x_multi(d_x_multi), .x_ent(d_x_ent), .rf_we(in_flat[O_BITS + 4]), .rf_idx(in_flat[O_RFI +: 2]),
        .rf_val(q_rf_val), .aw_we(in_flat[O_BITS + 5]), .aw_arr(in_flat[O_AWA +: 2]), .aw_idx(in_flat[O_AWI +: 2]),
        .aw_data(in_flat[O_AWD +: 32]), .as_we(in_flat[O_BITS + 6]), .as_v(in_flat[O_BITS + 7]),
        .ti(in_flat[O_BITS + 8]), .r_idx(in_flat[O_RI +: 2]), .r_ent(d_r_ent)
    );

    // ---- RAW capture, then the next cycle's XOR fold to one pin
    itlb_e_t  cap_f_ent, cap_x_ent, cap_r_ent;
    reg [5:0] cap_misc;                                  // f_any, f_multi, x_any, x_multi, f_idx
    always @(posedge clk) begin
        if (reset) begin cap_f_ent <= '0; cap_x_ent <= '0; cap_r_ent <= '0; cap_misc <= '0; end
        else begin
            cap_f_ent <= d_f_ent; cap_x_ent <= d_x_ent; cap_r_ent <= d_r_ent;
            cap_misc  <= {d_f_any, d_f_multi, d_x_any, d_x_multi, d_f_idx};
        end
    end
    always @(posedge clk) begin
        if (reset) digest <= 1'b0;
        else       digest <= ^{cap_f_ent, cap_x_ent, cap_r_ent, cap_misc};
    end
endmodule
