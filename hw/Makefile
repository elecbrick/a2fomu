TOPLEVEL = tb

# Simulate with a CPU that includes explicit bypass in the register file.
CPU = rtl/VexRiscv_bypass.v
# The standard Fomu CPUs and the simplified A2Fomu one all suffer from a
# simultation issue that fails to handle register interlock the way HW does.
#CPU = rtl/VexRiscv_Fomu_NoMMU.v
#CPU = rtl/VexRiscv_Fomu.v
#CPU = rtl/VexRiscv_Fomu_Debug.v

# LiteX compiles the entire Python part of the SoC into a single Verilog file.
# Any Verilog apart from the above CPU should be mentioned here as extra logic.
EXTRA_LOGIC = rtl/verilog-6502/cpu.v rtl/verilog-6502/ALU.v
#EXTRA_LOGIC += rtl/spiflash/AT25SF161.v

# Verilog generation is handled by the project script.
PROJECT_SCRIPT = a2fomu_build.py
BUILD  = build/gateware

# Default target: generate and flash project
flash:	$(BUILD)/a2fomu.bin
	-dfu-util -D $(BUILD)/a2fomu.bin
	while [ ! -e /dev/ttyACM0 ]; do echo -n "."; sleep 1; done;
	exec screen /dev/ttyACM0

# Generate gateware with specific BIOS
flash-%: TARGET = $(subst flash-,,$@)
flash-%:
	make ../sw/$(TARGET)/a2$(TARGET).init
	icebram rand8k.rom ../sw/$(TARGET)/a2$(TARGET).init < $(BUILD)/top.txt > $(BUILD)/$(TARGET).txt
	icepack -s $(BUILD)/$(TARGET).txt $(BUILD)/$(TARGET).bin
	dfu-util -D $(BUILD)/$(TARGET).bin
.PHONY: flash flash-%

# Requred simulation tools that are not installed automatically.
GITHUB_ROOT = $(HOME)/git
PYTHON_ENV = $(GITHUB_ROOT)/usb-test-suite-build/env/bin
PYTHONPATH = $(GITHUB_ROOT)/usb-test-suite-build/usb-test-suite-testbenches
TOOLS_DIR = $(GITHUB_ROOT)/fomu-toolchain-linux_x86_64-v1.5.5/bin
SIM_LIBRARY = $(TOOLS_DIR)/../share/yosys/ice40/cells_sim.v

# Variables requred by cocotb.
TOPLEVEL_LANG = verilog
TESTBENCH = $(TOPLEVEL).v
VERILOG_SOURCES = $(TESTBENCH) dut.v $(CPU) $(EXTRA_LOGIC) $(SIM_LIBRARY)

TEST_SCRIPT= bringup
TARGET = a2fomu
TARGET_DEPS = rtl/apple2.py

export TARGET_CONFIG = configs/$(TARGET)_descriptors.json
export TARGET = a2fomu

# The default simulator iverilog is used unless a command like:
# "make VERILATOR=1 sim" is given. This uses the Verilator simulator instead.
ifdef VERILATOR
VERILATOR_ARGS = --trace -Wno-lint -Wno-style
endif

#$(PYTHONPATH)/tests/test-enum.py
MODULE = tests.$(TEST_SCRIPT)

BUILD_PROJECT = python3 $(PROJECT_SCRIPT) --revision pvt --wishbone-debug
#--bios=/home/doug/git/usb-test-suite-build/foboot/hw/build/software/bios/bios.elf

# Convert binaries to hex data for use by the simulator.
%.init: %.bin
	od -v -tx4 -w4 $< | awk '{if(NF>1){print $$2}}' > $@

%.bram: %.bin
	od -v -tx4 -w4 --endian=big $< | awk '{if(NF>1){print $$2}}' > $@

# The Apple II ROM is at address 0xC000 so the simulator init file needs to be
# padded. The raw binary is used on hardware.
%.init: %.rom
	od -v -tx4 -w4 $< | awk 'BEGIN{for(i=0; i<13*1024; i++) print '0'}; {if(NF>1){print $$2}}' > $@

ifeq ($(MAKECMDGOALS),sim)
    include $(shell cocotb-config --makefiles)/Makefile.inc
    include $(shell cocotb-config --makefiles)/Makefile.sim
    CPU = rtl/VexRiscv_bypass.v
    SIMULATION = 1
    export SIMULATION
ifndef VIRTUAL_ENV
    $(info source /home/doug/git/usb-test-suite-build/env/bin/activate)
    $(error VIRTUAL_ENV must be defined for simulation) 
    $(die)
    VIRTUAL_ENV = "$(PYTHON_ENV)/.."
    PATH := $(PYTHON_ENV):$(PATH)
    PYTHONHOME = 
endif
endif

#SW ?= $(BUILD)/mem_2.init 
SW ?= build/software/bios/bios.init

sim:	dut.v rom/apple_II.init $(SW)
	cp $(BUILD)/mem.init .
	#cp rom/apple_II.init mem_1.init
	# Save 200ms of simulation time by skipping BELL at start
	# Keep the screen clear and add a scroll to verify HW compression
	#cp apple_II.nobell.init mem_1.init
	# Use patched ROM that bypasses 100ms beep for reasonable boot time
	cp apple_II.patched.init mem_1.init
	# Testing disk drive - skip bell, screen clear, fast seek time
	cp apple_II.fast.disk.init mem_1.init
	cp $(SW) mem_2.init
	rm -f results.xml
	make results.xml

sw:	$(SW:.init:.bin)
	mkdir -p build/software/bios
	cd build/software/bios && $(MAKE) LITEX=1 -f ../../../../sw/Makefile

build/software/bios/bios.bin:
	mkdir -p build/software/bios
	cd build/software/bios && $(MAKE) LITEX=1 -f ../../../../sw/Makefile

$(BUILD)/mem_2.init: $(BUILD)/a2fomu.init
	cp $(BUILD)/a2fomu.init $(BUILD)/mem_2.init

$(BUILD)/a2fomu.bin: ../sw/src/.obj/runtime.bin $(BUILD)/top.bin
	python3 genruntime.py

$(BUILD)/top.bin: $(PROJECT_SCRIPT) $(wildcard rtl/*.py)
	./$(PROJECT_SCRIPT) --boot-source=bios --bios=../sw/src/.obj/bios.hex

#$(BUILD)/dut.v: $(PROJECT_SCRIPT) build/software/bios/bios.bin 

tb.v:	tb_wrapper.v 
	cp tb_wrapper.v tb.v

dut.v:	$(PROJECT_SCRIPT) tb_wrapper.v $(TARGET_DEPS)
	@# This dep causes double build => $(BUILD)/dut.v
	$(BUILD_PROJECT) --sim
	mv $(BUILD)/dut.v .

usb.vcd: dut.v
	sed -i "s/dump.vcd/usb.vcd/g" tb.v
	sed -i "s/0, tb/0, usb_d_p, usb_d_n/g" tb.v
	make sim

usb.pcap: usb.vcd
	sigrok-cli -i usb.vcd -P 'usb_signalling:signalling=full-speed:dm=usb_d_n:dp=usb_d_p,usb_packet,usb_request' -l 3 -B usb_request=pcap > usb.pcap

decode: tb.v usb.pcap

clean::
	rm -f mem.init mem_1.init mem_2.init dut.v usb.vcd usb.pcap tb.v
	rm -rf build/