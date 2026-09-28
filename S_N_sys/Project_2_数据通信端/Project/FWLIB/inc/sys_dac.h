#ifndef __FWLIB_SYS_DAC_H
#define __FWLIB_SYS_DAC_H

#include "stm32f4xx.h"

/* ================================================================
 *  sys_dac.h —— 【系统】DAC 模拟输出（含内置波形 / DMA 任意波形）  头文件
 * ================================================================
 *  设计定位 : StdPeriph DAC 的薄封装，覆盖三类用法：
 *             ① 直写电压   —— 按键调压、基准源、给外部电路一个可调电平
 *             ② 内置波形   —— 三角波 / 噪声（硬件自己产生，几乎不耗 CPU）
 *             ③ DMA 任意波形 —— 正弦/任意序列自动循环播放
 *  依赖     : StdPeriph 的 DAC + DMA + TIM6（**工程 RTE 里必须勾上
 *             StdPeriph Drivers → DAC**，否则 stm32f4xx_dac.c 不会被编译）
 *  标准库关键词 : RCC_APB1PeriphClockCmd(DAC) / DAC_Init / DAC_Cmd /
 *                 DAC_SetChannel1Data / DAC_GetDataOutputValue /
 *                 DAC_Trigger_T6_TRGO / DAC_WaveGeneration_Triangle /
 *                 DAC_Noise_Init / DAC_Triangle_Init / DAC_DMACmd /
 *                 DMA_Init / TIM_TimeBaseInit(TIM6) / TIM_SelectOutputTrigger
 *
 *  【DAC 的三个反直觉点（面试常问）】
 *    ① 参考电压共用 **VDDA/Vref+**（本板 3.3V），不是独立基准。
 *       所以"输出电压 = 3.3V × 数值/4095"，且 VDDA 抖动 = 输出抖动；
 *    ② 输出必须先把引脚设成 **GPIO_Mode_AN（模拟）**，
 *       否则数字驱动的推挽级会把 DAC 的模拟输出"拉死"；
 *    ③ 想让它按固定频率输出波形，必须**外部触发**（定时器 TRGO），
 *       DAC 自己不会定时——这是初学者最常卡住的地方。
 *
 *  【接线（普中-天马 F407开发板）】
 *      DAC1 = PA4（网络名 STM_DAC，从 J8 排针引出）
 *      ⚠ PA5 是 DAC2，但本板 PA5 已被"电容触摸 / STM_ADC"占用 → **DAC2 不可用**
 *      ⚠ J8 是共享模拟排针（5 个脚：R_ADC / STM_ADC / P_TOUCH / STM_DAC / TAD1），
 *        用 DAC 时把跳线帽插到 STM_DAC 那一路即可（PA4 本身是独立引脚）
 *      ⚠ **PA4 同时是摄像头接口的 DCMI_HREF**（原理图上这两根线同标
 *        `STM_DAC` 与 `DCMI_HREF`）→ 插上摄像头模块后 DAC1 就没法用了，二选一。
 *
 *  【使用方式】
 *      SYS_DAC_Init(SYS_DAC_1);                    // ① 初始化（PA4）
 *      SYS_DAC_SetMilliVolt(SYS_DAC_1, 1650);      // ② 输出 1.65V
 *      SYS_DAC_SineInit(SYS_DAC_1, 1000);          // ③ 或直接输出 1kHz 正弦
 *      ...
 *      SYS_DAC_WaveStop(SYS_DAC_1);                // ④ 停
 *
 *  移植指引 : 换引脚改 SYS_DACx_PORT/PIN；换参考电压改 SYS_DAC_VREF_MV。
 * ================================================================ */


/* ================================================================
 *                    区块 1：定义与宏定义区（换板子只改这里）
 * ================================================================ */
#define SYS_DAC_VREF_MV     3300UL      /* 参考电压（本板 VDDA = 3.3V） */

/* 通道编号（对外用 1 / 2，内部映射到标准库的 DAC_Channel_x） */
#define SYS_DAC_1   1U
#define SYS_DAC_2   2U
#define SYS_DAC_COUNT   2U

/* 通道 → 引脚 */
#define SYS_DAC1_PORT   GPIOA
#define SYS_DAC1_PIN    GPIO_Pin_4      /* 网络名 STM_DAC */
#define SYS_DAC2_PORT   GPIOA
#define SYS_DAC2_PIN    GPIO_Pin_5      /* 本板被 触摸/ADC 占用，默认不用 */

/* 本板只有 DAC1 可用：置 1 才允许初始化 DAC2（PA5 需先放弃触摸/ADC 功能） */
#define SYS_DAC2_ENABLE 0

/* 触发 DAC 转换的定时器（内置波形 / DMA 波形都用它当节拍）
 * ⚠ 本库其它模块若也用了 TIM6，请改成别的基本定时器（TIM7）并同步
 *   SYS_DAC_TRGO_SOURCE 与 sys_dac.c 内的 TIM7 分支 */
#define SYS_DAC_TIM         TIM6
#define SYS_DAC_TIM_CLK     RCC_APB1Periph_TIM6
#define SYS_DAC_TRGO_SOURCE TIM_TRGOSource_Update

/* 正弦波查表点数（越多越平滑、占用 Flash 越多；32 点已够用） */
#define SYS_DAC_SINE_POINTS 32U

/* DMA 通道（F4 固定映射：DAC1→DMA1_Stream5_CH7，DAC2→DMA1_Stream6_CH7） */
#define SYS_DAC1_DMA_STREAM DMA1_Stream5
#define SYS_DAC1_DMA_CHANNEL DMA_Channel_7
#define SYS_DAC1_DMA_IRQ    DMA1_Stream5_IRQn


/* ================================================================
 *                    区块 2：基础功能（直写电压）
 * ================================================================ */
/* 初始化通道：开时钟 + 引脚设为模拟 + DAC 使能 + 输出缓冲
 * 参数 : ch —— SYS_DAC_1（本板可用）/ SYS_DAC_2（默认被禁）
 * 返回 : 0 = 成功；1 = 参数非法或该通道在本板不可用
 * 示例 : SYS_DAC_Init(SYS_DAC_1); */
uint8_t SYS_DAC_Init(uint8_t ch);

/* 关闭通道（停止输出，引脚变回模拟高阻——注意：不是 0V，是悬空）
 * 说明 : 若该通道本来就没 Init 过，重复调用也无害（内部有状态判断） */
void SYS_DAC_Stop(uint8_t ch);

/* 该通道是否已经 Init（1 = 已初始化；没 Init 就 SetValue 会被忽略） */
uint8_t SYS_DAC_IsInited(uint8_t ch);

/* 该通道是否正在输出波形（三角/噪声/DMA 正弦中） */
uint8_t SYS_DAC_IsWaveOn(uint8_t ch);

/* 直接写数值（12 位，0 ~ 4095；0 = 0V，4095 = VREF）
 * 注意 : 必须先 SYS_DAC_Init()，否则本次调用被忽略（防误写）
 * 示例 : SYS_DAC_SetValue(SYS_DAC_1, 2048);   // 约 1.65V */
void SYS_DAC_SetValue(uint8_t ch, uint16_t value);

/* 读回当前输出寄存器值（0 ~ 4095）
 * 注意 : 这是"最近一次写入的值"，不是万用表测到的真实电压 */
uint16_t SYS_DAC_GetValue(uint8_t ch);

/* 按毫伏设置输出（超范围自动截断）
 * 示例 : SYS_DAC_SetMilliVolt(SYS_DAC_1, 3300);   // 满量程 */
void SYS_DAC_SetMilliVolt(uint8_t ch, uint32_t millivolt);

/* 数值 ↔ 毫伏 换算（不想用浮点时自己算）
 * 毫伏 = 数值 × VREF / 4095 */
uint32_t SYS_DAC_ValueToMilliVolt(uint16_t value);
uint16_t SYS_DAC_MilliVoltToValue(uint32_t millivolt);


/* ================================================================
 *                    区块 3：扩展功能（波形输出）
 * ================================================================ */
/* ---- ③ 内置三角波（硬件产生，不占 CPU、不占 DMA）----
 * 说明 : DAC 内部有一个"阶梯累加器"，每次触发加一个台阶，加到幅度上限
 *        自动回落——纯硬件，所以 CPU 可以完全不管
 * 参数 : ch —— 通道；freq_hz —— 三角波频率（1 ~ 100000）
 *        amplitude —— 幅度台阶数（1 ~ 4095，越大峰峰值越高）
 * 返回 : 0 = 成功
 * 示例 : SYS_DAC_TriangleInit(SYS_DAC_1, 1000, 4095);   // 1kHz 满幅三角波
 * 提醒 : 三角波频率 = 触发频率 / (2 × (amplitude+1) × 4096/4096)，
 *        实际输出频率 = TIM6 触发频率 ÷ (2 × 台阶数) */
uint8_t SYS_DAC_TriangleInit(uint8_t ch, uint32_t freq_hz, uint16_t amplitude);

/* ---- ④ 内置噪声（伪随机，做"白噪声源/随机数"很好看）----
 * 参数 : ch —— 通道；freq_hz —— 触发频率（噪声本身是全带宽的）
 *        unmask_bits —— 参与异或的位宽（0~11，越大噪声越"密"）
 * 返回 : 0 = 成功
 * 示例 : SYS_DAC_NoiseInit(SYS_DAC_1, 1000000, DAC_LFSRUnmask_Bits8_0); */
uint8_t SYS_DAC_NoiseInit(uint8_t ch, uint32_t freq_hz, uint32_t unmask_bits);

/* ---- ⑤ DMA 播放任意波形（正弦 / 自定义序列，自动循环）----
 * 内部实现 : 开 DMA1_Stream5（DAC1 固定通道）→ TIM6 触发 → 每来一次触发
 *            就从内存搬一个数进 DAC 数据寄存器 → 循环模式（DMA_Mode_Circular）自动重复
 * 参数 : buf/len —— 波形数组（12 位数据，0~4095）与点数
 *        freq_hz —— **波形重复频率**：内部自动换算 TIM6 触发频率 = freq×len
 * 返回 : 0 = 成功；1 = 参数非法；2 = 频率超出可实现范围
 * 示例 : uint16_t w[64]; ...填 0~4095...; SYS_DAC_DmaInit(SYS_DAC_1, w, 64, 1000);
 *        SYS_DAC_DmaStart(SYS_DAC_1); */
uint8_t SYS_DAC_DmaInit(uint8_t ch, const uint16_t *buf, uint16_t len, uint32_t freq_hz);
void    SYS_DAC_DmaStart(uint8_t ch);
void    SYS_DAC_DmaStop(uint8_t ch);

/* ---- ⑥ 正弦波一键输出（内部自带 32 点查表）----
 * 参数 : ch —— 通道；freq_hz —— 正弦频率（1 ~ 100000）
 * 返回 : 0 = 成功
 * 示例 : SYS_DAC_SineInit(SYS_DAC_1, 1000);   // 1kHz 正弦（峰峰 0~3.3V） */
uint8_t SYS_DAC_SineInit(uint8_t ch, uint32_t freq_hz);

/* ---- 停止一切波形输出，回到"直写"模式 ----
 * 说明 : 关 DMA、关触发、关波形发生器，但**不改变当前输出电压**；
 *        想让输出归 0 再调 SYS_DAC_SetValue(ch, 0) */
void SYS_DAC_WaveStop(uint8_t ch);

/* 取正弦查表首地址（DMA 用户想自己改表时用） */
const uint16_t *SYS_DAC_GetSineTable(void);

#endif /* __FWLIB_SYS_DAC_H */
