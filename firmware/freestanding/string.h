#ifndef SG_FREESTANDING_STRING_H
#define SG_FREESTANDING_STRING_H

/*
 * freestanding 构建专用的 <string.h>。
 *
 * ARM 固件是 freestanding 编译的（没有 libc），工具链里根本没有 <string.h>，
 * 而 common/ 下的算法与协议代码会用到 memcpy / memmove / memset。
 * 与其为了三个函数把整个 newlib 拉进来，不如在这里补一个只含声明的头，
 * 实现放在 firmware/libc_min.c。
 *
 * 之所以单独放一个目录（而不是塞进 firmware/）：
 * 它只在 ARM 构建时进 include 路径，不会影响用系统 <string.h> 的场合。
 */

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void  *memcpy (void *dst, const void *src, size_t n);
void  *memmove(void *dst, const void *src, size_t n);
void  *memset (void *dst, int c, size_t n);
size_t strlen (const char *s);

#ifdef __cplusplus
}
#endif

#endif /* SG_FREESTANDING_STRING_H */
