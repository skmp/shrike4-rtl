//
// tp_decode_timing - standalone synthesis / timing harness around tp_decode (rtl/timing-proxy/tp_decode.sv): the
// instruction decoder (a 16-bit opcode and FPSCR FR / SZ / PR to the instruction's form, operand fields and issue
// class), one combinational stage; v1_core holds two (the D stage's pair) and v1_fetch six (the queue's pre-decode).
//
// Pattern (polly2-rtl's quartus/timming_tests stage harnesses): every input bit from an HPS-written input register
// bank; the stage's output captured RAW (pure stage timing); the captured vector XOR-folded to one `digest` pin the
// next cycle.
//
// Build: cd quartus/timing/tp_decode && quartus_sh --flow compile tp_decode
// Fmax:  output_files/tp_decode.sta.rpt
//
module tp_decode_timing import tp_pkg::*; (
    input             clk,
    input             reset,
    input             wr_en,
    input      [12:0] wr_addr,
    input      [31:0] wr_data,
    output reg        digest
);
    // ---- input register bank: op at 0, md above it
    localparam integer IN_W = 16 + 3;
    localparam integer NREG = (IN_W + 31) / 32;
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
    dec_t d_d;
    tp_decode u_dut (.op(in_flat[0 +: 16]), .md(in_flat[16 +: 3]), .d(d_d));

    // ---- RAW capture, then the next cycle's XOR fold to one pin
    dec_t cap_d;
    always @(posedge clk) begin
        if (reset) cap_d <= '0;
        else       cap_d <= d_d;
    end
    always @(posedge clk) begin
        if (reset) digest <= 1'b0;
        else       digest <= ^cap_d;
    end
endmodule
