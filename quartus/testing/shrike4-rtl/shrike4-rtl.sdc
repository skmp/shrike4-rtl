# shrike4_rtl_test runs on a real top-level `clk` pin: v1_soc's Iclk. The SoC's Bclk and Pclk domains are clock enables of it
# (v1_cpg's bce / pce), not clocks, so this one clock covers the design; every path is timed as single-cycle, which is
# conservative for the bce / pce-qualified logic.
#
# Target 10 ns (100 MHz), as polly2's per-stage harnesses (the console's Iclk is 200 MHz). The actual Fmax is read
# from the STA Fmax Summary regardless of this period.
create_clock -name clk -period 10.000 [get_ports clk]
derive_clock_uncertainty
