/* =============================================================================
 * MSPM0G3507 链接脚本 (Cortex-M0+, 128KB Flash, 32KB SRAM)
 *
 * ⚠ 上板前必须核对的两个数字 —— 它们错了固件根本起不来：
 *
 *     FLASH 起始 0x00000000  长度 128K
 *     SRAM  起始 0x20200000  长度 32K      ← MSPM0 的 SRAM 不在 0x20000000
 *
 *   核对来源：MSPM0G3507 数据手册 (SLASEZ4) §Memory Organization，
 *   或 TI MSPM0 SDK 里 ti/devices/msp/m0p/linker_files/mspm0g350x.lds。
 *   `make PROFILE=driverlib` 时建议直接改用 SDK 自带的链接脚本。
 *
 * 与麦轮固件的链接脚本一样，这里**不写 /DISCARD/ 段**：
 * CubeMX / SDK 模板里常见的 `/DISCARD/ : { libc.a(*) }` 会把 newlib 整个丢掉，
 * 一旦有人用了 memcpy 就会得到一个含义完全无关的链接错误。
 * 真要减体积应该靠 --gc-sections 与 nano.specs，那是可预测的。
 * ============================================================================= */

ENTRY(Reset_Handler)

MEMORY
{
    FLASH (rx)  : ORIGIN = 0x00000000, LENGTH = 128K
    SRAM  (rwx) : ORIGIN = 0x20200000, LENGTH = 32K
}

/* 栈顶 = SRAM 末端，向下生长 */
_estack = ORIGIN(SRAM) + LENGTH(SRAM);

/* 最小空间要求。链接期就把"栈堆挤爆了"变成一个明确的错误，
   而不是等到运行时踩坏变量。 */
_Min_Heap_Size  = 0x200;   /*  512 B */
_Min_Stack_Size = 0x800;   /* 2048 B */

SECTIONS
{
    /* 向量表必须在 Flash 最前端 */
    .isr_vector :
    {
        . = ALIGN(4);
        KEEP(*(.isr_vector))
        . = ALIGN(4);
    } > FLASH

    .text :
    {
        . = ALIGN(4);
        *(.text)
        *(.text*)
        *(.glue_7)
        *(.glue_7t)
        *(.eh_frame)
        KEEP(*(.init))
        KEEP(*(.fini))
        . = ALIGN(4);
        _etext = .;
    } > FLASH

    .rodata :
    {
        . = ALIGN(4);
        *(.rodata)
        *(.rodata*)
        . = ALIGN(4);
    } > FLASH

    .ARM.extab : { *(.ARM.extab* .gnu.linkonce.armextab.*) } > FLASH
    .ARM :
    {
        __exidx_start = .;
        *(.ARM.exidx*)
        __exidx_end = .;
    } > FLASH

    .preinit_array :
    {
        PROVIDE_HIDDEN(__preinit_array_start = .);
        KEEP(*(.preinit_array*))
        PROVIDE_HIDDEN(__preinit_array_end = .);
    } > FLASH
    .init_array :
    {
        PROVIDE_HIDDEN(__init_array_start = .);
        KEEP(*(SORT(.init_array.*)))
        KEEP(*(.init_array*))
        PROVIDE_HIDDEN(__init_array_end = .);
    } > FLASH
    .fini_array :
    {
        PROVIDE_HIDDEN(__fini_array_start = .);
        KEEP(*(SORT(.fini_array.*)))
        KEEP(*(.fini_array*))
        PROVIDE_HIDDEN(__fini_array_end = .);
    } > FLASH

    /* .data 的加载地址在 Flash，运行地址在 SRAM，由 Reset_Handler 搬运 */
    _sidata = LOADADDR(.data);

    .data :
    {
        . = ALIGN(4);
        _sdata = .;
        *(.data)
        *(.data*)
        . = ALIGN(4);
        _edata = .;
    } > SRAM AT> FLASH

    . = ALIGN(4);
    .bss :
    {
        _sbss = .;
        __bss_start__ = _sbss;
        *(.bss)
        *(.bss*)
        *(COMMON)
        . = ALIGN(4);
        _ebss = .;
        __bss_end__ = _ebss;
    } > SRAM

    /* 剩余 SRAM 必须放得下最小堆栈，否则链接失败 */
    ._user_heap_stack :
    {
        . = ALIGN(8);
        PROVIDE(end = .);
        PROVIDE(_end = .);
        . = . + _Min_Heap_Size;
        . = . + _Min_Stack_Size;
        . = ALIGN(8);
    } > SRAM

    .ARM.attributes 0 : { *(.ARM.attributes) }
}
