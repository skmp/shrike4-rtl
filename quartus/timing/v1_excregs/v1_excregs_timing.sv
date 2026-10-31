//
// v1_excregs_timing - standalone synthesis / timing harness around v1_excregs (rtl/v1/v1_excregs.sv): the exception
// registers TRA / EXPEVT / INTEVT on the P4 bus, their capture of an accepted exception / interrupt (excw), and their
// values after a reset (by the reset code).
//
// Pattern (polly2-rtl's quartus/timming_tests harnesses): every input bit from an HPS-written input register bank (so
// the fitter cannot fold the unit against constant inputs); every output captured RAW (no logic before the flops:
// pure unit timing); the captured vector XOR-folded to one `digest` pin the next cycle (the fold sits off the capture).
//
// Build: cd quartus/timing/v1_excregs && quartus_sh --flow compile v1_excregs
// Fmax:  output_files/v1_excregs.sta.rpt
//
module v1_excregs_timing import v1_soc_pkg::*; (
    input             clk,
    input             reset,
    input             wr_en,
    input      [12:0] wr_addr,
    input      [31:0] wr_data,
    output reg        digest
);
    // ---- input register bank, flattened (offsets below)
    localparam integer O_RST  = 0;
    localparam integer O_RC   = O_RST + $bits(rst_t);            // rst_code
    localparam integer O_PQ   = O_RC + $bits(rst_code_e);        // pq
    localparam integer O_EXCW = O_PQ + $bits(p4s_req_t);         // excw
    localparam integer IN_W   = O_EXCW + $bits(excw_t);
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

    rst_t      q_rst;
    rst_code_e q_rst_code;
    p4s_req_t  q_pq;
    excw_t     q_excw;
    always @* begin
        q_rst      = rst_t'(in_flat[O_RST +: $bits(rst_t)]);
        q_rst_code = rst_code_e'(in_flat[O_RC +: $bits(rst_code_e)]);
        q_pq       = p4s_req_t'(in_flat[O_PQ +: $bits(p4s_req_t)]);
        q_excw     = excw_t'(in_flat[O_EXCW +: $bits(excw_t)]);
    end

    // ---- DUT
    p4s_rsp_t d_pr;
    v1_excregs u_dut (.clk(clk), .rst(q_rst), .rst_code(q_rst_code), .pq(q_pq), .pr(d_pr), .excw(q_excw));

    // ---- RAW capture, then the next cycle's XOR fold to one pin
    p4s_rsp_t cap_pr;
    always @(posedge clk) begin
        if (reset) cap_pr <= '0;
        else       cap_pr <= d_pr;
    end
    always @(posedge clk) begin
        if (reset) digest <= 1'b0;
        else       digest <= ^cap_pr;
    end
endmodule
