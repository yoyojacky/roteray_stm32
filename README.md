# roteray_stm32

STM32F103C8T6（Blue Pill）旋转编码器测试工程，裸机实现，不依赖 HAL/标准库。
转动编码器或按下旋钮时，通过串口实时输出方向、累计计数和按键状态。

## 功能

- 正交编码器解码：SysTick 每 1ms 扫描 + 状态转移表，消抖不丢步
- 按键（SW）20ms 消抖，按下/松开事件输出
- USART1 串口输出：115200 8N1
- 每 2 秒打印一次心跳 `[alive]`，用于确认板子和串口链路正常

## 硬件接线

### 旋转编码器（接 STM32）

| 编码器引脚 | STM32 引脚 | 说明 |
|-----------|-----------|------|
| CLK | PA6 | 内部上拉输入 |
| DT  | PA4 | 内部上拉输入 |
| SW  | PA7 | 内部上拉输入（按下接地） |
| GND | GND | 共地 |
| +   | 3.3V | 如编码器模块有 VCC 引脚 |

### ST-Link 烧录（SWD）

| ST-Link | STM32 |
|---------|-------|
| SWDIO | SWDIO (PA13) |
| SWCLK | SWCLK (PA14) |
| GND | GND |
| 3.3V | 3.3V |

### 串口（USB 转 TTL，如 CH340）

注意：ST-Link V2 克隆版**不带串口功能**，需要单独的 USB 转 TTL 模块。

| USB 转 TTL | STM32 |
|-----------|-------|
| RX | PA9 (USART1_TX) |
| TX | PA10 (USART1_RX) |
| GND | GND |

## 环境依赖

- `arm-none-eabi-gcc`（编译工具链）
- `make`
- `openocd`（配合 ST-Link 烧录）

Debian/Ubuntu/树莓派安装：

```bash
sudo apt install gcc-arm-none-eabi binutils-arm-none-eabi make openocd
```

## 编译与烧录

```bash
make          # 编译，输出 build/encoder_test.elf 和 .bin
make flash    # 编译 + 通过 ST-Link/OpenOCD 烧录并校验
make clean    # 清理 build 目录
```

## 查看串口输出

串口参数：**115200 / 8 数据位 / 无校验 / 1 停止位**

树莓派 / Linux（CH340 通常为 /dev/ttyUSB0）：

```bash
stty -F /dev/ttyUSB0 115200 raw -echo && cat /dev/ttyUSB0
# 或
minicom -D /dev/ttyUSB0 -b 115200
```

Windows：用串口助手（如 SSCOM、XCOM、PuTTY），选择 CH340 对应的 COM 口，波特率 115200。

## 输出示例

```
=============================
STM32F103 Encoder Test ready
CLK=PA6 DT=PA4 SW=PA7
UART: 115200 8N1 on PA9(TX)
=============================
[alive] count=0  SW=0
Encoder: CW   count=1
Encoder: CW   count=2
Encoder: CCW  count=1
Button: PRESSED
Button: RELEASED
[alive] count=1  SW=0
```

## 配置项

`src/main.c` 中可调整的宏：

| 宏 | 默认值 | 说明 |
|----|-------|------|
| `PIN_CLK` | 6 | CLK 引脚（PA6） |
| `PIN_DT`  | 4 | DT 引脚（PA4） |
| `PIN_SW`  | 7 | SW 引脚（PA7） |
| `ENCODER_REVERSE` | 0 | 方向反了改为 1，重新 `make flash` 即可 |

改心跳间隔：修改 `main` 中 `ms_ticks - last_hb >= 2000u` 的 2000（单位 ms），删除该 `if` 块可关闭心跳。

## 目录结构

```
├── Makefile                          # make / make flash / make clean
├── linker/STM32F103C8Tx_FLASH.ld     # 链接脚本（64K Flash / 20K RAM）
└── src/
    ├── startup_stm32f103c8tx.s       # 启动文件 + 中断向量表
    └── main.c                        # 时钟/GPIO/UART/编码器/按键逻辑
```

## License

见 [LICENSE](LICENSE)。
