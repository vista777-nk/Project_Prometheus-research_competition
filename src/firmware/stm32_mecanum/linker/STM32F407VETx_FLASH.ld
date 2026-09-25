/*
 * STM32F407VET6 链接脚本
 *
 * 存储器布局 (RM0090 / DS8626)：
 *   FLASH   512 KB @ 0x08000000
 *   SRAM    128 KB @ 0x20000000   (SRAM1 112KB + SRAM2 16KB 连续)
 *   CCMRAM   64 KB @ 0x10000000   (仅内核可访问，DMA 到不了)
 *
 * 栈放在 SRAM 顶端向下生长，堆从 .bss 之后向上生长，两者相向而行，
 * 末尾的 ._user_heap_stack 段做静态余量检查：链接期就能发现 RAM 不够用。
 *
 * 本固件不使用动态内存 —— 所有缓冲都是静态数组。堆保留 1KB 只为兼容
 * newlib 里少数会碰 sbrk 的函数 (如 snprintf 的某些实现路径)。
 */

ENTRY(Reset_Handler)

_estack = ORIGIN(RAM) + LENGTH(RAM);    /* 主栈顶 = SRAM 末尾 */

_Min_Heap_Size  = 0x400;    /* 1 KB */
_Min_Stack_Size = 0x800;    /* 2 KB */

MEMORY
{
  FLASH  (rx)  : ORIGIN = 0x08000000, LENGTH = 512K
  RAM    (xrw) : ORIGIN = 0x20000000, LENGTH = 128K
  CCMRAM (rw)  : ORIGIN = 0x10000000, LENGTH = 64K
}

SECTIONS
{
  /* 中断向量表必须落在 Flash 起始处：复位后内核从这里取初始 SP 和 PC */
  .isr_vector :
  {
    . = ALIGN(4);
    KEEP(*(.isr_vector))
    . = ALIGN(4);
  } >FLASH

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
  } >FLASH

  .rodata :
  {
    . = ALIGN(4);
    *(.rodata)
    *(.rodata*)
    . = ALIGN(4);
  } >FLASH

  .ARM.extab : { *(.ARM.extab* .gnu.linkonce.armextab.*) } >FLASH
  .ARM : {
    __exidx_start = .;
    *(.ARM.exidx*)
    __exidx_end = .;
  } >FLASH

  .preinit_array :
  {
    PROVIDE_HIDDEN(__preinit_array_start = .);
    KEEP(*(.preinit_array*))
    PROVIDE_HIDDEN(__preinit_array_end = .);
  } >FLASH

  .init_array :
  {
    PROVIDE_HIDDEN(__init_array_start = .);
    KEEP(*(SORT(.init_array.*)))
    KEEP(*(.init_array*))
    PROVIDE_HIDDEN(__init_array_end = .);
  } >FLASH

  .fini_array :
  {
    PROVIDE_HIDDEN(__fini_array_start = .);
    KEEP(*(SORT(.fini_array.*)))
    KEEP(*(.fini_array*))
    PROVIDE_HIDDEN(__fini_array_end = .);
  } >FLASH

  /* .data 的加载地址在 Flash 里，运行地址在 SRAM，由 Reset_Handler 负责搬运 */
  _sidata = LOADADDR(.data);

  .data :
  {
    . = ALIGN(4);
    _sdata = .;
    *(.data)
    *(.data*)
    . = ALIGN(4);
    _edata = .;
  } >RAM AT> FLASH

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
  } >RAM

  /* 只占位不产出内容：如果剩余 RAM 装不下堆+栈，链接会直接报错 */
  ._user_heap_stack :
  {
    . = ALIGN(8);
    PROVIDE(end = .);
    PROVIDE(_end = .);
    . = . + _Min_Heap_Size;
    . = . + _Min_Stack_Size;
    . = ALIGN(8);
  } >RAM

  /* 注意：CubeMX 模板在这里有一段 /DISCARD/ { libc.a(*) libm.a(*) libgcc.a(*) }，
     本项目刻意不抄。我们会用到编译器生成的 memcpy/memset 与 libgcc 的浮点辅助
     例程，整包丢弃这些归档会变成一堆莫名其妙的 undefined reference。 */

  .ARM.attributes 0 : { *(.ARM.attributes) }
}
