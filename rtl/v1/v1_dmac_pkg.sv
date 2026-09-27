// v1_dmac_pkg.sv -- DMAC-private constants and decode functions (rtl/v1/spec/dmac.md; SOC.md 1.2, 3.8).
// Owner: WI-DMAC.  Parsed after v1_soc_pkg (it imports it) and before the v1_dmac* modules.
//
// Nothing here is a shared type: the ports of v1_dmac are the v1_soc_pkg structs of SOC.md 3.8.  These are the
// field accessors of CHCR / DMAOR / the DTR, the unit-size decode and the fixed / round-robin priority orders.
package v1_dmac_pkg;
  import v1_soc_pkg::*;

  // ==================================================================================================================
  // CHCR (spec dmac 2.5)
  // ==================================================================================================================
  // Writable bits.  TE (bit 1) follows the flag rule instead; bits 23:20 and 3 read 0.  [HW dmac1 S3]: a write of
  // 0xFFFFFFFE reads back 0xFF0FFFF4 on CHCR1 and 0xFF0AFFF4 on CHCR2 / CHCR3 (no RL / AL on channels 2 and 3).
  localparam logic [31:0] CHCR_WM01 = 32'hFF0F_FFF5;
  localparam logic [31:0] CHCR_WM23 = 32'hFF0A_FFF5;

  /* verilator lint_off UNUSEDSIGNAL */          // field accessors: each reads its own bits of the register
  function automatic logic       c_ds (input logic [31:0] c); return c[19];    endfunction  // DREQ falling edge
  function automatic logic       c_rl (input logic [31:0] c); return c[18];    endfunction  // DRAK active low
  function automatic logic       c_am (input logic [31:0] c); return c[17];    endfunction  // DACK in the write cycle
  function automatic logic       c_al (input logic [31:0] c); return c[16];    endfunction  // DACK active low
  function automatic logic [1:0] c_dm (input logic [31:0] c); return c[15:14]; endfunction
  function automatic logic [1:0] c_sm (input logic [31:0] c); return c[13:12]; endfunction
  function automatic logic [3:0] c_rs (input logic [31:0] c); return c[11:8];  endfunction
  function automatic logic       c_tm (input logic [31:0] c); return c[7];     endfunction  // burst
  function automatic logic [2:0] c_ts (input logic [31:0] c); return c[6:4];   endfunction
  function automatic logic       c_ie (input logic [31:0] c); return c[2];     endfunction
  function automatic logic       c_te (input logic [31:0] c); return c[1];     endfunction
  function automatic logic       c_de (input logic [31:0] c); return c[0];     endfunction
  /* verilator lint_on UNUSEDSIGNAL */

  // ---- transfer size (TS): 000 8 B, 001 1 B, 010 2 B, 011 4 B, 100 32 B; 101-111 reserved (stored, never started)
  function automatic logic ts_ok(input logic [2:0] ts);
    return ts <= 3'd4;
  endfunction

  function automatic logic [4:0] ts_amask(input logic [2:0] ts);     // the address bits that must be 0 (class A)
    unique case (ts)
      3'd0:    return 5'h07;
      3'd2:    return 5'h01;
      3'd3:    return 5'h03;
      3'd4:    return 5'h1F;
      default: return 5'h00;                                          // byte, and the reserved codes
    endcase
  endfunction

  function automatic logic [5:0] ts_bytes(input logic [2:0] ts);
    unique case (ts)
      3'd0:    return 6'd8;
      3'd1:    return 6'd1;
      3'd2:    return 6'd2;
      3'd3:    return 6'd4;
      3'd4:    return 6'd32;
      default: return 6'd0;
    endcase
  endfunction

  function automatic bsz_e ts_bsz(input logic [2:0] ts);
    unique case (ts)
      3'd1:    return BZ_B;
      3'd2:    return BZ_W;
      3'd3:    return BZ_L;
      3'd4:    return BZ_LINE;
      default: return BZ_Q;
    endcase
  endfunction

  function automatic logic [7:0] ts_be(input logic [2:0] ts);        // byte lanes of a unit at lane 0
    unique case (ts)
      3'd1:    return 8'h01;
      3'd2:    return 8'h03;
      3'd3:    return 8'h0F;
      default: return 8'hFF;                                          // quad, 32 bytes (every beat full)
    endcase
  endfunction

  // ---- RS (request source; spec dmac 2.5 table, hm Tables 14.4 / 14.5)
  /* verilator lint_off UNUSEDSIGNAL */          // the decoders below test the bits their codes need
  function automatic logic rs_prohib(input logic [3:0] rs);          // 0001, 0111, 1111: stored, never started [P]
    return rs == 4'b0001 || rs == 4'b0111 || rs == 4'b1111;
  endfunction
  function automatic logic rs_ext(input logic [3:0] rs);             // external request (DREQ pins, or DDT)
    return rs == 4'b0000 || rs == 4'b0010 || rs == 4'b0011;
  endfunction
  function automatic logic rs_auto(input logic [3:0] rs);            // 0100 0101 0110
    return rs[3:2] == 2'b01 && rs != 4'b0111;
  endfunction
  function automatic logic rs_onchip(input logic [3:0] rs);          // SCI / SCIF / TMU request (1000-1110)
    return rs[3] && rs != 4'b1111;
  endfunction
  function automatic logic rs_serial(input logic [3:0] rs);          // SCI / SCIF: cycle steal only (hm 14.3.2)
    return rs[3:2] == 2'b10;
  endfunction
  function automatic logic rs_tmu(input logic [3:0] rs);
    return rs[3:2] == 2'b11 && rs != 4'b1111;
  endfunction
  function automatic logic rs_single(input logic [3:0] rs);          // single address (DACK / TDACK device)
    return rs == 4'b0010 || rs == 4'b0011;
  endfunction
  function automatic logic rs_src_onchip(input logic [3:0] rs);      // the read leg goes to the peripheral bus
    return rs == 4'b0110 || rs == 4'b1001 || rs == 4'b1011 || rs == 4'b1110;
  endfunction
  function automatic logic rs_dst_onchip(input logic [3:0] rs);      // the write leg goes to the peripheral bus
    return rs == 4'b0101 || rs == 4'b1000 || rs == 4'b1010 || rs == 4'b1101;
  endfunction
  // A register the mode does not use is neither checked nor updated (spec dmac 2.3, [HW dmac1 S13]).
  function automatic logic rs_uses_sar(input logic [3:0] rs);        // dual, or single memory -> device
    return rs != 4'b0011;
  endfunction
  function automatic logic rs_uses_dar(input logic [3:0] rs);        // dual, or single device -> memory
    return rs != 4'b0010;
  endfunction

  // ---- the physical address map seen by a DMAC leg: bits 31:29 are ignored [HW dmac1 S4]
  function automatic logic a_onchip(input logic [31:0] a);           // 0x1Fxxxxxx: on-chip module (P4 bridge)
    return a[28:24] == 5'h1F;
  endfunction
  function automatic logic a_area7(input logic [31:0] a);            // 0x1C000000-0x1FFFFFFF
    return a[28:26] == 3'd7;
  endfunction
  /* verilator lint_on UNUSEDSIGNAL */

  // ==================================================================================================================
  // DMAOR.PR priority orders (hm 14.3.3): the channel at position k (0 = highest)
  // ==================================================================================================================
  function automatic logic [1:0] pri_ch(input logic [1:0] pr, input logic [1:0] top, input logic [1:0] k);
    unique case (pr)
      2'd0: return k;                                                 // CH0 > CH1 > CH2 > CH3
      2'd1: begin                                                     // CH0 > CH2 > CH3 > CH1
        unique case (k)
          2'd0: return 2'd0;
          2'd1: return 2'd2;
          2'd2: return 2'd3;
          default: return 2'd1;
        endcase
      end
      2'd2: begin                                                     // CH2 > CH0 > CH1 > CH3
        unique case (k)
          2'd0: return 2'd2;
          2'd1: return 2'd0;
          2'd2: return 2'd1;
          default: return 2'd3;
        endcase
      end
      default: return top + k;                                        // round robin: top, top + 1, ...
    endcase
  endfunction

  // ==================================================================================================================
  // DDT: the 64-bit DTR (hm Appendix H.1, spec dmac 3.9.2)
  // ==================================================================================================================
  typedef struct packed {
    logic [2:0]  sz;                  // 000 B 001 W 010 L 011 Q 100 32 B; 101 / 110 reserved; 111 transfer end
    logic        rw;                  // 0 memory read (ADDRESS = source), 1 memory write (ADDRESS = destination)
    logic [1:0]  id;                  // channel
    logic [1:0]  md;                  // 00 handshake (data bus), 01 burst / edge, 10 burst / level, 11 cycle steal
    logic [7:0]  cnt;                 // COUNT, 0 = 16M
    logic [15:0] rsv;
    logic [31:0] addr;
  } dtr_t;

  function automatic logic [2:0] dtr_ts(input logic [2:0] sz);       // SZ is not the TS encoding (quad: 011 / 000)
    unique case (sz)
      3'd0:    return 3'd1;
      3'd1:    return 3'd2;
      3'd2:    return 3'd3;
      3'd3:    return 3'd0;
      default: return 3'd4;
    endcase
  endfunction

  // CHCR0 as a channel-0 initialisation DTR loads it (hm H.1): DS = (MD == 01), RL = AM = AL = 0, DM = SM = 01,
  // RS = 001 R/W, TM = (MD != 11), TS = map(SZ), IE = TE = 0, DE = 1.  [HW dmac1 S1] 32 B MD 01: 0x000852C1 (read),
  // 0x000853C1 (write), which read back 0x852C3 / 0x853C3 once the unit has set TE.
  /* verilator lint_off UNUSEDSIGNAL */          // ID / COUNT / ADDRESS go to other registers
  function automatic logic [31:0] dtr_chcr0(input dtr_t d);
    return {8'h00, 4'h0, d.md == 2'b01, 3'b000, 2'b01, 2'b01, 3'b001, d.rw, d.md != 2'b11, dtr_ts(d.sz),
            1'b0, 1'b0, 1'b0, 1'b1};
  endfunction
  /* verilator lint_on UNUSEDSIGNAL */
endpackage
