#!/usr/bin/env python3
#
# a2fomu_build.py - Part of a2fomu - Copyright (c) 2020-2021 Doug Eaton
#
# This file contains a significant contribution from foboot-bitstream.py
# which wass made available under the Apache License, Version 2.0:
#           http://www.apache.org/licenses/LICENSE-2.0

# Import the litex build environment to integrate the deps/ directory
# external programs this lxbuildenv project relies on.
lx_dependencies = ["riscv", "icestorm", "yosys", "nextpnr-ice40"]
import lxbuildenv

# base hardware definitions: migen
from migen import Module, Signal, Instance, ClockDomain, If
from migen.fhdl.specials import TSTriple
from migen.fhdl.structure import ResetSignal
from migen.fhdl.decorators import ClockDomainsRenamer
# SoC modules and integrator: LiteX
from litex.build.lattice.platform import LatticePlatform
from litex.build.generic_platform import Pins, Subsignal
from litex.build.sim.platform import SimPlatform
from litex.soc.integration.doc import AutoDoc, ModuleDoc
from litex.soc.integration.soc_core import SoCCore
from litex.soc.cores.cpu import CPUNone
from litex.soc.integration.builder import Builder
from litex.soc.interconnect import wishbone
from litex.soc.cores import up5kspram, spi_flash
from litex_boards.targets.fomu import _CRG
import litex.soc.doc as lxsocdoc
# USB controller: ValentyUSB
from valentyusb.usbcore import io as usbio
from valentyusb.usbcore.cpu import epmem, unififo, epfifo, dummyusb, eptri
from valentyusb.usbcore.endpoint import EndpointType
# Fomu and ice40 modules:
from rtl.fomutouch import TouchPads
from rtl.romgen import RandomFirmwareROM, FirmwareROM
from rtl.sbwarmboot import SBWarmBoot

from rtl.apple2 import Apple2
from rtl.pdpspram import PDP_SPRAM

# Generic Python modules
import argparse
import os


# Simulations Interface: Create I/O pins that can interface with standard test
# suites.
sim_io = [
    # define top level connection between FOMU and simulator
    ("clk", 0,
        Subsignal("clk48", Pins(1)),
        Subsignal("clk12", Pins(1)),
    ),
    ("reset", 0, Pins(1)),

    ("user_led_n", 0, Pins(1)),
    ("rgb_led", 0,
        Subsignal("r", Pins(1)),
        Subsignal("g", Pins(1)),
        Subsignal("b", Pins(1)),
    ),

    ("touch_pins", 0,
        Subsignal("user_touch_0", Pins(1)),
        Subsignal("user_touch_1", Pins(1)),
        Subsignal("user_touch_2", Pins(1)),
        Subsignal("user_touch_3", Pins(1)),
    ),

    ("usb", 0,
        Subsignal("d_p",    Pins(1)),
        Subsignal("d_n",    Pins(1)),
        Subsignal("pullup", Pins(1)),
        Subsignal("tx_en", Pins(1)),
    ),

    ("spiflash", 0,
        Subsignal("cs_n", Pins(1)),
        Subsignal("clk",  Pins(1)),
        Subsignal("mosi", Pins(1)),
        Subsignal("miso", Pins(1)),
        Subsignal("wp",   Pins(1)),
        Subsignal("hold", Pins(1)),
    ),
    ("spiflash4x", 0,
        Subsignal("cs_n", Pins(1)),
        Subsignal("clk",  Pins(1)),
        Subsignal("dq",   Pins(4)),
    ),
]

sim_connectors = [("touch_pins", "user_touch_0, user_touch_1, user_touch_2, user_touch_3")]


# Clock and reset signals that the simulator needs for proper emulation.
class sim_CRG(Module):
    def __init__(self, platform):
        clk = platform.request("clk")
        rst = platform.request("reset")
        clk12 = Signal()

        self.clock_domains.cd_sys = ClockDomain()
        self.clock_domains.cd_usb_12 = ClockDomain()
        self.clock_domains.cd_usb_48 = ClockDomain()
        self.clock_domains.cd_usb_48_to_12 = ClockDomain()

        #clk12 = clk.clk12
        clk48 = clk.clk48
        #self.comb += clk.clk12.eq(clk12)
        #self.comb += clk.clk48.eq(clk48)
        self.comb += self.cd_usb_48.clk.eq(clk48)
        self.comb += self.cd_usb_48_to_12.clk.eq(clk48)

        # derive 12MHz clock by division of 48MHz clock
        clk12_counter = Signal(2)
        self.sync.usb_48_to_12 += clk12_counter.eq(clk12_counter + 1)
        self.comb += clk12.eq(clk12_counter[1])

        # Uncomment the following to enable 48MHz Risc-V for faster simulation
        # Warning: it breaks USB communication as all data will be sent 4x
        #self.comb += self.cd_sys.clk.eq(clk48)
        # Use the following for FPGA timing
        self.comb += self.cd_sys.clk.eq(clk12)
        self.comb += self.cd_usb_12.clk.eq(clk12)

        self.comb += [
            ResetSignal("sys").eq(rst),
            ResetSignal("usb_12").eq(rst),
            ResetSignal("usb_48").eq(rst),
            # Must not reset cd_usb_48_to_12 otherwise clock divider halts
            # and sys_clk domain fails to reset
        ]

class sim_Platform(SimPlatform):
    def __init__(self, revision=None, toolchain="verilator"):
        default_clk_name = "clk12"
        SimPlatform.__init__(self,
                             "sim",
                             sim_io,
                             sim_connectors,
                             toolchain="verilator")
        self.revision = revision
        self.spi_size = 2 * 1024 * 1024
        self.spi_dummy = 6

    def create_programmer(self):
        raise ValueError("programming is not supported")


def add_fsm_state_names():
    """Hack the FSM module to add state names to the output"""
    from migen.fhdl.visit import NodeTransformer
    from migen.genlib.fsm import NextState, NextValue, _target_eq
    from migen.fhdl.bitcontainer import value_bits_sign

    class My_LowerNext(NodeTransformer):
        def __init__(self, next_state_signal, next_state_name_signal, encoding,
                     aliases):
            self.next_state_signal = next_state_signal
            self.next_state_name_signal = next_state_name_signal
            self.encoding = encoding
            self.aliases = aliases
            # (target, next_value_ce, next_value)
            self.registers = []

        def _get_register_control(self, target):
            for x in self.registers:
                if _target_eq(target, x[0]):
                    return x[1], x[2]
            raise KeyError

        def visit_unknown(self, node):
            if isinstance(node, NextState):
                try:
                    actual_state = self.aliases[node.state]
                except KeyError:
                    actual_state = node.state
                return [
                    self.next_state_signal.eq(self.encoding[actual_state]),
                    self.next_state_name_signal.eq(
                        int.from_bytes(actual_state.encode(), byteorder="big"))
                ]
            elif isinstance(node, NextValue):
                try:
                    next_value_ce, next_value = self._get_register_control(
                        node.target)
                except KeyError:
                    related = node.target if isinstance(node.target,
                                                        Signal) else None
                    next_value = Signal(bits_sign=value_bits_sign(node.target),
                                        related=related)
                    next_value_ce = Signal(related=related)
                    self.registers.append(
                        (node.target, next_value_ce, next_value))
                return next_value.eq(node.value), next_value_ce.eq(1)
            else:
                return node

    import migen.genlib.fsm as fsm

    def my_lower_controls(self):
        self.state_name = Signal(len(max(self.encoding, key=len)) * 8,
                                 reset=int.from_bytes(
                                     self.reset_state.encode(),
                                     byteorder="big"))
        self.next_state_name = Signal(len(max(self.encoding, key=len)) * 8,
                                      reset=int.from_bytes(
                                          self.reset_state.encode(),
                                          byteorder="big"))
        self.comb += self.next_state_name.eq(self.state_name)
        self.sync += self.state_name.eq(self.next_state_name)
        return My_LowerNext(self.next_state, self.next_state_name,
                            self.encoding, self.state_aliases)

    fsm.FSM._lower_controls = my_lower_controls


class Platform(LatticePlatform):
    def __init__(self, revision=None, toolchain="icestorm"):
        self.revision = revision
        if revision == "evt":
            from litex_boards.platforms.fomu_evt import _io, _connectors
            LatticePlatform.__init__(self, "ice40-up5k-sg48", _io, _connectors, toolchain="icestorm")
            self.spi_size = 16 * 1024 * 1024
            self.spi_dummy = 6
        elif revision == "dvt":
            from litex_boards.platforms.fomu_pvt import _io, _connectors
            LatticePlatform.__init__(self, "ice40-up5k-uwg30", _io, _connectors, toolchain="icestorm")
            self.spi_size = 2 * 1024 * 1024
            self.spi_dummy = 6
        elif revision == "pvt":
            from litex_boards.platforms.fomu_pvt import _io, _connectors
            LatticePlatform.__init__(self, "ice40-up5k-uwg30", _io, _connectors, toolchain="icestorm")
            self.spi_size = 2 * 1024 * 1024
            self.spi_dummy = 6
        elif revision == "hacker":
            from litex_boards.platforms.fomu_hacker import _io, _connectors
            LatticePlatform.__init__(self, "ice40-up5k-uwg30", _io, _connectors, toolchain="icestorm")
            self.spi_size = 2 * 1024 * 1024
            self.spi_dummy = 4
        else:
            raise ValueError("Unrecognized revision: {}.  Known values: evt, dvt, pvt, hacker".format(revision))

    def create_programmer(self):
        raise ValueError("programming is not supported in this environment")


class BaseSoC(SoCCore, AutoDoc):
    """A2Fomu SoC and Bootloader

    Fomu is an FPGA that fits entirely within a USB port.
    A2Fomu is an Apple II clone inside of Fomu along with an operating
    system for its control processor. The SoC contains a small ROM that
    loads the OS from Flash memory into RAM.
    """

    SoCCore.csr_map = {
       #"ctrl":           0,  # LiteX - many better uses for the space
        "apple2":         0,
        "crg":            1,  # user - no registers in the default clock module
       #"uart_phy":       2,  # Fomu PVT has no pins for uart
       #"uart":           3,  # Fomu PVT has no pins for uart
       #"identifier_mem": 4,  # unnecessary
        "timer0":         5,  # provided by default (optional)
       #"cpu_or_bridge":  8,  # Nothing here
        "usb":            9,
       #"picorvspi":      10,
        "touch":          11,
        "reboot":         12,
        "rgb":            13,
       #"version":        14,
        "lxspi":          15,
       #"messible":       16,
    }

    SoCCore.mem_map = {
        "rom":              0x00000000,  # (default shadow @0x80000000)
        "sram":             0x10000000,  # (default shadow @0x90000000)
        "spiflash":         0x20000000,  # (default shadow @0xa0000000)
        "a2ram":            0xC0000000,  # (default shadow @0xc0000000)
        "csr":              0xe0000000,  # (default shadow @0xe0000000)
        "vexriscv_debug":   0xf00f0000,
    }

    interrupt_map = {
        "timer0": 2,
        "usb": 3,
    }
    interrupt_map.update(SoCCore.interrupt_map)

    def __init__(self, platform, boot_source="rand",
                 gdb_debug=None, usb_wishbone=False, bios_file=None,
                 use_dsp=False, placer="heap", output_dir="build",
                 pnr_seed=0,
                 warmboot_offsets=None,
                 **kwargs):
        # Disable integrated RAM unless using simulator - we'll add it later
        self.integrated_sram_size = 0

        self.output_dir = output_dir

        if kwargs["sim"]:
            clk_freq = int(48e6)
            self.submodules.crg = sim_CRG(platform)
            self.integrated_sram_size = 0 # 0x8000,
        else:
            clk_freq = int(12e6)
            self.submodules.crg = _CRG(platform)

        SoCCore.__init__(self, platform, clk_freq,
                integrated_sram_size=self.integrated_sram_size, with_uart=False,
                with_ctrl=False, csr_data_width=32, **kwargs)
        
        if gdb_debug is not None:
            if gdb_debug == "uart":
                from litex.soc.cores.uart import UARTWishboneBridge
                self.submodules.uart_bridge = UARTWishboneBridge(platform.request("serial"), clk_freq, baudrate=115200)
                self.add_wb_master(self.uart_bridge.wishbone)
            elif gdb_debug == "usb":
                usb_wishbone = True
            elif gdb_debug == "spi":
                import spibone
                # Add SPI Wishbone bridge
                debug_device = [
                    ("spidebug", 0,
                        Subsignal("mosi", Pins("dbg:0")),
                        Subsignal("miso", Pins("dbg:1")),
                        Subsignal("clk",  Pins("dbg:2")),
                        Subsignal("cs_n", Pins("dbg:3")),
                    )
                ]
                platform.add_extension(debug_device)
                spi_pads = platform.request("spidebug")
                self.submodules.spibone = ClockDomainsRenamer("usb_12")(spibone.SpiWishboneBridge(spi_pads, wires=4))
                self.add_wb_master(self.spibone.wishbone)
            if hasattr(self, "cpu") and not isinstance(self.cpu, CPUNone):
                self.cpu.use_external_variant("rtl/VexRiscv_Fomu_Debug.v")
                os.path.join(output_dir, "gateware")
                self.register_mem("vexriscv_debug", 0xf00f0000, self.cpu.debug_bus, 0x100)
        else:
            if hasattr(self, "cpu") and not isinstance(self.cpu, CPUNone):
                #self.cpu.use_external_variant("rtl/VexRiscv_Fomu.v")
                #self.cpu.use_external_variant("rtl/VexRiscv_Minimum.v")
                #self.cpu.use_external_variant("/home/doug/git/VexRiscv-verilog/mmuf_i0_ncot.v")
                self.cpu.use_external_variant("rtl/VexRiscv_Fomu_NoMMU.v")

        # SPRAM- UP5K has single port RAM, might as well use it as SRAM to
        # free up scarce block RAM.
        spram_size = 64*1024
        if not kwargs["sim"]:
            self.submodules.spram = up5kspram.Up5kSPRAM(size=spram_size)
        else:
            self.submodules.spram = wishbone.SRAM(spram_size, read_only=False, init=[])
        self.register_mem("sram", self.mem_map["sram"], self.spram.bus, spram_size)

        # Add a Messible for device->host communications
        #Doug
        #self.submodules.messible = Messible()

        # Apple II specific modules here
        a2mem = PDP_SPRAM(sim=kwargs["sim"])
        self.submodules.a2mem = a2mem
        a2mem_size = 64*1024
        self.register_mem("a2ram", self.mem_map["a2ram"], self.a2mem.bus, a2mem_size)
        self.submodules.apple2 = Apple2(platform, a2mem)

        if not kwargs["no_cpu"]:
            bios_size = 0x2000   # Fomu standard 8 Kb ROM
            if kwargs['sim']:
                # 64kB ROM used in place of flash during simulation
                bios_size = 0x10000
            else:
                # ROM consumes a large quantity of the limited EBR block RAM
                # 1 KB ROM that just initializes flash and jumps to it
                bios_size = 0x3000   # 12kB max size using all ebr
                bios_size = 0x2400   # 9kB
                bios_size = 0x2000   # 8kB foboot failsafe size
                bios_size = 0x1000   # 4kB bootloader
                bios_size = 0x0800   # 2kB bootloader
            if boot_source == "bios" or bios_file is not None:
                kwargs['cpu_reset_address'] = 0
                if bios_file is None:
                    self.integrated_rom_size = bios_size
                    self.submodules.rom = wishbone.SRAM(bios_size, read_only=True, init=[])
                    self.register_rom(self.rom.bus, bios_size)
                else:
                    self.firmware_rom_size = bios_size
                    self.submodules.firmware_rom = FirmwareROM(bios_size, bios_file)
                    self.add_constant("ROM_DISABLE", 1)
                    self.register_rom(self.firmware_rom.bus, bios_size)
            elif boot_source == "rand":
                kwargs['cpu_reset_address'] = 0
                self.submodules.random_rom = RandomFirmwareROM(bios_size)
                self.add_constant("ROM_DISABLE", 1)
                self.register_rom(self.random_rom.bus, bios_size)
            elif boot_source == "spi":
                kwargs['cpu_reset_address'] = 0
                self.integrated_rom_size = bios_size
                gateware_size = 0x1a000
                self.flash_boot_address = self.mem_map["spiflash"] + gateware_size
                self.submodules.rom = wishbone.SRAM(bios_size, read_only=True, init=[])
                self.register_rom(self.rom.bus, bios_size)
            else:
                raise ValueError("unrecognized boot_source: {}".format(boot_source))

        # The litex SPI module supports memory-mapped reads, as well as a bit-banged mode
        # for doing writes.
        #if not kwargs['no_spi']:
        spi_pads = platform.request("spiflash4x")
        self.submodules.lxspi = spi_flash.SpiFlashDualQuad(spi_pads, dummy=platform.spi_dummy, endianness="little")
        self.register_mem("spiflash", self.mem_map["spiflash"], self.lxspi.bus, size=platform.spi_size)

        # Add USB pads, as well as the appropriate USB controller.  If no CPU is
        # present, use the DummyUsb controller.
        usb_pads = platform.request("usb")
        usb_iobuf = usbio.IoBuf(usb_pads.d_p, usb_pads.d_n, usb_pads.pullup)
        if hasattr(self, "cpu") and not isinstance(self.cpu, CPUNone):
            self.submodules.usb = eptri.TriEndpointInterface(usb_iobuf, debug=usb_wishbone)
        else:
            self.submodules.usb = dummyusb.DummyUsb(usb_iobuf, debug=usb_wishbone)
        if kwargs['sim']:
            self.comb += usb_pads.tx_en.eq(usb_iobuf.usb_tx_en)

        if usb_wishbone:
            self.add_wb_master(self.usb.debug_bridge.wishbone)

        # For the EVT board, ensure the pulldown pin is tristated as an input
        if hasattr(usb_pads, "pulldown"):
            pulldown = TSTriple()
            self.specials += pulldown.get_tristate(usb_pads.pulldown)
            self.comb += pulldown.oe.eq(0)

        if not kwargs['no_touch']:
            # Add GPIO pads for the touch buttons
            platform.add_extension(TouchPads.touch_device)
            self.submodules.touch = TouchPads(platform.request("touch_pads"))

        # Allow the user to reboot the ICE40.  Additionally, connect the CPU
        # RESET line to a register that can be modified, to allow for
        # us to debug programs even during reset.
        #if not kwargs['sim']:
        self.submodules.reboot = SBWarmBoot(self, warmboot_offsets)
        #Doug - removed for PicoRV32
        if hasattr(self, "cpu") and not isinstance(self.cpu, CPUNone):
            self.cpu.cpu_params.update(
                i_externalResetVector=self.reboot.addr.storage,
            )

        if not kwargs['no_rgb']:
            from rtl.sbled import SBLED
            rgb_pads = platform.request("rgb_led")
            rgb = SBLED(platform.revision, rgb_pads)
            self.submodules.rgb = rgb
            if kwargs['sim']:
                # The ice40 LED hardware is not emulated so route to testbench
                self.comb += [
                        rgb_pads.r.eq(rgb.raw.storage[0]),
                        rgb_pads.g.eq(rgb.raw.storage[1]),
                        rgb_pads.b.eq(rgb.raw.storage[2]),
                ]

        # Doug: We need space and this is not useful as it only specifies the
        # model that the gateware was designed for and not the model that is
        # actually in use. If the wrong gateware is loaded, the LED will not
        # show the expected colors. This is the major difference.
        #self.submodules.version = Version(platform.revision, self, pnr_seed, models=[
        #        ("0x45", "E", "Fomu EVT"),
        #        ("0x44", "D", "Fomu DVT"),
        #        ("0x50", "P", "Fomu PVT (production)"),
        #        ("0x48", "H", "Fomu Hacker"),
        #        ("0x3f", "?", "Unknown model"),
        #    ])

        if not kwargs['sim']:
            # Override default LiteX's yosys/build templates
            assert hasattr(platform.toolchain, "yosys_template")
            assert hasattr(platform.toolchain, "build_template")
            platform.toolchain.yosys_template = [
                "{read_files}",
                "attrmap -tocase keep -imap keep=\"true\" keep=1 -imap keep=\"false\" keep=0 -remove keep=0",
                "synth_ice40 -json {build_name}.json -top {build_name}",
            ]
            platform.toolchain.build_template = [
                #"set -x",
                "set -x; yosys -q -w 'has an unprocessed .init. attribute.' -l {build_name}.rpt {build_name}.ys",
                "nextpnr-ice40 --json {build_name}.json --pcf {build_name}.pcf --asc {build_name}.txt"
                " --pre-pack {build_name}_pre_pack.py --{architecture} --package {package}",
                "icepack {build_name}.txt {build_name}.bin"
            ]

            # Add "-relut -dffe_min_ce_use 4" to the synth_ice40 command.
            # The "-reult" adds an additional LUT pass to pack more stuff in,
            # and the "-dffe_min_ce_use 4" flag prevents Yosys from generating a
            # Clock Enable signal for a LUT that has fewer than 4 flip-flops.
            # This increases density, and lets us use the FPGA more efficiently.
            platform.toolchain.yosys_template[2] += " -relut -abc2 -dffe_min_ce_use 4 -relut"

            # Disable final deep-sleep power down so firmware words are loaded
            # onto softcore's address bus.
            platform.toolchain.build_template[2] = "icepack -s {build_name}.txt {build_name}.bin"

            # Allow us to set the nextpnr seed
            platform.toolchain.build_template[1] += " --seed " + str(pnr_seed)

            if placer is not None:
                platform.toolchain.build_template[1] += " --placer {}".format(placer)

        if use_dsp:
            platform.toolchain.yosys_template[2] += " -dsp"

    def copy_memory_file(self, src):
        import os
        from shutil import copyfile
        if not os.path.exists(self.output_dir):
            os.mkdir(self.output_dir)
        if not os.path.exists(os.path.join(self.output_dir, "gateware")):
            os.mkdir(os.path.join(self.output_dir, "gateware"))
        copyfile(os.path.join("rtl", src), os.path.join(self.output_dir, "gateware", src))

def make_multiboot_header(filename, boot_offsets=[160]):
    """
    ICE40 allows you to program the SB_WARMBOOT state machine by adding the following
    values to the bitstream, before any given image:

    [7e aa 99 7e]       Sync Header
    [92 00 k0]          Boot mode (k = 1 for cold boot, 0 for warmboot)
    [44 03 o1 o2 o3]    Boot address
    [82 00 00]          Bank offset
    [01 08]             Reboot
    [...]               Padding (up to 32 bytes)

    Note that in ICE40, the second nybble indicates the number of remaining bytes
    (with the exception of the sync header).

    The above construct is repeated five times:

    INITIAL_BOOT        The image loaded at first boot
    BOOT_S00            The first image for SB_WARMBOOT
    BOOT_S01            The second image for SB_WARMBOOT
    BOOT_S10            The third image for SB_WARMBOOT
    BOOT_S11            The fourth image for SB_WARMBOOT
    """
    while len(boot_offsets) < 5:
        boot_offsets.append(boot_offsets[0])

    with open(filename, 'wb') as output:
        for offset in boot_offsets:
            # Sync Header
            output.write(bytes([0x7e, 0xaa, 0x99, 0x7e]))

            # Boot mode
            output.write(bytes([0x92, 0x00, 0x00]))

            # Boot address
            output.write(bytes([0x44, 0x03,
                    (offset >> 16) & 0xff,
                    (offset >> 8)  & 0xff,
                    (offset >> 0)  & 0xff]))

            # Bank offset
            output.write(bytes([0x82, 0x00, 0x00]))

            # Reboot command
            output.write(bytes([0x01, 0x08]))

            for x in range(17, 32):
                output.write(bytes([0]))

def main():
    parser = argparse.ArgumentParser(
        description="Build A2Fomu Main Gateware")
    parser.add_argument(
        "--boot-source", choices=["spi", "rand", "bios"], default="rand",
        help="where to have the CPU obtain its executable code from"
    )
    parser.add_argument(
        "--document-only", default=False, action="store_true",
        help="Don't build gateware or software, only build documentation"
    )
    parser.add_argument(
        # The only difference between revisions is LED color so default to
        # production version
        "--revision", choices=["evt", "dvt", "pvt", "hacker"], default="pvt",
        help="build a2fomu for a particular hardware model"
    )
    parser.add_argument(
        "--sim", help="build gateware optimized for simulation", action="store_true"
    )
    parser.add_argument(
        "--bios", help="use specified file as a BIOS, rather than building one"
    )
    parser.add_argument(
        "--gdb-debug", help="enable gdb debug support", choices=["usb", "uart", "spi", None], default=None
    )
    parser.add_argument(
        "--wishbone-debug", help="enable debug support", action="store_true"
    )
    parser.add_argument(
        "--with-dsp", help="use dsp inference in yosys (not all yosys builds have -dsp)", action="store_true"
    )
    parser.add_argument(
        "--no-cpu", help="disable cpu generation for debugging purposes", action="store_true"
    )
    parser.add_argument(
        "--no-spi", help="disable SPI flash", action="store_true"
    )
    parser.add_argument(
        "--no-rgb", help="disable RGB LED", action="store_true"
    )
    parser.add_argument(
        "--no-touch", help="disable touch pads", action="store_true"
    )
    parser.add_argument(
        "--placer", choices=["sa", "heap"], default="heap", help="which placer to use in nextpnr"
    )
    parser.add_argument(
        "--seed", default=0, help="seed to use in nextpnr"
    )
    parser.add_argument(
        "--export-random-rom-file", help="Generate a random ROM file and save it to a file"
    )
    args = parser.parse_args()

    output_dir = 'build'

    if args.export_random_rom_file is not None:
        size = 0x2000
        def xorshift32(x):
            x = x ^ (x << 13) & 0xffffffff
            x = x ^ (x >> 17) & 0xffffffff
            x = x ^ (x << 5)  & 0xffffffff
            return x & 0xffffffff

        def get_rand(x):
            out = 0
            for i in range(32):
                x = xorshift32(x)
                if (x & 1) == 1:
                    out = out | (1 << i)
            return out & 0xffffffff
        seed = 1
        with open(args.export_random_rom_file, "w", newline="\n") as output:
            for d in range(int(size / 4)):
                seed = get_rand(seed)
                print("{:08x}".format(seed), file=output)
        return 0

    compile_software = False
    if (args.boot_source == "bios" or args.boot_source == "spi") and args.bios is None:
        compile_software = True

    cpu_type = "vexriscv"
    cpu_variant = "minimal"
    if args.gdb_debug:
        cpu_variant = cpu_variant + "+debug"

    if args.no_cpu:
        cpu_type = None
        cpu_variant = None

    compile_gateware = True
    if args.document_only:
        compile_gateware = False
        compile_software = False

    warmboot_offsets = [
        160,
        160,
        157696,
        262144,
        262144 + 32768,
    ]

    os.environ["LITEX"] = "1" # Give our Makefile something to look for

    if args.sim:
        platform = sim_Platform(revision=args.revision)
    else:
        platform = Platform(revision=args.revision)
    soc = BaseSoC(platform, cpu_type=cpu_type, cpu_variant=cpu_variant,
                            gdb_debug=args.gdb_debug, usb_wishbone=args.wishbone_debug,
                            boot_source=args.boot_source,
                            bios_file=args.bios, sim=args.sim,
                            use_dsp=args.with_dsp, placer=args.placer,
                            pnr_seed=int(args.seed),
                            no_spi=args.no_spi,
                            no_rgb=args.no_rgb,
                            no_cpu=args.no_cpu,
                            no_touch=args.no_touch,
                            output_dir=output_dir,
                            warmboot_offsets=warmboot_offsets[1:])
    builder = Builder(soc, output_dir=output_dir, csr_csv="build/csr.csv", csr_svd="build/soc.svd",
                      compile_software=compile_software, compile_gateware=compile_gateware)
    if compile_software:
        builder.software_packages = [
            ("bios", os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "sw")))
        ]

    if args.sim:
        vns = builder.build(run=False)
        soc.do_exit(vns)
        print("""Simulation build complete.  Output files:
    build/gateware/dut.v               Run this Verilog file under Cocotb.
""")
        return

    vns = builder.build()
    soc.do_exit(vns)
    lxsocdoc.generate_docs(soc, "build/documentation/", project_name="A2Fomu", author="Elecbrick")

    if not args.document_only:
        make_multiboot_header(os.path.join(output_dir, "gateware", "multiboot-header.bin"),
                            warmboot_offsets)

        with open(os.path.join(output_dir, 'gateware', 'multiboot-header.bin'), 'rb') as multiboot_header_file:
            multiboot_header = multiboot_header_file.read()
            with open(os.path.join(output_dir, 'gateware', 'top.bin'), 'rb') as top_file:
                top = top_file.read()
                with open(os.path.join(output_dir, 'gateware', 'top-multiboot.bin'), 'wb') as top_multiboot_file:
                    top_multiboot_file.write(multiboot_header)
                    top_multiboot_file.write(top)

        print(
    """A2Fomu build complete.  Output files:
    build/gateware/top.bin             Bitstream file. Use dfu-util to load.
    build/gateware/top-multiboot.bin   Multiboot-enabled bitstream file. May be
    used with Booster to replace failsafe gateware. Caution: not reccommended
    as it will likely brick device.
    build/gateware/top.v               Verilog representation of SoC.
    build/software/include/generated/  Header files used by software.
""")

if __name__ == "__main__":
    main()
