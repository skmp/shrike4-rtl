// soc_vlprintf.h -- the SoC testbench's hook on Verilator's console output (owner WI-SOC; M3-fix, M3-verify F7).
// tb/mk/v1_soc.mk builds every soc_tb with -DVL_PRINTF=soc_vl_printf and this header force-included, so every $display
// of the RTL (verilated.cpp's VL_PRINTF_MT ends in VL_PRINTF) reaches tb/soc_tb.cpp's soc_vl_printf.  It prints the
// text unchanged and counts the lines that start "%Error" (the units' simulation-time protocol checks: v1_biu's
// %Error-BIU, v1_ocache's %Error-OC; Verilator's own %Error) and "%Warning" (v1_sq's "%Warning-SQ: unmeasured size",
// SOC.md 4.2's testbench assertion, which is reported, not failed).  A run with any %Error line fails.
#pragma once
int soc_vl_printf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
