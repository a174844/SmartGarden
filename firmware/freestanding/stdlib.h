#ifndef SG_FREESTANDING_STDLIB_H
#define SG_FREESTANDING_STDLIB_H

/*
 * freestanding 构建专用的 <stdlib.h>。
 *
 * FreeRTOS 的 tasks.c / queue.c / list.c / heap_4.c 都 #include <stdlib.h>，
 * 但翻一遍实际用到的符号就会发现：它们只用到了 size_t 和 NULL，
 * malloc/free 走的是自己的 pvPortMalloc/vPortFree（heap_4.c 实现），
 * 跟 C 库没有关系。所以这里给一个只含类型的最小头就够了，
 * 不需要把 newlib 的 stdlib 拉进来。
 *
 * 【如果将来 FreeRTOS 升级后这里编译不过，报某个 stdlib 函数未声明，
 *   说明它真的开始用 C 库了 —— 那时要在 firmware/libc_min.c 里补实现，
 *   而不是把这个头加大。】
 */

#include <stddef.h>   /* size_t / NULL */

#endif /* SG_FREESTANDING_STDLIB_H */
