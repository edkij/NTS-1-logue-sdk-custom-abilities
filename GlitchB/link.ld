ENTRY(_entry)
MEMORY {
  SRAM (rwx) : ORIGIN = 0x20019000, LENGTH = 12K
  SDRAM (rw) : ORIGIN = 0xC0420000, LENGTH = 2432K
}
SECTIONS {
  .hooks : ALIGN(16) {
    . = ALIGN(4);
    _hooks_start = .;
    KEEP(*(.hooks))
    . = ALIGN(4);
    _hooks_end = .;
  } > SRAM

  .text : ALIGN(4) {
    . = ALIGN(4);
    _text_start = .;
    *(.text .text.*)
    *(.glue_7) *(.glue_7t) *(.gcc*)
    . = ALIGN(4);
    _text_end = .;
  } > SRAM

  .rodata : ALIGN(4) {
    . = ALIGN(4);
    _rodata_start = .;
    *(.rodata .rodata.*)
    . = ALIGN(4);
    _rodata_end = .;
  } > SRAM

  .data : ALIGN(8) {
    . = ALIGN(8);
    _data_start = .;
    *(.data .data.*)
    . = ALIGN(8);
    _data_end = .;
  } > SRAM

  .bss (NOLOAD) : ALIGN(4) {
    . = ALIGN(4);
    _bss_start = .;
    *(.bss .bss.*) *(COMMON)
    . = ALIGN(4);
    _bss_end = .;
  } > SRAM

  .ARM.exidx : { *(.ARM.exidx* .gnu.linkonce.armexidx.*) } > SRAM
  .ARM.extab : { *(.ARM.extab* .gnu.linkonce.armextab.*) } > SRAM

  .sdram (NOLOAD) : ALIGN(4) {
    . = ALIGN(4);
    _usr_sdram_start = .;
    KEEP(*(.sdram*))
    . = ALIGN(4);
    _usr_sdram_end = .;
  } > SDRAM

  /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
