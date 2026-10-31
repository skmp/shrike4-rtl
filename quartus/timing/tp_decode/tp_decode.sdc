# tp_decode_timing runs on a real top-level `clk` pin (v1_soc's Iclk; the Bclk / Pclk domains are clock enables of it, so
# every path is timed as single-cycle). Target 10 ns (100 MHz), as polly2's per-stage harnesses; the actual Fmax is
# read from the STA Fmax Summary regardless of this period.
create_clock -name clk -period 10.000 [get_ports clk]
derive_clock_uncertainty
