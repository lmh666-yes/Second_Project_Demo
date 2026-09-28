#ifndef __STM32F4x7_ETH_CONF_H
#define __STM32F4x7_ETH_CONF_H

/* ================================================================
 *  stm32f4x7_eth_conf.h —— ST 官方以太网驱动配置（"标准模板"工程专用）
 * ================================================================
 *  由官方模板 stm32f4x7_eth_conf_template.h 裁剪而来，改动说明：
 *    ① 保留"增强型 DMA 描述符"（带时间戳/IPv4 校验字段，与官方
 *       示例一致，避免与库代码路径分叉）；
 *    ② 不启用 USE_Delay —— 使用驱动内置 _eth_delay_ 循环延时，
 *       不依赖任何 user 头（main.h）；
 *    ③ PHY 目标改为本板实际器件 **LAN8720**：链路状态判定所用的
 *       三个宏（PHY_SR / PHY_SPEED_STATUS / PHY_DUPLEX_STATUS）
 *       按 LAN8720 的"特殊模式寄存器(0x1F)"定义；
 *    ④ 补充 PHY_ID1 / PHY_ID2（IEEE 标准寄存器 0x02/0x03）——
 *       官方头文件未定义，sys_eth 用它做 PHY 存在性探测；
 *    ⑤ 移除模板中面向 DP83848 的 PHY 中断宏（防误导，见文末注释）。
 *
 *  与库的关系 : 本文件被 stm32f4x7_eth.h 包含（该驱动是"标准外设
 *  库时代"的独立组件，经 sys_eth 模块薄封装后对外提供统一接口）。
 * ================================================================ */

#include "stm32f4xx.h"

/* -------------------- 驱动缓冲区（默认即可） --------------------
 * 不定义 CUSTOM_DRIVER_BUFFERS_CONFIG：使用驱动默认
 * 5 个接收缓冲 + 5 个发送缓冲、每缓冲 ETH_MAX_PACKET_SIZE 字节 */

/* -------------------- 增强型描述符 -------------------- */
/* 与官方示例保持一致（时间戳字段在本基础版中不使用，仅占位） */
#define USE_ENHANCED_DMA_DESCRIPTORS

/* -------------------- 延时策略 -------------------- */
/* 未启用 USE_Delay：_eth_delay_ 指向驱动内置循环延时函数 ETH_Delay
 * （用于 PHY 复位等待、自协商轮询间隔、寄存器写间隔——精度要求不高） */
#define _eth_delay_    ETH_Delay

/* PHY 复位 / 配置等待（沿用官方默认量级） */
#define PHY_RESET_DELAY      ((uint32_t)0x000FFFFF)
#define PHY_CONFIG_DELAY     ((uint32_t)0x00FFFFFF)
#define ETH_REG_WRITE_DELAY  ((uint32_t)0x0000FFFF)

/* -------------------- PHY = LAN8720A --------------------
 * ETH_Init 在"自协商完成后"读 PHY_SR 判断速度/双工，必须与真实
 * PHY 匹配（LAN8720 特殊模式寄存器 0x1F）：
 *   bit2 = 速度： 1 = 100Mbps
 *   bit4 = 双工： 1 = 全双工 */
#define PHY_SR               ((uint16_t)0x1F)
#define PHY_SPEED_STATUS     ((uint16_t)0x0004)
#define PHY_DUPLEX_STATUS    ((uint16_t)0x0010)

/* -------------------- PHY 标识寄存器（库头未定义，按标准补充） -------------------- */
/* IEEE 802.3 标准寄存器 0x02/0x03；sys_eth 初始化用它探测 PHY 是否响应
 * （读回全 0x00 / 全 0xFF = SMI 不通：查供电/复位脚/MDC-MDIO 接线） */
#define PHY_ID1              ((uint16_t)0x02)
#define PHY_ID2              ((uint16_t)0x03)

/* -------------------- PHY 中断相关（扩展预留，本基础版不启用） --------------------
 * ⚠ 不要照抄 DP83848 的寄存器定义（ST 模板面向的是 DP83848）！
 *   LAN8720 的中断寄存器是 0x1D（Interrupt Source）/ 0x1E（Interrupt Mask）；
 *   以后做"链路中断唤醒 / 中断收包"功能时，再按 LAN8720 数据手册
 *   逐位核对后再定义宏。 */

#endif /* __STM32F4x7_ETH_CONF_H */
