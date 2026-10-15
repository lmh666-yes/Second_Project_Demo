# -*- coding: utf-8 -*-
"""gen_doc.py —— 从 Keil DFP 的 SPL 源码/头文件生成 data_doc.js

解决三个问题：
  1) 自动收录的函数没有「参数说明」→ 从 .h 取权威签名 + 从 .c 取 @param 官方注释
  2) 参数只有"填什么值"没有"是什么" → 参数名中文字典（常见参数名覆盖率高）+ 英文描述中文化
  3) 返回值说明完全缺失 → 从 @retval 取，并做中文归类

数据来源（零手抄误差）：
  inc/stm32f4xx_*.h  函数原型（返回类型 / 参数名与类型）
  src/stm32f4xx_*.c  Doxygen @brief / @param / @retval

用法：python gen_doc.py
输出：data_doc.js（const STD_DOC = {...}）
"""
import os, re, io, sys, glob, collections
sys.stdout.reconfigure(encoding='utf-8')

DFP = r'E:\k5_v5\ARM\PACK\Keil\STM32F4xx_DFP\1.0.8\Device\StdPeriph_Driver'
SRC = os.path.join(DFP, 'src')
INC = os.path.join(DFP, 'inc')
HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, 'data_doc.js')

# ============================================================
#  1. 参数名 → 中文解释（"这个参数是什么、为什么"）
#     覆盖率：TOP 120 参数名约占全部出现的 76%
# ============================================================
PARAM_CN = {
    # ---- 通用开关 ----
    'NewState':   '开关状态：ENABLE = 使能，DISABLE = 关闭。',
    'Newstate':   '同 NewState：ENABLE 打开 / DISABLE 关闭。',
    # ---- 外设实例 ----
    'TIMx':       (
        '定时器，'
        '填 TIM1 ~ TIM14。'
        'TIM1/TIM8 为高级定时器（互补输出、'
        '刹车输入）'
        '；'
        'TIM2~TIM5 为 32 位，'
        '其余 16 位；'
        'TIM2~7/12~14 挂 APB1，'
        'TIM1/8~11 挂 APB2。'),
    'ADCx':       'ADC，填 ADC1 / ADC2 / ADC3。公共配置由 ADC_CommonInit 设置；ADC1 常配合 DMA2_Stream0。',
    'I2Cx':       'I2C 总线，填 I2C1 / I2C2 / I2C3。三条总线独立。',
    'USARTx':     '串口，填 USART1 ~ USART6 / UART4 / UART5 / UART7 / UART8。USART1/6 挂 APB2，其余挂 APB1。',
    'SPIx':       'SPI，填 SPI1 / SPI2 / SPI3。SPI1 挂 APB2，SPI2/3 挂 APB1。',
    'CANx':       'CAN，填 CAN1 / CAN2。用 CAN2 须同时使能 CAN1 时钟。',
    'GPIOx':      'GPIO 端口，填 GPIOA ~ GPIOK（F407 实际到 GPIOI 或 GPIOG，取决于封装）。',
    'DMAy_Streamx': 'DMA 数据流，填 DMA1_Stream0 ~ DMA1_Stream7 / DMA2_Stream0 ~ DMA2_Stream7。数据流与通道为固定映射，须查「DMA 请求映射表」。',
    'DMA_Streamx': '同 DMAy_Streamx：目标 DMA 数据流（如 DMA2_Stream5）。要查映射表确认。',
    'DAC_Channel': 'DAC 通道，填 DAC_Channel_1 / DAC_Channel_2。',
    'SAI_Block_x': 'SAI 子模块，填 SAI_Block_A / SAI_Block_B。',
    'FMC_Bank':   'FMC 存储区编号，填 FMC_Bank1 ~ FMC_Bank3 等。',
    'FSMC_Bank':  'FSMC 存储区编号，填 FSMC_Bank1 ~ FSMC_Bank3。F4 上 FMC 与 FSMC 为同一套外设。',
    'LTDC_layerx': 'LTDC 图层，填 LTDC_Layer1 / LTDC_Layer2。',
    'None':       '该函数无参数（原型里写 void）。',
    # ---- 引脚 ----
    'GPIO_Pin':   '要操作的引脚，填 GPIO_Pin_0 ~ GPIO_Pin_15，可用按位或组合多个（如 GPIO_Pin_9 | GPIO_Pin_10）。',
    'GPIO_PinSource': '引脚编号 0 ~ 15，填 GPIO_PinSource0 ~ GPIO_PinSource15，用于 GPIO_PinAFConfig。不是 GPIO_Pin_N 位掩码。',
    'GPIO_AF':    '复用功能编号，填 GPIO_AF_xxx（如 GPIO_AF_USART1 / GPIO_AF_TIM3）。查「复用功能表」确认某引脚支持哪个功能。',
    'GPIO_Alternate': '复用功能编号（同上，用于部分型号的第二参数写法）。',
    'GPIO_PortSource': 'EXTI 的端口源，填 EXTI_PortSourceGPIOA ~ GPIOx。表示"用哪个端口"。',
    'EXTI_PinSource':  'EXTI 的引脚源，填 EXTI_PinSource0 ~ EXTI_PinSource15。表示"用哪一号脚"。',
    # ---- 数据/缓冲 ----
    'Data':       '要发送或写入的数据，宽度见函数原型（8/16/32 位）。',
    'Address':    '地址。Flash 操作填绝对地址（如 0x08060000）；I2C/SPI 填从机内部寄存器或存储地址。',
    'Buffer':     '数据缓冲区指针（数组首地址）。函数不做边界检查，缓冲区须足够大。',
    'pBuffer':    '数据缓冲区指针。',
    'pData':      '数据指针。',
    'Data1':      '第一个数据（多字节操作的第一个字节/字）。',
    'Data2':      '第二个数据。',
    'Data3':      '第三个数据。',
    'pData1':     '第一个数据指针。',
    'pData2':     '第二个数据指针。',
    'Size':       '数据长度。单位（字节或半字）见函数原型。',
    'Length':     '数据长度（个数）。',
    'Ilength':    '初始化向量长度（CTR/GCM 模式的初始向量字节数）。',
    'Keysize':    '密钥长度，填 CRYP_KeySize_128b / _192b / _256b。',
    'InitVectors': '初始向量个数。',
    'Key':        '密钥数据。',
    'pKey':       '密钥数据指针。',
    'Timeout':    '超时时间（计数或毫秒），用于避免死等。',
    'Input':      '输入数据。',
    'Output':     '输出数据，函数把结果写入此缓冲区。',
    'Counter':    '计数值/计数器初值。',
    'Reload':     '重装值（计数器数到这个值后归零或复位）。',
    'Prescaler':  '预分频值。实际分频系数 = 该值 + 1。',
    'Mode':       '工作模式选择，取值见函数说明的枚举列表。',
    'Format':     '数据格式，如 RTC_Format_BIN（十进制）/ RTC_Format_BCD（BCD 码）。',
    'RTC_Format': '时间/日期的格式：RTC_Format_BIN = 字段直接写十进制数字；RTC_Format_BCD = 字段写 BCD 码（如 0x17 表示 17）。',
    'RTC_Alarm':  '闹钟编号，填 RTC_Alarm_A / RTC_Alarm_B。',
    'RTC_IT':     'RTC 中断源，如 RTC_IT_WUT（周期唤醒）/ RTC_IT_ALRA（闹钟 A）/ RTC_IT_TAMP（侵入）。',
    'RTC_FLAG':   'RTC 状态标志，如 RTC_FLAG_WUTF / RTC_FLAG_ALRAF。',
    'RTC_Clock':  'RTC 时钟源，如 RCC_RTCCLKSource_LSE / _LSI / _HSE。',
    'RTC_HourFormat': '小时制：RTC_HourFormat_24 或 RTC_HourFormat_12。',
    # ---- 中断/标志 ----
    'EXTI_Line':  'EXTI 中断线。EXTI_Line0 ~ EXTI_Line15 对应引脚号（同号引脚共线）；EXTI_Line16~22 是内部源（16=PVD、17=RTC闹钟、22=RTC周期唤醒）。',
    'TIM_IT':     '定时器中断源，如 TIM_IT_Update（更新）/ TIM_IT_CC1~CC4（捕获比较）/ TIM_IT_Trigger。',
    'TIM_FLAG':   '定时器状态标志，如 TIM_FLAG_Update / TIM_FLAG_CC1。',
    'ADC_IT':     'ADC 中断源，如 ADC_IT_EOC（转换完成）/ ADC_IT_AWD（模拟看门狗）。',
    'ADC_FLAG':   'ADC 状态标志，如 ADC_FLAG_EOC / ADC_FLAG_STRT。',
    'CAN_IT':     'CAN 中断源，如 CAN_IT_FMP0（FIFO0 收到报文）/ CAN_IT_TME（发送邮箱空）。',
    'CAN_FLAG':   'CAN 状态标志。',
    'I2C_IT':     'I2C 中断源。',
    'I2C_FLAG':   'I2C 状态标志，如 I2C_FLAG_RXNE / I2C_FLAG_TXE / I2C_FLAG_BUSY。',
    'USART_IT':   '串口中断源，如 USART_IT_RXNE（收到字节）/ USART_IT_IDLE（总线空闲）/ USART_IT_TC（发送完成）。',
    'USART_FLAG': '串口状态标志，如 USART_FLAG_TXE / USART_FLAG_RXNE / USART_FLAG_IDLE / USART_FLAG_TC。',
    'SPI_I2S_FLAG': 'SPI/I2S 状态标志，如 SPI_I2S_FLAG_TXE / SPI_I2S_FLAG_RXNE / SPI_I2S_FLAG_BSY。',
    'SPI_I2S_IT':   'SPI/I2S 中断源。',
    'PWR_FLAG':   '电源管理状态标志，如 PWR_FLAG_WU（唤醒）/ PWR_FLAG_SB（待机）/ PWR_FLAG_PVDO。',
    'FLASH_FLAG': 'Flash 状态标志，如 FLASH_FLAG_EOP（操作结束）/ FLASH_FLAG_WRPERR（写保护错误）/ FLASH_FLAG_BSY（忙）。',
    'RCC_FLAG':   'RCC 状态标志，如 RCC_FLAG_HSERDY / RCC_FLAG_LSERDY / RCC_FLAG_PLLRDY / RCC_FLAG_PINRST。',
    'EXTI_LineCmd': '是否使能这条 EXTI 线：ENABLE / DISABLE。',
    'ADC_Channel': 'ADC 通道，填 ADC_Channel_0 ~ ADC_Channel_18。通道号要与引脚对应（如 PA0 = 通道 0）。',
    'ADC_ExternalTrigConv': '外部触发源（用外部事件启动转换时填），如 ADC_ExternalTrigConv_T2_TRGO；不用外部触发随便填个合法值即可。',
    # ---- 结构体指针 ----
    'GPIO_InitStruct':        'GPIO 配置结构体指针，先填好字段再传进来。字段含义见结构体卡 GPIO_InitTypeDef。',
    'USART_InitStruct':       '串口配置结构体指针，字段见结构体卡 USART_InitTypeDef。',
    'I2C_InitStruct':         'I2C 配置结构体指针，字段见结构体卡 I2C_InitTypeDef。',
    'SPI_InitStruct':         'SPI 配置结构体指针，字段见结构体卡 SPI_InitTypeDef。',
    'ADC_InitStruct':         'ADC 配置结构体指针，字段见结构体卡 ADC_InitTypeDef。',
    'ADC_CommonInitStruct':   'ADC 公共配置结构体指针（ADC1/2/3 共用），字段见 ADC_CommonInitTypeDef。',
    'TIM_TimeBaseInitStruct': '定时器时基结构体指针，决定预分频与周期。字段见 TIM_TimeBaseInitTypeDef。',
    'TIM_OCInitStruct':       '输出比较/PWM 配置结构体指针，字段见 TIM_OCInitTypeDef。',
    'TIM_ICInitStruct':       '输入捕获配置结构体指针，字段见 TIM_ICInitTypeDef。',
    'TIM_BDTRInitStruct':     '刹车与死区配置结构体指针（仅高级定时器 TIM1/TIM8）。',
    'DMA_InitStruct':         'DMA 配置结构体指针，字段见 DMA_InitTypeDef。',
    'CAN_InitStruct':         'CAN 配置结构体指针。',
    'CAN_FilterInitStruct':   'CAN 过滤器配置结构体指针，不配过滤器就收不到任何报文。',
    'EXTI_InitStruct':        'EXTI 配置结构体指针，字段见 EXTI_InitTypeDef。',
    'NVIC_InitStruct':        'NVIC 配置结构体指针，字段见 NVIC_InitTypeDef。',
    'RTC_InitStruct':         'RTC 配置结构体指针，字段见 RTC_InitTypeDef。',
    'RTC_TimeStruct':         '时间结构体指针，字段见 RTC_TimeTypeDef。',
    'RTC_DateStruct':         '日期结构体指针，字段见 RTC_DateTypeDef。',
    'RTC_AlarmStruct':        '闹钟结构体指针，字段见 RTC_AlarmTypeDef。',
    'TxMessage':              '待发送的 CAN 报文结构体指针（含 ID、DLC、数据）。',
    'RxMessage':              '接收 CAN 报文的结构体指针，函数把收到的报文填进来。',
    'DAC_InitStruct':         'DAC 配置结构体指针。',
    # ---- 时钟/复位 ----
    'RCC_AHB1Periph': 'AHB1 总线上的外设时钟，如 RCC_AHB1Periph_GPIOA / DMA1 / DMA2 / ETH_MAC。可用按位或一次开多个。',
    'RCC_AHB2Periph': 'AHB2 总线外设时钟（USB OTG / RNG / HASH / DCMI）。',
    'RCC_AHB3Periph': 'AHB3 总线外设时钟（FSMC/FMC）。',
    'RCC_APB1Periph': 'APB1 总线外设时钟：TIM2~7/12~14、USART2/3、UART4/5、I2C1~3、SPI2/3、CAN、DAC、PWR、IWDG/WWDG、RTC。',
    'RCC_APB2Periph': 'APB2 总线外设时钟：TIM1/8~11、USART1/6、ADC1~3、SDIO、SPI1、SYSCFG。',
    'RCC_AHBPeriph':  'AHB 总线外设时钟。',
    'RCC_HSE':        '外部高速晶振配置：RCC_HSE_OFF / RCC_HSE_ON / RCC_HSE_Bypass。',
    'RCC_LSE':        '外部低速晶振（32.768kHz，给 RTC）：RCC_LSE_ON / _OFF / _Bypass。',
    'RCC_PLL':        'PLL 时钟源：RCC_PLLSource_HSE / RCC_PLLSource_HSI。',
    'RCC_SYSCLKSource': '系统时钟源：RCC_SYSCLKSource_HSI / _HSE / _PLL。',
    'RCC_HCLK':       'AHB 时钟分频（RCC_SYSCLK_Div1 ~ Div512）。',
    'RCC_PCLK1':      'APB1 时钟分频（RCC_HCLK_Div1 ~ Div16），APB1 最高 42MHz。',
    'RCC_PCLK2':      'APB2 时钟分频（RCC_HCLK_Div1 ~ Div16），APB2 最高 84MHz。',
    'RCC_RTCCLKSource': 'RTC 时钟源：RCC_RTCCLKSource_LSE / _LSI / _HSE_Div2~Div31。',
    'VoltageRange':   '供电电压范围：VoltageRange_1(2.1~2.7V) / VoltageRange_2(2.7~3.6V) / VoltageRange_3。3.3V 供电用 VoltageRange_3。',
    'FLASH_Latency':  'Flash 等待周期。主频越高需要越多：168MHz 用 FLASH_Latency_5（≤30MHz 用 0，每 30MHz 加 1）。',
    'FLASH_Sector':   '要擦除的扇区。F407ZET6（512KB）为 Sector_0 ~ 7；F407ZGT6（1MB）为 Sector_0 ~ 11。使用芯片上不存在的扇区时行为未定义。',
    'PWR_Regulator':  '内部稳压器状态：PWR_Regulator_ON / PWR_Regulator_LowPower。',
    'PWR_STOPEntry':  '进入 STOP 模式的方式：PWR_STOPEntry_WFI（中断唤醒）/ _WFE（事件唤醒）。',
    'PWR_VoltageScaling': '电压调节档位（对应最高主频）。',
    'NVIC_PriorityGroup': '中断优先级分组，决定 4 个优先级位中几位用于抢占。裸机常用 Group_2；FreeRTOS 必须用 Group_4。',
    'NVIC_VectTab':   '中断向量表位置：NVIC_VectTab_FLASH / NVIC_VectTab_RAM。',
    'Offset':         '偏移量（配合基址使用，如向量表偏移）。',
    'PreemptionPriority': '抢占优先级（数值越小优先级越高），范围由当前分组决定。',
    'SubPriority':    '子优先级：抢占级相同时比它。Group_4 下该位无效。',
    'PriorityGroup':  '当前优先级分组编号。',
    'SysTick_CLKSource': 'SysTick 时钟源：SysTick_CLKSource_HCLK 或 HCLK_Div8。',
    'LowPowerMode':   '低功耗模式选择。',
    # ---- 定时器细项 ----
    'TIM_Channel':    '定时器通道，填 TIM_Channel_1 ~ TIM_Channel_4。',
    'TIM_ICPolarity': '输入捕获的边沿：TIM_ICPolarity_Rising / _Falling / _BothEdge。',
    'TIM_ICSelection': '捕获通道映射：TIM_ICSelection_DirectTI（直连）/ _IndirectTI / _TRC。一般用 DirectTI。',
    'TIM_ICPSC':      '输入捕获预分频：TIM_ICPSC_DIV1 ~ DIV8，表示每 N 个边沿捕获一次。',
    'TIM_ICFilter':   '输入滤波（0x0 ~ 0xF）。信号毛刺多就加大，代价是延迟增加。',
    'TIM_OCMode':     '输出比较模式，PWM 输出用 TIM_OCMode_PWM1（最常用）。',
    'TIM_OCPolarity': '输出极性：TIM_OCPolarity_High（不反相）/ _Low（反相）。',
    'TIM_OCNPolarity':'互补输出极性（仅高级定时器）。',
    'TIM_OCPreload':  '比较值预装载：TIM_OCPreload_Enable 让运行时改占空比在下一周期生效，输出无毛刺。',
    'TIM_OCFast':     '快速使能（高级用法）。',
    'TIM_OCClear':    '输出清除（高级用法）。',
    'TIM_OCIdleState':'空闲状态时的输出电平（仅高级定时器）。',
    'TIM_OCNIdleState':'互补输出的空闲电平（仅高级定时器）。',
    'TIM_OutputState':'通道输出使能：TIM_OutputState_Enable 才会输出到引脚。',
    'TIM_OutputNState':'互补输出使能（仅高级定时器）。',
    'TIM_Pulse':      '比较值（CCR），决定占空比：占空比 = TIM_Pulse / (TIM_Period+1)。',
    'TIM_Prescaler':  '预分频值（0~65535）。定时器时钟先除以 (该值+1)。',
    'TIM_Period':     '自动重装值（周期），计数器数到它就归零。频率 = 定时器时钟/(Prescaler+1)/(Period+1)。',
    'TIM_CounterMode':'计数方向：TIM_CounterMode_Up（向上，最常用）/ _Down / _CenterAligned1~3。',
    'TIM_ClockDivision': '时钟分频（用于数字滤波采样）：TIM_CKD_DIV1 / DIV2 / DIV4，一般 DIV1。',
    'TIM_RepetitionCounter': '重复计数（仅高级定时器 TIM1/TIM8；普通定时器填 0）。',
    'TIM_ForcedAction': '强制输出动作。',
    'TIM_ExtTRGPrescaler': '外部触发（ETR）预分频。',
    'TIM_ExtTRGPolarity':  'ETR 极性：TIM_ExtTRGPolarity_NonInverted / _Inverted。',
    'ExtTRGFilter':   'ETR 输入滤波（0x0~0xF）。',
    'TIM_EventSource':'要软件触发的事件源，如 TIM_EventSource_Update（立刻重装）。',
    'TIM_TRGOSource': 'TRGO 触发输出源，如 TIM_TRGOSource_Update（用更新事件触发 ADC）。',
    'TIM_DMASource':  '定时器 DMA 请求源。',
    'TIM_DMABase':    'DMA 传输的寄存器基址。',
    'TIM_DMABurstLength': 'DMA 突发长度。',
    'TIM_EncoderMode':'编码器模式（TIM_EncoderMode_TI1 / TI2 / TI12）。',
    'TIM_IC1Polarity':'编码器通道 1 极性。',
    'TIM_IC2Polarity':'编码器通道 2 极性。',
    'TIM_CCx':        '通道输出开关：TIM_CCx_Enable / _Disable。',
    'TIM_CCxN':       '互补通道输出开关。',
    'TIM_OPMode':     '单脉冲模式：TIM_OPMode_Single / _Repetitive。',
    'TIM_PSCReloadMode': '预分频重装方式：立即生效或下个更新事件生效。',
    'TIM_UpdateSource': '更新事件来源。',
    'TIM_CKD':        '时钟分频（同 TIM_ClockDivision）。',
    # ---- DMA ----
    'DMA_DIR':        '数据方向：DMA_DIR_PeripheralToMemory（外设→内存，接收）/ DMA_DIR_MemoryToPeripheral（内存→外设，发送）。',
    'DMA_BufferSize':'传输个数。按数据宽度折算字节数（Byte=个数，HalfWord=个数×2）。',
    'DMA_PeripheralInc': '外设地址是否递增。寄存器地址固定 → DMA_PeripheralInc_Disable。',
    'DMA_MemoryInc':  '内存地址是否递增。搬运数组 → DMA_MemoryInc_Enable。',
    'DMA_PeripheralDataSize': '外设数据宽度：Byte / HalfWord / Word。',
    'DMA_MemoryDataSize': '内存数据宽度：要与外设端一致，否则数据错乱。',
    'DMA_Mode':       '模式：DMA_Mode_Normal（搬一轮就停）/ DMA_Mode_Circular（循环自动回卷）。',
    'DMA_Priority':   '仲裁优先级：Low / Medium / High / VeryHigh。多个流同时用时才起作用。',
    'DMA_FIFOMode':   'FIFO 模式：简单用法用 DMA_FIFOMode_Disable。',
    'DMA_FIFOThreshold': 'FIFO 阈值（FIFO 关闭时无意义）。',
    'DMA_MemoryBurst': '内存端突发传输（FIFO 关闭时填 Single）。',
    'DMA_PeripheralBurst': '外设端突发传输。',
    'DMA_Channel':    'DMA 通道号，填 DMA_Channel_0 ~ 7。通道与外设为固定映射，须查「DMA 请求映射表」。',
    'DMA_PeripheralBaseAddr': '外设端地址，如 (uint32_t)&USART1->DR 或 &ADC1->DR。',
    'DMA_Memory0BaseAddr':    '内存端地址，填缓冲区数组首地址（须强制转换为 uint32_t）。',
    # ---- 其他 ----
    'I2C_AcknowledgedAddress': '地址位数：I2C_AcknowledgedAddress_7bit / _10bit。一般 7bit。',
    'I2C_Ack':        '收到字节后是否回 ACK：I2C_Ack_Enable / _Disable。读多字节时最后一个字节前要临时关掉。',
    'I2C_ClockSpeed': '总线速率（Hz），如 100000(100k) / 400000(400k)。先用 100k 调通再提速。',
    'I2C_DutyCycle':  '快速模式下 SCL 高低电平比：I2C_DutyCycle_2 或 _16_9。',
    'I2C_Direction':  '传输方向：I2C_Direction_Transmitter（写）/ I2C_Direction_Receiver（读）。',
    'I2C_OwnAddress1': '主机自身地址（做主时随便填 0x00；做从机时填本机地址）。',
    'I2C_Register':   '从机内部寄存器地址。',
    'SPI_BaudRatePrescaler': 'SPI 时钟分频：SPI_BaudRatePrescaler_2 ~ _256。SCK = PCLK / 分频。',
    'SPI_CPOL':       '时钟极性：空闲时 SCK 为高还是低。',
    'SPI_CPHA':       '时钟相位：在第 1 个还是第 2 个边沿采样。CPOL+CPHA 组成模式 0~3。',
    'SPI_DataSize':   '数据位宽：SPI_DataSize_8b / _16b。',
    'SPI_Direction':  '传输方向，最常用 SPI_Direction_2Lines_FullDuplex（全双工）。',
    'SPI_FirstBit':   '先发高位还是低位：SPI_FirstBit_MSB（常用）/ _LSB。',
    'SPI_Mode':       '主从模式：SPI_Mode_Master / SPI_Mode_Slave。',
    'SPI_NSS':        '片选方式：SPI_NSS_Soft（软件片选，最常用）/ SPI_NSS_Hard。',
    'SPI_CRCPolynomial': 'CRC 多项式（不用 CRC 也要填个值，如 7）。',
    'USART_BaudRate':'波特率（直接填数字，如 9600 / 115200）。',
    'USART_WordLength': '数据位：USART_WordLength_8b / _9b。8 位最常用；带校验时选 9b。',
    'USART_StopBits':'停止位：USART_StopBits_1 / _0_5 / _2 / _1_5。常用 1。',
    'USART_Parity':  '校验位：USART_Parity_No / _Even / _Odd。常用 No。',
    'USART_Mode':    '收发模式：USART_Mode_Rx | USART_Mode_Tx（可同时）。',
    'USART_HardwareFlowControl': '硬件流控：一般用 USART_HardwareFlowControl_None。',
    'USART_DMAReq':  'DMA 请求方向：USART_DMAReq_Tx（发）/ USART_DMAReq_Rx（收）。',
    'ADC_Resolution':'转换精度：ADC_Resolution_12b（0~4095，最常用）/ _10b / _8b / _6b。',
    'ADC_ScanConvMode': '是否扫描多通道：ENABLE 多通道 / DISABLE 单通道。',
    'ADC_ContinuousConvMode': '是否连续转换：ENABLE 后台一直转（配合 DMA 循环最好）。',
    'ADC_DataAlign': '数据对齐：ADC_DataAlign_Right（右对齐，读到 0~4095）/ _Left。',
    'ADC_NbrOfConversion': '规则序列里排了几个通道（1~16）。要和 ADC_RegularChannelConfig 的排名一致。',
    'ADC_ExternalTrigConvEdge': '外部触发边沿：不用外部触发填 ADC_ExternalTrigConvEdge_None。',
    'ADC_Mode':      'ADC 多重模式：ADC_Mode_Independent（各自独立，最常用）/ Dual 系列（同步采集）。',
    'ADC_Prescaler': 'ADC 时钟预分频，填 ADC_Prescaler_Div2 ~ Div8。ADC 时钟不得超过 36MHz；168MHz 系统用 Div4（21MHz）。',
    'ADC_DMAAccessMode': 'DMA 访问模式：单 ADC 用 ADC_DMAAccessMode_Disabled。',
    'ADC_TwoSamplingDelay': '两次采样间隔（仅多重模式有效）。',
    'ADC_SampleTime': '采样时间：ADC_SampleTime_3Cycles ~ _480Cycles。越长越稳（高阻抗信号源要长）。',
    'ADC_ExternalTrigInjecConv': '注入组外部触发源。',
    'ADC_ExternalTrigInjecConvEdge': '注入组触发边沿。',
    'ADC_InjectedChannel': '注入组通道。',
    'ADC_InjectedRank': '注入序列排名。',
    'ADC_InjectedSamplingTime': '注入通道采样时间。',
    'ADC_InjectedOffset': '注入通道偏移量。',
    'ADC_Offset':    '规则通道的偏移量（做减法用）。',
    'ADC_AnalogWatchdog': '模拟看门狗模式选择。',
    'ADC_AnalogWatchdogSingleChannel': '模拟看门狗监视的单个通道。',
    'ADC_HighThreshold': '看门狗上限阈值。',
    'ADC_LowThreshold':  '看门狗下限阈值。',
    'ADC_TempSensorVrefintCmd': '内部温度传感器/Vrefint 通道开关。',
    'ADC_DMACmd_x':  'ADC 的 DMA 请求开关。',
    'CAN_Mode':      'CAN 工作模式：CAN_Mode_Normal（正常）/ _LoopBack（自发自收，无硬件时自测）/ _Silent / _Silent_LoopBack。',
    'CAN_SJW':       '重新同步跳转宽度（CAN_SJW_1tq ~ _4tq），影响波特率计算。',
    'CAN_BS1':       '时间段 1（CAN_BS1_1tq ~ _16tq）。',
    'CAN_BS2':       '时间段 2（CAN_BS2_1tq ~ _8tq）。',
    'CAN_Prescaler': '波特率分频：波特率 = PCLK1 / (SJW+BS1+BS2) / Prescaler。',
    'CAN_FilterNumber': '过滤器编号（CAN1 用 0~13，CAN2 用 14~27）。',
    'CAN_FilterMode': '过滤模式：IdMask（掩码，收一段 ID）/ IdList（列表，只收指定 ID）。',
    'CAN_FilterScale': '过滤器位宽：32bit / 16bit。',
    'CAN_FilterFIFOAssignment': '关联的接收 FIFO：CAN_Filter_FIFO0 / _FIFO1。',
    'CAN_FilterActivation': '是否激活该过滤器：ENABLE 必须填，否则收不到。',
    'CAN_FilterIdHigh': '要收的 ID 高 16 位（32 位模式下标准帧要左移 5 位对齐）。',
    'CAN_FilterIdLow':  '要收的 ID 低 16 位。',
    'CAN_FilterMaskIdHigh': '掩码高 16 位。掩码位为 1 表示该位必须匹配。全 0 = 全通。',
    'CAN_FilterMaskIdLow':  '掩码低 16 位。',
    'FIFONumber':    'FIFO 编号：CAN_FIFO0 / CAN_FIFO1。要与过滤器里设置的一致。',
    'Mailbox':       '发送邮箱号（CAN_Transmit 的返回值）。',
    'TransmitMailbox': '发送邮箱号。',
    'CAN_OperatingMode': '要切换到的模式：Initialization / Normal / Sleep。',
    'CAN_BankNumber': '起始 bank 编号（CAN2 起始 bank）。',
    'SDIO_Argument': 'SD 命令参数。',
    'SDIO_CmdIndex': 'SD 命令号（如 CMD0/CMD8/ACMD41）。',
    'SDIO_Response': '期望的响应类型。',
    'SDIO_Wait':     '等待类型（无等待/短等待/长等待）。',
    'SDIO_CPSM':     '命令通道状态机开关：ENABLE 才会真正发命令。',
    'SDIO_DataTimeOut': '数据传输超时。',
    'SDIO_DataLength':  '数据长度（字节）。',
    'SDIO_DataBlockSize': '数据块大小（2^n 字节）。',
    'SDIO_TransferDir': '传输方向（读卡/写卡）。',
    'SDIO_TransferMode': '传输模式（块/流）。',
    'SDIO_DPSM':     '数据通道状态机开关。',
    'RNG_Seed':      '随机数种子。',
    'HASH_AlgoSelection': 'HASH 算法选择（MD5/SHA1/SHA224/SHA256）。',
    'HASH_AlgoMode': 'HASH 模式（HASH 或 HMAC）。',
    'HASH_DataType': 'HASH 数据类型（32/16/8 位 / 位流）。',
    'CRYP_AlgoDir':  '加解密方向：Encrypt / Decrypt。',
    'CRYP_AlgoMode': '算法模式（AES/TDES/DES + ECB/CBC/CTR/GCM/CCM）。',
    'CRYP_KeySize':  '密钥长度（128/192/256 位）。',
    'CRYP_KeySelect': '密钥选择（Key1 / Key2）。',
    'CRYP_InitVector': '初始向量。',
    'CRYP_InitVectorSize': '初始向量长度。',
    'CRYP_DataIn':   '待处理数据。',
    'CRYP_DataOut':  '处理结果。',
    'DAC_Trigger':   'DAC 触发源（软件触发/定时器触发/外部中断线）。',
    'DAC_WaveGeneration': '波形发生器模式（关闭/噪声/三角波）。',
    'DAC_LFSRUnmask_TriangleAmplitude': '噪声/三角波幅度设置。',
    'DAC_OutputBuffer': '输出缓冲（使能后驱动能力更强）。',
    'DAC_Align':     '数据对齐：DAC_Align_12b_R / _12b_L / _8b_R。',
    'DAC_Data':      '要输出的数字量。',
    'DAC_SoftwareTrigger': '软件触发方式（DAC_SoftwareTrigger_Enable）。',
    'DAC_IT':        'DAC 中断源。',
    'DAC_FLAG':      'DAC 状态标志。',
    'RCC_Clocks':    '时钟频率结果结构体指针（RCC_ClocksTypeDef），函数把各总线频率填进去。',
    'PWR_BackupAccess': '备份域访问：ENABLE 允许操作 RTC/备份寄存器。',
    'RTC_BKP_DR':    '备份寄存器编号（RTC_BKP_DR0 ~ DR19）。掉电由 VBAT 保持，常用来存"首次上电"标记。',
    'RTC_WakeUpCounter': '唤醒计数器。配合 CK_SPRE(1Hz) 时，填 0 = 每 1 秒触发一次。',
    'RTC_WakeUpClock':   '唤醒定时器时钟源：RTC_WakeUpClock_CK_SPRE_16bits（1Hz，最常用）/ RTC_WakeUpClock_RTCCLK_Div2 ~ Div16。',
    'WWDG_Prescaler':'窗口看门狗预分频：WWDG_Prescaler_1 / _2 / _4 / _8。',
    'WindowValue':   '窗口值（0x40 ~ 0x7F）。计数降到该值之后才允许喂狗，早喂会复位。',
    'Counter':       '计数器初值（WWDG 必须 0x40 ~ 0x7F，第 6 位必须为 1）。',
    'IWDG_Prescaler':'独立看门狗预分频：IWDG_Prescaler_4 ~ _256。超时 = 分频×(重装值+1)/32768 秒。',
    'IWDG_WriteAccess': '看门狗寄存器写保护：改参数前必须 IWDG_WriteAccess_Enable。',
    'Vref':          '参考电压（毫伏），用于补偿计算。',
    'SOC':           '起始转换通道。',
    'Prescaler':     '预分频值（实际分频 = 该值 + 1）。',
    'VoltageScaling':'电压调节档位。',
    'IWDG_Reload':   '看门狗重装值（0~4095），决定超时时间。',
    'ReloadValue':   '重装值。',
    # ---- 定时器数值类（补齐高频遗漏） ----
    'Autoreload':    '新的自动重装值（ARR）。改它等于改定时周期；周期 = (Prescaler+1)×(该值+1)/定时器时钟。',
    'Period':        '定时周期（自动重装值 ARR）。',
    'Pulse':         '比较值（CCR），决定 PWM 占空比 = 该值 / (Period+1)。',
    'Compare1':      '通道 1 的比较值（CCR1），即占空比。',
    'Compare2':      '通道 2 的比较值（CCR2）。',
    'Compare3':      '通道 3 的比较值（CCR3）。',
    'Compare4':      '通道 4 的比较值（CCR4）。',
    'CompareValue':  '比较值。',
    'Capture1':      '通道 1 的捕获值。',
    'Capture2':      '通道 2 的捕获值。',
    'Capture3':      '通道 3 的捕获值。',
    'Capture4':      '通道 4 的捕获值。',
    'Channel':       '通道编号（哪个通道）。',
    'Rank':          '在该序列里排第几（从 1 开始）。决定 DMA 缓冲区的下标顺序。',
    'TIM_UpdateDisable': '更新事件开关。',
    'PrescalerValue':'预分频值（实际分频 = 该值 + 1）。',
    'Value':         '要设置的值。',
    'Value1':        '第一个值。',
    'Value2':        '第二个值。',
    'Value3':        '第三个值。',
    'Data0':         '第 0 个数据。',
    'Data4':         '第 4 个数据。',
    'Data32':        '32 位数据 / 数据块。',
    'Data64':        '64 位数据。',
    'Address1':      '第一个地址。',
    'Address2':      '第二个地址。',
    'AddressOffset': '地址偏移量（相对基址）。',
    'Num':           '数量（个数）。',
    'Number':        '数量 / 编号。',
    'Count':         '计数个数。',
    'Index':         '下标 / 索引。',
    'Id':            '标识（编号）。',
    'ID':            '标识（编号）。',
    'Priority':      '优先级（数值越小优先级越高）。',
    'Flag':          '状态标志。',
    'State':         '状态值。',
    'Div':           '分频系数。',
    'CHx':           '通道号（x 为 1~4）。',
    'USART_Clock':   '同步时钟模式：USART_Clock_Disable / _Enable（一般用 Disable）。',
    'USART_CPOL':    '同步模式时钟极性（一般不用）。',
    'USART_CPHA':    '同步模式时钟相位（一般不用）。',
    'USART_LastBit': '同步模式最后一位时钟脉冲（一般不用）。',
    'USART_ClockMode':'同步模式时钟模式。',
    'MSB':           '最高有效位。',
    'LSB':           '最低有效位。',
    'ByteSel':       '字节选择。',
    'ByteSel_0':     '字节选择位 0。',
    'ByteSel_1':     '字节选择位 1。',
    'SD_CMD':        'SD 命令。',
    'ACmd':          '应用命令标志。',
    'SDIO_RESP':     'SD 响应。',
    'SDIO_RESP1':    'SD 响应寄存器 1。',
    'SDIO_CmdIndex2':'SD 命令索引。',
    'TIM_ICInit_Struct': '输入捕获配置结构体指针。',
    'TIM_ETR':       '外部触发输入（ETR）配置。',
    'TIM_ExtTRG':    '外部触发配置。',
    'DataValue':     '数据值。',
    'SensorSelect':  '传感器选择。',
    'USART_OverSampling': '过采样方式（16 倍或 8 倍）。',
    # ---- 看门狗 / ADC 阈值 ----
    'HighThreshold':'模拟看门狗上限阈值（12 位，0~4095）。转换值超过它就置标志。',
    'LowThreshold': '模拟看门狗下限阈值（12 位，0~4095）。低于它就置标志。',
    'BufferLength':'缓冲区长度（要参与运算的字节/字个数）。',
    'IDValue':     '要写入 ID 寄存器的值。',
    'Nonce':       'AES-CCM 用的一次性随机数（每个流程必须唯一）。',
    'NonceSize':   'Nonce 的字节长度。',
    'Header':      '附加认证数据（AAD）缓冲区指针。',
    'HLength':     '附加认证数据的长度。',
    'HBuffer':     '临时缓冲区，用于拼接附加认证数据。',
    'AuthTAG':     '认证标签（MAC / TAG）缓冲区指针，函数把算出的标签写进去。',
    'TAGSize':     '认证标签的长度（字节）。',
    'InputBuffer': '输入缓冲区指针。',
    'OutputBuffer':'输出缓冲区指针（存放处理结果）。',
    'CRYP_DMAReq':'CRYP 的 DMA 请求方向（输入/输出）。',
    'CRYP_FLAG':   'CRYP 状态标志。',
    'CRYP_IT':     'CRYP 中断源。',
    'Seed':        '随机数种子（写入后硬件开始产生随机数）。',
    'Argument':    '命令参数（如 SD 命令的 32 位参数）。',
    'CmdIndex':    '命令号。',
    'Response':    '期望的响应类型。',
    'Wait':        '等待类型（无等待/短等待/长等待）。',
    'CPSM':        '命令通道状态机开关：ENABLE 才真正发送命令。',
    'DPSM':        '数据通道状态机开关：ENABLE 才开始数据传输。',
    'DataBlockSize':'数据块大小。',
    'DataLength':  '数据长度（字节）。',
    'DataTimeOut': '数据传输超时时间。',
    'TransferDir': '传输方向（读/写）。',
    'TransferMode':'传输模式（块/流）。',
    # ---- 最后一轮：占位较广的"读/查"类参数 ----
    'CRYP_FLAG':   'CRYP 状态标志。',
    'CRYP_IT':     'CRYP 中断源。',
    'DMA2D_FLAG':  'DMA2D 状态标志（如 DMA2D_FLAG_TC 传输完成 / _TE 传输错误 / _CE 配置错误）。',
    'DMA2D_IT':    'DMA2D 中断源。',
    'DCMI_IT':     'DCMI 中断源（如 DCMI_IT_FRAME 帧结束 / _VSYNC 场同步 / _ERR 错误）。',
    'DCMI_FLAG':   'DCMI 状态标志。',
    'SDIO_IT':     'SDIO 中断源（如 SDIO_IT_CCRCFAIL 命令响应 CRC 失败 / _DATAEND 数据结束）。',
    'SDIO_FLAG':   'SDIO 状态标志。',
    'RCC_TIMCLKPrescaler': '定时器时钟预分频选择：RCC_TIMCLKPrescaler_HCLK（=HCLK）/ RCC_TIMCLKPrescaler_HCLKdiv2 / _HCLKdiv4。',
    'DBGMCU_Periph': '要调试时冻结的外设（可用按位或组合多个，如 DBGMCU_IWDG_STOP | DBGMCU_WWDG_STOP）。',
    'DMA2D_InitStruct': 'DMA2D 配置结构体指针。',
    'DMA2D_FG_InitStruct': 'DMA2D 前景层配置结构体指针。',
    'DMA2D_BG_InitStruct': 'DMA2D 背景层配置结构体指针。',
    'DCMI_InitStruct': 'DCMI 配置结构体指针。',
    'DCMI_CROPInitStruct': 'DCMI 裁剪窗口配置结构体指针。',
    'HASH_ContextSave': '保存 HASH 上下文的缓冲区指针。',
    'HASH_ContextRestore': '要恢复的 HASH 上下文缓冲区指针。',
    'HASH_InitStruct': 'HASH 配置结构体指针。',
    'CRYP_ContextSave': '保存 CRYP 上下文的缓冲区指针。',
    'CRYP_ContextRestore': '要恢复的 CRYP 上下文缓冲区指针。',
    'CRYP_InitStruct': 'CRYP 配置结构体指针。',
    'CRYP_KeyInitStruct': 'CRYP 密钥配置结构体指针。',
    'CRYP_IVInitStruct': 'CRYP 初始向量配置结构体指针。',
    'CRYP_Phase':  'AES-CCM/GCM 的阶段（初始化 / 头部认证 / 载荷 / 结束）。',
    'RNG_InitStruct': '（本型号无此结构，保留占位）',
    'SAI_InitStruct': 'SAI 配置结构体指针。',
    'SAI_FrameInitStruct': 'SAI 帧配置结构体指针。',
    'SAI_SlotInitStruct': 'SAI 时隙配置结构体指针。',
    'LTDC_InitStruct': 'LTDC 配置结构体指针。',
    'LTDC_Layer_InitStruct': 'LTDC 图层配置结构体指针。',
    'FMC_NORSRAMInitStruct': 'FMC NOR/SRAM 配置结构体指针。',
    'FMC_SDRAMInitStruct': 'FMC SDRAM 配置结构体指针。',
    'FSMC_NORSRAMInitStruct': 'FSMC NOR/SRAM 配置结构体指针。',
    'SDIO_InitStruct': 'SDIO 配置结构体指针。',
    'SDIO_CmdInitStruct': 'SDIO 命令配置结构体指针。',
    'SDIO_DataInitStruct': 'SDIO 数据配置结构体指针。',
    'I2S_InitStruct': 'I2S 配置结构体指针。',
    'I2S_Standard': 'I2S 标准（飞利浦 / MSB / LSB / PCM）。',
    'I2S_DataFormat': 'I2S 数据格式（16 位 / 24 位 / 32 位等）。',
    'I2S_MCLKOutput': '主时钟输出开关。',
    'I2S_AudioFreq': '音频采样率（Hz）。',
    'I2S_CPOL':    'I2S 空闲时钟电平。',
    'SPI_I2S_DMAReq': 'SPI/I2S 的 DMA 请求方向（发送/接收）。',
    # ---- 各外设的 _IT / _FLAG 家族（收官）----
    'DMA_IT':     'DMA 中断源（如 DMA_IT_TC 传输完成 / _HT 半传输 / _TE 传输错误 / _DME 直接模式错误）。',
    'DMA_FLAG':   'DMA 状态标志（如 DMA_FLAG_TCIF5 流 5 传输完成 / _TEIF5 错误）。',
    'FMC_IT':     'FMC 中断源。',
    'FMC_FLAG':   'FMC 状态标志。',
    'FSMC_IT':    'FSMC 中断源。',
    'FSMC_FLAG':  'FSMC 状态标志。',
    'HASH_IT':    'HASH 中断源（如 HASH_IT_DINI 数据输入完成 / _DCIS 摘要计算完成）。',
    'HASH_FLAG':  'HASH 状态标志。',
    'LTDC_IT':    'LTDC 中断源（如 LTDC_IT_LI 行中断 / _FU  FIFO 下溢 / _TERR 传输错误）。',
    'LTDC_FLAG':  'LTDC 状态标志。',
    'RCC_IT':     'RCC 中断源（如 RCC_IT_LSIRDY / _HSERDY / _PLLRDY / _CSS 时钟安全系统）。',
    'RNG_IT':     'RNG 中断源。',
    'RNG_FLAG':   'RNG 状态标志（如 RNG_FLAG_DRDY 数据就绪 / _CECS 时钟错误 / _SECS 种子错误）。',
    'SAI_IT':     'SAI 中断源。',
    'SAI_FLAG':   'SAI 状态标志。',
    'FLASH_IT':   'Flash 中断源（操作结束 / 错误）。',
    'Ilen':       '数据长度（字节数）。',
    'Keylen':     '密钥长度（字节数）。',
    'SDRAM_Bank': 'SDRAM 存储区编号（FMC_SDRAM_Bank1 / Bank2）。',
    'RTC_Tamper': 'RTC 侵入检测引脚（RTC_Tamper_1 / _2），检测外壳被打开。',
    'TIM_InputTriggerSource': '定时器输入触发源：TIM_TS_ITR0~ITR3（内部其他定时器）/ TIM_TS_TI1FP1 等（引脚）/ TIM_TS_ETRF（外部）。',
    'DAC_Wave':   'DAC 波形类型：DAC_Wave_Noise（噪声）/ DAC_Wave_Triangle（三角波）。',
    'OB_PCROP':   'Flash 专有代码读出保护选项字节。',
    'OB_PcROP':   'Flash 专有代码读出保护选项字节。',
    'OB_WRP':     'Flash 写保护选项字节（哪些扇区被写保护）。',
    'OB_RDP':     'Flash 读保护级别（Level 0/1/2）。',
    'OB_BOR':     '欠压复位阈值选项字节。',
    'OB_BOOT':    '启动模式选项字节（BOOT0/BOOT1 的默认值）。',
    'OB_IWDG':    '硬件看门狗选项字节（上电即启用 IWDG）。',
    'DMA2D_DeadTime': 'DMA2D 死区时间（防止同层读写冲突）。',
    'DMA2D_LWatermarkConfig': 'DMA2D 行水位线（FIFO 剩余多少行时触发中断）。',
    'DMA_CurrentMemory': '当前使用的目标内存区（DMA_Memory_0 / _1，双缓冲时切换）。',
    'DMA_MemoryTarget':  '目标内存区。',
    'DMA_FlowCtrl': '流控制方式：DMA_FlowCtrl_0（DMA 控制）/ _1（外设控制）。',
    'DMA_Pincos':   '外设递增偏移大小（DMA_PINCOS_Psize / _Word）。',
    'MemoryBaseAddr':  '内存端基址。',
    'Memory1BaseAddr': '内存 1 端基址（双缓冲的第二块）。',
    'DCMI_CodesInitStruct': 'DCMI 同步码配置结构体指针。',
    'USART_Clock': '同步时钟模式（一般用 Disable，异步串口不需要）。',
    # ---- 收尾：最后 32 个参数名 ----
    'SPIx':        'SPI，填 SPI1 / SPI2 / SPI3。SPI1 挂 APB2，SPI2/3 挂 APB1。',
    'SAI_Block_x': 'SAI 子模块，填 SAI_Block_A / SAI_Block_B。',
    'DAC_Channel':'DAC 通道，填 DAC_Channel_1 / DAC_Channel_2。',
    'DAC_IT':     'DAC 中断源（如 DMA 下溢中断）。',
    'DCMI_IT':    'DCMI 中断源（帧结束 / 场同步 / 行同步 / 错误）。',
    'DMA2D_IT':   'DMA2D 中断源（传输完成 / 传输错误 / 配置错误 / 水印）。',
    'I2C_IT':     'I2C 中断源（如 I2C_IT_BUF 缓冲 / I2C_IT_EVT 事件 / I2C_IT_ERR 错误）。',
    'SPI_I2S_IT':'SPI/I2S 中断源（如 SPI_I2S_IT_TXE 发送空 / _RXNE 接收非空 / _ERR 错误）。',
    'CRYP_IT':    'CRYP 中断源。',
    'TIM_ExtTRGPolarity': '外部触发（ETR）极性：TIM_ExtTRGPolarity_NonInverted（不反相）/ _Inverted（反相）。',
    'Number':     '数量 / 个数。',
    'ValidNumber':'有效个数。',
    'CAN_BankNumber': 'CAN2 的起始 bank 编号（过滤器分组用）。',
    'I2Sxext':    '扩展 I2S（I2S2_ext / I2S3_ext），用于全双工 I2S。',
    'HSICalibrationValue': 'HSI 内部时钟校准值（0~31），用于微调 HSI 频率。',
    'RCC_PLLSAIDivR': 'PLLSAI 的 R 分频（给 LTDC 用的像素时钟）。',
    'PLLM':       'PLL 输入分频：VCO 输入 = HSE / PLLM。影响 VCO 频率（须在 1~2MHz 输入、100~432MHz 之间）。',
    'PLLN':       'PLL 倍频系数：VCO 输出 = 输入 × PLLN。',
    'PLLP':       'PLL 系统时钟分频：SYSCLK = VCO / PLLP。PLLP 只能是 2/4/6/8。',
    'PLLQ':       'PLL 的 Q 分频，给 USB/SDIO/RNG 提供 48MHz 时钟。',
    'PLLI2SN':    'PLLI2S 倍频系数（I2S 时钟用）。',
    'PLLI2SR':    'PLLI2S 的 R 分频（I2S 时钟用）。',
    'PLLSAIN':    'PLLSAI 倍频系数（LTDC 时钟用）。',
    'PLLSAIQ':    'PLLSAI 的 Q 分频。',
    'RCC_RTCCLKSource': 'RTC 时钟源：RCC_RTCCLKSource_LSE（32.768kHz 晶振，最准）/ _LSI（内部，精度差）/ _HSE_Div2~Div31。',
    'RCC_PLLI2SDivQ': 'PLLI2S 的 Q 分频。',
    'RCC_PLLSAIDivQ': 'PLLSAI 的 Q 分频。',
    'SDIO_RESP':  'SD 响应寄存器值。',
    'EXTI_PortSourceGPIOx': 'EXTI 的端口源，填 EXTI_PortSourceGPIOA ~ GPIOx（表示用哪个端口）。',
    'SysTick_CLKSource': 'SysTick 时钟源：SysTick_CLKSource_HCLK（不分频）或 _HCLK_Div8。',
    'TIM_DMASource': '定时器的 DMA 请求源（如 TIM_DMASource_Update / _CC1）。',
    'ICFilter':   '输入捕获滤波（0x0 ~ 0xF）。信号毛刺多就加大，代价是延迟增加。',

    # ================= 本项目的芯片差异提示（写在参数里最省事） =================
    # LTDC / DMA2D / SAI / FMC / LCD-TFT 属于 F429/F427；CRYP/HASH 属于 F415/417。
    # F407ZET6（板1）/ F407ZGT6（板2）里没有这些外设，标准库整包里却带着，
    # 所以这些函数的参数一律标注"F407 用不到"，避免查的时候误以为能用。
    'LTDC_CD':       '要查询的 LTDC 状态位。F407 无此参数（LTDC 属 F429/F427）。',
    'LTDC_LIPositionConfig': '行中断位置，填行号 0 ~ 活动高度-1，扫描到该行时触发行中断。F407 无此参数。',
    'LTDC_Reload':   '重载方式：LTDC_Reload_Immediate（立即生效，画面可能撕裂）或 LTDC_Reload_Vblank（垂直消隐期生效）。F407 无此参数。',
    'ConstantAlpha': '常数 alpha 混合系数 0 ~ 255：0 = 该层完全透明，255 = 完全不透明。F407 无此参数。',
    'OffsetX':       '层窗口左上角相对活动显示区左边缘的横向像素偏移。F407 无此参数。',
    'OffsetY':       '层窗口左上角相对活动显示区上边缘的纵向像素偏移。F407 无此参数。',
    'Width':         '层窗口宽度（像素）。右边界 = OffsetX + Width - 1。F407 无此参数。',
    'Height':        '层窗口高度（像素）。下边界 = OffsetY + Height - 1。F407 无此参数。',
    'FMC_Number':    'SDRAM 自动刷新次数：每行刷新次数，按 SDRAM 手册的 tREF 计算。F407 无此参数（F407 用 FSMC）。',
    'FMC_Count':     '刷新计数器值：两次刷新之间的等待周期数，按 SDRAM 手册与工作频率计算。F407 无此参数。',
    'RCC_SAIBlockACLKSource': 'SAI 块 A 的时钟源：RCC_SAIACLKSource_PLLSAI / _PLLI2S / _Ext（外部引脚输入）。F407 无此参数（SAI 属 F429/F427）。',
    'RCC_SAIBlockBCLKSource': 'SAI 块 B 的时钟源：RCC_SAIBCLKSource_PLLSAI / _PLLI2S / _Ext。F407 无此参数（SAI 属 F429/F427）。',
    'SAI_CompandingMode': 'SAI 压扩模式：把线性 PCM 压成 A-law / µ-law 以省带宽。F407 无此参数。',
    'SAI_MuteCounter':    'SAI 静音帧计数：连续静音多少帧后置静音标志。F407 无此参数。',
    'SAI_MuteValue':      'SAI 静音时输出的填充值（0 或 1）。F407 无此参数。',
    'SAI_TRIState':       'SAI 空闲时数据脚与帧同步脚是否高阻。F407 无此参数。',

    # ===================== RTC（本项目 RTC 电子钟会用到） =====================
    'RTC_AlarmSubSecondValue': '闹钟的亚秒（sub-second）比较值，范围 0 ~ 0x7FFFF（共 20 位，含 SS 位）。它和"同步预分频值"比较，所以设 0 就是"每秒初"，设 (SynchPrediv+1)/2 之类可做半秒闹钟。',
    'RTC_AlarmSubSecondMask':  (
        '亚秒比较时哪些位不参与比较：RTC_AlarmSubSecondMask_None = 20 位全比较（最精确）'
        '；'
        '_SS14 = 只用高 14 位；'
        '_All = 忽略亚秒（整秒闹钟）'
        '。'
        '保留位数越少，'
        '闹钟精度越低。'),
    'RTC_CalibOutput':     (
        '校准输出：在 RTC_CALIB 引脚上输出 RTC_CalibOutput_512Hz 或 _1Hz，'
        '供示波器/频率计测量校准效果。'
        '输出须经 RTC_OutputConfig 映射到 PC13（或重映射到 PF11）'
        '。'),
    'RTC_CalibSign':       '粗校准的"加还是减"：RTC_CalibSign_Positive（走快时减脉冲，把钟调慢）或 _Negative。配合 RTC_CoarseCalibConfig 的校准值一起用。',
    'RTC_StoreOperation':  '夏令时切换时是否真的修改当前时间：RTC_StoreOperation_Set = 按 BKP 位把时间增减 1 小时；_Reset = 只做软件标记。',
    'RTC_Output':          'RTC 输出引脚（PC13/PA0 等）上输出哪个信号：RTC_Output_Disable（关）、_AlarmA、_AlarmB、_WakeUp。做"闹钟亮灯"这类硬件联动时用。',
    'RTC_OutputPolarity':  'RTC 输出信号极性：RTC_OutputPolarity_High（高电平有效）或 _Low。',
    'RTC_OutputType':      'RTC 输出引脚驱动方式：RTC_OutputType_OpenDrain（开漏，需外接上拉，可与其它开漏信号线"线与"共用）或 _PushPull（推挽，驱动能力强）。',
    'RTC_SmoothCalibPeriod': '平滑校准周期（参考时钟 32768Hz 时）：_32sec / _16sec / _8sec。周期越长，同样精度下每秒只需要微调更少的脉冲，走时更平滑。',
    'RTC_SmoothCalibPlusPulses': '校准方向：_Set=增加脉冲（钟走慢时用），_Reset=减少脉冲（钟走快时用）。对应 CALP 位。',
    'RTC_SmouthCalibMinusPulsesValue': '每个校准周期扣掉的脉冲数，0 ~ 511。库函数原型中该参数名拼写为 Smouth，按此拼写，不要写成 Smooth。',
    'RTC_ShiftAdd1S':      '同步移位时"要不要补 1 秒"：_Set=把秒加 1（给秒做粗调），_Reset=不动。配合 RTC_ShiftSubFS 用，可以一次把亚秒也拨到位。',
    'RTC_ShiftSubFS':      '同步移位时要替换掉的秒分数（sub-second fractions），0 ~ 0x7FFF。想"整秒对齐"就把当前亚秒值填进来。',
    'RTC_TamperFilter':    '防拆(tamper)输入的数字滤波：_Disable（不滤）、_2Sample / _4Sample / _8Sample（连续采到几次同电平才算有效）。按键抖动、干扰大时加大采样数。',
    'RTC_TamperPrechargeDuration': '防拆引脚采样前的预充电时长：_1RTCCLK / _2 / _4 / _8RTCCLK。引脚上有较大寄生电容时要拉长，否则读不准。',
    'RTC_TamperSamplingFreq': '防拆引脚的采样频率：RTCCLK_Div32768 ~ _Div256（分频越小采样越快、越灵敏也越费电）。',
    'RTC_TamperTrigger':   '防拆触发条件：RisingEdge/HighLevel（高电平或上升沿有效）或 FallingEdge/LowLevel（低电平或下降沿有效）。取决于拆机时引脚被拉高还是拉低。',
    'RTC_TimeStampEdge':   '时间戳捕获边沿：RTC_TimeStampEdge_Rising（上升沿记下当前时间）或 _Falling。',
    'RTC_TimeStampPin':    '用哪个引脚做时间戳输入：RTC_TimeStampPin_PC13 或 _PI8（内部重映射）。',

    # ===================== RCC（时钟树） =====================
    'RCC_SYSCLK':   (
        'AHB 总线分频系数，'
        '填 RCC_SYSCLK_Div1 ~ Div512；'
        'HCLK = SYSCLK / 该系数。'
        '参数名易误解：它填的是 AHB 分频值，'
        '不是系统时钟来源（来源由 RCC_PLLConfig 与 SystemInit 决定）'
        '。'),
    'RCC_PLLSource': '主 PLL 的输入时钟来源：RCC_PLLSource_HSI（内部 16MHz，精度差）或 RCC_PLLSource_HSE（外部晶振，两板都是 8MHz）。做串口/LoRa 这类对时钟精度敏感的通信，必须用 HSE。',
    'RCC_MCO1Source': 'MCO1 引脚（PA8）上输出哪个时钟：_HSI / _LSE / _HSE / _PLLCLK。调时钟树时用来"把内部时钟引出来测"，非常实用。',
    'RCC_MCO1Div':  'MCO1 输出分频：RCC_MCO1Div_1 ~ _5（分频 1~5）。示波器/频率计量程有限时降频用。',
    'RCC_MCO2Source': 'MCO2 引脚（PC9）上输出哪个时钟：_SYSCLK / _PLLI2SCLK / _HSE / _PLLCLK。',
    'RCC_MCO2Div':  'MCO2 输出分频：RCC_MCO2Div_1 ~ _5。',
    'RCC_I2SCLKSource': 'I2S 时钟源：RCC_I2SCLKSource_PLLI2S（用 PLLI2S，最常用）或 _External（用外部 I2S_CKIN 引脚 PC9 输入）。做 I2S 音频时才用得到。',

    # ===================== 其余外设 =====================
    'I2C_EVENT':    (
        '要检查的事件（状态位组合值）'
        '，'
        '如 I2C_EVENT_MASTER_MODE_SELECT（起始位已发出）'
        '、'
        'I2C_EVENT_MASTER_BYTE_TRANSMITTED（一个字节发送完毕）'
        '、'
        'I2C_EVENT_MASTER_BYTE_RECEIVED（收到一个字节）'
        '、'
        'I2C_EVENT_SLAVE_ACK_FAILURE（从机无应答）'
        '。'
        '用 I2C_CheckEvent 轮询。'
        '不要自行组合这些值，'
        '直接使用库宏。'),
    'I2C_DigitalFilter': '数字噪声滤波系数（0 ~ 15）：数值越大滤得越狠，能压掉总线毛刺，代价是高速通信时波形被削。I2C 速率不高（100k/400k）时一般填 0 或小值即可。',
    'I2C_NACKPosition': '收到最后一个字节后，ACK/NACK 应该在哪个位置给出：_Next（在下一个字节，即当前字节是倒数第二个）或 _Current（就在当前字节，即当前字节就是最后一个）。读多字节时用来告诉从机"这就是最后一片数据，别再发了"。',
    'I2C_PECPosition': 'PEC（包错误校验）字节相对当前字节的位置：_Next（PEC 在下一字节）或 _Current（PEC 就是当前字节）。用 SMBus PEC 校验时才有意义。',
    'I2C_SMBusAlert': 'SMBus 报警引脚（SMBALERT）的有效电平：_Low 或 _High。SMBus 主设备响应从机报警时用。',
    'IWDG_FLAG':    '要读取的 IWDG 状态标志：IWDG_FLAG_PVU（预分频寄存器更新中，此时写 PR 无效）、IWDG_FLAG_RVU（重装寄存器更新中）。修改分频值或重装值前须等对应标志清零。',
    'PWR_PVDLevel': 'PVD（可编程电压检测器）的阈值：PWR_PVDLevel_0 ~ _7，对应约 2.2V ~ 2.9V（具体见参考手册电压表）。低于阈值时产生 PVD 中断，可以在掉电瞬间抢时间保存参数。',
    'PWR_Regulator_Voltage': '内部稳压器输出电压档（用于权衡性能与功耗）：_Scale1（最高性能，跑 168MHz 必须用这个）/ _Scale2 / _Scale3（低频省电档）。',
    'SPI_CRC':      '要读哪个 CRC 寄存器：SPI_CRC_Tx（发送 CRC）或 SPI_CRC_Rx（接收 CRC）。',
    'SPI_NSSInternalSoft': '软件方式控制内部 NSS 电平（NSS 配成软件管理时才有效）：_Set（拉高）/ _Reset（拉低）。多机 SPI 里用来手动选片。',
    'SYSCFG_ETH_MediaInterface': '以太网 MAC 与 PHY 之间的接口类型：MII（4 位数据，引脚多、速率低）或 RMII（2 位数据，引脚少、需 50MHz 参考时钟）。板1 有 RJ45+RH911105A，本项目走 LoRa 用不到；若以后用以太网，按 PHY 芯片手册选。',
    'TIM_Remap':    (
        'TIM 的引脚重映射选择，'
        '填 GPIO_Remap_* 系列宏（如 GPIO_Remap_TIM2_Partial1）'
        '。'
        '它实际调用 GPIO_PinRemapConfig，'
        '须先使能 AFIO 时钟（RCC_APB2Periph_AFIO）'
        '，'
        '并在配置 GPIO 之前调用。'),
    'TIM_MasterSlaveMode': '主从模式使能（TIM_MasterSlaveMode_Enable / _Disable）。用多个定时器做级联、门控、外部触发时才需要开。',
    'TIM_SlaveMode': '从模式的选择，决定定时器被什么事件复位/启动：_Reset（触发信号把计数器清零重启）、_Gated（触发高电平期间才计数）、_Trigger（触发信号启动计数）、_External1（外部时钟引脚直接当计数时钟）。输入捕获测频率时常用 _Reset。',
    'TIM_TIxExternalCLKSource': '把哪个外部引脚当作计数时钟（TI1/TI2 的边沿），并指定触发极性（如 TIM_TIxExternalCLK1Source_TI2 或 TIM_TIxExternalCLK1Source_TI1ED 双边沿）。测外部脉冲个数时用。',
    'USART_IrDAMode': 'IrDA 红外模式：_Normal（正常速率，115.2kbit/s 以下）或 _LowPower（低功耗，脉冲收窄到 3/16 位时间）。',
    'USART_LINBreakDetectLength': 'LIN 总线断开检测长度：_10b（10 位）或 _11b（11 位）。用 LIN 协议时才需要。',
    'USART_Prescaler': '智能卡模式的时钟预分频，0 ~ 255。仅在 USART 工作于 Smartcard 模式时有意义；普通串口通信保持默认 0，否则会破坏波特率。',
    'USART_WakeUp': '从 STOP 模式唤醒 USART 的方式：_IdleLine（检测到空闲总线唤醒）或 _AddressMark（收到带地址标记的字节唤醒）。做低功耗多机通信时用。',
    'GPIO_Write':   (
        '要写入整个端口输出数据寄存器的 16 位值（每位对应一个引脚）'
        '。'
        '整端口覆盖写：值为 0 的位会把对应引脚输出写成 0；'
        '只改某几位用 GPIO_SetBits / GPIO_ResetBits / GPIO_WriteBit。'),
    'GPIO_WriteBit': '要写入指定引脚的值，填 BitAction 枚举：Bit_RESET（写 0）或 Bit_SET（写 1），不能填 0/1 整数。',
    'OB_STOP':      '进入 STOP 模式时是否产生复位：OB_STOP_NoRST（不复位）或 OB_STOP_RST。选项字节写错会导致芯片无法启动，配置后须调用 FLASH_OB_Launch 生效。',
    'OB_STDBY':     '进入 Standby 模式时是否产生复位：OB_STDBY_NoRST 或 OB_STDBY_RST。同一套选项字节配置里和 OB_STOP 一起设。',

    # ============ 第二批（把剩余 12 个"官方原文兜底"也补掉） ============
    # 参数名差异：GPIO_Write 的第二个参数官方就叫 PortVal（不是 GPIO_Pin），
    # GPIO_WriteBit 的第三个参数叫 BitVal。按参数名建字典，所以必须用这两个名字。
    'PortVal':      (
        '要写入整个端口输出数据寄存器的 16 位值（每位对应一个引脚）'
        '。'
        '整端口覆盖写：值为 0 的位会把对应引脚输出写成 0；'
        '只改某几位用 GPIO_SetBits / GPIO_ResetBits / GPIO_WriteBit。'),
    'BitVal':       '要写入指定引脚的值，填 BitAction 枚举：Bit_RESET（写 0）或 Bit_SET（写 1），不能填 0/1 整数。',
    'RTC_DayLightSaving': '夏令时调整方向：RTC_DayLightSaving_ADD1H（时间 +1 小时，进入夏令时）或 _SUB1H（-1 小时，退出夏令时）。改完要用 RTC_StoreOperation 决定是否真的写进日历寄存器。',
    'RTC_TamperPin': '用哪个引脚作防拆输入：RTC_TamperPin_PC13 或 RTC_TamperPin_PI8。RTC_TamperPinSelection 就是用来选这个的。',
    'SDIO_ReadWaitMode': 'SDIO 读等待模式：SDIO_ReadWaitMode_CLK（靠时钟停止实现等待，SD 卡默认）或 SDIO_ReadWaitMode_DATA2（用 DATA2 线做等待信号，多见于 MMC）。跟卡类型有关，选错会读不出数据。',
    'EXTI_PinSourcex': (
        'EXTI 线的引脚来源，'
        '填 EXTI_PinSource0 ~ EXTI_PinSource15。'
        '须与端口源成对选择一条 EXTI 线，'
        '如 SYSCFG_EXTILineConfig(EXTI_PortSourceGPIOB, EXTI_PinSource0) 把 PB0 连到 EXTI 线 0。'
        '同一根 EXTI 线上只能有一个端口：PA0/PB0/PC0… 共用 EXTI0。'),
    'SYSCFG_MemoryRemap': (
        '把 0x00000000 处的存储区重映射到：SYSCFG_MemoryRemap_Flash（默认，'
        '用户 Flash）'
        '、'
        '_SystemFlash（系统存储器，'
        '内置 bootloader，'
        'IAP 升级时切换）'
        '、'
        '_SRAM（SRAM 启动调试）'
        '、'
        '_FSMC。'
        '切换后须配合跳转，'
        '否则程序跑飞。'),
    'USART_Address': '本机在多机通信中的节点地址，0 ~ 15（4 位），地址匹配的从机才被唤醒。仅在多处理器模式（USART_Mode 中的 Multiprocessor）下有效。',
    'USART_GuardTime': '智能卡模式下两个字符之间插入的保护时间，单位 1/16 位时间，0 ~ 255。仅在 USART 工作于 Smartcard 模式时有意义。',
    'PLLSAIR':      'RCC_PLLSAIConfig 的 VCO 分频系数 R，取值 2 ~ 7。F407 无此参数（PLLSAI 是 F429/F427 的时钟，供 LTDC/SAI 使用）。',
    'SAIx':         '要操作的 SAI 外设，填 SAI1。F407 无此参数（SAI 属 F429/F427）。',
    'SAI_Mono_StreoMode': 'SAI 单声道/立体声模式选择。官方宏名把 Stereo 写作 Streo，按此拼写。F407 无此参数。',
}

# ============================================================
#  2. 返回值中文归类（@retval 里高频英文 → 中文）
# ============================================================
# 官方 @retval 英文原文 → 简短中文（只在带该函数专属信息时保留）
RETVAL_CN = {
    'The Data conversion value.': 'ADC 转换结果。',
    '32-bit CRC': 'CRC 计算结果。',
    '32-bit random number.': '32 位随机数。',
    'The FLASH PC Read/Write Protection Option Bytes value': '选项字节 PCROP 的值。',
    'The FLASH Write Protection Option Bytes value': '选项字节 WRP 的值。',
    'The Error Correction Code (ECC) value.': 'ECC 校验值。',
    'LSB of the 9-bit CAN Transmit Error Counter.': 'CAN 发送错误计数器（9 位）的低字节。',
    'CAN Receive Error Counter.': 'CAN 接收错误计数器。',
    'Constant indicates initialization succeed which will be CAN_InitStatus_Failed or CAN_InitStatus_Success.':
        '取值 CAN_InitStatus_Success 或 CAN_InitStatus_Failed。',
    'NbMessage : which is the number of pending message.': '待处理报文个数。',
    'status of the requested mode which can be - CAN_ModeStatus_Failed: CAN failed entering the specific mode - CAN_ModeStatus_Success: CAN Succeed entering the specific mode':
        '取值 CAN_ModeStatus_Success（已进入）或 CAN_ModeStatus_Failed（未进入）。',
    'CAN_Sleep_Ok if sleep entered, CAN_Sleep_Failed otherwise.':
        '取值 CAN_Sleep_Ok（已进入睡眠）或 CAN_Sleep_Failed。',
    'CAN_TxStatus_Ok if the CAN driver transmits the message, CAN_TxStatus_Failed in an other case.':
        '取值 CAN_TxStatus_Ok（发送成功）或 CAN_TxStatus_Failed。',
    'CAN_WakeUp_Ok if sleep mode left, CAN_WakeUp_Failed otherwise.':
        '取值 CAN_WakeUp_Ok（已退出睡眠）或 CAN_WakeUp_Failed。',
    '8-bit value of the ID register': 'ID 寄存器值（8 位）。',
    'Last data entered into the output FIFO.': '最后写入输出 FIFO 的数据。',
    'The selected DAC channel data output value.': 'DAC 通道输出数据。',
    'Device identifier': '器件标识。',
    'Data register value': '数据寄存器值。',
    'The number of remaining data units in the current DMAy Streamx transfer.': '当前传输剩余数据单元数。',
    'The memory target number: 0 for Memory0 or 1 for Memory1.': '0 = Memory0，1 = Memory1。',
    'The FIFO filling state. - DMA_FIFOStatus_Less1QuarterFull: when FIFO is less than 1 quarter-full and not empty. - DMA_FIFOStatus_1QuarterFull: if more than 1 quarter-full. - DMA_FIFOStatus_HalfFull: if more than 1 half-full. - DMA_FIFOStatus_3QuartersFull: if more than 3 quarters-full. - DMA_FIFOStatus_Empty: when FIFO is empty - DMA_FIFOStatus_Full: when FIFO is full':
        '取值 DMA_FIFOStatus_Empty / _Less1QuarterFull / _1QuarterFull / _HalfFull / _3QuartersFull / _Full。',
    'The FIFO filling state. - SAI_FIFOStatus_Empty: when FIFO is empty - SAI_FIFOStatus_Less1QuarterFull: when FIFO is less than 1 quarter-full and not empty. - SAI_FIFOStatus_1QuarterFull: if more than 1 quarter-full. - SAI_FIFOStatus_HalfFull: if more than 1 half-full. - SAI_FIFOStatus_3QuartersFull: if more than 3 quarters-full. - SAI_FIFOStatus_Full: when FIFO is full':
        '取值 SAI_FIFOStatus_Empty / _Less1QuarterFull / _1QuarterFull / _HalfFull / _3QuartersFull / _Full。',
    'FLASH ReadOut Protection Status: - SET, when OB_RDP_Level_1 or OB_RDP_Level_2 is set - RESET, when OB_RDP_Level_0 is set':
        'SET = 选项字节为 OB_RDP_Level_1 或 OB_RDP_Level_2，RESET = OB_RDP_Level_0。',
    'The FLASH User Option Bytes values: IWDG_SW(Bit0), RST_STOP(Bit1) and RST_STDBY(Bit2).':
        '用户选项字节：IWDG_SW(Bit0)、RST_STOP(Bit1)、RST_STDBY(Bit2)。',
    'The FLASH BOR level: - OB_BOR_LEVEL3: Supply voltage ranges from 2.7 to 3.6 V - OB_BOR_LEVEL2: Supply voltage ranges from 2.4 to 2.7 V - OB_BOR_LEVEL1: Supply voltage ranges from 2.1 to 2.4 V - OB_BOR_OFF : Supply voltage ranges from 1.62 to 2.1 V':
        '取值 OB_BOR_LEVEL3（2.7~3.6V）/ LEVEL2（2.4~2.7V）/ LEVEL1（2.1~2.4V）/ OB_BOR_OFF（1.62~2.1V）。',
    'The FMC SDRAM bank mode status': 'SDRAM 存储区模式状态。',
    'GPIO input data port value.': '端口输入数据。',
    'The input port pin value.': '引脚输入电平。',
    'GPIO output data port value.': '端口输出数据。',
    'The output port pin value.': '引脚输出电平。',
    'The value of words already pushed into the IN FIFO.': '已写入 IN FIFO 的字数。',
    'The last event': '最近一次事件（I2C_EVENT 状态位组合）。',
    'The PEC value.': 'PEC 校验值。',
    'The value of the read register.': '读回的寄存器值。',
    'The value of the received data.': '接收到的数据。',
    'The received data.': '接收到的数据。',
    'Data received': '接收到的数据。',
    'The clock source used as system clock. The returned value can be one of the following: - 0x00: HSI used as system clock - 0x04: HSE used as system clock - 0x08: PLL used as system clock':
        '0x00 = HSI，0x04 = HSE，0x08 = PLL。',
    'RTC Alarm Sub seconds value.': '闹钟亚秒值。',
    'RTC Day Light Saving stored operation. - RTC_StoreOperation_Reset - RTC_StoreOperation_Set':
        '取值 RTC_StoreOperation_Set 或 RTC_StoreOperation_Reset。',
    'RTC current Calendar Sub seconds value.': '当前日历亚秒值。',
    'RTC current timestamp Sub seconds value.': '当前时间戳亚秒值。',
    'The RTC WakeUp Counter value.': '唤醒计数器值。',
    'Returns the command index of the last command response received.': '最近一次命令响应的命令索引。',
    'Number of remaining data bytes to be transferred': '剩余待传输字节数。',
    'Remaining number of words.': 'FIFO 中剩余字数。',
    'Power status of the controller. The returned value can be one of the following values: - 0x00: Power OFF - 0x02: Power UP - 0x03: Power ON':
        '0x00 = 断电，0x02 = 上电中，0x03 = 已上电。',
    'The Corresponding response register value.': '对应响应寄存器的值。',
    'The selected CRC register value..': '选定的 CRC 寄存器值。',
    'The CRC Polynomial register value.': 'CRC 多项式寄存器值。',
    'Capture Compare 1 Register value.': 'CCR1 寄存器值。',
    'Capture Compare 2 Register value.': 'CCR2 寄存器值。',
    'Capture Compare 3 Register value.': 'CCR3 寄存器值。',
    'Capture Compare 4 Register value.': 'CCR4 寄存器值。',
    'Counter Register value': '计数器值（CNT）。',
    'Prescaler Register value.': '预分频器值（PSC）。',
}

# 官方 @retval 里"函数专属"的语义，按函数名给中文
RETVAL_FN = {
    'RTC_CoarseCalibCmd': 'SUCCESS = 粗校准已使能或失能。',
    'RTC_CoarseCalibConfig': 'SUCCESS = 粗校准已配置。',
    'RTC_EnterInitMode': 'SUCCESS = RTC 已进入初始化模式。',
    'RTC_Init': 'SUCCESS = RTC 寄存器已初始化。',
    'RTC_RefClockCmd': 'SUCCESS = 参考时钟检测已使能。',
    'RTC_SetDate': 'SUCCESS = 日期寄存器已配置。',
    'RTC_SetTime': 'SUCCESS = 时间寄存器已配置。',
    'RTC_SmoothCalibConfig': 'SUCCESS = 校准寄存器已配置。',
    'RTC_SynchroShiftConfig': 'SUCCESS = 移位寄存器已配置。',
    'RTC_WaitForSynchro': 'SUCCESS = RTC 寄存器已同步。',
    'RCC_WaitForHSEStartUp': 'SUCCESS = HSE 已稳定可用。',
    'I2C_CheckEvent': 'SUCCESS = 当前事件与给定的 I2C_EVENT 相符。',
    'CAN_GetLastErrorCode':
        '返回 CAN_ERRORCODE_* 错误码（NoErr / StuffErr / FormErr / ACKErr / BitRecessiveErr / BitDominantErr / CRCErr / SoftwareSetErr）。',
}

# 官方 @retval 只是重复 base 已有内容 → 整条丢弃，不追加
RETVAL_RE = re.compile(
    r'^(The (new|current) state of'
    r'|An ErrorStatus enumeration value: - SUCCESS: Operation done'
    r'|An ErrorStatus enumeration value: - SUCCESS: digest computation done'
    r'|Current state of the '
    r'|FLASH Status: The returned value can be:)'
)

# 自检：官方 @retval 原文未收录进上面两张表时记下来
UNKNOWN_RETVAL = set()

# 英文描述里高频片段的替换词典（把官方英文注释中文化）
# 只做"整句模式"和"安全词组"替换，不做逐词硬删，否则会把句子拆烂。
EN_PHRASE = [
    (r'^Specifies the (.+?)\.?$', r'指定\1'),
    (r'^This parameter can be a value of @ref (\w+)', r'取值见 \1 枚举'),
    (r'^This parameter can be an enumerator of @ref (\w+) enumeration', r'取值见 \1 枚举'),
    (r'^This parameter can be one of the following values?:?', r'可取以下值'),
    (r'^This parameter can be any combination of the following values?:?', r'可取以下值（可用按位或组合，例如 A | B）'),
    (r'^where x can be (.+?) to select', r'其中 x 取 \1，选择'),
    # 词组级
    (r'can be one of the following values?:?', '可取以下值'),
    (r'can be any combination of the following values?:?', '可取以下值（可用按位或组合）'),
    (r'following values', '以下取值'),
    (r'one of the following', '以下取值之一'),
    (r'pointer to (?:a |an )?(.+?) structure', r'指向 \1 结构体的指针'),
    (r'\bnew state of\b', '的新状态：'),
    (r'\bthe new state of\b', '的新状态：'),
    (r'\bspecified\b', '指定的'),
    (r'\bthis parameter\b', '该参数'),
    (r'\bmust range from\b', '取值范围'),
    (r'\bis not used\b', '（此函数未使用该参数）'),
    (r'\bthat contains\b', '其中包含'),
    (r'\bwhich will be\b', '将被'),
    (r'^\s*-\s*', '· '),
    (r'\bNumber of\b', '个数：'),
    (r'\bSelects\b', '选择'),
    (r'\bConfigures\b', '配置'),
    (r'\bInitializes\b', '初始化'),
    (r'\bEnables or disables\b', '使能或关闭'),
    (r'\bSpecifies\b', '指定'),
]

# ============================================================
#  3. 解析
# ============================================================
proto_re = re.compile(
    r'^\s*((?:void|uint8_t|uint16_t|uint32_t|int32_t|ITStatus|FlagStatus|ErrorStatus|FunctionalState|FLASH_Status))\s+'
    r'([A-Z]\w+)\s*\(([^;]*?)\)\s*;', re.M)
protos = {}
for h in glob.glob(os.path.join(INC, '*.h')):
    t = io.open(h, encoding='utf-8', errors='ignore').read()
    for m in proto_re.finditer(t):
        ret, name, args = m.group(1), m.group(2), m.group(3)
        if name in protos:
            continue
        protos[name] = (ret, re.sub(r'\s+', ' ', args).strip())
print('函数原型:', len(protos))

def split_params(args):
    """把 "uint32_t GPIOx, uint8_t Key[8]" 拆成 [(type, name), ...]
    要处理数组形式：uint8_t Key[8] / uint32_t pBuffer[]，
      参数名应是 Key / pBuffer，不是整串 "uint8_t Key[8]"。"""
    args = args.strip()
    if not args or args == 'void':
        return []
    out = []
    for a in args.split(','):
        a = a.strip()
        if not a:
            continue
        # 先剥掉数组维度
        arr = ''
        am = re.match(r'^(.*?)(\[[^\]]*\])\s*$', a)
        if am:
            a, arr = am.group(1).strip(), am.group(2)
        m = re.match(r'(.+?)\s*(\*+)?\s*([A-Za-z_]\w*)$', a)
        if m:
            ty = (m.group(1) + (m.group(2) or '')).strip()
            out.append((ty, m.group(3)))
        else:
            out.append(('', a))
    return out

doc_of = {}
doc_re = re.compile(r'/\*\*\s*(.*?)\*/', re.S)
for c in glob.glob(os.path.join(SRC, '*.c')):
    t = io.open(c, encoding='utf-8', errors='ignore').read()
    for m in doc_re.finditer(t):
        blk = m.group(1)
        after = t[m.end():m.end()+400]
        fm = re.search(r'\b([A-Z]\w+)\s*\(', after)
        if not fm:
            continue
        fn = fm.group(1)
        if fn in doc_of:
            continue
        brief = re.search(r'@brief\s+(.*?)(?=\n\s*\*\s*@|\Z)', blk, re.S)
        retv = re.findall(r'@retval\s+([^\n]*(?:\n\s*\*\s{3,}(?!\s*@)[^\n]*)*)', blk)
        params = re.findall(r'@param\s+(\w+)\s*:?\s*([^\n]*(?:\n\s*\*\s{3,}(?!\s*@)[^\n]*)*)', blk)
        def clean(s):
            s = re.sub(r'\n\s*\*\s*', ' ', s)
            return re.sub(r'\s+', ' ', s).strip()
        doc_of[fn] = {
            'brief': clean(brief.group(1)) if brief else '',
            'retval': [clean(r) for r in retv],
            'params': [(p, clean(d)) for p, d in params],
        }
print('有文档的函数:', len(doc_of))

def extract_values(en):
    """从英文描述里抓"可选值"：只要形如 Xxx_Yyy 或带数字的宏名。
    必须含数字，这样能滤掉 #if defined(STM32F405xx) 这类预处理条件，
      因为它们全部是纯字母，而真正的枚举/位掩码宏（RTC_HourFormat_24、
      FLASH_Sector_0、GPIO_Pin_0、USART_WordLength_8b）都带数字。"""
    toks = re.findall(r'\b([A-Z][A-Za-z0-9_]{2,})\b', en)
    vals = [t for t in toks
            if re.search(r'\d', t)                       # 必须含数字
            and not t.endswith('Struct')
            and not re.match(r'^STM32F\d', t)]            # 排除芯片型号
    seen, out = set(), []
    for v in vals:
        if v not in seen:
            seen.add(v); out.append(v)
    return out[:12]

def cn_param(pname, en):
    """参数中文解释：优先中文字典；否则从官方英文描述里提炼出一句能读懂的中文。
    策略（按优先级）：
      1) 命中 PARAM_CN 字典 → 直接用（覆盖约 73% 的参数出现次数）
      2) 官方描述里带 @ref Xxx → 提示"取值见 Xxx 枚举"，并保留说明前半句
      3) 官方描述前半句做词组级中文化
      4) 都没有 → 给出明确提示，不留空白
    """
    if pname in PARAM_CN:
        return PARAM_CN[pname]

    # 官方注释里偶有大小写差异（CRYP_IT vs CRYP_IT、DACy vs DAC_Channel 等），
    # 做一次大小写不敏感 + 去掉 y 的通配匹配，尽量命中字典。
    low = pname.lower()
    for k, v in PARAM_CN.items():
        if k.lower() == low:
            return v
    for k, v in PARAM_CN.items():
        if k.lower().replace('y', 'x') == low.replace('y', 'x'):
            return v

    raw = en or ''
    # 取 @ref 指向的枚举名（官方用它说明可选值）
    refs = re.findall(r'@ref\s+(\w+)', raw)
    # 去掉 @ref 片段本身，只留人类可读的前半句
    head = re.split(r'This parameter can be|This parameter is|@ref', raw)[0]
    head = re.sub(r'@ref\s*\w+', '', head)
    head = re.sub(r'\s+', ' ', head).strip(' .;:')

    # 前半句做词组级中文化
    for pat, rep in EN_PHRASE:
        head = re.sub(pat, rep, head, flags=re.I).strip()

    parts = []
    if head and len(head) >= 4:
        # 中文占比过低时补一个括注，避免出现"半句英文"
        cn = len(re.findall(r'[\u4e00-\u9fff]', head))
        if cn < 4 and len(head) > 10:
            parts.append('（官方原文：%s）' % head[:110])
        else:
            parts.append(head)
    if refs:
        parts.append('取值见 %s 枚举。' % ' / '.join(dict.fromkeys(refs)))
    if not parts:
        parts.append('（官方未提供说明）')
    return '　'.join(parts)

# 各返回类型的标准中文说明（官方 @retval 只在其上做补充）
BASE_RET = {
    'ErrorStatus': '返回 ErrorStatus：SUCCESS = 操作成功，ERROR = 失败。',
    'FLASH_Status': ('返回 FLASH_Status 枚举：FLASH_COMPLETE = 成功，FLASH_BUSY = 忙，'
                     'FLASH_ERROR_WRP = 写保护错误，FLASH_ERROR_PROGRAM = 编程失败，'
                     'FLASH_ERROR_RD = 读保护错误，'
                     'FLASH_ERROR_PGAERR / _PGPERR / _PGSERR = 对齐/并行/扇区错误，'
                     'FLASH_ERROR_OPERATION = 操作错误，FLASH_TIMEOUT = 超时。'),
    'FlagStatus': '返回 SET（标志成立）/ RESET（标志不成立）。',
    'ITStatus': '返回 SET（该中断源已触发）/ RESET（未触发）。',
    'FunctionalState': '返回 ENABLE / DISABLE。',
}

def cn_retval(fn, ret, rvs):
    """返回值中文说明。

    官方 @retval 英文原文只作"补充信息"来源：能对上简短中文就追加一句；
    与 base 重复（各类 "The new state of XXX (SET or RESET)."、通用 ErrorStatus
    说明）就整条丢弃，不再以「（官方注释：英文原文）」的形式挂在后面。
    """
    if ret == 'void':
        return '无返回值（void）。'
    base = BASE_RET.get(ret)
    extra = RETVAL_FN.get(fn, '')
    if not extra:
        j = re.sub(r'\s+', ' ', ' / '.join(rvs).replace('None', '无')).strip()
        if j and j != '无':
            extra = RETVAL_CN.get(j, '')
            if not extra and not RETVAL_RE.match(j):
                UNKNOWN_RETVAL.add(j)
    if not extra:
        return base if base else '返回 %s。' % ret
    if base:
        return base + '其中 ' + extra
    if extra.startswith('返回'):
        return extra
    return '返回 %s：%s' % (ret, extra)

# ============================================================
#  4. 生成 data_doc.js
# ============================================================
lines = []
lines.append('/* =============================================================')
lines.append(' *  data_doc.js —— 自动收录条目的「参数说明 / 返回值」补全')
lines.append(' * -------------------------------------------------------------')
lines.append(' *  由 gen_doc.py 从 Keil DFP 的 SPL 源码生成（勿手改，重跑即可）')
lines.append(' *  来源：inc/stm32f4xx_*.h（权威签名）+ src/stm32f4xx_*.c（Doxygen 注释）')
lines.append(' *')
lines.append(' *  结构：STD_DOC[函数名] = {')
lines.append(' *      r  : 返回值中文说明')
lines.append(' *      b  : 用途（官方 @brief）')
lines.append(' *      en : 英文原文（可选，鼠标悬停或折叠显示）')
lines.append(' *      a  : [[参数名, 类型, 中文解释, 可选值], ...]')
lines.append(' *  }')
lines.append(' * ============================================================= */')
lines.append('')
lines.append('const STD_DOC = {')

n_ok = 0
for fn in sorted(protos):
    ret, args = protos[fn]
    ps = split_params(args)
    d = doc_of.get(fn, {'brief': '', 'retval': [], 'params': []})
    pmap = dict(d['params'])
    entry = []
    for ty, pn in ps:
        en = pmap.get(pn, '')
        cn = cn_param(pn, en)
        vals = extract_values(en)
        entry.append([pn, ty, cn, ' / '.join(vals)])
    rtxt = cn_retval(fn, ret, d['retval'])
    brief = d['brief']
    lines.append('"%s":{"r":"%s","b":"%s","a":[%s]},' % (
        fn,
        rtxt.replace('\\', '\\\\').replace('"', '\\"'),
        brief.replace('\\', '\\\\').replace('"', '\\"')[:200],
        ','.join('["%s","%s","%s","%s"]' % (
            p[0].replace('"', '\\"'),
            p[1].replace('"', '\\"'),
            p[2].replace('\\', '\\\\').replace('"', '\\"'),
            p[3].replace('"', '\\"')) for p in entry)
    ))
    n_ok += 1
lines.append('};')
lines.append('')
io.open(OUT, 'w', encoding='utf-8', newline='').write('\n'.join(lines))
print('\n已生成:', OUT)
print('收录函数:', n_ok)
print('文件大小: %.0f KB' % (os.path.getsize(OUT) / 1024.0))

have_param = sum(1 for fn in protos if doc_of.get(fn, {}).get('params'))
print('其中有官方 @param 文档的:', have_param)
print('参数名命中中文字典的比例: %.1f%%' % (
    sum(1 for fn in protos for _, pn in split_params(protos[fn][1]) if pn in PARAM_CN) * 100.0 /
    max(1, sum(1 for fn in protos for _ in split_params(protos[fn][1])))))

# 自检：列出"解释偏弱"的参数（走了官方原文兜底，或解释里没有汉字），供人工补字典
weak = []
for fn in sorted(protos):
    ret, args = protos[fn]
    d = doc_of.get(fn, {'params': []})
    pmap = dict(d['params'])
    for ty, pn in split_params(args):
        txt = cn_param(pn, pmap.get(pn, ''))
        # 只把两种情形算"弱"：
        #   ① 走了官方原文兜底（说明字典没覆盖，也没提炼出中文）
        #   ② 解释里一个汉字都没有（纯英文）
        #   不用"长度 < 12"判断：像"密钥数据。""输入数据。"这种短而准确的解释是合格的。
        if ('官方原文' in txt) or ('官方未提供' in txt) or not re.search(r'[\u4e00-\u9fff]', txt):
            weak.append((fn, pn, txt[:70]))
if weak:
    print('\n【建议补字典】解释偏弱的参数 %d 个（前 25）:' % len(weak))
    for fn, pn, txt in weak[:25]:
        print('   %-26s %-22s %s' % (fn, pn, txt))
else:
    print('\n所有参数解释均达到可读标准')

# 自检：官方 @retval 英文原文没被 RETVAL_CN / RETVAL_FN / RETVAL_RE 覆盖到的，列出来补表
if UNKNOWN_RETVAL:
    print('\n【返回值原文未收录】%d 条（当前会退化为只写返回类型）:' % len(UNKNOWN_RETVAL))
    for x in sorted(UNKNOWN_RETVAL)[:25]:
        print('   %s' % x)
else:
    print('返回值官方原文全部有对应中文或已判定为重复')
