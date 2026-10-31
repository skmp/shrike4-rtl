//
// v1_bsc_mpx_timing - standalone synthesis / timing harness around v1_bsc_mpx (rtl/v1/v1_bsc_mpx.sv): the BSC's
// MPX-interface sequencer (area 0, the Dreamcast's Holly): the address / data phases, the RDY# waits, bursts and their
// beats.
//
// Pattern (polly2-rtl's quartus/timming_tests harnesses): every input bit from an HPS-written input register bank (so
// the fitter cannot fold the unit against constant inputs); every output captured RAW (no logic before the flops:
// pure unit timing); the captured vector XOR-folded to one `digest` pin the next cycle (the fold sits off the capture).
//
// Build: cd quartus/timing/v1_bsc_mpx && quartus_sh --flow compile v1_bsc_mpx
// Fmax:  output_files/v1_bsc_mpx.sta.rpt
//
module v1_bsc_mpx_timing import v1_soc_pkg::*, v1_bsc_pkg::*; (
    input             clk,
    input             reset,
    input             wr_en,
    input      [12:0] wr_addr,
    input      [31:0] wr_data,
    output reg        digest
);
    // ---- input register bank, flattened (offsets below)
    localparam integer O_RST  = 0;
    localparam integer O_REGS = O_RST + $bits(rst_t);            // regs
    localparam integer O_ISS  = O_REGS + $bits(bregs_t);         // iss_x
    localparam integer O_WBD  = O_ISS + $bits(bx_t);             // wbd  [1:0][3:0][63:0]
    localparam integer O_WBE  = O_WBD + 2 * 4 * 64;              // wbe  [1:0][3:0][7:0]
    localparam integer O_XI   = O_WBE + 2 * 4 * 8;               // xi
    localparam integer O_BITS = O_XI + $bits(xb_in_t);           // 0 bce, 1 go
    localparam integer IN_W   = O_BITS + 2;
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

    rst_t   q_rst;
    bregs_t q_regs;
    bx_t    q_iss_x;
    xb_in_t q_xi;
    always @* begin
        q_rst   = rst_t'(in_flat[O_RST +: $bits(rst_t)]);
        q_regs  = bregs_t'(in_flat[O_REGS +: $bits(bregs_t)]);
        q_iss_x = bx_t'(in_flat[O_ISS +: $bits(bx_t)]);
        q_xi    = xb_in_t'(in_flat[O_XI +: $bits(xb_in_t)]);
    end

    // ---- DUT
    logic [5:0] d_iss_len;
    logic       d_iss_ok, d_commit, d_commit_port, d_hold;
    xb_out_t    d_xo;
    bbeat_t     d_bo;
    v1_bsc_mpx u_dut (
        .clk(clk), .rst(q_rst), .bce(in_flat[O_BITS + 0]), .regs(q_regs), .iss_x(q_iss_x), .iss_len(d_iss_len),
        .iss_ok(d_iss_ok), .go(in_flat[O_BITS + 1]), .wbd(in_flat[O_WBD +: 512]), .wbe(in_flat[O_WBE +: 64]),
        .xi(q_xi), .xo(d_xo), .bo(d_bo), .commit(d_commit), .commit_port(d_commit_port), .hold(d_hold)
    );

    // ---- RAW capture, then the next cycle's XOR fold to one pin
    xb_out_t  cap_xo;
    bbeat_t   cap_bo;
    reg [9:0] cap_misc;                                  // iss_len, iss_ok, commit, commit_port, hold
    always @(posedge clk) begin
        if (reset) begin cap_xo <= '0; cap_bo <= '0; cap_misc <= '0; end
        else begin
            cap_xo <= d_xo; cap_bo <= d_bo; cap_misc <= {d_iss_len, d_iss_ok, d_commit, d_commit_port, d_hold};
        end
    end
    always @(posedge clk) begin
        if (reset) digest <= 1'b0;
        else       digest <= ^{cap_xo, cap_bo, cap_misc};
    end
endmodule
