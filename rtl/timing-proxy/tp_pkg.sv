// tp_pkg.sv -- types of the SH7091 pipeline timing proxy (rtl/timing-proxy, see README.md).
package tp_pkg;
  `include "tp_forms.svh"

  localparam int SYSW = SYSN;

  // one dynamic instruction of the trace (program order; the functional side supplies what timing needs)
  typedef struct packed {
    logic [31:0] seq;      // position in the trace (reported back at issue)
    logic [31:0] pc;
    logic [15:0] op;
    logic [2:0]  md;       // {FPSCR.FR, FPSCR.SZ, FPSCR.PR} while it executes
    logic        tk;       // a branch that is taken
  } tr_t;

  // operand decoder output (tp_decode.sv, generated): register masks by role
  typedef struct packed {
    logic        valid;
    form_e       form;
    logic [15:0] ga;       // GPR address base / index
    logic [15:0] gd;       // GPR store data
    logic [15:0] gs;       // GPR other sources
    logic [15:0] gwl;      // GPR load destination
    logic [15:0] gwu;      // GPR address update (@Rm+ / @-Rn)
    logic [15:0] gw;       // GPR other destinations
    logic [7:0]  bkr;      // R0_BANK..R7_BANK read / written
    logic [7:0]  bkw;
    logic [31:0] fs;       // FP registers (physical bank*16 + i) read as operands
    logic [31:0] fd;       // FP store data
    logic [31:0] fw;       // FP destinations
    logic [SYSW-1:0] sr;   // system registers read / written (SYS_*)
    logic [SYSW-1:0] sw;
  } dec_t;

  `include "tp_timing.svh"
endpackage
