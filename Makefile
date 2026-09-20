# 智能花园环境调控系统 —— 顶层入口
#
# 常用命令：
#   make deps      拉取上游依赖到 third_party/（只需一次）
#   make firmware  用 zig 把固件编成可烧写的镜像
#   make check     校验镜像（向量表 / 内核异常 / Flash 边界）
#   make all       上面全部
#
# Windows 上没有 make 的话，用 mingw32 自带的那份：
#   mingw32-make SHELL=sh firmware
# 或者直接调脚本（等价，且不依赖 make）：
#   python tools/fetch_deps.py
#   python tools/build_arm.py
#   python tools/check_image.py

PYTHON ?= python3

.PHONY: all deps firmware check clean distclean help

all: firmware check

help:
	@echo "deps      拉取 third_party/ 下的 HAL / CMSIS / FreeRTOS"
	@echo "firmware  编译 ARM 固件 -> build_arm/smartgarden.bin"
	@echo "check     校验 build_arm/smartgarden.elf 的向量表与边界"
	@echo "clean     删掉构建产物（保留已拉取的依赖）"
	@echo "distclean 连 third_party/ 一起删掉"

deps:
	$(PYTHON) tools/fetch_deps.py

firmware:
	$(PYTHON) tools/build_arm.py

check: firmware
	$(PYTHON) tools/check_image.py

clean:
	rm -rf build_arm

distclean: clean
	rm -rf third_party
