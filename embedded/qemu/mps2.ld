/*
 * BENCmouth - linker script for QEMU's MPS2 Cortex-M machines
 *
 * mps2-an385 (M3), mps2-an386 (M4) and mps2-an500 (M7) all present ZBT SRAM at
 * 0x00000000 for code and a second bank at 0x20000000 for data. Four megabytes
 * each is far more than the core needs - the point of this harness is to run
 * the code, not to prove it fits, which the size measurements already do.
 */

MEMORY
{
    FLASH (rx)  : ORIGIN = 0x00000000, LENGTH = 4M
    RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 4M
}

ENTRY(Reset_Handler)

SECTIONS
{
    .text :
    {
        KEEP(*(.vectors))
        *(.text*)
        *(.rodata*)
        . = ALIGN(4);
    } > FLASH

    _sidata = .;

    .data : AT(_sidata)
    {
        . = ALIGN(4);
        _sdata = .;
        *(.data*)
        . = ALIGN(4);
        _edata = .;
    } > RAM

    .bss (NOLOAD) :
    {
        . = ALIGN(4);
        _sbss = .;
        *(.bss*)
        *(COMMON)
        . = ALIGN(4);
        _ebss = .;
    } > RAM

    _estack = ORIGIN(RAM) + LENGTH(RAM);
}
