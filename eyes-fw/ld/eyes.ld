/*
 * Everything lives in RAM.
 *
 * The image is flashed where the second-stage bootloader normally sits (flash
 * offset 0). The mask ROM copies each segment to its load address and jumps to
 * _start, so there is no flash mapping, no MMU setup and no cache to manage.
 *
 * The two regions below are one block of internal memory (SRAM1) seen from the
 * instruction bus and from the data bus, 0x6F0000 apart. Code takes the first
 * 64 KB, data the next 192 KB. The ROM keeps its own stack and variables at
 * the very top of SRAM1 (0x3FCE9710 and up) and nothing here comes near it.
 */
ENTRY(_start)

MEMORY
{
  iram (rx) : ORIGIN = 0x40378000, LENGTH = 0x10000   /* data-bus alias 0x3FC88000 */
  dram (rw) : ORIGIN = 0x3FC98000, LENGTH = 0x30000
}

SECTIONS
{
  .text : ALIGN(4)
  {
    KEEP(*(.entry.text))
    *(.literal .text .literal.* .text.*)
    . = ALIGN(4);
  } > iram

  .data : ALIGN(4)
  {
    *(.rodata .rodata.*)
    *(.data .data.*)
    . = ALIGN(4);
  } > dram

  .bss (NOLOAD) : ALIGN(4)
  {
    _bss_start = .;
    *(.bss .bss.*)
    *(COMMON)
    . = ALIGN(4);
    _bss_end = .;
  } > dram

  _stack_top = ORIGIN(dram) + LENGTH(dram);

  /DISCARD/ : { *(.comment) }
}

/* Routines in the mask ROM (ESP-IDF: components/esp_rom/esp32s3/ld/). */
PROVIDE(memset = 0x400011e8);
PROVIDE(memcpy = 0x400011f4);
PROVIDE(memmove = 0x40001200);
PROVIDE(memcmp = 0x4000120c);
PROVIDE(ets_update_cpu_frequency = 0x40001a4c);
PROVIDE(rom_i2c_writeReg = 0x40005d60);
PROVIDE(rom_i2c_writeReg_Mask = 0x40005d6c);
