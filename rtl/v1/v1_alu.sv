// v1_alu.sv -- SH7091 integer ALU / shifter / T-Q-M flag logic (package v1_alu_pkg, combinational, unit A of v1).
//
// One function, v1_alu(), evaluated in the EX stage of either issue lane.  It is built the way the EX-stage datapath
// of a 1997 SH core is built: four function blocks share the two operand buses and one result bus, and the flag
// logic taps the blocks rather than recomputing:
//   ADDER    one 32-bit carry-propagate adder x + y + cin with carry-out C, overflow V and a zero detect Z on its sum.
//            Every arithmetic op, every compare, DT and DIV1 is a choice of (x, y, cin) and a flag pick; subtraction
//            is x + ~y + 1, the borrow forms feed !T as the carry-in and report !C as the borrow.
//   LOGIC    AND / OR / XOR / NOT; TST = zero detect on AND, CMP/STR = per-byte zero detect on XOR.
//   SHIFTER  one 64 -> 32 funnel shifter ({hi, lo} >> s, s = 0..32).  Every shift and rotate, including SHAD / SHLD,
//            is a choice of (hi, lo, s): left by k is {Rn, 0} >> (32 - k), right by k is {fill, Rn} >> k, a rotate
//            feeds Rn to both halves, ROTCL / ROTCR put T into the half that fills the vacated bit.  SHAD / SHLD
//            need no comparator or 0/32 special case: the manual's right amount (~Rm[4:0]) + 1 is 32 - Rm[4:0], the
//            same s as the left shift by Rm[4:0], and Rm[4:0] = 0 with Rm[31] = 1 gives s = 32 = all fill bits.
//   MISC     byte / word extract and swap, XTRCT, MOV, MOVT: wiring only.
//
// Operand convention (ARCH.md section 3, unit A): a = the Rm-side operand (Rm, or the #imm already sign- or
// zero-extended by the decoder), b = the Rn-side operand (Rn, or R0 for the #imm,R0 forms, or the loaded byte for
// the @(R0,GBR) read-modify-write forms).  One-operand forms read b when the operand is Rn (ROTL Rn, DT Rn,
// CMP/PZ Rn, SHLL2 Rn, ...) and a when it is Rm (NEG Rm,Rn, EXTS.B Rm,Rn, SWAP.W Rm,Rn, NOT Rm,Rn, MOV Rm,Rn).
// SHAD / SHLD: a = Rm (the shift amount), b = Rn.  The semantics are the manual's C pseudo-code (pm.txt Section 10),
// checked against isa.py execute() and a C port of the pseudo-code in tb/v1_alu_tb.
//
// Outputs: r = the value for Rn (R0 for the #imm,R0 forms).  For the forms that do not write a GPR (TST, CMP/xx,
// DIV0S, DIV0U, CLRT, SETT) r = b, so a stray write-back would be harmless; the core decides from the form whether
// Rn is written.  t / q / m are the new flag values when wr_t / wr_qm are set and the incoming values otherwise.
// DIV1 writes Q and M (wr_qm) with M unchanged, like the manual (M is only ever loaded by DIV0S / DIV0U).
//
// Additions to the ARCH.md contract (appended to the enum, existing encodings unchanged):
//   A_TAS  TAS.B's modify step: b = the loaded byte (zero-extended), r = b | 0x80, T = (b[7:0] == 0).
//          (AND.B / OR.B / XOR.B / TST.B #imm,@(R0,GBR) are A_AND / A_OR / A_XOR / A_TST with a = imm, b = byte.)
package v1_alu_pkg;
  typedef enum logic [5:0] { A_ADD, A_ADDC, A_ADDV, A_SUB, A_SUBC, A_SUBV, A_NEG, A_NEGC, A_AND, A_OR, A_XOR, A_NOT,
    A_TST, A_CMPEQ, A_CMPHS, A_CMPGE, A_CMPHI, A_CMPGT, A_CMPPZ, A_CMPPL, A_CMPSTR, A_DIV0S, A_DIV0U, A_DIV1, A_DT,
    A_EXTSB, A_EXTSW, A_EXTUB, A_EXTUW, A_SWAPB, A_SWAPW, A_XTRCT, A_MOV, A_MOVT, A_ROTL, A_ROTR, A_ROTCL, A_ROTCR,
    A_SHAL, A_SHAR, A_SHLL, A_SHLR, A_SHLL2, A_SHLR2, A_SHLL8, A_SHLR8, A_SHLL16, A_SHLR16, A_SHAD, A_SHLD,
    A_CLRT, A_SETT,
    A_TAS } alu_op_e;
  typedef struct packed { logic [31:0] r; logic t, q, m; logic wr_t, wr_qm; } alu_out_t;

  // a = Rm-side operand (or the immediate, sign- or zero-extended by the caller), b = Rn-side operand
  function automatic alu_out_t v1_alu(input alu_op_e op, input logic [31:0] a, input logic [31:0] b,
                                      input logic t, input logic q, input logic m);
    alu_out_t    o;
    // ---- adder ---------------------------------------------------------------------------------------------
    logic [31:0] x, y;          // adder inputs (y already inverted for a subtraction)
    logic        cin;
    logic [32:0] sum;           // {C, S}
    logic        c, v, z;
    logic        sub1;          // DIV1: this step subtracts the divisor (old Q == M)
    logic        q1;            // DIV1: the new Q
    // ---- logic unit / shifter / misc ----------------------------------------------------------------------
    logic [31:0] lu;
    logic [31:0] hi, lo;
    logic [5:0]  s;
    logic [63:0] fun;
    logic [31:0] misc;
    logic        sh_t;          // the bit a 1-bit shift / rotate moves out (T)
    // ---- result bus ----------------------------------------------------------------------------------------
    logic [1:0]  cls;           // 0 adder, 1 logic, 2 shifter, 3 misc
    logic [31:0] res;
    logic        wr_r;

    // ADDER operand select.  DIV1 (manual 10.19): Rn is shifted left through T (the old Q bit falls out of
    // Rn[31]), then the divisor is subtracted when old Q == M and added otherwise (non-restoring step).
    sub1 = (q == m);
    unique case (op)
      A_ADDC:                                   begin x = b;               y = a;             cin = t;    end
      A_SUB, A_SUBV,
      A_CMPEQ, A_CMPHS, A_CMPGE, A_CMPHI,
      A_CMPGT:                                  begin x = b;               y = ~a;            cin = 1'b1; end
      A_SUBC:                                   begin x = b;               y = ~a;            cin = ~t;   end
      A_NEG:                                    begin x = 32'd0;           y = ~a;            cin = 1'b1; end
      A_NEGC:                                   begin x = 32'd0;           y = ~a;            cin = ~t;   end
      A_DT:                                     begin x = b;               y = 32'hFFFF_FFFF; cin = 1'b0; end
      A_CMPPZ, A_CMPPL:                         begin x = b;               y = 32'd0;         cin = 1'b0; end
      A_DIV1:                                   begin x = {b[30:0], t};    y = sub1 ? ~a : a; cin = sub1; end
      default: /* A_ADD, A_ADDV */              begin x = b;               y = a;             cin = 1'b0; end
    endcase
    sum = {1'b0, x} + {1'b0, y} + {32'd0, cin};
    c   = sum[32];
    v   = (x[31] == y[31]) && (sum[31] != x[31]);   // two's-complement overflow of x + y (+cin)
    z   = (sum[31:0] == 32'd0);
    // DIV1's new Q.  The manual's 8-way switch reduces to Q = Rn[31] ^ tmp1 ^ M, where tmp1 is the borrow of a
    // subtract (!C) or the carry of an add (C), i.e. tmp1 = C ^ sub1; with sub1 = !(oldQ ^ M) that is
    // Q = !(Rn[31] ^ C ^ oldQ).
    q1  = ~(b[31] ^ c ^ q);

    // LOGIC unit
    unique case (op)
      A_AND, A_TST:     lu = a & b;
      A_OR:             lu = a | b;
      A_TAS:            lu = b | 32'h0000_0080;
      A_NOT:            lu = ~a;
      default:          lu = a ^ b;     // A_XOR, A_CMPSTR
    endcase

    // SHIFTER: fun = {hi, lo} >> s, result = fun[31:0]
    hi = b; lo = 32'd0; s = 6'd31;                  // SHAL / SHLL: left by 1
    unique case (op)
      A_ROTL:           begin hi = b;                lo = b;                s = 6'd31; end
      A_ROTR:           begin hi = b;                lo = b;                s = 6'd1;  end
      A_ROTCL:          begin hi = b;                lo = {t, 31'd0};       s = 6'd31; end
      A_ROTCR:          begin hi = {31'd0, t};       lo = b;                s = 6'd1;  end
      A_SHAR:           begin hi = {32{b[31]}};      lo = b;                s = 6'd1;  end
      A_SHLR:           begin hi = 32'd0;            lo = b;                s = 6'd1;  end
      A_SHLL2:          begin hi = b;                lo = 32'd0;            s = 6'd30; end
      A_SHLL8:          begin hi = b;                lo = 32'd0;            s = 6'd24; end
      A_SHLL16:         begin hi = b;                lo = 32'd0;            s = 6'd16; end
      A_SHLR2:          begin hi = 32'd0;            lo = b;                s = 6'd2;  end
      A_SHLR8:          begin hi = 32'd0;            lo = b;                s = 6'd8;  end
      A_SHLR16:         begin hi = 32'd0;            lo = b;                s = 6'd16; end
      // Rm[31] = 0: left by Rm[4:0]; Rm[31] = 1: right by 32 - Rm[4:0] (= (~Rm & 31) + 1, and 32 for Rm[4:0] = 0),
      // filling with Rn[31] (SHAD) or zero (SHLD)
      A_SHAD, A_SHLD:   begin
                          s  = 6'd32 - {1'b0, a[4:0]};
                          hi = a[31] ? ((op == A_SHAD) ? {32{b[31]}} : 32'd0) : b;
                          lo = a[31] ? b : 32'd0;
                        end
      default: ;
    endcase
    fun  = {hi, lo} >> s;
    // the bit a 1-bit shift or rotate sends to T: Rn[31] for the left ones, Rn[0] for the right ones
    sh_t = (op == A_ROTR || op == A_ROTCR || op == A_SHAR || op == A_SHLR) ? b[0] : b[31];

    // MISC wiring
    unique case (op)
      A_EXTSB:          misc = {{24{a[7]}}, a[7:0]};
      A_EXTSW:          misc = {{16{a[15]}}, a[15:0]};
      A_EXTUB:          misc = {24'd0, a[7:0]};
      A_EXTUW:          misc = {16'd0, a[15:0]};
      A_SWAPB:          misc = {a[31:16], a[7:0], a[15:8]};
      A_SWAPW:          misc = {a[15:0], a[31:16]};
      A_XTRCT:          misc = {a[15:0], b[31:16]};
      A_MOVT:           misc = {31'd0, t};
      default:          misc = a;       // A_MOV
    endcase

    // result bus select
    unique case (op)
      A_ADD, A_ADDC, A_ADDV, A_SUB, A_SUBC, A_SUBV, A_NEG, A_NEGC, A_DT, A_DIV1,
      A_CMPEQ, A_CMPHS, A_CMPGE, A_CMPHI, A_CMPGT, A_CMPPZ, A_CMPPL:           cls = 2'd0;
      A_AND, A_OR, A_XOR, A_NOT, A_TST, A_CMPSTR, A_TAS:                      cls = 2'd1;
      A_ROTL, A_ROTR, A_ROTCL, A_ROTCR, A_SHAL, A_SHAR, A_SHLL, A_SHLR,
      A_SHLL2, A_SHLR2, A_SHLL8, A_SHLR8, A_SHLL16, A_SHLR16, A_SHAD, A_SHLD:  cls = 2'd2;
      default:                                                                 cls = 2'd3;
    endcase
    unique case (cls)
      2'd0:    res = sum[31:0];
      2'd1:    res = lu;
      2'd2:    res = fun[31:0];
      default: res = misc;
    endcase
    // forms that set only flags leave Rn alone (r = b, see the header)
    wr_r = !((op == A_TST || op == A_CMPEQ || op == A_CMPHS || op == A_CMPGE || op == A_CMPHI || op == A_CMPGT ||
              op == A_CMPPZ || op == A_CMPPL || op == A_CMPSTR || op == A_DIV0S || op == A_DIV0U || op == A_CLRT ||
              op == A_SETT));
    o.r = wr_r ? res : b;

    // T / Q / M
    o.t = t; o.q = q; o.m = m; o.wr_t = 1'b1; o.wr_qm = 1'b0;
    unique case (op)
      A_ADDC:                    o.t = c;                           // carry out of Rn + Rm + T
      A_SUBC, A_NEGC:            o.t = ~c;                          // borrow out of Rn - Rm - T / 0 - Rm - T
      A_ADDV, A_SUBV:            o.t = v;
      A_CMPEQ:                   o.t = z;                           // Rn - Rm == 0
      A_CMPHS:                   o.t = c;                           // no borrow: Rn >= Rm unsigned
      A_CMPHI:                   o.t = c & ~z;
      A_CMPGE:                   o.t = ~(sum[31] ^ v);              // N == V: Rn >= Rm signed
      A_CMPGT:                   o.t = ~(sum[31] ^ v) & ~z;
      A_CMPPZ:                   o.t = ~sum[31];                    // sum = Rn + 0
      A_CMPPL:                   o.t = ~sum[31] & ~z;
      A_DT:                      o.t = z;                           // Rn - 1 == 0
      A_TST:                     o.t = (lu == 32'd0);
      A_TAS:                     o.t = (b[7:0] == 8'd0);
      A_CMPSTR:                  o.t = (lu[31:24] == 8'd0) || (lu[23:16] == 8'd0) ||
                                       (lu[15:8]  == 8'd0) || (lu[7:0]   == 8'd0);
      A_ROTL, A_ROTR, A_ROTCL, A_ROTCR,
      A_SHAL, A_SHAR, A_SHLL, A_SHLR:
                                 o.t = sh_t;
      A_CLRT:                    o.t = 1'b0;
      A_SETT:                    o.t = 1'b1;
      A_DIV0S:                   begin o.q = b[31]; o.m = a[31]; o.t = b[31] ^ a[31]; o.wr_qm = 1'b1; end
      A_DIV0U:                   begin o.q = 1'b0;  o.m = 1'b0;  o.t = 1'b0;          o.wr_qm = 1'b1; end
      A_DIV1:                    begin o.q = q1;    o.m = m;     o.t = (q1 == m);     o.wr_qm = 1'b1; end
      default:                   o.wr_t = 1'b0;
    endcase
    return o;
  endfunction
endpackage
