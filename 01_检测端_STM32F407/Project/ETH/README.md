# ETH —— ST 官方以太网驱动（随库集成）

本目录存放 **ST 官方** Cortex-M4 以太网底层驱动（MAC / DMA / SMI / PHY 管理），
被 `FWLIB` 的 `sys_eth` 模块封装调用，供本模板库直接使用。

## 来源与版本

| 项目 | 说明 |
|---|---|
| 驱动名称 | STM32F4x7_ETH_Driver |
| 版本 | V1.1.0（2013-07-31 发布） |
| 来源 | ST 官方软件包 `STM32F4x7_ETH_LwIP_V1.1.0`（经 GitHub 镜像 `jackeyjiang/STM32F4x7_ETH_LwIP_V1.1.0` 获取） |
| 许可 | 文件头保留 ST 原始许可（MCD-ST Liberty SW License Agreement V2），仅限与 ST 芯片配套使用 |

## 目录结构（与 FreeRTOS/ 同款：src / inc / port）

```
ETH/
├── src/   stm32f4x7_eth.c                    驱动实现（保持官方原样）
├── inc/   stm32f4x7_eth.h                    驱动接口（保持官方原样）
└── port/  stm32f4x7_eth_conf.h               工程级配置（换 PHY / 换板改这里）
           stm32f4x7_eth_conf_template.h      ST 原版模板（仅参考，不参与编译）
```

## 文件说明

| 文件 | 说明 | 是否可改 |
|---|---|---|
| `src/stm32f4x7_eth.c` | 驱动实现（MAC 配置、描述符初始化、收发查询、SMI 读写） | 保持官方原样 |
| `inc/stm32f4x7_eth.h` | 驱动接口与类型定义 | 保持官方原样 |
| `port/stm32f4x7_eth_conf.h` | **工程级配置**：PHY 地址延时、LAN8720 链路判定宏（`PHY_SR` 三件套）、时钟范围 | 换 PHY / 换板时改这里 |
| `port/stm32f4x7_eth_conf_template.h` | ST 原版配置模板（参考对照用，不参与编译） | —— |

## 使用方式

- 业务代码**不要直接调用**本目录函数，统一走 `FWLIB` 封装：`sys_eth.h`（`SYS_ETH_Init / SendFrame / RecvFrame …`）；
- 需要高级功能（中断收包、协议栈移植）时再直接引用官方接口，接口用法见 `stm32f4x7_eth.h` 注释；
- 工程树按物理结构分三组（`ETH/src`、`ETH/inc`、`ETH/port`）；头文件搜索路径两条（`.\ETH\inc`、`.\ETH\port`）；复制本模板到新工程时**整个目录一起拷贝**；
- 除本 README 与 `stm32f4x7_eth_conf.h` 外，其余文件与官方包保持一致（工程**仅在这两个文件上做过定制**；升级时只需替换这两个文件）。

## 关键前提（换板必读）

| 前提 | 说明 |
|---|---|
| 参考时钟 | RMII 要求 **50MHz REF_CLK**（本板由 PHY 侧晶振提供、经 PA1 输入 MAC）——换板先确认 PHY 时钟方案 |
| RMII 模式选择 | `sys_eth.c` 初始化时写 SYSCFG->PMC 自动完成（硬件要求，无需手动） |
| 中断归属 | **驱动本身不含 `ETH_IRQHandler`**；本库为轮询版、不使能以太网中断；将来做"中断收包"时在 `sys_eth` 区块 3 加 ISR（归属规则见主 README 5.22） |
| 引脚复用警告 | PA2(MDIO) 与 USART2_TX 板级复用——网口与 USART2 二选一 |

## 本板已配好的关键项（`stm32f4x7_eth_conf.h`）

- PHY 型号：LAN8720（RMII，地址 0）
- 链路判定：`PHY_SR = 0x1F`（Special Modes Register）、`PHY_SPEED_STATUS = 0x0004`、`PHY_DUPLEX_STATUS = 0x0010`
- 增强 DMA 描述符：`USE_ENHANCED_DMA_DESCRIPTORS` 已开启（与 ST 官方 V1.1.0 一致）
- `PHY_ID1 / PHY_ID2` 宏为本工程补充（官方头文件未定义），`sys_eth.c` 用它做 PHY 存在性探测
