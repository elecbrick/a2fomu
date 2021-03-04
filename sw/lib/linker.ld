INCLUDE output_format.ld

ENTRY(_start)

prog_off = DEFINED(prog_off) ? prog_off : 0;
LOAD_RAM_OFFSET = DEFINED(LOAD_RAM_OFFSET) ? LOAD_RAM_OFFSET : 0;


SECTIONS
{
	.text :
	{
                . += prog_off;  /* Use specific location in ROM/Flash */
		_ftext = .;
		*(.text.start)
		*(.text .stub .text.* .gnu.linkonce.t.*)
		_etext = .;
	} > romem

	.rodata :
	{
		. = ALIGN(4);
		_frodata = .;
		*(.rodata .rodata.* .gnu.linkonce.r.*)
		*(.rodata1)
		*(.srodata)
                /* Section size must be an integral number of words otherwise */
                /* crt0 will trap with misaligned access when copying data */
		. = ALIGN(4);
		_erodata = .;
	} > romem

	.data : AT (ADDR(.rodata) + SIZEOF (.rodata))
	{
		. += LOAD_RAM_OFFSET;  /* Define specific offset in RAM */
		. = ALIGN(4);
		_fdata = .;
		*(.data .data.* .gnu.linkonce.d.*)
		*(.data1)
		_gp = ALIGN(16);
		*(.sdata .sdata.* .gnu.linkonce.s.* .sdata2 .sdata2.*)
		_edata = ALIGN(16); /* Make sure _edata is >= _gp. */
	} > ram

	.bss :
	{
		. = ALIGN(4);
		_fbss = .;
		*(.dynsbss)
		*(.sbss .sbss.* .gnu.linkonce.sb.*)
		*(.scommon)
		*(.dynbss)
		*(.bss .bss.* .gnu.linkonce.b.*)
		*(COMMON)
		. = ALIGN(4);
		_ebss = .;
		_end = .;
	} > ram
}

PROVIDE(_fstack = ORIGIN(sram) + LENGTH(sram) - 4);
