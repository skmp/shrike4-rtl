//
// v1_fdiv_timing - standalone synthesis / timing harness around v1_fdiv (rtl/v1/v1_fdiv.sv): the FDIV / FSQRT unit of
// v1_fpu (single and double precision: operand unpack, the recurrence under `hold`, the rounding and result passes,
// the kill by ISN).
//
// Pattern (polly2-rtl's quartus/timming_tests harnesses): every input bit from an HPS-written input register bank (so
// the fitter cannot fold the unit against constant inputs); every output captured RAW (no logic before the flops:
// pure unit timing); the captured vector XOR-folded to one `digest` pin the next cycle (the fold sits off the capture).
//
// Build: cd quartus/timing/v1_fdiv && quartus_sh --flow compile v1_fdiv
// Fmax:  output_files/v1_fdiv.sta.rpt
//
module v1_fdiv_timing import v1_fp_pkg::*; (
    input             clk,
    input             reset,
    input             wr_en,
    input      [12:0] wr_addr,
    input      [31:0] wr_data,
    output reg        digest
);
    // ---- input register bank, flattened: a, b, isn, kill_isn, then the single bits
    //      (rst, start, hold, sqrt, dbl, rz, dn0, kill_v, kill_incl)
    localparam integer O_A    = 0;
    localparam integer O_B    = O_A + 64;
    localparam integer O_ISN  = O_B + 64;
    localparam integer O_KISN = O_ISN + 8;
    localparam integer O_BITS = O_KISN + 8;
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

    // ---- DUT
    logic        d_busy, d_last, d_rq, d_done;
    logic [63:0] d_r;
    fexc_t       d_x;
    v1_fdiv u_dut (
        .clk(clk), .rst(in_flat[O_BITS + 0]), .start(in_flat[O_BITS + 1]), .hold(in_flat[O_BITS + 2]),
        .sqrt(in_flat[O_BITS + 3]), .dbl(in_flat[O_BITS + 4]), .rz(in_flat[O_BITS + 5]), .dn0(in_flat[O_BITS + 6]),
        .a(in_flat[O_A +: 64]), .b(in_flat[O_B +: 64]), .isn(in_flat[O_ISN +: 8]),
        .kill_v(in_flat[O_BITS + 7]), .kill_isn(in_flat[O_KISN +: 8]), .kill_incl(in_flat[O_BITS + 8]),
        .busy(d_busy), .last(d_last), .rq(d_rq), .done(d_done), .r(d_r), .x(d_x)
    );

    // ---- RAW capture, then the next cycle's XOR fold to one pin
    reg [63:0] cap_r;
    fexc_t     cap_x;
    reg [3:0]  cap_misc;
    always @(posedge clk) begin
        if (reset) begin cap_r <= '0; cap_x <= '0; cap_misc <= '0; end
        else       begin cap_r <= d_r; cap_x <= d_x; cap_misc <= {d_busy, d_last, d_rq, d_done}; end
    end
    always @(posedge clk) begin
        if (reset) digest <= 1'b0;
        else       digest <= ^{cap_r, cap_x, cap_misc};
    end
endmodule
