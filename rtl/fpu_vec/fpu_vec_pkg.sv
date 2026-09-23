// fpu_vec_pkg.sv -- SH7091 vector/transcendental FPU block (FIPR / FSCA / FSRRA): shared declarations.
//
// The block is the arithmetic unit of Hitachi US 6,038,582 (FIG. 4 / 29 / 34): four Booth multipliers FM0..FM3,
// one aligner per product, a four-input adder and one normalise/positive/round circuit.  FIPR uses it once;
// FSCA and FSRRA sequence it (4 and 3 multiply steps) through feedback paths from the product registers to the
// multiplier input latches, with a coefficient ROM in place of the register file.  Widths below are the ones
// pinned by the bit-exact FIPR model (model/fipr/fipr_ref.h) and the FSCA/FSRRA structural models
// (model/fsca/sh4_fsca.h, model/fsrra/fsrra_ref.h; shareable C for all of it: model/sh4_fpu_approx.h).
package fpu_vec_pkg;
  // ---- datapath widths -------------------------------------------------------------------------------------
  localparam int RECW  = 28;   // Booth-recoded operand (port B): two's complement, 14 radix-4 digits over bits -1..27.
                               // FIPR presents the 24-bit FVn mantissa zero-extended; FSCA/FSRRA feed back signed values.
  localparam int MULW  = 25;   // multiplicand (port A): two's complement; FIPR = {1'b0, FVm mantissa}; FSCA dx is signed.
  localparam int RAWW  = 56;   // truncated row sum of the array (always a multiple of 2^16)
  localparam int PRODW = 29;   // product register M[n] = (raw + 2^19) >>> 20 : 28 bits + sign
  localparam int ALNW  = 32;   // aligner output / adder input
  localparam int SUMW  = 35;   // four-input adder result

  localparam logic [RAWW-1:0] ROUND_K = 56'd1 << 19;   // the rounding constant every product gets before >> 20

  // ---- opcodes --------------------------------------------------------------------------------------------
  typedef enum logic [1:0] { OP_FIPR = 2'd0, OP_FSCA = 2'd1, OP_FSRRA = 2'd2 } op_e;

  // ---- multiplier input-latch sources (the selectors 302..312 of FIG. 29, generalised) ----------------------
  // Port A (multiplicand) sources
  typedef enum logic [2:0] {
    A_HOLD = 3'd0,   // keep the latch ("value is held by restricting updating of input latch")
    A_OPA  = 3'd1,   // FIPR operand mantissa
    A_DX   = 3'd2,   // the offset dx from the sign expander
    A_TAB  = 3'd3,   // a table value (FSCA: sine field S of the addressed entry)
    A_FB   = 3'd4    // feedback: product register M[fb_src] >>> fb_sh
  } asel_e;
  // Port B (Booth-recoded) sources
  typedef enum logic [2:0] {
    B_HOLD = 3'd0,
    B_OPB  = 3'd1,   // FIPR operand mantissa
    B_DX   = 3'd2,   // dx (FSRRA cubic final product recodes dx)
    B_CONST= 3'd3,   // a stored coefficient (KC, C1F, C3<<5, REC1..3)
    B_FB   = 3'd4    // feedback: M[fb_src] >>> fb_sh
  } bsel_e;

  // one multiplier's control for one step
  typedef struct packed {
    asel_e       a_sel;
    bsel_e       b_sel;
    logic [1:0]  fb_src;     // which product register feeds back (A_FB / B_FB)
    logic [2:0]  fb_sh;      // arithmetic right shift on the feedback path (0, 3 or 5 in the models)
    logic [1:0]  const_sel;  // which stored coefficient (per op, see fpu_vec_top)
    logic        tab_cos;    // FSCA: use the cosine entry (node + 16) instead of the sine entry
  } mul_ctl_t;

  // adder-input source per slot: slot i's own product register M[i], the table constant C0 (slot 1 only) or zero
  typedef enum logic [1:0] { ADD_ZERO = 2'd0, ADD_PROD = 2'd1, ADD_C0 = 2'd2 } addsrc_e;
  typedef struct packed {
    addsrc_e     src;
    logic [5:0]  sh;         // aligner right shift (FIPR: Emax - E[n]; FSCA/FSRRA: fixed per term)
    logic        inv;        // subtract this term (one's-complement invert + hot one, see fpu_align)
  } add_ctl_t;
endpackage
