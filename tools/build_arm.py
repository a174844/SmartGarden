#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
把固件真正编成可烧写的镜像（ELF / BIN / HEX / MAP）。

为什么用 zig cc 而不是 arm-none-eabi-gcc：
    1) zig 自带 LLVM 的 ARM 后端，`-target thumb-freestanding-eabi` 就能产出
       Cortex-M4 的机器码，不需要为本机再装一套 ARM 工具链；
    2) 它自带的 compiler-rt 里有 __aeabi_* 这些 EABI 运行时函数，
       -nostartfiles 链接时能直接解析，不用手工补。
    唯一要注意的是 CPU 名要用下划线写法 cortex_m4（写 cortex-m4 会被
    zig 按 '-' 切开，报 "unknown CPU: 'cortex'"）。

目标平台没有 libc，所以 common/ 用到的 memcpy/memmove/memset 由
firmware/libc_min.c 提供，<string.h> 由 firmware/freestanding/ 提供。
见 firmware/freestanding/string.h 的说明。

用法：
    python tools/build_arm.py            # 增量编译
    python tools/build_arm.py --clean    # 先清干净再编
"""

import argparse
import os
import shutil
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BUILD = ROOT / "build_arm"
THIRD = ROOT / "third_party"

# ---------------------------------------------------------------- 目标参数

TARGET = "thumb-freestanding-eabi"
CPU = "cortex_m4"          # 注意：下划线，不能写 cortex-m4

# 关于浮点：本固件全程整数运算，没有一处 float/double（可 grep 验证），
# 因此按软浮点 ABI 编译，也不去使能 FPU（软浮点下 __SOFTFP__ 被定义，
# CMSIS 的 __FPU_USED 自动为 0，SystemInit 不会碰 CPACR）。
# 配套的 FreeRTOS 端口因此选 ARM_CM3 而不是 ARM_CM4F。
#
# 不传 -mfpu / -mfloat-abi 的原因：zig 的 freestanding target 对这两个
# 选项处理不完整 —— 传了之后 ELF 头会被标成 hard-float，但代码生成仍是
# 软浮点（目标文件里出现 bl __aeabi_fmul，而不是 vmul.f32），头部与机器码
# 不一致。唯一能让它真出 VFP 指令的写法是 -mcpu=cortex_m4+vfp4，但那又会
# 声明支持双精度、且无法用 +d16 把寄存器窗口限制到 F407 实有的 16 个
# 双字寄存器，等于给将来写浮点的人埋一颗故障。
# 结论：不做浮点运算的固件就不该假装有 FPU。
DEFINES = [
    "STM32F407xx",
    "USE_HAL_DRIVER",
]

# ---------------------------------------------------------------- 源文件

# 本工程自己的代码
PROJECT_SRC = [
    "firmware/main.c",
    "firmware/board_stm32f4.c",
    "firmware/display.c",
    "firmware/sensors.c",
    "firmware/actuators.c",
    "firmware/clock.c",
    "firmware/libc_min.c",
    "firmware/freertos_hooks.c",
    "firmware/faults.c",
    "firmware/uart_ble.c",
]

# 算法/协议核心：不依赖任何 STM32 头文件，与板级实现完全解耦
COMMON_SRC = [
    "common/ble_frame.c",
    "common/control.c",
    "common/crc32.c",
    "common/dht22.c",
    "common/filters.c",
    "common/nvstore.c",
    "common/panel.c",
    "common/ssd1306.c",
]

# 外部依赖（third_party/，由 tools/fetch_deps.py 拉取）
RTOS_SRC = [
    "third_party/freertos/tasks.c",
    "third_party/freertos/list.c",
    "third_party/freertos/queue.c",
    "third_party/freertos/portable/GCC/ARM_CM3/port.c",
    "third_party/freertos/portable/MemMang/heap_4.c",
]

HAL_SRC = [
    "third_party/stm32f4xx-hal-driver/Src/stm32f4xx_hal.c",
    "third_party/stm32f4xx-hal-driver/Src/stm32f4xx_hal_cortex.c",
    "third_party/stm32f4xx-hal-driver/Src/stm32f4xx_hal_rcc.c",
    "third_party/stm32f4xx-hal-driver/Src/stm32f4xx_hal_rcc_ex.c",
    "third_party/stm32f4xx-hal-driver/Src/stm32f4xx_hal_gpio.c",
    "third_party/stm32f4xx-hal-driver/Src/stm32f4xx_hal_dma.c",
    "third_party/stm32f4xx-hal-driver/Src/stm32f4xx_hal_flash.c",
    "third_party/stm32f4xx-hal-driver/Src/stm32f4xx_hal_flash_ex.c",
    "third_party/stm32f4xx-hal-driver/Src/stm32f4xx_hal_iwdg.c",
    "third_party/stm32f4xx-hal-driver/Src/stm32f4xx_hal_pwr.c",
    "third_party/stm32f4xx-hal-driver/Src/stm32f4xx_hal_uart.c",
]

CMSIS_SRC = [
    "third_party/cmsis-device-f4/system_stm32f4xx.c",
]

STARTUP = "third_party/cmsis-device-f4/startup_stm32f407xx.s"

LINKER_SCRIPT = "firmware/stm32f407_flash.ld"

# ---------------------------------------------------------------- include 路径

# firmware/freestanding 必须排在前面：它提供 freestanding 构建缺失的 <string.h>
INCLUDES = [
    "firmware",
    "firmware/freestanding",
    "common",
    "third_party/stm32f4xx-hal-driver/Inc",
    "third_party/cmsis-core",
    "third_party/cmsis-device-f4/Include",
    "third_party/freertos/include",
    "third_party/freertos/portable/GCC/ARM_CM3",
]

# 本工程代码开警告（-Wall -Wextra），第三方代码不开：
# HAL 和 FreeRTOS 在 -Wextra 下会刷出几百条，把真正重要的警告淹掉。
WARN_SRC = set(PROJECT_SRC) | set(COMMON_SRC)


def find_zig():
    """按 PATH -> 环境变量 -> ziglang 包目录 的顺序找 zig。"""
    exe = shutil.which("zig")
    if exe:
        return exe

    env = os.environ.get("ZIG")
    if env and Path(env).exists():
        return env

    import site  # noqa: PLC0415

    cands = []
    for d in list(site.getsitepackages()) + [site.getusersitepackages()]:
        cands.append(Path(d) / "ziglang" / "zig.exe")
        cands.append(Path(d) / "ziglang" / "zig")
    # 相对当前解释器再兜一次（site-packages 有时不在上面两个列表里）
    for rel in (("Lib", "site-packages"), ("lib", "site-packages")):
        cands.append(Path(sys.executable).parent.parent.joinpath(*rel)
                     / "ziglang" / "zig.exe")
        cands.append(Path(sys.executable).parent.parent.joinpath(*rel)
                     / "ziglang" / "zig")
    # ziglang 装在别的解释器/虚拟环境里时，上面的都找不到。
    # 从当前解释器的位置往上层走，把同级的 envs/ 和 versions/ 一起扫一遍 ——
    # 这是托管式 Python（以及 pyenv / conda）常见的目录排布。
    exe = Path(sys.executable).resolve()
    bases = []
    for up in (2, 3):
        if len(exe.parents) > up:
            bases.append(exe.parents[up])
    for key in ("VIRTUAL_ENV", "CONDA_PREFIX"):
        v = os.environ.get(key)
        if v:
            bases.append(Path(v))
    pats = ("envs/*/Lib/site-packages", "envs/*/lib/python*/site-packages",
            "versions/*/Lib/site-packages", "versions/*/lib/python*/site-packages",
            "Lib/site-packages", "lib/python*/site-packages")
    for base in bases:
        for pat in pats:
            for pkgroot in sorted(base.glob(pat)):
                cands.append(pkgroot / "ziglang" / "zig.exe")
                cands.append(pkgroot / "ziglang" / "zig")

    for c in cands:
        if c.exists():
            return str(c)

    sys.exit("找不到 zig。三种解法：\n"
             "  1) pip install ziglang\n"
             "  2) 用装了 ziglang 的那个解释器跑本脚本\n"
             "  3) 设环境变量 ZIG=<zig 可执行文件绝对路径>")


class Compiler:
    def __init__(self, zig, verbose):
        self.zig = zig
        self.verbose = verbose
        self.count = 0

    def run(self, args, quiet=True):
        if self.verbose:
            print("    " + " ".join(str(a) for a in args))
        r = subprocess.run([self.zig] + [str(a) for a in args],
                           capture_output=True, text=True)
        if r.returncode != 0:
            print("\n--- 命令 ---")
            print(" ".join(str(a) for a in [self.zig] + args))
            print("--- 输出 ---")
            print(r.stdout, end="")
            print(r.stderr, end="")
            sys.exit("构建失败")
        if not quiet:
            out = (r.stdout + r.stderr).strip()
            if out:
                print(out)
        return r

    def compile(self, src, obj):
        """单个 .c -> .o。头文件依赖不做跟踪，改头文件用 --clean 重编。"""
        args = [
            "cc",
            "-target", TARGET,
            "-mcpu=" + CPU,
            "-mthumb",
            "-Os",
            "-ffreestanding",
            "-ffunction-sections",
            "-fdata-sections",
            "-fno-common",
            "-g0",
        ]
        if str(src) in WARN_SRC:
            args += ["-Wall", "-Wextra"]
        args += ["-D" + d for d in DEFINES]
        args += ["-I" + str(ROOT / i) for i in INCLUDES]
        args += ["-c", str(ROOT / src), "-o", str(obj)]
        self.run(args)
        self.count += 1


def obj_path(src):
    """把源文件路径映射成 build_arm/ 下的唯一目标文件名。

    直接用 basename 会把 stm32f4xx_hal.c 这类不同目录同名文件撞在一起，
    所以用「相对路径 + 下划线」拼名字。
    """
    p = Path(src)
    parts = list(p.with_suffix("").parts)
    if parts[0] == "third_party":
        parts = parts[2:]        # 去掉 third_party/<repo>/ 两层前缀
    return BUILD / ("_".join(parts) + ".o")


def elf_to_bin(elf_path, bin_path, flash_base):
    """按各段的**加载地址**拼出真正的 Flash 镜像。

    为什么不用 `objcopy -O binary`：它把 SHF_ALLOC 的段全部按地址从低到高铺开，
    而 .data / .bss / ._user_heap_stack 的地址在 RAM（0x2000_xxxx），
    结果会从 0x08000000 一路填到 0x2000473C —— 393MB 的 .bin。
    正确做法是只取"内容真的存在 Flash 里"的那些段：
      - 地址落在 Flash 的段，直接按地址放；
      - .data 的 VMA 在 RAM、LMA 在 Flash，LMA 就是链接脚本里的 _sidata，
        从符号表里读出来即可（启动代码的拷贝循环用的也是这个符号）。

    返回 (镜像字节, 覆盖到的最高 Flash 地址)。
    """
    import struct

    sections, symbols = read_elf(elf_path)
    d = elf_path.read_bytes()
    SHF_ALLOC = 0x2
    SHT_NOBITS = 8

    sidata = None
    for value, size, kind, name in symbols:
        if name == "_sidata":
            sidata = value

    pieces = []   # (flash 内偏移, 字节)
    for s in sections:
        if not (s["flags"] & SHF_ALLOC) or s["size"] == 0:
            continue
        if s["type"] == SHT_NOBITS:
            continue
        if flash_base <= s["addr"] < flash_base + 0x10000000:
            off = s["addr"] - flash_base
        elif s["name"] == ".data" and sidata is not None:
            off = sidata - flash_base
        else:
            continue
        pieces.append((off, d[s["off"]:s["off"] + s["size"]]))

    if not pieces:
        raise RuntimeError("没有任何段落在 Flash 里，链接脚本可能有问题")

    size = max(off + len(b) for off, b in pieces)
    img = bytearray(b"\xff" * size)      # Flash 擦除后的状态是全 1
    for off, b in pieces:
        img[off:off + len(b)] = b

    bin_path.write_bytes(bytes(img))
    return bytes(img), size


def human(n):
    return "%d B (%.1f KB)" % (n, n / 1024.0)


def bin_to_ihex(bin_path, hex_path, base_addr, line_len=16):
    """把裸二进制转成 Intel HEX。

    自己写是因为 zig objcopy 只认 -O binary，不认 ihex；
    而这个格式一共几十行就能生成，比再拉一个工具链省事。
    记录类型：00 数据 / 01 结束 / 04 扩展线性地址（高 16 位）。
    """
    data = bin_path.read_bytes()
    out = []
    upper = None

    def emit(rtype, addr, payload):
        b = bytes([len(payload), (addr >> 8) & 0xFF, addr & 0xFF, rtype]) + payload
        out.append(":" + b.hex().upper() + "%02X" % ((-sum(b)) & 0xFF))

    for off in range(0, len(data), line_len):
        addr = base_addr + off
        hi = (addr >> 16) & 0xFFFF
        if hi != upper:
            emit(4, 0, bytes([(hi >> 8) & 0xFF, hi & 0xFF]))
            upper = hi
        emit(0, addr & 0xFFFF, data[off:off + line_len])

    emit(1, 0, b"")
    hex_path.write_text("\n".join(out) + "\n", encoding="ascii")


# ---------------------------------------------------------------- ELF 报告

# 物理区域。STM32F407VG：1024KB Flash / 128KB SRAM（另有 64KB CCM，本项目没用）
MEM_REGIONS = [
    ("FLASH", 0x08000000, 1024 * 1024),
    ("CCMRAM", 0x10000000, 64 * 1024),
    ("RAM", 0x20000000, 128 * 1024),
]


def read_elf(path):
    """返回 (sections, symbols)。只解析我们关心的字段，不引第三方库。"""
    import struct

    d = path.read_bytes()
    if d[:4] != b"\x7fELF":
        raise ValueError("不是 ELF 文件")

    shoff = struct.unpack_from("<I", d, 32)[0]
    shentsize, shnum, shstrndx = struct.unpack_from("<HHH", d, 46)

    raw = []
    for i in range(shnum):
        o = shoff + i * shentsize
        # name, type, flags, addr, offset, size, link, info, align, entsize
        raw.append(struct.unpack_from("<IIIIIIIIII", d, o))

    st = raw[shstrndx]
    strtab = d[st[4]:st[4] + st[5]]

    def cstr(buf, off):
        end = buf.index(b"\0", off)
        return buf[off:end].decode("utf-8", "replace")

    sections = []
    for r in raw:
        sections.append({
            "name": cstr(strtab, r[0]),
            "type": r[1],
            "flags": r[2],
            "addr": r[3],
            "off": r[4],
            "size": r[5],
            "link": r[6],
            "entsize": r[9],
        })

    # 符号表：SHT_SYMTAB = 2
    symbols = []
    for s in sections:
        if s["type"] != 2 or s["entsize"] == 0:
            continue
        symstr_sec = sections[s["link"]]
        symtab = d[symstr_sec["off"]:symstr_sec["off"] + symstr_sec["size"]]
        n = s["size"] // s["entsize"]
        for i in range(n):
            o = s["off"] + i * s["entsize"]
            nameoff, value, size, info, other, shndx = struct.unpack_from("<IIIBBH", d, o)
            if nameoff == 0:
                continue
            name = cstr(symtab, nameoff)
            if not name:
                continue
            symbols.append((value, size, info & 0xF, name))
        break

    return sections, symbols


def elf_report(path, out_txt):
    sections, symbols = read_elf(path)

    SHF_ALLOC, SHF_WRITE = 0x2, 0x1

    # 逐段归属到物理区域，算出各区实际占用
    used = {name: 0 for name, _, _ in MEM_REGIONS}
    rows = []
    for s in sections:
        if not (s["flags"] & SHF_ALLOC) or s["size"] == 0 or s["type"] == 8:
            # type 8 = NOBITS（.bss）：占 RAM 但不占 Flash，下面单独算
            if s["type"] == 8 and (s["flags"] & SHF_ALLOC) and s["size"]:
                for name, base, _ in MEM_REGIONS:
                    if base <= s["addr"] < base + 0x10000000 and name == "RAM":
                        used[name] += s["size"]
                        rows.append((s["addr"], s["name"], s["size"], name, "bss"))
            continue
        for name, base, _ in MEM_REGIONS:
            if base <= s["addr"] < base + 0x10000000:
                # .data 的加载地址在 Flash、运行地址在 RAM，两边都占
                used[name] += s["size"]
                rows.append((s["addr"], s["name"], s["size"], name,
                             "data" if s["flags"] & SHF_WRITE else "text"))
                break

    # .data 在 Flash 里还要占一份初始值
    for s in sections:
        if s["name"] == ".data" and s["size"]:
            used["FLASH"] += s["size"]

    print("")
    print("段布局")
    print("  %-10s %-22s %10s  %s" % ("地址", "段", "大小", "区域"))
    for addr, nm, size, region, kind in sorted(rows):
        print("  0x%08X %-22s %10d  %s (%s)" % (addr, nm, size, region, kind))

    print("")
    print("占用")
    lines = []
    for name, _, cap in MEM_REGIONS:
        if used[name] == 0 and name == "CCMRAM":
            continue
        pct = 100.0 * used[name] / cap
        s = "  %-7s %8s / %-10s %5.1f%%" % (name, human(used[name]), human(cap), pct)
        print(s + ("   <-- 超出！" if used[name] > cap else ""))
        lines.append(s)

    if out_txt:
        # 等价的 map：按地址排序的符号表，调试时用得上
        syms = sorted({(v, n, s, k) for v, s, k, n in symbols if v and k in (1, 2)})
        with open(out_txt, "w", encoding="utf-8") as f:
            f.write("# 按地址排序的符号表（等价于链接 map 的符号部分）\n")
            f.write("# 由 tools/build_arm.py 从 ELF 的 .symtab 生成\n")
            f.write("# %-10s %6s %-8s %s\n" % ("地址", "大小", "类型", "符号"))
            for v, n, s, k in syms:
                f.write("  0x%08X %6d %-8s %s\n"
                        % (v, s, "FUNC" if k == 2 else "OBJECT", n))
        print("  （符号表另存为 %s）" % out_txt.name)

    over = [n for n, _, cap in MEM_REGIONS if used[n] > cap]
    return over


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--clean", action="store_true", help="删掉 build_arm/ 重新编")
    ap.add_argument("-v", "--verbose", action="store_true", help="打印每条编译命令")
    args = ap.parse_args()

    if args.clean and BUILD.exists():
        shutil.rmtree(BUILD)
    BUILD.mkdir(exist_ok=True)

    # 依赖没拉的话先提示，不要在第 30 个文件上才报找不到头文件
    missing = [p for p in (RTOS_SRC + HAL_SRC + CMSIS_SRC + [STARTUP])
               if not (ROOT / p).exists()]
    if missing:
        print("缺少依赖文件，先跑一次：python tools/fetch_deps.py")
        for m in missing[:5]:
            print("   " + m)
        if len(missing) > 5:
            print("   ... 还有 %d 个" % (len(missing) - 5))
        return 1

    zig = find_zig()
    cc = Compiler(zig, args.verbose)

    t0 = time.time()
    print("zig          %s" % zig)

    sources = PROJECT_SRC + COMMON_SRC + RTOS_SRC + HAL_SRC + CMSIS_SRC
    objs = []
    for src in sources:
        o = obj_path(src)
        objs.append(o)
        cc.compile(src, o)

    # 启动文件（汇编）单独编
    startup_obj = BUILD / "startup_stm32f407xx.o"
    cc.run(["cc", "-target", TARGET, "-mcpu=" + CPU, "-mthumb",
            "-c", str(ROOT / STARTUP), "-o", str(startup_obj)])
    objs.append(startup_obj)
    cc.count += 1

    print("编译         %d 个文件，%.1fs" % (cc.count, time.time() - t0))

    # ---------------- 链接 ----------------
    elf = BUILD / "smartgarden.elf"
    symf = BUILD / "smartgarden.sym.txt"
    link = [
        "cc",
        "-target", TARGET,
        "-mcpu=" + CPU,
        "-mthumb",
        "-nostartfiles",                 # 启动代码由 startup_stm32f407xx.s 提供；
                                         # 不用 -nostdlib，zig 的 compiler-rt
                                         # 还要负责 __aeabi_* 那批 EABI 函数
        "-Wl,-T,%s" % (ROOT / LINKER_SCRIPT),
        "-Wl,-e,Reset_Handler",          # 显式指定入口。链接脚本里的 ENTRY() 被
                                         # zig 自带的默认入口参数盖掉了，不补这一句
                                         # ELF 头里的 e_entry 会是 0（上板不影响 ——
                                         # 硬件走的是向量表；但调试器载入时会告警）
        "-Wl,--gc-sections",
        # 注意：zig 的 cc 会过滤链接器参数，-Map 和 --print-memory-usage 都会被
        # 拒绝，所以体积报告和符号表由下面的 elf_report() 自己从 ELF 里算。
        "-o", str(elf),
    ] + [str(o) for o in objs]

    cc.run(link, quiet=False)

    if not elf.exists():
        return 1

    # ---------------- 产出烧写格式 ----------------
    binf = BUILD / "smartgarden.bin"
    hexf = BUILD / "smartgarden.hex"
    _, bin_size = elf_to_bin(elf, binf, 0x08000000)
    bin_to_ihex(binf, hexf, 0x08000000)

    over = elf_report(elf, symf)

    import struct
    e_entry = struct.unpack_from("<I", elf.read_bytes(), 24)[0]

    print("")
    print("入口  0x%08X（应为 Reset_Handler；为 0 说明 -e 没生效）" % e_entry)
    print("ELF  %s (%s 含符号)" % (elf.name, human(elf.stat().st_size)))
    print("BIN  %s (%s，就是真正写进 Flash 的字节数)"
          % (binf.name, human(bin_size)))
    print("HEX  %s (%s)" % (hexf.name, human(hexf.stat().st_size)))
    print("")
    print("烧写（ST-Link）：st-flash write %s 0x08000000" % binf.relative_to(ROOT))
    print("烧写（OpenOCD）：openocd -f interface/stlink.cfg -f target/stm32f4x.cfg \\")
    print("                        -c \"program %s 0x08000000 verify reset exit\""
          % binf.relative_to(ROOT))

    if over:
        print("")
        print("构建失败：以下区域放不下 -> %s" % ", ".join(over))
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
