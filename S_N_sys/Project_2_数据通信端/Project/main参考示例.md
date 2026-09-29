# 模板 `main.c` 参考（骨架 / 初始化顺序 / 模块调用）

> 位置：`01Project\000模板\main参考示例.md`
> 适用：`000模板` 以及**以后所有基于它新建的工程**

---

## 0. 现状：main.c 已精简为骨架

- **`000模板\main.c` 现在是精简骨架**（只留 include 列表 + 空 `main()`），
  **以后新建工程都从这个骨架开始写**。
- 旧的"全模块演示 main"**原文已不可考** —— 文件被覆盖，会话历史里也没留下完整副本
  （工作区全搜 + 14 个历史会话记录全扫过，只找到零散片段）。
  但它想做的事，**验证工程做得更全**，见 §2。

---

## 1. 当前模板骨架（标准写法）

```c
#include "stm32f4xx.h"      /* 芯片寄存器定义 */

/* -------------------- 板载外设 -------------------- */
#include "gpio_core.h"      /* 通用 GPIO 工具（引脚/电平/位带/精准延时） */
#include "led.h"            /* 板载 LED（DS0/DS1）          */
#include "key.h"            /* 板载按键（KEY0/1/2/KEY_UP）  */
#include "beep.h"           /* 板载蜂鸣器                    */
#include "lcd.h"            /* TFT-LCD（FSMC + ILI9481 320x480） */

int main(void)
{
    while (1) {

    }
}
```

> ⚠ `lcd.h` 的注释**应该是 `ILI9481 320×480`** —— 库早已从 ILI9341 换成 9481，
> 骨架里那行注释写的是旧的，注意改。

**用法**：新建工程 → 复制这份 `main.c` → 删掉用不到的 include → 在 `main()` 里按 §3 的顺序初始化。

---

## 2. 完整可运行示例 → `111综合验证工程\main.c`

那份是**40+ 模块的真实调用**，而且**已经在你的板子上实测全部通过**：

| 组 | 覆盖内容 |
|---|---|
| A | 时钟 168MHz / DWT 微秒延时 / SysTick / NVIC 分组 / 复位原因解码 |
| B | 片内 Flash / **外部 SRAM 1MB 全片读写** / I2C 扫描 / AT24C02 / MPU6050 / W25Q128 |
| C | DS18B20 / DHT11 / ADC / DAC / NTC-PT100 / **RTC** / **CAN 环回** |
| D | 数字滤波（中位/滑动/去极值/LPF）/ PID 闭环 |
| E | LED / 蜂鸣器 / 彩屏 / OLED / RGB 彩灯 / 步进 / 按键 / 触摸 / 红外 |

**想知道某个模块该怎么调** → 去那份文件里搜模块名，每个都有一小段完整可用的代码。

---

## 3. ★ 初始化顺序（有依赖，不能乱）

| 顺序规则 | 为什么 |
|---|---|
| **`SYS_NVIC_Init()` 放在最前** | 用到任何中断之前必须先设好优先级分组 |
| `SYS_TICK_Init()` 要早于依赖它的代码 | 它按 `SystemCoreClock` 算重装值 |
| **`EXT_IO_Init()` 必须在所有 `EXT_XXX_Init()` 之前** | 打底会覆盖旧配置：先打底、后个性化覆盖 |
| **切换时钟（`SYS_CLK_Switch`）之后必须重新 `SYS_TICK_Init()`** | 主频变了，毫秒节拍要重算 |
| 各模块 `Init` 之间互相独立 | `LED_Init` / `KEY_Init` / `BEEP_Init` 无先后要求 |
| 没用到的模块不要调 | 不关心精确延时可跳过 `SYS_TICK_Init()` |

### 推荐的最小启动序列（可直接抄）

```c
int main(void)
{
    SYS_NVIC_Init();                        /* ① 优先级分组（最先） */
    SYS_TICK_Init();                        /* ② 毫秒节拍 */
    SYS_USART_Init(SYS_USART_1, 115200);    /* ③ 串口（USB2 / CH340C） */
    SYS_USART_InitRxIT(SYS_USART_1, 115200);
    SYS_USART_SendLine(SYS_USART_1, "ready");

    LED_Init();                             /* ④ 业务外设，互相独立 */
    KEY_Init();
    BEEP_Init();
    EXT_IO_Init();                          /* ★ 必须在 EXT_XXX_Init() 之前 */

    for (;;) { }
}
```

> ⚠ **两枚跳线帽**：板上 `PA9T`↔`URXD`、`PA10R`↔`UTXD`，不插串口一个字都收不到。

---

## 4. 各模块调用样例（按需查）

### 4.1 板载外设

```c
LED_Init();                     LED_On(0);  LED_Toggle(1);  LED_AllOff();
KEY_Init();                     if (KEY_Scan() != KEY_NONE) { ... }
BEEP_Init();                    BEEP_Beep(2);  BEEP_SOS();
EXT_IO_Init();                  if (EXT_IR_Detected(0)) { ... }   /* 红外 */
LCD_Init();                     LCD_Clear(LCD_COLOR_BLUE);  LCD_ShowString(10,10,"hi",...);
```

### 4.2 系统服务

```c
SYS_NVIC_Init();                              /* 优先级分组 */
SYS_TICK_Init();                              /* 毫秒节拍 */
SYS_USART_Init(SYS_USART_1, 115200);          /* 串口（1=USB2/3=WIFI） */
SYS_I2C_Init(SYS_I2C_1, 400000);              /* I2C（AT24C02/MPU6050 都在 I2C1） */
SYS_SPI_Init(SYS_SPI_1, 1000000, SYS_SPI_MODE_0);   /* SPI1 = PB3/PB4/PB5 */
SYS_ADC_ReadAvg(ADC3, ADC_Channel_5, 8);      /* ADC（PA5 = STM_DAC 侧） */
SYS_DAC_Init(SYS_DAC_1);                      /* DAC（PA4） */
SYS_TIM_PwmInit(SYS_TIM_5, 1, GPIOA, GPIO_Pin_0, GPIO_AF_TIM5, 1000);
SYS_EXTI_InitLine(13, GPIOC, GPIO_Pin_13, ..., 回调);
SYS_CAN_Init(SYS_CAN_1, 500000);              /* CAN1（PA11/PA12 + TJA1050） */
SYS_RTC_Init();                               /* RTC（LSE + CR1220 电池座） */
SYS_WDG_Init(1000);                           /* 看门狗 1s */
```

### 4.3 器件驱动

```c
AT24C02_Init(100000);                AT24C02_WriteByte(0x00, 0x5A);
MPU6050_Init();                      MPU6050_Read(&d);      /* 温度见 4.4 */
W25QXX_Init(1000000);                W25QXX_Read(buf, addr, len);   /* 板上是 GD25Q128 */
DS18B20_Init();                      DS18B20_ReadTempC10(&t);      /* J6 座，PG9 */
DHT11_ReadInt(&t, &h);                                             /* J6 座，同一根 PG9 */
OLED_Init();                         OLED_ShowString(0,0,"OK",0);  OLED_Refresh();
XPT2046_Init();                      XPT2046_GetEvent(&x,&y);      /* J8 需短接 P_TOUCH */
NTC_PT100_Init();                    SENSOR_ReadTempC(...);        /* J8 需短接 TAD1 */
SRAM_Init();                         SRAM_Test(...);               /* 1MB @0x68000000 */
ULN2003_Init();                      ULN2003_Step(n, 1);           /* CN2 要飞线到 PC1~PC4 */
RGB5X5_Init();                       RGB5X5_Fill(0,60,0); RGB5X5_Show(); /* CN5 2-3 短接 + 飞线 PC5 */
```

### 4.4 ★ 几个容易搞错返回值极性的 API

| API | 返回 |
|---|---|
| `AT24C02_*` / `MPU6050_Init` / `W25QXX_Init` | **0 = 成功** |
| `DS18B20_Init()` | **1 = 有器件应答（好）**，0 = 无应答 |
| `MPU6050_WhoAmI()` | **WHO_AM_I 寄存器的原始值**（MPU6050 = `0x68`），不是 1/0 |
| `SYS_RTC_Init()` | 0 = 时间还在 · 1 = 首次上电已初始化 · 2 = 失败 |
| `SYS_I2C_*` | 0 = OK，负数错误码（`SYS_I2C_ErrStr` 转文字） |

### 4.5 通信 / 算法

```c
SYS_RS485_Init(115200);      MODBUS_Init(1, 115200);   /* 需 P5 跳线到 485 */
ESP8266_Init(115200);                                  /* 需 P10 跳线到 WIFI */
PID_Init(&pid, 2.0f, 8.0f, 0.0f);   PID_Update(&pid, sp, meas, dt);
FILTER_Median(buf, n);       FILTER_LpfInit(&f, 10.0f, 100.0f);
SYS_ENCODER_Init(...);
```

---

## 5. 换到新板子时要改什么

| 要改 | 在哪 |
|---|---|
| **HSE 晶振频率** | `RTE/Device/STM32F407ZG/system_stm32f4xx.c` 的 **`PLL_M`**，同时确认工程的 `HSE_VALUE` 宏 |
| 引脚分配 | 各模块 `.h` 顶部的"接线"宏 |
| 外设编号 | `SYS_USART_x` / `SYS_I2C_x` / `SYS_SPI_x` 等 |
| FSMC 时序 | `lcd.h` / `sram.h` 的 `*_DATA_SETUP`（单位是 HCLK 周期，**换主频要重算**） |

> ⚠ `PLL_M` 这个坑踩过：ST 模板默认 `PLL_M = 25`（给 25MHz 晶振的板子），
> 本板是 **8MHz**，不改的话主频只有 `8/25×336/2 = 53.76MHz`，
> 所有基于 `SystemCoreClock` 的延时、I2C、1-Wire 全都会莫名其妙地坏掉。
