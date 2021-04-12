`timescale 100ps / 1ps

module tb(
	input clk48_host,
	input clk48_device,
	output clk12,
	input reset,
	inout usb_d_p,
	inout usb_d_n,
	output usb_pullup,
	output usb_tx_en,
	input [4095:0] test_name,
	output clkdiff
);

// Verify host and device clocks are in sync
assign clkdiff = clk48_host ^ clk48_device;

// Host must have weak pulldowns on both data pins
pulldown(usb_d_n);
pulldown(usb_d_p);
wire led_r;
wire led_g;
wire led_b;

dut dut (
	.clk_clk48(clk48_device),
	.clk_clk12(clk12),
	.reset(reset),
	.usb_d_p(usb_d_p),
	.usb_d_n(usb_d_n),
	.usb_pullup(usb_pullup),
	.usb_tx_en(usb_tx_en),
	.rgb_led_r(led_r),
	.rgb_led_g(led_g),
	.rgb_led_b(led_b)
);

`ifndef VERILATOR
  // Dump waves
  initial begin
    $dumpfile("dump.vcd");
    $dumpvars(0, tb);

//  @(posedge clk12);
//  #(8);
//  reset = 1;
//  @(posedge clk12);
//  #(8);
//  reset = 0;
  end
`endif
endmodule
