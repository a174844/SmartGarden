/*
 * STM32F407VG（1024KB Flash / 128KB SRAM + 64KB CCM）的链接脚本。
 *
 * 写的是 GCC 语法（zig cc 用的也是 ld.lld，语法一致）。
 *
 * 段布局的要点：
 *   - 中断向量表必须在 Flash 最开始（0x08000000），上电时硬件直接从这里取
 *     初始 SP 和复位入口，所以 .isr_vector 要 KEEP 且排在最前；
 *     加了 --gc-sections 之后它没有代码引用，不 KEEP 会被整个丢掉。
 *   - .data 的运行地址在 RAM、加载地址在 Flash，复位时由 startup 里的
 *     拷贝循环用 _sidata/_sdata/_edata 搬过去。
 *   - 参数区用的是最后一个扇区（扇区 11，0x080E0000），
 *     这里不给它单独划段，靠 board_stm32f4.c 里的绝对地址访问，
 *     只要代码区别长到那里就行（1024KB 的固件如果用满 1000KB 以上要重新规划）。
 */

ENTRY(Reset_Handler)

/* 栈顶：SRAM 的最高地址，加 _estack 这个符号名是 startup 里约定的 */
_estack = 0x20020000;      /* 128KB SRAM 的末尾 */

_Min_Heap_Size  = 0x200;   /* 留给 newlib 的堆 —— 本工程不用 malloc，
                              但链接器仍会检查这两个符号存在 */
_Min_Stack_Size = 0x400;   /* 主栈：调度器启动前用的是它 */

MEMORY
{
  /* CCM 从 0x10000000 起，只能放数据且不能被 DMA 访问；
     本工程没用它，先留着后面的段定义，需要时可加 .ccmram */
  CCMRAM (xrw) : ORIGIN = 0x10000000, LENGTH = 64K
  RAM    (xrw) : ORIGIN = 0x20000000, LENGTH = 128K
  FLASH  (rx)  : ORIGIN = 0x08000000, LENGTH = 1024K
}

SECTIONS
{
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
    *(.glue_7)         /* ARM/Thumb 互调用胶水，外链库时用得上 */
    *(.glue_7t)
    *(.eh_frame)

    KEEP(*(.init))
    KEEP(*(.fini))

    . = ALIGN(4);
    _etext = .;
  } >FLASH

  /* .init_array / .fini_array 里放全局构造析构；libc_min.c 的
     __libc_init_array 会遍历前者 */
  .init_array :
  {
    . = ALIGN(4);
    PROVIDE_HIDDEN(__init_array_start = .);
    KEEP(*(SORT_BY_INIT_PRIORITY(.init_array.*)))
    KEEP(*(.init_array))
    PROVIDE_HIDDEN(__init_array_end = .);
  } >FLASH

  .fini_array :
  {
    . = ALIGN(4);
    PROVIDE_HIDDEN(__fini_array_start = .);
    KEEP(*(SORT_BY_INIT_PRIORITY(.fini_array.*)))
    KEEP(*(.fini_array))
    PROVIDE_HIDDEN(__fini_array_end = .);
  } >FLASH

  /* 只读数据：字符串常量、字库 FONT5X7、SSD1306 的初始化指令流都在这里。
     字库 448 字节 + 指令流几十字节，加起来不到 1KB。 */
  .rodata :
  {
    . = ALIGN(4);
    *(.rodata)
    *(.rodata*)
    . = ALIGN(4);
  } >FLASH

  .ARM.extab : { *(.ARM.extab* .gnu.linkonce.armextab.*) } >FLASH
  .ARM        :
  {
    __exidx_start = .;
    *(.ARM.exidx*)
    __exidx_end = .;
  } >FLASH

  /* .data 的加载地址在 Flash，运行地址在 RAM */
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

  /* 链接时检查 RAM 放得下：栈 + 堆 + 已经用掉的静态数据。
     这两个断言比"上板跑飞了再回来查"便宜太多。 */
  ._user_heap_stack :
  {
    . = ALIGN(8);
    PROVIDE(end = .);
    PROVIDE(_end = .);
    . = . + _Min_Heap_Size;
    . = . + _Min_Stack_Size;
    . = ALIGN(8);
  } >RAM

  /DISCARD/ :
  {
    *(.note.GNU-stack)
    *(.gnu_debuglink)
    *(.gnu.lto_*)
  }

  .ARM.attributes 0 : { *(.ARM.attributes) }
}
