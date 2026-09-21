# SmartGarden — 智能花园环境调控系统

**仓库：<https://github.com/a174844/SmartGarden>**

基于 **STM32F407 + FreeRTOS** 的家庭园艺自动灌溉与补光系统：环境参数实时监测、
阈值闭环控制、机载 OLED 状态屏，以及手机端远程干预。

固件面向 STM32F407VGT6，交叉编译产出 **20.3 KB Flash / 19.3 KB RAM** 的可执行镜像；
镜像校验（向量表、内核异常入口、指令集、参数区边界）全部通过。

```
 采集任务(2s) ──q_sensor(长度1,Overwrite)──┬─► BLE 通信任务 ──► App
                                             └─► 执行器控制任务 ──► PWM 水泵/补光
                                                        ▲
 App ──q_cmd(手动指令)────────────────────────────────────┘

 采集任务 ─┐
           ├─ mtx_i2c（I2C 总线互斥）─► OLED 页 2-6：实时数值 + 进度条
 监控任务 ─┘                            OLED 页 1/7：模式 · 链路 · 运行时间
 监控任务 ─── IWDG 喂狗（发现任务卡死就停止喂狗，由硬件看门狗复位）
```

## 技术栈

STM32F407 / FreeRTOS / C / ADC + DHT22 单总线 / PWM / **I2C（SSD1306 OLED）** /
BLE 透传模块 / Android App

## 硬件设计

### 主控

STM32F407VGT6（LQFP100，1 MB Flash / 128 KB SRAM，Cortex-M4F）。

选型依据：两路 ADC 通道 + 两路 PWM + 一路 I2C + 一路 UART 要同时用，
F103 那档的引脚复用不够宽裕；另外配置参数需要一个能单独擦除的 Flash 扇区，
F407 的扇区结构正好允许把最后一块单独划出来（见下面「参数区」）。

固件全程整数运算，没有一处 `float` / `double`，因此按软浮点 ABI 编译，
FreeRTOS 也用不带 FPU 的 Cortex-M 端口。

### 引脚分配

| 功能 | 引脚 | 配置 | 器件 / 接线 |
|---|---|---|---|
| 土壤湿度 | PA0 | 模拟输入，ADC1_IN0 | 电阻式探头，分压后输出 3000 mV（干）～ 1200 mV（浸水） |
| 光照强度 | PA1 | 模拟输入，ADC1_IN1 | 光敏电阻与固定电阻分压 |
| DHT22 数据线 | PA8 | 开漏输出 + 输入切换，外部上拉 | 单总线，4.7 kΩ 上拉 |
| 水泵 PWM | PA6 | 复用推挽，AF2 → TIM3_CH1 | 经驱动管/继电器接水泵 |
| 补光 PWM | PA7 | 复用推挽，AF2 → TIM3_CH2 | 经驱动管接 LED 补光灯 |
| BLE 模块 | PA2 / PA3 | 复用推挽，AF7 → USART2 | UART 透传模块 |
| OLED 状态屏 | PB6 / PB7 | 复用开漏，AF4 → I2C1 | SSD1306 128×64，从机地址 0x3C，400 kHz |
| 心跳指示灯 | PD0 | 推挽输出，低电平点亮 | 上电即亮，便于确认程序起来了 |
| 参数区 | — | Flash 扇区 11 | 0x080E0000 起，整扇区 128 KB 归参数用 |

外设时钟门控集中在 `board_init()` 里一次开完（`AHB1ENR` 的 GPIOA/B/D、
`APB1ENR` 的 TIM3/I2C1、`APB2ENR` 的 ADC1）。这一段漏掉任何一个，表现都是
"寄存器写进去了、读回来也对，但外设毫无反应"，且不报任何错。

### 时钟

`firmware/clock.c` 直接从 HSE 起 PLL，不经 `HAL_RCC_OscConfig`：

```
HSE 8MHz ──/8(PLL_M)──► 1MHz ──x336(PLL_N)──► 336MHz ──/2(PLL_P)──► SYSCLK 168MHz

AHB  168MHz   （HPRE  = /1）
APB1  42MHz   （PPRE1 = /4）─► TIM3 定时器时钟 = 2 x PCLK1 = 84MHz
APB2  84MHz   （PPRE2 = /2）─► ADC 预分频 /4 = 21MHz
```

三个容易踩的点：

- **Flash 等待周期必须先设好再提频。** 168 MHz @ 3.3 V 需要 5 个等待周期，
  顺序颠倒的话中间那段取指跑在欠压时序上，可能读到错指令。
  同时打开指令/数据 cache 与预取，否则 168 MHz 下 Flash 会成为瓶颈。
- **APB1 分频系数不为 1 时，挂在上面的定时器时钟是 PCLK1 的两倍。**
  所以 TIM3 的 `PSC=83` 分的是 84 MHz 而不是 42 MHz，得到 1 MHz 计数。
- **ADC 时钟上限 36 MHz。** APB2 = 84 MHz，用默认的 `/2` 就是 42 MHz，超规格且
  手册明确不保证精度。取 `/4` = 21 MHz。

**晶振失效时自动回落 HSI。** HSE 起振等待带超时，起不来就改用内部 16 MHz RC
（`PLL_M` 换成 16，`PLLSRC` 切到 HSI），同样出 168 MHz。RC 有百分之几的温漂，
但"土壤湿度 + 定时灌溉"是秒级应用，这个精度够用。`clock_hse_ok()` 把结果暴露
出来，现场一眼就能区分是软件问题还是晶振虚焊。

### 参数区

阈值与校准参数写在主 Flash 的最后一个扇区（扇区 11），整扇区独占，
和程序区不重叠。扇区是擦除的最小单位，所以改参数是"整扇区擦掉再写回"，
对几十字节的参数来说代价可以接受。

写入的内容带 CRC32 校验，校验不过就回落到编译期默认值 —— 掉电写坏、
或者固件升级后参数布局变了，设备不会带着垃圾参数跑。

### PWM

TIM3，CH1/CH2，PWM 模式 1 + 预装载，`PSC=83` → 1 MHz 计数、`ARR=999` → 1 kHz，
占空比分辨率千分之一。

## 机载状态屏

128×64 的屏分成 8 页（每页 8 像素高）。每一页固定归一个刷新来源，
两个任务各刷各的、互不覆盖：

```
┌──────────────────────────────┐
│ SMARTGARDEN                  │  页 0  标题（上电画一次）
│──────────────────────────────│
│ AUTO     LINK OK             │  页 1  模式 + 链路状态      ← 监控任务
│ T 25.3C  H 61.2%             │  页 2  温度 / 湿度
│ SOIL 45% LUX 3200            │  页 3  土壤湿度 / 光照
│ PUMP 62% LED 10%             │  页 4  水泵 / 补光占空比
│ MOIST  [===========       ]  │  页 5  土壤湿度条（45%）
│ LIGHT  [====              ]  │  页 6  光照条（3200/20000）  ← 采集任务
│ UP 0001234 S                 │  页 7  运行时间             ← 监控任务
└──────────────────────────────┘
```

`ssd1306.c` 只维护 1 KB 显存，真正的总线收发交给上层传入的回调，
所以驱动逻辑与 I2C 硬件完全解耦；`panel.c` 负责"哪一页画什么"的排版。

## 关键设计

| 问题 | 处理 |
|---|---|
| 水泵/补光灯启停造成电源纹波，ADC 采样跳变 | 先中值剔脉冲、再滑动平均平滑的二级滤波 |
| 掉电后阈值/校准参数错乱导致设备异常 | 参数写入内部 Flash，附 CRC32 校验，校验失败回落默认值 |
| **采集任务与监控任务共用 I2C 总线刷新屏幕** | **互斥量保护"显存 + 一次刷新的全部 I2C 传输"，页区间互不重叠** |
| 多任务并发抢共享外设 | 互斥量 + 队列，队列长度 1 且用 Overwrite 保证采集周期不被慢消费者拖住 |
| 任务卡死 | 监控任务检测各任务计数；疑似卡死时**故意停止喂狗**，由 IWDG 复位 |
| BLE 连接异常掉线但状态未更新 | 1s 心跳，连续 3 次无应答则强制关断执行器并回到自动模式 |
| BLE 透传是字节流，不保证一帧一次到达 | 独立组帧器，处理拆包 / 粘包 / 前置噪声 |
| 屏没插好导致 I2C 从机把 SDA 拉死 | 每一步等待都带超时；失败就发 STOP 释放总线，并停止后续刷新 |

三点具体落实现方式：

- **I2C 刷新独占总线。** 一次刷新是「页地址指令 + 128 字节显存」，中间被切走就会
  把数据写进别人的页；互斥量圈的是整段刷新，不是单次总线访问。拿不到锁的任务
  直接跳过这一轮，不排队等 —— 采集周期与喂狗节奏比这一帧屏幕内容重要。
- **DHT22 读取期间关中断。** 位判定靠高电平宽度（0 约 27 µs、1 约 70 µs），
  整段约 5 ms，被打断就必然读错。位宽与延时都用 DWT 周期计数器测量，
  不数循环圈数（后者随优化等级失效），起始低电平也用忙等。
- **BLE 掉线强制回自动模式。** 手动模式下发的是固定占空比，链路断了还停在上面
  可能让水泵一直转，所以心跳连续 3 次无应答时关断执行器并切回自动闭环。

## 时基与异常处理

SysTick 完全交给 FreeRTOS（`HAL_InitTick()` 覆盖为空实现，由
`xPortStartScheduler()` 统一配置），HAL 的毫秒计数改由 `vApplicationTickHook()`
供货 —— 否则 HAL 会让 SysTick 在调度器启动**之前**就打中断，此时就绪链表还没建好。

`firmware/faults.c` 自己实现了 `HardFault` / `MemManage` / `BusFault` / `UsageFault`
入口（默认全被 `.thumb_set` 指向死循环的 `Default_Handler`，跑飞后静悄悄卡住、
不留任何线索）：先把 `CFSR` / `HFSR` 存下来供调试器查看，再关中断等看门狗复位。
`freertos_hooks.c` 另挂了栈溢出 / 堆耗尽 / 断言失败的内核钩子。

## 目录结构

```
common/                     硬件无关的算法与协议层
  filters.c/.h              二级滤波：中值 + 滑动平均
  crc32.c/.h                CRC32 (IEEE 802.3)
  nvstore.c/.h              阈值/校准参数的序列化 + CRC32 校验
  control.c/.h              阈值闭环决策与手动模式
  dht22.c/.h                DHT22 数据帧解码（校验和、负温度）
  ble_frame.c/.h            BLE 组帧/解帧/校验/控制指令解析/字节流组帧
  ssd1306.c/.h              SSD1306 显存、5x7 字库、指令与显存打包
  panel.c/.h                状态屏排版（页 0-7 各放什么）
firmware/                   STM32F407 目标代码
  board.h                   板级抽象接口
  board_stm32f4.c           目标板实现（ADC / PWM / DHT22 / Flash / IWDG / UART / I2C1）
  clock.c/.h                系统时钟：HSE 8MHz -> PLL -> 168MHz（含晶振失效回落 HSI）
  uart_ble.c/.h             BLE 透传串口 USART2 的句柄定义与初始化
  freertos_hooks.c          HAL 时基挂到 FreeRTOS tick hook + 内核钩子（栈溢出/堆耗尽/断言）
  faults.c                  HardFault / MemManage / BusFault / UsageFault 入口
  libc_min.c                freestanding 下要自己给的 memcpy/memmove/memset + __libc_init_array
  freestanding/             只含声明的 <string.h> / <stdlib.h>，仅 ARM 构建使用
  FreeRTOSConfig.h          FreeRTOS 配置（含内核异常到 CMSIS 向量名的映射）
  stm32f4xx_hal_conf.h      HAL 模块裁剪与 HSE/HSI 取值
  stm32f407_flash.ld        链接脚本（1024KB Flash / 128KB SRAM）
  sensors.c/.h              传感器采集 + 工程量换算
  actuators.c/.h            水泵与补光的占空比封装
  display.c/.h              OLED 的板级封装（把 panel 画好的显存推到屏上）
  main.c                    四个任务、队列、互斥量、参数装载
tools/                      构建脚本
  fetch_deps.py             按固定提交号拉取 HAL / CMSIS / FreeRTOS 到 third_party/
  build_arm.py              zig 交叉编译 -> build_arm/smartgarden.{elf,bin,hex} + 体积报告
  check_image.py            读 ELF 字节校验向量表 / 内核异常 / 指令集 / Flash 边界
third_party/                上游依赖（.gitignore 忽略，由 fetch_deps.py 生成）
Makefile                    deps / firmware / check 的统一入口
docs/BLE协议.md             BLE 服务/特征与帧格式
```

## 编译与烧写

### 一、拉依赖 + 交叉编译 + 镜像校验

本工程**不需要安装 arm-none-eabi-gcc**。[zig](https://ziglang.org/) 自带 LLVM 的
ARM 后端，`-target thumb-freestanding-eabi` 就能产出 Cortex-M4 的目标文件，
它捆绑的 compiler-rt 还提供 `__aeabi_*` 那批 EABI 运行时函数。

```bash
pip install ziglang                     # 提供 zig 命令

python tools/fetch_deps.py              # 拉 HAL / CMSIS / FreeRTOS 到 third_party/
python tools/build_arm.py               # 编译 + 链接 -> build_arm/smartgarden.{elf,bin,hex}
python tools/check_image.py             # 校验镜像（向量表 / 内核异常 / Flash 边界）
```

有 `make` 的话等价于 `make deps && make firmware && make check`。

依赖按**固定提交号**从上游拉取（ST 的 F4 HAL 驱动、CMSIS Core + Device F4、
FreeRTOS-Kernel），不进版本库 —— 两万多行外部代码放进来只会把本项目的 diff 完全淹掉。
清单和提交号都在 `tools/fetch_deps.py` 里，`third_party/` 被 `.gitignore` 忽略。

编译产物：

```
编译         36 个文件，2.2s

段布局
  地址         段                              大小  区域
  0x08000000 .isr_vector                     392  FLASH (text)
  0x08000188 .text                         18900  FLASH (text)
  0x08004B3C .rodata                         952  FLASH (text)
  0x08004EF4 .ARM.extab                       24  FLASH (text)
  0x08004F0C .ARM                            944  FLASH (text)
  0x20000000 .data                            44  RAM (data)
  0x2000002C .bss                          18192  RAM (bss)
  0x2000473C ._user_heap_stack              1540  RAM (data)

占用
  FLASH   20816 B (20.3 KB) / 1048576 B (1024.0 KB)   2.0%
  RAM     19784 B (19.3 KB) /  131072 B (128.0 KB)  15.1%

入口  0x080042DD（Reset_Handler）
BIN   smartgarden.bin (20816 B，就是真正写进 Flash 的字节数)
```

`check_image.py` 不看编译日志，直接读 ELF 里的字节做检查：

```
[ 0] 0x20020000  初始 SP = _estack（128KB SRAM 的栈顶）
[ 1] 0x08004315  Reset_Handler
[ 3] 0x08001059  HardFault_Handler      ← 工程自己的实现，不是 Default_Handler
[11] 0x0800333D  SVC_Handler            ← FreeRTOS vPortSVCHandler
[14] 0x08003485  PendSV_Handler         ← FreeRTOS xPortPendSVHandler
[15] 0x08003579  SysTick_Handler        ← FreeRTOS xPortSysTickHandler
代码末尾 0x0800515C，参数区 0x080E0000（扇区 11）—— 没越界
镜像校验通过：向量表、内核异常、指令集、Flash 边界都正常。
```

这里"内核异常不能落在 `Default_Handler`"最值得一查：启动文件把未实现的异常都
`.thumb_set` 到了死循环的 `Default_Handler`，SVC/PendSV/SysTick 若也停在那儿，
第一次任务切换就会卡死在中断里 —— 而编译、链接全都是通过的。

### 二、烧写

`build_arm/smartgarden.bin` 从 `0x08000000` 写入即可（ST-Link / J-Link /
OpenOCD / Keil 均可）。以 OpenOCD 为例：

```bash
openocd -f interface/stlink.cfg -f target/stm32f4x.cfg \
        -c "program build_arm/smartgarden.elf verify reset exit"
```

上电后 PD0 的心跳灯先亮，随后 OLED 应显示标题页与实时数值。

## 构建说明

固件全程整数运算、无一处 `float` / `double`，因此按软浮点 ABI 编译，FreeRTOS 用
不带 FPU 的 `ARM_CM3` 端口 —— 选 CM4F 反而要多保存 17 个字的 FPU 上下文。

目标平台没有 libc，`firmware/freestanding/` 放了两个只有声明的头，
实现是 `firmware/libc_min.c` 里的 `memcpy` / `memmove` / `memset` 与
`__libc_init_array`。不引 newlib 的理由很简单：为这三个函数把整个 C 库拉进来，
光 Flash 就多占十几 KB，还会带进 `_sbrk` / `_write` 一串用不上的桩。

## 调试中修正的问题

这些都不是编译错误 —— 代码能编过、能链接、跑起来也"正常"，是调试时逐个定位的：

| 表现 | 原因 | 修法 |
|---|---|---|
| 水泵永远不转 | 阈值滞回判据写成 `low <= high`，恒成立 | 修正判据 |
| 土壤忽然变成 0%、水泵满速 | 滑动平均窗口初始全 0，头几次输出被拉低 | `sensors_prime()` 用首个读数预填 |
| 偶发花屏而非不亮，很难查 | 忘置 `I2C_CCR` 的 F/S 位，SCL 跑到 600 kHz，超出从机能力 | 置位选 400 kHz 快速模式 |
| `MANUAL` 切回 `AUTO` 后屏上留 "AL" 残影 | `ssd1306_text` 只清自己要画的格子 | 先涂 6 格空白再写 |
| 100% 湿度显示成 "00%" | 按 2 格排版，整数部分放不下 | 改 3 格 |
| 采集任务第一次读 DHT22 就挂死 | 关中断临界区里调 `HAL_Delay(2)`，`uwTick` 不前进 | 改用 DWT 忙等 `delay_us()` |
| DHT22 温湿度全错、校验和不过 | 判 0/1 数循环圈数，40 圈 ≈ 0.2 µs，远小于 26 µs 高电平 | 改用 DWT 量高电平时间，阈值 50 µs |
| 同上，读 ADC 时任务挂死 | F4 只有写 `SWSTART` 才启动规则转换，`ADON` 只管上电 | 改写 `SWSTART` |
| ADC 精度不达标 | APB2 84 MHz 下默认 `/2` = 42 MHz，超 36 MHz 上限 | 改 `/4` = 21 MHz |
| 健康运行时也会被看门狗复位 | 超时 2000 ms、喂狗周期同为 2000 ms | 超时 4 s、1 s 喂一次 |
| 控制 / BLE / 监控三个任务全不执行 | Flash 读出的 `sample_period` 为 0，`vTaskDelay(0)` 只让一次 CPU | 加 200 ms 下限 |
| DHT22 返回旧值 | 默认采样 1000 ms，快于器件要求的 2 s 间隔 | 默认改 2000 ms |
| `board_ble_send` 一调用就解引用空指针 | `huart2` 只有 `extern` 声明，从未定义 | 定义在 `firmware/uart_ble.c`（含 PA2/PA3 AF7 配置） |
| 参数区边界检查无法编译 | `FLASH_SECTOR_SIZE` 全工程无定义 | 补上；F407VG 扇区不均匀（0-3 16 KB、4 64 KB、5-11 128 KB） |

## BLE 协议

服务与特征、帧格式、环境数据帧与控制指令帧的字段定义见
[`docs/BLE协议.md`](docs/BLE协议.md)。

## License

MIT
