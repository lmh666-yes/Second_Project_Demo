#ifndef __FWLIB_SYS_DAC_H
#define __FWLIB_SYS_DAC_H

#include "stm32f4xx.h"

/* sys_dac.h: DAC 模拟输出（直写电压 / 内置波形 / DMA 任意波形）头文件
 * 依赖 StdPeriph 的 DAC + DMA + TIM6；工程 RTE 里必须勾选
 * StdPeriph Drivers → DAC，否则 stm32f4xx_dac.c 不参与编译
 *
 * 参考电压共用 VDDA/Vref+（本板 3.3V），输出电压 = 3.3V × 数值/4095，VDDA 抖动即输出抖动
 * 输出引脚必须先设成 GPIO_Mode_AN（模拟），数字推挽级会拉死模拟输出
 * 固定频率波形输出必须用外部触发（定时器 TRGO），DAC 自身不带定时
 *
 * 接线（普中-天马 F407 开发板）: DAC1 = PA4（网络名 STM_DAC，从 J8 排针引出）
 * J8 是共享模拟排针（R_ADC / STM_ADC / P_TOUCH / STM_DAC / TAD1），用 DAC 时跳线帽接 STM_DAC
 * PA5 是 DAC2，本板已被电容触摸 / STM_ADC 占用，DAC2 不可用
 * PA4 同时是摄像头接口的 DCMI_HREF，插上摄像头模块后 DAC1 不可用，两者二选一
 *
 * 移植: 换引脚改 SYS_DACx_PORT/PIN；换参考电压改 SYS_DAC_VREF_MV */


/* 定义与宏定义区（换板子只改这里） */
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
 * 本库其它模块若也占用 TIM6，改用 TIM7 并同步 SYS_DAC_TRGO_SOURCE 与
 * sys_dac.c 内的 TIM7 分支 */
#define SYS_DAC_TIM         TIM6
#define SYS_DAC_TIM_CLK     RCC_APB1Periph_TIM6
#define SYS_DAC_TRGO_SOURCE TIM_TRGOSource_Update

/* 正弦波查表点数：点数越多越平滑，占用 Flash 越多 */
#define SYS_DAC_SINE_POINTS 32U

/* DMA 通道（F4 固定映射：DAC1→DMA1_Stream5_CH7，DAC2→DMA1_Stream6_CH7） */
#define SYS_DAC1_DMA_STREAM DMA1_Stream5
#define SYS_DAC1_DMA_CHANNEL DMA_Channel_7
#define SYS_DAC1_DMA_IRQ    DMA1_Stream5_IRQn


/* 基础功能（直写电压） */
/* 初始化通道：开时钟 + 引脚设为模拟 + DAC 使能 + 输出缓冲
 * ch   : SYS_DAC_1（本板可用）/ SYS_DAC_2（默认被禁）
 * 返回 : 0 = 成功；1 = 参数非法或该通道在本板不可用 */
uint8_t SYS_DAC_Init(uint8_t ch);

/* 关闭通道：停止输出，引脚变回模拟高阻（悬空，不是 0V）
 * 该通道未 Init 过时重复调用无害（内部有状态判断） */
void SYS_DAC_Stop(uint8_t ch);

/* 该通道是否已经 Init（1 = 已初始化；没 Init 就 SetValue 会被忽略） */
uint8_t SYS_DAC_IsInited(uint8_t ch);

/* 该通道是否正在输出波形（三角/噪声/DMA 正弦中） */
uint8_t SYS_DAC_IsWaveOn(uint8_t ch);

/* 直接写数值（12 位，0 ~ 4095；0 = 0V，4095 = VREF）
 * 必须先 SYS_DAC_Init()，否则本次调用被忽略（防误写） */
void SYS_DAC_SetValue(uint8_t ch, uint16_t value);

/* 读回当前输出寄存器值（0 ~ 4095）
 * 这是最近一次写入的值，不是万用表测到的真实电压 */
uint16_t SYS_DAC_GetValue(uint8_t ch);

/* 按毫伏设置输出（超范围自动截断） */
void SYS_DAC_SetMilliVolt(uint8_t ch, uint32_t millivolt);

/* 数值与毫伏换算，供不用浮点的场合
 * 毫伏 = 数值 × VREF / 4095 */
uint32_t SYS_DAC_ValueToMilliVolt(uint16_t value);
uint16_t SYS_DAC_MilliVoltToValue(uint32_t millivolt);


/* 扩展功能（波形输出） */
/* 内置三角波：DAC 内部阶梯累加器硬件产生，每次触发加一个台阶，到幅度上限自动回落
 * ch        : 通道
 * freq_hz   : 三角波频率（1 ~ 100000）
 * amplitude : 幅度台阶数（1 ~ 4095，越大峰峰值越高）
 * 返回      : 0 = 成功
 * 输出频率  = TIM6 触发频率 / (2 × 台阶数) */
uint8_t SYS_DAC_TriangleInit(uint8_t ch, uint32_t freq_hz, uint16_t amplitude);

/* 内置噪声（伪随机）
 * ch          : 通道
 * freq_hz     : 触发频率，噪声本身是全带宽的
 * unmask_bits : 参与异或的位宽（0~11，越大噪声越密）
 * 返回        : 0 = 成功 */
uint8_t SYS_DAC_NoiseInit(uint8_t ch, uint32_t freq_hz, uint32_t unmask_bits);

/* DMA 播放任意波形（正弦 / 自定义序列，自动循环）
 * 实现: 开 DMA1_Stream5（DAC1 固定通道），TIM6 每来一次触发就从内存搬一个数进 DAC
 *       数据寄存器，循环模式（DMA_Mode_Circular）自动重复
 * buf/len : 波形数组（12 位数据，0~4095）与点数
 * freq_hz : 波形重复频率，内部换算 TIM6 触发频率 = freq × len
 * 返回    : 0 = 成功；1 = 参数非法；2 = 频率超出可实现范围 */
uint8_t SYS_DAC_DmaInit(uint8_t ch, const uint16_t *buf, uint16_t len, uint32_t freq_hz);
void    SYS_DAC_DmaStart(uint8_t ch);
void    SYS_DAC_DmaStop(uint8_t ch);

/* 正弦波输出（内部 32 点查表）
 * ch      : 通道
 * freq_hz : 正弦频率（1 ~ 100000）
 * 返回    : 0 = 成功 */
uint8_t SYS_DAC_SineInit(uint8_t ch, uint32_t freq_hz);

/* 停止一切波形输出，回到直写模式
 * 关 DMA、关触发、关波形发生器，但不改变当前输出电压；
 * 输出归 0 需再调 SYS_DAC_SetValue(ch, 0) */
void SYS_DAC_WaveStop(uint8_t ch);

/* 取正弦查表首地址（DMA 用户自行改表时用） */
const uint16_t *SYS_DAC_GetSineTable(void);

#endif /* __FWLIB_SYS_DAC_H */
