#ifndef CSR_DEFS__H
#define CSR_DEFS__H

#define CSR_MSTATUS_MIE 0x8

#define CSR_IRQ_MASK 0xBC0
#define CSR_IRQ_PENDING 0xFC0

#define CSR_DCACHE_INFO 0xCC0

enum riscv_csr {
  // Machine Information Registers
  mvendorid = 0xF11,    // Vendor ID
  marchid   = 0xF12,    // Architecture ID
  mimpid    = 0xF13,    // Implementation ID
  mhartid   = 0xF14,    // Hardware thread ID
  // Machine Trap Setup
  mstatus   = 0x300,    // Machine status register
  misa      = 0x301,    // ISA and extensions
  medeleg   = 0x302,    // Machine exception delegation register
  mideleg   = 0x303,    // Machine interrupt delegation register
  mie       = 0x304,    // Machine interrupt-enable register
  mtvec     = 0x305,    // Machine trap-handler base address
  mcounteren= 0x306,    // Machine counter enable
  mstatush  = 0x310,    // Additional machine status register, RV32 only
  // Machine Trap Handling
  mscratch  = 0x340,    // Scratch register for machine trap handlers
  mepc      = 0x341,    // Machine exception program counter
  mcause    = 0x342,    // Machine trap cause
  mtval     = 0x343,    // Machine bad address or instruction
  mip       = 0x344,    // Machine interrupt pending
  mtinst    = 0x34A,    // Machine trap instruction (transformed)
  mtval2    = 0x34B,    // Machine bad guest physical address
  // Machine Counter/Timers
  mcycle    = 0xB00,    // MRW mcycle Machine cycle counter.
  mcycleh   = 0xB80,    // MRW mcycleh Upper 32 bits of mcycle, RV32I only.
  minstret  = 0xB02,    // MRW minstret Machine instructions-retired counter.
  minstreth = 0xB82,    // MRW minstreth Upper 32 bits of minstret, RV32I only.
  //Machine Protection and Translation
  // not implemented
};


//Machine Counter/Timers
#define RISCV_CSR_MCYCLE        0xB00 // MRW mcycle Machine cycle counter.
#define RISCV_CSR_MINSTRET      0xB02 // MRW minstret Machine instructions-retired counter.
#define RISCV_CSR_MCYCLEH       0xB80 // MRW mcycleh Upper 32 bits of mcycle, RV32I only.
#define RISCV_CSR_MINSTRETH     0xB82 // MRW minstreth Upper 32 bits of minstret, RV32I only.


#endif	/* CSR_DEFS__H */
