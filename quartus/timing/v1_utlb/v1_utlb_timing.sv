//
// v1_utlb_timing - standalone synthesis / timing harness around v1_utlb (rtl/v1/v1_utlb.sv): the 64-entry fully
// associative UTLB -- the data-side lookup (d_*), the associative lookup of an array write (a_*), the LDTLB load, the
// address / data array writes and the indexed read.
//
// Pattern (polly2-rtl's quartus/timming_tests harnesses): every input bit from an HPS-written input register bank (so
// the fitter cannot fold the unit against constant inputs); every output captured RAW (no logic before the flops:
// pure unit timing); the captured vector XOR-folded to one `digest` pin the next cycle (the fold sits off the capture).
//
// Build: cd quartus/timing/v1_utlb && quartus_sh --flow compile v1_utlb
// Fmax:  output_files/v1_utlb.sta.rpt
//
module v1_utlb_timing import v1_mmu_pkg::*; (
    input             clk,
    input             reset,
    input             wr_en,
    input      [12:0] wr_addr,
    input      [31:0] wr_data,
    output reg        digest
);
    // ---- input register bank, flattened (offsets below)
    localparam integer W_E    = $bits(utlb_e_t);
    localparam integer O_DVA  = 0;                       // d_va
    localparam integer O_AVA  = O_DVA + 32;              // a_va
    localparam integer O_AWD  = O_AVA + 32;              // aw_data
    localparam integer O_LDV  = O_AWD + 32;              // ld_val
    localparam integer O_ASID = O_LDV + W_E;             // asid
    localparam integer O_LDI  = O_ASID + 8;              // ld_idx
    localparam integer O_AWI  = O_LDI + 6;               // aw_idx
    localparam integer O_RI   = O_AWI + 6;               // r_idx
    localparam integer O_AWA  = O_RI + 6;                // aw_arr
    localparam integer O_BITS = O_AWA + 2;               // 0 clr_v, 1 sv, 2 d_md, 3 a_md, 4 ld_we, 5 aw_we, 6 aw_assoc,
                                                         // 7 ti
    localparam integer IN_W   = O_BITS + 8;
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

    utlb_e_t q_ld_val;
    always @* q_ld_val = utlb_e_t'(in_flat[O_LDV +: W_E]);

    // ---- DUT
    logic       d_d_any, d_d_multi, d_a_any, d_a_multi;
    logic [5:0] d_a_idx;
    utlb_e_t    d_d_ent, d_a_ent, d_r_ent;
    v1_utlb u_dut (
        .clk(clk), .clr_v(in_flat[O_BITS + 0]), .asid(in_flat[O_ASID +: 8]), .sv(in_flat[O_BITS + 1]),
        .d_va(in_flat[O_DVA +: 32]), .d_md(in_flat[O_BITS + 2]), .d_any(d_d_any), .d_multi(d_d_multi), .d_ent(d_d_ent),
        .a_va(in_flat[O_AVA +: 32]), .a_md(in_flat[O_BITS + 3]), .a_any(d_a_any), .a_multi(d_a_multi),
        .a_idx(d_a_idx), .a_ent(d_a_ent), .ld_we(in_flat[O_BITS + 4]), .ld_idx(in_flat[O_LDI +: 6]), .ld_val(q_ld_val),
        .aw_we(in_flat[O_BITS + 5]), .aw_arr(in_flat[O_AWA +: 2]), .aw_assoc(in_flat[O_BITS + 6]),
        .aw_idx(in_flat[O_AWI +: 6]), .aw_data(in_flat[O_AWD +: 32]), .ti(in_flat[O_BITS + 7]),
        .r_idx(in_flat[O_RI +: 6]), .r_ent(d_r_ent)
    );

    // ---- RAW capture, then the next cycle's XOR fold to one pin
    utlb_e_t   cap_d_ent, cap_a_ent, cap_r_ent;
    reg [9:0]  cap_misc;                                 // d_any, d_multi, a_any, a_multi, a_idx
    always @(posedge clk) begin
        if (reset) begin cap_d_ent <= '0; cap_a_ent <= '0; cap_r_ent <= '0; cap_misc <= '0; end
        else begin
            cap_d_ent <= d_d_ent; cap_a_ent <= d_a_ent; cap_r_ent <= d_r_ent;
            cap_misc  <= {d_d_any, d_d_multi, d_a_any, d_a_multi, d_a_idx};
        end
    end
    always @(posedge clk) begin
        if (reset) digest <= 1'b0;
        else       digest <= ^{cap_d_ent, cap_a_ent, cap_r_ent, cap_misc};
    end
endmodule
