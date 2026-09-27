// v1_rstc.sv -- the reset controller of the v1 SoC (WI-CLK; SOC.md 2.2, 2.3, 3.8; hm 5.6.1): reset requests in,
// reset kinds out.
//
// Requests (each a one-cycle pulse, or the RESET pin level):
//   RESET low (reset_n = 0), SCK2 high -> pin power-on reset;  SCK2 low -> manual reset (the MRESET / SCK2 pin,
//     hm 5.6.1: "SCK2 pin high level and RESET pin low level" / "SCK2 pin low level and RESET pin low level").  Both
//     pins go through a two-flop synchroniser (the pin is asynchronous to Iclk); SCK2 is sampled in the same cycles as
//     RESET, and a pin reset during which SCK2 was ever high is a power-on reset (the merge rule below).
//   wdt_rst_por, hudi_rst            -> power-on reset (EXPEVT 0x000)
//   wdt_rst_man                      -> manual reset (0x020)
//   mhit_req (core), arr_mhit (MMU)  -> manual reset with EXPEVT 0x140 (TLB multiple hit)
//   cpu_rst_req (core)               -> CPU-only reset (0x020; a general exception with SR.BL = 1, C12)
//
// Outputs: rst_t levels and rst_code, both registered.  A request asserts them from the next cycle until 8 Icyc after
// the request ends (the `hold` counter): 2 Pcyc at FRQCR 0x0E0A, so every Pclk-domain register sees at least one pce
// under reset [P] (SOC.md 2.2).  The levels imply the weaker kinds: pin -> por -> cpu, man -> cpu.  Requests during an
// active reset merge (they reload the hold) and the strongest kind wins: pin > por > man > cpu.  rst_code: RC_000 for
// the power-on kinds, RC_140 for a manual reset that merged a multiple hit [P: within one manual reset the multiple
// hit's code wins], RC_020 otherwise (manual, CPU-only).
//
// There is no reset input: this module is the reset.  At time 0 the synchronisers read 0, so the chip starts under a
// (manual-looking) pin reset until the testbench's RESET / SCK2 levels have gone through them; the board holds RESET
// low for longer than that (SOC.md 7).  Whatever the flops power up with, the first request replaces it: a `kind`
// outside the five codes never merges, so a held RESET always reaches a pin power-on reset [P].
module v1_rstc
  import v1_soc_pkg::*;
(
  input  logic      clk,
  input  logic      reset_n, sck2_i,
  input  logic      wdt_rst_por, wdt_rst_man, hudi_rst, mhit_req, arr_mhit, cpu_rst_req,
  output rst_t      rst,
  output rst_code_e rst_code
);
  typedef enum logic [2:0] { K_NONE = 3'd0, K_CPU = 3'd1, K_MAN = 3'd2, K_POR = 3'd3, K_PIN = 3'd4 } kind_e;

  logic  rn1, rn2, sk1, sk2;              // RESET / SCK2 synchronisers
  kind_e kind;                            // the reset in progress
  logic  mh;                              // it is a manual reset that merged a TLB multiple hit
  logic [3:0] hold;                       // Icyc left after the last request (8 .. 1; 0 = no reset)

  // this cycle's strongest request
  kind_e req;
  always_comb begin
    req = K_NONE;
    if (cpu_rst_req)                               req = K_CPU;
    if (wdt_rst_man || mhit_req || arr_mhit)       req = K_MAN;
    if (!rn2 && !sk2)                              req = K_MAN;
    if (wdt_rst_por || hudi_rst)                   req = K_POR;
    if (!rn2 && sk2)                               req = K_PIN;
  end
  wire   req_mh = mhit_req || arr_mhit;
  wire   act    = hold != 4'd0 && kind <= K_PIN;   // a power-up value outside the codes is no reset to merge with [P]
  kind_e merged;
  always_comb merged = (act && kind > req) ? kind : req;

  always_ff @(posedge clk) begin
    rn1 <= reset_n;
    rn2 <= rn1;
    sk1 <= sck2_i;
    sk2 <= sk1;
    if (req != K_NONE) begin
      kind <= merged;
      mh   <= merged == K_MAN && ((act && kind == K_MAN && mh) || req_mh);
      hold <= 4'd8;
    end else if (act) begin
      hold <= hold - 4'd1;
      if (hold == 4'd1) begin
        kind <= K_NONE;
        mh   <= 1'b0;
      end
    end
  end

  assign rst.pin = kind == K_PIN;
  assign rst.por = kind == K_POR || kind == K_PIN;
  assign rst.man = kind == K_MAN;
  assign rst.cpu = kind != K_NONE;
  assign rst_code = (kind == K_POR || kind == K_PIN) ? RC_000 : (kind == K_MAN && mh) ? RC_140 :
                    kind != K_NONE ? RC_020 : RC_000;
endmodule
