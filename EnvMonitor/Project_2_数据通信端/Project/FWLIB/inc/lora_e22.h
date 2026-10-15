#ifndef __LORA_E22_H
#define __LORA_E22_H

/*
 *  lora_e22.h : 亿佰特 E22-400T22S LoRa 模块驱动头文件
 *  模块型号 : 亿佰特 E22-400T22S，433MHz 频段，22dBm，纯串口 AT，与 SPI 无关
 *  通信方式 : 模块与 MCU 之间只有一路 UART，透传模式下发什么收什么，模块本身
 *             没有包格式，空中协议由应用层定义。本项目帧格式 = 20 字节：
 *             0xAA55 + 14 + CRC16-MODBUS + 55AA，见《检测数据端设计.md》§5。
 *
 *  ---------------------------------------------------------------
 *  上电前注意事项
 *    1) 先接天线再上电。不带天线发射会把功率反射回功放，轻则距离骤减，重则
 *       损坏模块。
 *    2) M0/M1 不能悬空。芯片内部无上拉，悬空时模块随机进或不进配置模式，
 *       现象是 AT 有时通有时不通。本驱动初始化时把两脚推挽输出到透传(00)。
 *    3) 发射瞬时电流 100~120mA。供电需就近去耦：100µF 电解 + 100nF 陶瓷并到
 *       模块 VCC/GND，并与 MCU 共地。
 *    4) AUX 可以不接，但建议接。它同时指示模块空闲（可发下一包）与上电初始化
 *       完成。不接时本驱动退化为固定延时等待（见 .c 里 lora_wait_idle）。
 *
 *  ---------------------------------------------------------------
 *  工作模式（M1/M0 两个脚的电平）
 *    模式名        M1  M0   功能                              适用场合
 *    -------       --  --   ------------------------------    ----------------------
 *    透传模式       0   0   串口收到什么就发什么（默认）      正常业务数据
 *    唤醒模式(WOR)  0   1   发前自动加前导码唤醒对端接收方    对端开 WOR 省电时
 *    配置模式       1   0   AT 指令配置参数（串口=115200 8N1） 上电配置、改地址信道
 *    深睡模式       1   1   最低功耗，配置参数保持            电池供电休眠
 *
 *    切模式有延时。手册时序：从透传/唤醒切进配置/休眠要等当前包发完（典型
 *    <5ms）；从配置/休眠切回透传要重新初始化射频（典型 <15ms，最坏 1s）。
 *    本驱动统一用 AUX 等，没接 AUX 时用固定 2ms 兜底。切模式后立刻发指令会丢。
 *
 *  ---------------------------------------------------------------
 *  本板接线（板1 检测端 / 板2 数据通信端）
 *    信号    板1（检测端）        板2（数据通信端）      备注
 *    ------  ------------------  --------------------  --------------------------
 *    TXD     PB11 (USART3_RX)     PC6  (USART6_TX)      模块 TXD → MCU RX
 *    RXD     PB10 (USART3_TX)     PC7  (USART6_RX)      模块 RXD ← MCU TX
 *    M0      PB0                  PE5                   推挽输出，本驱动接管
 *    M1      PB1                  PE6                   推挽输出，本驱动接管
 *    AUX     PB2                  PE7                   输入上拉，建议接
 *    VCC     3.3V（就近去耦）      3.3V                  同左
 *    GND     GND                  GND                   必须共地
 *
 *    板1 用原 ESP8266 座（PB10/PB11 = USART3）。板2 走 USART6（PC6/PC7，从 P1
 *    双排排针引出），其余串口已占满：USART1=调试(CH340C，需插两枚跳线帽
 *    PA9T↔URXD / PA10R↔UTXD)、USART2=跳线 P6 选 SP3485(RS485)/SP3232(RS232)、
 *    USART3=跳线 P10 选 ESP8266/3P 端子。M0/M1/AUX 脚位已查原理图确认空闲，
 *    换板或换接线只改下面这几个宏。
 *
 *  ---------------------------------------------------------------
 *  用不到的引脚怎么屏蔽：把该脚的 PORT 与 PIN 两行宏一起注释掉。
 *    不接 AUX   : 注释掉 SYS_LORA_AUX_PORT / SYS_LORA_AUX_PIN，驱动不再等
 *                 AUX，改用固定 2ms 延时兜底
 *    不接 M0/M1 : 同样注释掉；此时模块必须由硬件拨到透传(00)，否则 SendAT /
 *                 ReadConfig / SetBaud 都不可用（它们都靠切模式）
 *    不能把它们改成 0。SPL 的 GPIO 宏（GPIOB、GPIO_Pin_0）是带强制类型转换的
 *    表达式（((GPIO_TypeDef *) GPIOB_BASE)、((uint16_t)0x0001)），预处理器无法
 *    求值，出现在 #if 的算术表达式里会报 #29 / #59 / #18。驱动里用的是
 *    #if defined(宏名)，只看有没有定义。
 *
 *  ---------------------------------------------------------------
 *  AT 指令速查（配置模式下，全部以 \r\n 结尾）
 *    AT                    测试连通，回 OK
 *    AT+VER                查询固件版本
 *    AT+RESET              复位模块
 *    AT+ADDR=?             查询本机地址（0~65535）
 *    AT+ADDR=1234          设置本机地址
 *    AT+NETID=18           设置网络 ID（同 NETID + 同信道 + 同地址才能互通）
 *    AT+REG0=18,0,0,433000000,22,1,1,0,0,1,0,0   寄存器组
 *        字段顺序 : 网络ID, 本机地址, 信道, 频率, 发射功率, 带宽, 扩频因子,
 *                   编码率, 前导码, 是否固定传输, 唤醒时间, 保留
 *        一般只改 NETID 和信道，其余保持出厂值。
 *    AT+UART=115200,8,1,NON   设置串口（LORA_E22_SetBaud 用的就是它）
 *    AT+UART               查询串口参数
 *    AT+CFG                打印当前全部配置
 *    AT+WOR                查询/设置唤醒模式参数
 *
 *    改完必须 AT+RESET 或断电重启才在透传模式下生效。部分固件 AT+UART 会立即
 *    生效并把串口切到新波特率，LORA_E22_SetBaud 已处理本端跟随切换。
 */

#include "sys_usart.h"
#include "stm32f4xx.h"


/* 1. 用户配置区（换板/换线只改这里） */
/* -------------------- 串口与波特率 -------------------- */
/* 本板（板2 数据通信端）= USART6（PC6/PC7，从 P1 双排排针引出）。
 * 板2 的 USART1=调试(CH340C)、USART2=跳线 P6 选 RS485/RS232、USART3=跳线 P10
 * 选 ESP8266/3P 端子，三路已占满，只剩 USART6。
 * 板1（检测端）那一行是 SYS_USART_3（原 ESP8266 座 PB10/PB11），两份 .h 只差
 * 这一处和下面 M0/M1/AUX 的脚位。
 * 换到别的串口时该路必须在 SysUsartId_t 里存在（板1/板2 的 sys_usart 都有
 * 6 路，见 sys_usart.h）。 */
#define SYS_LORA_UART            SYS_USART_6

/* 模块出厂默认 9600 8N1，本端必须与模块一致。
 * 改这里只影响本端，模块里的波特率要用 LORA_E22_SetBaud() 改。 */
#define SYS_LORA_BAUD            9600U


/* -------------------- 控制引脚 -------------------- */
/* M0/M1 是模块的输入，MCU 侧推挽输出；AUX 是模块的输出，MCU 侧输入。
 * 下面这组是板2 的脚位（PE5/PE6/PE7，已核实为本板空闲），板1 那份 .h 用的是
 * PB0/PB1/PB2。某个脚没接就把它的 PORT/PIN 两行一起注释掉（见文件头说明）。
 *
 * 不用 PA4/PA5：板2（普中-天马 F407ZG）原理图与配套手册 Project\README.md 写明
 * 这两脚是本板模拟跳线 J8 的公共端（R_ADC / STM_ADC / P_TOUCH / STM_DAC /
 * TAD1 五选一，见 README.md:177），同一时刻只允许一个用途；它们又分别被库里的
 * SYS_DAC_OUT/SYS_DAC1（PA4）与 SENSOR_ADC/SYS_ADC_JMP/SYS_DAC2（PA5）声明
 * 占用，属于电气争用。不用 PA6：手册里没有它空闲的书面依据（README.md:1551 只
 * 在 TIM3 示例里提过 CH1=PA6）。
 * PE5/PE6/PE7 的依据：README.md:2497「示例选 PE5（板上空闲脚）」、
 * README.md:2543「示例改用空闲的 PE6/PE7」，以及工具 本地工具\仓库巡检\
 * pin_audit.py 扫完两板全部 FWLIB 头文件后确认这三脚在库内无人认领（结果见同级
 * _pin_audit.txt）。
 * 三脚都是普通 GPIO，无复用功能约束；若本板 PE5~PE7 已被占用，换成任意空闲脚
 * 即可，改完请重跑 python 本地工具\仓库巡检\pin_audit.py 复核。 */
#define SYS_LORA_M0_PORT          GPIOE
#define SYS_LORA_M0_PIN           GPIO_Pin_5
#define SYS_LORA_M1_PORT          GPIOE
#define SYS_LORA_M1_PIN           GPIO_Pin_6
#define SYS_LORA_AUX_PORT         GPIOE
#define SYS_LORA_AUX_PIN          GPIO_Pin_7


/* -------------------- 超时与帧边界 -------------------- */
/* AUX 等待上限：切模式/发完一包最多等这么久 */
#define SYS_LORA_AUX_TIMEOUT_MS   1000U

/* 一条 AT 指令的回复等待上限 */
#define SYS_LORA_AT_TIMEOUT_MS    500U

/* 帧边界判据（透传模式）
 * E22 透传模式不输出包开始/包结束标记，只能靠字节流里的停顿分段。
 * 本项目一帧 20 字节，9600bps 下连续 21ms 发完：
 *   帧内字节间隔 ≈ 0.2ms（1 字节约 1.04ms）
 *   帧与帧之间 ≈ 1s（采集周期）
 * 取 10ms 作阈值：可容忍发送方任务被打断几毫秒，又远小于帧间隔。
 * 若把 SYS_LORA_BAUD 降到 1200，1 字节约 8.3ms，本值必须加大（建议 ≥ 30ms），
 * 否则一帧会被切成好几段。 */
#define SYS_LORA_FRAME_GAP_MS     10U

/* 收帧缓冲（.c 内部的兜底缓冲，调用方给了缓冲时用不到） */
#define SYS_LORA_RX_BUF_SIZE      256U


/* 2. 状态码 */
/* 返回码约定 : 0 = 成功；非 0 都是失败，可直接 switch 分类处理。
 * 暂无数据也占一个码，不用超时含混表示没数据。 */
#define LORA_E22_OK               0U   /* 成功 */
#define LORA_E22_ERR_AUX          1U   /* AUX 一直忙：模块没上电/没接好/M0M1 错 */
#define LORA_E22_ERR_NO_MODULE    2U   /* AT 无回应：没接 M0M1 / 波特率不符 / TXRX 反 */
#define LORA_E22_ERR_AT_ERROR     3U   /* 模块回了 ERROR：指令本身有问题 */
#define LORA_E22_ERR_PARAM        4U   /* 入参非法（空指针/零长度/不支持的波特率） */
#define LORA_E22_ERR_OVERFLOW     5U   /* 调用方缓冲装不下，本帧已丢弃 */
#define LORA_E22_ERR_NO_DATA      6U   /* 暂无完整帧，不是错误，下轮再来 */


/* 3. 工作模式 */
typedef enum {
    LORA_E22_MODE_TRANSPARENT = 0,   /* M1M0=00 透传（正常业务用这个） */
    LORA_E22_MODE_WOR         = 1,   /* M1M0=01 唤醒模式 */
    LORA_E22_MODE_CONFIG      = 2,   /* M1M0=10 配置模式（AT 指令） */
    LORA_E22_MODE_SLEEP       = 3    /* M1M0=11 深睡模式 */
} LoraE22Mode_t;


/* 4. 接口 */

/* 初始化 : 配 M0/M1/AUX 引脚 → 进透传模式 → 开中断接收 → 等模块就绪
 * 返回 : LORA_E22_OK / LORA_E22_ERR_AUX
 * 说明 : 内部会调用 SYS_USART_InitRxIT()，不要再对该串口做别的 DMA 配置；
 *        重复调用本函数是安全的（重开会重新清缓冲）。 */
uint8_t LORA_E22_Init(void);

/* 切换工作模式（会自动等 AUX 空闲）
 * 返回 : LORA_E22_OK / LORA_E22_ERR_PARAM / LORA_E22_ERR_AUX
 * 说明 : 业务收发前请确保在 LORA_E22_MODE_TRANSPARENT。 */
uint8_t LORA_E22_SetMode(LoraE22Mode_t mode);

/* 模块忙不忙（读 AUX；没接 AUX 时恒返回 0）
 * 返回 : 1 = 忙（不能灌数据）/ 0 = 空闲 */
uint8_t LORA_E22_IsBusy(void);

/* 等模块空闲
 * 返回 : 1 = 已空闲（或没接 AUX，用固定延时兜底）/ 0 = 超时仍忙 */
uint8_t LORA_E22_WaitReady(uint32_t timeout_ms);

/* 发一包（透传模式）。内部会先等 AUX 空闲，再阻塞发完
 * 返回 : LORA_E22_OK / LORA_E22_ERR_PARAM
 * 说明 : 驱动无锁，不要在多任务里并发调用，建议固定由 CommTask 调用。 */
uint8_t LORA_E22_Send(const uint8_t *data, uint16_t len);

/* 收一帧（透传模式，靠字节间间隔切帧）
 * 参数 : buf/max = 调用方缓冲；out_len = 出参，收到几个字节
 * 返回 : LORA_E22_OK        收到完整一帧，*out_len 有效
 *        LORA_E22_ERR_NO_DATA 暂时没有（正常，下轮再来）
 *        LORA_E22_ERR_OVERFLOW 这一帧比 max 长，已丢弃（状态已自动复位）
 *        LORA_E22_ERR_PARAM   入参非法
 * 说明 : 1) 必须周期性调用（建议 10~50ms 一次），调用太稀会把两帧粘成一帧。
 *        2) 缓冲建议 ≥ 32 字节（20 字节帧 + 余量）。 */
uint8_t LORA_E22_Recv(uint8_t *buf, uint16_t max, uint16_t *out_len);

/* 清接收状态（丢掉半截帧）。切模式、改波特率之后建议调一次 */
void LORA_E22_Flush(void);

/* 发一条 AT 指令（自动切配置模式 → 发 → 等 OK/ERROR → 切回透传）
 * 参数 : cmd = 不含结尾 \r\n 的指令串，如 "AT+ADDR=1234"
 * 返回 : LORA_E22_OK / LORA_E22_ERR_NO_MODULE / LORA_E22_ERR_AT_ERROR
 *        / LORA_E22_ERR_PARAM / LORA_E22_ERR_AUX
 * 本函数执行期间模块不能收发业务数据（会切走模式），只在开机配置或人为改参数
 * 时调用，不要放进数据通路。 */
uint8_t LORA_E22_SendAT(const char *cmd);

/* 把模块当前配置读成一行文本（AT+VER / AT+ADDR / AT+NETID / AT+REG0）
 * 参数 : out/out_size = 输出文本缓冲（建议 ≥ 96 字节）
 * 返回 : LORA_E22_OK / LORA_E22_ERR_NO_MODULE / LORA_E22_ERR_PARAM / LORA_E22_ERR_AUX
 * 用途 : 开机打印一次，现场排障时核对地址/网络ID/信道。 */
uint8_t LORA_E22_ReadConfig(char *out, uint16_t out_size);

/* 改模块串口波特率（AT+UART=<baud>,8,1,NON），并把本端跟着切过去
 * 返回 : LORA_E22_OK / LORA_E22_ERR_PARAM（只支持 1200/2400/4800/9600/19200
 *        /38400/57600/115200）/ LORA_E22_ERR_NO_MODULE / LORA_E22_ERR_AT_ERROR
 * 改完请把 SYS_LORA_BAUD 也改成同一值，否则下次上电两边波特率不一致。 */
uint8_t LORA_E22_SetBaud(uint32_t baud);

/* 复位模块（AT+RESET）并等它重新就绪
 * 返回 : LORA_E22_OK / LORA_E22_ERR_NO_MODULE / LORA_E22_ERR_AUX */
uint8_t LORA_E22_Reset(void);

#endif /* __LORA_E22_H */
