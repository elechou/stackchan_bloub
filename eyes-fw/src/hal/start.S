/*
 * Entry point.
 *
 * There is no second-stage bootloader and no OS underneath. The mask ROM reads
 * this image from flash offset 0, copies its segments into RAM and calls
 * _start. From here on the chip is ours: one core, one stack, no interrupts.
 *
 * The ROM's vector table stays installed on purpose. This code uses the
 * windowed ABI like any other Xtensa C program, so register-window overflow
 * and underflow exceptions do happen, and the ROM already handles them. It is
 * the same arrangement the stock second-stage bootloader runs under.
 */
    .section .entry.text, "ax"
    .literal_position
    .align  4
    .global _start
    .type   _start, @function
_start:
    /* No interrupt may fire: nothing would handle it. */
    movi    a2, 0
    wsr.intenable a2

    /* Start a fresh register window: this becomes the root of the call chain.
     * After windowbase changes, every a-register is a different physical one,
     * so nothing is carried across. */
    movi    a3, 1
    wsr.windowstart a3
    wsr.windowbase  a2
    rsync
    movi    a0, 0

    /* Our own stack, at the top of the data region. */
    movi    a1, _stack_top

    /* PS: user-vector mode, window overflow checks on, interrupt level 0. */
    movi    a3, 0x00040020
    wsr.ps  a3
    rsync

    /* Coprocessor 0 is the FPU. Disabled, the first float instruction traps. */
    movi    a3, 1
    wsr.cpenable a3
    rsync

    /* Erratum 572: zero-overhead loops need the loop buffer switched on. */
    movi    a3, 1
    wsr.memctl a3

    call4   hal_main

    /* hal_main never returns */
1:  j       1b
    .size   _start, . - _start
