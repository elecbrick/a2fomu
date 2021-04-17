#
# apple2.py - Part of a2fomu - Copyright (c) 2020-2021 Doug Eaton
#
# This file is part of a2fomu which is released under the two clause BSD
# licence.  See file LICENSE in the project root directory or visit the
# project at https://github.com/elecbrick/a2fomu for full license details.

from migen import Module, Signal, If, ResetSignal, ClockSignal, Instance, Cat
from litex.soc.interconnect.csr import CSR, AutoCSR, CSRStatus, CSRStorage, CSRField
from litex.soc.integration.doc import ModuleDoc
from migen.genlib import fifo
from os import getenv


class Apple2(Module, AutoCSR):
    def __init__(self, platform, mem, minimal=False):

        self.intro = ModuleDoc("""FOMU Apple II+
            A virtual computer within a virtual computer inside a USB port.
            Instantiate 6502 processor with 1MHz core clock.
            Tie in to system memory as a wishbone master.
            Create virtual keyboard, video display and disk drive.
            """)

        addr = Signal(16)
        dout = Signal(8)
        din  = Signal(8)
        wren = Signal()
        iosel = Signal()
        ior_addr = Signal(8)
        r_memsel = Signal()
        w_memsel = Signal()
        clk_en = Signal()       # Clock divider limits CPU activity
        active = Signal()       # CPU is active this cycle
        available = Signal()    # Key press ready for reading
        div1M=4
        idlecount = Signal(div1M)   # Counter to slow CPU clock

        # Disk II Controller Registers
        disk_phase   = Signal(4)
        disk_motor   = Signal()
        disk_drive   = Signal()
        #disk_write   = Signal()
        disk_reading = Signal()     # DOS trying to read sector
        disk_data_available = Signal()     # Data available for DOS to read
        disk_data_wanted    = Signal()     # Data wanted by DOS
        disk_read    = Signal()     # Data read by DOS so clear readable

        simulation = getenv("SIMULATION")
        synthesis = not simulation

        # Wishbone visible registers
        self.control = CSRStorage(fields=[
            CSRField("Reset", reset=1 if synthesis else 0,  # auto-start in sim
                description="6502 Reset line - 1: Reset Asserted, 0: Running"),
            CSRField("RWROM", size=1, description="Allow writes to ROM"),
            #CSRField("Pause", description="Halt processor allowing stepping"),
            #CSRField("Step",  description="Single step 6502 one clock cycle"),
            #CSRField("NMI", size=1, description="Non-maskable interrupt"),
            #CSRField("IRQ", size=1, description="Maskable interrupt request"),
            #CSRField("RDY", size=1, description=""),
            CSRField("Divisor", size=div1M, offset=8,
                reset=11 if synthesis else 0,  # Over-clock simulation
                description="Clock divider minius 1: 11 for 1MHz, 0 for 12MHz"),
            #CSRField("DI", size=8, offset=16, description=""),
        ])
        self.keyboard=CSRStorage(8, write_from_dev=True,
                description="Keyboard input ($C000)")
        self.strobe=CSR(1) #, description="Keyboard strobe ($C010)")
        self.screen=CSRStatus(fields=[
            CSRField("Character", size=8,
                description="Character written to screen"),
            CSRField("Valid", size=1,
                description="Character is valid"),
            CSRField("More", size=1,
                description="Additional characters are available to read"),
            #CSRField("Move", size=1,
            #    description="Character is not adjacent to previous"),
            CSRField("Repeat", size=1, offset=11,
                description="Previous character repeated to current position"),
            CSRField("ScrollStart", size=1, offset=12,
                description="Start of scroll region detected"),
            CSRField("ScrollEnd", size=1, offset=13,
                description="End of scroll region"),
            CSRField("Horizontal", size=6, offset=16,
                description="Location of current character in screen memory"),
            CSRField("Vertical", size=5, offset=24,
                description="Location of current character in screen memory"),
            ], description="Video Display Output")
        self.diskctrl=CSRStatus(fields=[
            CSRField("Phase", size=4,
                description="Four phases of the track selection stepper motor"),
            CSRField("Motor", size=1,
                description="Drive is spinning"),
            CSRField("Drive", size=1,
                description="Drive select: drive 1 if clear, drive 2 if set"),
            CSRField("Wanted", size=1,
                description="Drive is waiting for data"),
            CSRField("Pending", size=1,
                description="Drive has not yet read data written"),
            #CSRField("WriteMode", size=1,
            #    description="Drive is reading when clear, writing when set"),
            ], description="Disk drive control ($C0EX)")
        self.diskdata=CSRStorage(8,
            description="Disk drive data ($C0EC)")

        #self.bus=CSRStatus(32, fields=[
        #    CSRField("Addr", size=16, description="Address bus"),
        #    CSRField("Data", size=8, description="Data bus"),
        #    CSRField("WrEn", size=1, description="Write enable"),
        #    ], description="Address and data bus")
        #self.debug=CSRStatus(32, fields=[
        #    CSRField("PC", size=8,
        #        description="Program counter"),
        #    CSRField("A", size=8,
        #        description="Accumulator"),
        #    CSRField("X", size=8,
        #        description="X index register"),
        #    CSRField("Y", size=8,
        #        description="Y index register"),
        #    ], description="Address and data bus")

        if not minimal:
            # The minimal configuration provides all registers the host needs to
            # see so software can run unmodified. However, it does not implement
            # the 6502 to save gates. The video driver is also greatly reduced.

            # TODO eliminate wire [31:0] apple2_display_fifo_wrport_dat_r
            self.submodules.display_fifo= fifo.SyncFIFOBuffered(width=32, depth=256)

            self.comb += [
                mem.addr6502.eq(addr),
                mem.din6502.eq(dout),
                mem.wren6502.eq(wren),
                self.strobe.w.eq(available),
                #self.bus.fields.Addr.eq(addr),
                #self.bus.fields.Data.eq(dout),
                #self.bus.fields.WrEn.eq(wren),
                If(addr[8:16]==0xC0,
                    iosel.eq(1),
                    w_memsel.eq(0),
                    mem.access6502.eq(0)
                ).Else(
                    iosel.eq(0),
                    w_memsel.eq(1),
                    mem.access6502.eq(active)
                ),
                disk_read.eq(0),
                If(r_memsel,
                    din.eq(mem.dout6502),
                ).Else(
                    # I/O Read Address Decoder (reading but not from memory)
                    If(ior_addr[4:8]==0x0,
                        din.eq(Cat(self.keyboard.storage[0:7], available)),
                    ),
                    # Disk II Controller Card in slot 6  (0x8 | 0x6)
                    # The only data to be read are locations C and D. Simplify the
                    # logic to only look at bit 0 of the address.
                    If(ior_addr[4:8]==0xE,
                        din[0:7].eq(self.diskdata.storage[0:7]),
                        # Write protect - TODO write is not supported
                        #If(ior_addr[0],
                        #    # Return high bit set - protected
                        #    din[7].eq(1)
                        #).Else(
                        disk_read.eq(1),
                        If(disk_data_available | self.diskdata.re,
                            # Return byte given by host
                            din[7].eq(self.diskdata.storage[7]),
                        ).Else(
                            # Return high bit clear - data not available
                            din[7].eq(0),
                        ),
                        #),
                    ),
                ),
                active.eq(clk_en & self.display_fifo.writable),
            ]

            self.sync += [
                # Slow clock to prototypical speed or as configured by user
                If(idlecount==self.control.fields.Divisor,
                    idlecount.eq(0),
                    clk_en.eq(1),
                ).Else(
                    idlecount.eq(idlecount+1),
                    clk_en.eq(0),
                ),
                # Read (DI) follows Write (AB/DO) by one clock cycle
                If(active,
                    r_memsel.eq(w_memsel),
                    ior_addr.eq(addr[0:8]),
                ),
                # Keyboard key available when written by host
                If(self.keyboard.re,
                    available.eq(1),
                ),
                If(iosel,
                    # I/O Write Address Decoder
                    If(addr[4:8]==0x1,
                        # KBDSTRB
                        # Strobe cleared on read or write to KBDSTRB
                        # Any read or write to this address clears the pending key
                        available.eq(0),
                    ),
                ),

            ]


            self.specials += Instance("cpu",
                i_clk = ClockSignal(),
                i_reset = ResetSignal() | self.control.storage[0],
                i_DI = din,
                # &(self.rand.we|self.cfg.storage[1])),

                o_AB = addr,  # .dat_w,
                o_DO = dout,  # .dat_w,
                o_WE = wren,

                i_IRQ = False,  # self.control.fields.IRQ,
                i_NMI = False,  # self.control.fields.NMI,  # seed.re,
                i_RDY = active, # self.control.fields.RDY,
            )
            platform.add_source("rtl/verilog-6502/cpu.v")
            platform.add_source("rtl/verilog-6502/ALU.v")

#===============================================================================
#       Disk Drive - Emulate Disk II controller in slot 6
#===============================================================================
# The Disk II controller card has 16 addressable locations. Eight of these are
# dedicated to moving the arm, two for motor control, two for drive selection
# and four that handle read, write, and write protection detection.
#===============================================================================

            self.comb += [
                self.diskctrl.fields.Phase.eq(disk_phase),
                self.diskctrl.fields.Motor.eq(disk_motor),
                self.diskctrl.fields.Drive.eq(disk_drive),
                #self.diskctrl.fields.WriteMode.eq(disk_write),
                self.diskctrl.fields.Wanted.eq(disk_data_wanted),
                self.diskctrl.fields.Pending.eq(disk_data_available),
            ]

            self.sync += [
                If(self.diskdata.re,
                    disk_data_available.eq(1),
                ),
                # Set false again on read
                If(active & disk_read,
                    disk_reading.eq(0),
                    If(disk_data_available,
                        disk_data_wanted.eq(0),
                    ).Else(
                        disk_data_wanted.eq(1),
                    ),
                    disk_data_available.eq(0),
                ),
                If(iosel,
                    # Disk II Controller Card in slot 6

                    # C0E0 PHASEOFF  Stepper motor phase 0 off.
                    # C0E1 PHASEON   Stepper motor phase 0 on.
                    # C0E2 PHASE1OFF Stepper motor phase 1 off.
                    # C0E3 PHASElON  Stepper motor phase 1 on.
                    # C0E4 PHASE2OFF Stepper motor phase 2 off.
                    # C0E5 PHASE2ON  Stepper notor phase 2 on.
                    # C0E6 PHASE3OFF Stepper motor phase 3 off.
                    # C0E7 PHASE3ON  Stepper motor phase 3 on.
                    # C0E8 MOTOROFF  Turn motor off.
                    # C0E9 MOTORON   Turn motor on.
                    # C0EA DRV0EN    Engage drive 1.
                    # C0EB DRV1EN    Engage drive 2.
                    # C0EC Q6L       Strobe Data Latch for I/O.
                    # C0ED Q6H       Load Data Latch.
                    # C0EE Q7H       Prepare latch for input (read from disk).
                    # C0EF Q7L       Prepare latch for output (write to disk).

                    # Q7L with Q6L = Read
                    # Q7L with Q6H = Sense Write Protect
                    # Q7H with Q6L = Write
                    # Q7H with Q6H = Load Write Latch

                    If(addr[4:8]==0xE,  # (8|6) 
                        # Addresses 0-7 simply update a bit in the status register
                        If(addr[0:4]==0x0, disk_phase[0].eq(0)),
                        If(addr[0:4]==0x1, disk_phase[0].eq(1)),
                        If(addr[0:4]==0x2, disk_phase[1].eq(0)),
                        If(addr[0:4]==0x3, disk_phase[1].eq(1)),
                        If(addr[0:4]==0x4, disk_phase[2].eq(0)),
                        If(addr[0:4]==0x5, disk_phase[2].eq(1)),
                        If(addr[0:4]==0x6, disk_phase[3].eq(0)),
                        If(addr[0:4]==0x7, disk_phase[3].eq(1)),
                        # Likewise, motor active and drive select update status
                        If(addr[0:4]==0x8, disk_motor.eq(0)),
                        If(addr[0:4]==0x9, disk_motor.eq(1)),
                        If(addr[0:4]==0xA, disk_drive.eq(0)),
                        If(addr[0:4]==0xB, disk_drive.eq(1)),
                        # Write is ignored and read must be delayed one clock tick
                        If(addr[0:4]==0xC, disk_reading.eq(1)),
                        #If(addr[0:4]==0xD, disk_ior_wp.eq(1)),
                        #If(addr[0:4]==0xE, disk_write.eq(0)),
                        #If(addr[0:4]==0xF, disk_write.eq(1)),
                    ),
                ),
            ]

#===============================================================================
#       Video Output - Text Mode
#===============================================================================
# The Apple II screen memory contains 8 segments containing 3 rows each in a 128
# byte block leaving 8 unused bytes in each of the 8 blocks To assist with
# scroll detection, we convert memory addresses to screen coordinates.
#===============================================================================

            # Video memory - Frame Buffer access shortcuts
            fbsel = Signal()
            fb_r = Signal()
            fb_w = Signal()

            # Conversion from memory address to X,Y screen coordinates
            segment = Signal(3)
            triple = Signal(7)
            third = Signal(2)
            horiz = Signal(6)
            vert = Signal(5)
            move = Signal()

            scroll_active = Signal()
            scroll_match = Signal()
            scroll_start = Signal()
            scroll_end = Signal()
            scroll_read = Signal()
            scroll_write_valid = Signal()
            scroll_next_col = Signal()
            scroll_next_row = Signal()
            scroll_sequential = Signal()
            read_horiz = Signal(6)
            read_vert = Signal(5)

            repeat_active = Signal()
            repeat_match = Signal()
            repeat_start = Signal()
            repeat_end = Signal()
            repeat_next_col = Signal()
            repeat_next_row = Signal()
            repeat_sequential = Signal()

            # Registers shared by scroll and repeat compression circuits
            #horiz_start = Signal(max=40)
            #vert_start = Signal(max=24)
            #horiz_end = Signal(max=40)
            #vert_end = Signal(max=24)
            prev_horiz = Signal(max=40)
            prev_vert = Signal(max=24)
            prev_char = Signal(8)
            prev_start = Signal()
            push_save = Signal()
            push_saving = Signal()

            fifo_out = Signal(32)

            self.comb += [
                # Detect access to frame memory: Address range 0x0400-0x7ff
                fbsel.eq((addr[10:15]==0x1) & active),
                fb_r.eq(fbsel&~wren),
                fb_w.eq(fbsel&wren),
                # Convert memory address to X,Y coordinates
                segment.eq(addr[7:10]),
                triple.eq(addr[0:7]),
                # TODO This generates reg - change to cause only wire
                If(triple>=80,
                    third.eq(2),
                    horiz.eq(addr[0:7]-80),
                ).Else(If(triple>=40,
                    third.eq(1),
                    horiz.eq(addr[0:7]-40),
                ).Else(
                    third.eq(0),
                    horiz.eq(addr[0:7]),
                )),
                vert.eq(Cat(segment, third)),

                # TODO Detect scroll - frame buffer read immediately followed by
                # frame buffer write to character on previous line, directly above.
                # Scroll is Right to Left (asm: DEY at FC90 in Autostart ROM)
                scroll_match.eq((horiz==read_horiz) & (vert+1==read_vert)),
                    # & (din==read_char))  <== TODO Need to delay din by 1 cycle
                scroll_write_valid.eq(scroll_read & fb_w & scroll_match),
                scroll_start.eq(scroll_write_valid & ~scroll_active),
                # Scroll ends on any write that does not follow the required pattern
                scroll_end.eq(scroll_active & fb_w & ~scroll_write_valid),
                scroll_next_col.eq((horiz+1==prev_horiz) & (vert==prev_vert)),
                scroll_next_row.eq((horiz==39) & (prev_horiz==0) &
                    (vert==prev_vert+1)),
                scroll_sequential.eq(scroll_next_col | scroll_next_row),

                # Detect repeated charaters (spaces)
                # Clear is Left to Right (asm: INY at FCA2 in Autostart ROM)
#               repeat_match.eq(fb_w & (dout==prev_char)),
#               repeat_start.eq(repeat_match & repeat_sequential & ~repeat_active),
#               repeat_end.eq(fb_w & repeat_active &
#                   (~repeat_match |~repeat_sequential)),
#               repeat_next_col.eq((horiz==prev_horiz+1) & (vert==prev_vert)),
#               repeat_next_row.eq((horiz==0) & (prev_horiz==39) &
#                   (vert==prev_vert+1)),
#               repeat_sequential.eq(repeat_next_col | repeat_next_row),
#               repeat_sequential.eq(repeat_next_col),  # This or the previous one

                # Place writes in the fifo
                self.display_fifo.din[    8].eq(0), # Valid is calculated
                self.display_fifo.din[    9].eq(0), # More is calculated
                #self.display_fifo.din[   10].eq(move),
                self.display_fifo.din[   11].eq(repeat_end),
                If(push_save,
                    self.display_fifo.din[ 0: 8].eq(prev_char),
                    self.display_fifo.din[   12].eq(prev_start),
                    self.display_fifo.din[   13].eq(scroll_end),
                    #self.display_fifo.din[14:16].eq(0), # Reserved
                    self.display_fifo.din[16:22].eq(prev_horiz), # 2 bits padding
                    self.display_fifo.din[24:29].eq(prev_vert),  # 3 bits padding
                ).Elif(push_saving,
                    # push_save will be valid on the next cycle - so push previous
                    self.display_fifo.din[ 0: 8].eq(prev_char),
                    #self.display_fifo.din[    8].eq(0), # Valid is calculated
                    #self.display_fifo.din[    9].eq(0), # More is calculated
                    #self.display_fifo.din[   10].eq(move),
                    #self.display_fifo.din[   11].eq(repeat_end),
                    self.display_fifo.din[   12].eq(scroll_start),
                    self.display_fifo.din[   13].eq(scroll_end),
                    #self.display_fifo.din[14:16].eq(0), # Reserved
                    self.display_fifo.din[16:22].eq(prev_horiz), # 2 bits padding
                    self.display_fifo.din[24:29].eq(prev_vert),  # 3 bits padding
                ).Else(
                    #self.display_fifo.din.eq(Cat(dout, horiz, vert,
                    #    move, scroll_start, scroll_end, repeat_start, repeat_end)),
                    self.display_fifo.din[ 0: 8].eq(dout),
                    #self.display_fifo.din[    8].eq(0), # Valid is calculated
                    #self.display_fifo.din[    9].eq(0), # More is calculated
                    #self.display_fifo.din[   10].eq(move),
                    #self.display_fifo.din[   11].eq(repeat_end),
                    self.display_fifo.din[   12].eq(scroll_start),
                    self.display_fifo.din[   13].eq(scroll_end),
                    #self.display_fifo.din[14:16].eq(0), # Reserved
                    self.display_fifo.din[16:22].eq(horiz), # 2 bits padding
                    self.display_fifo.din[24:29].eq(vert),  # 3 bits padding
                ),
                self.display_fifo.we.eq(push_save | repeat_end |
                        scroll_start | scroll_end |
                        (fb_w & ~scroll_active & ~repeat_active & ~repeat_start)),
                push_saving.eq(((repeat_end & ~repeat_sequential) | scroll_end) &
                        ~push_save),

                # Retrieve characters from fifo
                self.display_fifo.re.eq(self.screen.we),
                self.screen.we.eq(self.display_fifo.re),
                self.screen.fields.Valid.eq(self.display_fifo.readable),
                self.screen.fields.More.eq(self.display_fifo.readable),
                self.screen.fields.Character.eq(fifo_out[0:8]),
                self.screen.fields.Horizontal.eq(fifo_out[16:22]),
                self.screen.fields.Vertical.eq(fifo_out[24:29]),
                #self.screen.fields.Move.eq(fifo_out[10]),
                self.screen.fields.Repeat.eq(fifo_out[11]),
                self.screen.fields.ScrollStart.eq(fifo_out[12]),
                self.screen.fields.ScrollEnd.eq(fifo_out[13]),
            ]

            self.sync += [
                fifo_out.eq(self.display_fifo.dout),

                # Scroll
                If(scroll_start,
                    scroll_active.eq(1),
                    #horiz_start.eq(horiz),
                    #vert_start.eq(vert),
                    #horiz_end.eq(horiz),
                    #vert_end.eq(vert),
                ),
                If(scroll_end,
                    push_save.eq(1),
                    scroll_active.eq(0),
                    # These happen on any write to the frame buffer
                    #prev_horiz.eq(horiz),
                    #prev_vert.eq(vert),
                    #prev_char.eq(dout),
                    prev_start.eq(scroll_start),
                ),
                If(fb_r,
                    If((scroll_read & (scroll_sequential | ~scroll_active)) |
                        (repeat_active & repeat_sequential),
                        # A faithful 6502 model issues a read of the target
                        # address before the write cycle of an indirect store
                        # instruction.  Do nothing if we suspect the current read is
                        # actually part of a store instruction.
                    ).Else(
                        scroll_read.eq(1),
                        read_horiz.eq(horiz),
                        read_vert.eq(vert),
                        # TODO read_char should be din on the next clock cycle
                        # read_char.eq(dout),
                    ),
                ),

                # Write to the frame buffer: remember the location and character
                # that generate the sequential and repeat signals which are used
                # by the scroll and clear functions. Also, update scroll signals.
                If(fb_w,
                    scroll_read.eq(0),
                    prev_vert.eq(vert),
                    prev_horiz.eq(horiz),
                    prev_char.eq(dout),

                    # Repeat - Mostly needed for screen clearing operations but made
                    # generic to conserve bandwidth and allow any character to be
                    # repeated.
                    If(repeat_match & repeat_sequential,
                        # Supress output, begin sequence if not started already
                        # Store location and character as this will be needed if the
                        # following write is not sequential
                        # Setting repeat is redundant if already active
                        repeat_active.eq(1),
                    ).Elif(repeat_active & ~repeat_sequential,
                        # The cursor moved and we must terminate repeat mode
                        # indicating the last character in the sequence.  This is
                        # required whether or not the same character is present.
                        # Output saved location with Repeat flag set to mark end.
                        # Then output current signal set on next cycle
                        push_save.eq(1),
                        prev_start.eq(scroll_start),
                        repeat_active.eq(0),
                    ).Else(
                        # Push current character on stack either because:
                        # a. character is different breaking repeat
                        # b. character is different preventing repeat
                        # c. location is not sequential breaking repeat
                        # d. location is not sequential preventing repeat
                        # Cases a,c need to clear repeat. This is irrelevant for b,d
                        repeat_active.eq(0),
                    ),

                ),
                # We can safely use the cycle after a write to frame memory as a
                # second push into the character fifo knowing that the only time the
                # 6502 has two consecutive write cycles is the JSR instruction which
                # writes the return address onto the stack, and the RMW instructions
                # INC, DEC, LSR, ASR, ROL, ROR which write the original and the new
                # values in two consecutive cycles. It is extremely poor programming
                # practice to keep the stack inside the frame buffer while clearing
                # or scrolling the screen so no special handling of these cases is
                # taken.
                If(push_save,
                    # Auto-clear after one clock cycle
                    push_save.eq(0),
                ),

            ]
