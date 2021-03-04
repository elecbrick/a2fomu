INCLUDE regions.ld

/* Override program and data destination to RAM */
REGION_ALIAS("romem", spiflash)
REGION_ALIAS("ram", sram)


/* When the executable is in RAM, the RAM offset indicates the location of
 * the text section, not the data section. Change start address accordingly. */
/* prog_off = DEFINED(LOAD_RAM_ADDR) ? LOAD_RAM_OFFSET : DEFINED(PROGRAM_OFFSET) ? PROGRAM_OFFSET : 0; */

prog_off = SEGMENT_START("text-segment", ORIGIN(spiflash)) - ORIGIN(spiflash);
data_off = 0;
