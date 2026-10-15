/* =============================================================
 *  data_rel.js —— 「相关与联动」表
 * -------------------------------------------------------------
 *  说明：有些结构体/函数是配套使用的（谁先谁后、缺哪个不行）。
 *        这里按“条目名 → [[相关条目, 为什么/怎么联动], ...]”记录；
 *        详情页会显示成“相关与联动”段，条目名可点击跳转。
 *
 *  维护：直接加/改条目即可；目标条目名要和各数据文件里的 n 字段一致。
 *        已有 rel 字段的手写卡优先，本表补缺。
 * ============================================================= */

const CARD_REL = {

/* ===================== 结构体 ↔ 函数 ===================== */
"GPIO_InitTypeDef": [["GPIO_Init","结构体填好后交给它生效"],["GPIO_PinAFConfig","复用模式（GPIO_Mode_AF）必须再调它选功能编号"],["RCC_AHB1PeriphClockCmd","用之前先开对应端口时钟"]],
"NVIC_InitTypeDef": [["NVIC_PriorityGroupConfig","全工程先定一次优先级分组"],["NVIC_Init","结构体填好后交给它生效"],["USART_ITConfig","外设端的中断源开关（以串口为例，其他外设同理）"],["GPIO_InitTypeDef","中断源外设的引脚配置（以 EXTI 按键为例）"]],
"EXTI_InitTypeDef": [["SYSCFG_EXTILineConfig","引脚→EXTI 线的映射（必须）"],["EXTI_Init","结构体填好后交给它生效"],["NVIC_Init","使能对应 EXTIx_IRQn 中断"],["EXTI_ClearITPendingBit","中断里清标志防重复进"]],
"USART_InitTypeDef": [["USART_Init","结构体填好后交给它生效"],["USART_Cmd","初始化完必须使能才能收发"],["GPIO_PinAFConfig","TX/RX 引脚复用配置"],["USART_ITConfig","要中断接收时开 RXNE 并配 NVIC_Init"]],
"I2C_InitTypeDef": [["I2C_Init","结构体填好后交给它生效"],["I2C_Cmd","初始化完使能"],["GPIO_PinAFConfig","SCL/SDA 复用（引脚还要配开漏 GPIO_OType_OD）"],["I2C_CheckEvent","传输过程用它等待事件"]],
"SPI_InitTypeDef": [["SPI_Init","结构体填好后交给它生效"],["SPI_Cmd","使能后才收发"],["SPI_I2S_SendData","发送数据"],["SPI_I2S_ReceiveData","接收数据"],["GPIO_PinAFConfig","SCK/MISO/MOSI 引脚复用"]],
"ADC_InitTypeDef": [["ADC_CommonInit","先做公共配置（预分频不能超 36MHz）"],["ADC_Init","结构体填好后交给它生效"],["ADC_RegularChannelConfig","把通道排进序列"],["ADC_Cmd","使能 ADC"],["ADC_SoftwareStartConv","启动转换"],["GPIO_InitTypeDef","引脚配成模拟模式（GPIO_Mode_AN）"]],
"ADC_CommonInitTypeDef": [["ADC_CommonInit","结构体填好后交给它生效（ADC1/2/3 共用）"],["ADC_Init","之后各 ADC 分别配置"]],
"DMA_InitTypeDef": [["DMA_Init","结构体填好后交给它生效"],["DMA_Cmd","使能数据流开始搬运"],["USART_DMACmd","只配 DMA 不够：外设端也要开 DMA 请求（以串口为例）"],["DMA_ITConfig","搬完要中断通知时开 TC 并配 NVIC_Init"]],
"TIM_TimeBaseInitTypeDef": [["RCC_APB1PeriphClockCmd","先开定时器时钟（TIM1/8~11 在 APB2）"],["TIM_TimeBaseInit","结构体填好后交给它生效"],["TIM_ITConfig","要周期中断再加"],["TIM_Cmd","启动计数"]],
"TIM_OCInitTypeDef": [["TIM_OC1Init","通道 1 传给它（通道 2/3/4 用 TIM_OC2/3/4Init）"],["TIM_SetCompare1","运行时改占空比"],["TIM_Cmd","启动输出"],["GPIO_PinAFConfig","PWM 引脚复用必须配"],["TIM_CtrlPWMOutputs","高级定时器 TIM1/8 还要开主输出（不调引脚没波形）"]],
"TIM_ICInitTypeDef": [["TIM_ICInit","结构体填好后交给它生效"],["TIM_GetCapture1","读捕获值"],["TIM_ITConfig","捕获中断（TIM_IT_CC1~CC4）"],["GPIO_PinAFConfig","捕获引脚复用必须配"]],
"RTC_AlarmTypeDef": [["RTC_SetAlarm","结构体填好后交给它生效"],["RTC_AlarmCmd","使能闹钟才会响"],["RTC_ITConfig","要中断处理开 RTC_IT_ALRA/ALRB + NVIC_Init"],["RTC_GetAlarm","回读当前闹钟设置"]],
"TIM_BDTRInitTypeDef": [["TIM_BDTRConfig","结构体填好后交给它生效（仅 TIM1/TIM8）"],["TIM_CtrlPWMOutputs","刹车/死区是高级定时器输出的一部分"],["TIM_OC1Init","互补输出通道配置"]],
"SDIO_InitTypeDef": [["SDIO_Init","结构体填好后交给它生效"],["SDIO_SendCommand","之后发 CMD0/CMD8/ACMD41 识别卡"],["SDIO_DataConfig","读写数据块前配数据传输"]],
"CAN_InitTypeDef": [["CAN_Init","结构体填好后交给它生效"],["CAN_FilterInit","接收前须配置过滤器。"],["CAN_Transmit","发送报文"],["CAN_Receive","接收报文"]],
"CAN_FilterInitTypeDef": [["CAN_FilterInit","结构体填好后交给它生效"],["CAN_Receive","命中的报文从 FIFO 读"]],
"CanTxMsg": [["CAN_Transmit","填好报文后传给它发送"],["CAN_TransmitStatus","查发送结果"],["CanRxMsg","接收方的对应容器"]],
"CanRxMsg": [["CAN_Receive","传给它读取收到的报文"],["CAN_MessagePending","先查有几帧待读"],["CanTxMsg","发送方的对应容器"]],
"DAC_InitTypeDef": [["DAC_Init","结构体填好后交给它生效"],["DAC_Cmd","使能输出"],["DAC_SetChannel1Data","写输出值"],["GPIO_InitTypeDef","引脚配成模拟模式（PA4=通道1 / PA5=通道2）"]],
"I2S_InitTypeDef": [["I2S_Init","结构体填好后交给它生效"],["I2S_Cmd","使能后出声"],["SPI_I2S_SendData","播放数据靠它 + DMA"]],
"FSMC_NORSRAMInitTypeDef": [["FSMC_NORSRAMInit","结构体填好后交给它生效"],["FSMC_NORSRAMCmd","使能存储块"],["FSMC_NORSRAMTimingInitTypeDef","读写时序挂在它里面"],["RCC_AHB3PeriphClockCmd","先开 FSMC 时钟（AHB3）"]],
"FSMC_NORSRAMTimingInitTypeDef": [["FSMC_NORSRAMInitTypeDef","时序指针填进这个总结构体的读写/写时序字段"],["FSMC_NORSRAMInit","一起传给初始化函数"]],
"FMC_NORSRAMInitTypeDef": [["FMC_NORSRAMInit","结构体填好后交给它生效（F429 的接口）"],["FMC_NORSRAMCmd","使能存储块"]],
"FMC_NORSRAMTimingInitTypeDef": [["FMC_NORSRAMInitTypeDef","时序指针填进总结构体"],["FMC_NORSRAMInit","一起传给它"]],
"FMC_SDRAMInitTypeDef": [["FMC_SDRAMInit","结构体填好后交给它生效"],["FMC_SDRAMCmdConfig","初始化命令序列用它发"],["FMC_SetRefreshCount","刷新率按 SDRAM 手册配"]],
"FMC_SDRAMCommandTypeDef": [["FMC_SDRAMCmdConfig","命令结构体传给这个函数执行"],["FMC_SDRAMInitTypeDef","属于同一条初始化链"]],
"FMC_SDRAMTimingInitTypeDef": [["FMC_SDRAMInitTypeDef","时序指针填进总结构体"],["FMC_SDRAMInit","一起传给它"]],

/* ===================== GPIO / NVIC 函数 ===================== */
"GPIO_Init": [["GPIO_InitTypeDef","先填好结构体"],["GPIO_PinAFConfig","复用模式必须配（少它引脚归 GPIO 管）"],["RCC_AHB1PeriphClockCmd","先开端口时钟"],["GPIO_SetBits","配成输出后的操作"]],
"GPIO_PinAFConfig": [["GPIO_Init","配完功能编号还要把引脚设成 GPIO_Mode_AF"],["RCC_AHB1PeriphClockCmd","先开端口时钟"]],
"GPIO_StructInit": [["GPIO_Init","填完默认值改字段后传给它"]],
"GPIO_SetBits": [["GPIO_Init","先把引脚配成输出"],["GPIO_ResetBits","拉低"],["GPIO_ToggleBits","翻转"]],
"NVIC_PriorityGroupConfig": [["NVIC_Init","分组定完后配具体中断"],["xTaskCreate","用 FreeRTOS 必须 NVIC_PriorityGroup_4（抢占式内核要求）"]],
"NVIC_Init": [["NVIC_PriorityGroupConfig","先定分组"],["USART_ITConfig","外设端中断源要开（以串口为例）"],["EXTI_ClearITPendingBit","中断里清标志（以 EXTI 为例）"]],
"RCC_AHB1PeriphClockCmd": [["GPIO_Init","GPIO 引脚配置前必须开时钟"],["RCC_AHB1PeriphResetCmd","外设异常时复位一下"]],
"RCC_APB1PeriphClockCmd": [["RCC_APB1PeriphResetCmd","外设异常时复位一下"],["RTC_ITConfig","RTC 等 APB1 外设中断（以 RTC 为例）"]],
"RCC_APB2PeriphClockCmd": [["RCC_APB2PeriphResetCmd","外设异常时复位一下"],["SYSCFG_EXTILineConfig","EXTI 映射前必须开 SYSCFG 时钟"]],

/* ===================== USART ===================== */
"USART_Init": [["USART_Cmd","配置完必须使能（少这步串口不工作）"],["USART_SendData","发送"],["USART_ReceiveData","接收"],["USART_ITConfig","中断接收再加"],["USART_DMACmd","大数据量用 DMA"]],
"USART_Cmd": [["USART_Init","先配参数"],["USART_SendData","使能后收发"]],
"USART_SendData": [["USART_GetFlagStatus","发送前等 TXE、发完等 TC"],["USART_Cmd","先使能串口"],["USART_DMACmd","大量数据改用 DMA 发送"]],
"USART_ReceiveData": [["USART_GetFlagStatus","轮询等 RXNE"],["USART_ITConfig","中断方式接收（RXNE 中断里读）"],["DMA_GetCurrDataCounter","DMA 不定长接收时算收了多少字节"]],
"USART_ITConfig": [["NVIC_Init","开完中断源还要 NVIC 使能（必须）"],["USART_GetITStatus","中断函数里判断是不是它"],["USART_ReceiveData","读走数据（RXNE 自动清）"],["USART_ClearITPendingBit","TC 等中断需要手清"]],
"USART_GetITStatus": [["USART_ITConfig","先开对应中断源"],["USART_ClearITPendingBit","处理完清挂起"]],
"USART_DMACmd": [["DMA_Init","配好对应数据流（USART1_RX= DMA2_Stream5 通道4 / TX= Stream7 通道4）"],["DMA_Cmd","使能数据流"],["USART_Init","串口本身先初始化"]],

/* ===================== I2C ===================== */
"I2C_Init": [["I2C_Cmd","初始化完使能"],["GPIO_PinAFConfig","SCL/SDA 复用（开漏）"],["I2C_CheckEvent","之后传输用它等事件"]],
"I2C_Cmd": [["I2C_Init","先配参数"],["I2C_GenerateSTART","使能后开始传输"]],
"I2C_GenerateSTART": [["I2C_CheckEvent","等 ADDR/EV5 事件"],["I2C_Send7bitAddress","紧接着发从机地址"],["I2C_GenerateSTOP","传输结束发停止"]],
"I2C_Send7bitAddress": [["I2C_CheckEvent","等模式选中事件再发数据"],["I2C_SendData","写数据"],["I2C_ReceiveData","读数据"]],
"I2C_SendData": [["I2C_CheckEvent","等 TXE/BTF 再发下一个"],["I2C_GenerateSTOP","最后一个字节后发停止"]],
"I2C_ReceiveData": [["I2C_AcknowledgeConfig","读多字节时最后一个先关 ACK"],["I2C_GenerateSTOP","读完后发停止"]],
"I2C_CheckEvent": [["I2C_GetFlagStatus","底层就是查标志组合"],["I2C_GenerateSTART","状态机第一步"]],
"I2C_SoftwareResetCmd": [["I2C_Init","复位后要重新初始化"],["I2C_GetFlagStatus","先看是不是 BUSY 卡死"]],

/* ===================== SPI ===================== */
"SPI_Init": [["SPI_Cmd","初始化完使能"],["GPIO_PinAFConfig","SCK/MISO/MOSI 复用"],["SPI_I2S_SendData","之后收发用"]],
"SPI_Cmd": [["SPI_Init","先配参数"],["SPI_I2S_SendData","收发"]],
"SPI_I2S_SendData": [["SPI_I2S_GetFlagStatus","先等 TXE（发送寄存器空）"],["SPI_I2S_ReceiveData","全双工：发的同时收"],["SPI_I2S_DMACmd","大数据量接 DMA"]],
"SPI_I2S_ReceiveData": [["SPI_I2S_GetFlagStatus","先等 RXNE（收到数据）"],["SPI_I2S_SendData","想只读一个：先随便发 0xFF"]],

/* ===================== ADC ===================== */
"ADC_Init": [["ADC_CommonInit","先公共配置（预分频）"],["GPIO_InitTypeDef","把引脚配成 GPIO_Mode_AN"],["ADC_RegularChannelConfig","把通道排进序列"],["ADC_Cmd","使能 ADC"],["ADC_SoftwareStartConv","启动转换"]],
"ADC_CommonInit": [["ADC_Init","之后配置各 ADC（共享这里的预分频）"],["RCC_APB2PeriphClockCmd","先开 ADC 时钟"]],
"ADC_Cmd": [["ADC_Init","先配置"],["ADC_SoftwareStartConv","使能后启动转换"],["ADC_GetConversionValue","读数"]],
"ADC_RegularChannelConfig": [["ADC_Init","先整体配置（扫描模式开、通道数对）"],["ADC_SoftwareStartConv","启动转换"],["ADC_GetConversionValue","读结果"]],
"ADC_SoftwareStartConv": [["ADC_Cmd","先使能 ADC"],["ADC_GetFlagStatus","等 EOC 标志"],["ADC_GetConversionValue","读数"],["ADC_DMACmd","多通道用 DMA 自动搬"]],
"ADC_DMACmd": [["DMA_Init","配数据流（外设地址 = (uint32_t)&ADC1->DR，循环模式）"],["DMA_Cmd","使能 DMA"],["ADC_SoftwareStartConv","启动后 DMA 自动搬结果"]],
"ADC_ITConfig": [["NVIC_Init","使能 ADC_IRQn"],["ADC_GetITStatus","中断里判源"],["ADC_ClearITPendingBit","清挂起"]],
"ADC_TempSensorVrefintCmd": [["ADC_RegularChannelConfig","内部通道也要排进序列"],["ADC_GetConversionValue","读温度/基准值"]],

/* ===================== DMA ===================== */
"DMA_Init": [["DMA_Cmd","配好后使能"],["DMA_ITConfig","搬完通知（TC 中断）"],["USART_DMACmd","外设端请求开关（以串口为例）"],["DMA_GetCurrDataCounter","查询进度/剩余"]],
"DMA_Cmd": [["DMA_Init","先配数据流"],["DMA_SetCurrDataCounter","重新启动前重填计数"],["DMA_GetCurrDataCounter","查进度"]],
"DMA_ITConfig": [["NVIC_Init","使能 DMAy_Streamx_IRQn"],["DMA_GetITStatus","中断判源"],["DMA_ClearITPendingBit","清挂起"]],
"DMA_GetITStatus": [["DMA_ClearITPendingBit","处理完清"],["DMA_ITConfig","先开中断"]],
"DMA_SetCurrDataCounter": [["DMA_Cmd","重填后使能重启"],["DMA_GetCurrDataCounter","读剩余"]],
"DMA_GetCurrDataCounter": [["DMA_SetCurrDataCounter","重填计数重启传输"],["USART_GetITStatus","串口 IDLE 中断里用它算收了多少字节"]],
"DMA_DoubleBufferModeConfig": [["DMA_DoubleBufferModeCmd","双缓冲开关"],["DMA_MemoryTargetConfig","指定当前缓冲"],["DMA_Init","先基础配置"]],

/* ===================== TIM ===================== */
"TIM_TimeBaseInit": [["TIM_Cmd","启动计数"],["TIM_ITConfig","定时中断"],["TIM_OC1Init","输出 PWM 用"],["TIM_ICInit","输入捕获用"]],
"TIM_Cmd": [["TIM_TimeBaseInit","先配时基"],["TIM_OC1Init","PWM 输出场景"],["TIM_ITConfig","中断场景"]],
"TIM_ITConfig": [["NVIC_Init","使能 TIMx_IRQn"],["TIM_GetITStatus","中断判源"],["TIM_ClearITPendingBit","清标志（必须）"],["TIM_Cmd","使能后开始计数"]],
"TIM_GetITStatus": [["TIM_ClearITPendingBit","处理完清（不清会反复进中断）"],["TIM_ITConfig","先开中断源"]],
"TIM_OC1Init": [["TIM_TimeBaseInit","先定频率（周期）"],["TIM_SetCompare1","运行时改占空比"],["TIM_OC1PreloadConfig","CCR 预装载（建议开）"],["TIM_Cmd","启动输出"],["GPIO_PinAFConfig","PWM 引脚复用"],["TIM_CtrlPWMOutputs","TIM1/TIM8 必须开主输出（不调引脚没波形）"]],
"TIM_SetCompare1": [["TIM_OC1Init","先配通道输出"],["TIM_Cmd","启动后生效"]],
"TIM_ICInit": [["TIM_TimeBaseInit","先定时基（分辨率）"],["TIM_GetCapture1","读捕获值"],["TIM_ITConfig","捕获中断"],["GPIO_PinAFConfig","捕获引脚复用"]],
"TIM_GetCapture1": [["TIM_ICInit","先配捕获"],["TIM_GetCounter","配合算时间差"]],
"TIM_ETRClockMode2Config": [["GPIO_PinAFConfig","ETR 引脚复用（如 PA0=TIM2_ETR）"],["TIM_TimeBaseInit","定计数周期"],["TIM_Cmd","启动计数"],["TIM_GetCounter","读脉冲数"]],
"TIM_CtrlPWMOutputs": [["TIM_OC1Init","通道配置完再开主输出"],["TIM_Cmd","启动定时器"],["TIM_BDTRConfig","刹车/死区配置（电机场景）"]],
"TIM_PWMIConfig": [["TIM_TimeBaseInit","先定时基"],["TIM_GetCapture1","读周期"],["TIM_GetCapture2","读高电平时间（算占空比）"],["GPIO_PinAFConfig","两路输入引脚复用"]],
"TIM_SelectOutputTrigger": [["TIM_OC1Init","PWM 场景的触发源"],["DAC_DMACmd","定时器触发 DAC 输出波形"],["ADC_ExternalTrigInjectedConvConfig","定时器触发 ADC 采样（见 ADC 注入组）"]],

/* ===================== EXTI ===================== */
"EXTI_Init": [["SYSCFG_EXTILineConfig","必须先做引脚→线映射"],["NVIC_Init","使能 EXTIx_IRQn"],["EXTI_ClearITPendingBit","中断里清标志"],["GPIO_InitTypeDef","引脚模式配置（输入）"]],
"EXTI_GetITStatus": [["EXTI_ClearITPendingBit","处理后清（否则反复进）"],["EXTI_Init","先配线"]],
"EXTI_ClearITPendingBit": [["EXTI_GetITStatus","判断是不是这条线"],["EXTI_Init","配线的触发条件"]],
"SYSCFG_EXTILineConfig": [["EXTI_Init","映射后配置中断条件"],["NVIC_Init","使能中断"],["RCC_APB2PeriphClockCmd","先开 SYSCFG 时钟"],["GPIO_InitTypeDef","引脚本身也要配成输入"]],

/* ===================== RTC ===================== */
"RCC_LSEConfig": [["RCC_GetFlagStatus","等 RCC_FLAG_LSERDY 就绪"],["RCC_RTCCLKConfig","选 LSE 做 RTC 时钟源"],["PWR_BackupAccessCmd","RTC 配置链里的备份域访问"]],
"RCC_RTCCLKConfig": [["RCC_RTCCLKCmd","选好源后使能"],["RTC_Init","然后初始化 RTC"],["RCC_LSEConfig","晶振先起振"]],
"RCC_RTCCLKCmd": [["RTC_Init","使能时钟后初始化 RTC"],["RTC_SetTime","设时间"]],
"PWR_BackupAccessCmd": [["RTC_WriteBackupRegister","写备份寄存器前必须开访问"],["RTC_Init","RTC 配置链前置动作"]],
"RTC_Init": [["RCC_RTCCLKConfig","时钟源配置链（LSE 起振→选源→使能）"],["RTC_SetTime","设时间"],["RTC_SetDate","设日期"],["RTC_ITConfig","周期唤醒/闹钟中断"]],
"RTC_SetTime": [["RTC_SetDate","先设时间再设日期（硬件要求顺序）"],["RTC_GetTime","读回验证"],["RTC_WaitForSynchro","改寄存器前等同步"]],
"RTC_SetDate": [["RTC_SetTime","必须先设时间"],["RTC_GetDate","读回验证"]],
"RTC_GetTime": [["RTC_GetDate","一起读出完整日期时间"],["RTC_GetSubSecond","要更细读亚秒"]],
"RTC_ITConfig": [["NVIC_Init","使能 RTC_Alarm_IRQn / RTC_WKUP_IRQn"],["RTC_GetITStatus","中断判源"],["RTC_ClearITPendingBit","清挂起（必须）"],["RTC_SetAlarm","闹钟先设好"]],
"RTC_GetITStatus": [["RTC_ClearITPendingBit","处理后清"],["RTC_ITConfig","先开中断"]],
"RTC_SetAlarm": [["RTC_AlarmCmd","使能闹钟才会触发"],["RTC_ITConfig","要中断加 RTC_IT_ALRA"],["RTC_AlarmTypeDef","闹钟时间/掩码结构体"],["RTC_GetAlarm","回读"]],
"RTC_AlarmCmd": [["RTC_SetAlarm","先设闹钟时间"]],
"RTC_WriteBackupRegister": [["RTC_ReadBackupRegister","回读校验"],["PWR_BackupAccessCmd","先开备份域访问"]],
"RTC_ReadBackupRegister": [["RTC_WriteBackupRegister","首次上电时写标志"],["RTC_Init","常见用法：标志不对就重新初始化 RTC"]],
"RTC_WakeUpClockConfig": [["RTC_SetWakeUpCounter","配计数器定周期"],["RTC_WakeUpCmd","使能唤醒"],["RTC_ITConfig","开 RTC_IT_WUT 中断"]],
"RTC_SetWakeUpCounter": [["RTC_WakeUpClockConfig","先选时钟分频"],["RTC_WakeUpCmd","使能"]],
"RTC_WakeUpCmd": [["RTC_WakeUpClockConfig","先配时钟"],["RTC_SetWakeUpCounter","再配周期"],["RTC_ITConfig","中断处理"]],

/* ===================== IWDG ===================== */
"IWDG_SetPrescaler": [["IWDG_SetReload","再设重装值（定超时时间）"],["IWDG_Enable","使能后开始倒计时"]],
"IWDG_SetReload": [["IWDG_SetPrescaler","先设预分频"],["IWDG_Enable","使能"],["IWDG_ReloadCounter","喂狗"]],
"IWDG_Enable": [["IWDG_SetPrescaler","先设参数"],["IWDG_SetReload","先设参数"],["IWDG_ReloadCounter","喂狗，须在周期任务中调用。"]],
"IWDG_ReloadCounter": [["IWDG_Enable","先使能看门狗"],["IWDG_GetFlagStatus","改参数前等标志清零"]],
"IWDG_GetFlagStatus": [["IWDG_ReloadCounter","喂狗"]],

/* ===================== FLASH ===================== */
"FLASH_Unlock": [["FLASH_EraseSector","解锁后擦除"],["FLASH_ProgramWord","解锁后编程"],["FLASH_Lock","操作完锁上"]],
"FLASH_Lock": [["FLASH_Unlock","成对使用：先解锁操作再锁定"]],
"FLASH_EraseSector": [["FLASH_Unlock","先解锁"],["FLASH_WaitForLastOperation","等擦除完成"],["FLASH_ProgramWord","擦干净了才能写"]],
"FLASH_ProgramWord": [["FLASH_Unlock","先解锁"],["FLASH_EraseSector","写入的扇区必须先擦除（F4 不能盖写）"],["FLASH_WaitForLastOperation","等编程完成"],["FLASH_Lock","操作完锁定"]],
"FLASH_WaitForLastOperation": [["FLASH_GetFlagStatus","查操作结果标志"],["FLASH_EraseSector","擦除后等它"],["FLASH_ProgramWord","编程后等它"],["FLASH_ClearFlag","清标志"]],
"FLASH_OB_RDPConfig": [["FLASH_OB_Unlock","先解锁选项字节"],["FLASH_OB_Launch","改完必须调用它生效（然后复位芯片）"],["FLASH_OB_Lock","锁回选项字节"]],

/* ===================== PWR / DAC ===================== */
"PWR_EnterSTOPMode": [["RCC_APB1PeriphClockCmd","唤醒后重配外设时钟（STOP 会停时钟）"],["EXTI_Init","先配好唤醒源（如按键 EXTI）"],["PWR_GetFlagStatus","判断唤醒原因"]],
"PWR_EnterSTANDBYMode": [["PWR_GetFlagStatus","判断是否从待机唤醒（唤醒=复位重启）"],["RTC_ITConfig","待机唤醒源：RTC 闹钟/唤醒"]],
"DAC_Init": [["DAC_Cmd","使能输出"],["GPIO_InitTypeDef","引脚配成模拟模式（PA4=通道1 / PA5=通道2）"],["DAC_SetChannel1Data","写输出值"],["DAC_SoftwareTriggerCmd","软件触发模式要触发"],["DAC_DMACmd","波形输出接 DMA 喂数据"]],
"DAC_SetChannel1Data": [["DAC_Init","先初始化"],["DAC_Cmd","先使能"],["DAC_SoftwareTriggerCmd","触发模式下触发才输出"]],
"DAC_Cmd": [["DAC_Init","先配置"],["DAC_SetChannel1Data","写值输出"]],
"DAC_DMACmd": [["DMA_Init","配数据流（外设地址 = (uint32_t)&DAC->DHR12R1）"],["DMA_Cmd","使能"],["TIM_SelectOutputTrigger","定时器触发 DAC 才是标准波形输出方案"],["DAC_Init","触发源选定时器"]],

/* ===================== FreeRTOS ===================== */
"xTaskCreate": [["vTaskStartScheduler","所有任务建好后启动调度器"],["vTaskDelay","任务里周期让出 CPU"],["uxTaskGetStackHighWaterMark","查栈够不够"],["vTaskDelete","删任务"]],
"vTaskDelay": [["xTaskCreate","任务函数里使用"],["xTaskGetTickCount","节拍时基"]],
"vTaskStartScheduler": [["xTaskCreate","先创建至少一个任务"],["xTimerCreate","软件定时器也归它调度"],["vTaskDelay","任务里延时让出"]],
"xQueueCreate": [["xQueueSend","发送消息"],["xQueueReceive","接收消息"],["xQueueSendFromISR","中断里发送用 FromISR 版"],["uxQueueMessagesWaiting","查队列积压"]],
"xQueueSend": [["xQueueCreate","先创建队列"],["xQueueReceive","接收端"],["xQueueSendFromISR","中断里必须改用这个"]],
"xQueueReceive": [["xQueueCreate","先创建队列"],["xQueueSend","发送端"]],
"xQueueSendFromISR": [["xQueueCreate","先创建队列"],["xQueueReceive","任务端接收"],["xQueueSend","任务端发送（中断里不能用它）"]],
"xSemaphoreCreateBinary": [["xSemaphoreGive","释放信号量"],["xSemaphoreTake","获取信号量"],["xSemaphoreGiveFromISR","中断里释放（如串口空闲中断通知任务）"]],
"xSemaphoreGive": [["xSemaphoreTake","配对使用"],["xSemaphoreGiveFromISR","中断里必须用 FromISR 版"]],
"xSemaphoreTake": [["xSemaphoreGive","用完必须还（互斥量场景）"],["xTaskCreate","任务里等待"]],
"xSemaphoreGiveFromISR": [["xSemaphoreTake","任务端等待配对"],["xSemaphoreCreateBinary","先创建信号量"]],
"xSemaphoreCreateMutex": [["xSemaphoreTake","上锁"],["xSemaphoreGive","开锁"]],
"xEventGroupCreate": [["xEventGroupSetBits","置事件位"],["xEventGroupWaitBits","等事件位"]],
"xEventGroupSetBits": [["xEventGroupCreate","先创建"],["xEventGroupWaitBits","等待端"]],
"xEventGroupWaitBits": [["xEventGroupSetBits","设置端"],["xEventGroupClearBits","清除位"]],
"xTimerCreate": [["xTimerStart","启动定时器才计时"],["xTimerStop","停止"],["xTimerChangePeriod","改周期"],["vTaskStartScheduler","软件定时器靠调度器运行"]],

/* ================= 新增模块（SPI / 输出比较 OC / DAC / CAN / RTC 闹钟） ================= */
"⑬ SPI 主机通信（W25QXX Flash / NRF24L01 / XPT2046 触摸）": [["🧭 SPI 主机引导（接 W25QXX Flash / NRF24L01 / 触摸屏）","配套引导：从「SPI 为什么比 I2C 快」到 DMA 收发的 8 步详解"],["W25QXX_Init","本项目板2 的 W25Q128 Flash 走 SPI1；与 NRF24L01 共用总线，不能同时使用。"],["SYS_SPI_Transfer","同一条 SPI1 上的 NRF24L01 与 Flash 靠各自 CS 片选区分，共用总线要互斥（同一时刻只拉低一根 CS）"],["SPI_InitTypeDef","先填这个结构体（方向/模式/数据宽度/分频），再交给 SYS_SPI_Init"],["③ I2C 读/写从机寄存器（接 SHT30 / BH1750 / CCS811 / BMP280 / OLED）","反方向对照：器件多、线少选 I2C；要速度选 SPI"]],
"🧭 SPI 主机引导（接 W25QXX Flash / NRF24L01 / 触摸屏）": [["⑬ SPI 主机通信（W25QXX Flash / NRF24L01 / XPT2046 触摸）","配套流程卡：完整可执行代码 + 逐行注释"],["SYS_SPI_Init","本项目封装好的入口：一次配好时钟、引脚、模式、速度"],["SYS_SPI_Transfer","收发一长串（内部按字节循环，等待 TXE/RXNE）"],["⑱ SPI Flash 断网缓存日志（掉线先存本地，上线再补传）","SPI 主机最典型的应用：W25QXX 缓存日志"]],
"⑭ 定时器输出比较 OC（单脉冲 / 精确翻转，别和 PWM 混）": [["🧭 定时器输出比较 OC 引导（PWM ≠ 输出比较，别混）","配套引导：PWM 与输出比较到底差在哪，8 步讲透"],["SYS_TIM_OcInit","本项目已封装：六种 oc_mode（翻转/强制高/强制低/PWM1/PWM2/单脉冲）"],["SYS_TIM_OcInitIT","带中断版：回调在定时器中断里跑，NVIC 优先级必须 ≥5"],["⑤ 定时器输出 PWM（调光 / 调速 / 蜂鸣器）","PWM 是输出比较的一种特例；先看 PWM 卡再回来看这张更清楚"],["⑥ 定时器输入捕获（测脉宽 / 频率 / 红外解码）","输入捕获是「量外面的波形」，输出比较是「往外产生波形」，别混"]],
"🧭 定时器输出比较 OC 引导（PWM ≠ 输出比较，别混）": [["⑭ 定时器输出比较 OC（单脉冲 / 精确翻转，别和 PWM 混）","配套流程卡：完整代码 + 六种 oc_mode 对照"],["SYS_TIM_OcSetCompare","运行中改比较值 → 改占空比 / 脉冲宽度"],["SYS_TIM_OcStop","停止输出；不用了要及时关，否则引脚一直被定时器占着"]],
"⑮ DAC 模拟电压输出（12 位真模拟量，不是 PWM 滤波）": [["🧭 DAC 模拟电压输出引导（真的输出电压，不是 PWM 滤波）","配套引导：从「DAC 不是 PWM 滤波」讲到 DMA 输出波形"],["④ ADC + DMA 循环采集（MQ-135 等模拟量，后台自动刷新）","一个是把模拟量读进来，一个是把数字量送出去，配对理解最快"],["DAC_InitTypeDef","触发源 / 波形发生器 / 缓冲 / 对齐方式都在这里配"],["DAC_SetChannel1Data","12 位右对齐写值；软件触发模式下还要调 DAC_SoftwareTriggerCmd"],["① 串口 USART + DMA + 空闲中断接收（不定长帧）","波形输出常用 DMA 循环搬运，和串口 DMA 是同一套 DMA 控制器"],["DAC_SoftwareTriggerCmd","软件触发模式下不调用它则无输出。"],["DAC_DMACmd","要用 DMA 输出波形时，除了 DMA 本身还必须在 DAC 侧打开 DMA 请求"]],
"🧭 DAC 模拟电压输出引导（真的输出电压，不是 PWM 滤波）": [["⑮ DAC 模拟电压输出（12 位真模拟量，不是 PWM 滤波）","配套流程卡：完整代码 + 万用表实测验证法"],["DAC_SoftwareTriggerCmd","软件触发模式下不调用它则无输出。"],["DAC_DMACmd","要用 DMA 输出波形时，除了 DMA 本身还必须在 DAC 侧打开 DMA 请求"]],
"⑯ CAN 总线通信（工业现场抗干扰首选）": [["🧭 CAN 总线通信引导（工业现场最抗干扰的总线）","配套引导：位时序怎么算、过滤器为什么必须配，8 步讲清"],["⑨ 数据帧组包 + CRC16-MODBUS 校验（已定案协议）","CAN 一帧只有 8 字节，本项目 20 字节协议帧要用 CAN 传必须自己分包"],["CAN_InitTypeDef","ABOM（自动离线恢复）须设为 ENABLE，否则 Bus-Off 后无法恢复。"],["CAN_FilterInit","未配置过滤器时收不到任何报文，且无报错。"],["CAN_Transmit","它返回成功只代表「已进邮箱」，真正结果要看 CAN_TransmitStatus"],["CAN_Receive","中断里必须读走 FIFO，否则 FMP0 一直有效、反复进中断"]],
"🧭 CAN 总线通信引导（工业现场最抗干扰的总线）": [["⑯ CAN 总线通信（工业现场抗干扰首选）","配套流程卡：完整代码 + 排错总表"],["CAN_Transmit","它返回成功只代表「已进邮箱」，真正结果要看 CAN_TransmitStatus"],["CAN_Receive","中断里必须读走 FIFO，否则 FMP0 一直有效、反复进中断"]],
"⑰ RTC 闹钟 + 周期唤醒（到点自己动，不用主循环轮询）": [["🧭 RTC 闹钟与周期唤醒引导（让 CPU 真正能去睡觉）","配套引导：闹钟 / 周期唤醒怎么配、EXTI17/22 为什么必须配，8 步讲透"],["🧭 RTC 电子钟引导（初始化 + 秒中断自动走时）","先能用 RTC 走时，再谈闹钟和周期唤醒"],["RTC_SetAlarm","写闹钟值；写完后须调用 RTC_AlarmCmd(ENABLE) 才生效。"],["RTC_SetWakeUpCounter","周期唤醒的计数值：周期 = (N+1) / 唤醒时钟频率"],["PWR_EnterSTOPMode","低功耗场景的灵魂：CPU 睡觉靠 RTC 唤醒（唤醒后必须重配时钟）"],["⑦ EXTI 外部中断（按键 / 传感器报警脚 / 唤醒）","先把 EXTI 弄明白再看这张：RTC 的两条中断线都挂在 EXTI 上"],["RTC_AlarmCmd","配置闹钟值后须调用它，否则闹钟不触发。"],["RTC_WakeUpCmd","同理，不使能唤醒定时器，配置全对也从不触发"],["EXTI_Init","RTC 中断要先过 EXTI：闹钟走 Line17、唤醒走 Line22，漏配则「标志置了但中断不进」"]],
"🧭 RTC 闹钟与周期唤醒引导（让 CPU 真正能去睡觉）": [["⑰ RTC 闹钟 + 周期唤醒（到点自己动，不用主循环轮询）","配套流程卡：完整代码 + 标志清除铁律"],["RTC_AlarmCmd","配置闹钟值后须调用它，否则闹钟不触发。"],["RTC_WakeUpCmd","同理，不使能唤醒定时器，配置全对也从不触发"],["EXTI_Init","RTC 中断要先过 EXTI：闹钟走 Line17、唤醒走 Line22，漏配则「标志置了但中断不进」"],["⑦ EXTI 外部中断（按键 / 传感器报警脚 / 唤醒）","EXTI_Init 那一步就是这里说的「先把 EXTI 弄明白」"]],

/* ================= 新增模块（SPI / 输出比较 OC / DAC / CAN / RTC 闹钟） ================= */

/* ================= 卡片联动（流程卡 × 引导向导 × 自研 API） ================= */
"① 串口 USART + DMA + 空闲中断接收（不定长帧）": [["🧭 串口 DMA + 空闲中断接收引导（不定长帧收包）","配套引导：从空闲中断原理到环形缓冲，7 步讲透"],["② 串口发送 + printf 重定向（调试必备）","接收和发送是一对：一个用 DMA+IDLE 收不定长包，一个用轮询/中断发"],["⑨ 数据帧组包 + CRC16-MODBUS 校验（已定案协议）","串口收到的是字节流，要变成「一帧」得靠状态机 + CRC16"],["⑩ LoRa E22-400T22S 模块收发（AT 配置 + 透传）","LoRa 模组就挂在这条串口上：E22 透传靠「字节间停顿」切帧"],["⑪ ESP8266 WiFi + MQTT 上报巴法云","ESP8266 同样走串口 AT 指令；一次收一行，别把两行粘一起"],["④ ADC + DMA 循环采集（MQ-135 等模拟量，后台自动刷新）","两者都靠 DMA 后台搬运：ADC 是循环模式，串口是正常模式 + IDLE 收尾"]],
"② 串口发送 + printf 重定向（调试必备）": [["🧭 串口 DMA + 空闲中断接收引导（不定长帧收包）","配套引导：printf 重定向与发送超时保护"],["① 串口 USART + DMA + 空闲中断接收（不定长帧）","发送是「我主动说」，接收是「听别人说」，两半合起来才是一条链路"]],
"③ I2C 读/写从机寄存器（接 SHT30 / BH1750 / CCS811 / BMP280 / OLED）": [["🧭 I2C 接传感器引导（多个传感器挂一条总线）","配套引导：扫描 → 读写寄存器 → 多任务互斥，6 步"],["⑬ SPI 主机通信（W25QXX Flash / NRF24L01 / XPT2046 触摸）","选型必看：I2C 两根线挂多器件、有应答能查错；SPI 四根线快得多但无应答"]],
"④ ADC + DMA 循环采集（MQ-135 等模拟量，后台自动刷新）": [["🧭 ADC + DMA 循环采集引导（MQ-135 等模拟量）","配套引导：规则组 / 循环模式 / DMA 半满中断，7 步"],["⑮ DAC 模拟电压输出（12 位真模拟量，不是 PWM 滤波）","一个把模拟量读进来、一个把数字量送出去；配对理解最快"],["⑤ 定时器输出 PWM（调光 / 调速 / 蜂鸣器）","要「模拟量输出」但没有 DAC 时，常用 PWM + RC 滤波凑合（精度差很多）"],["① 串口 USART + DMA + 空闲中断接收（不定长帧）","两者都靠 DMA 后台搬运；ADC 是循环模式，串口是正常模式 + IDLE"]],
"⑤ 定时器输出 PWM（调光 / 调速 / 蜂鸣器）": [["🧭 定时器 PWM 输出引导（调光 / 调速 / 蜂鸣器）","配套引导：时基与占空比怎么算，7 步"],["⑥ 定时器输入捕获（测脉宽 / 频率 / 红外解码）","一个往外发波形、一个量外面的波形；想验证 PWM 对不对，用输入捕获量自己"],["⑭ 定时器输出比较 OC（单脉冲 / 精确翻转，别和 PWM 混）","输出比较是 PWM 的「底层动作」：PWM 只是 OC 的一种特例"]],
"⑥ 定时器输入捕获（测脉宽 / 频率 / 红外解码）": [["🧭 定时器输入捕获引导（测脉宽 / 频率 / 占空比）","配套引导：边沿检测 / 溢出处理 / 频率与占空比换算，7 步"],["⑤ 定时器输出 PWM（调光 / 调速 / 蜂鸣器）","可用本路 PWM 输出接本路输入捕获做自测。"],["⑭ 定时器输出比较 OC（单脉冲 / 精确翻转，别和 PWM 混）","输入捕获量波形、输出比较产波形，方向相反"]],
"⑦ EXTI 外部中断（按键 / 传感器报警脚 / 唤醒）": [["🧭 EXTI 外部中断引导（按键 / 报警脚 / 唤醒）","配套引导：中断线映射 + 优先级 + 消抖，8 步"],["⑰ RTC 闹钟 + 周期唤醒（到点自己动，不用主循环轮询）","RTC 闹钟/唤醒不是直接进 NVIC，要先过 EXTI Line17/22 这道门"]],
"⑧ FreeRTOS 任务 + 队列 + 信号量（本项目主骨架）": [["🧭 FreeRTOS 任务框架引导（本项目采集→处理→通信→喂狗）","配套引导：任务 / 队列 / 信号量 / 优先级，10 步"],["① 串口 USART + DMA + 空闲中断接收（不定长帧）","经典组合：串口中断只负责把字节丢进队列，任务在队列上等，中断里绝不做重活"],["⑫ 独立看门狗 IWDG + 心跳喂狗（防任务卡死）","多任务下看门狗要用「心跳喂狗」：每个任务报到，全员到齐才喂"]],
"⑨ 数据帧组包 + CRC16-MODBUS 校验（已定案协议）": [["🧭 数据帧组包 + CRC16-MODBUS 校验引导（已定案协议）","配套引导：帧头 / 长度 / CRC16 / 状态机收包，6 步"],["⑯ CAN 总线通信（工业现场抗干扰首选）","CAN 一帧最多 8 字节，本项目 20 字节协议帧走 CAN 必须自己分包"],["⑩ LoRa E22-400T22S 模块收发（AT 配置 + 透传）","LoRa 只负责把字节搬到空中，帧格式（帧头/长度/CRC16）全靠应用层自己定"]],
"⑩ LoRa E22-400T22S 模块收发（AT 配置 + 透传）": [["① 串口 USART + DMA + 空闲中断接收（不定长帧）","LoRa 挂在这条串口上；必须用 DMA+IDLE 一次收一整包，否则帧边界会丢"],["⑨ 数据帧组包 + CRC16-MODBUS 校验（已定案协议）","20 字节定稿帧就是通过 LoRa 传的：帧头 0xAA55 + CRC16-MODBUS"]],
"⑪ ESP8266 WiFi + MQTT 上报巴法云": [["① 串口 USART + DMA + 空闲中断接收（不定长帧）","AT 指令是一问一答：发完命令要等 OK/ERROR，不能只看超时"],["⑫ 独立看门狗 IWDG + 心跳喂狗（防任务卡死）","联网任务是最容易卡死的一个，看门狗要能覆盖它"]],
"⑫ 独立看门狗 IWDG + 心跳喂狗（防任务卡死）": [["🧭 独立看门狗 IWDG 引导（心跳喂狗防卡死）","配套引导：超时算法与心跳喂狗，共 6 步。"],["⑧ FreeRTOS 任务 + 队列 + 信号量（本项目主骨架）","心跳喂狗模式要和任务数对齐（SYS_WDG_HEARTBEAT_COUNT）"]],
"🧭 FreeRTOS 任务框架引导（本项目采集→处理→通信→喂狗）": [["⑧ FreeRTOS 任务 + 队列 + 信号量（本项目主骨架）","配套流程卡：完整可执行代码 + 逐行注释"],["⑫ 独立看门狗 IWDG + 心跳喂狗（防任务卡死）","任务卡死是现场第一大故障，喂狗要和任务框架一起设计"],["㉖ FreeRTOS 双板闭环联调总览（检测端采集 → LoRa → 网关端 → MQTT 云端 / 断网落盘）","本项目两板的任务划分与队列设计实例"]],
"🧭 RTC 电子钟引导（初始化 + 秒中断自动走时）": [["⑰ RTC 闹钟 + 周期唤醒（到点自己动，不用主循环轮询）","走时只是基础，到点自动干活靠闹钟与周期唤醒"]],
"🧭 串口 DMA + 空闲中断接收引导（不定长帧收包）": [["① 串口 USART + DMA + 空闲中断接收（不定长帧）","配套流程卡：完整可执行代码 + 逐行注释"],["② 串口发送 + printf 重定向（调试必备）","收与发是一套：DMA+IDLE 负责收，printf 重定向负责发"],["⑨ 数据帧组包 + CRC16-MODBUS 校验（已定案协议）","收到的字节流要靠组帧状态机切成一帧一帧"],["⑲ LoRa E22-400T22S 点对点无线通信（板1 采集 → 板2 网关）","串口后面挂 LoRa 模块的完整做法"],["⑳ ESP8266 AT 联网 + MQTT 上报云端（无线上行主通道）","串口后面挂 ESP8266 + MQTT 的完整做法"],["㉑ Modbus RTU 从机 + RS485 半双工（把板子挂进工业现场有线链路）","串口后面挂 RS485 收发器的完整做法"]],
"🧭 I2C 接传感器引导（多个传感器挂一条总线）": [["③ I2C 读/写从机寄存器（接 SHT30 / BH1750 / CCS811 / BMP280 / OLED）","配套流程卡：完整可执行代码 + 逐行注释"],["⑬ SPI 主机通信（W25QXX Flash / NRF24L01 / XPT2046 触摸）","I2C 与 SPI 是两条最常用的板级总线，选型必看"],["㉓ SSD1306 OLED 显示（I2C）","I2C 显示器件的完整用法"],["㉔ DHT11 单总线温湿度（检测端的第一路数据源）","单总线传感器（不是 I2C，但常一起挂）"],["㉕ 一条 I2C 总线挂多个器件 + AT24C02 掉电保存参数","一条总线挂多个从机的工程做法"]],
"🧭 ADC + DMA 循环采集引导（MQ-135 等模拟量）": [["④ ADC + DMA 循环采集（MQ-135 等模拟量，后台自动刷新）","配套流程卡：完整可执行代码 + 逐行注释"],["⑮ DAC 模拟电压输出（12 位真模拟量，不是 PWM 滤波）","读模拟量与写模拟量配对理解最快"],["㉗ 数字滤波 + 卡尔曼 + PID 闭环（把抖动的读数变成可信的控制量）","采到的模拟量怎么滤波 / 怎么闭环"]],
"🧭 板载 FLASH 扇区配置与参数保存引导（ZE 512KB / ZG 1MB）": [["⑬ SPI 主机通信（W25QXX Flash / NRF24L01 / XPT2046 触摸）","板载 Flash 就是挂在 SPI 上的（板2 走 SPI1）"],["W25QXX_Init","本项目板2 的 Flash 初始化入口"],["⑱ SPI Flash 断网缓存日志（掉线先存本地，上线再补传）","同一个 Flash 的另一半用途：断网缓存日志"],["㉕ 一条 I2C 总线挂多个器件 + AT24C02 掉电保存参数","参数也可以放外部 EEPROM，两者取舍看这张"]],
"🧭 独立看门狗 IWDG 引导（心跳喂狗防卡死）": [["⑫ 独立看门狗 IWDG + 心跳喂狗（防任务卡死）","配套流程卡：完整可执行代码 + 逐行注释"],["⑧ FreeRTOS 任务 + 队列 + 信号量（本项目主骨架）","心跳喂狗必须和多任务框架一起看"]],
"🧭 窗口看门狗 WWDG 引导（喂早也复位，比 IWDG 更严）": [["⑫ 独立看门狗 IWDG + 心跳喂狗（防任务卡死）","WWDG 与 IWDG 的区别先看这张卡：一个窗口期，一个单纯超时"]],
"🧭 定时器 PWM 输出引导（调光 / 调速 / 蜂鸣器）": [["⑤ 定时器输出 PWM（调光 / 调速 / 蜂鸣器）","配套流程卡：完整可执行代码 + 逐行注释"],["⑭ 定时器输出比较 OC（单脉冲 / 精确翻转，别和 PWM 混）","PWM 只是输出比较的一种特例，想真正吃透看这张"],["⑥ 定时器输入捕获（测脉宽 / 频率 / 红外解码）","用输入捕获测量本路 PWM 输出是否正常。"],["㉗ 数字滤波 + 卡尔曼 + PID 闭环（把抖动的读数变成可信的控制量）","PWM 只是执行器，控制律要另写"]],
"🧭 EXTI 外部中断引导（按键 / 报警脚 / 唤醒）": [["⑦ EXTI 外部中断（按键 / 传感器报警脚 / 唤醒）","配套流程卡：完整可执行代码 + 逐行注释"],["⑰ RTC 闹钟 + 周期唤醒（到点自己动，不用主循环轮询）","RTC 的闹钟与唤醒同样要过 EXTI 这道门"]],
"🧭 数据帧组包 + CRC16-MODBUS 校验引导（已定案协议）": [["⑨ 数据帧组包 + CRC16-MODBUS 校验（已定案协议）","配套流程卡：完整可执行代码 + 逐行注释"],["⑩ LoRa E22-400T22S 模块收发（AT 配置 + 透传）","这套帧格式的实际传输通道就是 LoRa"],["⑯ CAN 总线通信（工业现场抗干扰首选）","走高可靠总线时协议帧怎么分包看这张"],["㉒ sys_frame 自定义 20 字节数据帧协议（AA 55 帧头 + CRC16-MODBUS + 55 AA 帧尾）","本项目已定案的 20 字节帧格式"]],
"🧭 定时器输入捕获引导（测脉宽 / 频率 / 占空比）": [["⑥ 定时器输入捕获（测脉宽 / 频率 / 红外解码）","配套流程卡：完整可执行代码 + 逐行注释"],["⑤ 定时器输出 PWM（调光 / 调速 / 蜂鸣器）","先用 PWM 产生已知波形，再喂给输入捕获验证"]],
"⑱ SPI Flash 断网缓存日志（掉线先存本地，上线再补传）": [["🧭 ⑱ SPI Flash 断网缓存日志引导（先存本地，上线再补传）","配套引导：从认芯片到断电恢复自检，一步步做"],["⑬ SPI 主机通信（W25QXX Flash / NRF24L01 / XPT2046 触摸）","底层就是 SPI1 主机：先把 SPI 收发跑通，再回来看这层缓存逻辑"],["🧭 板载 FLASH 扇区配置与参数保存引导（ZE 512KB / ZG 1MB）","同一个 Flash、两套用法：那张存参数，这张存掉线期间的数据"],["W25QXX_Init","入口：SPI1 时钟/引脚/片选一次配好，先跑通它再谈日志"],["SPI_I2S_ReceiveData","读 JEDEC ID 时先发命令字节再收 3 字节。"],["SPI_I2S_SendData","读写都是先发命令/地址再收发，发送这一段靠它"],["SPI_Cmd","先把 SPI 外设使能起来，才谈得上扫日志区、恢复写指针"],["⑩ LoRa E22-400T22S 模块收发（AT 配置 + 透传）","断网时把数据写进这里，联网后走 LoRa 补传"],["⑳ ESP8266 AT 联网 + MQTT 上报云端（无线上行主通道）","MQTT 判活失败才落本地；两边的「算不算掉线」口径必须一致"],["㉒ sys_frame 自定义 20 字节数据帧协议（AA 55 帧头 + CRC16-MODBUS + 55 AA 帧尾）","往上传时按自定义帧打包，别把裸结构体直接丢上总线"]],
"🧭 ⑱ SPI Flash 断网缓存日志引导（先存本地，上线再补传）": [["⑱ SPI Flash 断网缓存日志（掉线先存本地，上线再补传）","配套流程卡：完整代码 + 三个真实教训（ID / 原子提交 / 擦除单位）"],["🧭 板载 FLASH 扇区配置与参数保存引导（ZE 512KB / ZG 1MB）","板载 Flash 的另一个分区用法：参数保存与掉电默认值"],["🧭 SPI 主机引导（接 W25QXX Flash / NRF24L01 / 触摸屏）","底层 SPI 主机怎么配，先看这个"],["SPI_Init","更底层：SPI1 主机模式与软件片选怎么配"],["W25QXX_Init","更底层：SPI1 + 片选初始化"],["⑱ SPI Flash 断网缓存日志（掉线先存本地，上线再补传）","配套流程卡：完整代码 + 三个真实教训"]],
"⑲ LoRa E22-400T22S 点对点无线通信（板1 采集 → 板2 网关）": [["🧭 ⑲ LoRa E22-400T22S 点对点链路引导（从接线到两板互通）","配套引导：三根模式脚 + AUX 握手，把「第一条指令总是丢」讲透"],["① 串口 USART + DMA + 空闲中断接收（不定长帧）","LoRa 就挂在这条串口上，收发不定长数据靠 DMA + 空闲中断"],["㉒ sys_frame 自定义 20 字节数据帧协议（AA 55 帧头 + CRC16-MODBUS + 55 AA 帧尾）","透明传输不划帧边界，必须配自定义帧才能切出一包一包"],["⑱ SPI Flash 断网缓存日志（掉线先存本地，上线再补传）","链路断了就把数据落到本地缓存，等恢复再补传"],["㉑ Modbus RTU 从机 + RS485 半双工（把板子挂进工业现场有线链路）","Modbus 走有线、LoRa 走无线，是现场两种典型链路"],["㉖ FreeRTOS 双板闭环联调总览（检测端采集 → LoRa → 网关端 → MQTT 云端 / 断网落盘）","两块板之间的无线链路就是它，联调总览里看全貌"],["⑩ LoRa E22-400T22S 模块收发（AT 配置 + 透传）","同一个模块的另一张卡：AT 配置命令逐条讲"]],
"🧭 ⑲ LoRa E22-400T22S 点对点链路引导（从接线到两板互通）": [["⑲ LoRa E22-400T22S 点对点无线通信（板1 采集 → 板2 网关）","配套流程卡：模式表 + 引脚分配（两板不同）+ 排错表"],["① 串口 USART + DMA + 空闲中断接收（不定长帧）","底层串口怎么收一整包，先看这张"],["㉒ sys_frame 自定义 20 字节数据帧协议（AA 55 帧头 + CRC16-MODBUS + 55 AA 帧尾）","上层协议帧，两者是一套"],["⑱ SPI Flash 断网缓存日志（掉线先存本地，上线再补传）","断链后的数据落到哪里"],["⑲ LoRa E22-400T22S 点对点无线通信（板1 采集 → 板2 网关）","配套流程卡：模式表 + 引脚分配 + 排错表"]],
"⑳ ESP8266 AT 联网 + MQTT 上报云端（无线上行主通道）": [["🧭 ⑳ ESP8266 AT 联网 + MQTT 上报云端引导（从一条 AT 到一条 MQTT 报文）","配套引导：AT 联网四步 → MQTT 连接 → 订阅 → 上报，含半开连接排查"],["① 串口 USART + DMA + 空闲中断接收（不定长帧）","ESP8266 也是串口器件，AT 命令的收发都走这条链路"],["㉒ sys_frame 自定义 20 字节数据帧协议（AA 55 帧头 + CRC16-MODBUS + 55 AA 帧尾）","MQTT 之外的自定义帧；上报内容怎么打包看这张"],["⑱ SPI Flash 断网缓存日志（掉线先存本地，上线再补传）","只发 PINGREQ 不查 PINGRESP 会导致「假在线」，判活失败必须转本地缓存"],["⑩ LoRa E22-400T22S 模块收发（AT 配置 + 透传）","MQTT 与 LoRa 是两条互为备份的上行通道"],["㉖ FreeRTOS 双板闭环联调总览（检测端采集 → LoRa → 网关端 → MQTT 云端 / 断网落盘）","双板联调时两条通道怎么切，看总览卡"]],
"🧭 ⑳ ESP8266 AT 联网 + MQTT 上报云端引导（从一条 AT 到一条 MQTT 报文）": [["⑳ ESP8266 AT 联网 + MQTT 上报云端（无线上行主通道）","配套流程卡：AT 命令全集 + 剩余长度编码 + 返回码分级"],["⑱ SPI Flash 断网缓存日志（掉线先存本地，上线再补传）","掉线判据失效的直接后果：数据静默丢失"],["① 串口 USART + DMA + 空闲中断接收（不定长帧）","底层串口收包机制"],["㉖ FreeRTOS 双板闭环联调总览（检测端采集 → LoRa → 网关端 → MQTT 云端 / 断网落盘）","两条上行通道怎么容错"],["⑳ ESP8266 AT 联网 + MQTT 上报云端（无线上行主通道）","配套流程卡：AT 命令全集 + 返回码分级"]],
"㉑ Modbus RTU 从机 + RS485 半双工（把板子挂进工业现场有线链路）": [["🧭 ㉑ Modbus RTU 从机 + RS485 半双工引导（从接线到能被组态软件读）","配套引导：DE 脚方向切换为什么必须等 TC，9 步做通从机"],["⑨ 数据帧组包 + CRC16-MODBUS 校验（已定案协议）","CRC16 与帧边界：这张是自定义协议，那张是标准 Modbus"],["① 串口 USART + DMA + 空闲中断接收（不定长帧）","RS485 收发不定长帧同样靠 DMA + 空闲中断"],["⑲ LoRa E22-400T22S 点对点无线通信（板1 采集 → 板2 网关）","有线的 Modbus 与无线的 LoRa，现场两种链路对照看"],["㉖ FreeRTOS 双板闭环联调总览（检测端采集 → LoRa → 网关端 → MQTT 云端 / 断网落盘）","从机被主站轮询的时序，在双板联调里最好观察"]],
"🧭 ㉑ Modbus RTU 从机 + RS485 半双工引导（从接线到能被组态软件读）": [["㉑ Modbus RTU 从机 + RS485 半双工（把板子挂进工业现场有线链路）","配套流程卡：功能码表 + 异常码 + 3.5 字符静默"],["⑨ 数据帧组包 + CRC16-MODBUS 校验（已定案协议）","另一种帧格式：本项目自定义 20 字节帧"],["① 串口 USART + DMA + 空闲中断接收（不定长帧）","底层串口机制"],["㉑ Modbus RTU 从机 + RS485 半双工（把板子挂进工业现场有线链路）","配套流程卡：功能码表 + 异常码 + 静默间隔"]],
"㉒ sys_frame 自定义 20 字节数据帧协议（AA 55 帧头 + CRC16-MODBUS + 55 AA 帧尾）": [["🧭 ㉒ sys_frame 自定义帧协议引导（从「串口只能收字节」到一条能自检的帧）","配套引导：从帧头到 CRC 逐字节拆，含接收状态机与超时"],["🧭 数据帧组包 + CRC16-MODBUS 校验引导（已定案协议）","协议前传：那张讲为什么需要组帧，这张讲本项目已定案的帧格式"],["⑨ 数据帧组包 + CRC16-MODBUS 校验（已定案协议）","同一套 CRC16-MODBUS，一个讲原理一个讲工程实现"],["⑲ LoRa E22-400T22S 点对点无线通信（板1 采集 → 板2 网关）","这套帧的实际无线通道"],["⑳ ESP8266 AT 联网 + MQTT 上报云端（无线上行主通道）","MQTT 上报前也可先组帧再当载荷发"],["㉑ Modbus RTU 从机 + RS485 半双工（把板子挂进工业现场有线链路）","工业总线上的标准帧格式，与自定义帧对照"],["⑱ SPI Flash 断网缓存日志（掉线先存本地，上线再补传）","缓存里的记录往外传时按帧打包"],["㉖ FreeRTOS 双板闭环联调总览（检测端采集 → LoRa → 网关端 → MQTT 云端 / 断网落盘）","两板之间传的就是这种帧"]],
"🧭 ㉒ sys_frame 自定义帧协议引导（从「串口只能收字节」到一条能自检的帧）": [["㉒ sys_frame 自定义 20 字节数据帧协议（AA 55 帧头 + CRC16-MODBUS + 55 AA 帧尾）","配套流程卡：帧结构 + 校验范围 + 返回码"],["🧭 数据帧组包 + CRC16-MODBUS 校验引导（已定案协议）","先理解为什么要组帧"],["⑲ LoRa E22-400T22S 点对点无线通信（板1 采集 → 板2 网关）","帧的无线通道"],["㉑ Modbus RTU 从机 + RS485 半双工（把板子挂进工业现场有线链路）","标准 Modbus 帧格式"],["㉒ sys_frame 自定义 20 字节数据帧协议（AA 55 帧头 + CRC16-MODBUS + 55 AA 帧尾）","配套流程卡：帧结构 + 校验范围 + 返回码"]],
"㉓ SSD1306 OLED 显示（I2C）": [["🧭 ㉓ OLED 显示引导（从一堆散落函数到一块能跑起来的屏）","配套引导：控制字节 + 页寻址 + 整帧加锁，把花屏讲清"],["③ I2C 读/写从机寄存器（接 SHT30 / BH1750 / CCS811 / BMP280 / OLED）","OLED 就是挂在这条 I2C 上的一个从机，读写机制完全一样"],["🧭 I2C 接传感器引导（多个传感器挂一条总线）","同一条总线上还挂着 24C02 / MPU6050 / 各传感器"],["⑧ FreeRTOS 任务 + 队列 + 信号量（本项目主骨架）","刷屏放在任务里做，别在主循环里阻塞"],["㉕ 一条 I2C 总线挂多个器件 + AT24C02 掉电保存参数","同总线多从机的互斥与地址分配，这张讲得最细"],["I2C_GenerateSTART","扫地址的本质：挨个发 START + 从机地址，看有没有 ACK"],["I2C_SendData","命令与显存数据最终都从这里写下去"],["I2C_ReceiveData","回读显存做自检（可选）"]],
"🧭 ㉓ OLED 显示引导（从一堆散落函数到一块能跑起来的屏）": [["㉓ SSD1306 OLED 显示（I2C）","配套流程卡：初始化序列 + 显存布局 + 撕裂根因"],["③ I2C 读/写从机寄存器（接 SHT30 / BH1750 / CCS811 / BMP280 / OLED）","I2C 读写从机的通用套路"],["㉕ 一条 I2C 总线挂多个器件 + AT24C02 掉电保存参数","多从机共存时的总线互斥"],["㉓ SSD1306 OLED 显示（I2C）","配套流程卡：初始化序列 + 显存布局 + 撕裂根因"]],
"㉔ DHT11 单总线温湿度（检测端的第一路数据源）": [["🧭 ㉔ DHT11 单总线温湿度引导（从一根线到一次可信的读数）","配套引导：单总线时序 + 微秒级延时 + 校验和，5 步做成"],["⑧ FreeRTOS 任务 + 队列 + 信号量（本项目主骨架）","采集放独立任务，采完通过队列交给处理任务"],["⑱ SPI Flash 断网缓存日志（掉线先存本地，上线再补传）","温湿度超限或断网时，写进本地缓存等补传"],["⑲ LoRa E22-400T22S 点对点无线通信（板1 采集 → 板2 网关）","采到的数据最终走 LoRa 传出去"],["㉖ FreeRTOS 双板闭环联调总览（检测端采集 → LoRa → 网关端 → MQTT 云端 / 断网落盘）","采集端到通信端的完整数据流，看总览卡"]],
"🧭 ㉔ DHT11 单总线温湿度引导（从一根线到一次可信的读数）": [["㉔ DHT11 单总线温湿度（检测端的第一路数据源）","配套流程卡：40 位数据格式 + 时序容忍度 + 重试策略"],["⑱ SPI Flash 断网缓存日志（掉线先存本地，上线再补传）","数据的本地归宿"],["⑲ LoRa E22-400T22S 点对点无线通信（板1 采集 → 板2 网关）","数据的上行通道"],["㉔ DHT11 单总线温湿度（检测端的第一路数据源）","配套流程卡：40 位数据格式 + 时序容忍度"]],
"㉕ 一条 I2C 总线挂多个器件 + AT24C02 掉电保存参数": [["🧭 ㉕ I2C 多器件与参数保存引导（一条总线接多个器件，含参数保存）","配套引导：一条总线挂多个从机，地址分配 + 互斥 + 分页写"],["③ I2C 读/写从机寄存器（接 SHT30 / BH1750 / CCS811 / BMP280 / OLED）","单从机读写的通用做法，先看那张再看这张"],["🧭 I2C 接传感器引导（多个传感器挂一条总线）","I2C 外设本身的配置（时钟、开漏、上拉）"],["㉓ SSD1306 OLED 显示（I2C）","OLED 也是这条总线上的从机，多任务下最考验互斥"],["⑱ SPI Flash 断网缓存日志（掉线先存本地，上线再补传）","EEPROM 存参数、Flash 存缓存，两者分工别混"],["🧭 板载 FLASH 扇区配置与参数保存引导（ZE 512KB / ZG 1MB）","参数保存的另一条路：片内 Flash 扇区 A/B 轮换"],["I2C_GenerateSTART","总线接好第一件事：发 START + 从机地址，把应答的地址摸出来"],["I2C_SendData","写从机寄存器 / 多字节数据"],["I2C_ReceiveData","读从机寄存器 / 多字节数据"],["I2C_SoftwareResetCmd","总线被从机拉死时的救场手段之一（另一个是手动发 9 个时钟）"]],
"🧭 ㉕ I2C 多器件与参数保存引导（一条总线接多个器件，含参数保存）": [["㉕ 一条 I2C 总线挂多个器件 + AT24C02 掉电保存参数","配套流程卡：器件地址总表 + 分页写 + 互斥量用法"],["③ I2C 读/写从机寄存器（接 SHT30 / BH1750 / CCS811 / BMP280 / OLED）","I2C 基础读写"],["㉓ SSD1306 OLED 显示（I2C）","同总线的显示器件"],["🧭 板载 FLASH 扇区配置与参数保存引导（ZE 512KB / ZG 1MB）","参数到底存 EEPROM 还是片内 Flash"],["㉕ 一条 I2C 总线挂多个器件 + AT24C02 掉电保存参数","配套流程卡：器件地址总表 + 分页写"]],
"㉖ FreeRTOS 双板闭环联调总览（检测端采集 → LoRa → 网关端 → MQTT 云端 / 断网落盘）": [["🧭 ㉖ FreeRTOS 双板闭环联调向导（从「编译过了」到「两板真的通」）","配套引导：从两块单板到一条数据链路，任务/队列/超时怎么排"],["⑧ FreeRTOS 任务 + 队列 + 信号量（本项目主骨架）","任务骨架的通用写法先看这张"],["🧭 FreeRTOS 任务框架引导（本项目采集→处理→通信→喂狗）","FreeRTOS 基础：任务、队列、信号量、互斥量"],["㉔ DHT11 单总线温湿度（检测端的第一路数据源）","检测端采集任务"],["⑱ SPI Flash 断网缓存日志（掉线先存本地，上线再补传）","断网缓存的落盘与补传"],["⑲ LoRa E22-400T22S 点对点无线通信（板1 采集 → 板2 网关）","无线链路的收发"],["⑳ ESP8266 AT 联网 + MQTT 上报云端（无线上行主通道）","无线上行与云端上报"],["㉑ Modbus RTU 从机 + RS485 半双工（把板子挂进工业现场有线链路）","有线从机侧"],["㉒ sys_frame 自定义 20 字节数据帧协议（AA 55 帧头 + CRC16-MODBUS + 55 AA 帧尾）","两板之间的数据帧"]],
"🧭 ㉖ FreeRTOS 双板闭环联调向导（从「编译过了」到「两板真的通」）": [["㉖ FreeRTOS 双板闭环联调总览（检测端采集 → LoRa → 网关端 → MQTT 云端 / 断网落盘）","配套流程卡：两板任务表 + 队列尺寸 + 超时预算"],["🧭 FreeRTOS 任务框架引导（本项目采集→处理→通信→喂狗）","先把 FreeRTOS 基础过一遍"],["🧭 独立看门狗 IWDG 引导（心跳喂狗防卡死）","联调时最容易忽略的就是喂狗"],["㉖ FreeRTOS 双板闭环联调总览（检测端采集 → LoRa → 网关端 → MQTT 云端 / 断网落盘）","配套流程卡：两板任务表 + 队列尺寸"]],
"㉗ 数字滤波 + 卡尔曼 + PID 闭环（把抖动的读数变成可信的控制量）": [["🧭 ㉗ 数字滤波 + 卡尔曼 + PID 闭环引导（从抖动的读数到能用的控制量）","配套引导：从滑动平均到 PID，三种算法各自解决什么问题"],["④ ADC + DMA 循环采集（MQ-135 等模拟量，后台自动刷新）","算法处理的对象就是 ADC 采回来的模拟量"],["⑤ 定时器输出 PWM（调光 / 调速 / 蜂鸣器）","PID 的输出通常送给 PWM 调占空比"],["⑧ FreeRTOS 任务 + 队列 + 信号量（本项目主骨架）","算法放在独立任务里跑固定周期，节奏才稳"],["🧭 ADC + DMA 循环采集引导（MQ-135 等模拟量）","ADC 采集本身怎么配"],["🧭 定时器 PWM 输出引导（调光 / 调速 / 蜂鸣器）","执行器侧怎么接"]],
"🧭 ㉗ 数字滤波 + 卡尔曼 + PID 闭环引导（从抖动的读数到能用的控制量）": [["㉗ 数字滤波 + 卡尔曼 + PID 闭环（把抖动的读数变成可信的控制量）","配套流程卡：三种算法对照 + 采样周期决定成败"],["🧭 ADC + DMA 循环采集引导（MQ-135 等模拟量）","输入从哪来"],["🧭 定时器 PWM 输出引导（调光 / 调速 / 蜂鸣器）","输出到哪去"],["㉗ 数字滤波 + 卡尔曼 + PID 闭环（把抖动的读数变成可信的控制量）","配套流程卡：三种算法对照"]],


/* ===================== 新增流程卡 / 向导（㉘~㊳ 与两个新向导）===================== */
"㉘ TFT 并口彩屏 ILI9481（FSMC NE4 + A6 当 RS，320×480）": [["🧭 ㉘ TFT 并口彩屏 ILI9481 引导（FSMC 并口，从黑屏到出字）","同主题向导：一步一步从黑屏做到出字"],["㊱ 外扩 SRAM（FSMC NE3，1 MB，当普通内存用）","同一套 FSMC 控制器，先弄明白 NE3 再上 NE4 屏"],["㉙ XPT2046 电阻触摸（软件时序 + 校准参数保存）","彩屏的配套输入层，屏跑通后接着做触摸"],["FSMC_NORSRAMInitTypeDef","并口屏就是挂在 FSMC 上的 NOR/SRAM 设备，靠它配时序"],["FSMC_NORSRAMInit","时序结构体填好后交给它生效"],["RCC_AHB3PeriphClockCmd","FSMC 走 AHB3 总线，用之前必须开时钟"],["GPIO_PinAFConfig","FSMC 的数据/控制/地址脚全部要配成复用功能"],["GPIO_PinAFConfig","PG12/PF12/PD5/PD4/PC0..PC15 等脚都要选到 FSMC_AF"]],
"㉙ XPT2046 电阻触摸（软件时序 + 校准参数保存）": [["🧭 ㉘ TFT 并口彩屏 ILI9481 引导（FSMC 并口，从黑屏到出字）","触摸层要贴在屏上，屏没跑通就没法验证触摸"],["㉘ TFT 并口彩屏 ILI9481（FSMC NE4 + A6 当 RS，320×480）","触摸坐标要按屏的分辨率换算"],["㉓ SSD1306 OLED 显示（I2C）","另一种显示+输入的入门组合（无触摸），可对照选型"],["SPI_InitTypeDef","XPT2046 走软件 SPI 时序，用 SPL 的 SPI 结构体配"],["SPI_Init","SPI 主机参数填好后交给它生效"],["SPI_Cmd","配完使能 SPI 外设"],["SPI_I2S_SendData","读触摸芯片靠发命令字节再收 12 位回数"],["SPI_I2S_ReceiveData","收回来的 X/Y 原始值用它取"],["EXTI_InitTypeDef","PEN 引脚（PB1）用中断方式感知按下"],["EXTI_Init","外部中断参数填好后交给它生效"]],
"㉚ DS18B20 单总线温度（-55~+125℃，一根线挂多颗）": [["🧭 ㉙ DS18B20 单总线温度引导（一根线读温度，从复位到算出摄氏度）","同主题向导：从复位到算出摄氏度"],["㉔ DHT11 单总线温湿度（检测端的第一路数据源）","同样是单总线，时序一个用 µs、一个用 ms，可对照"],["㉝ MPU6050 六轴姿态（两板 API 不同，附统一封装）","检测端的另一路数据源，一起接到采集任务里"],["GPIO_InitTypeDef","DQ 脚（PG9）要在输出/输入之间来回切，靠它反复配"],["GPIO_Init","GPIO 参数填好后交给它生效"],["GPIO_ReadInputDataBit","读 DQ 电平判断存在脉冲与每一位"],["GPIO_WriteBit","拉低/释放 DQ 发起复位与写时隙"],["TIM_SetCounter","用微秒延时卡时序时，计数器加载值靠它"],["TIM_GetCounter","读计数器算微秒延时"]],
"㉛ HC-SR04 超声波测距（TRIG 触发 + ECHO 回波宽度换算）": [["㉜ VL53L0X 激光测距（I2C，30~2000 mm，ToF）","另一种测距方案，可对比精度与抗干扰"],["⑥ 定时器输入捕获（测脉宽 / 频率 / 红外解码）","ECHO 高电平宽度就是用输入捕获量出来的"],["🧭 定时器输入捕获引导（测脉宽 / 频率 / 占空比）","输入捕获的完整搭建过程"],["GPIO_InitTypeDef","TRIG/ECHO 两个脚都要配（一个输出一个输入）"],["GPIO_Init","GPIO 参数填好后交给它生效"],["TIM_ICInitTypeDef","捕获 ECHO 上升沿/下降沿靠它配"],["TIM_ICInit","输入捕获参数填好后交给它生效"],["TIM_ITConfig","捕获事件要开中断才能算时间差"],["TIM_GetCapture1","读两次捕获值相减就是回波宽度"]],
"㉜ VL53L0X 激光测距（I2C，30~2000 mm，ToF）": [["㉛ HC-SR04 超声波测距（TRIG 触发 + ECHO 回波宽度换算）","另一种测距方案，ToF 比超声波抗环境噪声"],["③ I2C 读/写从机寄存器（接 SHT30 / BH1750 / CCS811 / BMP280 / OLED）","VL53L0X 的寄存器读写就靠这一层"],["㉕ 一条 I2C 总线挂多个器件 + AT24C02 掉电保存参数","同一总线上的多器件地址分配与互斥保护"],["I2C_InitTypeDef","I2C1 的参数（速率/地址/应答）靠它配"],["I2C_Init","I2C 参数填好后交给它生效"],["I2C_GenerateSTART","VL53L0X 的整段初始化要按它自己的寄存器序列收发"],["I2C_Send7bitAddress","先寻址 0x29 再读写寄存器"],["I2C_SendData","把寄存器地址/命令字发出去"],["I2C_ReceiveData","取回寄存器内容"],["I2C_CheckEvent","每收完一个字节要等 EV 事件，否则会读错"]],
"㉝ MPU6050 六轴姿态（两板 API 不同，附统一封装）": [["㉔ DHT11 单总线温湿度（检测端的第一路数据源）","检测端另一路数据源，一起进采集任务"],["㉚ DS18B20 单总线温度（-55~+125℃，一根线挂多颗）","同样是传感器接入的样板卡"],["③ I2C 读/写从机寄存器（接 SHT30 / BH1750 / CCS811 / BMP280 / OLED）","MPU6050 的寄存器读写就用这一层"],["㉗ 数字滤波 + 卡尔曼 + PID 闭环（把抖动的读数变成可信的控制量）","原始加速度/角速度抖动大，接这个做姿态融合"],["I2C_InitTypeDef","I2C 参数靠它配"],["I2C_Init","I2C 参数填好后交给它生效"],["I2C_ReadRegister","读 WhoAmI(0x75) 与数据寄存器 0x3B 起 14 字节"],["I2C_SendData","写 PWR_MGMT_1(0x6B) 唤醒、写量程寄存器，字节全靠它发"]],
"㉞ 直流电机 TB6612 + 编码器测速（速度环闭环）": [["⑤ 定时器输出 PWM（调光 / 调速 / 蜂鸣器）","TB6612 的调速就靠 PWM 占空比"],["㉟ ULN2003 步进电机（28BYJ-48 类，8 拍 2038 步一圈）","另一种电机方案，可对比驱动方式"],["㉗ 数字滤波 + 卡尔曼 + PID 闭环（把抖动的读数变成可信的控制量）","速度环 PID 直接接在这张卡的测速结果上"],["TIM_TimeBaseInitTypeDef","PWM 与编码器接口都要先配时基"],["TIM_TimeBaseInit","时基参数填好后交给它生效"],["TIM_OCInitTypeDef","输出比较通道配成 PWM 模式驱动调速脚"],["TIM_OC2Init","PWM 通道参数填好后交给它生效"],["TIM_CtrlPWMOutputs","高级定时器要调它，主输出才真的出波形"],["TIM_EncoderInterfaceConfig","TIM2/TIM3 配成编码器接口读正交脉冲"],["TIM_GetCounter","读编码器计数，两次相减就是转速"]],
"㉟ ULN2003 步进电机（28BYJ-48 类，8 拍 2038 步一圈）": [["㉞ 直流电机 TB6612 + 编码器测速（速度环闭环）","另一种电机方案：直流靠 PWM 调速，步进靠拍序定位"],["⑤ 定时器输出 PWM（调光 / 调速 / 蜂鸣器）","步进也可以用 PWM 定时节拍，比纯延时更稳"],["GPIO_InitTypeDef","ULN2003 的 4 个 IN 脚都是普通输出脚"],["GPIO_Init","GPIO 参数填好后交给它生效"],["GPIO_WriteBit","按 8 拍序列逐个写 IN1~IN4 电平"],["GPIO_SetBits","一拍里把要通的相一起置高"],["GPIO_ResetBits","一拍结束时把相复位"],["TIM_SetCounter","用定时器做步间延时比软件延时准"]],
"㊱ 外扩 SRAM（FSMC NE3，1 MB，当普通内存用）": [["㉘ TFT 并口彩屏 ILI9481（FSMC NE4 + A6 当 RS，320×480）","同一套 FSMC，NE3 给 SRAM、NE4 给屏"],["FSMC_NORSRAMInitTypeDef","SRAM 的时序/位宽/地址建立都靠它配"],["FSMC_NORSRAMInit","时序结构体填好后交给它生效"],["FSMC_NORSRAMCmd","配完使能对应片选（NE3）才真的可读写"],["RCC_AHB3PeriphClockCmd","FSMC 挂 AHB3，用之前先开时钟"],["GPIO_PinAFConfig","地址/数据/控制脚全部要配成 FSMC 复用"],["FSMC_NORSRAMTimingInitTypeDef","读写时序分两套，靠它分别填"]],
"㊲ 板1 系统 OLED（I2C1 PB8/PB9，0x3C，脏页刷新）": [["㉓ SSD1306 OLED 显示（I2C）","同一款屏的通用流程卡，板1 是封装好的 SYS_OLED_*"],["㉕ 一条 I2C 总线挂多个器件 + AT24C02 掉电保存参数","I2C1 上还挂着别的器件，地址不能冲突"],["③ I2C 读/写从机寄存器（接 SHT30 / BH1750 / CCS811 / BMP280 / OLED）","OLED 的通信层就在这一层"],["I2C_InitTypeDef","I2C1 参数靠它配"],["I2C_Init","I2C 参数填好后交给它生效"],["I2C_SendData","把命令字/显存数据发到 0x3C"],["I2C_CheckEvent","发完要等 EV 事件，否则丢字节"],["TIM_SetCounter","脏页刷新节奏靠定时器或任务节拍控制"]],
"㊳ 蜂鸣器 / 按键 / LED 基础外设（两板引脚与按键顺序都不同）": [["⑦ EXTI 外部中断（按键 / 传感器报警脚 / 唤醒）","按键的长按短按可以走中断或轮询，这张卡讲轮询"],["⑤ 定时器输出 PWM（调光 / 调速 / 蜂鸣器）","蜂鸣器想变调就得上 PWM，简单响就 GPIO 翻转"],["② 串口发送 + printf 重定向（调试必备）","这些基础外设的验证手段主要靠串口打印"],["GPIO_InitTypeDef","LED/蜂鸣器输出、按键输入都靠它配"],["GPIO_Init","GPIO 参数填好后交给它生效"],["GPIO_SetBits","点亮 LED、蜂鸣器置位"],["GPIO_ResetBits","熄灭 LED、蜂鸣器复位"],["GPIO_ToggleBits","LED 闪烁最省事的写法"],["GPIO_ReadInputDataBit","读按键电平判断按下"],["RCC_AHB1PeriphClockCmd","GPIO 端口时钟必须先开"],["TIM_SetCounter","软件延时消抖/闪烁节拍可以用它"]],
"🧭 ㉘ TFT 并口彩屏 ILI9481 引导（FSMC 并口，从黑屏到出字）": [["㉘ TFT 并口彩屏 ILI9481（FSMC NE4 + A6 当 RS，320×480）","这张向导对应的流程卡：完整代码与排错"],["㊱ 外扩 SRAM（FSMC NE3，1 MB，当普通内存用）","先懂 FSMC 再去理解屏的地址映射会轻松很多"],["㉙ XPT2046 电阻触摸（软件时序 + 校准参数保存）","屏跑通后的下一步"],["FSMC_NORSRAMInitTypeDef","并口屏的读写时序全靠它"],["FSMC_NORSRAMInit","时序结构体填好后交给它生效"],["RCC_AHB3PeriphClockCmd","FSMC 走 AHB3，先开时钟"],["GPIO_PinAFConfig","屏的 16 位数据线与控制线都要配成 FSMC 复用"]],
"🧭 ㉙ DS18B20 单总线温度引导（一根线读温度，从复位到算出摄氏度）": [["㉚ DS18B20 单总线温度（-55~+125℃，一根线挂多颗）","这张向导对应的流程卡"],["🧭 ㉔ DHT11 单总线温湿度引导（从一根线到一次可信的读数）","同样是单总线的另一个向导，可对照时序写法"],["GPIO_InitTypeDef","DQ 脚要输出/输入切换，靠它反复配"],["GPIO_Init","GPIO 参数填好后交给它生效"],["GPIO_ReadInputDataBit","读存在脉冲与每一位数据"],["GPIO_WriteBit","拉低 DQ 发起复位/写时隙"],["TIM_SetCounter","微秒级延时靠它加载计数值"]],

/* ===================== 本次升级新增的速查卡（协议/滤波/显示/通信/存储/执行器）===================== */
"自定义帧协议 sys_frame 速查（帧格式 / 字段 / CRC16 / 状态机）": [["CRC16-MODBUS 计算流程（逐字节查表 / 逐位移位）","帧里的校验字段就是它算出来的"],["㉒ sys_frame 自定义 20 字节数据帧协议（AA 55 帧头 + CRC16-MODBUS + 55 AA 帧尾）","这张卡对应的流程卡：完整代码与 9 态状态机"],["🧭 ㉒ sys_frame 自定义帧协议引导（从「串口只能收字节」到一条能自检的帧）","一步一步搭出收发两端"],["⑨ 数据帧组包 + CRC16-MODBUS 校验（已定案协议）","协议定案前的通用组帧卡"],["⑲ LoRa E22-400T22S 点对点无线通信（板1 采集 → 板2 网关）","组好的帧通过 LoRa 发出去"],["㉖ FreeRTOS 双板闭环联调总览（检测端采集 → LoRa → 网关端 → MQTT 云端 / 断网落盘）","两板联调时帧协议是核心链路"],["CRC_CalcCRC","STM32 自带的硬件 CRC 外设（多项式与 MODBUS 不同，别混用）"]],
"CRC16-MODBUS 计算流程（逐字节查表 / 逐位移位）": [["自定义帧协议 sys_frame 速查（帧格式 / 字段 / CRC16 / 状态机）","校验字段在帧里的位置与范围"],["㉑ Modbus RTU 从机 + RS485 半双工（把板子挂进工业现场有线链路）","Modbus 用的就是这套 CRC"],["🧭 ㉑ Modbus RTU 从机 + RS485 半双工引导（从接线到能被组态软件读）","CRC 算错会被组态软件直接丢包"],["⑨ 数据帧组包 + CRC16-MODBUS 校验（已定案协议）","组帧与校验的完整实现"],["CRC_CalcBlockCRC","硬件 CRC 外设的块计算（注意多项式与初值不同）"]],
"卡尔曼滤波一维实现流程（从抖动读数到可信值）": [["PID 闭环控制流程（位置式 / 增量式）","滤波后的值直接喂给 PID 当反馈"],["㉗ 数字滤波 + 卡尔曼 + PID 闭环（把抖动的读数变成可信的控制量）","这张卡对应的流程卡：从限幅到 PWM 的完整链"],["🧭 ㉗ 数字滤波 + 卡尔曼 + PID 闭环引导（从抖动的读数到能用的控制量）","一步一步整定 Q/R 与 Kp/Ki/Kd"],["㉝ MPU6050 六轴姿态（两板 API 不同，附统一封装）","姿态融合的数据源，抖动最明显"],["㉛ HC-SR04 超声波测距（TRIG 触发 + ECHO 回波宽度换算）","测距值抖动大，是滤波的典型场景"],["㉜ VL53L0X 激光测距（I2C，30~2000 mm，ToF）","另一种需要滤波的测距源"]],
"PID 闭环控制流程（位置式 / 增量式）": [["卡尔曼滤波一维实现流程（从抖动读数到可信值）","反馈量先滤波再进 PID 效果更好"],["⑤ 定时器输出 PWM（调光 / 调速 / 蜂鸣器）","PID 的输出最终变成 PWM 占空比"],["㉞ 直流电机 TB6612 + 编码器测速（速度环闭环）","速度环就是 PID 的典型落地"],["㉗ 数字滤波 + 卡尔曼 + PID 闭环（把抖动的读数变成可信的控制量）","这张卡对应的流程卡"],["🧭 ㉗ 数字滤波 + 卡尔曼 + PID 闭环引导（从抖动的读数到能用的控制量）","调参顺序与现象验证"],["TIM_SetCompare1","输出量转占空比靠它写比较寄存器"]],
"SSD1306 OLED 显存与刷新流程（页寻址 / 脏页刷新）": [["㉓ SSD1306 OLED 显示（I2C）","这张卡对应的流程卡：完整初始化序列与字库"],["🧭 ㉓ OLED 显示引导（从一堆散落函数到一块能跑起来的屏）","从黑屏到出字的完整过程"],["㊲ 板1 系统 OLED（I2C1 PB8/PB9，0x3C，脏页刷新）","板1 已验证的封装版本"],["㉘ TFT 并口彩屏 ILI9481（FSMC NE4 + A6 当 RS，320×480）","另一种显示方案（并口彩屏），可对照选型"],["常用器件 I2C 地址表（7 位地址）","OLED 默认 0x3C、可配 0x3D"],["I2C_Init","OLED 通信层的前提"]],
"Modbus RTU 从机速查（功能码 / 帧格式 / 3.5 字符间隔）": [["CRC16-MODBUS 计算流程（逐字节查表 / 逐位移位）","Modbus 每帧末尾的校验"],["㉑ Modbus RTU 从机 + RS485 半双工（把板子挂进工业现场有线链路）","这张卡对应的流程卡"],["🧭 ㉑ Modbus RTU 从机 + RS485 半双工引导（从接线到能被组态软件读）","从接线到被组态软件读走的完整过程"],["自定义帧协议 sys_frame 速查（帧格式 / 字段 / CRC16 / 状态机）","自定义协议与 Modbus 的取舍对照"],["① 串口 USART + DMA + 空闲中断接收（不定长帧）","Modbus 不定长帧的接收基础"],["TIM_GetCounter","量 3.5 字符间隔靠定时器计数"]],
"LoRa E22-400T22S 模式脚与透传速查": [["⑲ LoRa E22-400T22S 点对点无线通信（板1 采集 → 板2 网关）","这张卡对应的流程卡"],["🧭 ⑲ LoRa E22-400T22S 点对点链路引导（从接线到两板互通）","三根模式脚 + AUX 握手的完整过程"],["⑩ LoRa E22-400T22S 模块收发（AT 配置 + 透传）","AT 配置与透传的通用卡"],["自定义帧协议 sys_frame 速查（帧格式 / 字段 / CRC16 / 状态机）","无线链路上跑的帧格式"],["① 串口 USART + DMA + 空闲中断接收（不定长帧）","LoRa 模块就是挂在串口上的"],["GPIO_WriteBit","M0/M1 模式脚靠 GPIO 设电平"]],
"测距与温度传感器速查（HC-SR04 / VL53L0X / DS18B20 / DHT11）": [["㉛ HC-SR04 超声波测距（TRIG 触发 + ECHO 回波宽度换算）","超声波的完整流程卡"],["㉜ VL53L0X 激光测距（I2C，30~2000 mm，ToF）","激光 ToF 的完整流程卡"],["㉚ DS18B20 单总线温度（-55~+125℃，一根线挂多颗）","DS18B20 的完整流程卡"],["㉔ DHT11 单总线温湿度（检测端的第一路数据源）","DHT11 的完整流程卡"],["传感器寄存器与换算表","寄存器层面的换算公式汇总"],["⑥ 定时器输入捕获（测脉宽 / 频率 / 红外解码）","超声波 ECHO 宽度靠它量"]],
"电机与执行器速查（TB6612 / ULN2003 / 舵机 / 继电器）": [["㉞ 直流电机 TB6612 + 编码器测速（速度环闭环）","直流电机的完整流程卡"],["㉟ ULN2003 步进电机（28BYJ-48 类，8 拍 2038 步一圈）","步进电机的完整流程卡"],["⑤ 定时器输出 PWM（调光 / 调速 / 蜂鸣器）","所有执行器的控制量都靠 PWM"],["PID 闭环控制流程（位置式 / 增量式）","速度环/位置环的控制算法"],["TIM_OC2Init","PWM 通道配置"],["TIM_EncoderInterfaceConfig","直流电机测速的编码器接口"]],
"断网缓存与补传速查（离线落盘 / 原子提交 / 上线补传）": [["⑱ SPI Flash 断网缓存日志（掉线先存本地，上线再补传）","这张卡对应的流程卡"],["🧭 ⑱ SPI Flash 断网缓存日志引导（先存本地，上线再补传）","槽结构、原子提交、断电恢复的完整过程"],["W25QXX_Init","SPI Flash 初始化的前置条件"],["W25Q128 SPI Flash 常用指令表","读/写/擦除/状态轮询指令"],["FLASH 扇区表（F407）","板载 FLASH 的扇区范围（备选方案）"],["FLASH_EraseSector","用板载 FLASH 存缓存时的擦除函数"],["㉖ FreeRTOS 双板闭环联调总览（检测端采集 → LoRa → 网关端 → MQTT 云端 / 断网落盘）","缓存补传在整条链路里的位置"]],
"触摸屏 XPT2046 速查（软件 SPI / 采样 / 校准）": [["㉙ XPT2046 电阻触摸（软件时序 + 校准参数保存）","这张卡对应的流程卡：完整时序与校准参数保存"],["㉘ TFT 并口彩屏 ILI9481（FSMC NE4 + A6 当 RS，320×480）","触摸层贴在它上面"],["⑬ SPI 主机通信（W25QXX Flash / NRF24L01 / XPT2046 触摸）","SPI 时序基础"],["GPIO_WriteBit","软件 SPI 的时钟/数据脚靠它翻转"],["GPIO_ReadInputDataBit","读 MISO 与 PEN 电平"],["EXTI_Init","PEN 引脚用中断方式感知按下"]],
/* ================= 2026-10 新增：OTA 升级两张卡（流程卡 × 引导向导 × 库函数）================= */
"㊴ OTA 固件升级（应用区 + 下载区 + CRC 校验 + 断电安全）": [["🧭 ㉚ OTA 在线升级引导（板1 经 LoRa、板2 经 ESP8266/MQTT 下发固件）","配套引导：分区规划 → 下载区落盘 → 校验 → Bootloader 搬运 → 跳转，11 步"],["OTA 分区与标志位表（板1 ZE 512KB / 板2 ZG 1MB）","四段区域的起止地址与硬约束，定分区先看它"],["FLASH_ProgramWord","下载区与运行区的写入都靠它（写前必须先擦）"],["FLASH_EraseSector","按扇区擦除：F4 只能整扇区擦，不能按字节擦"],["CRC_CalcBlockCRC","校验新固件用的硬件 CRC（注意长度参数是字数）"],["NVIC_SetVectorTable","跳转前必须把向量表指到 App 基址，否则一进中断就跑飞"],["🧭 板载 FLASH 扇区配置与参数保存引导（ZE 512KB / ZG 1MB）","升级流程里那些「不许碰」的参数区地址来自这里"]],
"🧭 ㉚ OTA 在线升级引导（板1 经 LoRa、板2 经 ESP8266/MQTT 下发固件）": [["㊴ OTA 固件升级（应用区 + 下载区 + CRC 校验 + 断电安全）","配套流程卡：Bootloader 与设备侧的完整代码"],["OTA 分区与标志位表（板1 ZE 512KB / 板2 ZG 1MB）","第一步就是照着它定分区，两块板扇区大小不同"],["FLASH_ProgramHalfWord","固件长度不是 4 字节整数倍时，尾部用它补齐"],["FLASH_ClearFlag","擦写前先清标志，否则「写失败却报成功」"],["FLASH_WaitForLastOperation","擦大扇区可能要秒级，等它做完再往下走"],["SYSCFG_MemoryRemapConfig","内置系统 Bootloader 启动方式的重映射（备选方案）"],["⑨ 数据帧组包 + CRC16-MODBUS 校验（已定案协议）","升级数据块沿用同一套帧格式与帧校验"]],

};
