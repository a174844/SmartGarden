#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
拉取 ARM 固件构建所需的上游源码到 third_party/。

为什么不把这些代码直接放进仓库：ST 的 HAL 加 FreeRTOS 加起来两万多行，
放进来会把本项目的 diff 完全淹掉；而它们本来就是外部依赖，
按固定提交号拉取同样能复现。所以 third_party/ 被 .gitignore 忽略，
构建前跑一次这个脚本即可。

只访问 api.github.com（列目录）和 raw.githubusercontent.com（取文件）。

用法：
    python tools/fetch_deps.py            # 拉到 third_party/
    python tools/fetch_deps.py --force    # 已存在也重新拉
"""

import argparse
import json
import os
import sys
import urllib.error
import urllib.request
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DEST = ROOT / "third_party"

# 依赖清单。ref 固定到提交号，保证不同时间拉到的是同一份代码。
DEPS = [
    {
        "dest": "stm32f4xx-hal-driver",
        "repo": "STMicroelectronics/stm32f4xx-hal-driver",
        "ref": "1f6451c3e07728b4c830744de380e56bf5bc0026",
        # Inc/Legacy 是必须的：stm32f4xx_hal_def.h 里无条件 #include 了
        # "Legacy/stm32_hal_legacy.h"，少了它所有 HAL 头文件都读不下去
        "dirs": ["Inc", "Inc/Legacy", "Src"],
        "exts": (".h", ".c"),
        # 一个模块都不裁：HAL 的 .c 之间互相引用，挑着拉很容易缺符号，
        # 而这些文件只在本地存在、不进仓库，多拉一点无所谓
    },
    {
        "dest": "cmsis-core",
        "repo": "ARM-software/CMSIS_5",
        "ref": "55b19837f5703e418ca37894d5745b1dc05e4c91",
        "dirs": ["CMSIS/Core/Include"],
        "exts": (".h",),
        "strip": "CMSIS/Core/Include",
    },
    {
        "dest": "cmsis-device-f4",
        "repo": "STMicroelectronics/cmsis-device-f4",
        "ref": "a833f4af71410f25b01468f976560d7ff63a2fc9",
        "dirs": ["Include"],
        "exts": (".h",),
        # 只需要 F407 的启动文件与时钟配置，不必把 23 个型号都拉下来
        "files": [
            ("Source/Templates/gcc/startup_stm32f407xx.s", "startup_stm32f407xx.s"),
            # SystemInit()：把 HSE 8MHz 拉到 168MHz，并设好 Flash 等待周期
            ("Source/Templates/system_stm32f4xx.c", "system_stm32f4xx.c"),
        ],
    },
    {
        "dest": "freertos",
        "repo": "FreeRTOS/FreeRTOS-Kernel",
        "ref": "8be86d4a24fd4091f8f4192018423ab590f408db",
        # 用 ARM_CM3 端口而不是 ARM_CM4F：本固件全程整数运算、没有一处浮点
        # （grep 一下就知道），所以按软浮点 ABI 编译，也就没有 FPU 上下文
        # 需要保存 —— 这正是 FreeRTOS 对"不带 FPU 的 M3/M4/M7"推荐的端口。
        # 选 CM4F 反而要多存 17 个字的 FPU 上下文，纯属浪费。
        # 详细取舍见 README 的"编译配置"一节。
        "dirs": ["include", "portable/GCC/ARM_CM3"],
        "exts": (".h", ".c"),
        # 内核本体在仓库根目录，不在 include/ 里，得单独点名
        "files": [
            ("tasks.c", "tasks.c"),
            ("list.c", "list.c"),
            ("queue.c", "queue.c"),
            ("portable/MemMang/heap_4.c", "portable/MemMang/heap_4.c"),
        ],
    },
]

API = "https://api.github.com/repos/{repo}/contents/{path}?ref={ref}"
RAW = "https://raw.githubusercontent.com/{repo}/{ref}/{path}"


def _headers():
    """带上令牌（若有）以避开 api.github.com 的匿名限流（60 次/小时）"""
    h = {"User-Agent": "smartgarden-fetch"}
    tok = os.environ.get("GITHUB_TOKEN") or os.environ.get("GH_TOKEN")
    if tok:
        h["Authorization"] = "Bearer " + tok.strip()
    return h


def http_get(url, timeout=60):
    req = urllib.request.Request(url, headers=_headers())
    with urllib.request.urlopen(req, timeout=timeout) as r:
        return r.read()


def list_dir(repo, ref, path):
    """列出一级目录下所有文件（跳过子目录）"""
    raw = http_get(API.format(repo=repo, path=path, ref=ref))
    items = json.loads(raw.decode("utf-8"))
    if isinstance(items, dict):
        raise RuntimeError("list %s/%s failed: %s" % (repo, path, items.get("message")))
    return [it["path"] for it in items if it["type"] == "file"]


def fetch_one(repo, ref, path, out_path, force):
    if out_path.exists() and not force:
        return "skip"
    out_path.parent.mkdir(parents=True, exist_ok=True)
    data = http_get(RAW.format(repo=repo, ref=ref, path=path))
    out_path.write_bytes(data)
    return "ok"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--force", action="store_true", help="已存在的文件也重新拉")
    args = ap.parse_args()

    jobs = []          # (repo, ref, src_path, out_path)
    for dep in DEPS:
        repo, ref, dest = dep["repo"], dep["ref"], DEST / dep["dest"]
        exts, strip = dep["exts"], dep.get("strip")

        for d in dep.get("dirs", []):
            try:
                files = list_dir(repo, ref, d)
            except Exception as e:                       # noqa: BLE001
                print("列目录失败 %s/%s: %s" % (repo, d, e), file=sys.stderr)
                return 1
            for p in files:
                if not p.endswith(exts):
                    continue
                rel = p[len(strip) + 1:] if strip else p
                jobs.append((repo, ref, p, dest / rel))

        for p, rel in dep.get("files", []):
            jobs.append((repo, ref, p, dest / rel))

    print("待拉取 %d 个文件 -> %s" % (len(jobs), DEST))
    stats = {"ok": 0, "skip": 0, "fail": 0}

    def worker(job):
        repo, ref, src, out = job
        try:
            r = fetch_one(repo, ref, src, out, args.force)
            return r, None
        except Exception as e:                            # noqa: BLE001
            return "fail", "%s: %s" % (src, e)

    with ThreadPoolExecutor(max_workers=8) as pool:
        for r, err in pool.map(worker, jobs):
            stats[r] += 1
            if err:
                print("  失败 %s" % err, file=sys.stderr)

    print("完成：下载 %d，已存在 %d，失败 %d"
          % (stats["ok"], stats["skip"], stats["fail"]))
    return 1 if stats["fail"] else 0


if __name__ == "__main__":
    sys.exit(main())
