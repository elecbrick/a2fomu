#
# pdpspram.py - Part of a2fomu - Copyright (c) 2020-2021 Doug Eaton
#
# This file is part of a2fomu which is released under the two clause BSD
# licence.  See file LICENSE in the project root directory or visit the
# project at https://github.com/elecbrick/a2fomu for full license details.

# Pseudo Dual-Ported RAM 

from migen import *
from litex.soc.interconnect import wishbone

"""
Pseudo Dual-Ported RAM that works similar to the original Apple ][ Woz design in
that CPU and Video display accesses are time division multiplexed. However,
the original memory ran with a 2MHz cycle giving 50% of the accesses to each
client while this memory runs at 12MHz allowing 11 access by the video driver
for each one from the 6502.

This module implements the Apple ][ 6502 CPU's RAM region and also makes it
available to a Risc-V host processor or other logic to extract the images, drive
the video and manage USB communication.

Inputs from both the 32-bit 12MHz Wishbone interface and the 8-bit 1MHz 6502
system bus are multiplexed to the ICE40 Single Port RAM (SPRAM) primitives.
To allow for single cycle access over the 32-bit Wishbone bus, two of the
16-bit wide SPRAMs are width-cascaded.
"""

class PDP_SPRAM(Module):
    def __init__(self, sim):
        self.bus = wishbone.Interface(32)

        # Detect rising edge of 6502 clock and grant memory access to that
        # processor on the following memory clock cycle.  Wishbone interface
        # given access on all other clock cycles
        #prevclk6502 = Signal()
        read6502 = Signal()
        access6502 = Signal()
        hold_en6502 = Signal()
        shift = Signal(2)
        read_shift = Signal(2)

        # Memory block (bus to pair)
        addr = Signal(14)
        din = Signal(32)
        dout = Signal(32)
        wren = Signal()
        wrenmask = Signal(8)
        # 6502 (Apple II)
        addr6502 = Signal(16)
        din6502 = Signal(8)
        dout6502 = Signal(8)
        hold6502 = Signal(8)
        transparent6502 = Signal(8)
        wren6502 = Signal()
        wrenmask6502 = Signal()
        # Wishbone (Risc-V)
        addrwb = Signal(14)
        dinwb = Signal(32)
        doutwb = Signal(32)
        wrenwb = Signal()
        wrenmaskwb = Signal(4)
        accesswb = Signal()
        # The parent module is left to attach these signal to the CPU bus
        self.addr6502=addr6502
        self.din6502=din6502
        self.dout6502=dout6502
        self.wren6502=wren6502
        self.wrenmask6502=wrenmask6502
        self.access6502=access6502

        self.comb += [
            # Access granted to 6502 on rising edge of its clock
            #access6502.eq(self.cpu.clk & ~prevclk6502),
            shift.eq(addr6502[0:2]),
            # Assign Wishbone signals to mux inputs
            addrwb.eq(self.bus.adr[:14]),
            dinwb.eq(self.bus.dat_w),
            doutwb.eq(dout),
            wrenwb.eq(self.bus.we & self.bus.stb & self.bus.cyc),
            wrenmaskwb.eq(self.bus.sel),
            self.bus.dat_r.eq(doutwb),
            # Assign 6502 signals to mux inputs
            Case(read_shift, {
                0: transparent6502.eq(dout[0:8]),
                1: transparent6502.eq(dout[8:16]),
                2: transparent6502.eq(dout[16:24]),
                3: transparent6502.eq(dout[24:32]),
            }),
            # Attach appropriate master to memory
            If(hold_en6502,
                dout6502.eq(transparent6502),
            ).Else(
                dout6502.eq(hold6502),
            ),
            If(access6502,
                addr.eq(addr6502[2:16]),
                din.eq(Replicate(din6502, 4)),
                wren.eq(wren6502),
                # SPRAM MASKWREN is nibble based so replicate byte enables
                #wrenmask.eq(0),
                Case(shift, {
                    0: wrenmask[0:2].eq(Replicate(1, 2)),
                    1: wrenmask[2:4].eq(Replicate(1, 2)),
                    2: wrenmask[4:6].eq(Replicate(1, 2)),
                    3: wrenmask[6:8].eq(Replicate(1, 2)),
                }),
            ).Else(
                addr.eq(addrwb),
                din.eq(dinwb),
                wren.eq(wrenwb),
                # SPRAM MASKWREN is nibble based so replicate byte enables
                wrenmask.eq(Cat(wrenmaskwb[0], wrenmaskwb[0],
                    wrenmaskwb[1], wrenmaskwb[1],
                    wrenmaskwb[2], wrenmaskwb[2],
                    wrenmaskwb[3], wrenmaskwb[3])),
            )
        ]

        self.sync += [
            # 6502 read data trails address by one clock
            read_shift.eq(shift),
            hold_en6502.eq(access6502),
            If(hold_en6502,
                hold6502.eq(transparent6502),
            ),
            #prevclk6502.eq(cpu6502.clk),
            If(access6502,
                self.bus.ack.eq(0),
            ).Else(
                accesswb.eq(self.bus.stb & self.bus.cyc & ~accesswb),
                # Ensure access does not get reaserted due to delay
                self.bus.ack.eq(accesswb & self.bus.stb & self.bus.cyc),
            )
        ]

        if sim:
            self.mem = Memory(32, 64*1024//(32//8), init=[])
            port = self.mem.get_port(write_capable=True, we_granularity=8,
                mode=READ_FIRST)
            self.specials += self.mem, port
            self.comb += [
                port.adr.eq(addr),
                port.dat_w.eq(din),
                dout.eq(port.dat_r)
            ]
            self.comb += [port.we[i].eq(wren&wrenmask[i*2]) for i in range(4)],
        else:
            for w in range(2):
                self.specials += Instance("SB_SPRAM256KA",
                    i_ADDRESS=addr,
                    i_DATAIN=din[16*w:16*(w+1)],
                    i_MASKWREN=wrenmask[4*w:4*(w+1)],
                    i_WREN=wren,
                    i_CHIPSELECT=0b1,
                    i_CLOCK=ClockSignal("sys"),
                    i_STANDBY=0b0,
                    i_SLEEP=0b0,
                    i_POWEROFF=0b1,
                    o_DATAOUT=dout[16*w:16*(w+1)]
                )
