//
// fpu_vec_top_timing - standalone synthesis / timing harness around fpu_vec_top (rtl/fpu_vec/fpu_vec_top.sv): the
// whole shared FIPR / FSCA / FSRRA / FTRV-row datapath (four Booth multipliers, aligners, the four-input adder,
// normalise / round, the FSCA / FSRRA ROMs and its stage registers), as v1_fpu instantiates it.
//
// Pattern (polly2-rtl's quartus/timming_tests harnesses): every input bit from an HPS-written input register bank (so
// the fitter cannot fold the unit against constant inputs); every output captured RAW (no logic before the flops:
// pure unit timing); the captured vector XOR-folded to one `digest` pin the next cycle (the fold sits off the capture).
//
// Build: cd quartus/timing/fpu_vec_top && quartus_sh --flow compile fpu_vec_top
// Fmax:  output_files/fpu_vec_top.sta.rpt
//
module fpu_vec_top_timing import fpu_vec_pkg::*; (
    input             clk,
    input             reset,
    input             wr_en,
    input      [12:0] wr_addr,
    input      [31:0] wr_data,
    output reg        digest
);
    // ---- input register bank, flattened (offsets below, in port order)
    localparam integer O_FVA  = 0;                       // fva[0..3]
    localparam integer O_FVB  = O_FVA + 4 * 32;          // fvb[0..3]
    localparam integer O_FPUL = O_FVB + 4 * 32;
    localparam integer O_FRN  = O_FPUL + 32;
    localparam integer O_ISN  = O_FRN + 32;
    localparam integer O_KISN = O_ISN + 8;               // kill_isn
    localparam integer O_OP   = O_KISN + 8;
    localparam integer O_BITS = O_OP + $bits(op_e);      // rst, start, hold, rz, dn0, kill_v, kill_incl
    localparam integer IN_W   = O_BITS + 7;
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

    logic [31:0] q_fva [4];
    logic [31:0] q_fvb [4];
    op_e         q_op;
    always @* begin
        for (int k = 0; k < 4; k++) begin
            q_fva[k] = in_flat[O_FVA + 32*k +: 32];
            q_fvb[k] = in_flat[O_FVB + 32*k +: 32];
        end
        q_op = op_e'(in_flat[O_OP +: $bits(op_e)]);
    end

    // ---- DUT
    logic [4:0]  d_st_x, d_res_x;
    logic        d_st_e, d_busy, d_res_valid, d_res_second, d_res_e;
    logic [31:0] d_res;
    fpu_vec_top u_dut (
        .clk(clk), .rst(in_flat[O_BITS + 0]), .start(in_flat[O_BITS + 1]), .hold(in_flat[O_BITS + 2]),
        .op(q_op), .rz(in_flat[O_BITS + 3]), .dn0(in_flat[O_BITS + 4]), .fva(q_fva), .fvb(q_fvb),
        .fpul(in_flat[O_FPUL +: 32]), .frn(in_flat[O_FRN +: 32]), .isn(in_flat[O_ISN +: 8]),
        .kill_v(in_flat[O_BITS + 5]), .kill_isn(in_flat[O_KISN +: 8]), .kill_incl(in_flat[O_BITS + 6]),
        .st_x(d_st_x), .st_e(d_st_e), .busy(d_busy), .res_valid(d_res_valid), .res_second(d_res_second),
        .res(d_res), .res_x(d_res_x), .res_e(d_res_e)
    );

    // ---- RAW capture, then the next cycle's XOR fold to one pin
    reg [31:0] cap_res;
    reg [14:0] cap_misc;
    always @(posedge clk) begin
        if (reset) begin cap_res <= '0; cap_misc <= '0; end
        else begin
            cap_res  <= d_res;
            cap_misc <= {d_st_x, d_res_x, d_st_e, d_busy, d_res_valid, d_res_second, d_res_e};
        end
    end
    always @(posedge clk) begin
        if (reset) digest <= 1'b0;
        else       digest <= ^{cap_res, cap_misc};
    end
endmodule
