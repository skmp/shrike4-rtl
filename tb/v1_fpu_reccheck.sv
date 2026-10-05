// v1_fpu_reccheck.sv -- testbench check of v1_fpu's commit-log records (rtl/v1/SOC.md 6.3; WI-CORE-F).  No hardware.
//
// A shadow of the FPU's registers is kept from the records alone: after a reset it copies the registers once (their
// reset value and the backdoor's), then each cycle's `frec` records are applied in slot order.  In every cycle the
// shadow must equal the registers: every FR word, FPUL, MACH and MACL exactly; FPSCR without its cause / flag fields
// (they have no records, SOC.md 6.3) and only while no FRCHG / FSCHG pass is in flight (their flip lands in the
// register at issue, their record when the pass leaves FS).  So a write without its record, a record without its write
// or with another value, or a record in the wrong cycle is an error; so is a record of an undefined kind or index.
// So is a cycle with more writes than the record slots of their source (v1_fpu's rec_ovf_err, `ovf`).  The ISNs are
// checked where the program is known (tb/v1_fpuhold_unit.cpp) and by tb/soc_checker (tb/v1_fpuhold_x.cpp --check).
//   err    the mismatching cycles (the first three are printed); FATAL = 1 stops the simulation at the first
module v1_fpu_reccheck #(parameter bit FATAL = 1'b0) (
  input  logic                          clk,
  input  logic                          rst,
  input  v1_soc_pkg::commit_rec_t [5:0] frec,
  input  logic [31:0]                   fr [32],
  input  logic [31:0]                   fpul, fpscr, mach, macl,
  input  logic                          mfl_busy,     // an FRCHG / FSCHG pass is in F1 / F2 / FS
  input  logic                          ovf,          // v1_fpu's rec_ovf_err
  output logic [31:0]                   err
);
  localparam logic [31:0] FMASK = ~32'h0003_F07C;      // FPSCR without cause / flags
  logic [31:0] s_fr [32];
  logic [31:0] s_fpul, s_fpscr, s_mach, s_macl;
  logic        sync;
  logic [63:0] cyc;
  always_ff @(posedge clk) begin
    if (rst) begin
      sync <= 1'b0; err <= '0; cyc <= '0;
    end else begin
      logic [31:0] b_fr [32];
      logic [31:0] b_ul, b_sc, b_mh, b_ml;
      logic        bad;
      string       why;
      bad = ovf; why = ovf ? " more writes than record slots (rec_ovf_err)" : "";
      cyc <= cyc + 64'd1;
      if (sync) begin
        for (int w = 0; w < 32; w++)
          if (s_fr[w] != fr[w]) begin bad = 1'b1; why = $sformatf("%s FR%0d %08X (records %08X)", why, w, fr[w], s_fr[w]); end
        if (s_fpul != fpul) begin bad = 1'b1; why = $sformatf("%s FPUL %08X (records %08X)", why, fpul, s_fpul); end
        if (s_mach != mach) begin bad = 1'b1; why = $sformatf("%s MACH %08X (records %08X)", why, mach, s_mach); end
        if (s_macl != macl) begin bad = 1'b1; why = $sformatf("%s MACL %08X (records %08X)", why, macl, s_macl); end
        if (!mfl_busy && ((s_fpscr ^ fpscr) & FMASK) != 32'd0) begin
          bad = 1'b1; why = $sformatf("%s FPSCR %08X (records %08X)", why, fpscr, s_fpscr);
        end
      end
      for (int w = 0; w < 32; w++) b_fr[w] = sync ? s_fr[w] : fr[w];
      b_ul = sync ? s_fpul : fpul; b_sc = sync ? s_fpscr : fpscr; b_mh = sync ? s_mach : mach; b_ml = sync ? s_macl : macl;
      for (int k = 0; k < 6; k++) if (frec[k].v) begin
        if (frec[k].kind == v1_soc_pkg::CK_FPR && frec[k].idx < 8'd32) b_fr[frec[k].idx[4:0]] = frec[k].data[31:0];
        else if (frec[k].kind == v1_soc_pkg::CK_SYS && frec[k].idx == 8'(v1_soc_pkg::SY_FPUL)) b_ul = frec[k].data[31:0];
        else if (frec[k].kind == v1_soc_pkg::CK_SYS && frec[k].idx == 8'(v1_soc_pkg::SY_FPSCR)) b_sc = frec[k].data[31:0];
        else if (frec[k].kind == v1_soc_pkg::CK_SYS && frec[k].idx == 8'(v1_soc_pkg::SY_MACH)) b_mh = frec[k].data[31:0];
        else if (frec[k].kind == v1_soc_pkg::CK_SYS && frec[k].idx == 8'(v1_soc_pkg::SY_MACL)) b_ml = frec[k].data[31:0];
        else begin bad = 1'b1; why = $sformatf("%s slot %0d: kind %0d idx %0d", why, k, frec[k].kind, frec[k].idx); end
      end
      for (int w = 0; w < 32; w++) s_fr[w] <= b_fr[w];
      s_fpul <= b_ul; s_fpscr <= b_sc; s_mach <= b_mh; s_macl <= b_ml;
      sync <= 1'b1;
      if (bad) begin
        err <= err + 32'd1;
        if (err < 32'd3) $display("v1_fpu_reccheck: cycle %0d after reset:%s", cyc, why);
        if (FATAL) $fatal(1, "v1_fpu_reccheck: the FPU's commit-log records do not match its register writes");
      end
    end
  end
endmodule
