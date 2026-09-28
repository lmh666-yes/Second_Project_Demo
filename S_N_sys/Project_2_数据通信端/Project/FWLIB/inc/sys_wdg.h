#ifndef __FWLIB_SYS_WDG_H
#define __FWLIB_SYS_WDG_H

#include "stm32f4xx.h"

/* ================================================================
 *  sys_wdg.h —— 【系统】看门狗模块  头文件
 * ================================================================
 *  设计定位 : 程序"跑飞卡死"时的自动复位保险——
 *             IWDG 独立看门狗（基础）+ WWDG 窗口看门狗（区块 3）
 *  标准库关键词 : IWDG_WriteAccessCmd / IWDG_SetPrescaler / IWDG_SetReload /
 *                 IWDG_ReloadCounter / IWDG_Enable / WWDG_SetPrescaler /
 *                 WWDG_Enable / RCC_GetFlagStatus（复位原因）
 *
 *  IWDG 特点 :
 *   ① 时钟源 = LSI 内部低速 RC（约 32kHz），与主频无关——
 *      就算主时钟配置跑飞，看门狗照样工作，是最可靠的保险；
 *   ② 一旦启动就无法停止（除非复位），这是官方特性；
 *   ③ 调试提示 : 本模块初始化时自动开启"调试暂停冻结"，
 *      在 Keil 里打断点、单步时看门狗不计数，不会把板子复位掉；
 *   ④ 超时换算按 LSI 典型值 32kHz——LSI 实际频率有偏差
 *      （约 17k~47kHz），对时间精度要求高时可实测后改宏。
 *
 *  使用方式 :
 *      SYS_WDG_Init(2000);              // 2 秒超时，启动即看门
 *      while (1) {
 *          关键任务们...;
 *          SYS_WDG_Feed();             // 所有关键任务都跑到了才喂狗
 *      }
 *  ⚠ 喂狗位置：不要每个任务各喂各的——一个任务卡死、别的任务
 *    还照常喂狗，看门狗就失去意义了。
 *
 *  与其它模块的联动（注意）:
 *   - 与 sys_pwr：Stop 模式下 LSI 继续运行、看门狗继续计数——
 *     休眠时间超过看门狗超时会直接复位！休眠前要评估超时，
 *     或唤醒后立即喂狗 / 使用更长的超时；
 *   - 与 sys_clock：WWDG 超时随 PCLK1 变化，切换主频后需重新
 *     SYS_WDG_WwdgInit()；IWDG 用 LSI，不受切换影响；
 *   - 与 sys_fault：故障捕获 + 看门狗复位组合使用效果最好，
 *     重启后用 SYS_WDG_ResetCause() 判断是否被看门狗救回来。
 * ================================================================ */


/* ================================================================
 *                    区块 1：定义与宏定义区
 * ================================================================ */
/* LSI 频率（Hz）：用于超时换算。芯片手册典型值 32kHz；
 * 实测偏差大时可改成实测值（一般落在 17k ~ 47k 之间） */
#define SYS_WDG_LSI_HZ          32000UL

/* 1 = 调试器暂停 CPU 时看门狗冻结（推荐保持；0 = 不冻结） */
#define SYS_WDG_DEBUG_FREEZE    1

/* 超时允许范围（ms）：超出自动截断到边界 */
#define SYS_WDG_MIN_TIMEOUT_MS  1UL
#define SYS_WDG_MAX_TIMEOUT_MS  32768UL    /* 最大 ≈ 256×4096/32kHz ≈ 32.8s */

/* 复位原因标志（SYS_WDG_ResetCause 返回值，按位组合） */
#define SYS_WDG_RST_POR     (1UL << 0)     /* 上电复位          */
#define SYS_WDG_RST_PIN     (1UL << 1)     /* 复位引脚复位      */
#define SYS_WDG_RST_BOR     (1UL << 2)     /* 欠压复位          */
#define SYS_WDG_RST_SOFT    (1UL << 3)     /* 软件复位          */
#define SYS_WDG_RST_IWDG    (1UL << 4)     /* 独立看门狗复位 ★  */
#define SYS_WDG_RST_WWDG    (1UL << 5)     /* 窗口看门狗复位    */
#define SYS_WDG_RST_LPWR    (1UL << 6)     /* 低功耗模式复位    */


/* ================================================================
 *                    区块 2：基础功能
 * ================================================================ */
/* 启动独立看门狗 IWDG：timeout_ms 毫秒内必须喂一次狗，否则复位
 * 参数 : timeout_ms —— 超时时间（1 ~ 32768ms，超范围自动截断）
 * 说明 : 内部自动换算分频与重载值；启动后无法关闭（官方特性）
 * 标准库 : IWDG_WriteAccessCmd + IWDG_SetPrescaler + IWDG_SetReload
 *          + IWDG_ReloadCounter + IWDG_Enable
 * 示例 : SYS_WDG_Init(2000);      // 2 秒超时；主循环里定期 SYS_WDG_Feed() */
void SYS_WDG_Init(uint32_t timeout_ms);

/* 喂狗（复位 IWDG 计数，必须在超时前重复调用）
 * 标准库 : IWDG_ReloadCounter
 * 示例 : while (1) { 关键任务们(); SYS_WDG_Feed(); } */
void SYS_WDG_Feed(void);


/* ================================================================
 *                    区块 3：扩展功能
 * ================================================================ */
/* ---- 复位原因诊断（与 sys_fault 配合，判断"刚才是怎么重启的"）---- */

/* 读取上次复位原因（返回 SYS_WDG_RST_xxx 的位组合）
 * 标准库 : RCC_GetFlagStatus（逐个查 RCC_FLAG_xxxRST） */
uint32_t SYS_WDG_ResetCause(void);

/* 清除复位原因标志（读完判定后再调用，避免影响下次判断）
 * 标准库 : RCC_ClearFlag */
void SYS_WDG_ClearResetFlags(void);

/* 把复位原因解码成短文字（纯 ASCII），如 "POR" / "IWDG" / "SOFT,PIN"
 * 返回 : 实际写入长度（不含结尾 '\0'）；缓冲区不足自动截断
 * 示例 : char line[32];
 *        SYS_WDG_ResetCauseDecode(SYS_WDG_ResetCause(), line, sizeof(line)); */
uint32_t SYS_WDG_ResetCauseDecode(uint32_t cause, char *buf, uint32_t size);

/* ---- 窗口看门狗 WWDG（需在 Keil RTE 勾选 WWDG 组件）----
 * 比 IWDG 更严格：喂太早（计数器还在窗口上方）或喂太晚都复位，
 * 适合检测"卡死前的异常快循环"。注意超时随 PCLK1 变化 */

/* 启动 WWDG：timeout_ms 为"从启动到复位的允许时间窗上限"
 * 说明 : 越界自动截断；窗口默认全开（任何时刻都能喂），
 *        需要严格窗口时可自行调用 WWDG_SetWindowValue 收紧
 * 标准库 : RCC_APB1PeriphClockCmd(WWDG) + RCC_GetClocksFreq +
 *          WWDG_SetPrescaler + WWDG_SetWindowValue + WWDG_Enable */
void SYS_WDG_WwdgInit(uint32_t timeout_ms);

/* 喂 WWDG（保持与初始化相同的计数值）
 * 标准库 : WWDG_SetCounter */
void SYS_WDG_WwdgFeed(void);

#endif /* __FWLIB_SYS_WDG_H */
