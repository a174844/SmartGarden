/*
 * freestanding 构建下需要自己补的那几个 libc 符号。
 *
 * 本工程的 ARM 固件用 `zig cc -target thumb-freestanding-eabi` 编译，
 * 目标平台没有 libc：编译器自带的内建头里没有 <string.h>，
 * 也没有链接任何 C 运行库。common/ 下的算法与协议代码用到了
 * memcpy / memmove / memset 三个（见 firmware/freestanding/string.h），
 * 这里给它们实现。
 *
 * 不用 newlib 的理由很直接：为了三个内存函数把整个 C 库拉进固件，
 * 光 flash 就多占十几 KB，还会带进 _sbrk/_write 那一串根本用不上的桩。
 */

#include <stddef.h>
#include <stdint.h>

void *memcpy(void *dst, const void *src, size_t n)
{
    uint8_t       *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;

    /* 按字搬比按字节快得多，这里对齐情况不固定，所以不做对齐优化，
       先保证正确：源和目的可能重叠的情况由 memmove 负责，memcpy 不管。 */
    while (n--) *d++ = *s++;
    return dst;
}

void *memmove(void *dst, const void *src, size_t n)
{
    uint8_t       *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;

    if (d == s || n == 0) return dst;

    /* 目的在前、且两段重叠时必须从后往前搬，否则前几个字节就把源数据盖掉了。
       ble_frame.c 的字节流组帧器每丢一个字节就 memmove 一次整段，
       正是这种重叠场景。 */
    if (d < s) {
        while (n--) *d++ = *s++;
    } else {
        d += n;
        s += n;
        while (n--) *--d = *--s;
    }
    return dst;
}

void *memset(void *dst, int c, size_t n)
{
    uint8_t *d = (uint8_t *)dst;

    while (n--) *d++ = (uint8_t)c;
    return dst;
}

size_t strlen(const char *s)
{
    const char *p = s;

    while (*p) p++;
    return (size_t)(p - s);
}

/*
 * 启动文件（startup_stm32f407xx.s）在跳 main 之前会调 __libc_init_array，
 * 正常由 newlib 提供。这里按它的语义实现：依次执行 .init_array 段里的函数指针。
 *
 * 本工程没有 C++ 全局对象，但编译器/链接器仍可能往 .init_array 里放东西，
 * 所以还是老老实实走一遍，而不是给个空函数糊过去 ——
 * 空函数在"现在没事"和"以后加了带构造的东西就悄悄不执行"之间选错了。
 */
typedef void (*init_fn_t)(void);

extern init_fn_t __init_array_start[];
extern init_fn_t __init_array_end[];

void __libc_init_array(void)
{
    size_t i;
    size_t n = (size_t)(__init_array_end - __init_array_start);

    for (i = 0; i < n; i++) {
        __init_array_start[i]();
    }
}
