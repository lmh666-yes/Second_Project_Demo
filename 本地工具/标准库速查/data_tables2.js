/* =============================================================
 *  data_tables2.js —— 速查工具「查找表」模块（增补表）
 *  手写整理，供初学者按“结构体字段 / 引脚 / 通道 / 频率 / 地址”速查。
 *  载入时把 STD_TABLES2 追加进 data_tables.js 的 STD_TABLES，
 *  并给“复用功能表”补上“对应外设”列（DFP 只能提取到 AF 宏本身）。
 *  数据以 STM32F407（RM0090 / DS8626）为准；表中“本项目”列指
 *  本仓库 S_N_sys 的两块板（板1 检测端 ZET6、板2 数据通信端 ZGT6）。
 * ============================================================= */

/* ---------------- 1) 结构体成员速查表 ---------------- */
const T_STRUCT = {
    m: "查找表", t: "table",
    n: "结构体成员速查表（SPL Init 结构体逐字段）",
    p: "每个 xxx_InitTypeDef 的字段含义、取值范围与搭配规则；配外设时先查这张表，再填 xxx_Init(外设, &结构体)。",
    d: "结构体字段表：按结构体名或字段名搜（如 GPIO_OType / DMA_MemoryDataSize），逐字段给出取值与含义：",
    h: ["结构体", "字段", "常用取值", "含义 / 注意"],
    k: "结构体 InitTypeDef 字段 成员 GPIO_InitTypeDef NVIC_InitTypeDef USART_InitTypeDef TIM_TimeBaseInitTypeDef TIM_OCInitTypeDef DMA_InitTypeDef ADC_InitTypeDef I2C_InitTypeDef SPI_InitTypeDef EXTI_InitTypeDef RTC FSMC CAN",
    a: [
        ["GPIO_InitTypeDef", "GPIO_Pin", "GPIO_Pin_0 ~ GPIO_Pin_15（可用 | 组合）", "要配置的引脚；多个引脚按位或"],
        ["GPIO_InitTypeDef", "GPIO_Mode", "GPIO_Mode_IN / _OUT / _AF / _AN", "输入 / 输出 / 复用功能 / 模拟输入（ADC 引脚用 _AN）"],
        ["GPIO_InitTypeDef", "GPIO_Speed", "GPIO_Speed_2MHz / 25MHz / 50MHz / 100MHz", "输出翻转速率；够用即可，速率越高边沿越陡、噪声越大"],
        ["GPIO_InitTypeDef", "GPIO_OType", "GPIO_OType_PP / GPIO_OType_OD", "推挽 / 开漏；I2C 与“线与”总线必须开漏"],
        ["GPIO_InitTypeDef", "GPIO_PuPd", "GPIO_PuPd_NOPULL / _UP / _DOWN", "内部上拉 / 下拉 / 不接；I2C 引脚要有上拉（外部或内部）"],

        ["NVIC_InitTypeDef", "NVIC_IRQChannel", "某某_IRQn（如 USART1_IRQn）", "要开的中断通道，名称见“中断向量表”"],
        ["NVIC_InitTypeDef", "NVIC_IRQChannelPreemptionPriority", "0 ~ (2^抢占位数 − 1)", "抢占优先级，数值小者优先；分组决定它有几位"],
        ["NVIC_InitTypeDef", "NVIC_IRQChannelSubPriority", "0 ~ (2^子位数 − 1)", "子优先级，抢占相同时比大小，不能打断"],
        ["NVIC_InitTypeDef", "NVIC_IRQChannelCmd", "ENABLE / DISABLE", "使能该中断通道"],

        ["USART_InitTypeDef", "USART_BaudRate", "9600 / 115200 等", "波特率，库按当前 PCLK 自动算 BRR"],
        ["USART_InitTypeDef", "USART_WordLength", "USART_WordLength_8b / _9b", "数据位；带校验时常用 9b（校验位占 1 位）"],
        ["USART_InitTypeDef", "USART_StopBits", "USART_StopBits_1 / _0_5 / _2 / _1_5", "停止位，两端必须一致"],
        ["USART_InitTypeDef", "USART_Parity", "USART_Parity_No / _Even / _Odd", "奇偶校验；无校验用 No"],
        ["USART_InitTypeDef", "USART_Mode", "USART_Mode_Rx | USART_Mode_Tx", "收 / 发；只收就只填 Rx"],
        ["USART_InitTypeDef", "USART_HardwareFlowControl", "USART_HardwareFlowControl_None / RTS / CTS / RTS_CTS", "硬件流控；普通串口与模块通信一般用 None"],

        ["TIM_TimeBaseInitTypeDef", "TIM_Prescaler", "0 ~ 65535", "预分频，实际分频 = 值 + 1；计数时钟 = 定时器时钟 ÷（值+1）"],
        ["TIM_TimeBaseInitTypeDef", "TIM_CounterMode", "TIM_CounterMode_Up / _Down / _CenterAligned1/2/3", "向上 / 向下 / 中央对齐计数"],
        ["TIM_TimeBaseInitTypeDef", "TIM_Period", "16 位定时器 0~65535；TIM2/TIM5 为 0~2^32−1", "自动重装值 ARR，溢出周期 =（ARR+1）个计数"],
        ["TIM_TimeBaseInitTypeDef", "TIM_ClockDivision", "TIM_CKD_DIV1 / DIV2 / DIV4", "数字滤波采样时钟分频，不改变计数频率"],
        ["TIM_TimeBaseInitTypeDef", "TIM_RepetitionCounter", "0 ~ 255（高级定时器）", "重复计数；TIM1/TIM8 用，普通定时器填 0"],

        ["TIM_OCInitTypeDef", "TIM_OCMode", "TIM_OCMode_PWM1 / PWM2 / Timing / Active / Inactive / Toggle", "输出比较模式；PWM 常用 PWM1"],
        ["TIM_OCInitTypeDef", "TIM_OutputState", "TIM_OutputState_Enable / Disable", "是否把比较结果输出到引脚"],
        ["TIM_OCInitTypeDef", "TIM_Pulse", "0 ~ ARR", "比较值 CCR；占空比 = Pulse ÷（ARR+1）"],
        ["TIM_OCInitTypeDef", "TIM_OCPolarity", "TIM_OCPolarity_High / Low", "有效电平极性"],
        ["TIM_OCInitTypeDef", "TIM_OutputNState / TIM_OCNPolarity / TIM_OCIdleState", "Enable/Disable、High/Low", "互补通道与空闲电平；仅 TIM1/TIM8 有效"],
        ["TIM_OCInitTypeDef", "TIM_OCFast / TIM_OCClear", "Enable / Disable", "快速使能与输出清除，进阶用法"],

        ["DMA_InitTypeDef", "DMA_Channel", "DMA_Channel_0 ~ DMA_Channel_7", "通道号必须与外设请求匹配，见“DMA 请求映射表”"],
        ["DMA_InitTypeDef", "DMA_PeripheralBaseAddr", "(uint32_t)&外设->DR", "外设端地址，如 (uint32_t)&USART1->DR"],
        ["DMA_InitTypeDef", "DMA_Memory0BaseAddr", "(uint32_t)缓冲区", "内存端地址"],
        ["DMA_InitTypeDef", "DMA_DIR", "DMA_DIR_PeripheralToMemory / MemoryToPeripheral", "收数据用外设到内存，发数据用内存到外设"],
        ["DMA_InitTypeDef", "DMA_BufferSize", "1 ~ 65535", "传输个数（不是字节数），项宽由对齐字段决定"],
        ["DMA_InitTypeDef", "DMA_PeripheralInc / DMA_MemoryInc", "DMA_PeripheralInc_Disable / MemoryInc_Enable 等", "地址是否自增；外设寄存器一般不自增，内存缓冲一般自增"],
        ["DMA_InitTypeDef", "DMA_PeripheralDataSize / DMA_MemoryDataSize", "DMA_PeripheralDataSize_Byte / _HalfWord / _Word", "两端数据宽度要一致，否则字节错位"],
        ["DMA_InitTypeDef", "DMA_Mode", "DMA_Mode_Normal / DMA_Mode_Circular", "单次 / 循环；连续采集用循环"],
        ["DMA_InitTypeDef", "DMA_Priority", "DMA_Priority_Low / Medium / High / VeryHigh", "多条流同时请求时的仲裁优先级"],
        ["DMA_InitTypeDef", "DMA_FIFOMode / DMA_FIFOThreshold / DMA_MemoryBurst / DMA_PeripheralBurst", "FIFOMode_Disable / Enable 等", "FIFO 与突发，单次小数据量常用 Disable"],

        ["ADC_CommonInitTypeDef", "ADC_Mode", "ADC_Mode_Independent / _Dual... / _Triple...", "单 ADC / 多重 ADC 模式，本项目用 Independent"],
        ["ADC_CommonInitTypeDef", "ADC_Prescaler", "ADC_Prescaler_Div2 / Div4 / Div6 / Div8", "ADC 时钟分频，ADC 时钟上限 36 MHz（PCLK2=84 MHz 时至少 Div4）"],
        ["ADC_CommonInitTypeDef", "ADC_DMAAccessMode", "ADC_DMAAccessMode_Disabled / Mode1 / Mode2 / Mode3", "多重模式下的 DMA 方式"],
        ["ADC_CommonInitTypeDef", "ADC_TwoSamplingDelay", "ADC_TwoSamplingDelay_5Cycles ~ 20Cycles", "多重模式两次采样间隔"],
        ["ADC_InitTypeDef", "ADC_Resolution", "ADC_Resolution_12b / 10b / 8b / 6b", "分辨率；12 位满量程 4095"],
        ["ADC_InitTypeDef", "ADC_ScanConvMode", "ENABLE / DISABLE", "多通道扫描；单通道用 DISABLE"],
        ["ADC_InitTypeDef", "ADC_ContinuousConvMode", "ENABLE / DISABLE", "连续转换；单次触发用 DISABLE"],
        ["ADC_InitTypeDef", "ADC_ExternalTrigConv / ADC_ExternalTrigConvEdge", "ADC_ExternalTrigConv_T1_CC1 等；边沿 None / Rising / Falling / Both", "外部触发源与边沿；软件触发填 None/Rising"],
        ["ADC_InitTypeDef", "ADC_DataAlign", "ADC_DataAlign_Right / Left", "数据对齐，一般右对齐"],
        ["ADC_InitTypeDef", "ADC_NbrOfConversion", "1 ~ 16", "规则序列长度，要 ≥ 实际转换通道数"],
        ["ADC_InitTypeDef", "ADC_Channel（规则通道配置）", "ADC_Channel_0 ~ ADC_Channel_18", "通道号对应的引脚见“ADC 通道映射表”"],

        ["I2C_InitTypeDef", "I2C_ClockSpeed", "100000 / 400000", "标准模式 100 kHz / 快速模式 400 kHz"],
        ["I2C_InitTypeDef", "I2C_Mode", "I2C_Mode_I2C / SMBusDevice / SMBusHost", "普通器件用 I2C"],
        ["I2C_InitTypeDef", "I2C_DutyCycle", "I2C_DutyCycle_2 / I2C_DutyCycle_16_9", "快速模式占空比"],
        ["I2C_InitTypeDef", "I2C_OwnAddress1", "7 位地址左移一位后的值", "本机地址；即使是主机也要填一个非 0 值"],
        ["I2C_InitTypeDef", "I2C_Ack", "I2C_Ack_Enable / Disable", "收到一个字节后是否应答"],
        ["I2C_InitTypeDef", "I2C_AcknowledgedAddress", "I2C_AcknowledgedAddress_7bit / 10bit", "地址位宽，绝大多数器件是 7 位"],

        ["SPI_InitTypeDef", "SPI_Direction", "SPI_Direction_2Lines_FullDuplex / 2Lines_RxOnly / 1Line 等", "线制；常用 2 线全双工"],
        ["SPI_InitTypeDef", "SPI_Mode", "SPI_Mode_Master / SPI_Mode_Slave", "主 / 从；MCU 一般做主机"],
        ["SPI_InitTypeDef", "SPI_DataSize", "SPI_DataSize_8b / SPI_DataSize_16b", "数据位宽"],
        ["SPI_InitTypeDef", "SPI_CPOL / SPI_CPHA", "SPI_CPOL_Low / High；SPI_CPHA_1Edge / 2Edge", "时钟极性与相位，组合出模式 0~3；要按从机器件手册选"],
        ["SPI_InitTypeDef", "SPI_NSS", "SPI_NSS_Soft / SPI_NSS_Hard", "片选由软件控制（普通 GPIO）还是硬件 NSS"],
        ["SPI_InitTypeDef", "SPI_BaudRatePrescaler", "SPI_BaudRatePrescaler_2 ~ _256", "分频，SCK = PCLK ÷ 分频值"],
        ["SPI_InitTypeDef", "SPI_FirstBit", "SPI_FirstBit_MSB / LSB", "先发高位还是低位；多数器件是 MSB"],
        ["SPI_InitTypeDef", "SPI_CRCPolynomial", "默认 7", "CRC 多项式；不用 CRC 时保持默认"],

        ["EXTI_InitTypeDef", "EXTI_Line", "EXTI_Line0 ~ EXTI_Line22", "中断线；0~15 是引脚线，16~22 是内部源，见 EXTI 表"],
        ["EXTI_InitTypeDef", "EXTI_Mode", "EXTI_Mode_Interrupt / EXTI_Mode_Event", "产生中断还是仅产生事件"],
        ["EXTI_InitTypeDef", "EXTI_Trigger", "EXTI_Trigger_Rising / Falling / Rising_Falling", "触发边沿"],
        ["EXTI_InitTypeDef", "EXTI_LineCmd", "ENABLE / DISABLE", "是否使能该线"],

        ["RTC_InitTypeDef", "RTC_HourFormat", "RTC_HourFormat_24 / RTC_HourFormat_12", "24 小时制或 12 小时制"],
        ["RTC_InitTypeDef", "RTC_AsynchPrediv / RTC_SynchPrediv", "常用 0x7F / 0xFF", "异步 / 同步分频；两者一起把 32768 Hz 分成 1 Hz"],
        ["RTC_TimeTypeDef", "RTC_Hours / RTC_Minutes / RTC_Seconds / RTC_H12", "0 ~ 23 / 0 ~ 59 / 0 ~ 59", "时间字段；写后要 RTC_SetTime 并等 RSF"],
        ["RTC_DateTypeDef", "RTC_Year / RTC_Month / RTC_Date / RTC_WeekDay", "0 ~ 99 / 1 ~ 12 / 1 ~ 31 / 1 ~ 7", "日期字段；星期是把日期换算成 1~7 的星期号"],

        ["FSMC_NORSRAMInitTypeDef", "FSMC_Bank", "FSMC_Bank1_NORSRAM1 ~ FSMC_Bank1_NORSRAM4", "用哪块片选；TFT-LCD 常用 NE4 = NORSRAM4"],
        ["FSMC_NORSRAMInitTypeDef", "FSMC_DataAddressMux", "FSMC_DataAddressMux_Disable", "地址/数据是否复用，LCD 类接口用 Disable"],
        ["FSMC_NORSRAMInitTypeDef", "FSMC_MemoryType", "FSMC_MemoryType_SRAM / NOR / PSRAM / CRAM", "存储器类型；ILI9481 这类并口屏用 SRAM 模式"],
        ["FSMC_NORSRAMInitTypeDef", "FSMC_MemoryDataWidth", "FSMC_MemoryDataWidth_8b / 16b", "总线宽度；并口屏常用 16b"],
        ["FSMC_NORSRAMInitTypeDef", "FSMC_WriteOperation / FSMC_ExtendedMode / FSMC_WriteBurst", "ENABLE / DISABLE", "写使能、读写分别时序、写突发"],
        ["FSMC_NORSRAMTimingInitTypeDef", "FSMC_AddressSetupTime / FSMC_AddressHoldTime / FSMC_DataSetupTime", "单位 HCLK 周期", "地址建立 / 保持、数据建立时间；太快会花屏，太慢会闪"],
        ["FSMC_NORSRAMTimingInitTypeDef", "FSMC_BusTurnAroundDuration / FSMC_CLKDivision / FSMC_DataLatency / FSMC_AccessMode", "0~15 / 1~15 / 0~15 / 模式 A~D", "总线周转、时钟分频、数据延迟与访问模式"],

        ["CAN_InitTypeDef", "CAN_TTCM / CAN_ABOM / CAN_AWUM / CAN_NART / CAN_RFLM / CAN_TXFP", "ENABLE / DISABLE", "时间触发、自动离线管理、自动唤醒、自动重传、锁定 FIFO、发送优先级"],
        ["CAN_InitTypeDef", "CAN_Prescaler", "1 ~ 1024", "波特率分频；位时间 = 1 + BS1 + BS2 个 tq，波特率 = PCLK1 ÷ 分频 ÷ 位时间"],
        ["CAN_InitTypeDef", "CAN_Mode", "CAN_Mode_Normal / Loopback / Silent / Silent_Loopback", "正常 / 回环（自测） / 静默（只听） / 静默回环"],
        ["CAN_InitTypeDef", "CAN_SJW / CAN_BS1 / CAN_BS2", "1~4 / 1~16 / 1~8", "同步跳转宽度与位段时间段；三者决定采样点"]
    ]
};

/* ---------------- 2) 外设引脚映射表 ---------------- */
const T_PINS = {
    m: "查找表", t: "table",
    n: "外设引脚映射表（USART / I2C / SPI / CAN / USB / ETH / SDIO）",
    p: "配通信外设时先查“信号 → 引脚 → AF 号”，再写 GPIO_PinAFConfig(端口, GPIO_PinSourceN, GPIO_AFx)；“本项目”列是实际接线。",
    d: "外设引脚表：搜外设名（USART3 / I2C1 / SPI1 / CAN1）、引脚名（PB10 / PC6）或 AF 号都能定位：",
    h: ["外设", "信号", "引脚（可选组）", "AF", "备注（本项目用法）"],
    k: "引脚映射 USART UART I2C SPI CAN USB ETH SDIO AF 复用 PB10 PC6 PC7 PA2 PA3 PB8 PB9",
    a: [
        ["USART1", "TX / RX / CK / CTS / RTS", "PA9 / PA10 / PA8 / PA11 / PA12", "AF7", "板1、板2 的调试串口（TX=PA9, RX=PA10, 115200）"],
        ["USART1", "TX / RX", "PB6 / PB7", "AF7", "与 I2C1 复用同脚，二者只能选一"],
        ["USART2", "TX / RX / CK / CTS / RTS", "PA2 / PA3 / PA4 / PA5 / PA6", "AF7", "板2 到 PC 的转发串口（TX=PA2, RX=PA3, 115200）"],
        ["USART2", "TX / RX", "PD5 / PD6", "AF7", "与 FSMC_NWE / FSMC_NOE 同脚，不能同时用"],
        ["USART3", "TX / RX / CK / CTS / RTS", "PB10 / PB11 / PB12 / PB13 / PB14", "AF7", "板1 LoRa（TX=PB10, RX=PB11, 9600）；板2 的 ESP8266 也走这组"],
        ["USART3", "TX / RX", "PC10 / PC11 / PD8 / PD9", "AF7", "重映射组，选另一组时引脚要换"],
        ["UART4", "TX / RX", "PA0 / PA1 或 PC10 / PC11", "AF8", "本项目未使用"],
        ["UART5", "TX / RX", "PC12 / PD2", "AF8", "本项目未使用"],
        ["USART6", "TX / RX / CK", "PC6 / PC7 / PC8", "AF8", "板2 LoRa（TX=PC6, RX=PC7, 9600）"],

        ["I2C1", "SCL / SDA", "PB6 / PB7 或 PB8 / PB9", "AF4", "板1 用 PB8/PB9：OLED、24C02、MPU6050、SHT30、BMP280、BH1750、CCS811 共用"],
        ["I2C2", "SCL / SDA", "PB10 / PB11 或 PF1 / PF0 或 PH4 / PH5", "AF4", "板2 用 PB10/PB11：AT24C02 参数存储"],
        ["I2C3", "SCL / SDA", "PA8 / PC9", "AF4", "本项目未使用"],

        ["SPI1", "SCK / MISO / MOSI / NSS", "PA5 / PA6 / PA7 / PA4", "AF5", "板2 W25Q128 走 PB3/PB4/PB5 + 片选 PB14（软件片选）"],
        ["SPI2", "SCK / MISO / MOSI / NSS", "PB13 / PB14 / PB15 / PB12", "AF5", "板1 屏接口用这组"],
        ["SPI3", "SCK / MISO / MOSI / NSS", "PB3 / PB4 / PB5 / PA15 或 PC10 / PC11 / PC12", "AF6", "与 JTAG 脚（PB3/PB4/PA15）复用，需先关 JTAG 才可用"],

        ["CAN1", "TX / RX", "PD1 / PD0 或 PB9 / PB8", "AF9", "与 I2C1（PB8/PB9）冲突，二者只能选一"],
        ["CAN2", "TX / RX", "PB13 / PB12", "AF9", "与 SPI2 的 SCK/NSS 同脚"],

        ["USB_OTG_FS", "DM / DP / ID / SOF / VBUS", "PA11 / PA12 / PA10 / PA8 / PA9", "AF10", "与 USART1 的 RX/CK 冲突"],
        ["USB_OTG_HS", "DM / DP / ID / SOF / ULPI", "PB14 / PB15 / PB12 / PB13 / PA3 等", "AF10 或 AF12", "用 ULPI 时走 AF12"],
        ["ETH（RMII）", "REF_CLK / MDIO / MDC / CRS_DV / RXD0 / RXD1 / TX_EN / TXD0 / TXD1", "PA1 / PA2 / PC1 / PA7 / PC4 / PC5 / PB11 / PG13 / PG14", "AF11", "本项目未使用（ETH 驱动在库里备着）"],
        ["SDIO", "D0 / D1 / D2 / D3 / CK / CMD", "PC8 / PC9 / PC10 / PC11 / PC12 / PD2", "AF12", "本项目未使用"]
    ]
};

/* ---------------- 3) 定时器通道映射表 ---------------- */
const T_TIMCH = {
    m: "查找表", t: "table",
    n: "定时器通道映射表（TIM 通道 ↔ 引脚）",
    p: "用 PWM 输出、输入捕获或编码器时，先在这里确定“哪个定时器的哪个通道能引到哪个引脚”，再配 AF。",
    d: "定时器通道表：搜 TIM3 / CH2 / PWM 引脚（如 PA6、PB0）即可；同一通道的多个引脚只能选一个：",
    h: ["定时器", "通道", "引脚（可选组）", "AF", "备注"],
    k: "定时器 TIM 通道 CH1 CH2 CH3 CH4 PWM 输入捕获 编码器 ETR BKIN 引脚 AF1 AF2 AF3 AF9",
    a: [
        ["TIM1（高级）", "CH1 / CH2 / CH3 / CH4", "PA8 / PA9 / PA10 / PA11 或 PE9 / PE11 / PE13 / PE14", "AF1", "高级定时器，有互补输出"],
        ["TIM1（高级）", "CH1N / CH2N / CH3N", "PB13 / PB14 / PB15 或 PE8 / PE10 / PE12", "AF1", "互补通道，可配死区"],
        ["TIM1（高级）", "BKIN / ETR", "PA6 或 PB12 / PE15；PA12 或 PE7", "AF1", "刹车输入与外部时钟"],
        ["TIM2（32 位）", "CH1 / CH2 / CH3 / CH4", "PA0 / PA1 / PA2 / PA3 或 PA5 / PB3 / PB10 / PB11 或 PA15", "AF1", "32 位计数器，做长延时或 µs 计时方便"],
        ["TIM3", "CH1 / CH2 / CH3 / CH4", "PA6 / PA7 / PB0 / PB1 或 PB4 / PB5 / PC8 / PC9 或 PC6 / PC7", "AF2", "本项目未使用"],
        ["TIM4", "CH1 / CH2 / CH3 / CH4", "PB6 / PB7 / PB8 / PB9 或 PD12 / PD13 / PD14 / PD15", "AF2", "与 I2C1 的 PB8/PB9 冲突"],
        ["TIM5（32 位）", "CH1 / CH2 / CH3 / CH4", "PA0 / PA1 / PA2 / PA3（或 PH10~PH12）", "AF2", "与 TIM2 的 CH1~CH4 同脚，二选一"],
        ["TIM8（高级）", "CH1 / CH2 / CH3 / CH4", "PC6 / PC7 / PC8 / PC9", "AF3", "与 USART6 的 PC6/PC7 冲突"],
        ["TIM8（高级）", "CH1N / CH2N / CH3N", "PA5 / PA7、PB0 / PB14、PB1 / PB15", "AF3", "互补通道"],
        ["TIM9", "CH1 / CH2", "PA2 / PA3 或 PE5 / PE6", "AF3", "只有两个通道"],
        ["TIM10", "CH1", "PB8 或 PF6", "AF3", "单通道"],
        ["TIM11", "CH1", "PB9 或 PF7", "AF3", "单通道；PF7 与 ADC3_IN5 同脚"],
        ["TIM12", "CH1 / CH2", "PB14 / PB15（或 PH6 / PH9）", "AF9", "与 SPI2 的 MISO/MOSI 同脚"],
        ["TIM13", "CH1", "PA6 或 PF8", "AF9", "单通道"],
        ["TIM14", "CH1", "PA7 或 PF9", "AF9", "单通道"]
    ]
};

/* ---------------- 4) ADC 通道映射表 ---------------- */
const T_ADCCH = {
    m: "查找表", t: "table",
    n: "ADC 通道映射表（通道 ↔ 引脚 / 内部源）",
    p: "ADC_RegularChannelConfig 的通道号填哪个？按此表查引脚对应的 ADC 与通道号；三个 ADC 的 0~15 通道引脚相同，ADC3 的高编号通道在 PF 口。",
    d: "ADC 通道表：搜引脚（PF7 / PA0）或通道号（ADC_Channel_5）；内部通道只有 ADC1 能测：",
    h: ["通道", "ADC1", "ADC2", "ADC3", "说明"],
    k: "ADC 通道 引脚 ADC_Channel 规则通道 注入通道 PF7 PA0 PC0 温度 Vrefint VBAT 采样",
    a: [
        ["ADC_Channel_0 ~ 3", "PA0 ~ PA3", "PA0 ~ PA3", "PA0 ~ PA3", "三个 ADC 都有"],
        ["ADC_Channel_4", "PA4", "PA4", "PF6", "ADC3 在 PF 口"],
        ["ADC_Channel_5", "PA5", "PA5", "PF7", "本项目 MQ-135 模拟输出走 PF7（ADC3_IN5）"],
        ["ADC_Channel_6", "PA6", "PA6", "PF8", "—"],
        ["ADC_Channel_7", "PA7", "PA7", "PF9", "—"],
        ["ADC_Channel_8", "PB0", "PB0", "PF10", "—"],
        ["ADC_Channel_9", "PB1", "PB1", "PF3", "—"],
        ["ADC_Channel_10 ~ 13", "PC0 ~ PC3", "PC0 ~ PC3", "PC0 ~ PC3", "三个 ADC 都有"],
        ["ADC_Channel_14 / 15", "PC4 / PC5", "PC4 / PC5", "PF4 / PF5", "ADC3 在 PF 口"],
        ["ADC_Channel_16", "内部温度传感器", "—", "—", "仅 ADC1；采样时间要 ≥ 10 µs（用最长采样周期）"],
        ["ADC_Channel_17", "内部参考电压 Vrefint", "—", "—", "仅 ADC1；典型 1.21 V，可用来反推实际 VDDA"],
        ["ADC_Channel_18", "VBAT 电池电压（÷2）", "—", "—", "仅 ADC1；测得值乘 2 才是电池电压"],
        ["滤波 / 精度提示", "12 位 = 4095", "参考电压 3.3 V", "约 0.8 mV/LSB", "模拟引脚要配 GPIO_Mode_AN；采样时间越长越稳，但转换越慢"]
    ]
};

/* ---------------- 5) 时钟树与频率配置表 ---------------- */
const T_CLOCK = {
    m: "查找表", t: "table",
    n: "时钟树与常用频率配置表（F407）",
    p: "算定时器 / 串口 / ADC 时钟前先确定这条链：HSE → PLL → SYSCLK → AHB / APB1 / APB2，注意 APB 分频不为 1 时定时器时钟会翻倍。",
    d: "时钟表：搜 PLL / APB1 / APB2 / 定时器时钟 / 168 / 84 / 42，可查到分频关系与上限：",
    h: ["项目", "取值", "说明"],
    k: "时钟 PLL APB1 APB2 AHB SYSCLK HSE HSI 168 84 42 定时器时钟 分频 Flash 等待周期 SysTick",
    a: [
        ["HSE（外部晶振）", "8 MHz（本项目两块板）", "外部高速时钟，精度高，系统主时钟源"],
        ["HSI（内部 RC）", "16 MHz", "内部时钟，精度差，一般只做备份"],
        ["PLL 输入分频 M", "8", "VCO 输入 = HSE ÷ M = 1 MHz"],
        ["PLL 倍频 N", "336", "VCO 输出 = 1 MHz × 336 = 336 MHz（须在 100~432 MHz）"],
        ["PLL 输出分频 P", "2", "SYSCLK = 336 ÷ 2 = 168 MHz"],
        ["PLL 输出分频 Q", "7", "48 MHz 域（USB / SDIO）= 336 ÷ 7 = 48 MHz"],
        ["SYSCLK", "168 MHz", "F407 最高 168 MHz"],
        ["AHB 分频（HCLK）", "1", "HCLK = 168 MHz（内核、DMA、SRAM 时钟）"],
        ["APB1 分频（PCLK1）", "4", "PCLK1 = 42 MHz（F407 上限 42 MHz）"],
        ["APB2 分频（PCLK2）", "2", "PCLK2 = 84 MHz（F407 上限 84 MHz）"],
        ["APB1 定时器时钟", "84 MHz（= PCLK1 × 2）", "APB 分频 ≠ 1 时定时器时钟自动 ×2，算 PSC/ARR 别用 42 MHz"],
        ["APB2 定时器时钟", "168 MHz（= PCLK2 × 2）", "同理；TIM1/TIM8/TIM9~11 属 APB2"],
        ["Flash 等待周期（LATENCY）", "5 WS @168 MHz、3.3 V", "电压越低要越多等待周期，配错会跑飞"],
        ["SysTick", "HCLK 或 HCLK ÷ 8", "SPL 用 HCLK；本项目 FreeRTOS 节拍 1000 Hz（1 ms）"],
        ["ADC 时钟", "PCLK2 ÷ 分频（≤ 36 MHz）", "84 MHz 时至少 Div4 → 21 MHz"],
        ["常用主频组合", "168 / 120 / 84 / 48 / 24 MHz", "改频率要同时改 PLL_M/N/P/Q、APB 分频与 Flash 等待周期"]
    ]
};

/* ---------------- 6) 常用器件 I2C 地址表 ---------------- */
const T_I2CADDR = {
    m: "查找表", t: "table",
    n: "常用器件 I2C 地址表（7 位地址）",
    p: "I2C 读写函数里的地址是 7 位地址；SPL 的 I2C_OwnAddress1 与部分寄存器要左移一位（7 位地址 << 1），别与“0x78 这种 8 位写法”混用。",
    d: "I2C 地址表：搜器件名（SSD1306 / BMP280 / CCS811）或地址（0x3C / 0x76）都可：",
    h: ["器件", "7 位地址", "可配范围", "本项目用法"],
    k: "I2C 地址 器件 SSD1306 OLED AT24C02 24C02 MPU6050 BMP280 BME280 BH1750 CCS811 SHT30 PCF8574 DS3231 0x3C 0x50 0x68 0x76 0x23 0x5A 0x44",
    a: [
        ["SSD1306（0.96 寸 OLED）", "0x3C", "0x3C / 0x3D", "板1 使用（I2C1 PB8/PB9）；模块背面跳线决定 0x3C 或 0x3D"],
        ["AT24C02 / 24C02（EEPROM）", "0x50", "0x50 ~ 0x57（A0~A2）", "板1、板2 都使用（I2C1 / I2C2）"],
        ["MPU6050（六轴）", "0x68", "0x68 / 0x69（AD0）", "库内有驱动，未接实物"],
        ["BMP280（气压）", "0x76", "0x76 / 0x77（SDO）", "计划补装；芯片 ID 读 0xD0 = 0x58，BME280 是 0x60"],
        ["BH1750（光照）", "0x23", "0x23 / 0x5C（ADDR）", "计划补装"],
        ["CCS811（TVOC / eCO2）", "0x5A", "0x5A / 0x5B（ADDR）", "计划补装；只用 3.3 V，WAKE 脚要拉低"],
        ["SHT30（温湿度）", "0x44", "0x44 / 0x45（ADDR）", "计划补装；驱动待补"],
        ["PCF8574（IO 扩展）", "0x20", "0x20 ~ 0x27", "通用器件，本项目未使用"],
        ["DS1307 / DS3231（RTC）", "0x68", "0x68（固定）", "与 MPU6050 地址相同，同总线只能接一个"],
        ["I2C 字符屏转接板", "0x27", "0x27 / 0x3F", "通用器件，本项目未使用"],
        ["说明", "7 位地址", "写 = 地址 << 1", "读 = (地址 << 1) | 1；有些库函数直接收 8 位写地址，用前先确认"]
    ]
};

/* ---------------- 7) FreeRTOS 常用配置宏表 ---------------- */
const T_FREERTOS = {
    m: "查找表", t: "table",
    n: "FreeRTOS 常用配置宏表（本项目取值）",
    p: "取值来自两块板的 FreeRTOS/port/FreeRTOSConfig.h；改这些宏会影响任务调度、栈与中断优先级，改完必须重新编译并跑一遍自检。",
    d: "FreeRTOS 配置表：搜 config / 节拍 / 堆 / 优先级 / 栈溢出 即可看到本项目取值与含义：",
    h: ["配置宏", "本项目取值", "含义 / 取舍"],
    k: "FreeRTOS config 配置 节拍 tick 堆 heap 优先级 抢占 时间片 互斥量 栈溢出 configTICK_RATE_HZ configTOTAL_HEAP_SIZE configMAX_PRIORITIES",
    a: [
        ["configUSE_PREEMPTION", "1", "抢占式调度：高优先级任务就绪即可打断低优先级任务"],
        ["configUSE_PORT_OPTIMISED_TASK_SELECTION", "0", "通用选任务算法（不用 Cortex-M 的 CLZ 指令优化），可移植性好"],
        ["configUSE_TICKLESS_IDLE", "0", "不启用无节拍低功耗，调试时行为更直观"],
        ["configCPU_CLOCK_HZ", "168000000", "主频，必须与 SystemCoreClock 一致"],
        ["configTICK_RATE_HZ", "1000", "节拍 1 ms；vTaskDelay(100) 就是 100 ms"],
        ["configMAX_PRIORITIES", "8", "优先级档数 0~7，数值大者优先；本项目任务用 1~4"],
        ["configMINIMAL_STACK_SIZE", "128（字 = 512 字节）", "空闲任务栈；自建任务按用途取 128~256 字（本项目 128~256）"],
        ["configTOTAL_HEAP_SIZE", "30 × 1024 = 30 KB", "heap_4 堆总量；任务栈、队列、互斥量都从这里分配"],
        ["configSUPPORT_STATIC_ALLOCATION / DYNAMIC", "0 / 1", "只用动态分配，入门少写一堆静态内存"],
        ["configUSE_MUTEXES / RECURSIVE_MUTEXES", "1 / 1", "互斥量与递归互斥量（本项目共享总线用互斥量保护）"],
        ["configUSE_COUNTING_SEMAPHORES", "1", "计数信号量，可用于资源计数"],
        ["configUSE_QUEUE_SETS", "0", "队列集（进阶特性，未用）"],
        ["configQUEUE_REGISTRY_SIZE", "8", "队列调试登记数量，0 表示不登记"],
        ["configUSE_TIMERS / TIMER_TASK_PRIORITY", "1 / 3", "软件定时器服务任务优先级 3，低于通信任务高于空闲"],
        ["configTIMER_QUEUE_LENGTH / TIMER_TASK_STACK_DEPTH", "10 / 128", "定时器命令队列长度与任务栈"],
        ["configUSE_IDLE_HOOK / TICK_HOOK", "0 / 0", "空闲钩子与节拍钩子未用"],
        ["configUSE_MALLOC_FAILED_HOOK", "1", "堆不足时进入钩子，便于定位泄漏"],
        ["configCHECK_FOR_STACK_OVERFLOW", "2", "栈溢出检查（含模式串比对），比 1 更可靠"],
        ["configPRIO_BITS", "4", "NVIC 优先级位数（F4 为 4 位）"],
        ["configLIBRARY_LOWEST_INTERRUPT_PRIORITY", "15", "最低（数值最大）优先级"],
        ["configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY", "5", "调用 FromISR 类 API 的中断，优先级数值必须 ≥ 5"],
        ["configKERNEL_INTERRUPT_PRIORITY", "15 << 4 = 0xF0", "内核（PendSV/SysTick）用的最低优先级"],
        ["configMAX_SYSCALL_INTERRUPT_PRIORITY", "5 << 4 = 0x50", "屏蔽阈值：数值高于它的中断不得调用 FreeRTOS API"],
        ["中断优先级提醒", "抢占优先级数值 ≥ 5", "本项目外设中断一般配 5~7；配成 0~4 又调用 FromISR 会触发断言或丢中断"]
    ]
};

/* ---------------- 8) 外设常用计算公式表 ---------------- */
const T_FORMULA = {
    m: "查找表", t: "table",
    n: "外设常用计算公式表（频率 / 时间 / 波特率）",
    p: "填 PSC / ARR / BRR / 分频前先算一遍；式子里的时钟按“时钟树表”取值（注意 APB 分频不为 1 时定时器时钟翻倍）。",
    d: "计算公式表：搜“波特率 / 定时器频率 / 占空比 / 看门狗 / 超时”即可看到公式与算例：",
    h: ["目标", "公式", "算例"],
    k: "计算 公式 波特率 BRR 定时器 频率 PWM 占空比 看门狗 超时 IWDG WWDG ADC 采样时间 SPI 分频 RTC 唤醒 SysTick 微秒延时",
    a: [
        ["串口波特率（BRR）", "过采样 16 时 BRR = fPCLK ÷ 波特率（整数部分 + 小数部分 × 16）", "USART1 用 PCLK2=84 MHz，115200 → 84e6 ÷ 115200 = 729.17 → 0x2D93"],
        ["定时器中断 / PWM 频率", "f = 定时器时钟 ÷ ((PSC+1) × (ARR+1))", "APB1 定时器 84 MHz，PSC=83、ARR=999 → 1 kHz"],
        ["PWM 占空比", "占空比 = CCR ÷ (ARR+1) × 100%", "ARR=999、CCR=250 → 25%"],
        ["定时器延时时间", "t = (PSC+1) × (ARR+1) ÷ 定时器时钟", "同上参数 → 1 ms"],
        ["输入捕获测频率", "f = 定时器时钟 ÷ (PSC+1) ÷ 两次捕获的计数差", "计数差 1000，PSC=83 → 84e6 ÷ 84 ÷ 1000 = 1 kHz"],
        ["I2C 速率", "由 I2C_ClockSpeed 决定（100 kHz 标准 / 400 kHz 快速）", "本项目 OLED、EEPROM 用 100 kHz 或 400 kHz"],
        ["SPI 时钟", "SCK = PCLK ÷ 分频（2/4/8/16/32/64/128/256）", "SPI1 在 APB2=84 MHz，Div8 → 10.5 MHz（W25Q128 上限 104 MHz）"],
        ["ADC 转换时间", "t = (采样周期 + 12) ÷ ADC 时钟", "ADC 时钟 21 MHz、采样 480 周期 → (480+12)/21e6 ≈ 23.4 µs"],
        ["IWDG 超时", "t ≈ 4 × 2^PR × (RLR+1) ÷ LSI(约 32 kHz)", "PR=4、RLR=1249 → 4×16×1250/32000 ≈ 2.5 s"],
        ["WWDG 窗口", "递减周期 = 4096 × 2^WDGTP ÷ PCLK1", "WDGTP=3、PCLK1=42 MHz → 4096×8/42e6 ≈ 780 µs"],
        ["RTC 唤醒周期", "t = (WUT + 1) ÷ 唤醒时钟频率", "选 1 Hz、WUT=9 → 10 s"],
        ["SysTick 时基", "中断周期 = 1 ÷ (SystemCoreClock ÷ 重装值)", "168 MHz ÷ 168000 = 1 kHz（1 ms）"],
        ["DWT 微秒级延时", "延时 µs = 计数差 ÷ SystemCoreClock(MHz)", "168 MHz 下 1 µs = 168 个计数；比 for 循环准"],
        ["FreeRTOS 延时换算", "vTaskDelay(n) = n × 1000 ÷ configTICK_RATE_HZ 毫秒", "节拍 1000 Hz 时 vTaskDelay(50) = 50 ms"],
        ["NVIC 优先级级数", "可用级数 = 2^(抢占位数)", "分组 4（4 位抢占）→ 16 级；数值小者优先"]
    ]
};

/* ---------------- USART 常用波特率配置表（BRR 值与误差） ---------------- */
const T_BAUD = {
    m: "查找表", t: "table",
    n: "USART 常用波特率配置表（BRR 值与误差）",
    p: "USART_Init 只填 BaudRate，库按 PCLK 自动算 BRR；这张表列出常用波特率在 84 MHz（APB2：USART1、USART6）与 42 MHz（APB1：USART2/3、UART4/5）下的 BRR 值与实际误差（按 16 倍过采样）。",
    d: "波特率速查：搜 115200 / 波特率 / BRR / 误差，查该波特率在 84 MHz 与 42 MHz 时钟下的寄存器值与偏差。",
    h: ["波特率", "BRR（PCLK=84 MHz）", "误差", "BRR（PCLK=42 MHz）", "误差", "常见用途"],
    a: [
        ["9600", "0x222E", "+0.00%", "0x1117", "+0.00%", "低速调试口、老式仪表"],
        ["19200", "0x1117", "+0.00%", "0x88C", "-0.02%", "与传感器/老模块通信"],
        ["38400", "0x88C", "-0.02%", "0x446", "-0.02%", "部分蓝牙模块默认档"],
        ["57600", "0x5B2", "+0.02%", "0x2D9", "+0.02%", "中速调试打印"],
        ["115200", "0x2D9", "+0.02%", "0x16D", "-0.11%", "本项目两板与上位机默认（8N1）"],
        ["230400", "0x16D", "-0.11%", "0x0B6", "+0.16%", "高速日志、大包传输"],
        ["460800", "0x0B6", "+0.16%", "0x05B", "+0.16%", "需短接线与好质量 USB-TTL"],
        ["921600", "0x05B", "+0.16%", "0x02E", "-0.93%", "接近极限，杜邦线易误码"]
    ],
    k: "波特率 115200 9600 19200 38400 57600 230400 BRR USART 误差 过采样 PCLK"
};

/* ---------------- SPI 波特率分频表（fPCLK/2 ~ fPCLK/256） ---------------- */
const T_SPI = {
    m: "查找表", t: "table",
    n: "SPI 波特率分频表（fPCLK/2 ~ fPCLK/256）",
    p: "SPI_Init 的 SPI_BaudRatePrescaler 只能取 2 的幂分频；SPI1 挂 APB2（84 MHz），SPI2/SPI3 挂 APB1（42 MHz）。从机的最高 SCK 往往低于 MCU，先按从机手册卡住上限再选档。",
    d: "SPI 速率速查：搜 SPI 分频 / BaudRatePrescaler / SCK，查每个分频档在 84 MHz 与 42 MHz 下的实际时钟。",
    h: ["分频宏", "实际 SCK（SPI1 @84 MHz）", "实际 SCK（SPI2/3 @42 MHz）", "说明"],
    a: [
        ["SPI_BaudRatePrescaler_2", "42 MHz", "21 MHz", "最高速（F407 SPI 上限就是 fPCLK/2），只能短接线 + 低容性负载"],
        ["SPI_BaudRatePrescaler_4", "21 MHz", "10.5 MHz", "高速档，杜邦线短接时常用上限"],
        ["SPI_BaudRatePrescaler_8", "10.5 MHz", "5.25 MHz", "本项目这类 10~20 cm 杜邦线的稳妥档"],
        ["SPI_BaudRatePrescaler_16", "5.25 MHz", "2.625 MHz", "中速，抗干扰更好"],
        ["SPI_BaudRatePrescaler_32", "2.625 MHz", "1.312 MHz", "低速外设（如部分 Flash、传感器）"],
        ["SPI_BaudRatePrescaler_64", "1.312 MHz", "0.6562 MHz", "很慢，长线或调试抓波形用"],
        ["SPI_BaudRatePrescaler_128", "0.6562 MHz", "0.3281 MHz", "兼容极慢从机"],
        ["SPI_BaudRatePrescaler_256", "0.3281 MHz", "0.1641 MHz", "最慢档，仅用于超长线或验证时序"]
    ],
    k: "SPI 分频 BaudRatePrescaler SCK 速率 2 4 8 16 32 64 128 256"
};

/* ---------------- I2C 速率配置表（100 kHz / 400 kHz @ APB1 42 MHz） ---------------- */
const T_I2C = {
    m: "查找表", t: "table",
    n: "I2C 速率配置表（100 kHz / 400 kHz @ APB1 42 MHz）",
    p: "I2C_Init 的 I2C_ClockSpeed 填目标速率（单位 Hz），CCR 与 TRISE 由它和 fPCLK 算出；下表给出 APB1=42 MHz 时两种标准速率的寄存器值。",
    d: "I2C 速率速查：搜 I2C 速率 / ClockSpeed / CCR / TRISE / 100k / 400k。",
    h: ["模式", "速率", "CCR", "TRISE", "说明"],
    a: [
        ["标准模式 SM", "100 kHz", "210", "43", "占空比固定 1:1；上拉 4.7 kΩ、总线电容 ≤ 400 pF（OLED、传感器常用这档）"],
        ["快速模式 FM", "400 kHz", "35", "14", "占空比 2:1（I2C_DutyCycle_2）；上拉 2.2 kΩ；F407 的 I2C 上限就是 400 kHz"],
        ["自定义速率", "任意（≤400 kHz）", "fPCLK ÷ (2×速率)（SM）；fPCLK ÷ (3×速率)（FM）", "fPCLK×t_r + 1", "库会在 I2C_Init 里自动算，只需把 I2C_ClockSpeed 填成目标速率；上表两行是 APB1=42 MHz 的现成值"]
    ],
    k: "I2C 速率 100k 400k ClockSpeed CCR TRISE 上拉 占空比 标准 快速"
};

/* ---------------- CAN 波特率配置表（APB1 = 42 MHz） ---------------- */
const T_CAN = {
    m: "查找表", t: "table",
    n: "CAN 波特率配置表（APB1 = 42 MHz）",
    p: "CAN_Init 用 CAN_Prescaler + CAN_BS1 + CAN_BS2 拼出目标波特率：波特率 = fPCLK ÷ ((BS1 + BS2 + 1) × 分频)。下表为 42 MHz 下常用速率的现成组合（BS1=11tq、BS2=2tq，采样点 85.7%）。",
    d: "CAN 波特率速查：搜 CAN 500k 250k 125k 波特率 Prescaler BS1 BS2 采样点。",
    h: ["目标波特率", "分频（Prescaler）", "BS1", "BS2", "采样点", "典型用途"],
    a: [
        ["1000 kbit/s", "3", "CAN_BS1_11tq", "CAN_BS2_2tq", "85.7%（12/14）", "短总线（<10 m）＋120 Ω 终端电阻"],
        ["500 kbit/s", "6", "CAN_BS1_11tq", "CAN_BS2_2tq", "85.7%（12/14）", "工业现场最常用"],
        ["250 kbit/s", "12", "CAN_BS1_11tq", "CAN_BS2_2tq", "85.7%（12/14）", "节点多、线较长时更稳"],
        ["125 kbit/s", "24", "CAN_BS1_11tq", "CAN_BS2_2tq", "85.7%（12/14）", "长线 / 多节点网络"],
        ["100 kbit/s", "30", "CAN_BS1_11tq", "CAN_BS2_2tq", "85.7%（12/14）", "兼容旧设备"],
        ["50 kbit/s", "60", "CAN_BS1_11tq", "CAN_BS2_2tq", "85.7%（12/14）", "调试与教学"],
        ["20 kbit/s", "150", "CAN_BS1_11tq", "CAN_BS2_2tq", "85.7%（12/14）", "仅供慢速设备或抓波形"]
    ],
    k: "CAN 波特率 500k 250k 125k 100k 50k 20k 1M Prescaler BS1 BS2 采样点 终端电阻"
};

/* ---------------- 看门狗配置表（IWDG / WWDG 超时，含常用值） ---------------- */
const T_WDG = {
    m: "查找表", t: "table",
    n: "看门狗配置表（IWDG / WWDG 超时，含常用值）",
    p: "IWDG 超时 = 4 × 2^预分频 × (重装值 + 1) ÷ LSI（按 32 kHz 算）；WWDG 超时 = 4096 × 2^WDGTB × (计数值 + 1) ÷ PCLK（42 MHz）。表里给各档的最小/最大超时与常用配置。",
    d: "看门狗速查：搜 看门狗 IWDG WWDG 超时 预分频 重装值 喂狗。",
    h: ["看门狗", "分频档", "最小超时", "最大超时", "常用配置", "说明"],
    a: [
        ["IWDG", "÷4（IWDG_Prescaler_4）", "0.125 ms", "0.512 s", "", "最短档：喂狗稍慢就复位，一般不用"],
        ["IWDG", "÷8（IWDG_Prescaler_8）", "0.250 ms", "1.024 s", "", "极短超时，只适合主循环极快的场合"],
        ["IWDG", "÷16（IWDG_Prescaler_16）", "0.500 ms", "2.048 s", "", "短超时档"],
        ["IWDG", "÷32（IWDG_Prescaler_32）", "1.000 ms", "4.096 s", "", "1 ms 步进，适合几十~几百毫秒的喂狗周期"],
        ["IWDG", "÷64（IWDG_Prescaler_64）", "2.000 ms", "8.192 s", "重装值 499 → 2 s", "2 ms 步进；2 s 常用（重装值 499）"],
        ["IWDG", "÷128（IWDG_Prescaler_128）", "4.000 ms", "16.38 s", "重装值 249 → 1 s", "4 ms 步进；1 s 常用（重装值 249）"],
        ["IWDG", "÷256（IWDG_Prescaler_256）", "8.000 ms", "32.77 s", "重装值 4095 → 32.768 s", "最长档（最大约 32.8 s），适合低速休眠系统"],
        ["WWDG", "WDGTB=0（÷1）", "6.34 ms", "12.48 ms", "", "超时只有 6~12 ms，喂狗窗口极窄；计数值只能 0x40~0x7F，喂早了也复位"],
        ["WWDG", "WDGTB=1（÷2）", "12.68 ms", "24.97 ms", "", "12~25 ms；计数值只能 0x40~0x7F，喂早了也复位"],
        ["WWDG", "WDGTB=2（÷4）", "25.36 ms", "49.93 ms", "计数值 0x7F → 49.9 ms", "25~50 ms，最常用（约 50 ms）；计数值只能 0x40~0x7F，喂早了也复位"],
        ["WWDG", "WDGTB=3（÷8）", "50.71 ms", "99.86 ms", "", "50~100 ms，适合慢速任务；计数值只能 0x40~0x7F，喂早了也复位"]
    ],
    k: "看门狗 IWDG WWDG 超时 预分频 重装值 喂狗 LSI 32kHz WDGTB 窗口"
};

/* ---------------- 存储映射与位带地址表（F407） ---------------- */
const T_MAPBIT = {
    m: "查找表", t: "table",
    n: "存储映射与位带地址表（F407）",
    p: "查外设基地址（寄存器级操作、给 DMA 填外设地址用）与位带别名换算：外设位带 = 0x42000000 + (地址 − 0x40000000) × 32 + 位号 × 4；SRAM 位带 = 0x22000000 + (地址 − 0x20000000) × 32 + 位号 × 4。位带可把某一引脚/标志位当普通变量读写，省去读改写。",
    d: "基地址速查：搜 基地址 / 地址 / 位带 / bit-band / PERIPH_BASE / 0x4002 0000 / 0x6C000000。",
    h: ["区域 / 外设", "基地址", "说明"],
    a: [
        ["FLASH（程序区）", "0x0800 0000", "F407ZE 512 KB / F407ZG 1 MB；扇区划分见 FLASH 扇区表"],
        ["CCM RAM", "0x1000 0000", "64 KB，只有内核能访问，DMA 不能读写"],
        ["SRAM1 + SRAM2", "0x2000 0000", "112 KB + 16 KB，DMA 可用（本工程堆/栈与缓存缓冲区都在这）"],
        ["外设区 PERIPH_BASE", "0x4000 0000", "下面所有外设基地址的基准，也是位带区起点"],
        ["APB1PERIPH", "0x4000 0000", "上限 42 MHz：TIM2~7/12~14、USART2/3、UART4/5、I2C1~3、SPI2/3、CAN1/2、IWDG、WWDG、PWR、DAC"],
        ["APB2PERIPH", "0x4001 0000", "上限 84 MHz：TIM1/8/9~11、USART1/6、SPI1、ADC1~3、SDIO、SYSCFG、EXTI"],
        ["AHB1PERIPH", "0x4002 0000", "GPIOA~K、RCC、DMA1/2、FLASH 接口、CRC"],
        ["AHB2PERIPH", "0x5000 0000", "USB OTG FS、DCMI、RNG、HASH、CRYP"],
        ["AHB3PERIPH", "0x6000 0000", "FSMC 寄存器区；FSMC 的存储 bank 另在 0x6000 0000 / 0xA000 0000"],
        ["GPIOA ~ GPIOK", "0x4002 0000 起，每端口 +0x400", "GPIOA 0x40020000、GPIOB 0x40020400、GPIOC 0x40020800、GPIOD 0x40020C00、GPIOE 0x40021000、GPIOF 0x40021400、GPIOG 0x40021800、GPIOH 0x40021C00、GPIOI 0x40022000、GPIOJ 0x40022400、GPIOK 0x40022800"],
        ["RCC", "0x4002 3800", "CR / PLLCFGR / CFGR / 各使能寄存器都在此外设内"],
        ["FLASH 接口", "0x4002 3C00", "ACR（等待周期/预取）、KEYR、SR、CR（解锁后才能擦写）"],
        ["DMA1 / DMA2", "0x4002 6000 / 0x4002 6400", "每条数据流占 0x18 字节；DMA1 有 8 流，DMA2 有 8 流"],
        ["TIM2 ~ TIM5", "0x4000 0000 / 0400 / 0800 / 0C00", "APB1 通用定时器，相邻间隔 0x400"],
        ["TIM1 / TIM8", "0x4001 0000 / 0x4001 0400", "APB2 高级定时器（带互补输出与刹车）"],
        ["USART1 / USART6", "0x4001 1000 / 0x4001 1400", "APB2，最高 84 MHz 时钟"],
        ["USART2 / USART3", "0x4000 4400 / 0x4000 4800", "APB1；板2 的 USB-TTL 与协议口在这两个里"],
        ["UART4 / UART5", "0x4000 4C00 / 0x4000 5000", "APB1"],
        ["I2C1 / I2C2 / I2C3", "0x4000 5400 / 5800 / 5C00", "APB1；板1 OLED 走 I2C1"],
        ["SPI1", "0x4001 3000", "APB2"],
        ["SPI2 / SPI3", "0x4000 3800 / 0x4000 3C00", "APB1；板2 的 W25Q128、LoRa 配置口在 SPI 上"],
        ["ADC1 / ADC2 / ADC3", "0x4001 2000 起（公共寄存器 0x4001 2300）", "APB2；三个 ADC 共享公共部分"],
        ["FSMC 存储区（LCD）", "0x6C00 0000", "板2 TFT 走 FSMC_NE4：命令 0x6C000000、数据 0x6C000080（见 lcd.h）"],
        ["位带别名区（外设）", "0x4200 0000", "0x42000000 + (addr − 0x40000000) × 32 + bit × 4；只覆盖 0x4000 0000~0x400F FFFF"],
        ["位带别名区（SRAM）", "0x2200 0000", "0x22000000 + (addr − 0x20000000) × 32 + bit × 4；只覆盖 0x2000 0000~0x200F FFFF"],
        ["位带算例：PA5 输出电平", "0x42400294", "GPIOA_ODR 地址 0x40020014 的第 5 位：0x42000000 + (0x40020014 − 0x40000000) × 32 + 5 × 4 = 0x42400294；写 1 即 PA5 输出 1"]
    ],
    k: "基地址 存储映射 位带 bit-band PERIPH_BASE GPIOA RCC DMA USART I2C SPI ADC FSMC LCD 0x6C000000 别名区"
};

/* ---------------- W25Q128 SPI Flash 常用指令表 ---------------- */
const T_W25Q = {
    m: "查找表", t: "table",
    n: "W25Q128 SPI Flash 常用指令表",
    p: "板2 缓存芯片 W25Q128（16 MB）的常用指令：写使能 0x06 → 擦除/编程 → 轮询 0x05 的 BUSY 位；工程用 4 KB 扇区擦除 + 32 字节槽编程。",
    d: "Flash 指令速查：搜 W25Q128 / Flash 指令 / 0x06 / 0x20 / 0xEB / JEDEC / 状态寄存器。",
    h: ["指令", "代码", "作用 / 用法"],
    a: [
        ["0x06 Write Enable", "0x06", "写使能：擦除 / 编程前必须先发一次（WEL 位会被自动清零）"],
        ["0x04 Write Disable", "0x04", "写禁止：主动撤销写使能"],
        ["0x05 Read Status-1", "0x05", "读状态寄存器 1：bit0=BUSY（1=正在擦写）、bit1=WEL；擦写后必须轮询 BUSY 变 0"],
        ["0x35 Read Status-2", "0x35", "读状态寄存器 2：bit1=QE（四线使能位，用 0xEB 前要置 1）"],
        ["0x01 Write Status", "0x01", "写状态寄存器：改保护位 / 置 QE 位（板2 的 W25Q128 用 QE 时）"],
        ["0x03 Read Data", "0x03", "普通读：单线，最高 50 MHz，适合低速读"],
        ["0x0B Fast Read", "0x0B", "快速读：带 1 个 dummy 字节，可上到 104 MHz"],
        ["0xEB Fast Read Quad I/O", "0xEB", "四线快速读：数据 4 位并行，需先置 QE 位（板2 缓存回读可用）"],
        ["0x02 Page Program", "0x02", "页编程：一次最多 256 字节且不能跨页；本工程按 32 字节槽写"],
        ["0x20 Sector Erase 4KB", "0x20", "4 KB 扇区擦除：本工程每扇区 128 槽 × 32 字节，擦除耗时约 45 ms"],
        ["0x52 Block Erase 32KB", "0x52", "32 KB 块擦除（更快，整块清理时用）"],
        ["0xD8 Block Erase 64KB", "0xD8", "64 KB 块擦除"],
        ["0xC7 / 0x60 Chip Erase", "0xC7", "整片擦除：耗时几十秒，正常流程别用（清缓存请用扇区擦）"],
        ["0x9F JEDEC ID", "0x9F", "读厂商与容量：W25Q128 返回 EF 40 18（EF=Winbond，4018=128 Mbit）；上电自检读它"],
        ["0x90 Device ID", "0x90", "读厂商 / 器件 ID（老式识别方式）"],
        ["0xAB Release Power-down", "0xAB", "退出掉电模式 / 读器件 ID，唤醒后的第一个指令"],
        ["0xB9 Deep Power-down", "0xB9", "进入掉电模式：功耗最低，需 0xAB 唤醒"],
        ["0x66 + 0x99 Reset", "0x66/0x99", "复位使能 + 执行复位：先 0x66 再 0x99，用于异常后恢复"]
    ],
    k: "W25Q128 Flash 指令 0x03 0x0B 0xEB 0x02 0x20 0xD8 0x06 0x05 0x9F JEDEC 状态寄存器 扇区擦除 页编程 BUSY"
};

/* ---------------- 本项目中断优先级分配表（两板 FWLIB + FreeRTOS） ---------------- */
const T_IRQPRIO = {
    m: "查找表", t: "table",
    n: "本项目中断优先级分配表（两板 FWLIB + FreeRTOS）",
    p: "两板 SYS_NVIC_Init() 第一步就把 NVIC 分组设为 NVIC_PriorityGroup_4（16 级抢占、无子优先级）；FWLIB 里所有外设中断统一用 5/0。改优先级前先看这张表，避免不能调用 FreeRTOS API 的优先级。",
    d: "中断优先级速查：搜 中断优先级 NVIC 抢占 子优先级 FreeRTOS 5 15 PriorityGroup_4。",
    h: ["中断 / 用途", "抢占", "子优先级", "能否调用 FreeRTOS API", "说明"],
    a: [
        ["FreeRTOS 内核（PendSV / SysTick / SVC）", "15", "0", "内核自身", "configKERNEL_INTERRUPT_PRIORITY = 15<<4 = 0xF0，最低优先级，保证任务切换不被抢占"],
        ["板1：USART（调试 / 协议 / LoRa）、DMA、EXTI、RTC、TIM", "5", "0", "可以", "宏在 sys_usart.h / sys_dma.h / sys_exti.h / sys_rtc.h / sys_tim.h：SYS_xxx_IRQ_PRE_PRIO=5、SUB=0"],
        ["板2：在板1 基础上再加 CAN1_RX0", "5", "0", "可以", "宏在 sys_can.h，同样 5/0"],
        ["新加中断若取 0 ~ 4 级", "0 ~ 4", "0", "不可以", "FreeRTOS 要求调用 xxxFromISR() 的中断优先级数值 ≥ configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY = 5，否则命中 configASSERT"]
    ],
    k: "中断优先级 NVIC 抢占 子优先级 分组 PriorityGroup_4 5 15 FreeRTOS FromISR configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY"
};

const T_SENSOR = {
    m: "传感器",
    n: "传感器寄存器与换算表",
    p: "板1 采集的 8 颗器件：命令字 / 寄存器 / 换算公式一页查完（对照 FWLIB 真实驱动）",
    d: "写传感器驱动时真正要查的三件事：发什么命令、读几个字节、怎么换算成物理量。" +
       "本表把项目里涉及的 SHT30 / BMP280 / BH1750 / CCS811 / MQ-135 / DHT11 / AHT10 / 光敏电阻 " +
       "以及 I2C 原语的调用方式汇总在一起，换算公式与协议倍率（温度 ×100、湿度 ×100、气压 ×10）一并给出。" +
       "表中函数名与参数均对照两板 FWLIB 的真实声明，可直接复制。",
    h: ["器件", "接口 / 地址", "寄存器 / 命令 / 函数", "作用", "换算公式与要点"],
    a: [
        ["SHT30 温湿度", "I2C1 · 0x44", "写 0x24 0x00", "单次测量，高重复性，不拉伸", "发完等 15 ms 再读 6 字节"],
        ["SHT30 温湿度", "I2C1 · 0x44", "写 0x2C 0x06", "单次测量，高重复性，时钟拉伸", "芯片自己把 SCL 拉低到出数，主机不必延时"],
        ["SHT30 温湿度", "I2C1 · 0x44", "写 0x30 0xA2", "软复位", "I2C 异常或读数明显不对时先发，等 1.5 ms"],
        ["SHT30 温湿度", "I2C1 · 0x44", "读 6 字节（无寄存器号）", "温度 2 + CRC 1 + 湿度 2 + CRC 1", "T = -45 + 175×t/65535；RH = 100×h/65535；协议 ×100"],
        ["BMP280 气压", "I2C1 · 0x76", "读 0xD0", "型号判别", "0x58 = BMP280；0x60 = BME280（寄存器不兼容）"],
        ["BMP280 气压", "I2C1 · 0x76", "读 0x88 ~ 0x9F", "24 字节出厂校准系数", "上电读一次；不读就套公式，气压偏差可达数倍"],
        ["BMP280 气压", "I2C1 · 0x76", "写 0xF4 CTRL_MEAS", "温度/气压过采样 + 模式", "(osrs_t<<5)|(osrs_p<<2)|mode；驱动默认 (2<<5)|(3<<2)|0x01 = 0x4D"],
        ["BMP280 气压", "I2C1 · 0x76", "写 0xF5 CONFIG", "待机时间 / IIR 滤波 / SPI3W", "驱动 IIR = 2（系数 4）；0x27 是 ×1/×1/正常，别照抄"],
        ["BMP280 气压", "I2C1 · 0x76", "读 0xF7 ~ 0xFC", "气压 3 字节 + 温度 3 字节", "adc = (b0<<12)|(b1<<4)|(b2>>4)；先算温度 t_fine 再算气压；协议 ×10"],
        ["BMP280 气压", "I2C1 · 0x76", "SYS_BMP280_Read(bus, &d)", "驱动一次读全", "d.temp_c / press_hpa / altitude_m；SYS_BMP280_Init() 先调用"],
        ["BH1750 光照", "I2C1 · 0x23", "写 0x01 再写 0x10", "上电 + 连续 H 分辨率", "不发 0x01 时读回恒为 0（能扫到地址但没数据）"],
        ["BH1750 光照", "I2C1 · 0x23", "读 2 字节（无寄存器号）", "照度原始值（高字节在前）", "lux = raw ÷ 1.2；0xFFFF = 过饱和（54612 lx 是下限）"],
        ["BH1750 光照", "I2C1 · 0x23", "写 0x13", "连续 L 分辨率，量程 ×4", "强光过饱和时换；分辨率降到 4 lx、约 16 ms"],
        ["BH1750 光照", "I2C1 · 0x23", "写 0x40|(mt>>5) 与 0x60|(mt&31)", "改积分时间 MTreg", "出厂 69，有效 31~254；换算 lux = raw/1.2×(69/MTreg)"],
        ["CCS811 空气", "I2C1 · 0x5A", "读 0x20", "HW_ID 自检", "须为 0x81；先拉低 WAKE 脚，否则地址都扫不到"],
        ["CCS811 空气", "I2C1 · 0x5A", "写 0xF4（只发命令）", "APP_START 进应用模式", "用 SYS_I2C_WriteCmd；不发则结果寄存器全 0"],
        ["CCS811 空气", "I2C1 · 0x5A", "写 0x01 = 0x10", "MEAS_MODE：恒供电每秒一次", "0x00 空闲 / 0x20 脉冲 10 s / 0x30 60 s / 0x40 250 ms"],
        ["CCS811 空气", "I2C1 · 0x5A", "读 0x02（4 字节）", "eCO2 2 字节 + TVOC 2 字节（大端）", "TVOC 原值进协议；eCO2 只在界面显示"],
        ["CCS811 空气", "I2C1 · 0x5A", "读 / 写 0x11", "BASELINE 基线（2 字节）", "存 Flash，下次上电写回可跳过 48 h 老化"],
        ["CCS811 空气", "I2C1 · 0x5A", "读 0xE0", "ERROR_ID 错误码", "0x01 写非法 / 0x02 读非法 / 0x04 模式非法 / 0x08 超量程 / 0x10 加热器 / 0x20 加热电压"],
        ["MQ-135 空气", "ADC 通道", "SYS_ADC_ReadAvg(ADCx, ch, 8)", "读原始 ADC（8 次平均）", "5 V 供电；AO 可能接近 5 V，须分压到 ≤ 3.3 V"],
        ["MQ-135 空气", "ADC 通道", "Rs = (VCC − Vout) / Vout × RL", "敏感体电阻", "ratio = Rs/R0，洁净空气 ≈ 1；ppm 需先标定 R0，个体差异大"],
        ["DHT11 温湿度", "单总线 · GPIOG9", "主机拉低 ≥ 18 ms 后释放", "起始信号", "从机应答 80 µs 低 + 80 µs 高，随后发 40 位"],
        ["DHT11 温湿度", "单总线 · GPIOG9", "40 位数据 + 校验", "湿度整数/小数 + 温度整数/小数 + 校验", "校验 = 前 4 字节之和的低 8 位；采样间隔 ≥ 1 s，读时关中断"],
        ["AHT10 温湿度", "I2C1 · 0x38", "写 0xE1 0x08 0x00", "上电校准", "发完等 10 ms，读状态 0x71 看 bit3 校准位"],
        ["AHT10 温湿度", "I2C1 · 0x38", "写 0xAC 0x33 0x00", "触发一次测量", "发完等 80 ms，再读 6 字节"],
        ["AHT10 温湿度", "I2C1 · 0x38", "读 6 字节", "状态 + 湿度 20 位 + 温度 20 位", "RH = h×100/2^20；T = t×200/2^20 − 50；协议 ×100"],
        ["光敏电阻（辅助）", "ADC3_IN5 · PF7", "分压 Vout = 3.3×R/(R+R_photo)", "辅助光强", "R_photo = R×(3.3−Vout)/Vout；不标定只能给相对值"],
        ["I2C 原语", "任意总线", "SYS_I2C_WriteBytes(id, addr7, reg, buf, len)", "连写（reg 在前）", "SHT30/AHT10 的 2~3 字节命令：首字节当 reg 传"],
        ["I2C 原语", "任意总线", "SYS_I2C_WriteCmd(id, addr7, cmd)", "只发一个字节命令", "BH1750 模式字、CCS811 APP_START 用这个"],
        ["I2C 原语", "任意总线", "SYS_I2C_ReadRaw(id, addr7, buf, len)", "无寄存器号读", "BH1750 / SHT30 / AHT10 读数据用这个（ReadBytes 会多发寄存器号）"],
        ["I2C 原语", "任意总线", "SYS_I2C_Scan(id, found, max)", "扫描 0x08 ~ 0x77", "返回值 = 器件个数；上电自检用"],
        ["总线与协议", "板1 采集", "SYS_FRAME_BuildEnv(...)", "组环境帧", "温度 ×100 / 湿度 ×100 / 气压 ×10 / 光照原值 / TVOC ppb / MQ135 原始 ADC"]
    ],
    k: "传感器 寄存器 换算 命令字 SHT30 BMP280 BH1750 CCS811 MQ135 DHT11 AHT10 光敏电阻 数据手册 I2C 地址 协议 倍率 采集"
};

const T_DRIVER = {
    m: "外设",
    n: "两板外设驱动一览表",
    p: "板1 检测端 37 个驱动头文件 + 板2 数据通信端 58 个驱动头文件，一个头文件一行，头文件名 / 真实函数名 / 引脚 / 易错点一页查完",
    d: "两板 FWLIB 的 inc 目录共 95 个头文件，本表逐个对照真实声明写成。" +
       "同名不同板的外设 API 前缀不同，抄代码前先看「所属板」这一列：" +
       "板1 是 sys_ 前缀（SYS_USART_* / SYS_OLED_* / SYS_DHT11_Read / SYS_MPU6050_Read / SYS_MODBUS_*），" +
       "板2 去掉了 sys_（DHT11_ReadInt / MPU6050_Read / MODBUS_Poll），" +
       "带 sys_ 的基本保留（SYS_I2C_* / SYS_ADC_* / SYS_TIM_*）。" +
       "板1 蜂鸣器在 PF8、4 个 LED 在 PF9/PF10 与 PE13/PE14、LoRa 走 SYS_USART_3；" +
       "板2 蜂鸣器同样 PF8，但只有 2 个 LED（PF9/PF10）、LoRa 改走 SYS_USART_6。" +
       "板1 接的是 240×320 的 ILI9341 类并口屏（LCD_FSMC_DATA_SETUP 6），" +
       "板2 接的是 320×480 的 ILI9481 并口屏（LCD_FSMC_DATA_SETUP 9）并多一个 lcd_font.h 字库头，两板 lcd.h 同名但是两款屏（板1 240×320 ILI9341 类、板2 320×480 ILI9481），表里各占一行。" +
       "外设驱动速查：搜 驱动一览 头文件 引脚 API 板1 板2 各模块名。",
    h: ["模块 / 器件", "所属板", "头文件", "关键 API（真实存在的）", "引脚 / 地址 / 总线", "备注 / 易错点"],
    a: [
        ["ADC 采集", "板1", "sys_adc.h", "SYS_ADC_Read / SYS_ADC_ReadAvg / SYS_ADC_DmaScanInit / SYS_ADC_ReadMilliVolt", "ADC3_IN5 · PF7（光敏）；VREF 宏 SYS_ADC_VREF_MV = 3300", "采样 480 周期、SYS_ADC_AVG_TIMES = 8；换算用 SYS_ADC_ReadMilliVolt 而不是手写 4096 比例"],
        ["位带操作", "板1", "sys_bitband.h", "PAout / PAin / BITBAND_PERIPH / GPIO_BB_OUT", "地址区 0x42000000（外设）/ 0x22000000（SRAM）", "单独写一个位不影响同端口其它位；只对 SRAM 与外设区有效，不能指向任意变量"],
        ["时钟配置", "板1", "sys_clock.h", "SYS_CLK_GetFreq / SYS_CLK_GetBusFreq / SYS_CLK_Switch / SYS_CLK_ToHighSpeed", "HCLK ≤ SYS_CLK_MAX_HCLK = 168000000；PCLK1 ≤ 42 MHz、PCLK2 ≤ 84 MHz", "改总线分频后 USART/CAN 的波特率跟着变，必须重新算"],
        ["DMA", "板1", "sys_dma.h", "SYS_DMA_PeriphToMem / SYS_DMA_MemToPeriph / SYS_DMA_SetCallback / SYS_DMA_WaitDone", "流由调用方传入（DMA_Stream_TypeDef *）", "外设与流要一一对应（ADC1 只能用 DMA2_Stream0/4），传错流不会报错只是不动"],
        ["EXTI 外部中断", "板1", "sys_exti.h", "SYS_EXTI_InitLine / SYS_EXTI_SetTrigger / SYS_EXTI_Disable / SYS_EXTI_GetFlag", "线与端口由调用方传（line + GPIO_TypeDef* + pin）", "同一个 line 号全局只能绑一个端口；回调在中断里执行要短；优先级宏 SYS_EXTI_IRQ_PRE_PRIO = 5"],
        ["故障日志", "板1", "sys_fault.h", "SYS_FAULT_Report / SYS_FAULT_Happened / SYS_FAULT_SetCallback / SYS_FAULT_Clear", "无独立引脚（默认只解析不自动复位）", "不开 SYS_FAULT_AUTO_RESET 时打印完不会自己重启；Report 的缓冲要自己给"],
        ["片内 FLASH", "板1", "sys_flash.h", "SYS_FLASH_SaveParams / SYS_FLASH_LoadParams / SYS_FLASH_EraseSector / SYS_FLASH_Write", "参数扇区 0x08070000（备用 0x08060000）· MAGIC 0x50415241", "板2 同一套 API 但地址是 0x080E0000，两个板子的地址不能互抄；擦除最小单位就是一个扇区"],
        ["GPIO 核心", "板1", "gpio_core.h", "GPIO_OutInit / GPIO_OutWrite / GPIO_InInit / GPIO_InRead / GPIO_Claim", "不固定引脚（由调用方传 GPIO_TypeDef* + pin）", "带 Claim 的版本会登记占用者，冲突计数用 GPIO_ClaimConflictCount 查；同一脚被两次初始化是现场最常见的事故"],
        ["EXT_IO 外接开关量", "板1", "ext_io.h", "EXT_IO_Init / EXT_IR_Detected / EXT_TRACE_Detected / EXT_TOUCH_Detected / EXT_SOUND_Detected", "无独立引脚（板载端子，宏 EXT_*_ACTIVE_LOW = 1）", "红外 3 路、循迹 2 路、触摸 / 声音各 1 路；EXT_*_COUNT 决定有效 id 范围"],
        ["按键", "板1", "key.h", "KEY_Scan / KEY_Read / KEY_LongPress / KEY_EXTI_Enable", "KEY1 = PA0、KEY2 = PE2、KEY3 = PE3、KEY4 = PE4（KEY_COUNT = 4）", "板2 的编号顺序完全不同（KEY0 = PE4 … KEY3 = PA0）；KEY_Scan 内部已消抖 KEY_DEBOUNCE_MS = 10 ms，外面不要再 delay"],
        ["LED", "板1", "led.h", "LED_Init / LED_On / LED_Toggle / LED_Marquee / LED_ShowHex", "LED0 = PF9、LED1 = PF10、LED2 = PE13、LED3 = PE14（LED_COUNT = 4）", "板2 只有 LED0 / LED1 两个，LED_COUNT = 2；低电平点亮 LED_ACTIVE_LOW = 1"],
        ["蜂鸣器", "板1", "beep.h", "BEEP_Init / BEEP_Beep / BEEP_SOS / BEEP_AsyncStart / BEEP_Update", "PF8（顶到 3.3 V 才响，BEEP_ACTIVE_LOW = 0）", "异步模式必须周期性调 BEEP_Update；改用 SYS_TIM_Tone* 时就不要用这里的 GPIO 版本"],
        ["延时", "板1", "delay.h", "delay_ms / delay_us / delay_ns / delay_ms_dwt", "无独立引脚（走 SYSTICK / DWT）", "中断里不要用长延时；板2 的同名头文件多了 DWT_GetUs / DWT_ElapsedUs 两个测时函数"],
        ["LoRa E22 模块", "板1", "lora_e22.h", "LORA_E22_Init / LORA_E22_Send / LORA_E22_Recv / LORA_E22_SetMode / LORA_E22_SetBaud", "命令行走 SYS_USART_3；M0 = PB0、M1 = PB1、AUX = PB2 · 默认 9600", "板2 走 SYS_USART_6 且 M0/M1/AUX 换成 PE5/PE6/PE7；M0/M1 切换后再发 AT 要等 AUX 拉低"],
        ["以太网 MAC", "板1", "sys_eth.h", "SYS_ETH_Init / SYS_ETH_SendFrame / SYS_ETH_RecvFrame / SYS_ETH_LinkUp", "RMII：REF_CLK PA1、MDIO PA2、MDC PC1、CRS_DV PA7、TXD0 PG13、TXD1 PG14、TX_EN PG11", "SYS_ETH_ENABLE = 0 时后面的 PHY 函数整个不编译，链接报未定义先看这个宏"],
        ["Modbus RTU 从机", "板1", "sys_modbus.h", "SYS_MODBUS_Init / SYS_MODBUS_Poll / SYS_MODBUS_SetTxHook / SYS_MODBUS_Counters", "串口由调用方传（SYS_MODBUS_Init(uart, addr, baud, ...)）", "用 RS485 时必须在 TxHook 里翻 DE 脚；不做 3.5 字符静默间隔判断会把两帧粘成一帧"],
        ["MPU6050 六轴", "板1", "sys_mpu6050.h", "SYS_MPU6050_Init / SYS_MPU6050_Read / SYS_MPU6050_ReadPitchRoll / SYS_MPU6050_GetID", "I2C · 地址 0x68（SYS_I2C_ADDR_MPU6050）", "每个 API 第一个参数都是 SysI2cId_t 总线号，切总线不用改宏；量程宏 SYS_MPU6050_GYRO_FS / ACCEL_FS 都为 0（±250 dps / ±2 g）"],
        ["NVIC 中断管理", "板1", "sys_nvic.h", "SYS_NVIC_SetPriority / SYS_NVIC_EnableIRQ / SYS_NVIC_IsActive / SYS_NVIC_GetGroup", "无独立引脚（优先级分组 NVIC_PriorityGroup_4）", "分组 4 = 4 位全给抢占，没有子优先级；FreeRTOS 下用的中断抢占值要 ≥ 5"],
        ["片内 RTC", "板1", "sys_rtc.h", "SYS_RTC_SetTime / SYS_RTC_GetDate / SYS_RTC_SetAlarmDaily / SYS_RTC_WakeUp1sOn / SYS_RTC_BackupWrite", "无独立引脚；备份寄存器 SYS_RTC_BKP_COUNT = 19", "板2 的 RTC 是另一套 API（SYS_RTC_SetOnlyTime / SYS_RTC_AlarmSet / SYS_RTC_GetUnix），两者不能互抄"],
        ["电源管理", "板1", "sys_pwr.h", "SYS_PWR_Sleep / SYS_PWR_Stop / SYS_PWR_Standby / SYS_PWR_SetWakeupPin / SYS_PWR_GetWakeupFlag", "唤醒脚 WKUP（PA0）", "Stop 模式唤醒后时钟会丢，SYS_PWR_STOP_RESTORE_CLK = 1 才会自己恢复"],
        ["FreeRTOS 适配", "板1", "sys_rtos.h", "SYS_RTOS_PRESENT / SYS_RTOS_SYSTICK_RELOAD / SYS_TICK_Init", "无独立引脚（SysTick / PendSV / SVC）", "SYS_RTOS_PRESENT 由是否包含 FreeRTOS.h 自动判定；没有 FreeRTOS 时不要调 SYS_RTOS_SYSTICK_RELOAD"],
        ["软件定时器", "板1", "sys_softimer.h", "SYS_SOFTIMER_Add / SYS_SOFTIMER_Poll / SYS_SOFTIMER_Remove / SYS_SOFTIMER_Count", "无独立引脚 · 槽位上限 SYS_SOFTIMER_MAX = 8", "在主循环里调 SYS_SOFTIMER_Poll；回调里不要再调耗时函数"],
        ["字符串解析", "板1", "sys_str.h", "SYS_STR_Split / SYS_STR_ToInt / SYS_STR_ParseInt / SYS_STR_Format", "无独立引脚（纯软件模块）", "SYS_STR_Split 会改写入参字符串，传字面量会跑飞；SYS_STR_ParseInt 比 ToInt 多一个成功标志"],
        ["SysTick 时基", "板1", "sys_tick.h", "SYS_TICK_GetTick / SYS_TICK_Delay_ms / SYS_TICK_Timeout / SYS_TICK_Every", "无独立引脚 · SYS_TICK_PERIOD_MS = 1", "跑 FreeRTOS 时周期由内核接管（SYS_TICK_USE_RTOS = 1）；轮询型超时用 SYS_TICK_Timeout，别自己减计数器"],
        ["通用定时器", "板1", "sys_tim.h", "SYS_TIM_PwmInit / SYS_TIM_PwmSetDuty / SYS_TIM_CaptureInitIT / SYS_TIM_TonePlay / SYS_TIM_ServoSetAngle", "引脚由调用方传（id + ch + GPIO_TypeDef* + pin + af）", "PWM 占空比是千分比 0~1000；舵机宏 SYS_TIM_SERVO_FREQ_HZ = 50、500~2500 µs"],
        ["串口", "板1", "sys_usart.h", "SYS_USART_Init / SYS_USART_SendString / SYS_USART_Printf / SYS_USART_SendDMA / SYS_USART_DmaRxLen", "USART1 = PA9/PA10、USART2 = PA2/PA3、USART3 = PB10/PB11、UART4 = PC10/PC11、UART5 = PC12/PD2、USART6 = PC6/PC7", "printf 重定向固定走 SYS_USART_PRINTF_ID = SYS_USART_1；DMA 发送没完成时不能改发送缓冲"],
        ["独立/窗口看门狗", "板1", "sys_wdg.h", "SYS_WDG_Init / SYS_WDG_Feed / SYS_WDG_ResetCause / SYS_WDG_Heartbeat / SYS_WDG_WwdgFeed", "无独立引脚 · LSI 按 32 kHz 折算（SYS_WDG_LSI_HZ = 32000）", "超时范围 1~32768 ms（0.5 ms 步进）；SYS_WDG_HEARTBEAT_COUNT = 0 时心跳登记功能不编译"],
        ["RS485 半双工", "板1", "sys_rs485.h", "SYS_RS485_Init / SYS_RS485_Send / SYS_RS485_SendString / SYS_RS485_SetTx", "DE 脚由调用方传（uart + de_port + de_pin）", "板2 换成了 SYS_RS485_InitEx 与 SYS_RS485_SendTo，并且默认 DE = PG8；发完必须及时切回接收，否则收不到回包"],
        ["自定义数据帧", "板1", "sys_frame.h", "SYS_FRAME_Send / SYS_FRAME_Poll / SYS_FRAME_Feed / SYS_FRAME_BuildEnv", "无独立引脚（跑在任意 SysUsartId_t 上）", "帧头 AA 55、帧尾 55 AA、载荷上限 SYS_FRAME_MAX_PAYLOAD = 64；环境帧固定 SYS_FRAME_ENV_LEN = 12 字节，协议倍率在 T_SENSOR 表"],
        ["I2C 主机", "板1", "sys_i2c.h", "SYS_I2C_Init / SYS_I2C_IsDeviceReady / SYS_I2C_ReadBytes / SYS_I2C_WriteBytes / SYS_I2C_Scan", "I2C1 SCL = PB8 / SDA = PB9（板载 24C02 与 MPU6050，SSD1306 地址 0x3C）", "全操作超时封顶 SYS_I2C_TIMEOUT = 200000UL（板2 同名宏是 2000000UL）；器件不存在时 IsDeviceReady 返回负错误码，总线拉死先调 SYS_I2C_BusReset"],
        ["TFT 并口彩屏（板1）", "板1", "lcd.h", "LCD_Init / LCD_Clear / LCD_FillRect / LCD_DrawPoint / LCD_ShowString / LCD_SetRotation", "FSMC Bank1 NE4 · 命令 0x6C000000 / 数据 0x6C000080 · RS = A6 · CS = PG12 / WR = PD5 / RD = PD4 / BL = PB15 · 240×320", "板1 是 240×320 的 ILI9341 类屏，时序 LCD_FSMC_ADDR_SETUP 6 / HOLD 1 / DATA_SETUP 6；板1 没有 lcd_font.h，也没有只写不读以外的回读（读时序未配，ReadPoint 不可用）"],
        ["AT24C02 EEPROM", "板2", "at24c02.h", "AT24C02_WriteBytes / AT24C02_ReadBytes / AT24C02_WriteFloat / AT24C02_IsMagicSet", "I2C1 · 地址 SYS_I2C_ADDR_24C02 = 0x50 · 2 Kbit（256 字节）", "页写 8 字节，跨页必须分包，否则地址回卷覆盖；每次写完要等 AT24C02_WRITE_MS = 5 ms"],
        ["DHT11 温湿度（板2）", "板2", "dht11.h", "DHT11_ReadInt / DHT11_ReadRaw / DHT11_CheckSum / DHT11_TimeToNextRead", "单总线 · PG9（DHT11_PORT / DHT11_PIN）", "板1 是另一套：SYS_DHT11_Read(float*, float*)。两次读取间隔 ≥ DHT11_MIN_INTERVAL_MS = 1100 ms，位判定阈值 DHT11_BIT_THRESHOLD_US = 45"],
        ["DS18B20 单总线温度", "板2", "ds18b20.h", "DS18B20_StartConvert / DS18B20_ReadTempRaw / DS18B20_ReadScratch / DS18B20_SetResolution", "单总线 · PG9（DS18B20_PORT / DS18B20_PIN）", "12 位分辨率单次转换要等 DS18B20_CONV_MS_12 = 800 ms；初始化时序 500 µs 复位、释放 30 µs、存在脉冲 240 µs"],
        ["ESP8266 WiFi", "板2", "esp8266.h", "ESP8266_Init / ESP8266_JoinAP / ESP8266_ConnectTCP / ESP8266_SendString / ESP8266_HasAsyncData", "命令行走 SYS_USART_3 · 默认 115200", "AT 超时 ESP8266_CMD_TIMEOUT_MS = 2000、入网 15000 ms；接收缓冲 512 字节，长响应必须及时取走"],
        ["HC-SR04 超声波", "板2", "hcsr04.h", "HCSR04_Init / HCSR04_ReadCm / HCSR04_ReadUs / HCSR04_ReadCmMedian", "TRIG / ECHO 两个引脚由 HCSR04_Init 传入 · 声速 343400 mm/s", "量程 2~400 cm，超时 32000 µs；ECHO 是 5 V 电平要分压到 3.3 V 再进 STM32"],
        ["卡尔曼滤波", "板2", "kalman.h", "KALMAN_Init / KALMAN_Update / KALMAN_GetValue / KALMAN_AutoTune", "无独立引脚（纯软件模块）", "默认 Q = 0.01、R = 4.00（KALMAN_DEFAULT_Q / _R）；Q/R 调反了要么抖动要么迟钝，整数量用 KALMAN_UpdateInt"],
        ["按键（板2）", "板2", "key.h", "KEY_Scan / KEY_Read / KEY_LongPress / KEY_ActiveLow", "KEY0 = PE4、KEY1 = PE3、KEY2 = PE2、KEY3 = PA0 · KEY_COUNT = 4", "KEY3（PA0）是高有效 KEY3_ACTIVE_LOW = 0，另外三个低有效；比板1 多一个 KEY_ActiveLow 查询接口"],
        ["TFT 并口彩屏 ILI9481", "板2", "lcd.h", "LCD_Init / LCD_Clear / LCD_SetWindow / LCD_ShowString / LCD_SetRotation", "FSMC NE4：命令 0x6C000000、数据 0x6C000080（A6 当 RS）；背光 PB15（高有效）· 320×480", "命令与数据建立时间不同（LCD_FSMC_ADDR_SETUP 6 / DATA_SETUP 9）；开窗口后必须连续写满像素，中途换控制线会花屏"],
        ["TFT 字库", "板2", "lcd_font.h", "LCD_FONT8X16_W / LCD_FONT8X16_H / LCD_FONT8X16_CNT", "无独立引脚 · 8×16 点阵，95 个可打印字符", "纯宏头文件没有函数，显示字符走 lcd.h 的 LCD_ShowChar / LCD_ShowString"],
        ["LoRa E22（板2）", "板2", "lora_e22.h", "LORA_E22_Init / LORA_E22_Send / LORA_E22_Recv / LORA_E22_ReadConfig / LORA_E22_SetMode", "SYS_USART_6 · 9600；M0 = PE5、M1 = PE6、AUX = PE7", "板1 是 PB0/PB1/PB2 + SYS_USART_3；AUX 超时 1000 ms、AT 超时 500 ms，收发切换必须查 AUX"],
        ["MPU6050 六轴（板2）", "板2", "mpu6050.h", "MPU6050_Init / MPU6050_Read / MPU6050_GetTiltAngle / MPU6050_CalibrateGyro", "I2C1 · 0x68；中断脚 PC0（MPU6050_INT_PIN）", "板1 是 SYS_MPU6050_* 且总线号要逐个传参；这里默认量程 ±8 g / ±2000 dps、DLPF 44 Hz、采样分频 4"],
        ["MQTT 客户端", "板2", "mqtt.h", "MQTT_ConnectSimple / MQTT_PublishInt / MQTT_Subscribe / MQTT_Poll / MQTT_KeepAliveService", "TCP 1883（MQTT_DEFAULT_PORT）· 跑在 ESP8266 链路上", "客户 ID 要唯一，重复连会被服务器踢；主题上限 64、载荷上限 128 字节，周期调 KeepAliveService 否则被判定掉线"],
        ["NTC / PT100 测温", "板2", "ntc_pt100.h", "SENSOR_Init / SENSOR_ReadTempC / SENSOR_ReadResistance / SENSOR_Calibrate", "ADC1_IN5 · PA5（SENSOR_ADC_CH）· 上拉 100 kΩ", "NTC 的 B = 3950、PT100 的 R0 = 100 Ω / α = 0.00385；原始值超过 SENSOR_SAT_RAW = 4050 说明已饱和（断线或超量程）"],
        ["OLED 显示屏（板2）", "板2", "oled.h", "OLED_Init / OLED_RefreshDirty / OLED_ShowString / OLED_ShowFixed / OLED_ShowProgress", "I2C1 · 0x3C · 128×64", "只改了几个像素用 OLED_RefreshDirty（继承脏页标记）比整屏 OLED_Refresh 快得多；板1 是 SYS_OLED_* 且刷新按 page 传参"],
        ["PID 闭环", "板2", "pid.h", "PID_Init / PID_Update / PID_SetOutputLimit / PID_GetTerms", "无独立引脚（纯软件模块）", "输出默认限幅 −100~+100；只有在输出饱和时才需要抗积分饱和，阈值用 PID_SetSeparateThreshold 设"],
        ["RGB 5×5 灯板", "板2", "rgb5x5.h", "RGB5X5_SetPixel / RGB5X5_SetPixelHSV / RGB5X5_Show / RGB5X5_SetBrightness", "数据线 PC5（RGB5X5_DATA_PIN）· 25 颗灯", "GRB 顺序（RGB5X5_COLOR_ORDER_GRB = 1）；改完颜色必须调 RGB5X5_Show，复位低电平要 ≥ 300 µs"],
        ["外扩 SRAM", "板2", "sram.h", "SRAM_Init / SRAM_ReadWords / SRAM_WriteBytes / SRAM_SpeedTestUs / SRAM_Test", "FSMC Bank1 NORSRAM3 · 基址 0x68000000 · 1 MB（512 K 字）", "地址建立 0 / 数据建立 12 个 HCLK；直接当普通数组用之前先调 SRAM_Init，否则一读就硬件错误"],
        ["ADC 采集（板2）", "板2", "sys_adc.h", "SYS_ADC_Read / SYS_ADC_ReadAvg / SYS_ADC_DmaScanInit / SYS_ADC_ReadMilliVolt", "ADC3_IN5 · PF7（光敏）、ADC1_IN5 · PA5（MQ-135）", "比板1 多一组 ADC1 通道宏；SYS_ADC_CONFLICT 在同一个 ADC 被两处初始化时计数"],
        ["位带操作（板2）", "板2", "sys_bitband.h", "PAout / PGin / BITBAND_SRAM / GPIO_BB_OUT", "地址区 0x42000000（外设）/ 0x22000000（SRAM）", "与板1 同名同实现；位带别名区对 DWT、FSMC 等区不生效"],
        ["气压温度 BMP280", "板2", "sys_bmp280.h", "SYS_BMP280_Read / SYS_BMP280_ReadRaw / SYS_BMP280_Trigger / SYS_BMP280_AltitudeM", "I2C1 · 0x76（SDO 接地）/ 0x77（接 VDD）", "0xD0 芯片 ID 必须为 0x58；驱动默认 osrs_t=2、osrs_p=3、IIR=2，强制定时模式延时 15 ms"],
        ["CAN 总线", "板2", "sys_can.h", "SYS_CAN_Init / SYS_CAN_Transmit / SYS_CAN_Receive / SYS_CAN_FilterById / SYS_CAN_GetErrors", "CAN1 · TX = PA12、RX = PA11 · APB1 42 MHz", "没有 120 Ω 终端电阻时收发全错；滤波器不放行就一条都收不到；接收队列 8 帧，满了丢帧计数在 SYS_CAN_DropCount"],
        ["空气质量 CCS811", "板2", "sys_ccs811.h", "SYS_CCS811_Read / SYS_CCS811_DataReady / SYS_CCS811_SetEnvData / SYS_CCS811_GetBaseline", "I2C1 · 0x5A（ADDR 接地）/ 0x5B", "HW_ID 必须 0x81，之后要发 APP_START 才出数据；基线存起来下次写回可跳过 48 小时老化"],
        ["时钟配置（板2）", "板2", "sys_clock.h", "SYS_CLK_GetFreq / SYS_CLK_GetBusFreq / SYS_CLK_Switch / SYS_CLK_SetBusDiv", "HCLK ≤ 168 MHz、PCLK1 ≤ 42 MHz、PCLK2 ≤ 84 MHz", "和板1 同一套 API；切 HSE 失败会返回 SYS_CLK_ERR_HSE 并停在 HSI，串口波特率会整体偏掉"],
        ["DAC 模拟输出", "板2", "sys_dac.h", "SYS_DAC_SetValue / SYS_DAC_SetMilliVolt / SYS_DAC_SineInit / SYS_DAC_DmaInit", "DAC1 = PA4、DAC2 = PA5（SYS_DAC2_ENABLE = 0 默认关闭）", "PA5 同时是 SPI1_SCK 和 ADC1_IN5，三者只能留一个；正弦表 32 点，定时器 TIM6 触发"],
        ["DMA（板2）", "板2", "sys_dma.h", "SYS_DMA_PeriphToMem / SYS_DMA_SetCallback / SYS_DMA_Remain / SYS_DMA_GetError", "流由调用方传入（DMA_Stream_TypeDef *）", "和板1 同名同实现；错误统计用 SYS_DMA_ErrCount / SYS_DMA_LastErrStream 定位"],
        ["编码器测速", "板2", "sys_encoder.h", "SYS_ENCODER_Init / SYS_ENCODER_GetDelta / SYS_ENCODER_GetTotal / SYS_ENCODER_DeltaToRpm", "定时器编码器模式由调用方传（id + SysTimId_t + 引脚）", "模式固定 TI12；上限 SYS_ENCODER_MAX = 2 路；计数差用 GetDelta，累计值会溢出要自己处理"],
        ["以太网 MAC（板2）", "板2", "sys_eth.h", "SYS_ETH_Init / SYS_ETH_LinkUp / SYS_ETH_ReadPHY / SYS_ETH_SendFrame / SYS_ETH_RecvFrame", "RMII：REF_CLK PA1、MDIO PA2、MDC PC1、RXD0 PC4、RXD1 PC5、TX_EN PG11 · PHY 地址 0", "SYS_ETH_ENABLE = 0——这份工程默认根本没开以太网，PHY 相关函数整段不编译"],
        ["故障日志（板2）", "板2", "sys_fault.h", "SYS_FAULT_Init / SYS_FAULT_Report / SYS_FAULT_Happened / SYS_FAULT_SetCallback", "无独立引脚（默认只解析不自动复位）", "和板1 同一套 API；HardFault 里只能用 Report 攒字符串，不能调用会阻塞的接口"],
        ["数字滤波", "板2", "sys_filter.h", "FILTER_Median / FILTER_MovingAvg / FILTER_TrimmedMean / FILTER_LpfUpdate / FILTER_AvgUpdate", "无独立引脚 · 缓冲上限 FILTER_BUF_MAX = 32", "滑动平均默认窗口 FILTER_AVG_DEFAULT_SIZE = 8；一阶低通要先用 FILTER_LpfInit 算系数再用 FILTER_LpfUpdate"],
        ["片内 FLASH（板2）", "板2", "sys_flash.h", "SYS_FLASH_SaveParams / SYS_FLASH_LoadParams / SYS_FLASH_EraseSector / SYS_FLASH_Write", "参数扇区 0x080E0000（备用 0x080C0000）· MAGIC 0x50415241", "地址和板1（0x08070000）完全不同；写之前必须整扇区擦除，擦一次 1 MB 芯片要几十毫秒"],
        ["自定义数据帧（板2）", "板2", "sys_frame.h", "SYS_FRAME_Send / SYS_FRAME_Poll / SYS_FRAME_Get / SYS_FRAME_Verify", "无独立引脚（跑在任意 SysUsartId_t 上）", "比板1 多出查询应答 / 历史 / 补传命令（SYS_FRAME_CMD_QUERY / HISTORY / REPLAY），查询应答固定 16 字节"],
        ["I2C 主机（板2）", "板2", "sys_i2c.h", "SYS_I2C_Init / SYS_I2C_WriteByte / SYS_I2C_ReadBytes / SYS_I2C_IsDeviceReady / SYS_I2C_Scan", "I2C1 = PB8/PB9、I2C2 = PB10/PB11、I2C3 = PA8/PC9", "地址常量齐全（24C02 0x50、MPU6050 0x68、AHT10 0x38、SHT30 0x44、SSD1306 0x3C）；超时 2000000 次循环，比板1 的 200000 大 10 倍"],
        ["LoRa / 串口设备（板2）", "板2", "lora_e22.h", "LORA_E22_WaitReady / LORA_E22_IsBusy / LORA_E22_Flush / LORA_E22_SetBaud", "AUX = PE7、M0/M1 = PE5/PE6", "如果只做数据透传：先 WaitReady 再 Send，两帧之间留 SYS_LORA_FRAME_GAP_MS = 10 ms"],
        ["RS485 半双工（板2）", "板2", "sys_rs485.h", "SYS_RS485_SendTo / SYS_RS485_SendStringTo / SYS_RS485_SetTxTo / SYS_RS485_ReadBuf", "DE = PG8（高电平发送）；默认走 SYS_USART_2、9600", "同一套 API 要同时管多个串口，所以每个函数都比板1 多一个 uart 参数；DE 翻转太早会把最后一个字节切掉"],
        ["RTC 实时时钟（板2）", "板2", "sys_rtc.h", "SYS_RTC_SetOnlyTime / SYS_RTC_GetUnix / SYS_RTC_AlarmSet / SYS_RTC_WakeUpInit / SYS_RTC_Format", "无独立引脚 · 走 LSE 32.768 kHz（SYS_RTC_LSE_HZ）", "板1 是 SYS_RTC_SetTime(h, m, s) 那一套；本板给 Unix 时间戳接口，另有 19 个备份寄存器（SYS_RTC_BKP_REG_MAX）"],
        ["SPI 主机（板2）", "板2", "sys_spi.h", "SYS_SPI_Init / SYS_SPI_TransferByte / SYS_SPI_Transfer / SYS_SPI_Write / SYS_SPI_SetSpeed", "SPI1 = PB3/PB4/PB5（SCK/MISO/MOSI）、SPI2 = PB13/PB14/PB15", "SYS_SPI_FREE_JTAG = 1 会释放 PB3/PB4（默认是 JTAG 脚）；SPI3 的宏和 SPI1 撞在同一组引脚上，不能同时用"],
        ["字符串解析（板2）", "板2", "sys_str.h", "SYS_STR_Split / SYS_STR_ToInt / SYS_STR_ParseInt / SYS_STR_Format", "无独立引脚（纯软件模块）", "与板1 同一套实现；解析来自串口的整行文本用 Split + ParseInt 两步"],
        ["SysTick 时基（板2）", "板2", "sys_tick.h", "SYS_TICK_GetTick / SYS_TICK_Delay_ms / SYS_TICK_GetUs / SYS_TICK_Every", "无独立引脚 · SYS_TICK_PERIOD_MS = 1", "跑 FreeRTOS 时 1 ms 节拍由内核的 vPortSetupTimerInterrupt 提供，别再自己再初始化一次"],
        ["通用定时器（板2）", "板2", "sys_tim.h", "SYS_TIM_PwmInit / SYS_TIM_PwmSetDuty / SYS_TIM_CaptureInitIT / SYS_TIM_OcInitIT / SYS_TIM_EtrInit", "引脚由调用方传（id + ch + GPIO_TypeDef* + pin + af）", "与板1 同一套 API；TB6612 的 PWMA/PWMB 就挂在 SYS_TIM_4 的 CH1/CH2 上"],
        ["串口（板2）", "板2", "sys_usart.h", "SYS_USART_Init / SYS_USART_SendString / SYS_USART_Printf / SYS_USART_SendDMA / SYS_USART_DmaRxDone", "USART1 = PA9/PA10、USART2 = PA2/PA3、USART3 = PB10/PB11、USART6 = PC6/PC7", "printf 重定向同样固定在 SYS_USART_1；ESP8266 占 USART3、RS485 占 USART2、LoRa 占 USART6，调试口只剩 USART1"],
        ["看门狗（板2）", "板2", "sys_wdg.h", "SYS_WDG_Init / SYS_WDG_Feed / SYS_WDG_ResetCause / SYS_WDG_HeartbeatPoll / SYS_WDG_WwdgInit", "无独立引脚 · LSI 按 32000 Hz 折算", "与板1 同一套；复位原因用 SYS_WDG_ResetCauseDecode 解成文本，IWDG / WWDG 分别对应 SYS_WDG_RST_IWDG / _WWDG"],
        ["TB6612 直流电机", "板2", "tb6612.h", "TB6612_SetMotor / TB6612_Car / TB6612_CarTank / TB6612_CarRamp / TB6612_Standby", "PWMA = PB6（TIM4_CH1）、PWMB = PB7（TIM4_CH2）；方向 AIN1/AIN2 = PG2/PG3、BIN1/BIN2 = PG4/PG5；STBY = PG13", "PWM 2 kHz、速度 0~1000（TB6612_SPEED_MAX）；STBY 不拉高电机一点不动；TB6612_CarRamp 阻塞式渐变，别放在高优先级任务里"],
        ["ULN2003 步进电机", "板2", "uln2003.h", "ULN2003_Step / ULN2003_OneStep / ULN2003_Stop / ULN2003_SetStepDelay / ULN2003_GetPhase", "IN1~IN4 = PC1/PC2/PC3/PC4", "8 拍模式每圈 2038 步（ULN2003_STEPS_PER_REV）、单步延时 1500 µs；堵转时不要一直保持通电，用 ULN2003_Stop 掉"],
        ["VL53L0X 激光测距", "板2", "vl53l0x.h", "VL53L0X_Init / VL53L0X_ReadMm / VL53L0X_StartContinuous / VL53L0X_ReadContinuousMm / VL53L0X_SetTimingBudget", "I2C1 · 0x29 · 400 kHz（VL53L0X_I2C_SPEED）", "量程 30~2000 mm，预算默认 33000 µs；必须读完内部 80 字节校准数据（不读报 SYS 级的 SPAD 错误）"],
        ["W25Q128 SPI Flash", "板2", "w25qxx.h", "W25QXX_Read / W25QXX_WriteSafe / W25QXX_EraseSector / W25QXX_ReadID / W25QXX_WaitBusy", "SPI1 · CS = PB14 · 16 MB（0xEF4018）", "扇区 4 KB、页 256 字节；跨页写必须用 W25QXX_WriteSafe（内部自动分页），直接 Write 会覆盖页首"],
        ["W25QXX 掉电日志", "板2", "w25qxx_log.h", "W25QXX_LogWrite / W25QXX_LogRead / W25QXX_LogCount / W25QXX_LogTail / W25QXX_LogStat", "占用 W25Q128 的 0x001000 起 16 个扇区（W25QXX_LOG_SECTORS）", "槽 32 字节、头 12 字节（有效载荷 20 字节）；写满后旧数据被覆盖，丢失计数看 W25QXX_LogLost"],
        ["XPT2046 触摸", "板2", "xpt2046.h", "XPT2046_Init / XPT2046_Read / XPT2046_GetEvent / XPT2046_Calibrate / XPT2046_CalibSave", "软件模拟 SPI：CS = PC13、CLK = PB0、DIN = PF11、DOUT = PB2、PEN = PB1（低有效）", "用的是软件时序不是 SPI 外设（PB0/PB2 也算不上 SPI 复用脚）；屏幕原始坐标是 240×320 而 TFT 是 320×480，必须做触摸校准（XPT2046_CalibLoad）才能对上"],
        ["SPI Flash 日志（板2 备选）", "板2", "w25qxx_log.h", "W25QXX_LogWriteStr / W25QXX_LogClear / W25QXX_LogCapacity / W25QXX_LogErrStr", "基址宏 W25QXX_LOG_BASE 指向 0x001000", "文本日志用 LogWriteStr 更省事；容量查询用 LogCapacity，头尾指针用 LogStat 一次取全"],
        ["按键 / LED / 蜂鸣器 / 延时（板2 合订）", "板2", "key.h · led.h · beep.h · delay.h", "KEY_Scan / LED_Toggle / BEEP_Beep / delay_ms / DWT_ElapsedUs", "按键 KEY0~KEY3 = PE4/PE3/PE2/PA0；LED0/LED1 = PF9/PF10；蜂鸣器 PF8", "按键顺序和板1 相反、LED 只有 2 个，这两个差异是最容易抄错的；delay.h 比板1 多 DWT_GetUs / DWT_ElapsedUs 两个微秒测时函数"]
    ],
    k: "驱动一览 外设驱动 头文件 板1 检测端 板2 数据通信端 sys_ 前缀 API 引脚 FSMC 地址 I2C 地址 SPI 外设 差异 对照 工程结构"
};

/* ---------------- 19) OTA 分区与标志位表（IAP 片上升级） ---------------- */
/* 地址全部按“把 App 从 0x08000000 挪到 0x08004000”的规划给，参数区沿用工程里
 * 现有的 SYS_FLASH_PARAM_ADDR / ADDR2，不动它们。两块芯片扇区大小不同，
 * 板2 容量富余所以下载区更大；两块表的“代码不得越过”一列是硬约束。 */
const T_OTA = {
    m: "查找表", t: "table",
    n: "OTA 分区与标志位表（板1 ZE 512KB / 板2 ZG 1MB）",
    p: "做片上升级（IAP / OTA）第一步就是定分区：Bootloader、运行区、下载区、参数区四段不能重叠，且每段都必须落在扇区边界上。表里的参数区两行就是本工程 sys_flash 已在用的地址，升级流程不许碰。",
    d: "升级分区速查：搜 OTA / 升级 / 分区 / 扇区 / 下载区 / 备份区 / bootloader / 固件头 / 标志位，查每段区域的起止地址、容量与硬约束。",
    h: ["区 / 标志", "板1（ZE 512KB）", "板2（ZG 1MB）", "作用与约束"],
    k: "OTA IAP 升级 分区 扇区 地址 Bootloader 引导 应用区 运行区 App 下载区 备份区 暂存 参数区 标志位 备份寄存器 固件头 magic 魔数 crc32 版本 回滚 断电安全",
    a: [
        ["Bootloader", "0x08000000，Sector 0，16KB", "同左（Sector 0，16KB）", "芯片复位后的入口，永远不动。只做四件事：看有没有待安装固件 → 校验 → 搬运 → 跳转"],
        ["App 运行区", "0x08004000，Sector 1~3，48KB", "0x08004000，Sector 1~3，48KB", "App 的链接地址（Keil IROM1）与跳转地址都必须是它；本仓库代码约 30KB（板1）/ 43KB（板2），48KB 够用但不宽裕，加功能前先看 .map"],
        ["下载区（新固件暂存）", "0x08010000，Sector 4，64KB", "0x08010000，Sector 4~5，192KB", "新固件先写这里，校验通过才搬运；容量必须 ≥ 运行区，绝不能和运行区重叠"],
        ["预留 / 扩展", "0x08020000 ~ 0x0805FFFF（Sector 5~6，256KB）", "0x08040000 ~ 0x080BFFFF（Sector 6~9，512KB）", "字库、日志、双份配置以后要用；定分区时就不要占"],
        ["参数区 A（在用）", "0x08070000（Sector 7 后半段）", "0x080E0000（Sector 11）", "就是工程里的 SYS_FLASH_PARAM_ADDR；升级流程里任何擦除都不许落在它身上"],
        ["参数区 B（在用）", "0x08060000（Sector 7 前半段）", "0x080C0000（Sector 10）", "SYS_FLASH_PARAM_ADDR2，A/B 轮换的第二块；两块不同扇区，掉电可回滚"],
        ["代码不得越过", "0x08060000", "0x080E0000", "越过去代码就落进参数区扇区，保存参数时被整扇区擦掉（本仓库 README 已写明这条约束）"],
        ["固件头（32 字节）", "magic / version / size / crc32 / 保留 16B", "字段顺序两端必须完全一致", "放在下载区最前面。magic 判“是不是有效固件”（本表示例用 0x4F544131），size 用来卡“不能超过运行区容量”"],
        ["待安装标志", "备份寄存器 RTC_BKP_DR1 = 0xA5A50001", "同左", "掉电保持，比写 Flash 快得多。注意 RTC_BKP_DR0 已被“已初始化 0x5A5A”占用，别混用"],
        ["标志取值", "0 = 无 / 1 = 待安装 / 2 = 已安装", "同左", "Bootloader 只认 1 才搬运；搬完并回读校验通过后置 2，再跳转"],
        ["运行区里的 App 要改的", "Keil IROM1 = 0x08004000；VECT_TAB_OFFSET = 0x4000", "同左", "两个都要改。只改 IROM1 不改向量表偏移，一进中断就跑到老表上去（最经典的 IAP 事故）"]
    ]
};

/* ---------------- 20) FreeRTOS 任务版 / 中断版 API 对照表 ---------------- */
const T_RTOS_API = {
    m: "查找表", t: "table",
    n: "FreeRTOS 任务版 / 中断版 API 对照表（含优先级规则）",
    p: "同一件事在任务里和中断里用的函数不一样：中断里只能调 xxxFromISR 版本，且要把 pxHigherPriorityTaskWoken 交给 portYIELD_FROM_ISR。表里最后一节是本工程的优先级硬约束。",
    d: "FreeRTOS 中断安全速查：搜 中断 FromISR 任务版 对照 临界区 中断优先级 分组 即可看到该用哪个函数、以及优先级要求。",
    h: ["要做的事", "任务里用", "中断里用（FromISR 系列）", "要点"],
    k: "FreeRTOS FromISR 中断 任务版 对照 临界区 中断优先级 分组 抢占 NVIC 队列 信号量 任务通知 事件组 延时 让出 删除",
    a: [
        ["往队列发消息", "xQueueSend / xQueueSendToFront", "xQueueSendFromISR / xQueueSendToFrontFromISR", "中断版多一个 pxHigherPriorityTaskWoken 指针参数，其余一样"],
        ["从队列取消息", "xQueueReceive", "xQueueReceiveFromISR", "中断里取数据很少见；能在任务里取就别在 ISR 里取"],
        ["覆盖式写入（长度 1 的队列）", "xQueueOverwrite", "xQueueOverwriteFromISR", "“最新值信箱”专用：不被积压，永远只有最新一份"],
        ["释放信号量", "xSemaphoreGive", "xSemaphoreGiveFromISR", "中断里只能用它；本工程串口空闲中断就是靠它通知接收任务"],
        ["获取信号量", "xSemaphoreTake（可等待）", "xSemaphoreTakeFromISR（极少用）", "中断里不要等：ISR 必须尽快返回，等待请交给任务"],
        ["给一个任务发通知", "xTaskNotifyGive", "xTaskNotifyGiveFromISR", "比信号量少创建一个对象；只能一对一，不能广播"],
        ["等通知", "ulTaskNotifyTake / xTaskNotifyWait", "（ISR 侧只有“给”，没有“等”）", "接收方在任务里等，ISR 里只负责发"],
        ["事件组置位 / 等位", "xEventGroupSetBits / xEventGroupWaitBits", "xEventGroupSetBitsFromISR（本工程未开 trace，不可用）", "要让多个任务等同一组条件，用事件组而不是任务通知"],
        ["延时", "vTaskDelay / vTaskDelayUntil", "（ISR 里绝对不能延时）", "ISR 里要“稍后再做”，用软件定时器或 xTimerPendFunctionCall"],
        ["主动让出 CPU", "taskYIELD", "portYIELD_FROM_ISR(woken)", "ISR 里必须按 woken 的真假决定切不切，否则唤醒的任务要等下一次节拍"],
        ["临界区", "taskENTER_CRITICAL / taskEXIT_CRITICAL", "taskENTER_CRITICAL_FROM_ISR / taskEXIT_CRITICAL_FROM_ISR(mask)", "ISR 版必须把 mask 原样传回；临界区里不能调 FreeRTOS API"],
        ["删除对象", "vQueueDelete / vSemaphoreDelete", "（ISR 里不删对象）", "确认没有任务还在等它再删"],
        ["中断优先级要求", "—", "要调 FromISR 的中断，抢占优先级数值必须 ≥ 5", "本工程 configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY = 5，sys_usart / sys_tim / sys_exti 等都配成 5，正好可用"],
        ["NVIC 分组要求", "必须 NVIC_PriorityGroup_4（4 位全做抢占）", "同左", "改成 Group_2 会把 PRE=5 截成 1（0x10 < BASEPRI 0x50），ISR 越过内核保护，configASSERT 会抓——所以 SYS_NVIC_Init() 那两句不能删"]
    ]
};

const STD_TABLES2 = [T_STRUCT, T_PINS, T_TIMCH, T_ADCCH, T_CLOCK, T_I2CADDR, T_FREERTOS, T_FORMULA,
                      T_BAUD, T_SPI, T_I2C, T_CAN, T_WDG, T_MAPBIT, T_W25Q, T_IRQPRIO, T_SENSOR, T_DRIVER,
                      T_OTA, T_RTOS_API];

/* ---------------- 给“复用功能表”补“对应外设”列 ---------------- */
/* DFP 里只有 GPIO_AF_x 宏本身，这里按 F407 的复用功能分配补全；
 * 同一 AF 号在不同引脚上可能接到不同外设，具体引脚仍以数据手册为准。 */
(function () {
    const AF_USE = {
        "AF0": "MCO1/MCO2（时钟输出）、JTAG/SWD 调试口",
        "AF1": "TIM1、TIM2",
        "AF2": "TIM3、TIM4、TIM5",
        "AF3": "TIM8、TIM9、TIM10、TIM11",
        "AF4": "I2C1、I2C2、I2C3",
        "AF5": "SPI1、SPI2",
        "AF6": "SPI3",
        "AF7": "USART1、USART2、USART3",
        "AF8": "UART4、UART5、USART6",
        "AF9": "CAN1、CAN2、TIM12、TIM13、TIM14",
        "AF10": "USB OTG_FS、USB OTG_HS",
        "AF11": "ETH（以太网 MII/RMII）",
        "AF12": "FSMC、SDIO、USB OTG_HS（ULPI）",
        "AF13": "DCMI（摄像头接口）",
        "AF14": "LCD-TFT（F429 才有，F407 不用）",
        "AF15": "EVENTOUT（事件输出）"
    };
    if (typeof STD_TABLES === "undefined") return;
    STD_TABLES.forEach(function (t) {
        if (String(t.n).indexOf("复用功能表") !== 0) return;
        t.a.forEach(function (r) {
            if (!r[2] || r[2] === "") r[2] = AF_USE[r[0]] || "";
        });
        t.p = "GPIO_PinAFConfig 的 GPIO_AF 参数用这里的宏；“对应外设”是该复用号的常见用途，同一 AF 号在不同引脚上接的外设不同，具体引脚见“外设引脚映射表”与数据手册。";
    });


    /* 给「时钟使能表」「DMA 请求映射表」补“说明”列，并补「看门狗配置表」空的“常用配置” */
    var CLK_NOTE = {"ADC": "ADC 公共部分（ADC1/2/3 共享的公共寄存器，三者的时钟由它一起控制）", "ADC1": "ADC1（12 位逐次逼近，规则/注入通道；F407 三 ADC 可交替采样）", "ADC2": "ADC2（与 ADC1 可组成双重/三重模式提高采样率）", "ADC3": "ADC3（在 F407 上只挂部分引脚，另有独立通道）", "BKPSRAM": "备份域 SRAM（4 KB）：主电源掉电后靠 VBAT 保数据，需先开 PWR 时钟并允许备份域访问", "CAN1": "CAN1 控制器（工业总线，需外接收发器如 TJA1050）", "CAN2": "CAN2 控制器（与 CAN1 共享 512 字节过滤器）", "CCMDATARAMEN": "CCM 数据 RAM（64 KB，地址 0x10000000）：只有内核能读写，DMA 访问不到，适合放栈或变量", "CRC": "CRC 计算单元：硬件算 CRC-32/16，也常被拿来当伪随机数发生器", "CRYP": "硬件加解密（AES/DES/TDES），F407 有；本项目未用", "DAC": "12 位 DAC（PA4/PA5 输出模拟电压，也可输出噪声/三角波）", "DCMI": "数字摄像头接口：并行取摄像头数据（配合 DMA），本项目未用", "DMA1": "DMA1 控制器（8 条数据流）：外设↔内存搬运，不占 CPU", "DMA2": "DMA2 控制器（8 条数据流，带双缓冲）：本工程串口/LoRa 收发靠它", "DMA2D": "图形加速 DMA2D（仅 F42x/F43x 有，F407 没有这个外设，工程里用不到）", "ETH_MAC": "以太网 MAC（F407 有，需外接 PHY）：本项目未用以太网", "ETH_MAC_PTP": "以太网 PTP 时间戳（精确时钟协议）子模块", "ETH_MAC_Rx": "以太网接收通道（独立时钟门控，收方向）", "ETH_MAC_Tx": "以太网发送通道（独立时钟门控，发方向）", "FLITF": "Flash 接口（取指/预取缓冲）：跑高频时必须开，否则 FLASH 等待周期取指会慢", "FMC": "外部存储控制器 FMC（F42x/F43x 的新名，F407 用 FSMC）", "FSMC": "灵活的静态存储控制器：本工程板2 的 3.5 寸 TFT 与板1 的外扩 SRAM 都靠它", "HASH": "硬件哈希（MD5/SHA1），F407 有；本项目未用", "I2C1": "I2C1 总线（板1 的 0.96 寸 OLED 接在这里：PB8/PB9）", "I2C2": "I2C2 总线（常用 PB10/PB11）", "I2C3": "I2C3 总线（常用 PA8/PC9）", "LTDC": "LCD-TFT 控制器（F42x/F43x 才有；F407 的 TFT 走 FSMC，不用它）", "OTG_FS": "USB OTG 全速（片内 FS PHY，直接接 USB 口即可）", "OTG_HS": "USB OTG 高速（需外接 ULPI PHY，本项目未用）", "OTG_HS_ULPI": "USB OTG HS 的 ULPI 接口时钟（外接 PHY 时用）", "PWR": "电源管理接口：低功耗模式、PVD 电压监测、备份域访问都要先开它", "RNG": "硬件随机数发生器（32 位真随机）", "SAI1": "SAI 音频接口（F42x/F43x 才有，F407 没有）", "SDIO": "SDIO 控制器（SD/TF 卡，1/4 位总线 + DMA）", "SPI1": "SPI1（APB2，最高 42 MHz SCK；板2 的 W25Q128 与 LoRa 挂这里）", "SPI2": "SPI2 / I2S2（APB1）", "SPI3": "SPI3 / I2S3（APB1）", "SPI4": "SPI4（F407 无此 SPI，DFP 头文件里通用定义）", "SPI5": "SPI5（F407 无此 SPI）", "SPI6": "SPI6（F407 无此 SPI）", "SRAM1": "SRAM1（112 KB 主体，0x20000000）：普通变量/堆栈/DMA 缓冲", "SRAM2": "SRAM2（16 KB，紧随 SRAM1）", "SRAM3": "SRAM3（64 KB）：F407 上就是 CCM 区，DMA 不能访问", "SYSCFG": "系统配置控制器：EXTI 的引脚映射（SYSCFG_EXTILineConfig）必须靠它", "UART4": "UART4（只有收发，无同步/流控；APB1）", "UART5": "UART5（同 UART4）", "UART7": "UART7（F407 无此串口）", "UART8": "UART8（F407 无此串口）", "USART1": "USART1（APB2，最高 84 MHz 时钟；调试串口常用 PA9/PA10）", "USART2": "USART2（APB1；板2 与 CH340/USB-TTL 的调试口常用 PA2/PA3）", "USART3": "USART3（APB1；板1 与 LoRa 通信常用 PB10/PB11）", "USART6": "USART6（APB2；常用 PC6/PC7）", "WWDG": "窗口看门狗（喂狗必须在窗口内，喂早也复位）"};
    var DMA_NOTE = {"USART1_RX": "USART1 接收（RXNE 触发，每收一字节搬一次）", "USART1_TX": "USART1 发送（TXE 触发，发完一字节自动补）", "USART2_RX": "USART2 接收（RXNE 触发，每收一字节搬一次）", "USART2_TX": "USART2 发送（TXE 触发，发完一字节自动补）", "USART3_RX": "USART3 接收（RXNE 触发，每收一字节搬一次）", "USART3_TX": "USART3 发送（TXE 触发，发完一字节自动补）", "USART6_RX": "USART6 接收（RXNE 触发，每收一字节搬一次）", "USART6_TX": "USART6 发送（TXE 触发，发完一字节自动补）", "UART4_RX": "UART4 接收（RXNE 触发，每收一字节搬一次）", "UART4_TX": "UART4 发送（TXE 触发，发完一字节自动补）", "UART5_RX": "UART5 接收（RXNE 触发，每收一字节搬一次）", "UART5_TX": "UART5 发送（TXE 触发，发完一字节自动补）", "SPI1_RX": "SPI1 接收（RXNE 触发）", "SPI1_TX": "SPI1 发送（TXE 触发）", "SPI2_RX": "SPI2 接收（RXNE 触发）", "SPI2_TX": "SPI2 发送（TXE 触发）", "SPI3_RX": "SPI3 接收（RXNE 触发）", "SPI3_TX": "SPI3 发送（TXE 触发）", "I2C1_RX": "I2C1 接收（RXNE 触发）", "I2C1_TX": "I2C1 发送（TXE 触发）", "I2C2_RX": "I2C2 接收（RXNE 触发）", "I2C2_TX": "I2C2 发送（TXE 触发）", "I2C3_RX": "I2C3 接收（RXNE 触发）", "I2C3_TX": "I2C3 发送（TXE 触发）", "ADC1": "ADC1 转换完成（每完成一次规则转换自动搬走结果）", "ADC2": "ADC2 转换完成（每完成一次规则转换自动搬走结果）", "ADC3": "ADC3 转换完成（每完成一次规则转换自动搬走结果）", "DAC1": "DAC1 输出（由定时器触发，从内存搬数据到 DAC 寄存器）", "DAC2": "DAC2 输出（由定时器触发，从内存搬数据到 DAC 寄存器）", "SDIO": "SDIO 数据搬运（SD 卡读写，配合 4 位总线）", "TIM1_CH1": "TIM1 的 CH1 比较/捕获事件（PWM 占空比更新、输入捕获搬数据）", "TIM1_CH2": "TIM1 的 CH2 比较/捕获事件（PWM 占空比更新、输入捕获搬数据）", "TIM1_CH3": "TIM1 的 CH3 比较/捕获事件（PWM 占空比更新、输入捕获搬数据）", "TIM1_CH4": "TIM1 的 CH4 比较/捕获事件（PWM 占空比更新、输入捕获搬数据）", "TIM1_UP": "TIM1 更新事件（计数溢出/重装，常用来周期性触发 DMA）", "TIM2_CH1": "TIM2 的 CH1 比较/捕获事件（PWM 占空比更新、输入捕获搬数据）", "TIM2_CH2": "TIM2 的 CH2 比较/捕获事件（PWM 占空比更新、输入捕获搬数据）", "TIM2_CH3 / TIM2_UP": "TIM2_CH3 的比较/捕获事件与 TIM2_UP 的更新事件共用同一个 DMA 请求（同一数据流通道）", "TIM2_CH4": "TIM2 的 CH4 比较/捕获事件（PWM 占空比更新、输入捕获搬数据）", "TIM3_CH1": "TIM3 的 CH1 比较/捕获事件（PWM 占空比更新、输入捕获搬数据）", "TIM3_CH2": "TIM3 的 CH2 比较/捕获事件（PWM 占空比更新、输入捕获搬数据）", "TIM3_CH3": "TIM3 的 CH3 比较/捕获事件（PWM 占空比更新、输入捕获搬数据）", "TIM3_CH4 / TIM3_UP": "TIM3_CH4 的比较/捕获事件与 TIM3_UP 的更新事件共用同一个 DMA 请求（同一数据流通道）", "TIM4_CH1": "TIM4 的 CH1 比较/捕获事件（PWM 占空比更新、输入捕获搬数据）", "TIM4_CH2": "TIM4 的 CH2 比较/捕获事件（PWM 占空比更新、输入捕获搬数据）", "TIM4_CH3": "TIM4 的 CH3 比较/捕获事件（PWM 占空比更新、输入捕获搬数据）", "TIM4_CH4": "TIM4 的 CH4 比较/捕获事件（PWM 占空比更新、输入捕获搬数据）", "TIM4_UP": "TIM4 更新事件（计数溢出/重装，常用来周期性触发 DMA）", "DCMI": "DCMI 摄像头数据搬运（行/帧结束触发）"};
    var WD_COMMON = {"÷4（IWDG_Prescaler_4）": "重装值 4095 → 0.512 s（本档最长）", "÷8（IWDG_Prescaler_8）": "重装值 4095 → 1.024 s", "÷16（IWDG_Prescaler_16）": "重装值 4095 → 2.048 s", "÷32（IWDG_Prescaler_32）": "重装值 2047 → 2 s；4095 → 4.096 s", "÷64（IWDG_Prescaler_64）": "重装值 499 → 2 s（工程里常用这档喂狗）", "÷128（IWDG_Prescaler_128）": "重装值 249 → 1 s", "÷256（IWDG_Prescaler_256）": "重装值 4095 → 32.768 s（最长档）", "WDGTB=0（÷1）": "计数值 0x7F → 12.5 ms", "WDGTB=1（÷2）": "计数值 0x7F → 25.0 ms", "WDGTB=2（÷4）": "计数值 0x7F → 50.0 ms（常用）", "WDGTB=3（÷8）": "计数值 0x7F → 100 ms"};
    STD_TABLES.forEach(function (t) {
        var n = String(t.n);
        if (n.indexOf("时钟使能表") === 0) {
            t.h = t.h.concat(["说明（这个外设是干什么的）"]);
            var gnote = {
                GPIOH: "GPIO 端口 H（F407 144 脚封装只引出极少数引脚，本工程未用）",
                GPIOI: "GPIO 端口 I（144 脚封装未引出，本工程未用）",
                GPIOJ: "GPIO 端口 J（F407 无此端口，DFP 头文件里是通用定义）",
                GPIOK: "GPIO 端口 K（F407 无此端口，DFP 头文件里是通用定义）"
            };
            var tnote = {
                TIM1: "高级定时器 TIM1（PWM / 互补输出 / 刹车 / 输入捕获，APB2）",
                TIM8: "高级定时器 TIM8（同 TIM1，APB2）",
                TIM2: "通用定时器 TIM2（32 位计数器，APB1；定时器时钟实际 84 MHz）",
                TIM3: "通用定时器 TIM3（16 位，APB1）",
                TIM4: "通用定时器 TIM4（16 位，APB1）",
                TIM5: "通用定时器 TIM5（32 位，APB1）",
                TIM6: "基本定时器 TIM6（只有时基、无引脚，常做 DAC 触发或纯延时）",
                TIM7: "基本定时器 TIM7（同 TIM6）",
                TIM9: "通用定时器 TIM9（2 通道，APB2）",
                TIM10: "通用定时器 TIM10（1 通道，APB2）",
                TIM11: "通用定时器 TIM11（1 通道，APB2）",
                TIM12: "通用定时器 TIM12（2 通道，APB1）",
                TIM13: "通用定时器 TIM13（1 通道，APB1）",
                TIM14: "通用定时器 TIM14（1 通道，APB1）"
            };
            t.a.forEach(function (r) {
                var key = r[1];
                var text = CLK_NOTE[key] || gnote[key] || tnote[key];
                if (!text && /^GPIO[A-K]$/.test(key)) text = "GPIO 端口 " + key.slice(4) + "（用 P" + key.slice(4) + " 口任意引脚前必须开）";
                if (!text && key.indexOf("CAN") === 0) text = key + " 控制器（工业总线，收发器必须外接）";
                r.push(text || "（说明待补）");
            });
        }
        if (n.indexOf("DMA 请求映射表") === 0) {
            t.h = t.h.concat(["说明（什么时候触发 / 干什么用）"]);
            t.a.forEach(function (r) { r.push(DMA_NOTE[r[0]] || "（说明待补）"); });
        }
    });
    /* 补齐自动生成表里缺解释的单元格：SDIO 的 24 条标志从中断表同名项复制说明 */
    var flagT = null, itT = null;
    STD_TABLES.forEach(function (t) {
        if (String(t.n).indexOf("标志宏表") === 0) flagT = t;
        if (String(t.n).indexOf("中断宏表") === 0) itT = t;
    });
    if (flagT && itT) {
        var itMap = {};
        itT.a.forEach(function (r) { itMap[r[1]] = r[3]; });
        flagT.a.forEach(function (r) {
            if (r[3] || String(r[1]).indexOf("SDIO_FLAG_") !== 0) return;
            var src = itMap[String(r[1]).replace("SDIO_FLAG_", "SDIO_IT_")];
            if (src) r[3] = src;
        });
    }
    /* NVIC 优先级分组表里 Group_1 / Group_3 的“说明”列补文字 */
    var nvicNote = {
        NVIC_PriorityGroup_1: "抢占只有 2 级：中断嵌套能力弱，同抢占内的先后靠 8 级子优先级拉开，只在中断很少时用。",
        NVIC_PriorityGroup_3: "抢占 8 级、子优先级仅 2 级：不同抢占级能嵌套，但同级内的先后几乎分不出来。"
    };
    STD_TABLES.forEach(function (t) {
        if (String(t.n).indexOf("NVIC 优先级分组表") !== 0) return;
        t.a.forEach(function (r) { if (!r[3] && nvicNote[r[0]]) r[3] = nvicNote[r[0]]; });
    });
    Array.prototype.push.apply(STD_TABLES, STD_TABLES2);

    /* 看门狗配置表在 STD_TABLES2 里，push 之后才补得上“常用配置”列 */
    STD_TABLES.forEach(function (t) {
        if (String(t.n).indexOf("看门狗配置表") !== 0) return;
        t.a.forEach(function (r) {
            if (r[4]) return;
            var m = /÷(\d+)/.exec(r[1]);
            if (!m) return;
            var div = parseInt(m[1], 10);
            if (String(r[0]) === "IWDG") {
                if (div === 4) r[4] = "重装值 4095 → 0.512 s（本档最长）";
                else if (div === 8) r[4] = "重装值 4095 → 1.024 s";
                else if (div === 16) r[4] = "重装值 4095 → 2.048 s";
                else if (div === 32) r[4] = "重装值 2047 → 2 s；4095 → 4.096 s";
            } else {
                r[4] = "计数值 0x7F → " + (4096 * div * 128 / 42000000 * 1000).toFixed(1) + " ms";
            }
        });
    });
})();
