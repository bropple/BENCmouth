/*
 * BENCmouth - minimal Cortex-M startup for the QEMU harness
 *
 * No newlib, no rdimon, no specs files. The core is freestanding and the
 * harness stays freestanding with it: a vector table, a reset handler that
 * initialises .data and .bss, and a call to main. Everything the bench needs
 * from the outside world it gets through semihosting, in semihost.h.
 */

extern unsigned _sidata, _sdata, _edata, _sbss, _ebss, _estack;

int  main(void);
void Reset_Handler(void);

static void Default_Handler(void)
{
    /* A fault here means the guest went wrong, and hanging is the honest
     * outcome - QEMU's -no-reboot turns it into a visible timeout rather than
     * a silent reset loop. */
    for (;;) { }
}

void Reset_Handler(void)
{
    unsigned *src = &_sidata;
    unsigned *dst = &_sdata;

#if defined(__ARM_FP) && (__ARM_FP != 0)
    /* The FPU is disabled out of reset on every Cortex-M that has one, so a
     * hard-float build faults on its first VFP instruction and never reaches
     * main. That failure is silent and total - the fault handler loops, the
     * program appears to run forever, and nothing says why - so it is worth
     * more than the three lines it costs.
     *
     * CPACR bits 23:20 grant full access to CP10 and CP11. The barriers are
     * required: the enable must retire before any coprocessor instruction is
     * fetched against it. */
    *(volatile unsigned *)0xE000ED88u |= (0xFu << 20);
    __asm__ volatile ("dsb" ::: "memory");
    __asm__ volatile ("isb" ::: "memory");
#endif

    while (dst < &_edata) *dst++ = *src++;
    for (dst = &_sbss; dst < &_ebss; ) *dst++ = 0u;

    main();
    for (;;) { }
}

__attribute__((section(".vectors"), used))
void (* const g_vectors[])(void) = {
    (void (*)(void))&_estack,   /* initial MSP  */
    Reset_Handler,              /* reset        */
    Default_Handler,            /* NMI          */
    Default_Handler,            /* HardFault    */
    Default_Handler,            /* MemManage    */
    Default_Handler,            /* BusFault     */
    Default_Handler,            /* UsageFault   */
    0, 0, 0, 0,
    Default_Handler,            /* SVCall       */
    Default_Handler,            /* DebugMon     */
    0,
    Default_Handler,            /* PendSV       */
    Default_Handler             /* SysTick      */
};
