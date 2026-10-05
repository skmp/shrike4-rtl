// v1_rstc_tb.sv -- flat-port wrapper around v1_rstc for tb/v1_rstc_tb.cpp.
module v1_rstc_tb
  import v1_soc_pkg::*;
(
  input  logic       clk,
  input  logic       reset_n, sck2_i, wdt_rst_por, wdt_rst_man, hudi_rst, mhit_req, arr_mhit, cpu_rst_req,
  output logic       rst_pin, rst_por, rst_man, rst_cpu,
  output logic [1:0] rst_code
);
  rst_t      rst;
  rst_code_e code;
  v1_rstc u (.clk, .reset_n, .sck2_i, .wdt_rst_por, .wdt_rst_man, .hudi_rst, .mhit_req, .arr_mhit, .cpu_rst_req,
             .rst, .rst_code(code));
  assign {rst_pin, rst_por, rst_man, rst_cpu} = {rst.pin, rst.por, rst.man, rst.cpu};
  assign rst_code = code;
endmodule
